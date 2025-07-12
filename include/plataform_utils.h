#ifndef PLATFORM_UTILS_H
#define PLATFORM_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #include <io.h>
    #define PATH_SEPARATOR "\\"
    #define mkdir(path, mode) _mkdir(path)
    #define access _access
    #define F_OK 0
    #define W_OK 2
    #define R_OK 4
#else
    #include <unistd.h>
    #include <pwd.h>
    #define PATH_SEPARATOR "/"
#endif

// Estrutura para informações de plataforma
typedef struct {
    int is_wsl;
    int is_windows;
    int is_linux;
    int is_admin;
    char home_dir[512];
    char config_dir[512];
} PlatformInfo;

// Funções de plataforma
PlatformInfo* platform_get_info(void);
void platform_free_info(PlatformInfo* info);
int platform_is_wsl(void);
int platform_is_admin(void);
char* platform_get_home_dir(void);
char* platform_get_config_dir(const char* app_name);
int platform_create_directory(const char* path);
int platform_ensure_permissions(const char* path);
char* platform_normalize_path(const char* path);
int platform_can_write(const char* path);

// Funções auxiliares para WSL
char* wsl_to_windows_path(const char* wsl_path);
char* windows_to_wsl_path(const char* windows_path);
int wsl_use_windows_directory(void);

#endif // PLATFORM_UTILS_H