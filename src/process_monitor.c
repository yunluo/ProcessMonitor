#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <time.h>
#include <direct.h>
#include <ctype.h>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#define MAX_PATH_LENGTH 512
#define MAX_CMD_LENGTH 1024
#define MAX_LOG_LENGTH 1024
#define MAX_SECTION_NAME 64
#define MAX_LOG_SIZE 1024*1024
#define MAX_LOG_BACKUPS 20

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
    int argc = 0;
    char** argv = NULL;
    int result = 0;

    if (strlen(lpCmdLine) > 0) {
        argc = 1;

        int in_quotes = 0;
        for (size_t i = 0; i < strlen(lpCmdLine); i++) {
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
            while (*token == ' ') token++;

            if (!*token) break;

            char* start = token;
            char delimiter = ' ';

            if (*token == '"') {
                in_quotes = 1;
                delimiter = '"';
                start = ++token;
            }

            while (*token && (*token != delimiter || (delimiter == '"' && !in_quotes))) {
                if (*token == '"' && delimiter == ' ') {
                    in_quotes = !in_quotes;
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

            if (*token) token++;

            i++;
            in_quotes = 0;
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
    strcat(log_file_path, "\\log");
    create_log_directory(log_file_path);
    strcat(log_file_path, "\\process_monitor.log");

    rotate_log_file();

    write_log("=== Process Monitor Started ===");

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

    ProgramConfig configs[32];
    int config_count = parse_ini_config(ini_file_path, configs, 32);

    if (config_count <= 0) {
        write_log("Error: No valid configurations found in INI file");
        result = 1;
        goto cleanup;
    }

    write_log("Found %d program configurations", config_count);

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
            }
        }
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

        snprintf(backup_path, sizeof(backup_path), "%s.%d", log_file_path, MAX_LOG_BACKUPS);
        if (GetFileAttributesA(backup_path) != INVALID_FILE_ATTRIBUTES) {
            if (!DeleteFileA(backup_path)) {
                write_log("Warning: Failed to delete oldest log backup");
            }
        }

        for (int i = MAX_LOG_BACKUPS - 1; i >= 1; i--) {
            snprintf(old_path, sizeof(old_path), "%s.%d", log_file_path, i);
            snprintf(new_path, sizeof(new_path), "%s.%d", log_file_path, i + 1);

            if (GetFileAttributesA(old_path) != INVALID_FILE_ATTRIBUTES) {
                DeleteFileA(new_path);
                if (!MoveFileA(old_path, new_path)) {
                    write_log("Warning: Failed to rename log backup %d to %d", i, i + 1);
                }
            }
        }

        char first_backup[MAX_PATH_LENGTH];
        snprintf(first_backup, sizeof(first_backup), "%s.1", log_file_path);
        DeleteFileA(first_backup);
        if (!MoveFileA(log_file_path, first_backup)) {
            write_log("Warning: Failed to rotate current log file");
        }
    }
}

void write_log(const char* format, ...) {
    FILE* file;
    char time_str[32];
    char log_line[MAX_LOG_LENGTH];
    va_list args;

    file = fopen(log_file_path, "a");
    if (file == NULL) {
        return;
    }

    get_current_time_str(time_str, sizeof(time_str));

    va_start(args, format);
    _vsnprintf(log_line, sizeof(log_line), format, args);
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
    CreateDirectoryA(log_path, NULL);
}

