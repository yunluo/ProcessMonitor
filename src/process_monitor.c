#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <time.h>
#include <direct.h>
#include <ctype.h>

#define VERSION "1.3.0.0"
#define VERSION_MAJOR 1
#define VERSION_MINOR 3
#define VERSION_PATCH 0
#define VERSION_BUILD 0

void get_version_string(char* buffer, size_t size) {
    int len = snprintf(buffer, size, "%d.%d.%d.%d",
             VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD);
    if (len < 0 || (size_t)len >= size) {
        buffer[size - 1] = '\0';
    }
}

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#define MAX_PATH_LENGTH 512
#define MAX_CMD_LENGTH 1024
#define MAX_LOG_LENGTH 1024
#define MAX_SECTION_NAME 64
#define MAX_LOG_SIZE 1024*1024
#define MAX_LOG_BACKUPS 20
#define MAX_CONFIGS 50
#define MAX_SECTIONS 100

typedef struct {
    char section_name[MAX_SECTION_NAME];
    char process_name[MAX_PATH_LENGTH];
    char command[MAX_CMD_LENGTH];
    char working_dir[MAX_PATH_LENGTH];
    int enabled;
} ProgramConfig;

char ini_file_path[MAX_PATH_LENGTH];
char log_file_path[MAX_PATH_LENGTH];

void get_current_time_str(char* buffer, size_t size);
void write_log(const char* format, ...);
void rotate_log_file();
int parse_ini_config(const char* ini_file, ProgramConfig* configs, int max_configs);
int is_process_running(const char* process_name);
int start_process(const char* command, const char* working_dir);
void get_exe_directory(char* buffer, size_t size);
void get_default_ini_path(char* buffer, size_t size);
void create_log_directory(const char* log_path);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Suppress unused parameter warnings
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    int argc = 0;
    char** argv = NULL;
    int result = 0;

    size_t cmdline_len = strlen(lpCmdLine);
    if (cmdline_len > 0) {
        argc = 1;

        int in_quotes = 0;
        for (size_t i = 0; i < cmdline_len; i++) {
            if (lpCmdLine[i] == '"') {
                in_quotes = !in_quotes;
            } else if (lpCmdLine[i] == ' ' && !in_quotes) {
                argc++;
            }
        }

        argv = (char**)malloc(argc * sizeof(char*));
        if (argv == NULL) {
            return 1;
        }

        argv[0] = (char*)malloc(MAX_PATH_LENGTH);
        if (argv[0] == NULL) {
            free(argv);
            return 1;
        }
        GetModuleFileNameA(NULL, argv[0], MAX_PATH_LENGTH);

        char* cmd_copy = _strdup(lpCmdLine);
        if (cmd_copy == NULL) {
            free(argv[0]);
            free(argv);
            return 1;
        }

        int i = 1;
        char* token = cmd_copy;
        in_quotes = 0;

        while (*token && i < argc) {
            // Skip leading spaces
            while (*token == ' ') token++;
            if (!*token) break;

            char* start = token;
            int in_quotes = 0;

            // Check for opening quote
            if (*token == '"') {
                in_quotes = 1;
                start = ++token;
            }

            // Find end of token
            while (*token) {
                if (in_quotes) {
                    if (*token == '"') {
                        break;
                    }
                } else {
                    if (*token == ' ') {
                        break;
                    }
                }
                token++;
            }

            size_t len = token - start;
            argv[i] = (char*)malloc(len + 1);
            if (argv[i] == NULL) {
                for (int j = 0; j < i; j++) {
                    free(argv[j]);
                }
                free(argv);
                free(cmd_copy);
                return 1;
            }

            strncpy(argv[i], start, len);
            argv[i][len] = '\0';

            // Skip closing quote if present
            if (*token == '"') token++;
            // Skip space separator
            if (*token == ' ') token++;
            i++;
        }

        free(cmd_copy);
    } else {
        argc = 1;
        argv = (char**)malloc(argc * sizeof(char*));
        if (argv == NULL) {
            return 1;
        }

        argv[0] = (char*)malloc(MAX_PATH_LENGTH);
        if (argv[0] == NULL) {
            free(argv);
            return 1;
        }
        GetModuleFileNameA(NULL, argv[0], MAX_PATH_LENGTH);
    }

    get_exe_directory(log_file_path, sizeof(log_file_path));
    strncat(log_file_path, "\\log", sizeof(log_file_path) - strlen(log_file_path) - 1);
    create_log_directory(log_file_path);
    strncat(log_file_path, "\\process_monitor.log", sizeof(log_file_path) - strlen(log_file_path) - 1);

    rotate_log_file();

    char version_str[32];
    get_version_string(version_str, sizeof(version_str));
    write_log("=== Process Monitor Started (Version: %s) ===", version_str);

    if (argc > 1) {
        strncpy(ini_file_path, argv[1], sizeof(ini_file_path) - 1);
        ini_file_path[sizeof(ini_file_path) - 1] = '\0';
    } else {
        get_default_ini_path(ini_file_path, sizeof(ini_file_path));
    }

    write_log("Using INI file: %s", ini_file_path);

    if (GetFileAttributesA(ini_file_path) == INVALID_FILE_ATTRIBUTES) {
        write_log("Error: INI file not found!");
        result = 1;
        goto cleanup;
    }

    ProgramConfig configs[MAX_CONFIGS];
    int config_count = parse_ini_config(ini_file_path, configs, 50);

    if (config_count <= 0) {
        write_log("Error: No valid configurations found in INI file");
        result = 1;
        goto cleanup;
    }

    write_log("Found %d program configurations", config_count);

    int start_failure_count = 0;

    for (int i = 0; i < config_count; i++) {
        if (!configs[i].enabled) {
            write_log("Skipping disabled program: %s", configs[i].section_name);
            continue;
        }

        write_log("Checking program: %s", configs[i].section_name);

        if (is_process_running(configs[i].process_name)) {
            write_log("Process '%s' is already running, skipping start", configs[i].process_name);
        } else {
            write_log("Process '%s' is not running, starting...", configs[i].process_name);

            if (start_process(configs[i].command, configs[i].working_dir)) {
                write_log("Successfully started process: %s", configs[i].process_name);
            } else {
                write_log("Failed to start process: %s", configs[i].process_name);
                start_failure_count++;
            }
        }
    }

    if (start_failure_count > 0) {
        write_log("Warning: %d process(es) failed to start", start_failure_count);
        result = 1;
    }

    write_log("=== Process Monitor Finished ===");

