#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <time.h>
#include <direct.h>  // 添加这个头文件用于创建目录
#include <ctype.h>   // 支持tolower函数

// 最大路径长度
#define MAX_PATH_LENGTH 512
// 最大命令行长度
#define MAX_CMD_LENGTH 1024
// 最大日志行长度
#define MAX_LOG_LENGTH 1024
// INI节名称最大长度
#define MAX_SECTION_NAME 64
// 日志文件最大大小（1MB）
#define MAX_LOG_SIZE 1024*1024
// 日志文件备份数量
#define MAX_LOG_BACKUPS 20

// 配置项结构体
typedef struct {
    char section_name[MAX_SECTION_NAME];
    char process_name[MAX_PATH_LENGTH];
    char command[MAX_CMD_LENGTH];
    char working_dir[MAX_PATH_LENGTH];  // 新增：工作目录
    int enabled;
} ProgramConfig;

// 全局变量
char ini_file_path[MAX_PATH_LENGTH];
char log_file_path[MAX_PATH_LENGTH];

// 函数声明
void get_current_time_str(char* buffer, size_t size);
void write_log(const char* format, ...);
void rotate_log_file();  // 新增：日志轮转函数
int parse_ini_config(const char* ini_file, ProgramConfig* configs, int max_configs);
int is_process_running(const char* process_name);
int start_process(const char* command, const char* working_dir);  // 修改：添加工作目录参数
void get_exe_directory(char* buffer, size_t size);
void get_default_ini_path(char* buffer, size_t size);
void create_log_directory(const char* log_path);
int parse_command_line(const char* cmd_line, char*** argv_out, int* argc_out);

