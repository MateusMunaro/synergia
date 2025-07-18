//
// Created by HP on 08/07/2025.
//
#include "versioning.h"
#include "utils.h"
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#define INITIAL_CAPACITY 10
#define LCS_THRESHOLD 1000  // Threshold para usar LCS vs algoritmo simples

// Estrutura para armazenar uma linha com hash
typedef struct {
    char* content;
    size_t length;
    unsigned long hash;
} LineInfo;

// Estrutura para resultado do diff
typedef struct {
    Operation** operations;
    int count;
    int capacity;
} DiffResult;

VersioningManager* versioning_create(void) {
    VersioningManager* vm = (VersioningManager*)safe_malloc(sizeof(VersioningManager));
    vm->files = (FileState**)safe_malloc(INITIAL_CAPACITY * sizeof(FileState*));
    vm->file_count = 0;
    vm->capacity = INITIAL_CAPACITY;

    log_message(LOG_DEBUG, "Created versioning manager");
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

    log_message(LOG_DEBUG, "Destroyed versioning manager");
}

static FileState* find_file_state(VersioningManager* vm, const char* filepath) {
    for (int i = 0; i < vm->file_count; i++) {
        if (strcmp(vm->files[i]->filepath, filepath) == 0) {
            return vm->files[i];
        }
    }
    return NULL;
}

static void expand_capacity_if_needed(VersioningManager* vm) {
    if (vm->file_count >= vm->capacity) {
        vm->capacity *= 2;
        vm->files = (FileState**)safe_realloc(vm->files, vm->capacity * sizeof(FileState*));
        log_message(LOG_DEBUG, "Expanded versioning manager capacity to %d", vm->capacity);
    }
}

int versioning_add_file(VersioningManager* vm, const char* filepath) {
    if (!vm || !filepath) return -1;

    // Verificar se o arquivo existe
    if (!file_exists(filepath)) {
        log_message(LOG_WARNING, "File %s does not exist", filepath);
        return -1;
    }

    // Verificar se o arquivo já está sendo monitorado
    if (find_file_state(vm, filepath)) {
        log_message(LOG_DEBUG, "File %s is already being tracked", filepath);
        return 0;
    }

    // Expandir array se necessário
    expand_capacity_if_needed(vm);

    // Criar novo estado de arquivo
    FileState* fs = (FileState*)safe_malloc(sizeof(FileState));
    strncpy(fs->filepath, filepath, MAX_FILEPATH_LEN - 1);
    fs->filepath[MAX_FILEPATH_LEN - 1] = '\0';

    // Ler conteúdo inicial
    fs->last_content = file_read_all(filepath, &fs->last_content_size);
    if (!fs->last_content) {
        log_message(LOG_ERROR, "Failed to read file %s", filepath);
        safe_free(fs);
        return -1;
    }

    fs->last_modified = file_get_mtime(filepath);
    vm->files[vm->file_count++] = fs;

    log_message(LOG_INFO, "Added file %s to version tracking (%zu bytes)",
                filepath, fs->last_content_size);
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

    log_message(LOG_WARNING, "File %s not found in tracking list", filepath);
    return -1;
}

// Função para calcular hash de uma linha
static unsigned long hash_line(const char* line, size_t length) {
    unsigned long hash = 5381;
    for (size_t i = 0; i < length; i++) {
        hash = ((hash << 5) + hash) + line[i];
    }
    return hash;
}

// Função para criar array de LineInfo
static LineInfo* create_line_info_array(char** lines, int line_count) {
    LineInfo* info = (LineInfo*)safe_malloc(line_count * sizeof(LineInfo));

    for (int i = 0; i < line_count; i++) {
        info[i].content = lines[i];
        info[i].length = strlen(lines[i]);
        info[i].hash = hash_line(lines[i], info[i].length);
    }

    return info;
}

// Função para adicionar operação ao resultado
static void add_operation_to_result(DiffResult* result, Operation* op) {
    if (result->count >= result->capacity) {
        result->capacity = result->capacity ? result->capacity * 2 : 10;
        result->operations = (Operation**)safe_realloc(
            result->operations, result->capacity * sizeof(Operation*)
        );
    }
    result->operations[result->count++] = op;
}

// Algoritmo LCS (Longest Common Subsequence) para diff mais preciso
static int** compute_lcs_table(LineInfo* old_info, int old_count,
                              LineInfo* new_info, int new_count) {
    int** lcs = (int**)safe_malloc((old_count + 1) * sizeof(int*));
    for (int i = 0; i <= old_count; i++) {
        lcs[i] = (int*)safe_malloc((new_count + 1) * sizeof(int));
        memset(lcs[i], 0, (new_count + 1) * sizeof(int));
    }

    for (int i = 1; i <= old_count; i++) {
        for (int j = 1; j <= new_count; j++) {
            if (old_info[i-1].hash == new_info[j-1].hash &&
                old_info[i-1].length == new_info[j-1].length &&
                strcmp(old_info[i-1].content, new_info[j-1].content) == 0) {
                lcs[i][j] = lcs[i-1][j-1] + 1;
            } else {
                lcs[i][j] = (lcs[i-1][j] > lcs[i][j-1]) ? lcs[i-1][j] : lcs[i][j-1];
            }
        }
    }

    return lcs;
}