cleanup:
    if (argv != NULL) {
        for (int i = 0; i < argc; i++) {
            if (argv[i] != NULL) {
                free(argv[i]);
            }
        }
        free(argv);
    }

    return result;
}

void get_current_time_str(char* buffer, size_t size) {
    time_t rawtime;
    struct tm* timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    if (timeinfo) {
        strftime(buffer, size, "%Y-%m-%d %H:%M:%S", timeinfo);
    } else {
        strncpy(buffer, "1970-01-01 00:00:00", size - 1);
        buffer[size - 1] = '\0';
    }
}

void rotate_log_file() {
    if (GetFileAttributesA(log_file_path) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (!GetFileAttributesExA(log_file_path, GetFileExInfoStandard, &fileData)) {
        write_log("Warning: Failed to get log file attributes");
        return;
    }

    ULARGE_INTEGER fileSize;
    fileSize.LowPart = fileData.nFileSizeLow;
    fileSize.HighPart = fileData.nFileSizeHigh;

    if (fileSize.QuadPart > MAX_LOG_SIZE) {
        char backup_path[MAX_PATH_LENGTH];
        char old_path[MAX_PATH_LENGTH];
        char new_path[MAX_PATH_LENGTH];

        if (snprintf(backup_path, sizeof(backup_path), "%s.%d", log_file_path, MAX_LOG_BACKUPS) >= (int)sizeof(backup_path)) {
            write_log("Warning: Backup path too long, skipping rotation");
            return;
        }
        if (GetFileAttributesA(backup_path) != INVALID_FILE_ATTRIBUTES) {
            if (!DeleteFileA(backup_path)) {
                write_log("Warning: Failed to delete oldest log backup");
            }
        }

        for (int i = MAX_LOG_BACKUPS - 1; i >= 1; i--) {
            if (snprintf(old_path, sizeof(old_path), "%s.%d", log_file_path, i) >= (int)sizeof(old_path) ||
                snprintf(new_path, sizeof(new_path), "%s.%d", log_file_path, i + 1) >= (int)sizeof(new_path)) {
                write_log("Warning: Path too long during log rotation");
                return;
            }

            if (GetFileAttributesA(old_path) != INVALID_FILE_ATTRIBUTES) {
                DeleteFileA(new_path);
                if (!MoveFileA(old_path, new_path)) {
                    write_log("Warning: Failed to rename log backup %d to %d", i, i + 1);
                }
            }
        }

        char first_backup[MAX_PATH_LENGTH];
        if (snprintf(first_backup, sizeof(first_backup), "%s.1", log_file_path) >= (int)sizeof(first_backup)) {
            write_log("Warning: First backup path too long");
            return;
        }
        DeleteFileA(first_backup);
        if (!MoveFileA(log_file_path, first_backup)) {
            write_log("Warning: Failed to rotate current log file");
        }
    }
}

void write_log(const char* format, ...) {
    FILE* file = fopen(log_file_path, "a");
    if (file == NULL) {
        // Retry after creating directory
        char log_dir[MAX_PATH_LENGTH];
        strncpy(log_dir, log_file_path, sizeof(log_dir) - 1);
        log_dir[sizeof(log_dir) - 1] = '\0';
        char* last_backslash = strrchr(log_dir, '\\');
        if (last_backslash) {
            *last_backslash = '\0';
            create_log_directory(log_dir);
            file = fopen(log_file_path, "a");
        }
        if (file == NULL) {
            return;
        }
    }

    char time_str[32];
    char log_line[MAX_LOG_LENGTH];
    va_list args;

    get_current_time_str(time_str, sizeof(time_str));

    va_start(args, format);
    int len = _vsnprintf(log_line, sizeof(log_line) - 1, format, args);
    log_line[sizeof(log_line) - 1] = '\0';
    if (len < 0 || (size_t)len >= sizeof(log_line)) {
        log_line[sizeof(log_line) - 2] = '.';
        log_line[sizeof(log_line) - 3] = '.';
        log_line[sizeof(log_line) - 4] = '.';
    }
    va_end(args);

    fprintf(file, "[%s] %s\n", time_str, log_line);
    fclose(file);
}

void get_exe_directory(char* buffer, size_t size) {
    GetModuleFileNameA(NULL, buffer, (DWORD)size);
    char* last_slash = strrchr(buffer, '\\');
    if (last_slash) {
        *last_slash = '\0';
    }
}

void create_log_directory(const char* log_path) {
    DWORD attrs = GetFileAttributesA(log_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        // Directory doesn't exist, try to create it
        if (!CreateDirectoryA(log_path, NULL)) {
            // Failed to create directory, silently ignore
            // (log file write will fail and retry later)
        }
    } else if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        // Path exists but is not a directory
    }
    // If attrs has FILE_ATTRIBUTE_DIRECTORY, directory already exists - that's fine
}