// WinMain函数替代main函数，创建Windows GUI应用程序
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    int argc = 0;
    char** argv = NULL;
    int result = 0;  // 添加返回结果变量
    
    // 将命令行参数转换为argc/argv格式
    if (strlen(lpCmdLine) > 0) {
        argc = 1; // 至少有一个参数（程序名）
        
        // 计算参数数量（改进版，支持引号参数）
        int in_quotes = 0;
        for (size_t i = 0; i < strlen(lpCmdLine); i++) {
            if (lpCmdLine[i] == '"') {
                in_quotes = !in_quotes;
            } else if (lpCmdLine[i] == ' ' && !in_quotes) {
                argc++;
            }
        }
        
        // 分配参数数组
        argv = (char**)malloc(argc * sizeof(char*));
        if (argv == NULL) {
            return 1; // 内存分配失败
        }
        
        argv[0] = (char*)malloc(MAX_PATH_LENGTH);
        if (argv[0] == NULL) {
            free(argv);
            return 1; // 内存分配失败
        }
        GetModuleFileNameA(NULL, argv[0], MAX_PATH_LENGTH);
        
        // 解析参数（改进版，支持引号参数）
        char* cmd_copy = _strdup(lpCmdLine);
        if (cmd_copy == NULL) {
            free(argv[0]);
            free(argv);
            return 1; // 内存分配失败
        }
        
        int i = 1;
        char* token = cmd_copy;
        in_quotes = 0;
        
        while (*token && i < argc) {
            // 跳过前导空格
            while (*token == ' ') token++;
            
            if (!*token) break;
            
            // 确定参数开始位置
            char* start = token;
            char delimiter = ' '; // 默认分隔符
            
            // 检查是否有引号
            if (*token == '"') {
                in_quotes = 1;
                delimiter = '"';
                start = ++token; // 跳过开始引号
            }
            
            // 查找参数结束位置
            while (*token && (*token != delimiter || (delimiter == '"' && !in_quotes))) {
                if (*token == '"' && delimiter == ' ') {
                    in_quotes = !in_quotes;
                }
                token++;
            }
            
            // 分配并复制参数
            size_t len = token - start;
            argv[i] = (char*)malloc(len + 1);
            if (argv[i] == NULL) {
                // 释放已分配的内存
                for (int j = 0; j < i; j++) {
                    free(argv[j]);
                }
                free(argv);
                free(cmd_copy);
                return 1; // 内存分配失败
            }
            
            strncpy_s(argv[i], len + 1, start, len);
            argv[i][len] = '\0';
            
            // 跳过分隔符
            if (*token) token++;
            
            i++;
            in_quotes = 0;
        }
        
        free(cmd_copy);
    } else {
        // 没有命令行参数
        argc = 1;
        argv = (char**)malloc(argc * sizeof(char*));
        if (argv == NULL) {
            return 1; // 内存分配失败
        }
        
        argv[0] = (char*)malloc(MAX_PATH_LENGTH);
        if (argv[0] == NULL) {
            free(argv);
            return 1; // 内存分配失败
        }
        GetModuleFileNameA(NULL, argv[0], MAX_PATH_LENGTH);
    }
    
    // 初始化日志路径到log子目录
    get_exe_directory(log_file_path, sizeof(log_file_path));
    strcat_s(log_file_path, sizeof(log_file_path), "\\log");
    create_log_directory(log_file_path);  // 确保log目录存在
    strcat_s(log_file_path, sizeof(log_file_path), "\\process_monitor.log");
    
    // 检查并执行日志轮转
    rotate_log_file();
    
    write_log("=== Process Monitor Started ===");
    
    // 获取INI文件路径
    if (argc > 1) {
        strncpy_s(ini_file_path, sizeof(ini_file_path), argv[1], _TRUNCATE);
    } else {
        get_default_ini_path(ini_file_path, sizeof(ini_file_path));
    }
    
    write_log("Using INI file: %s", ini_file_path);
    
    // 检查INI文件是否存在
    if (GetFileAttributesA(ini_file_path) == INVALID_FILE_ATTRIBUTES) {
        write_log("Error: INI file not found!");
        result = 1; // 设置错误返回码
        goto cleanup; // 跳转到清理代码
    }
    
    // 解析INI配置
    ProgramConfig configs[32];  // 最多支持32个配置节
    int config_count = parse_ini_config(ini_file_path, configs, 32);
    
    if (config_count <= 0) {
        write_log("Error: No valid configurations found in INI file");
        result = 1; // 设置错误返回码
        goto cleanup; // 跳转到清理代码
    }
    
    write_log("Found %d program configurations", config_count);
    
    // 处理每个配置项
    for (int i = 0; i < config_count; i++) {
        if (!configs[i].enabled) {
            write_log("Skipping disabled program: %s", configs[i].section_name);
            continue;
        }
        
        write_log("Checking program: %s", configs[i].section_name);
        
        // 检查进程是否正在运行
        if (is_process_running(configs[i].process_name)) {
            write_log("Process '%s' is already running, skipping start", configs[i].process_name);
        } else {
            write_log("Process '%s' is not running, starting...", configs[i].process_name);
            
            // 启动进程
            if (start_process(configs[i].command, configs[i].working_dir)) {
                write_log("Successfully started process: %s", configs[i].process_name);
            } else {
                write_log("Failed to start process: %s", configs[i].process_name);
            }
        }
    }
    
    write_log("=== Process Monitor Finished ===");

cleanup:
    // 释放内存
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