// Gerar operações usando tabela LCS
static void generate_operations_from_lcs(int** lcs, LineInfo* old_info, int old_count,
                                       LineInfo* new_info, int new_count,
                                       DiffResult* result, const char* author) {
    int i = old_count, j = new_count;

    // Reconstruir o diff a partir da tabela LCS
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 &&
            old_info[i-1].hash == new_info[j-1].hash &&
            old_info[i-1].length == new_info[j-1].length &&
            strcmp(old_info[i-1].content, new_info[j-1].content) == 0) {
            // Linhas iguais - mover para trás
            i--;
            j--;
        } else if (j > 0 && (i == 0 || lcs[i][j-1] >= lcs[i-1][j])) {
            // Inserção
            Operation* op = operation_create("insert", j-1, 0, new_info[j-1].content, author);
            add_operation_to_result(result, op);
            j--;
        } else if (i > 0 && (j == 0 || lcs[i][j-1] < lcs[i-1][j])) {
            // Deleção
            Operation* op = operation_create("delete", i-1, 0, old_info[i-1].content, author);
            add_operation_to_result(result, op);
            i--;
        }
    }
}

// Liberar tabela LCS
static void free_lcs_table(int** lcs, int old_count) {
    for (int i = 0; i <= old_count; i++) {
        safe_free(lcs[i]);
    }
    safe_free(lcs);
}

// Algoritmo de diff simples para arquivos pequenos
static void simple_diff_algorithm(char** old_lines, int old_count,
                                char** new_lines, int new_count,
                                DiffResult* result, const char* author) {
    int i = 0, j = 0;

    while (i < old_count || j < new_count) {
        if (i >= old_count) {
            // Linhas adicionadas no final
            Operation* op = operation_create("insert", j, 0, new_lines[j], author);
            add_operation_to_result(result, op);
            j++;
        } else if (j >= new_count) {
            // Linhas removidas no final
            Operation* op = operation_create("delete", i, 0, old_lines[i], author);
            add_operation_to_result(result, op);
            i++;
        } else if (strcmp(old_lines[i], new_lines[j]) != 0) {
            // Linha modificada
            Operation* op = operation_create("replace", i, 0, new_lines[j], author);
            add_operation_to_result(result, op);
            i++;
            j++;
        } else {
            // Linhas iguais
            i++;
            j++;
        }
    }
}

int versioning_diff_lines(const char* old_content, const char* new_content, Operation*** ops) {
    if (!old_content || !new_content || !ops) return -1;

    const char* author = getenv("USER");
    if (!author) author = "system";

    int old_lines_count, new_lines_count;
    char** old_lines = str_split_lines(old_content, &old_lines_count);
    char** new_lines = str_split_lines(new_content, &new_lines_count);

    if (!old_lines || !new_lines) {
        str_free_lines(old_lines, old_lines_count);
        str_free_lines(new_lines, new_lines_count);
        return -1;
    }

    DiffResult result = {0};

    // Escolher algoritmo baseado no tamanho
    if (old_lines_count > LCS_THRESHOLD || new_lines_count > LCS_THRESHOLD) {
        // Usar algoritmo simples para arquivos grandes
        simple_diff_algorithm(old_lines, old_lines_count,
                             new_lines, new_lines_count, &result, author);
    } else {
        // Usar LCS para arquivos menores
        LineInfo* old_info = create_line_info_array(old_lines, old_lines_count);
        LineInfo* new_info = create_line_info_array(new_lines, new_lines_count);

        int** lcs = compute_lcs_table(old_info, old_lines_count,
                                     new_info, new_lines_count);

        generate_operations_from_lcs(lcs, old_info, old_lines_count,
                                   new_info, new_lines_count, &result, author);

        free_lcs_table(lcs, old_lines_count);
        safe_free(old_info);
        safe_free(new_info);
    }

    str_free_lines(old_lines, old_lines_count);
    str_free_lines(new_lines, new_lines_count);

    *ops = result.operations;
    return result.count;
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

    if (count > 0) {
        log_message(LOG_INFO, "Detected %d changes in %s", count, filepath);

        // Atualizar estado do arquivo
        safe_free(fs->last_content);
        fs->last_content = current_content;
        fs->last_content_size = current_size;
        fs->last_modified = current_mtime;
    } else {
        safe_free(current_content);
    }

    *op_count = count;
    return ops;
}

int versioning_apply_patch(const char* filepath, Operation** ops, int op_count) {
    if (!filepath || !ops || op_count <= 0) return -1;

    // Ler conteúdo atual
    size_t size;
    char* content = file_read_all(filepath, &size);
    if (!content) {
        log_message(LOG_ERROR, "Failed to read file %s for patching", filepath);
        return -1;
    }

    int line_count;
    char** lines = str_split_lines(content, &line_count);
    safe_free(content);

    if (!lines) {
        log_message(LOG_ERROR, "Failed to split file into lines");
        return -1;
    }

    // Aplicar cada operação
    for (int i = 0; i < op_count; i++) {
        Operation* op = ops[i];

        if (strcmp(op->op_type, "insert") == 0) {
            // TODO: Implementar inserção real
            log_message(LOG_DEBUG, "Would insert at line %d: %s", op->line, op->text);
        } else if (strcmp(op->op_type, "delete") == 0) {
            // TODO: Implementar deleção real
            log_message(LOG_DEBUG, "Would delete line %d", op->line);
        } else if (strcmp(op->op_type, "replace") == 0) {
            // TODO: Implementar substituição real
            log_message(LOG_DEBUG, "Would replace line %d with: %s", op->line, op->text);
        }
    }

    // TODO: Reconstruir arquivo e salvar
    str_free_lines(lines, line_count);

    log_message(LOG_INFO, "Applied %d operations to %s", op_count, filepath);
    return 0;
}

char* versioning_get_file_content(const char* filepath, size_t* size) {
    return file_read_all(filepath, size);
}