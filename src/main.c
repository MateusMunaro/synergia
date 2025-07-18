#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "versioning.h"
#include "log.h"
#include "websocket_client.h"
#include "file_watcher.h"
#include "utils.h"

#define VERSION "0.1.3"
#define DEFAULT_SERVER "localhost"
#define DEFAULT_PORT 8080

// Variáveis globais para gerenciar o estado do programa
static volatile int running = 1;
static VersioningManager* vm = NULL;
static LogManager* lm = NULL;
static WebSocketClient* ws = NULL;
static FileWatcher* fw = NULL;
static pthread_mutex_t operations_mutex = PTHREAD_MUTEX_INITIALIZER;

// Handler para sinais
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        log_message(LOG_INFO, "Received signal %d, shutting down...", sig);
        running = 0;
    }
}

// Callback para processar operações recebidas do servidor
void handle_remote_operation(const Operation* op, void* user_data) {
    (void)user_data;

    log_message(LOG_INFO, "Received remote operation from %s: %s at line %d, col %d",
                op->author, op->op_type, op->line, op->column);

    pthread_mutex_lock(&operations_mutex);

    // Salvar operação no log local
    if (lm) {
        log_save_operation(lm, op);
    }

    // Aplicar operação ao arquivo local se não for nossa própria operação
    const char* current_user = getenv("USER");
    if (!current_user) current_user = "unknown";

    if (strcmp(op->author, current_user) != 0) {
        // TODO: Implementar aplicação de operação remota
        log_message(LOG_DEBUG, "Would apply remote operation to file");
    }

    pthread_mutex_unlock(&operations_mutex);
}

// Callback para mudanças de arquivo detectadas pelo file watcher
void handle_file_change(const char* filepath, FileChangeType type, void* user_data) {
    (void)user_data;

    const char* type_str = "";
    switch (type) {
        case FILE_CREATED: type_str = "created"; break;
        case FILE_MODIFIED: type_str = "modified"; break;
        case FILE_DELETED: type_str = "deleted"; break;
    }

    log_message(LOG_INFO, "File %s: %s", type_str, filepath);

    pthread_mutex_lock(&operations_mutex);

    if (type == FILE_CREATED) {
        // Adicionar arquivo ao controle de versão
        if (vm) {
            versioning_add_file(vm, filepath);
        }

        // Criar operação de criação
        const char* current_user = getenv("USER");
        if (!current_user) current_user = "unknown";

        size_t content_size;
        char* content = file_read_all(filepath, &content_size);
        if (content) {
            Operation* op = operation_create("create", 0, 0, content, current_user);

            // Salvar no log
            if (lm) {
                log_save_operation(lm, op);
            }

            // Enviar para servidor
            if (ws && ws_get_state(ws) == WS_CONNECTED) {
                ws_send_operation(ws, op);
            }

            operation_destroy(op);
            safe_free(content);
        }
    }
    else if (type == FILE_MODIFIED) {
        // Detectar mudanças específicas
        if (vm) {
            int op_count;
            Operation** ops = versioning_detect_changes(vm, filepath, &op_count);

            if (ops && op_count > 0) {
                log_message(LOG_INFO, "Detected %d changes in %s", op_count, filepath);

                // Processar cada operação
                for (int i = 0; i < op_count; i++) {
                    Operation* op = ops[i];

                    // Salvar no log
                    if (lm) {
                        log_save_operation(lm, op);
                    }

                    // Enviar para servidor
                    if (ws && ws_get_state(ws) == WS_CONNECTED) {
                        ws_send_operation(ws, op);
                    }

                    operation_destroy(op);
                }

                safe_free(ops);
            }
        }
    }
    else if (type == FILE_DELETED) {
        // Remover arquivo do controle de versão
        if (vm) {
            versioning_remove_file(vm, filepath);
        }

        // Criar operação de deleção
        const char* current_user = getenv("USER");
        if (!current_user) current_user = "unknown";

        Operation* op = operation_create("delete", 0, 0, "", current_user);

        // Salvar no log
        if (lm) {
            log_save_operation(lm, op);
        }

        // Enviar para servidor
        if (ws && ws_get_state(ws) == WS_CONNECTED) {
            ws_send_operation(ws, op);
        }

        operation_destroy(op);
    }

    pthread_mutex_unlock(&operations_mutex);
}

