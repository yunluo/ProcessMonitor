@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Process Monitor Build Script
echo ========================================

set ORIGINAL_PATH=%PATH%
set "MINGW_DIR=D:\Program Files (x86)\i686-5.3.0-release-win32-dwarf-rt_v4-rev0\mingw32\bin"
set "MINGW64_DIR=D:\Program Files (x86)\llvm-mingw-20251021-msvcrt-x86_64\bin"
set "PATH=%MINGW_DIR%;%PATH%"

if exist build rmdir /s /q build 2>nul
mkdir build >nul 2>&1

echo.
echo Building 32-bit XP version...

echo Compiling resources...
windres -i src\resource.rc -o src\resource.res -O coff
if !errorlevel! neq 0 (
    echo Failed to compile resources
    pause
    exit /b 1
)
echo Resources compiled successfully.

echo Compiling and linking...
gcc src\process_monitor.c src\resource.res -o build\process_monitor.exe -mwindows -lpsapi -D_WIN32_WINNT=0x0501 -O2
if !errorlevel! neq 0 (
    echo Failed to build XP version
    pause
    exit /b 1
)
echo XP version built successfully.

set "X64_BUILT=0"
if exist "%MINGW64_DIR%\gcc.exe" (
    echo.
    echo Building 64-bit version...
    set "PATH=%MINGW64_DIR%;%ORIGINAL_PATH%"
    echo Compiling resources...
    x86_64-w64-mingw32-windres -i src\resource.rc -o src\resource_x64.res -O coff
    if !errorlevel! neq 0 (
        echo Failed to compile x64 resources
        pause
        exit /b 1
    )
    echo Compiling and linking...
    gcc src\process_monitor.c src\resource_x64.res -o build\process_monitor_x64.exe -mwindows -lpsapi -D_WIN32_WINNT=0x0501 -O2
    if !errorlevel! neq 0 (
        echo Failed to build 64-bit version
        pause
        exit /b 1
    )
    set "X64_BUILT=1"
    echo 64-bit version built successfully.
) else (
    echo.
    echo 64-bit toolchain not found, skipped x64 build
)

echo.
echo Copying config files...
copy src\process_monitor.ini build\ >nul
copy src\create_task.bat build\ >nul
echo Config files copied.

echo.
echo ========================================
echo Build Summary:
echo ========================================
for %%A in (build\process_monitor.exe) do echo   XP (x86): %%~zA bytes
if "%X64_BUILT%"=="1" for %%A in (build\process_monitor_x64.exe) do echo   x64:      %%~zA bytes
echo.
echo Done!
echo Output: build\process_monitor.exe
if "%X64_BUILT%"=="1" echo         build\process_monitor_x64.exe

endlocal
pause
