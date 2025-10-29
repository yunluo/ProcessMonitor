@echo off
setlocal

:: Get the directory where the script is located
set "SCRIPT_DIR=%~dp0"
set "EXE_PATH=%SCRIPT_DIR%process_monitor.exe"

:: Check if process_monitor.exe exists
if not exist "%EXE_PATH%" (
    echo Error: %EXE_PATH% not found
    echo Please ensure this script is in the same directory as process_monitor.exe
    pause
    exit /b 1
)

:: Create scheduled task
echo Creating scheduled task...
schtasks /create /tn "ProcessMonitor" /tr "\"%EXE_PATH%\"" /sc minute /mo 5 /f

if %errorlevel% equ 0 (
    echo.
    echo Scheduled task created successfully!
    echo Task Name: ProcessMonitor
    echo Executable: %EXE_PATH%
    echo Frequency: Every 5 minutes
    echo.
    echo To modify the frequency, delete the existing task with:
    echo schtasks /delete /tn "ProcessMonitor" /f
    echo.
    echo To run the task immediately for testing:
    echo schtasks /run /tn "ProcessMonitor"
) else (
    echo.
    echo Failed to create scheduled task, error code: %errorlevel%
    echo You may need to run this script with administrator privileges
)

pause