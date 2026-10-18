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
- 支持以最高权限运行定时任务（Vista+，自动添加 `/RL HIGHEST`），可启动需要管理员权限的目标进程

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
  - 必须是可执行文件的**完整路径**（如 `C:\App\app.exe` 或 `\\server\share\app.exe`），不支持仅写文件名依赖系统 PATH 搜索
  - 路径包含空格时必须用双引号包裹，例如 `"C:\Program Files\App\app.exe"`
- **process_name**：要监控的进程文件名（可选）
  - 如果未指定，程序会自动从 `command` 的可执行文件路径中提取
  - 进程检测会按配置值精确匹配，并在未带 `.exe` 时额外尝试 `.exe` 后缀
- **working_dir**：进程的工作目录（可选）
  - 支持绝对路径和相对路径（相对于程序所在目录）
  - 如果未指定且command包含路径，程序会自动提取目录部分
- **enabled**：是否启用该程序监控（可选，默认值为1）
  - 0=禁用监控，1=启用监控

程序最多支持同时监控50个进程。

## 构建方法

### 使用提供的批处理脚本

前置要求：安装 MinGW GCC 到以下路径：

`D:\Program Files (x86)\i686-5.3.0-release-win32-dwarf-rt_v4-rev0\mingw32\bin\gcc.exe`

构建步骤：
1. 运行 `build_final.bat`
2. 脚本会编译 XP 兼容的 32 位版本
3. 编译成功后会生成 `build\process_monitor.exe`

## 配置Windows定时任务

先运行 `build_final.bat`，然后运行 `build\create_task.bat` 自动创建定时任务，每5分钟一次。

> **管理员权限说明**：
> - Windows Vista 及以上版本：脚本会自动以最高权限（`/RL HIGHEST`）创建定时任务，使程序能够启动需要管理员权限的目标进程。任务运行账户必须是管理员组成员，否则提权无效。
> - Windows XP：无 UAC，脚本不添加 `/RL` 参数，无需额外处理（XP 的 `schtasks` 不支持该参数）。
> - **触发频率**：Vista 及以上每 5 分钟一次；XP 为**每小时**一次（XP 的 `schtasks` 中 minute 计划存在 24 小时重复窗口限制，无法跨天持续每 5 分钟运行，故降级为原生可靠的 hourly 计划）。

## 使用方法

1. 创建并编辑 `process_monitor.ini` 配置文件（或与可执行文件同名的INI文件）
2. 将编译好的程序配置到Windows定时任务中
3. 程序会定期检查指定进程是否运行，如果未运行则自动启动
4. 查看 `log` 目录下的日志文件了解程序运行状态

## 注意事项

- INI文件中 `;` 后的内容被视为注释
- 配置项key大小写不敏感（如 `Command` 和 `command` 等效）
- INI 文件中的 Windows 路径可以使用普通反斜杠，例如 `C:\Path\App.exe`
- 路径包含空格时必须用双引号包裹，例如 `"C:\Program Files\App\app.exe"`
- `;` 在引号外表示注释；引号内的 `;` 会作为值的一部分保留
- 运行程序的用户需要有足够的权限来启动目标进程
  - Vista 及以上：定时任务默认以最高权限运行，可启动需要管理员权限的目标进程，前提是任务账户为管理员组成员
  - Windows XP：无 UAC，管理员账户登录即拥有完整权限
- 程序会自动在同目录下创建 `log` 文件夹用于存储日志
- 日志文件超过1MB时会自动轮转，最多保留20个备份文件
- 如果有进程启动失败，程序退出码为1
- 进程检测按文件名匹配（不区分大小写）：若其他目录存在同名 exe 且正在运行，会被误判为已运行而不启动本配置
- INI 文件编码：支持 ANSI 或 UTF-8（含 BOM），不支持 UTF-16 编码
- `build_final.bat` 每次构建会清空并重建 `build\` 目录，其中被手动修改过的文件（如 `process_monitor.ini`）会被覆盖，如有需要请先备份

## 日志说明

日志文件存储在 `log` 子目录中，包含以下信息：
- 时间戳
- 事件描述

示例：
```
[2024-01-01 12:00:00] === Process Monitor Started (Version: 1.5.0.0) ===
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
