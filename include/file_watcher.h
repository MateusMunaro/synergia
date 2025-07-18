#ifndef FILE_WATCHER_H
#define FILE_WATCHER_H

#include <sys/stat.h>
#include <pthread.h>

#define MAX_PATH_LEN 512
#define MAX_HASH_LEN 32

typedef enum {
    FILE_CREATED,
    FILE_MODIFIED,
    FILE_DELETED
} FileChangeType;

typedef struct {
    char filepath[MAX_PATH_LEN];
    time_t last_modified;
    off_t size;
    char hash[MAX_HASH_LEN];
} WatchedFile;

typedef struct FileWatcher FileWatcher;

typedef void (*file_change_callback)(const char* filepath, FileChangeType type, void* user_data);

// Criar e destruir watcher
FileWatcher* file_watcher_create(const char* root_path);
void file_watcher_destroy(FileWatcher* watcher);

// Controlar monitoramento
int file_watcher_start(FileWatcher* watcher, file_change_callback callback, void* user_data);
void file_watcher_stop(FileWatcher* watcher);

// Obter informações
int file_watcher_get_files(FileWatcher* watcher, WatchedFile** files, int* count);
int file_watcher_poll_changes(FileWatcher* watcher);

#endif // FILE_WATCHER_H