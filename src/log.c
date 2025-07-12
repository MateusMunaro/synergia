//
// Created by HP on 08/07/2025.
//
#include "log.h"
#include "utils.h"
#include <jansson.h>
#include <time.h>
#include <errno.h>

LogManager* log_create(const char* project_path) {
    if (!project_path) return NULL;

    LogManager* lm = (LogManager*)safe_malloc(sizeof(LogManager));

    strncpy(lm->project_path, project_path, sizeof(lm->project_path) - 1);
    lm->project_path[sizeof(lm->project_path) - 1] = '\0';

    // Construir caminho do log
    snprintf(lm->log_path, sizeof(lm->log_path), "%s/%s", project_path, LOG_DIR);

    // Verificar se o diretório .myvc existe
    if (!dir_exists(lm->log_path)) {
        log_message(LOG_ERROR, "Version control not initialized in %s", project_path);
        safe_free(lm);
        return NULL;
    }

    lm->log_file = NULL;

    log_message(LOG_INFO, "Log manager created for project: %s", project_path);
    return lm;
}

void log_destroy(LogManager* lm) {
    if (!lm) return;

    if (lm->log_file) {
        fclose(lm->log_file);
    }

    safe_free(lm);
}

int log_init_directory(const char* project_path) {
    if (!project_path) return -1;

    char path[512];

    // Criar diretório .myvc
    snprintf(path, sizeof(path), "%s/%s", project_path, LOG_DIR);
    if (!dir_exists(path)) {
        if (dir_create(path) != 0 && errno != EEXIST) {
            log_message(LOG_ERROR, "Failed to create directory %s: %s", path, strerror(errno));
            return -1;
        }
    }

    // Criar subdiretórios
    snprintf(path, sizeof(path), "%s/%s/%s", project_path, LOG_DIR, OPS_DIR);
    if (!dir_exists(path)) {
        if (dir_create(path) != 0 && errno != EEXIST) {
            log_message(LOG_ERROR, "Failed to create directory %s: %s", path, strerror(errno));
            return -1;
        }
    }

    snprintf(path, sizeof(path), "%s/%s/%s", project_path, LOG_DIR, VERSIONS_DIR);
    if (!dir_exists(path)) {
        if (dir_create(path) != 0 && errno != EEXIST) {
            log_message(LOG_ERROR, "Failed to create directory %s: %s", path, strerror(errno));
            return -1;
        }
    }

    // Criar arquivo de índice inicial
    snprintf(path, sizeof(path), "%s/%s/index", project_path, LOG_DIR);
    if (!file_exists(path)) {
        json_t* index = json_object();
        json_object_set_new(index, "version", json_string("1.0"));
        json_object_set_new(index, "created", json_integer(time_get_unix()));
        json_object_set_new(index, "last_operation_id", json_integer(0));

        char* json_str = json_dumps(index, JSON_INDENT(2));
        file_write_all(path, json_str, strlen(json_str));

        free(json_str);
        json_decref(index);
    }

    // Criar arquivo de log inicial
    snprintf(path, sizeof(path), "%s/%s/%s", project_path, LOG_DIR, LOG_FILE);
    if (!file_exists(path)) {
        json_t* log_array = json_array();
        char* json_str = json_dumps(log_array, JSON_INDENT(2));
        file_write_all(path, json_str, strlen(json_str));

        free(json_str);
        json_decref(log_array);
    }

    log_message(LOG_INFO, "Initialized version control in %s", project_path);
    return 0;
}

int log_save_operation(LogManager* lm, const Operation* op) {
    if (!lm || !op) return -1;

    // Gerar ID único para a operação
    char op_filename[512];
    snprintf(op_filename, sizeof(op_filename), "%s/%s/%ld_%s.json",
             lm->log_path, OPS_DIR, op->timestamp, op->author);

    // Serializar operação
    char* json_str = operation_serialize(op);
    if (!json_str) return -1;

    // Salvar em arquivo
    int result = file_write_all(op_filename, json_str, strlen(json_str));
    free(json_str);

    if (result != 0) {
        log_message(LOG_ERROR, "Failed to save operation to %s", op_filename);
        return -1;
    }

    // Atualizar log.json
    char log_file_path[512];
    snprintf(log_file_path, sizeof(log_file_path), "%s/%s", lm->log_path, LOG_FILE);

    // Ler log existente
    size_t size;
    char* log_content = file_read_all(log_file_path, &size);
    if (!log_content) {
        log_message(LOG_ERROR, "Failed to read log file");
        return -1;
    }

    json_error_t error;
    json_t* log_array = json_loads(log_content, 0, &error);
    safe_free(log_content);

    if (!log_array) {
        log_message(LOG_ERROR, "Failed to parse log file: %s", error.text);
        return -1;
    }

    // Adicionar nova entrada
    json_t* entry = json_object();
    json_object_set_new(entry, "timestamp", json_integer(op->timestamp));
    json_object_set_new(entry, "type", json_string(op->op_type));
    json_object_set_new(entry, "author", json_string(op->author));
    json_object_set_new(entry, "file", json_string(op_filename));
    json_array_append_new(log_array, entry);

    // Salvar log atualizado
    char* updated_log = json_dumps(log_array, JSON_INDENT(2));
    result = file_write_all(log_file_path, updated_log, strlen(updated_log));

    free(updated_log);
    json_decref(log_array);

    log_message(LOG_DEBUG, "Saved operation to %s", op_filename);
    return result;
}

