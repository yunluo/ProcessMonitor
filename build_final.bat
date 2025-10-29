@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Process Monitor Final Build
echo ========================================

REM 设置LLVM MinGW路径
set LLVM_MINGW_32="D:\Program Files (x86)\llvm-mingw-20251021-msvcrt-i686"
set LLVM_MINGW_64="D:\Program Files (x86)\llvm-mingw-20251021-msvcrt-x86_64"


REM 检查LLVM MinGW是否存在
if not exist "!LLVM_MINGW_32!" (
    echo Error: LLVM MinGW 32-bit not found at:
    echo   !LLVM_MINGW_32!
    pause
    exit /b 1
)

if not exist "!LLVM_MINGW_64!" (
    echo Error: LLVM MinGW 64-bit not found at:
    echo   !LLVM_MINGW_64!
    pause
    exit /b 1
)

echo LLVM MinGW paths verified.

REM 清理旧的构建目录
if exist build (
    echo Cleaning old build directory...
    rmdir /s /q build 2>nul
)

REM 创建build目录
echo Creating build directories...
mkdir build >nul 2>&1
mkdir build\x86 >nul 2>&1
mkdir build\x64 >nul 2>&1

REM 保存原始PATH
set ORIGINAL_PATH=%PATH%

REM 编译32位版本（无控制台窗口）
echo.
echo ========================================
echo Building 32-bit final version...
echo ========================================
set PATH=!LLVM_MINGW_32!\bin;%ORIGINAL_PATH%


clang -Wall -Os -m32 -flto=full -ffunction-sections -fdata-sections -fno-ident -fno-asynchronous-unwind-tables -fno-stack-protector src\process_monitor.c -o build\x86\process_monitor.exe -lpsapi -Wl,--gc-sections -Wl,--strip-all -static-libgcc -Wl,--subsystem,windows



if !errorlevel! neq 0 (
    echo Error: Failed to build 32-bit version
    pause
    exit /b 1
)

echo 32-bit build successful.

REM 编译64位版本（无控制台窗口）
echo.
echo ========================================
echo Building 64-bit final version...
echo ========================================
set PATH=!LLVM_MINGW_64!\bin;%ORIGINAL_PATH%


clang -Wall -Os -m64 -flto=full -ffunction-sections -fdata-sections -fno-ident -fno-asynchronous-unwind-tables -fno-stack-protector src\process_monitor.c -o build\x64\process_monitor.exe -lpsapi -Wl,--gc-sections -Wl,--strip-all -static-libgcc -Wl,--subsystem,windows


if !errorlevel! neq 0 (
    echo Error: Failed to build 64-bit version
    pause
    exit /b 1
)

echo 64-bit build successful.

REM 复制配置文件和创建log目录
echo.
echo Setting up directories and copying files...
copy src\process_monitor.ini build\x86\ >nul 2>&1
copy src\process_monitor.ini build\x64\ >nul 2>&1


REM 创建README文件
copy src\README.md build\x86\ >nul 2>&1
copy src\README.md build\x64\ >nul 2>&1
echo README.md copied successfully.

REM 创建create_task.bat文件
copy src\create_task.bat build\x86\ >nul 2>&1
copy src\create_task.bat build\x64\ >nul 2>&1
echo create_task.bat copied successfully.

REM 显示生成文件信息
echo.
echo ========================================
echo Final Build Summary:
echo ========================================
echo 32-bit version: 
for %%A in (build\x86\process_monitor.exe) do echo   Size: %%~zA bytes
echo 64-bit version: 
for %%A in (build\x64\process_monitor.exe) do echo   Size: %%~zA bytes

echo.
echo Final build completed successfully!
echo Output files are located in:
echo   build\x86\process_monitor.exe
echo   build\x64\process_monitor.exe

pause