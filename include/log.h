//
// Created by HP on 08/07/2025.
//

#ifndef LOG_H
#define LOG_H

#include "operation.h"
#include "stdio.h"

#define LOG_DIR ".myvc"
#define LOG_FILE "log.json"
#define OPS_DIR "ops"
#define VERSIONS_DIR "versions"

typedef struct {
    char project_path[256];
    char log_path[512];
    FILE* log_file;
} LogManager;

// Funções do gerenciador de logs
LogManager* log_create(const char* project_path);
void log_destroy(LogManager* lm);
int log_init_directory(const char* project_path);
int log_save_operation(LogManager* lm, const Operation* op);
int log_save_snapshot(LogManager* lm, const char* filepath, const char* content);
Operation** log_load_operations(LogManager* lm, int* count);
char* log_load_snapshot(LogManager* lm, const char* version_id);
int log_create_checkpoint(LogManager* lm, const char* message);

#endif // LOG_H
