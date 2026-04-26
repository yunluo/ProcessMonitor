@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "EXE_PATH=%SCRIPT_DIR%process_monitor.exe"

if not exist "%EXE_PATH%" (
    echo Error: "%EXE_PATH%" not found
    if /i "%PAUSE_ON_EXIT%"=="1" pause
    exit /b 1
)

echo Creating scheduled task...
echo Note: This uses the current user account (no password required)

schtasks /query /tn "ProcessMonitor" >nul 2>&1
if %errorlevel% equ 0 (
    echo Task "ProcessMonitor" already exists. Recreating it...
    schtasks /delete /tn "ProcessMonitor" /f >nul
    if %errorlevel% neq 0 (
        echo Failed to delete existing task "ProcessMonitor"
        if /i "%PAUSE_ON_EXIT%"=="1" pause
        exit /b 1
    )
)

schtasks /create /tn "ProcessMonitor" /tr """%EXE_PATH%""" /sc minute /mo 5

if %errorlevel% equ 0 (
    echo.
    echo Success: Task "ProcessMonitor" created
    echo Frequency: Every 5 minutes
    echo.
    echo Commands:
    echo   schtasks /query /tn "ProcessMonitor"  - View status
    echo   schtasks /run /tn "ProcessMonitor"    - Run now
    echo   schtasks /delete /tn "ProcessMonitor" /f - Delete
) else (
    echo.
    echo Failed to create task
    echo.
    echo Troubleshooting:
    echo   1. Right-click this script ^> Run as administrator
    echo   2. Ensure Task Scheduler service is running:
    echo      net start Schedule
    echo   3. If still failing, try creating manually:
    echo      schtasks /create /tn "ProcessMonitor" /tr """%EXE_PATH%""" /sc minute /mo 5
)

endlocal