static char* skip_bom(char* content, DWORD fileSize) {
    if (fileSize >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) {
        return content + 3;
    }
    return content;
}

static void trim_whitespace(char* str) {
    if (!str) return;
    char* end;
    while (*str == ' ' || *str == '\t') str++;
    if (*str == '\0') return;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end--;
    *(end + 1) = '\0';
}

static int get_ini_value(char* content, const char* section, const char* key, char* value, int value_size) {
    char* p = content;
    char current_section[256] = {0};
    size_t key_len = strlen(key);

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            p++;
            char* section_end = strchr(p, ']');
            if (section_end) {
                size_t section_len = section_end - p;
                if (section_len < sizeof(current_section)) {
                    strncpy(current_section, p, section_len);
                    current_section[section_len] = '\0';
                }
                p = section_end + 1;
            } else {
                while (*p && *p != '\r' && *p != '\n') p++;
                if (*p) p++;
            }
        } else if (strcmp(current_section, section) == 0) {
            if (*p == '\0' || *p == '\r' || *p == '\n') {
                p++;
                continue;
            }
            if (_strnicmp(p, key, key_len) == 0) {
                p += key_len;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '=') {
                    p++;
                    while (*p == ' ' || *p == '\t') p++;
                    int i = 0;
                    while (*p && *p != '\r' && *p != '\n' && *p != ';' && i < value_size - 1) {
                        value[i++] = *p++;
                    }
                    value[i] = '\0';
                    trim_whitespace(value);
                    return 1;
                }
            }
            while (*p && *p != '\r' && *p != '\n') p++;
        } else {
            while (*p && *p != '\r' && *p != '\n') p++;
        }
        if (*p) p++;
    }
    return 0;
}

