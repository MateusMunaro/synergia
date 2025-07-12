//
// Created by HP on 08/07/2025.
//
#include "versioning.h"
#include "utils.h"
#include <dirent.h>
#include <errno.h>

#define INITIAL_CAPACITY 10

VersioningManager* versioning_create(void) {
    VersioningManager* vm = (VersioningManager*)safe_malloc(sizeof(VersioningManager));
    vm->files = (FileState**)safe_malloc(INITIAL_CAPACITY * sizeof(FileState*));
    vm->file_count = 0;
    vm->capacity = INITIAL_CAPACITY;
    return vm;
}

void versioning_destroy(VersioningManager* vm) {
    if (!vm) return;

    for (int i = 0; i < vm->file_count; i++) {
        if (vm->files[i]) {
            safe_free(vm->files[i]->last_content);
            safe_free(vm->files[i]);
        }
    }
    safe_free(vm->files);
    safe_free(vm);
}

static FileState* find_file_state(VersioningManager* vm, const char* filepath) {
    for (int i = 0; i < vm->file_count; i++) {
        if (strcmp(vm->files[i]->filepath, filepath) == 0) {
            return vm->files[i];
        }
    }
    return NULL;
}

int versioning_add_file(VersioningManager* vm, const char* filepath) {
    if (!vm || !filepath) return -1;

    // Verificar se o arquivo já está sendo monitorado
    if (find_file_state(vm, filepath)) {
        log_message(LOG_WARNING, "File %s is already being tracked", filepath);
        return 0;
    }

    // Expandir array se necessário
    if (vm->file_count >= vm->capacity) {
        vm->capacity *= 2;
        vm->files = (FileState**)safe_realloc(vm->files, vm->capacity * sizeof(FileState*));
    }

    // Criar novo estado de arquivo
    FileState* fs = (FileState*)safe_malloc(sizeof(FileState));
    strncpy(fs->filepath, filepath, MAX_FILEPATH_LEN - 1);
    fs->filepath[MAX_FILEPATH_LEN - 1] = '\0';

    // Ler conteúdo inicial
    size_t size;
    fs->last_content = file_read_all(filepath, &size);
    fs->last_content_size = size;
    fs->last_modified = file_get_mtime(filepath);

    vm->files[vm->file_count++] = fs;

    log_message(LOG_INFO, "Added file %s to version tracking", filepath);
    return 0;
}

int versioning_remove_file(VersioningManager* vm, const char* filepath) {
    if (!vm || !filepath) return -1;

    for (int i = 0; i < vm->file_count; i++) {
        if (strcmp(vm->files[i]->filepath, filepath) == 0) {
            safe_free(vm->files[i]->last_content);
            safe_free(vm->files[i]);

            // Mover elementos restantes
            for (int j = i; j < vm->file_count - 1; j++) {
                vm->files[j] = vm->files[j + 1];
            }
            vm->file_count--;

            log_message(LOG_INFO, "Removed file %s from version tracking", filepath);
            return 0;
        }
    }

    return -1;
}

static int calculate_line_column(const char* content, int offset, int* line, int* column) {
    *line = 0;
    *column = 0;

    for (int i = 0; i < offset && content[i]; i++) {
        if (content[i] == '\n') {
            (*line)++;
            *column = 0;
        } else {
            (*column)++;
        }
    }

    return 0;
}

int versioning_diff_lines(const char* old_content, const char* new_content, Operation*** ops) {
    if (!old_content || !new_content || !ops) return -1;

    int old_lines_count, new_lines_count;
    char** old_lines = str_split_lines(old_content, &old_lines_count);
    char** new_lines = str_split_lines(new_content, &new_lines_count);

    // Lista dinâmica de operações
    Operation** op_list = NULL;
    int op_count = 0;
    int op_capacity = 0;

    // Algoritmo simples de diff linha por linha
    int i = 0, j = 0;

    while (i < old_lines_count || j < new_lines_count) {
        if (i >= old_lines_count) {
            // Linhas adicionadas no final
            if (op_count >= op_capacity) {
                op_capacity = op_capacity ? op_capacity * 2 : 10;
                op_list = (Operation**)safe_realloc(op_list, op_capacity * sizeof(Operation*));
            }

            op_list[op_count++] = operation_create("insert", j, 0, new_lines[j], "system");
            j++;
        } else if (j >= new_lines_count) {
            // Linhas removidas no final
            if (op_count >= op_capacity) {
                op_capacity = op_capacity ? op_capacity * 2 : 10;
                op_list = (Operation**)safe_realloc(op_list, op_capacity * sizeof(Operation*));
            }

            op_list[op_count++] = operation_create("delete", i, 0, old_lines[i], "system");
            i++;
        } else if (strcmp(old_lines[i], new_lines[j]) != 0) {
            // Linha modificada
            if (op_count >= op_capacity) {
                op_capacity = op_capacity ? op_capacity * 2 : 10;
                op_list = (Operation**)safe_realloc(op_list, op_capacity * sizeof(Operation*));
            }

            op_list[op_count++] = operation_create("replace", i, 0, new_lines[j], "system");
            i++;
            j++;
        } else {
            // Linhas iguais
            i++;
            j++;
        }
    }

    str_free_lines(old_lines, old_lines_count);
    str_free_lines(new_lines, new_lines_count);

    *ops = op_list;
    return op_count;
}

Operation** versioning_detect_changes(VersioningManager* vm, const char* filepath, int* op_count) {
    if (!vm || !filepath || !op_count) return NULL;

    FileState* fs = find_file_state(vm, filepath);
    if (!fs) {
        log_message(LOG_WARNING, "File %s is not being tracked", filepath);
        return NULL;
    }

    // Verificar se o arquivo foi modificado
    time_t current_mtime = file_get_mtime(filepath);
    if (current_mtime == fs->last_modified) {
        *op_count = 0;
        return NULL; // Sem mudanças
    }

    // Ler conteúdo atual
    size_t current_size;
    char* current_content = file_read_all(filepath, &current_size);
    if (!current_content) {
        log_message(LOG_ERROR, "Failed to read file %s", filepath);
        return NULL;
    }

    // Detectar diferenças
    Operation** ops = NULL;
    int count = versioning_diff_lines(fs->last_content, current_content, &ops);

    // Atualizar estado do arquivo
    safe_free(fs->last_content);
    fs->last_content = current_content;
    fs->last_content_size = current_size;
    fs->last_modified = current_mtime;

    *op_count = count;
    return ops;
}

int versioning_apply_patch(const char* filepath, Operation** ops, int op_count) {
    if (!filepath || !ops || op_count <= 0) return -1;

    // Ler conteúdo atual
    size_t size;
    char* content = file_read_all(filepath, &size);
    if (!content) return -1;

    // Aplicar cada operação
    for (int i = 0; i < op_count; i++) {
        Operation* op = ops[i];

        // TODO: Implementar aplicação real das operações
        // Por enquanto, apenas logar
        log_message(LOG_DEBUG, "Applying operation: %s at line %d",
                    op->op_type, op->line);
    }

    // TODO: Escrever conteúdo modificado de volta ao arquivo

    safe_free(content);
    return 0;
}

char* versioning_get_file_content(const char* filepath, size_t* size) {
    return file_read_all(filepath, size);
}