// Função para monitorar mudanças em arquivos
void monitor_files(void) {
    log_message(LOG_INFO, "Starting file monitoring...");

    // Criar file watcher
    fw = file_watcher_create(".");
    if (!fw) {
        log_message(LOG_ERROR, "Failed to create file watcher");
        return;
    }

    // Iniciar monitoramento
    if (file_watcher_start(fw, handle_file_change, NULL) != 0) {
        log_message(LOG_ERROR, "Failed to start file watcher");
        file_watcher_destroy(fw);
        fw = NULL;
        return;
    }

    // Adicionar arquivos existentes ao versioning manager
    WatchedFile* files;
    int file_count;
    if (file_watcher_get_files(fw, &files, &file_count) == 0) {
        for (int i = 0; i < file_count; i++) {
            versioning_add_file(vm, files[i].filepath);
        }
        log_message(LOG_INFO, "Added %d existing files to version control", file_count);
    }

    // Loop principal de monitoramento
    while (running) {
        // Processar eventos do WebSocket
        if (ws && ws_get_state(ws) == WS_CONNECTED) {
            ws_service(ws, 100);
        }

        // Em sistemas sem inotify, fazer polling manual
        #ifndef __linux__
        if (fw) {
            file_watcher_poll_changes(fw);
        }
        #endif

        // Aguardar um pouco antes da próxima verificação
        usleep(100000); // 100ms
    }

    // Cleanup
    if (fw) {
        file_watcher_stop(fw);
        file_watcher_destroy(fw);
        fw = NULL;
    }

    log_message(LOG_INFO, "File monitoring stopped");
}

// Função para exibir status atual
void show_status(void) {
    printf("MyVC Status\n");
    printf("===========\n");

    // Verificar se está inicializado
    if (!dir_exists(".myvc")) {
        printf("Not a myvc repository (or any of the parent directories)\n");
        printf("Run 'myvc init' to initialize.\n");
        return;
    }

    // Mostrar arquivos monitorados
    vm = versioning_create();
    if (vm) {
        printf("Tracked files:\n");

        // Simular lista de arquivos (implementação simplificada)
        DIR* dir = opendir(".");
        if (dir) {
            struct dirent* entry;
            int file_count = 0;

            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.' ||
                    !strstr(entry->d_name, ".c") &&
                    !strstr(entry->d_name, ".h") &&
                    !strstr(entry->d_name, ".txt") &&
                    !strstr(entry->d_name, ".md")) {
                    continue;
                }

                struct stat st;
                if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                    printf("  %s\n", entry->d_name);
                    file_count++;
                }
            }

            closedir(dir);
            printf("\nTotal: %d files\n", file_count);
        }

        versioning_destroy(vm);
    }

    // Mostrar conexão com servidor
    printf("\nServer connection: Not connected\n");
    printf("Last sync: Never\n");
}

// Função para exibir histórico
void show_log(void) {
    printf("MyVC Log\n");
    printf("========\n");

    lm = log_create(".");
    if (!lm) {
        printf("Error: Not a myvc repository\n");
        return;
    }

    int op_count;
    Operation** ops = log_load_operations(lm, &op_count);

    if (!ops || op_count == 0) {
        printf("No operations found\n");
        log_destroy(lm);
        return;
    }

    printf("Found %d operations:\n\n", op_count);

    for (int i = op_count - 1; i >= 0; i--) { // Mais recentes primeiro
        Operation* op = ops[i];
        char* time_str = time_format(op->timestamp);

        printf("Operation %d:\n", i + 1);
        printf("  Type: %s\n", op->op_type);
        printf("  Author: %s\n", op->author);
        printf("  Time: %s\n", time_str);
        printf("  Location: line %d, column %d\n", op->line, op->column);
        if (op->text && strlen(op->text) > 0) {
            printf("  Text: %.50s%s\n", op->text, strlen(op->text) > 50 ? "..." : "");
        }
        printf("\n");

        operation_destroy(op);
    }

    safe_free(ops);
    log_destroy(lm);
}

// Exibir ajuda
void print_usage(const char* program_name) {
    printf("Usage: %s [OPTIONS] [COMMAND]\n", program_name);
    printf("\nOptions:\n");
    printf("  -s, --server SERVER    Server address (default: %s)\n", DEFAULT_SERVER);
    printf("  -p, --port PORT        Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -d, --directory DIR    Project directory (default: current)\n");
    printf("  -v, --verbose          Enable verbose logging\n");
    printf("  -h, --help             Show this help message\n");
    printf("  --version              Show version information\n");
    printf("\nCommands:\n");
    printf("  init                   Initialize version control in current directory\n");
    printf("  watch                  Start watching files for changes\n");
    printf("  commit MESSAGE         Create a checkpoint with message\n");
    printf("  status                 Show current status\n");
    printf("  log                    Show operation history\n");
}

