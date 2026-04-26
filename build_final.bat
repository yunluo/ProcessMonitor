@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Process Monitor Build Script
echo ========================================

set ORIGINAL_PATH=%PATH%
set "MINGW_DIR=D:\Program Files (x86)\i686-5.3.0-release-win32-dwarf-rt_v4-rev0\mingw32\bin"
set PATH="%MINGW_DIR%";%PATH%

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
gcc src\process_monitor.c src\resource.res -o build\process_monitor.exe -lws2_32 -lgdi32 -lcomctl32 -lcomdlg32 -lwinhttp -mwindows -D_WIN32_WINNT=0x0501 -O2
if !errorlevel! neq 0 (
    echo Failed to build XP version
    pause
    exit /b 1
)
echo XP version built successfully.

echo.
echo Copying config files...
copy src\process_monitor.ini build\ >nul
copy src\create_task.bat build\ >nul
echo Config files copied.

echo.
echo ========================================
echo Build Summary:
echo ========================================
for %%A in (build\process_monitor.exe) do echo   XP:      %%~zA bytes
echo.
echo Done!
echo Output: build\process_monitor.exe

endlocal
pause
