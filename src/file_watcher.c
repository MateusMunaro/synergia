//
// Created by HP on 17/07/2025.
//
#include "file_watcher.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <pthread.h>

#ifdef __linux__
#include <sys/inotify.h>
#include <sys/select.h>
#endif

#define MAX_EVENTS 1024
#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (MAX_EVENTS * (EVENT_SIZE + 16))

struct FileWatcher {
    char root_path[MAX_PATH_LEN];
    WatchedFile* files;
    int file_count;
    int file_capacity;
    int inotify_fd;
    int* watch_descriptors;
    int wd_count;
    int wd_capacity;
    pthread_t watch_thread;
    volatile int running;
    file_change_callback callback;
    void* user_data;
    pthread_mutex_t mutex;
};

// Funções auxiliares
static int should_ignore_file(const char* filename) {
    // Ignorar arquivos e diretórios do sistema
    if (filename[0] == '.') {
        // Permitir apenas .myvc
        if (strcmp(filename, ".myvc") != 0) {
            return 1;
        }
    }

    // Ignorar arquivos temporários
    size_t len = strlen(filename);
    if (len > 4 && strcmp(filename + len - 4, ".tmp") == 0) return 1;
    if (len > 4 && strcmp(filename + len - 4, ".swp") == 0) return 1;
    if (len > 1 && filename[len - 1] == '~') return 1;

    return 0;
}