int parse_ini_config(const char* ini_file, ProgramConfig* configs, int max_configs) {
    HANDLE hFile = CreateFileA(ini_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return 0;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return 0;
    }

    if (fileSize == 0) {
        CloseHandle(hFile);
        return 0;
    }

    char* fileContent = (char*)malloc(fileSize + 1);
    if (!fileContent) {
        write_log("Error: Failed to allocate memory for INI file parsing");
        CloseHandle(hFile);
        return 0;
    }

    DWORD bytesRead;
    if (!ReadFile(hFile, fileContent, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        free(fileContent);
        CloseHandle(hFile);
        return 0;
    }
    CloseHandle(hFile);
    fileContent[fileSize] = '\0';

    char* content = skip_bom(fileContent, fileSize);

    char section_names[8192] = {0};
    char* p_section = section_names;
    char* section_names_end = section_names + sizeof(section_names);
    int config_count = 0;
    char* p = content;
    int section_total = 0;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            p++;
            char* section_end = strchr(p, ']');
            if (section_end) {
                size_t len = section_end - p;
                if (len < MAX_SECTION_NAME && section_total < MAX_SECTIONS && p_section + len + 1 < section_names_end) {
                    strncpy(p_section, p, len);
                    p_section[len] = '\0';
                    // Trim whitespace from section name
                    trim_whitespace(p_section);
                    size_t trimmed_len = strlen(p_section);
                    if (trimmed_len > 0) {
                        p_section += trimmed_len + 1;
                        section_total++;
                    }
                } else {
                    // Section name too long or buffer full
                    char warn_buf[128];
                    char warn_msg[160];
                    if (len >= MAX_SECTION_NAME) {
                        snprintf(warn_buf, sizeof(warn_buf), "Section name too long (%zu chars, max %d)", len, MAX_SECTION_NAME - 1);
                    } else {
                        snprintf(warn_buf, sizeof(warn_buf), "Too many sections or buffer full");
                    }
                    snprintf(warn_msg, sizeof(warn_msg), "Warning: Skipping section at offset %zu: %s", (size_t)(p - content), warn_buf);
                    write_log(warn_msg);
                }
            }
            p = section_end ? section_end + 1 : p;
        }
        while (*p && *p != '\r' && *p != '\n') p++;
        if (*p) p++;
    }

    config_count = 0;
    p_section = section_names;

    while (*p_section && config_count < max_configs) {
        if (*p_section == '\0') {
            p_section++;
            continue;
        }

        char enabled_str[8] = "1";
        char process_name[MAX_PATH_LENGTH] = {0};
        char command[MAX_CMD_LENGTH] = {0};
        char working_dir[MAX_PATH_LENGTH] = {0};

        if (!get_ini_value(content, p_section, "enabled", enabled_str, sizeof(enabled_str))) {
            strcpy(enabled_str, "1");
        }

        if (strcmp(enabled_str, "1") == 0) {
            get_ini_value(content, p_section, "process_name", process_name, sizeof(process_name));
            get_ini_value(content, p_section, "command", command, sizeof(command));
            get_ini_value(content, p_section, "working_dir", working_dir, sizeof(working_dir));

            size_t cmd_len = strlen(command);
            if (cmd_len > 0) {
                size_t proc_len = strlen(process_name);
                size_t work_len = strlen(working_dir);

                if (proc_len == 0) {
                    const char* cmd_start = command;
                    if (cmd_start[0] == '"') {
                        const char* end_quote = strchr(cmd_start + 1, '"');
                        if (end_quote) {
                            cmd_start = command + 1;
                        }
                    }

                    const char* last_slash = strrchr(cmd_start, '\\');
                    const char* last_slash2 = strrchr(cmd_start, '/');
                    const char* exe_start = cmd_start;

                    if (last_slash && last_slash2) {
                        exe_start = (last_slash > last_slash2) ? last_slash + 1 : last_slash2 + 1;
                    } else if (last_slash) {
                        exe_start = last_slash + 1;
                    } else if (last_slash2) {
                        exe_start = last_slash2 + 1;
                    }

                    // Find end of executable token (space or end of string)
                    const char* exe_end = exe_start;
                    while (*exe_end && *exe_end != ' ' && *exe_end != '\t') {
                        exe_end++;
                    }

                    size_t exe_len = exe_end - exe_start;
                    if (exe_len > 0 && exe_len < sizeof(process_name)) {
                        strncpy(process_name, exe_start, exe_len);
                        process_name[exe_len] = '\0';
                    }
                }

                if (work_len == 0) {
                    const char* cmd_start = command;
                    if (cmd_start[0] == '"') {
                        const char* end_quote = strchr(cmd_start + 1, '"');
                        if (end_quote) {
                            cmd_start = command + 1;
                        }
                    }

                    const char* last_slash = strrchr(cmd_start, '\\');
                    const char* last_slash2 = strrchr(cmd_start, '/');
                    const char* dir_end = NULL;

                    if (last_slash && last_slash2) {
                        dir_end = (last_slash > last_slash2) ? last_slash : last_slash2;
                    } else if (last_slash) {
                        dir_end = last_slash;
                    } else if (last_slash2) {
                        dir_end = last_slash2;
                    }

                    if (dir_end) {
                        size_t dir_len = dir_end - cmd_start;
                        if (dir_len > 0 && dir_len < sizeof(working_dir)) {
                            strncpy(working_dir, cmd_start, dir_len);
                            working_dir[dir_len] = '\0';
                        }
                    }
                }
            }

            if (strlen(command) > 0) {
                strncpy(configs[config_count].section_name, p_section, sizeof(configs[config_count].section_name) - 1);
                configs[config_count].section_name[sizeof(configs[config_count].section_name) - 1] = '\0';
                strncpy(configs[config_count].process_name, process_name, sizeof(configs[config_count].process_name) - 1);
                configs[config_count].process_name[sizeof(configs[config_count].process_name) - 1] = '\0';
                strncpy(configs[config_count].command, command, sizeof(configs[config_count].command) - 1);
                configs[config_count].command[sizeof(configs[config_count].command) - 1] = '\0';
                strncpy(configs[config_count].working_dir, working_dir, sizeof(configs[config_count].working_dir) - 1);
                configs[config_count].working_dir[sizeof(configs[config_count].working_dir) - 1] = '\0';
                configs[config_count].enabled = 1;

                config_count++;

                write_log("Loaded config: [%s] process='%s', command='%s', working_dir='%s'",
                         p_section, process_name, command, working_dir);
            } else {
                write_log("Warning: Invalid config in section [%s] - missing required 'command' parameter", p_section);
            }
        } else {
            write_log("Skipping disabled config section: [%s]", p_section);
        }

        p_section += strlen(p_section) + 1;
    }

    free(fileContent);
    return config_count;
}

