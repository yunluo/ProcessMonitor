#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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
    if (!buffer || size == 0) {
        return;
    }

    int len = snprintf(buffer, size, "%d.%d.%d.%d",
             VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD);
    if (len < 0 || (size_t)len >= size) {
        buffer[size - 1] = '\0';
    }
}

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

static int get_module_filename_safe(char* buffer, size_t size);
static int safe_append(char* dst, size_t dst_size, const char* suffix);
static int safe_copy_range(char* dst, size_t dst_size, const char* start, const char* end);
static int count_command_line_tokens(const char* cmdline);
static int split_command_line(const char* cmdline, char*** out_argv);
static void free_argv(char** argv, int argc);
static int get_executable_bounds(const char* command, const char** exe_start, const char** exe_end);
static int basename_from_range(char* dst, size_t dst_size, const char* start, const char* end);
static int dirname_from_range(char* dst, size_t dst_size, const char* start, const char* end);
static int is_drive_absolute_path(const char* path);
static int is_unc_path(const char* path);
static int is_root_relative_path(const char* path);
static int resolve_working_directory(const char* working_dir, char* resolved, size_t resolved_size, const char** out_work_dir);

static int get_module_filename_safe(char* buffer, size_t size) {
    DWORD len;

    if (!buffer || size == 0 || size > MAXDWORD) {
        return 0;
    }

    buffer[0] = '\0';
    len = GetModuleFileNameA(NULL, buffer, (DWORD)size);
    if (len == 0 || len >= size) {
        buffer[size - 1] = '\0';
        return 0;
    }

    return 1;
}

static int safe_append(char* dst, size_t dst_size, const char* suffix) {
    size_t len = 0;
    size_t suffix_len;

    if (!dst || !suffix || dst_size == 0) {
        return 0;
    }

    while (len < dst_size && dst[len] != '\0') {
        len++;
    }

    if (len >= dst_size) {
        dst[dst_size - 1] = '\0';
        return 0;
    }

    suffix_len = strlen(suffix);
    if (suffix_len >= dst_size - len) {
        return 0;
    }

    memcpy(dst + len, suffix, suffix_len + 1);
    return 1;
}

static int safe_copy_range(char* dst, size_t dst_size, const char* start, const char* end) {
    size_t len;

    if (!dst || dst_size == 0 || !start || !end || end < start) {
        return 0;
    }

    len = (size_t)(end - start);
    if (len >= dst_size) {
        dst[0] = '\0';
        return 0;
    }

    memcpy(dst, start, len);
    dst[len] = '\0';
    return 1;
}

static int count_command_line_tokens(const char* cmdline) {
    int count = 0;
    int in_quotes = 0;
    int in_token = 0;
    const char* p;

    if (!cmdline) {
        return 0;
    }

    for (p = cmdline; *p; p++) {
        if (*p == '"') {
            in_quotes = !in_quotes;
            if (!in_token) {
                in_token = 1;
                count++;
            }
        } else if ((*p == ' ' || *p == '\t') && !in_quotes) {
            in_token = 0;
        } else if (!in_token) {
            in_token = 1;
            count++;
        }
    }

    return count;
}

static int split_command_line(const char* cmdline, char*** out_argv) {
    int token_count;
    int argc;
    int index = 1;
    const char* p;
    char** argv;

    if (!out_argv) {
        return 0;
    }
    *out_argv = NULL;

    token_count = count_command_line_tokens(cmdline);
    argc = token_count + 1;

    argv = (char**)calloc((size_t)argc, sizeof(char*));
    if (!argv) {
        return 0;
    }

    argv[0] = (char*)malloc(MAX_PATH_LENGTH);
    if (!argv[0]) {
        free(argv);
        return 0;
    }
    if (!get_module_filename_safe(argv[0], MAX_PATH_LENGTH)) {
        free(argv[0]);
        free(argv);
        return 0;
    }

    p = cmdline ? cmdline : "";
    while (*p && index < argc) {
        const char* start;
        const char* end;
        int in_quotes = 0;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }

        if (*p == '"') {
            in_quotes = 1;
            p++;
        }

        start = p;
        while (*p) {
            if (in_quotes) {
                if (*p == '"') {
                    break;
                }
            } else if (*p == ' ' || *p == '\t') {
                break;
            }
            p++;
        }
        end = p;

        argv[index] = (char*)malloc((size_t)(end - start) + 1);
        if (!argv[index]) {
            free_argv(argv, argc);
            return 0;
        }
        safe_copy_range(argv[index], (size_t)(end - start) + 1, start, end);
        index++;

        if (*p == '"') {
            p++;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    }

    *out_argv = argv;
    return index;
}

