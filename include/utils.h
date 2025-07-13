//
// Created by HP on 08/07/2025.
//

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

// Funções de arquivo
int file_exists(const char* filepath);
long file_get_size(const char* filepath);
time_t file_get_mtime(const char* filepath);
char* file_read_all(const char* filepath, size_t* size);
int file_write_all(const char* filepath, const char* content, size_t size);
int dir_create(const char* path);
int dir_exists(const char* path);

// Funções de string
char* str_duplicate(const char* str);
char** str_split_lines(const char* text, int* line_count);
void str_free_lines(char** lines, int line_count);
char* str_trim(char* str);

// Funções de tempo
long time_get_unix(void);
char* time_format(long timestamp);

// Funções de memória
void* safe_malloc(size_t size);
void* safe_realloc(void* ptr, size_t size);
void safe_free(void* ptr);

// Logging
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

void log_message(LogLevel level, const char* format, ...);
void log_set_level(LogLevel level);

#endif // UTILS_H