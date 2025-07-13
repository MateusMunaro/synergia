//
// Created by HP on 08/07/2025.
//

#ifndef OPERATION_H
#define OPERATION_H

#include <time.h>

#define MAX_OP_TYPE_LEN 10
#define MAX_AUTHOR_LEN 32
#define MAX_TEXT_LEN 4096

typedef enum {
    OP_INSERT,
    OP_DELETE,
    OP_REPLACE
} OpType;

typedef struct {
    char op_type[MAX_OP_TYPE_LEN];  // "insert", "delete", "replace"
    int line;                        // Linha afetada
    int column;                      // Coluna afetada
    char* text;                      // Texto inserido/removido
    char author[MAX_AUTHOR_LEN];     // Autor da operação
    long timestamp;                  // Tempo UNIX
} Operation;

// Funções para manipular operações
Operation* operation_create(const char* type, int line, int column,
                           const char* text, const char* author);
void operation_destroy(Operation* op);
char* operation_serialize(const Operation* op);
Operation* operation_deserialize(const char* json_str);
int operation_apply_to_file(const Operation* op, const char* filepath);

#endif // OPERATION_H