int log_save_snapshot(LogManager* lm, const char* filepath, const char* content) {
    if (!lm || !filepath || !content) return -1;

    // Gerar nome único para o snapshot
    long timestamp = time_get_unix();
    char snapshot_filename[512];
    snprintf(snapshot_filename, sizeof(snapshot_filename), "%s/%s/%ld_%s.snapshot",
             lm->log_path, VERSIONS_DIR, timestamp, filepath);

    // Salvar conteúdo
    int result = file_write_all(snapshot_filename, content, strlen(content));

    if (result == 0) {
        log_message(LOG_INFO, "Saved snapshot to %s", snapshot_filename);
    } else {
        log_message(LOG_ERROR, "Failed to save snapshot to %s", snapshot_filename);
    }

    return result;
}

Operation** log_load_operations(LogManager* lm, int* count) {
    if (!lm || !count) return NULL;

    *count = 0;

    // Ler log.json
    char log_file_path[512];
    snprintf(log_file_path, sizeof(log_file_path), "%s/%s", lm->log_path, LOG_FILE);

    size_t size;
    char* log_content = file_read_all(log_file_path, &size);
    if (!log_content) return NULL;

    json_error_t error;
    json_t* log_array = json_loads(log_content, 0, &error);
    safe_free(log_content);

    if (!log_array || !json_is_array(log_array)) {
        log_message(LOG_ERROR, "Invalid log file format");
        return NULL;
    }

    size_t array_size = json_array_size(log_array);
    if (array_size == 0) {
        json_decref(log_array);
        return NULL;
    }

    Operation** ops = (Operation**)safe_malloc(array_size * sizeof(Operation*));
    int op_count = 0;

    // Carregar cada operação
    for (size_t i = 0; i < array_size; i++) {
        json_t* entry = json_array_get(log_array, i);
        const char* op_file = json_string_value(json_object_get(entry, "file"));

        if (op_file) {
            size_t op_size;
            char* op_content = file_read_all(op_file, &op_size);
            if (op_content) {
                Operation* op = operation_deserialize(op_content);
                if (op) {
                    ops[op_count++] = op;
                }
                safe_free(op_content);
            }
        }
    }

    json_decref(log_array);

    *count = op_count;
    return ops;
}

char* log_load_snapshot(LogManager* lm, const char* version_id) {
    if (!lm || !version_id) return NULL;

    char snapshot_path[512];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s/%s",
             lm->log_path, VERSIONS_DIR, version_id);

    size_t size;
    char* content = file_read_all(snapshot_path, &size);

    if (content) {
        log_message(LOG_INFO, "Loaded snapshot from %s", snapshot_path);
    }

    return content;
}

int log_create_checkpoint(LogManager* lm, const char* message) {
    if (!lm || !message) return -1;

    // Criar arquivo de checkpoint
    long timestamp = time_get_unix();
    char checkpoint_path[512];
    snprintf(checkpoint_path, sizeof(checkpoint_path), "%s/%s/checkpoint_%ld.json",
             lm->log_path, VERSIONS_DIR, timestamp);

    json_t* checkpoint = json_object();
    json_object_set_new(checkpoint, "timestamp", json_integer(timestamp));
    json_object_set_new(checkpoint, "message", json_string(message));
    json_object_set_new(checkpoint, "author", json_string(getenv("USER") ? getenv("USER") : "unknown"));

    // Adicionar lista de operações até este ponto
    int op_count;
    Operation** ops = log_load_operations(lm, &op_count);

    json_t* op_refs = json_array();
    for (int i = 0; i < op_count; i++) {
        char op_ref[256];
        snprintf(op_ref, sizeof(op_ref), "%ld_%s.json", ops[i]->timestamp, ops[i]->author);
        json_array_append_new(op_refs, json_string(op_ref));
        operation_destroy(ops[i]);
    }
    safe_free(ops);

    json_object_set_new(checkpoint, "operations", op_refs);

    // Salvar checkpoint
    char* json_str = json_dumps(checkpoint, JSON_INDENT(2));
    int result = file_write_all(checkpoint_path, json_str, strlen(json_str));

    free(json_str);
    json_decref(checkpoint);

    if (result == 0) {
        log_message(LOG_INFO, "Created checkpoint: %s", message);
    } else {
        log_message(LOG_ERROR, "Failed to create checkpoint");
    }

    return result;
}