int is_process_running(const char* process_name) {
    if (!process_name || !*process_name) {
        return 0;
    }

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    char name_buf[MAX_PATH];
    char exe_name_buf[MAX_PATH];
    size_t name_len = strlen(process_name);

    strncpy(name_buf, process_name, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';

    if (name_len >= sizeof(name_buf)) {
        CloseHandle(hSnapshot);
        return 0;
    }

    char* last_slash = strrchr(name_buf, '\\');
    char* last_slash2 = strrchr(name_buf, '/');
    if (last_slash && last_slash2) {
        last_slash = (last_slash > last_slash2) ? last_slash : last_slash2;
    } else if (!last_slash) {
        last_slash = last_slash2;
    }
    if (last_slash) {
        strcpy(exe_name_buf, last_slash + 1);
    } else {
        strcpy(exe_name_buf, name_buf);
    }

    size_t exe_name_len = strlen(exe_name_buf);
    if (exe_name_len < 4 || _strnicmp(exe_name_buf + exe_name_len - 4, ".exe", 4) != 0) {
        if (exe_name_len + 4 < sizeof(exe_name_buf)) {
            strcpy(exe_name_buf + exe_name_len, ".exe");
            exe_name_len += 4;
        }
    }

    if (exe_name_len >= sizeof(name_buf)) {
        CloseHandle(hSnapshot);
        return 0;
    }

    PROCESSENTRY32 pe32;
    ZeroMemory(&pe32, sizeof(pe32));
    pe32.dwSize = sizeof(pe32);

    if (!Process32First(hSnapshot, &pe32)) {
        CloseHandle(hSnapshot);
        return 0;
    }

    int found = 0;

    do {
        if (_stricmp(pe32.szExeFile, exe_name_buf) == 0) {
            found = 1;
            break;
        }

        char* sz_exe_name = strrchr(pe32.szExeFile, '\\');
        if (sz_exe_name) {
            if (_stricmp(sz_exe_name + 1, exe_name_buf) == 0) {
                found = 1;
                break;
            }
        }
    } while (Process32Next(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    return found;
}

int start_process(const char* command, const char* working_dir) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    if (!command || strlen(command) == 0) {
        write_log("Error: Invalid command parameter");
        return 0;
    }

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char* cmd_copy = _strdup(command);
    if (cmd_copy == NULL) {
        write_log("Error: Failed to allocate memory for command");
        return 0;
    }

    const char* work_dir = NULL;
    static char full_working_dir[MAX_PATH_LENGTH];

    if (working_dir && strlen(working_dir) > 0) {
        // First check if working_dir is an absolute path that exists
        DWORD attrs = GetFileAttributesA(working_dir);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            work_dir = working_dir;
        } else {
            // Try joining with exe directory for relative paths
            get_exe_directory(full_working_dir, sizeof(full_working_dir));

            // Ensure exe_dir ends with backslash
            size_t exe_dir_len = strlen(full_working_dir);
            if (exe_dir_len > 0 && full_working_dir[exe_dir_len - 1] != '\\') {
                strncat(full_working_dir, "\\", sizeof(full_working_dir) - exe_dir_len - 1);
            }

            // Check if working_dir starts with a slash (already absolute-ish)
            if (working_dir[0] == '\\' || working_dir[0] == '/') {
                // Absolute path - just use it directly after drive letter
                size_t total_len = strlen(full_working_dir) + strlen(working_dir);
                if (total_len < sizeof(full_working_dir)) {
                    strncat(full_working_dir, working_dir + 1, sizeof(full_working_dir) - strlen(full_working_dir) - 1);
                }
            } else {
                // Relative path - append directly
                size_t total_len = strlen(full_working_dir) + strlen(working_dir);
                if (total_len + 1 <= sizeof(full_working_dir)) {
                    strncat(full_working_dir, working_dir, sizeof(full_working_dir) - strlen(full_working_dir) - 1);
                } else {
                    write_log("Warning: Path too long for working directory '%s'", working_dir);
                }
            }

            DWORD full_attrs = GetFileAttributesA(full_working_dir);
            if (full_attrs != INVALID_FILE_ATTRIBUTES && (full_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                work_dir = full_working_dir;
            } else {
                write_log("Warning: Working directory '%s' (tried '%s') does not exist, using current directory",
                          working_dir, full_working_dir);
            }
        }
    }

    DWORD creation_flags = CREATE_NO_WINDOW;

    if (!CreateProcessA(NULL, cmd_copy, NULL, NULL, FALSE, creation_flags, NULL, work_dir, &si, &pi)) {
        DWORD error = GetLastError();
        write_log("Error: Failed to start process. Error code: %lu, command: '%s'", error, command);
        free(cmd_copy);
        return 0;
    }

    if (pi.hProcess != NULL) {
        CloseHandle(pi.hProcess);
    }
    if (pi.hThread != NULL) {
        CloseHandle(pi.hThread);
    }

    free(cmd_copy);

    return 1;
}

void get_default_ini_path(char* buffer, size_t size) {
    GetModuleFileNameA(NULL, buffer, (DWORD)size);
    char* last_dot = strrchr(buffer, '.');
    char* last_slash = strrchr(buffer, '\\');
    
    if (last_dot && (!last_slash || last_dot > last_slash)) {
        *last_dot = '\0';
        strncat(buffer, ".ini", size - strlen(buffer) - 1);
    } else {
        strncat(buffer, ".ini", size - strlen(buffer) - 1);
    }
}
