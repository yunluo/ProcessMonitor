@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "EXE_PATH=%SCRIPT_DIR%process_monitor.exe"

if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

if not exist "%EXE_PATH%" (
    echo Error: %EXE_PATH% not found
    echo Please ensure this script is in the same directory as process_monitor.exe
    pause
    exit /b 1
)

echo Creating scheduled task...
schtasks /create /tn "ProcessMonitor" /tr "\"%EXE_PATH%\"" /sc minute /mo 5 >nul 2>&1

if %errorlevel% equ 0 (
    echo.
    echo Success: Task "ProcessMonitor" created
    echo Frequency: Every 5 minutes
    echo.
    echo Commands:
    echo   schtasks /query /tn "ProcessMonitor"  - View task
    echo   schtasks /run /tn "ProcessMonitor"    - Run now
    echo   schtasks /delete /tn "ProcessMonitor" /f - Delete
) else (
    echo.
    echo Failed to create task (error: %errorlevel%)
    echo Please run as administrator
)

endlocal