int main(int argc, char* argv[]) {
    // Opções de linha de comando
    char* server = DEFAULT_SERVER;
    int port = DEFAULT_PORT;
    char* directory = ".";
    int verbose = 0;

    // Estrutura para getopt_long
    static struct option long_options[] = {
        {"server", required_argument, 0, 's'},
        {"port", required_argument, 0, 'p'},
        {"directory", required_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 0},
        {0, 0, 0, 0}
    };

    // Processar opções
    int option_index = 0;
    int c;

    while ((c = getopt_long(argc, argv, "s:p:d:vh", long_options, &option_index)) != -1) {
        switch (c) {
            case 0:
                if (strcmp(long_options[option_index].name, "version") == 0) {
                    printf("myvc version %s\n", VERSION);
                    return 0;
                }
                break;
            case 's':
                server = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                directory = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Configurar logging
    if (verbose) {
        log_set_level(LOG_DEBUG);
    }

    // Mudar para diretório especificado
    if (chdir(directory) != 0) {
        log_message(LOG_ERROR, "Failed to change to directory %s", directory);
        return 1;
    }

    // Configurar handlers de sinal
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Processar comando
    if (optind < argc) {
        const char* command = argv[optind];

        if (strcmp(command, "init") == 0) {
            // Resolver caminho absoluto
            char resolved_path[PATH_MAX];
            if (realpath(".", resolved_path) == NULL) {
                strncpy(resolved_path, ".", PATH_MAX - 1);
                resolved_path[PATH_MAX - 1] = '\0';
            }

            log_message(LOG_INFO, "Initializing version control in %s", resolved_path);

            // Verificar se já foi inicializado
            if (dir_exists(".myvc")) {
                fprintf(stderr, "Error: Already initialized in %s\n", resolved_path);
                return 1;
            }

            if (log_init_directory(".") != 0) {
                log_message(LOG_ERROR, "Failed to initialize directory");
                fprintf(stderr, "Error: Could not initialize .myvc in %s\n", resolved_path);
                fprintf(stderr, "Make sure you have write permissions in this directory.\n");
                return 1;
            }
            printf("Initialized empty myvc repository in %s/.myvc\n", resolved_path);
            return 0;
        }
        else if (strcmp(command, "watch") == 0) {
            // Verificar se está inicializado
            if (!dir_exists(".myvc")) {
                fprintf(stderr, "Error: Not a myvc repository. Run 'myvc init' first.\n");
                return 1;
            }

            // Inicializar componentes
            vm = versioning_create();
            lm = log_create(".");
            ws = ws_create(server, port);

            if (!vm || !lm || !ws) {
                log_message(LOG_ERROR, "Failed to initialize components");
                goto cleanup;
            }

            // Conectar ao servidor
            if (ws_connect(ws) != 0) {
                log_message(LOG_WARNING, "Failed to connect to server %s:%d, working offline", server, port);
            } else {
                log_message(LOG_INFO, "Connected to server %s:%d", server, port);
                ws_receive_operations(ws, handle_remote_operation, NULL);
            }

            // Iniciar monitoramento
            monitor_files();
        }
        else if (strcmp(command, "commit") == 0) {
            if (optind + 1 >= argc) {
                fprintf(stderr, "Error: commit requires a message\n");
                return 1;
            }

            if (!dir_exists(".myvc")) {
                fprintf(stderr, "Error: Not a myvc repository. Run 'myvc init' first.\n");
                return 1;
            }

            lm = log_create(".");
            if (!lm) {
                log_message(LOG_ERROR, "Failed to initialize log manager");
                return 1;
            }

            const char* message = argv[optind + 1];
            if (log_create_checkpoint(lm, message) == 0) {
                printf("Created checkpoint: %s\n", message);
            } else {
                fprintf(stderr, "Failed to create checkpoint\n");
                return 1;
            }

            log_destroy(lm);
            return 0;
        }
        else if (strcmp(command, "status") == 0) {
            show_status();
            return 0;
        }
        else if (strcmp(command, "log") == 0) {
            show_log();
            return 0;
        }
        else {
            fprintf(stderr, "Unknown command: %s\n", command);
            print_usage(argv[0]);
            return 1;
        }
    } else {
        print_usage(argv[0]);
        return 1;
    }

cleanup:
    if (fw) {
        file_watcher_stop(fw);
        file_watcher_destroy(fw);
    }
    if (ws) {
        ws_disconnect(ws);
        ws_destroy(ws);
    }
    if (lm) {
        log_destroy(lm);
    }
    if (vm) {
        versioning_destroy(vm);
    }

    pthread_mutex_destroy(&operations_mutex);
    log_message(LOG_INFO, "Shutdown complete");
    return 0;
}