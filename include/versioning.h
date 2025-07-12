//
// Created by HP on 08/07/2025.
//
#ifndef VERSIONING_H
#define VERSIONING_H

#include "operation.h"

#define MAX_FILEPATH_LEN 256
#define BUFFER_SIZE 1024

typedef struct {
    char filepath[MAX_FILEPATH_LEN];
    char* last_content;
    size_t last_content_size;
    time_t last_modified;
} FileState;

typedef struct {
    FileState** files;
    int file_count;
    int capacity;
} VersioningManager;

// Funções do gerenciador de versões
VersioningManager* versioning_create(void);
void versioning_destroy(VersioningManager* vm);
int versioning_add_file(VersioningManager* vm, const char* filepath);
int versioning_remove_file(VersioningManager* vm, const char* filepath);
Operation** versioning_detect_changes(VersioningManager* vm, const char* filepath, int* op_count);
int versioning_apply_patch(const char* filepath, Operation** ops, int op_count);
char* versioning_get_file_content(const char* filepath, size_t* size);
int versioning_diff_lines(const char* old_content, const char* new_content, Operation*** ops);

#endif // VERSIONING_H