// 获取当前时间字符串
void get_current_time_str(char* buffer, size_t size) {
    time_t rawtime;
    struct tm timeinfo;
    
    time(&rawtime);
    localtime_s(&timeinfo, &rawtime);
    
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

// 日志轮转函数 - 确保最大保留20个日志文件
void rotate_log_file() {
    // 检查日志文件是否存在
    if (GetFileAttributesA(log_file_path) == INVALID_FILE_ATTRIBUTES) {
        return; // 文件不存在，无需轮转
    }
    
    // 获取文件大小
    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (!GetFileAttributesExA(log_file_path, GetFileExInfoStandard, &fileData)) {
        write_log("Warning: Failed to get log file attributes");
        return;
    }
    
    ULARGE_INTEGER fileSize;
    fileSize.LowPart = fileData.nFileSizeLow;
    fileSize.HighPart = fileData.nFileSizeHigh;
    
    // 如果文件大小超过限制，执行轮转
    if (fileSize.QuadPart > MAX_LOG_SIZE) {
        char backup_path[MAX_PATH_LENGTH];
        char old_path[MAX_PATH_LENGTH];
        char new_path[MAX_PATH_LENGTH];
        
        // 删除最旧的备份文件
        snprintf(backup_path, sizeof(backup_path), "%s.%d", log_file_path, MAX_LOG_BACKUPS);
        if (GetFileAttributesA(backup_path) != INVALID_FILE_ATTRIBUTES) {
            if (!DeleteFileA(backup_path)) {
                write_log("Warning: Failed to delete oldest log backup");
            }
        }
        
        // 依次重命名备份文件，确保即使某个文件操作失败也能继续处理
        for (int i = MAX_LOG_BACKUPS - 1; i >= 1; i--) {
            snprintf(old_path, sizeof(old_path), "%s.%d", log_file_path, i);
            snprintf(new_path, sizeof(new_path), "%s.%d", log_file_path, i + 1);
            
            // 安全地重命名文件
            if (GetFileAttributesA(old_path) != INVALID_FILE_ATTRIBUTES) {
                // 先删除目标文件（如果存在）
                DeleteFileA(new_path);
                // 然后重命名
                if (!MoveFileA(old_path, new_path)) {
                    write_log("Warning: Failed to rename log backup %d to %d", i, i + 1);
                }
            }
        }
        
        // 将当前日志文件重命名为第一个备份
        char first_backup[MAX_PATH_LENGTH];
        snprintf(first_backup, sizeof(first_backup), "%s.1", log_file_path);
        DeleteFileA(first_backup); // 先删除可能存在的第一个备份
        if (!MoveFileA(log_file_path, first_backup)) {
            write_log("Warning: Failed to rotate current log file");
        }
    }
}

// 写入日志
void write_log(const char* format, ...) {
    FILE* file;
    char time_str[32];
    char log_line[MAX_LOG_LENGTH];
    va_list args;
    
    // 打开日志文件（追加模式）
    if (fopen_s(&file, log_file_path, "a") != 0) {
        return;
    }
    
    // 获取当前时间
    get_current_time_str(time_str, sizeof(time_str));
    
    // 格式化日志内容
    va_start(args, format);
    vsnprintf_s(log_line, sizeof(log_line), _TRUNCATE, format, args);
    va_end(args);
    
    // 写入日志
    fprintf(file, "[%s] %s\n", time_str, log_line);
    fclose(file);

}

// 获取可执行文件所在目录
void get_exe_directory(char* buffer, size_t size) {
    GetModuleFileNameA(NULL, buffer, (DWORD)size);
    char* last_slash = strrchr(buffer, '\\');
    if (last_slash) {
        *last_slash = '\0';
    }
}

// 创建日志目录
void create_log_directory(const char* log_path) {
    // 使用CreateDirectory创建目录，更兼容不同Windows版本
    CreateDirectoryA(log_path, NULL);
}

// 解析INI配置文件
int parse_ini_config(const char* ini_file, ProgramConfig* configs, int max_configs) {
    char section_names[8192] = {0};  // 存储所有节名称
    char* p_section = section_names;
    int config_count = 0;
    
    // 获取所有节名称
    DWORD section_len = GetPrivateProfileSectionNamesA(section_names, sizeof(section_names), ini_file);
    if (section_len == 0) {
        return 0;
    }
    
    // 遍历每个节
    while (*p_section && config_count < max_configs) {
        char enabled_str[8] = {0};
        char process_name[MAX_PATH_LENGTH] = {0};
        char command[MAX_CMD_LENGTH] = {0};
        char working_dir[MAX_PATH_LENGTH] = {0};  // 新增：工作目录
        
        // 获取enabled值（使用新格式），默认值为1表示启用
        GetPrivateProfileStringA(p_section, "enabled", "1", enabled_str, sizeof(enabled_str), ini_file);
        
        // 如果启用，则获取其他配置
        if (strcmp(enabled_str, "1") == 0) {
            // 获取进程名（可选）
            GetPrivateProfileStringA(p_section, "process_name", "", process_name, sizeof(process_name), ini_file);
            
            // 获取启动命令（必须项）
            GetPrivateProfileStringA(p_section, "command", "", command, sizeof(command), ini_file);
            
            // 获取工作目录（可选）
            GetPrivateProfileStringA(p_section, "working_dir", "", working_dir, sizeof(working_dir), ini_file);
            
            // 自动从command中提取process_name和working_dir（如果未指定）
            if (strlen(command) > 0) {
                // 如果process_name为空，且command是exe文件路径，自动提取
                if (strlen(process_name) == 0) {
                    // 首先尝试从引号包围的命令中提取可执行文件名
                    char command_copy[MAX_CMD_LENGTH];
                    strncpy_s(command_copy, sizeof(command_copy), command, _TRUNCATE);
                    
                    // 移除首尾引号（如果有）
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
                    
                    // 找到最后一个反斜杠或斜杠
                    if (last_slash && last_slash2) {
                        exe_start = (last_slash > last_slash2) ? last_slash + 1 : last_slash2 + 1;
                    } else if (last_slash) {
                        exe_start = last_slash + 1;
                    } else if (last_slash2) {
                        exe_start = last_slash2 + 1;
                    } else {
                        exe_start = cmd_start; // 没有路径，直接使用command
                    }
                    
                    // 检查是否以.exe结尾（不区分大小写）
                    if (strlen(exe_start) >= 4) {
                        size_t exe_len = strlen(exe_start);
                        // 检查是否以.exe结尾（不区分大小写）
                        if (exe_len >= 4) {
                            char ext[5];
                            strncpy_s(ext, sizeof(ext), exe_start + exe_len - 4, 4);
                            ext[4] = '\0';
                            
                            // 转换为小写进行比较
                            for (int i = 0; ext[i]; i++) {
                                ext[i] = tolower(ext[i]);
                            }
                            
                            if (strcmp(ext, ".exe") == 0) {
                                // 直接使用完整的文件名作为process_name
                                strncpy_s(process_name, sizeof(process_name), exe_start, _TRUNCATE);
                            }
                        }
                    }
                }
                
                // 如果working_dir为空，且command包含路径，自动提取目录
                if (strlen(working_dir) == 0) {
                    // 处理可能包含引号的命令
                    char command_copy[MAX_CMD_LENGTH];
                    strncpy_s(command_copy, sizeof(command_copy), command, _TRUNCATE);
                    
                    // 移除首尾引号（如果有）
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
                    
                    // 找到最后一个反斜杠或斜杠
                    if (last_slash && last_slash2) {
                        dir_end = (last_slash > last_slash2) ? last_slash : last_slash2;
                    } else if (last_slash) {
                        dir_end = last_slash;
                    } else if (last_slash2) {
                        dir_end = last_slash2;
                    }
                    
                    if (dir_end) {
                        // 提取目录部分
                        size_t dir_len = dir_end - cmd_start;
                        if (dir_len > 0 && dir_len < sizeof(working_dir)) {
                            strncpy_s(working_dir, sizeof(working_dir), cmd_start, dir_len);
                            working_dir[dir_len] = '\0';
                        }
                    }
                }
            }
            
            // 验证必要配置是否存在（现在只检查command）
            if (strlen(command) > 0) {
                strncpy_s(configs[config_count].section_name, sizeof(configs[config_count].section_name), p_section, _TRUNCATE);
                strncpy_s(configs[config_count].process_name, sizeof(configs[config_count].process_name), process_name, _TRUNCATE);
                strncpy_s(configs[config_count].command, sizeof(configs[config_count].command), command, _TRUNCATE);
                strncpy_s(configs[config_count].working_dir, sizeof(configs[config_count].working_dir), working_dir, _TRUNCATE);
                configs[config_count].enabled = 1;
                config_count++;
                
                write_log("Loaded config: [%s] process='%s', command='%s', working_dir='%s'", p_section, process_name, command, working_dir);
            } else {
                write_log("Warning: Invalid config in section [%s] - missing required 'command' parameter", p_section);
            }
        } else {
            write_log("Skipping disabled config section: [%s]", p_section);
        }
        
        // 移动到下一个节名称
        p_section += strlen(p_section) + 1;
    }
    
    return config_count;
}

// 增强的进程检测函数 - 兼容32/64位系统
int is_process_running(const char* process_name) {
    // 参数安全检查
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
    ZeroMemory(&pe32, sizeof(pe32)); // 确保结构体被正确初始化
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    // 确保Process32First调用成功
    if (!Process32First(hSnapshot, &pe32)) {
        DWORD err = GetLastError();
        CloseHandle(hSnapshot);
        write_log("Error: Failed to get first process, error code: %lu", err);
        return 0;
    }
    
    int found = 0;
    do {
        // 比较进程名称（不区分大小写）
        if (_stricmp(pe32.szExeFile, process_name) == 0) {
            found = 1;
            break;
        }
        
        // 有时进程名可能包含路径，尝试比较不带路径的部分
        char* exe_name = strrchr(pe32.szExeFile, '\\');
        if (exe_name) {
            exe_name++; // 跳过反斜杠
            if (_stricmp(exe_name, process_name) == 0) {
                found = 1;
                break;
            }
        }
        
        // 还可以尝试比较进程名去掉.exe后缀的情况
        size_t process_name_len = strlen(process_name);
        char* dot_exe = strstr(process_name, ".exe");
        if (dot_exe && (dot_exe - process_name == (int)(process_name_len - 4))) {
            // process_name以.exe结尾
            if (process_name_len < MAX_PATH_LENGTH) {
                char name_without_exe[MAX_PATH_LENGTH];
                strncpy_s(name_without_exe, sizeof(name_without_exe), process_name, process_name_len - 4);
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
            // 如果process_name不以.exe结尾，尝试添加.exe后缀进行比较
            if (process_name_len + 5 < MAX_PATH_LENGTH) { // +5 for ".exe" and null terminator
                char name_with_exe[MAX_PATH_LENGTH];
                strncpy_s(name_with_exe, sizeof(name_with_exe), process_name, process_name_len);
                strncat_s(name_with_exe, sizeof(name_with_exe), ".exe", 4);
                
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
    
    // 安全关闭句柄
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        CloseHandle(hSnapshot);
    }
    
    return found;
}

// 启动进程 - 增强稳定性，确保主程序不会因子进程异常而崩溃
int start_process(const char* command, const char* working_dir) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    
    // 安全检查
    if (!command || strlen(command) == 0) {
        write_log("Error: Invalid command parameter");
        return 0;
    }
    
    // 初始化结构体
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    // 创建命令行副本，因为CreateProcess可能会修改它
    char* cmd_copy = _strdup(command);
    if (cmd_copy == NULL) {
        write_log("Error: Failed to allocate memory for command");
        return 0;
    }
    
    // 如果指定了工作目录，则使用它，否则使用NULL（默认当前目录）
    const char* work_dir = NULL;
    char full_working_dir[MAX_PATH_LENGTH];
    
    // 只有当工作目录非空时才处理
    if (working_dir && strlen(working_dir) > 0) {
        // 检查目录是否存在
        DWORD attrs = GetFileAttributesA(working_dir);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            work_dir = working_dir;
        } else {
            // 尝试使用相对路径
            get_exe_directory(full_working_dir, sizeof(full_working_dir));
            if (strlen(full_working_dir) + strlen(working_dir) + 2 <= sizeof(full_working_dir)) {
                strcat_s(full_working_dir, sizeof(full_working_dir), "\\");
                strcat_s(full_working_dir, sizeof(full_working_dir), working_dir);
                
                // 检查完整路径是否存在且为目录
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
    
    // 设置进程创建标志：在低权限环境下也能工作
    DWORD creation_flags = 0;
    
    // 使用CreateProcessA可以处理包含空格和中文的路径
    if (!CreateProcessA(NULL, cmd_copy, NULL, NULL, FALSE, creation_flags, NULL, work_dir, &si, &pi)) {
        DWORD error = GetLastError();
        write_log("Error: Failed to start process. Error code: %lu, command: '%s'", error, command);
        free(cmd_copy);
        return 0;
    }
    
    // 关闭进程和线程句柄，让子进程独立运行
    // 确保即使子进程立即崩溃，主程序也不会受到影响
    if (pi.hProcess != NULL) {
        CloseHandle(pi.hProcess);
    }
    if (pi.hThread != NULL) {
        CloseHandle(pi.hThread);
    }
    
    // 释放内存
    free(cmd_copy);
    
    return 1;
}

// 获取默认INI文件路径（与exe同名）
void get_default_ini_path(char* buffer, size_t size) {
    GetModuleFileNameA(NULL, buffer, (DWORD)size);
    char* last_dot = strrchr(buffer, '.');
    if (last_dot) {
        *last_dot = '\0';
        strncat_s(buffer, size, ".ini", _TRUNCATE);
    } else {
        strncat_s(buffer, size, "\\config.ini", _TRUNCATE);
    }
}

// 解析命令行参数，支持带引号的参数
int parse_command_line(const char* cmd_line, char*** argv_out, int* argc_out) {
    if (!cmd_line || !argv_out || !argc_out) {
        return -1; // 参数错误
    }
    
    // 计算参数数量
    *argc_out = 0;
    int in_quotes = 0;
    int expecting_arg = 1;
    
    for (size_t i = 0; i < strlen(cmd_line); i++) {
        if (cmd_line[i] == '"') {
            if (expecting_arg) {
                (*argc_out)++;
                expecting_arg = 0;
            }
            in_quotes = !in_quotes;
        } else if (cmd_line[i] == ' ' && !in_quotes) {
            expecting_arg = 1;
        } else if (expecting_arg) {
            (*argc_out)++;
            expecting_arg = 0;
        }
    }
    
    if (*argc_out == 0) {
        *argv_out = NULL;
        return 0;
    }
    
    // 分配参数数组
    *argv_out = (char**)malloc(*argc_out * sizeof(char*));
    if (*argv_out == NULL) {
        *argc_out = 0;
        return -1; // 内存分配失败
    }
    
    // 解析参数
    int arg_index = 0;
    const char* p = cmd_line;
    
    while (*p && arg_index < *argc_out) {
        // 跳过前导空格
        while (*p == ' ') p++;
        if (!*p) break;
        
        // 确定参数开始和结束位置
        const char* start = p;
        const char* end = p;
        in_quotes = 0;
        
        if (*p == '"') {
            in_quotes = 1;
            start = ++p;
            while (*p && (*p != '"' || in_quotes)) {
                if (*p == '"') in_quotes = 0;
                p++;
            }
            end = p;
            if (*p == '"') p++; // 跳过结束引号
        } else {
            while (*p && *p != ' ') p++;
            end = p;
        }
        
        // 分配并复制参数
        size_t len = end - start;
        (*argv_out)[arg_index] = (char*)malloc(len + 1);
        if ((*argv_out)[arg_index] == NULL) {
            // 释放已分配的内存
            for (int i = 0; i < arg_index; i++) {
                free((*argv_out)[i]);
            }
            free(*argv_out);
            *argc_out = 0;
            *argv_out = NULL;
            return -1; // 内存分配失败
        }
        
        strncpy_s((*argv_out)[arg_index], len + 1, start, len);
        (*argv_out)[arg_index][len] = '\0';
        arg_index++;
    }
    
    *argc_out = arg_index;
    return 0;
}
