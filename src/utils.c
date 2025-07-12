//
// Created by HP on 08/07/2025.
//
#include "../include/utils.h"
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

static LogLevel current_log_level = LOG_INFO;

// Funções de arquivo
int file_exists(const char* filepath) {
    struct stat st;
    return stat(filepath, &st) == 0;
}

long file_get_size(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) != 0) return -1;
    return st.st_size;
}

time_t file_get_mtime(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) != 0) return 0;
    return st.st_mtime;
}

char* file_read_all(const char* filepath, size_t* size) {
    FILE* file = fopen(filepath, "rb");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(file);
        return NULL;
    }

    char* buffer = (char*)safe_malloc(file_size + 1);
    size_t read_size = fread(buffer, 1, file_size, file);
    buffer[read_size] = '\0';

    fclose(file);

    if (size) *size = read_size;
    return buffer;
}

int file_write_all(const char* filepath, const char* content, size_t size) {
    FILE* file = fopen(filepath, "wb");
    if (!file) return -1;

    size_t written = fwrite(content, 1, size, file);
    fclose(file);

    return (written == size) ? 0 : -1;
}

int dir_create(const char* path) {
    return mkdir(path, 0755);
}

int dir_exists(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

// Funções de string
char* str_duplicate(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* dup = (char*)safe_malloc(len + 1);
    strcpy(dup, str);
    return dup;
}

char** str_split_lines(const char* text, int* line_count) {
    if (!text || !line_count) return NULL;

    // Contar linhas
    int count = 1;
    const char* p = text;
    while (*p) {
        if (*p == '\n') count++;
        p++;
    }

    char** lines = (char**)safe_malloc(count * sizeof(char*));
    *line_count = count;

    // Dividir linhas
    int i = 0;
    const char* start = text;
    p = text;

    while (*p) {
        if (*p == '\n') {
            size_t len = p - start;
            lines[i] = (char*)safe_malloc(len + 1);
            strncpy(lines[i], start, len);
            lines[i][len] = '\0';
            i++;
            start = p + 1;
        }
        p++;
    }

    // Última linha
    if (start < p) {
        size_t len = p - start;
        lines[i] = (char*)safe_malloc(len + 1);
        strncpy(lines[i], start, len);
        lines[i][len] = '\0';
    }

    return lines;
}

void str_free_lines(char** lines, int line_count) {
    if (!lines) return;
    for (int i = 0; i < line_count; i++) {
        safe_free(lines[i]);
    }
    safe_free(lines);
}

char* str_trim(char* str) {
    if (!str) return NULL;

    // Trim início
    while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) {
        str++;
    }

    // Trim fim
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }

    return str;
}

// Funções de tempo
long time_get_unix(void) {
    return (long)time(NULL);
}

char* time_format(long timestamp) {
    static char buffer[64];
    struct tm* tm_info = localtime(&timestamp);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}

// Funções de memória
void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "Failed to allocate %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "Failed to reallocate %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

void safe_free(void* ptr) {
    free(ptr);
}

// Logging
void log_message(LogLevel level, const char* format, ...) {
    if (level < current_log_level) return;

    const char* level_str[] = {"DEBUG", "INFO", "WARNING", "ERROR"};

    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(stderr, "[%s] [%s] ", time_str, level_str[level]);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");
}

void log_set_level(LogLevel level) {
    current_log_level = level;
}