static int is_text_file(const char* filepath) {
    // Verificar extensões de texto conhecidas
    const char* ext = strrchr(filepath, '.');
    if (!ext) return 0;

    const char* text_extensions[] = {
        ".c", ".h", ".cpp", ".hpp", ".cc", ".hh",
        ".py", ".js", ".ts", ".html", ".css", ".json",
        ".txt", ".md", ".xml", ".yaml", ".yml",
        ".java", ".go", ".rs", ".rb", ".php",
        ".sh", ".bash", ".zsh", ".fish",
        NULL
    };

    for (int i = 0; text_extensions[i]; i++) {
        if (strcasecmp(ext, text_extensions[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

static char* get_file_hash(const char* filepath) {
    static char hash[32];
    FILE* file = fopen(filepath, "rb");
    if (!file) return NULL;

    // Hash simples baseado em tamanho e timestamp
    struct stat st;
    if (fstat(fileno(file), &st) == 0) {
        snprintf(hash, sizeof(hash), "%ld_%ld",
                 (long)st.st_size, (long)st.st_mtime);
    }

    fclose(file);
    return hash;
}

static WatchedFile* find_watched_file(FileWatcher* watcher, const char* filepath) {
    for (int i = 0; i < watcher->file_count; i++) {
        if (strcmp(watcher->files[i].filepath, filepath) == 0) {
            return &watcher->files[i];
        }
    }
    return NULL;
}

static int add_watched_file(FileWatcher* watcher, const char* filepath) {
    if (find_watched_file(watcher, filepath)) {
        return 0; // Já existe
    }

    // Expandir array se necessário
    if (watcher->file_count >= watcher->file_capacity) {
        watcher->file_capacity *= 2;
        watcher->files = (WatchedFile*)safe_realloc(
            watcher->files,
            watcher->file_capacity * sizeof(WatchedFile)
        );
    }

    WatchedFile* file = &watcher->files[watcher->file_count++];
    strncpy(file->filepath, filepath, MAX_PATH_LEN - 1);
    file->filepath[MAX_PATH_LEN - 1] = '\0';

    struct stat st;
    if (stat(filepath, &st) == 0) {
        file->last_modified = st.st_mtime;
        file->size = st.st_size;
    }

    char* hash = get_file_hash(filepath);
    if (hash) {
        strncpy(file->hash, hash, sizeof(file->hash) - 1);
        file->hash[sizeof(file->hash) - 1] = '\0';
    }

    log_message(LOG_DEBUG, "Added file to watch: %s", filepath);
    return 1;
}

static int remove_watched_file(FileWatcher* watcher, const char* filepath) {
    for (int i = 0; i < watcher->file_count; i++) {
        if (strcmp(watcher->files[i].filepath, filepath) == 0) {
            // Mover elementos restantes
            for (int j = i; j < watcher->file_count - 1; j++) {
                watcher->files[j] = watcher->files[j + 1];
            }
            watcher->file_count--;
            log_message(LOG_DEBUG, "Removed file from watch: %s", filepath);
            return 1;
        }
    }
    return 0;
}

static int scan_directory(FileWatcher* watcher, const char* dir_path) {
    DIR* dir = opendir(dir_path);
    if (!dir) return -1;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (should_ignore_file(entry->d_name)) {
            continue;
        }

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            // Recursivamente escanear subdiretórios
            scan_directory(watcher, full_path);
        } else if (S_ISREG(st.st_mode) && is_text_file(full_path)) {
            add_watched_file(watcher, full_path);
        }
    }

    closedir(dir);
    return 0;
}

#ifdef __linux__
static void handle_inotify_event(FileWatcher* watcher, struct inotify_event* event) {
    if (event->len == 0) return;

    char full_path[MAX_PATH_LEN];
    snprintf(full_path, sizeof(full_path), "%s/%s", watcher->root_path, event->name);

    pthread_mutex_lock(&watcher->mutex);

    if (event->mask & IN_CREATE) {
        if (is_text_file(full_path)) {
            add_watched_file(watcher, full_path);
            if (watcher->callback) {
                watcher->callback(full_path, FILE_CREATED, watcher->user_data);
            }
        }
    }

    if (event->mask & IN_DELETE) {
        remove_watched_file(watcher, full_path);
        if (watcher->callback) {
            watcher->callback(full_path, FILE_DELETED, watcher->user_data);
        }
    }

    if (event->mask & IN_MODIFY) {
        WatchedFile* file = find_watched_file(watcher, full_path);
        if (file) {
            char* new_hash = get_file_hash(full_path);
            if (new_hash && strcmp(file->hash, new_hash) != 0) {
                strncpy(file->hash, new_hash, sizeof(file->hash) - 1);
                file->hash[sizeof(file->hash) - 1] = '\0';

                struct stat st;
                if (stat(full_path, &st) == 0) {
                    file->last_modified = st.st_mtime;
                    file->size = st.st_size;
                }

                if (watcher->callback) {
                    watcher->callback(full_path, FILE_MODIFIED, watcher->user_data);
                }
            }
        }
    }

    if (event->mask & IN_MOVED_FROM) {
        remove_watched_file(watcher, full_path);
        if (watcher->callback) {
            watcher->callback(full_path, FILE_DELETED, watcher->user_data);
        }
    }

    if (event->mask & IN_MOVED_TO) {
        if (is_text_file(full_path)) {
            add_watched_file(watcher, full_path);
            if (watcher->callback) {
                watcher->callback(full_path, FILE_CREATED, watcher->user_data);
            }
        }
    }

    pthread_mutex_unlock(&watcher->mutex);
}

static void* watch_thread_func(void* arg) {
    FileWatcher* watcher = (FileWatcher*)arg;
    char buffer[BUF_LEN];

    while (watcher->running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(watcher->inotify_fd, &read_fds);

        struct timeval timeout = {1, 0}; // 1 segundo
        int ready = select(watcher->inotify_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ready < 0) {
            if (errno != EINTR) {
                log_message(LOG_ERROR, "Select error: %s", strerror(errno));
                break;
            }
            continue;
        }

        if (ready == 0) continue; // Timeout

        if (FD_ISSET(watcher->inotify_fd, &read_fds)) {
            int length = read(watcher->inotify_fd, buffer, BUF_LEN);
            if (length < 0) {
                log_message(LOG_ERROR, "Read error: %s", strerror(errno));
                continue;
            }

            int i = 0;
            while (i < length) {
                struct inotify_event* event = (struct inotify_event*)&buffer[i];
                handle_inotify_event(watcher, event);
                i += EVENT_SIZE + event->len;
            }
        }
    }

    return NULL;
}
#endif

FileWatcher* file_watcher_create(const char* root_path) {
    if (!root_path) return NULL;

    FileWatcher* watcher = (FileWatcher*)safe_malloc(sizeof(FileWatcher));
    memset(watcher, 0, sizeof(FileWatcher));

    strncpy(watcher->root_path, root_path, MAX_PATH_LEN - 1);
    watcher->root_path[MAX_PATH_LEN - 1] = '\0';

    watcher->file_capacity = 100;
    watcher->files = (WatchedFile*)safe_malloc(watcher->file_capacity * sizeof(WatchedFile));

    watcher->wd_capacity = 100;
    watcher->watch_descriptors = (int*)safe_malloc(watcher->wd_capacity * sizeof(int));

    pthread_mutex_init(&watcher->mutex, NULL);

#ifdef __linux__
    watcher->inotify_fd = inotify_init();
    if (watcher->inotify_fd < 0) {
        log_message(LOG_ERROR, "Failed to initialize inotify: %s", strerror(errno));
        file_watcher_destroy(watcher);
        return NULL;
    }
#endif

    log_message(LOG_INFO, "Created file watcher for: %s", root_path);
    return watcher;
}

void file_watcher_destroy(FileWatcher* watcher) {
    if (!watcher) return;

    file_watcher_stop(watcher);

#ifdef __linux__
    if (watcher->inotify_fd >= 0) {
        close(watcher->inotify_fd);
    }
#endif

    pthread_mutex_destroy(&watcher->mutex);
    safe_free(watcher->files);
    safe_free(watcher->watch_descriptors);
    safe_free(watcher);
}

int file_watcher_start(FileWatcher* watcher, file_change_callback callback, void* user_data) {
    if (!watcher || !callback) return -1;

    watcher->callback = callback;
    watcher->user_data = user_data;

    // Escanear diretório inicial
    scan_directory(watcher, watcher->root_path);

#ifdef __linux__
    // Adicionar watch para o diretório raiz
    int wd = inotify_add_watch(watcher->inotify_fd, watcher->root_path,
                               IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd < 0) {
        log_message(LOG_ERROR, "Failed to add inotify watch: %s", strerror(errno));
        return -1;
    }

    watcher->watch_descriptors[watcher->wd_count++] = wd;

    // Iniciar thread de monitoramento
    watcher->running = 1;
    if (pthread_create(&watcher->watch_thread, NULL, watch_thread_func, watcher) != 0) {
        log_message(LOG_ERROR, "Failed to create watch thread");
        watcher->running = 0;
        return -1;
    }
#endif

    log_message(LOG_INFO, "Started file watcher, monitoring %d files", watcher->file_count);
    return 0;
}

void file_watcher_stop(FileWatcher* watcher) {
    if (!watcher) return;

    watcher->running = 0;

#ifdef __linux__
    if (watcher->watch_thread) {
        pthread_join(watcher->watch_thread, NULL);
        watcher->watch_thread = 0;
    }

    // Remover watches
    for (int i = 0; i < watcher->wd_count; i++) {
        inotify_rm_watch(watcher->inotify_fd, watcher->watch_descriptors[i]);
    }
    watcher->wd_count = 0;
#endif

    log_message(LOG_INFO, "Stopped file watcher");
}

int file_watcher_get_files(FileWatcher* watcher, WatchedFile** files, int* count) {
    if (!watcher || !files || !count) return -1;

    pthread_mutex_lock(&watcher->mutex);

    *files = watcher->files;
    *count = watcher->file_count;

    pthread_mutex_unlock(&watcher->mutex);
    return 0;
}

int file_watcher_poll_changes(FileWatcher* watcher) {
    if (!watcher) return -1;

    int changes = 0;

    pthread_mutex_lock(&watcher->mutex);

    // Verificar mudanças manuais nos arquivos (fallback para sistemas sem inotify)
    for (int i = 0; i < watcher->file_count; i++) {
        WatchedFile* file = &watcher->files[i];

        struct stat st;
        if (stat(file->filepath, &st) != 0) {
            // Arquivo foi deletado
            if (watcher->callback) {
                watcher->callback(file->filepath, FILE_DELETED, watcher->user_data);
            }

            // Remover da lista
            for (int j = i; j < watcher->file_count - 1; j++) {
                watcher->files[j] = watcher->files[j + 1];
            }
            watcher->file_count--;
            i--; // Ajustar índice
            changes++;
            continue;
        }

        // Verificar se foi modificado
        if (st.st_mtime != file->last_modified || st.st_size != file->size) {
            char* new_hash = get_file_hash(file->filepath);
            if (new_hash && strcmp(file->hash, new_hash) != 0) {
                strncpy(file->hash, new_hash, sizeof(file->hash) - 1);
                file->hash[sizeof(file->hash) - 1] = '\0';

                file->last_modified = st.st_mtime;
                file->size = st.st_size;

                if (watcher->callback) {
                    watcher->callback(file->filepath, FILE_MODIFIED, watcher->user_data);
                }
                changes++;
            }
        }
    }

    pthread_mutex_unlock(&watcher->mutex);
    return changes;
}