static void free_argv(char** argv, int argc) {
    int i;

    if (!argv) {
        return;
    }

    for (i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
}

static int get_executable_bounds(const char* command, const char** exe_start, const char** exe_end) {
    const char* p;

    if (!command || !exe_start || !exe_end) {
        return 0;
    }

    p = command;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (*p == '\0') {
        return 0;
    }

    if (*p == '"') {
        p++;
        *exe_start = p;
        *exe_end = strchr(p, '"');
        return *exe_end && *exe_end > *exe_start;
    }

    *exe_start = p;
    while (*p && *p != ' ' && *p != '\t') {
        p++;
    }
    *exe_end = p;

    return *exe_end > *exe_start;
}

static int basename_from_range(char* dst, size_t dst_size, const char* start, const char* end) {
    const char* p;
    const char* base;

    if (!dst || dst_size == 0 || !start || !end || end <= start) {
        return 0;
    }

    base = start;
    for (p = start; p < end; p++) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }

    return safe_copy_range(dst, dst_size, base, end);
}

static int dirname_from_range(char* dst, size_t dst_size, const char* start, const char* end) {
    const char* p;
    const char* slash = NULL;

    if (!dst || dst_size == 0 || !start || !end || end <= start) {
        return 0;
    }

    for (p = start; p < end; p++) {
        if (*p == '\\' || *p == '/') {
            slash = p;
        }
    }

    if (!slash || slash == start) {
        return 0;
    }

    return safe_copy_range(dst, dst_size, start, slash);
}

static int is_drive_absolute_path(const char* path) {
    return path && isalpha((unsigned char)path[0]) && path[1] == ':' &&
           (path[2] == '\\' || path[2] == '/');
}

static int is_unc_path(const char* path) {
    return path && path[0] == '\\' && path[1] == '\\';
}

static int is_root_relative_path(const char* path) {
    return path && (path[0] == '\\' || path[0] == '/') && !is_unc_path(path);
}

static int resolve_working_directory(const char* working_dir, char* resolved, size_t resolved_size, const char** out_work_dir) {
    DWORD attrs;

    if (!out_work_dir) {
        return 0;
    }
    *out_work_dir = NULL;

    if (!working_dir || working_dir[0] == '\0') {
        return 1;
    }

    attrs = GetFileAttributesA(working_dir);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        *out_work_dir = working_dir;
        return 1;
    }

    if (is_drive_absolute_path(working_dir) || is_unc_path(working_dir) || is_root_relative_path(working_dir)) {
        write_log("Warning: Working directory '%s' does not exist, using current directory", working_dir);
        return 1;
    }

    get_exe_directory(resolved, resolved_size);
    if (resolved[0] == '\0' || !safe_append(resolved, resolved_size, "\\") ||
        !safe_append(resolved, resolved_size, working_dir)) {
        write_log("Warning: Path too long for working directory '%s'", working_dir);
        return 1;
    }

    attrs = GetFileAttributesA(resolved);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        *out_work_dir = resolved;
        return 1;
    }

    write_log("Warning: Working directory '%s' (tried '%s') does not exist, using current directory",
              working_dir, resolved);
    return 1;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Suppress unused parameter warnings
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    int argc = 0;
    char** argv = NULL;
    int result = 0;

    argc = split_command_line(lpCmdLine, &argv);
    if (argc <= 0 || argv == NULL) {
        return 1;
    }

    get_exe_directory(log_file_path, sizeof(log_file_path));
    if (log_file_path[0] == '\0' ||
        !safe_append(log_file_path, sizeof(log_file_path), "\\log")) {
        free_argv(argv, argc);
        return 1;
    }
    create_log_directory(log_file_path);
    if (!safe_append(log_file_path, sizeof(log_file_path), "\\process_monitor.log")) {
        free_argv(argv, argc);
        return 1;
    }

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
    free_argv(argv, argc);

    return result;
}

