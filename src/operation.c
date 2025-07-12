//
// Created by HP on 08/07/2025.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "../include/operation.h"
#include "../include/utils.h"

Operation* operation_create(const char* type, int line, int column, const char* text, const char* author) {
    Operation* op = (Operation*)safe_malloc(sizeof(Operation));

    strncpy(op->op_type, type, MAX_OP_TYPE_LEN - 1);
    op->op_type[MAX_OP_TYPE_LEN - 1] = '\0';

    op->line = line;
    op->column = column;
    op->text = str_duplicate(text);

    strncpy(op->author, author, MAX_AUTHOR_LEN - 1);
    op->author[MAX_AUTHOR_LEN - 1] = '\0';

    op->timestamp = time_get_unix();

    return op;
}

void operation_destroy(Operation* op) {
    if (op) {
        safe_free(op->text);
        safe_free(op);
    }
}

char* operation_serialize(const Operation* op) {
    if (!op) return NULL;

    json_t* root = json_object();
    json_object_set_new(root, "op_type", json_string(op->op_type));
    json_object_set_new(root, "line", json_integer(op->line));
    json_object_set_new(root, "column", json_integer(op->column));
    json_object_set_new(root, "text", json_string(op->text));
    json_object_set_new(root, "author", json_string(op->author));
    json_object_set_new(root, "timestamp", json_integer(op->timestamp));

    char* json_str = json_dumps(root, JSON_COMPACT);
    json_decref(root);

    return json_str;
}

Operation* operation_deserialize(const char* json_str) {
    if (!json_str) return NULL;

    json_error_t error;
    json_t* root = json_loads(json_str, 0, &error);
    if (!root) {
        log_message(LOG_ERROR, "Failed to parse JSON: %s", error.text);
        return NULL;
    }

    Operation* op = (Operation*)safe_malloc(sizeof(Operation));

    const char* op_type = json_string_value(json_object_get(root, "op_type"));
    strncpy(op->op_type, op_type, MAX_OP_TYPE_LEN - 1);
    op->op_type[MAX_OP_TYPE_LEN - 1] = '\0';

    op->line = json_integer_value(json_object_get(root, "line"));
    op->column = json_integer_value(json_object_get(root, "column"));

    const char* text = json_string_value(json_object_get(root, "text"));
    op->text = str_duplicate(text);

    const char* author = json_string_value(json_object_get(root, "author"));
    strncpy(op->author, author, MAX_AUTHOR_LEN - 1);
    op->author[MAX_AUTHOR_LEN - 1] = '\0';

    op->timestamp = json_integer_value(json_object_get(root, "timestamp"));

    json_decref(root);
    return op;
}

int operation_apply_to_file(const Operation* op, const char* filepath) {
    if (!op || !filepath) return -1;

    size_t size;
    char* content = file_read_all(filepath, &size);
    if (!content) return -1;

    int line_count;
    char** lines = str_split_lines(content, &line_count);
    safe_free(content);

    if (op->line < 0 || op->line >= line_count) {
        str_free_lines(lines, line_count);
        return -1;
    }

    // Aplicar operação baseada no tipo
    if (strcmp(op->op_type, "insert") == 0) {
        // Implementar lógica de inserção
        // Por enquanto, vamos apenas logar
        log_message(LOG_INFO, "Applying INSERT operation at line %d, col %d",
                    op->line, op->column);
    } else if (strcmp(op->op_type, "delete") == 0) {
        // Implementar lógica de deleção
        log_message(LOG_INFO, "Applying DELETE operation at line %d, col %d",
                    op->line, op->column);
    } else if (strcmp(op->op_type, "replace") == 0) {
        // Implementar lógica de substituição
        log_message(LOG_INFO, "Applying REPLACE operation at line %d, col %d",
                    op->line, op->column);
    }

    // TODO: Reconstruir o arquivo com as mudanças aplicadas

    str_free_lines(lines, line_count);
    return 0;
}