int parse_ini_config(const char* ini_file, ProgramConfig* configs, int max_configs) {
    char section_names[8192] = {0};
    char* p_section = section_names;
    int config_count = 0;

    if (GetPrivateProfileSectionNamesA(section_names, sizeof(section_names), ini_file) == 0) {
        return 0;
    }

    while (*p_section && config_count < max_configs) {
        char enabled_str[8] = {0};
        char process_name[MAX_PATH_LENGTH] = {0};
        char command[MAX_CMD_LENGTH] = {0};
        char working_dir[MAX_PATH_LENGTH] = {0};

        GetPrivateProfileStringA(p_section, "enabled", "1", enabled_str, sizeof(enabled_str), ini_file);

        if (strcmp(enabled_str, "1") == 0) {
            GetPrivateProfileStringA(p_section, "process_name", "", process_name, sizeof(process_name), ini_file);
            GetPrivateProfileStringA(p_section, "command", "", command, sizeof(command), ini_file);
            GetPrivateProfileStringA(p_section, "working_dir", "", working_dir, sizeof(working_dir), ini_file);

            if (strlen(command) > 0) {
                if (strlen(process_name) == 0) {
                    char command_copy[MAX_CMD_LENGTH];
                    strncpy(command_copy, command, sizeof(command_copy) - 1);
                    command_copy[sizeof(command_copy) - 1] = '\0';

                    char* cmd_start = command_copy;
                    if (cmd_start[0] == '"') {
                        cmd_start++;
                        char* end_quote = strchr(cmd_start, '"');
                        if (end_quote) {
                            *end_quote = '\0';
                        }
                    }

                    char* last_slash = strrchr(cmd_start, '\\');
                    char* last_slash2 = strrchr(cmd_start, '/');
                    char* exe_start = NULL;

                    if (last_slash && last_slash2) {
                        exe_start = (last_slash > last_slash2) ? last_slash + 1 : last_slash2 + 1;
                    } else if (last_slash) {
                        exe_start = last_slash + 1;
                    } else if (last_slash2) {
                        exe_start = last_slash2 + 1;
                    } else {
                        exe_start = cmd_start;
                    }

                    if (strlen(exe_start) >= 4) {
                        size_t exe_len = strlen(exe_start);
                        if (exe_len >= 4) {
                            char ext[5];
                            strncpy(ext, exe_start + exe_len - 4, 4);
                            ext[4] = '\0';

                            for (int i = 0; ext[i]; i++) {
                                ext[i] = tolower(ext[i]);
                            }

                            if (strcmp(ext, ".exe") == 0) {
                                strncpy(process_name, exe_start, sizeof(process_name) - 1);
                                process_name[sizeof(process_name) - 1] = '\0';
                            }
                        }
                    }
                }

                if (strlen(working_dir) == 0) {
                    char command_copy[MAX_CMD_LENGTH];
                    strncpy(command_copy, command, sizeof(command_copy) - 1);
                    command_copy[sizeof(command_copy) - 1] = '\0';

                    char* cmd_start = command_copy;
                    if (cmd_start[0] == '"') {
                        cmd_start++;
                        char* end_quote = strchr(cmd_start, '"');
                        if (end_quote) {
                            *end_quote = '\0';
                        }
                    }

                    char* last_slash = strrchr(cmd_start, '\\');
                    char* last_slash2 = strrchr(cmd_start, '/');
                    char* dir_end = NULL;

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

    return config_count;
}

int is_process_running(const char* process_name) {
    if (!process_name || strlen(process_name) == 0) {
        write_log("Error: Invalid process_name parameter");
        return 0;
    }

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        write_log("Error: Failed to create process snapshot, error code: %lu", GetLastError());
        return 0;
    }

    PROCESSENTRY32 pe32;
    ZeroMemory(&pe32, sizeof(pe32));
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hSnapshot, &pe32)) {
        DWORD err = GetLastError();
        CloseHandle(hSnapshot);
        write_log("Error: Failed to get first process, error code: %lu", err);
        return 0;
    }

    int found = 0;
    do {
        if (_stricmp(pe32.szExeFile, process_name) == 0) {
            found = 1;
            break;
        }

        char* exe_name = strrchr(pe32.szExeFile, '\\');
        if (exe_name) {
            exe_name++;
            if (_stricmp(exe_name, process_name) == 0) {
                found = 1;
                break;
            }
        }

        size_t process_name_len = strlen(process_name);
        char* dot_exe = strstr(process_name, ".exe");
        if (dot_exe && (dot_exe - process_name == (int)(process_name_len - 4))) {
            if (process_name_len < MAX_PATH_LENGTH) {
                char name_without_exe[MAX_PATH_LENGTH];
                strncpy(name_without_exe, process_name, process_name_len - 4);
                name_without_exe[process_name_len - 4] = '\0';

                if (_stricmp(pe32.szExeFile, name_without_exe) == 0) {
                    found = 1;
                    break;
                }

                if (exe_name && _stricmp(exe_name, name_without_exe) == 0) {
                    found = 1;
                    break;
                }
            }
        } else {
            if (process_name_len + 5 < MAX_PATH_LENGTH) {
                char name_with_exe[MAX_PATH_LENGTH];
                strncpy(name_with_exe, process_name, process_name_len);
                strncat(name_with_exe, ".exe", 4);

                if (_stricmp(pe32.szExeFile, name_with_exe) == 0) {
                    found = 1;
                    break;
                }

                if (exe_name && _stricmp(exe_name, name_with_exe) == 0) {
                    found = 1;
                    break;
                }
            }
        }
    } while (Process32Next(hSnapshot, &pe32));

    if (hSnapshot != INVALID_HANDLE_VALUE) {
        CloseHandle(hSnapshot);
    }

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
    char full_working_dir[MAX_PATH_LENGTH];

    if (working_dir && strlen(working_dir) > 0) {
        DWORD attrs = GetFileAttributesA(working_dir);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            work_dir = working_dir;
        } else {
            get_exe_directory(full_working_dir, sizeof(full_working_dir));
            if (strlen(full_working_dir) + strlen(working_dir) + 2 <= sizeof(full_working_dir)) {
                strcat(full_working_dir, "\\");
                strcat(full_working_dir, working_dir);

                DWORD full_attrs = GetFileAttributesA(full_working_dir);
                if (full_attrs != INVALID_FILE_ATTRIBUTES && (full_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                    work_dir = full_working_dir;
                } else {
                    write_log("Warning: Working directory '%s' does not exist, using current directory", working_dir);
                }
            } else {
                write_log("Warning: Path too long for working directory '%s'", working_dir);
            }
        }
    }

    DWORD creation_flags = 0;

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
    if (last_dot) {
        *last_dot = '\0';
        strncat(buffer, ".ini", size - strlen(buffer) - 1);
    } else {
        strncat(buffer, "\\config.ini", size - strlen(buffer) - 1);
    }
}
