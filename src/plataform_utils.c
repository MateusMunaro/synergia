#include "plataform_utils.h"
#include "utils.h"
#include <errno.h>
#include <ctype.h>

#ifdef __linux__
    #include <sys/utsname.h>
#endif

// Detecta se está rodando no WSL
int platform_is_wsl(void) {
    #ifdef __linux__
        FILE* fp = fopen("/proc/version", "r");
        if (fp) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), fp)) {
                fclose(fp);
                // WSL1 e WSL2 contêm "Microsoft" ou "WSL" no /proc/version
                if (strstr(buffer, "Microsoft") || strstr(buffer, "WSL")) {
                    return 1;
                }
            } else {
                fclose(fp);
            }
        }

        // Verificação adicional: checar se existe /mnt/c
        struct stat st;
        if (stat("/mnt/c", &st) == 0 && S_ISDIR(st.st_mode)) {
            return 1;
        }
    #endif
    return 0;
}

// Verifica se tem privilégios administrativos
int platform_is_admin(void) {
    #ifdef _WIN32
        BOOL is_admin = FALSE;
        HANDLE token = NULL;

        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION elevation;
            DWORD size = sizeof(TOKEN_ELEVATION);
            if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
                is_admin = elevation.TokenIsElevated;
            }
            CloseHandle(token);
        }
        return is_admin;
    #else
        return (geteuid() == 0);
    #endif
}

// Obtém o diretório home do usuário
char* platform_get_home_dir(void) {
    static char home_dir[512];

    #ifdef _WIN32
        char* userprofile = getenv("USERPROFILE");
        if (userprofile) {
            strncpy(home_dir, userprofile, sizeof(home_dir) - 1);
            home_dir[sizeof(home_dir) - 1] = '\0';
            return home_dir;
        }
    #else
        char* home = getenv("HOME");
        if (home) {
            strncpy(home_dir, home, sizeof(home_dir) - 1);
            home_dir[sizeof(home_dir) - 1] = '\0';
            return home_dir;
        }

        // Fallback: usar getpwuid
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            strncpy(home_dir, pw->pw_dir, sizeof(home_dir) - 1);
            home_dir[sizeof(home_dir) - 1] = '\0';
            return home_dir;
        }
    #endif

    return NULL;
}

// Obtém diretório de configuração apropriado para a plataforma
char* platform_get_config_dir(const char* app_name) {
    static char config_dir[512];

    #ifdef _WIN32
        char* appdata = getenv("APPDATA");
        if (appdata) {
            snprintf(config_dir, sizeof(config_dir), "%s\\%s", appdata, app_name);
            return config_dir;
        }
    #else
        char* home = platform_get_home_dir();
        if (home) {
            // No Linux, usar ~/.config/app_name
            snprintf(config_dir, sizeof(config_dir), "%s/.config/%s", home, app_name);
            return config_dir;
        }
    #endif

    return NULL;
}

// Cria diretório com tratamento especial para WSL
int platform_create_directory(const char* path) {
    if (!path) return -1;

    // Se estiver no WSL e o path começar com /mnt/, tentar usar permissões do Windows
    if (platform_is_wsl() && strncmp(path, "/mnt/", 5) == 0) {
        // No WSL, diretórios em /mnt/ podem ter problemas de permissão
        // Tentar criar com umask mais permissivo
        mode_t old_mask = umask(0);
        int result = mkdir(path, 0777);
        umask(old_mask);

        if (result != 0 && errno != EEXIST) {
            // Se falhar, tentar criar no sistema de arquivos Linux nativo
            log_message(LOG_WARNING, "Failed to create directory in /mnt/, errno=%d", errno);
            return -1;
        }
        return 0;
    }

    // Criação normal
    #ifdef _WIN32
        int result = mkdir(path);
    #else
        int result = mkdir(path, 0755);
    #endif

    if (result != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

// Garante que temos permissões adequadas
int platform_ensure_permissions(const char* path) {
    if (!path) return -1;

    #ifndef _WIN32
        struct stat st;
        if (stat(path, &st) == 0) {
            // Se somos o dono, garantir permissões de escrita
            if (st.st_uid == getuid()) {
                if (chmod(path, st.st_mode | S_IWUSR | S_IXUSR) != 0) {
                    log_message(LOG_WARNING, "Failed to ensure permissions on %s", path);
                    return -1;
                }
            }
        }
    #endif

    return 0;
}

// Converte path WSL para Windows (ex: /mnt/c/... -> C:\...)
char* wsl_to_windows_path(const char* wsl_path) {
    static char windows_path[512];

    if (!wsl_path || strncmp(wsl_path, "/mnt/", 5) != 0) {
        return NULL;
    }

    // Extrair letra do drive
    char drive = tolower(wsl_path[5]);
    if (drive < 'a' || drive > 'z' || wsl_path[6] != '/') {
        return NULL;
    }

    // Construir path Windows
    snprintf(windows_path, sizeof(windows_path), "%c:\\%s",
             toupper(drive), wsl_path + 7);

    // Substituir / por
    for (char* p = windows_path; *p; p++) {
    if (*p == '/') *p = '\\';
    }

    return windows_path;
}

// Verifica se podemos escrever em um diretório
int platform_can_write(const char* path) {
    if (!path) return 0;

    #ifdef _WIN32
        return _access(path, W_OK) == 0;
    #else
        // No Linux/WSL, fazer teste mais robusto
        if (access(path, W_OK) == 0) {
            return 1;
        }

        // Se falhar, tentar criar um arquivo temporário
        char test_file[512];
        snprintf(test_file, sizeof(test_file), "%s/.myvc_test_%d", path, getpid());

        FILE* fp = fopen(test_file, "w");
        if (fp) {
            fclose(fp);
            unlink(test_file);
            return 1;
        }
    #endif

    return 0;
}

// Obtém informações completas da plataforma
PlatformInfo* platform_get_info(void) {
    PlatformInfo* info = (PlatformInfo*)safe_malloc(sizeof(PlatformInfo));
    memset(info, 0, sizeof(PlatformInfo));

    #ifdef _WIN32
        info->is_windows = 1;
    #else
        info->is_linux = 1;
        info->is_wsl = platform_is_wsl();
    #endif

    info->is_admin = platform_is_admin();

    char* home = platform_get_home_dir();
    if (home) {
        strncpy(info->home_dir, home, sizeof(info->home_dir) - 1);
    }

    char* config = platform_get_config_dir("myvc");
    if (config) {
        strncpy(info->config_dir, config, sizeof(info->config_dir) - 1);
    }

    return info;
}

void platform_free_info(PlatformInfo* info) {
    safe_free(info);
}

// Normaliza caminhos para a plataforma atual
char* platform_normalize_path(const char* path) {
    static char normalized[512];

    if (!path) return NULL;

    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';

    #ifdef _WIN32
        // Converter / para \ no Windows
        for (char* p = normalized; *p; p++) {
            if (*p == '/') *p = '\\';
        }
    #else
        // Converter \ para / no Linux
        for (char* p = normalized; *p; p++) {
            if (*p == '\\') *p = '/';
        }
    #endif

    return normalized;
}

// Verifica se devemos usar diretório Windows no WSL
int wsl_use_windows_directory(void) {
    if (!platform_is_wsl()) return 0;

    // Verificar se estamos em /mnt/
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd))) {
        if (strncmp(cwd, "/mnt/", 5) == 0) {
            // Estamos em um diretório Windows montado
            // Verificar se temos permissões
            if (!platform_can_write(".")) {
                log_message(LOG_WARNING, "No write permission in Windows mount");
                return 0;
            }
            return 1;
        }
    }

    return 0;
}