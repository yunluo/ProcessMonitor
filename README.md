# 免责声明
该程式是使用Qoder生成，內部自用，具体代码请檢查

# Windows进程监控程序

这是一个轻量级的Windows进程监控程序，用于被Windows定时任务调用，检查指定进程是否运行，如未运行则自动启动。

## 功能特点

- 支持Windows XP、Windows 7及以上版本
- 支持32位和64位系统
- 使用INI配置文件进行配置
- 支持日志输出和日志轮转（最大保留20个备份文件）
- 用于被Windows定时任务调用
- 自动从命令行提取进程名称和工作目录
- 无控制台窗口运行模式
- 支持INI注释（`;` 开头）
- 配置项key大小写不敏感
- 进程启动失败时退出码为1

## 配置文件格式

程序会自动加载与可执行文件同名的INI文件。配置文件支持监控多个程序，格式如下：

```ini
; 程序1配置 - ; 开头为注释
[程序名称]
; 要监控的进程名称（可选，如未指定且command是exe路径，会自动提取）
process_name = 进程名.exe
; 启动进程的命令行（必填项）
command = 启动命令
; 工作目录（可选，如未指定且command是exe路径，会自动提取）
working_dir = 工作目录路径
; 是否启用：0=否，1=是（可选，默认值为1）
enabled = 1

; 程序2配置（可添加多个）
[另一个程序]
command = "C:\Path\To\Program.exe" 参数1 参数2
; 其他配置项可选
```

### 配置项说明

- **程序名称**：每个程序配置的唯一标识符，用于在日志中区分不同程序
- **command**：启动进程的完整命令行（**必填项**）
- **process_name**：要监控的进程文件名（可选）
  - 如果未指定，程序会自动从command的第一个token提取
  - 支持任意可执行文件类型（不只是.exe）
- **working_dir**：进程的工作目录（可选）
  - 支持绝对路径和相对路径（相对于程序所在目录）
  - 如果未指定且command包含路径，程序会自动提取目录部分
- **enabled**：是否启用该程序监控（可选，默认值为1）
  - 0=禁用监控，1=启用监控

程序最多支持同时监控50个进程。

## 构建方法

### 使用提供的批处理脚本

前置要求：下载并安装 LLVM MinGW 工具链
- 32位: https://github.com/mstorsjo/llvm-mingw/releases/download/20251021/llvm-mingw-20251021-msvcrt-i686.zip
- 64位: https://github.com/mstorsjo/llvm-mingw/releases/download/20251021/llvm-mingw-20251021-msvcrt-x86_64.zip

安装到以下目录：
- `D:\Program Files (x86)\llvm-mingw-20251021-msvcrt-i686`
- `D:\Program Files (x86)\llvm-mingw-20251021-msvcrt-x86_64`

构建步骤：
1. 运行 `build_final.bat`
2. 脚本会使用LLVM MinGW进行编译
3. 编译成功后会在 `build` 目录下生成可执行文件

## 配置Windows定时任务

双击 `create_task.bat` 自动创建定时任务，每5分钟一次

## 使用方法

1. 创建并编辑 `process_monitor.ini` 配置文件（或与可执行文件同名的INI文件）
2. 将编译好的程序配置到Windows定时任务中
3. 程序会定期检查指定进程是否运行，如果未运行则自动启动
4. 查看 `log` 目录下的日志文件了解程序运行状态

## 注意事项

- INI文件中 `;` 后的内容被视为注释
- 配置项key大小写不敏感（如 `Command` 和 `command` 等效）
- 命令行路径中的反斜杠需要使用双反斜杠或正斜杠
- 运行程序的用户需要有足够的权限来启动目标进程
- 程序会自动在同目录下创建 `log` 文件夹用于存储日志
- 日志文件超过1MB时会自动轮转，最多保留20个备份文件
- 如果有进程启动失败，程序退出码为1

## 日志说明

日志文件存储在 `log` 子目录中，包含以下信息：
- 时间戳
- 事件描述

示例：
```
[2024-01-01 12:00:00] === Process Monitor Started (Version: 1.2.0.1) ===
[2024-01-01 12:00:00] Using INI file: D:\App\process_monitor.ini
[2024-01-01 12:00:00] Loaded config: [notepad] process='notepad.exe', command='C:\Windows\System32\notepad.exe', working_dir='C:\Windows\System32'
[2024-01-01 12:00:00] Found 1 program configurations
[2024-01-01 12:00:00] Checking program: notepad
[2024-01-01 12:00:00] Process 'notepad.exe' is not running, starting...
[2024-01-01 12:00:00] Successfully started process: notepad.exe
[2024-01-01 12:00:00] === Process Monitor Finished ===
```

如果启动失败，日志会显示警告并返回退出码1：
```
[2024-01-01 12:00:00] Process 'app.exe' is not running, starting...
[2024-01-01 12:00:00] Failed to start process: app.exe
[2024-01-01 12:00:00] Warning: 1 process(es) failed to start
[2024-01-01 12:00:00] === Process Monitor Finished ===
```