void get_current_time_str(char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }

    time_t rawtime;
    struct tm* timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    if (timeinfo) {
        strftime(buffer, size, "%Y-%m-%d %H:%M:%S", timeinfo);
        buffer[size - 1] = '\0';
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
    if (!get_module_filename_safe(buffer, size)) {
        if (buffer && size > 0) {
            buffer[0] = '\0';
        }
        return;
    }

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
    char* start;
    char* end;

    if (!str) {
        return;
    }

    start = str;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    if (*str == '\0') {
        return;
    }

    end = str + strlen(str) - 1;
    while (end >= str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        if (end == str) {
            break;
        }
        end--;
    }
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
                    trim_whitespace(current_section);
                } else {
                    current_section[0] = '\0';
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
                    int in_quotes = 0;
                    while (*p && *p != '\r' && *p != '\n' && i < value_size - 1) {
                        if (*p == '"') {
                            in_quotes = !in_quotes;
                        }
                        if (*p == ';' && !in_quotes) {
                            break;
                        }
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
                    write_log("%s", warn_msg);
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
                const char* exe_start = NULL;
                const char* exe_end = NULL;

                if (get_executable_bounds(command, &exe_start, &exe_end)) {
                    if (proc_len == 0) {
                        basename_from_range(process_name, sizeof(process_name), exe_start, exe_end);
                    }

                    if (work_len == 0) {
                        dirname_from_range(working_dir, sizeof(working_dir), exe_start, exe_end);
                    }
                }
            }

            if (strlen(command) > 0 && strlen(process_name) > 0) {
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
            } else if (strlen(command) == 0) {
                write_log("Warning: Invalid config in section [%s] - missing required 'command' parameter", p_section);
            } else {
                write_log("Warning: Invalid config in section [%s] - missing or unresolvable 'process_name' parameter", p_section);
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
    char exe_name_with_ext[MAX_PATH];
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

    strncpy(exe_name_with_ext, exe_name_buf, sizeof(exe_name_with_ext) - 1);
    exe_name_with_ext[sizeof(exe_name_with_ext) - 1] = '\0';

    size_t exe_name_len = strlen(exe_name_with_ext);
    if (exe_name_len < 4 || _strnicmp(exe_name_with_ext + exe_name_len - 4, ".exe", 4) != 0) {
        if (exe_name_len + 4 < sizeof(exe_name_with_ext)) {
            strcpy(exe_name_with_ext + exe_name_len, ".exe");
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
        if (_stricmp(pe32.szExeFile, exe_name_buf) == 0 ||
            _stricmp(pe32.szExeFile, exe_name_with_ext) == 0) {
            found = 1;
            break;
        }

        char* sz_exe_name = strrchr(pe32.szExeFile, '\\');
        if (sz_exe_name) {
            if (_stricmp(sz_exe_name + 1, exe_name_buf) == 0 ||
                _stricmp(sz_exe_name + 1, exe_name_with_ext) == 0) {
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

    char application_name[MAX_PATH_LENGTH];
    const char* exe_start = NULL;
    const char* exe_end = NULL;

    if (!get_executable_bounds(command, &exe_start, &exe_end) ||
        !safe_copy_range(application_name, sizeof(application_name), exe_start, exe_end)) {
        write_log("Error: Failed to parse executable path from command: '%s'", command);
        free(cmd_copy);
        return 0;
    }

    DWORD app_attrs = GetFileAttributesA(application_name);
    if (app_attrs == INVALID_FILE_ATTRIBUTES || (app_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        write_log("Error: Executable path does not exist or is not a file: '%s'", application_name);
        free(cmd_copy);
        return 0;
    }

    const char* work_dir = NULL;
    char full_working_dir[MAX_PATH_LENGTH];

    full_working_dir[0] = '\0';
    resolve_working_directory(working_dir, full_working_dir, sizeof(full_working_dir), &work_dir);

    DWORD creation_flags = CREATE_NO_WINDOW;

    if (!CreateProcessA(application_name, cmd_copy, NULL, NULL, FALSE, creation_flags, NULL, work_dir, &si, &pi)) {
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
    if (!get_module_filename_safe(buffer, size)) {
        if (buffer && size > 0) {
            buffer[0] = '\0';
        }
        return;
    }

    char* last_dot = strrchr(buffer, '.');
    char* last_slash = strrchr(buffer, '\\');

    if (last_dot && (!last_slash || last_dot > last_slash)) {
        *last_dot = '\0';
    }

    if (!safe_append(buffer, size, ".ini")) {
        buffer[0] = '\0';
    }
}
