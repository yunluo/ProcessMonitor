# Process Monitor - 代理指南

## 项目概述

一个使用 C 语言编写的轻量级 Windows 进程监控程序，用于检查指定进程是否运行并在未运行时自动启动。由 Windows 定时任务调用。

**支持的平台**:
- Windows XP (0x0501) 及更高版本
- 32位和64位系统

**目标受众**: 构建自动化、定时任务运行器、进程管理

## 必要命令

### 构建

```bash
# 构建所有版本
build_final.bat

# 这会使用 LLVM MinGW 工具链编译：
# - build\x86_xp\process_monitor.exe (XP兼容版，32位)
# - build\x86\process_monitor.exe (Windows 7+，32位)
# - build\x64\process_monitor.exe (Windows 7+，64位)

# 注意：必须安装 LLVM MinGW 工具链到：
# D:\Program Files (x86)\llvm-mingw-20251021-msvcrt-i686
# D:\Program Files (x86)\llvm-mingw-20251021-msvcrt-x86_64
```

### 开发

```bash
# 创建 Windows 定时任务
src/create_task.bat

# 手动任务命令：
schtasks /query /tn "ProcessMonitor"   # 查看状态
schtasks /run /tn "ProcessMonitor"     # 立即运行
schtasks /delete /tn "ProcessMonitor" /f  # 删除任务
```

## 代码组织

```
ProcessMonitor/
├── src/
│   ├── process_monitor.c    # 主源文件（单文件）
│   ├── process_monitor.ini  # 默认配置
│   └── create_task.bat      # 任务创建脚本
├── build/                    # 输出目录
│   ├── process_monitor.exe
│   ├── create_task.bat
│   └── process_monitor.ini
├── README.md                 # 用户文档
└── LICENSE                   # MIT 许可证
```

**架构说明**:
- 单文件实现 (process_monitor.c)
- 无外部构建依赖（仅使用 Windows API）
- 无外部 C 库依赖
- 无单元测试或自动化测试基础设施
- 手动配置 INI 文件

## 构建和开发

### 前置要求

1. **LLVM MinGW 工具链**（需手动下载）:
   - 32位: https://github.com/mstorsjo/llvm-mingw/releases/download/20251021/llvm-mingw-20251021-msvcrt-i686.zip
   - 64位: https://github.com/mstorsjo/llvm-mingw/releases/download/20251021/llvm-mingw-20251021-msvcrt-x86_64.zip
   - 安装到: `D:\Program Files (x86)\llvm-mingw-20251021-msvcrt-i686` 和 `D:\Program Files (x86)\llvm-mingw-20251021-msvcrt-x86_64`

2. **Windows 构建环境**: Windows 7+，MSVC（可选，但建议用于调试）

### 编译详情

**当前构建配置** (在 `build_final.bat` 中):
```batch
"D:\Program Files (x86)\i686-5.3.0-release-win32-dwarf-rt_v4-rev0\mingw32\bin\gcc.exe"
src\process_monitor.c -o build\process_monitor.exe -lws2_32 -lgdi32 -lcomctl32 -lcomdlg32 -lwinhttp -mwindows -D_WIN32_WINNT=0x0501 -O2
```

**关键编译标志**:
- `-mwindows`: 创建 GUI 应用程序（无控制台窗口）
- `-D_WIN32_WINNT=0x0501`: Windows XP 支持
- `-O2`: 优化

### 修改代码

1. 编辑 `src/process_monitor.c`
2. 运行 `build_final.bat`
3. 将新的可执行文件复制到 build 目录

**重要**: 无自动化构建系统 - 需要手动编译

## 配置

### INI 文件格式

配置文件命名为 `<可执行文件名>.ini`（通常是 `process_monitor.ini`）。

**结构**:
```ini
[SectionName]
command = path/to/exe.exe arg1 arg2
process_name = process_name.exe
working_dir = C:\path\to\workdir
enabled = 1
```

**必填项**:
- `command`: 启动进程的完整命令行（必须包含可执行文件路径）

**可选项**:
- `process_name`: 要监控的进程名（无 .exe 如果启用自动检测）
- `working_dir`: 工作目录（如未指定则从命令自动提取）
- `enabled`: 0=禁用，1=启用（默认：1）

**自动检测**:
- 如未指定 `process_name` 且命令是 exe 路径，自动提取文件名
- 如未指定 `working_dir`，从命令路径自动提取目录

**限制**:
- 最多 32 个并发进程
- 章节名最大 64 字符

### 示例配置

```ini
[notepad]
command = "C:\Windows\System32\notepad.exe"
enabled = 1

[custom_app]
command = "C:\MyApp\app.exe" --config config.ini
process_name = app.exe
working_dir = C:\MyApp
enabled = 1
```

## 代码模式和约定

### API 使用

**Windows API**:
- `CreateToolhelp32Snapshot()` 用于进程枚举
- `Process32First/Next()` 用于遍历进程
- `CreateProcessA()` 用于启动进程
- `GetModuleFileNameA()` 用于可执行文件路径
- 手动 INI 解析（无外部库）

**内存管理**:
- 使用自定义 `malloc/free`（无安全字符串函数）
- `strncpy()` 带显式空终止
- 执行字符串操作前检查缓冲区大小

### 错误处理

**日志记录方式**:
- 自定义 `write_log()` 函数，带时间戳
- 日志记录到 `log\process_monitor.log`（自动创建）
- 最大日志大小：1MB，保留 20 个备份
- 日志格式：`[YYYY-MM-DD HH:MM:SS] message`

