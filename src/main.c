#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>

#include "versioning.h"
#include "log.h"
#include "websocket_client.h"
#include "utils.h"

#define VERSION "0.1.0"
#define DEFAULT_SERVER "localhost"
#define DEFAULT_PORT 8080

// Variáveis globais para gerenciar o estado do programa
static volatile int running = 1;
static VersioningManager* vm = NULL;
static LogManager* lm = NULL;
static WebSocketClient* ws = NULL;

// Handler para sinais
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        log_message(LOG_INFO, "Received signal %d, shutting down...", sig);
        running = 0;
    }
}

// Callback para processar operações recebidas do servidor
void handle_remote_operation(const Operation* op, void* user_data) {
    (void)user_data; // Não usado por enquanto

    log_message(LOG_INFO, "Received operation from %s: %s at line %d, col %d",
                op->author, op->op_type, op->line, op->column);

    // Salvar operação no log local
    if (lm) {
        log_save_operation(lm, op);
    }

    // TODO: Aplicar operação ao arquivo local
}

// Função para monitorar mudanças em arquivos
void monitor_files(void) {
    while (running) {
        // TODO: Implementar detecção de mudanças em arquivos
        // Por enquanto, apenas aguarda
        sleep(1);
    }
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

    // Configurar handlers de sinal
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Processar comando
    if (optind < argc) {
        const char* command = argv[optind];

        if (strcmp(command, "init") == 0) {
            // Resolver caminho absoluto
            char resolved_path[PATH_MAX];
            if (realpath(directory, resolved_path) == NULL) {
                // Se o diretório não existe ou não pode ser resolvido, usar o caminho fornecido
                strncpy(resolved_path, directory, PATH_MAX - 1);
                resolved_path[PATH_MAX - 1] = '\0';
            }

            log_message(LOG_INFO, "Initializing version control in %s", resolved_path);

            // Verificar se já foi inicializado
            char myvc_path[PATH_MAX];
            snprintf(myvc_path, sizeof(myvc_path), "%s/.myvc", resolved_path);
            if (dir_exists(myvc_path)) {
                fprintf(stderr, "Error: Already initialized in %s\n", resolved_path);
                return 1;
            }

            if (log_init_directory(resolved_path) != 0) {
                log_message(LOG_ERROR, "Failed to initialize directory");
                fprintf(stderr, "Error: Could not initialize .myvc in %s\n", resolved_path);
                fprintf(stderr, "Make sure you have write permissions in this directory.\n");
                return 1;
            }
            printf("Initialized empty myvc repository in %s/.myvc\n", resolved_path);
            return 0;
        }
        else if (strcmp(command, "watch") == 0) {
            // Inicializar componentes
            vm = versioning_create();
            lm = log_create(directory);
            ws = ws_create(server, port);

            if (!vm || !lm || !ws) {
                log_message(LOG_ERROR, "Failed to initialize components");
                goto cleanup;
            }

            // Conectar ao servidor
            if (ws_connect(ws) != 0) {
                log_message(LOG_WARNING, "Failed to connect to server, working offline");
            }

            log_message(LOG_INFO, "Starting file monitoring...");
            monitor_files();
        }
        else if (strcmp(command, "commit") == 0) {
            if (optind + 1 >= argc) {
                fprintf(stderr, "Error: commit requires a message\n");
                return 1;
            }

            lm = log_create(directory);
            if (!lm) {
                log_message(LOG_ERROR, "Failed to initialize log manager");
                return 1;
            }

            const char* message = argv[optind + 1];
            if (log_create_checkpoint(lm, message) == 0) {
                printf("Created checkpoint: %s\n", message);
            } else {
                fprintf(stderr, "Failed to create checkpoint\n");
            }

            log_destroy(lm);
            return 0;
        }
        else if (strcmp(command, "status") == 0) {
            // TODO: Implementar comando status
            printf("Status: Not implemented yet\n");
            return 0;
        }
        else if (strcmp(command, "log") == 0) {
            // TODO: Implementar comando log
            printf("Log: Not implemented yet\n");
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

    log_message(LOG_INFO, "Shutdown complete");
    return 0;
}