**错误模式**:
- 失败返回 `0`，成功返回 `1`
- 记录详细错误信息
- 优雅降级（如果某个配置失败则继续处理其余配置）
- 非致命问题记录警告消息

### 跨平台支持

**Windows 版本支持**:
- 编译时定义：`_WIN32_WINNT=0x0501` (Windows XP)
- 仅使用 XP 兼容的 Windows API 函数
- 无现代功能（如 Windows 7+）

**路径处理**:
- 全程使用反斜杠 (`\`)
- 命令中也可使用正斜杠 (`/`)
- INI 中建议使用双反斜杠或正斜杠

### 版本控制

```c
#define VERSION "1.1.0.0"
#define VERSION_MAJOR 1
#define VERSION_MINOR 1
#define VERSION_PATCH 0
#define VERSION_BUILD 0
```

版本字符串格式：`major.minor.patch.build`

## 重要注意事项

### 内存安全

**无安全字符串函数**: 使用 `strncpy()`、`strncat()` 并手动检查边界
- 执行字符串操作前必须验证缓冲区大小
- `strncpy()` 后显式添加空终止符

### 进程检测

**匹配逻辑**:
- 按文件名匹配（不区分大小写）
- 处理 `.exe` 扩展名的变化（有无均可）
- 分别检查完整路径和文件名
- 如果 process_name 指定了 `.exe`，也会检查不带扩展名的情况

**限制**: 仅检查当前用户上下文中运行的进程

### 进程启动

**工作目录**:
- 如指定且存在：使用它
- 如指定但不存在：记录警告，使用当前目录
- 如未指定，从命令路径自动提取

**命令行**:
- 命令在修改前使用 `_strdup()` 复制
- 自定义解析处理引号字符串
- 最大命令长度：1024 字符

### 日志记录

**日志轮转**:
- 写入前检查文件大小
- 使用 `MoveFileA()` 旋转备份
- 最旧备份（20）先删除
- 轮转失败静默处理（记录警告）

**日志位置**:
- 自动在可执行文件的目录创建
- 路径：`<exe_dir>\log\process_monitor.log`

### 配置解析

**INI 格式**:
- 章节名在 `[ ]` 中
- Key=value 对
- 注释：`;` 前缀
- 章节名不区分大小写
- 处理值周围的空格

**解析限制**:
- 不支持嵌套章节
- 不支持一个键多个值
- 不验证键名（必须完全匹配）

### 构建系统

**无 Makefile**: 所有构建使用批处理脚本
- 无自动化依赖
- 无构建目标或阶段
- 无增量构建
- 总是重新编译整个源代码

### 测试

**无自动化测试**: 测试必须手动运行
- 无单元测试框架
- 无集成测试套件
- 无 CI/CD 自动化
- 需要手动验证

## 测试方法

由于不存在自动化测试，测试需要：

1. **构建验证**:
   - 运行 `build_final.bat`
   - 验证无编译错误
   - 检查 build 目录中的输出大小

2. **功能测试**:
   - 创建测试 INI 配置
   - 手动运行程序
   - 检查日志文件以确认正确行为
   - 验证进程启动/监控是否正确

3. **平台测试**:
   - 在目标 Windows 版本上测试
   - 如针对 XP，验证 XP 兼容性
   - 检查 32 位和 64 位构建

4. **定时任务测试**:
   - 使用 `src/create_task.bat`
   - 验证任务按时运行
   - 检查日志执行成功

## 常见任务

### 添加新进程

1. 编辑 `src/process_monitor.ini`:
   ```ini
   [MyProcess]
   command = "C:\Path\To\Program.exe" args
   enabled = 1
   ```

2. 构建：`build_final.bat`

3. 先手动测试

### 调试

**无控制台窗口**:
- 程序使用 `-mwindows` 标志运行
- 无可见的控制台输出
- 必须检查日志文件进行调试

**日志文件**:
- 位置：`<exe_dir>\log\process_monitor.log`
- 包含时间戳和事件
- 检查 "Error" 或 "Warning" 消息

**调试构建**:
- 要获取控制台输出，重新编译时移除 `-mwindows`
- 移除 `-O2` 优化标志
- 使用 Process Monitor 或 Sysinternals Suite 等调试工具

### 修改构建配置

编辑 `build_final.bat`:
- 编译器路径（第 16 行）
- 链接器标志（第 16 行）
- 输出路径

**重要**: 更改需要从源代码重新编译

## 安全考虑

**权限**:
- 需要启动进程的权限
- 以用户上下文运行（除非作为管理员调度，否则无管理员权限）
- 进程启动失败可能表示权限问题

**路径处理**:
- 不对命令参数进行清理
- 用户必须信任配置来源
- INI 中的路径可以包含转义的特殊字符

**日志安全**:
- 日志文件可能包含敏感信息（进程路径、命令）
- 日志目录可被用户访问
- 考虑限制日志目录权限

## 迁移说明

**之前的 CI/CD 设置**: 项目之前有 GitHub Actions CI（已回退到本地构建）
- 检查 git 历史：`git log --oneline | grep ci`
- 不存在自动化测试基础设施

**版本历史**:
- 从基本实现开始
- 添加 XP 兼容性（0x0501）
- 增强错误处理
- 简化构建流程（移除 CI）

## 其他资源

**相关工具**:
- LLVM MinGW: https://github.com/mstorsjo/llvm-mingw
- Sysinternals Suite: Process Monitor, Task Manager

**代码文档**:
- Windows API 文档: https://docs.microsoft.com/en-us/windows/win32/api/
- C 标准库（需要基本知识）

**项目**:
- GitHub 仓库（检查问题并更新）
- MIT 许可证（允许修改）
