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

:: Try using schtasks first (Windows XP SP2 and later supports this)
echo Creating scheduled task using schtasks...
schtasks /create /tn "ProcessMonitor" /tr "\"%EXE_PATH%\"" /sc minute /mo 5 /f

if %errorlevel% equ 0 (
    echo.
    echo Scheduled task created successfully!
    echo Task Name: ProcessMonitor
    echo Executable: %EXE_PATH%
    echo Frequency: Every 5 minutes indefinitely
    echo.
    echo To modify the frequency, delete the existing task with:
    echo schtasks /delete /tn "ProcessMonitor" /f
    echo.
    echo To run the task immediately for testing:
    echo schtasks /run /tn "ProcessMonitor"
) else (
    echo.
    echo Failed to create scheduled task using schtasks, error level: %errorlevel%
    echo You may need to run this script with administrator privileges
    echo.
    
    :: Fallback for older Windows systems
    echo Trying alternative method for older systems...
    goto xp_support
)

goto end

:xp_support
:: For Windows XP/Server 2003 systems or when schtasks fails
echo Creating scheduled task using at command for hourly execution...

:: Get current username
for /f "tokens=2 delims=\" %%i in ('whoami') do set USERNAME=%%i

:: Create a VBS script to run the process monitor without showing console window
set "VBS_FILE=%SCRIPT_DIR%process_monitor_runner.vbs"
echo Set objShell = CreateObject("WScript.Shell") > "%VBS_FILE%"
echo objShell.Run """%EXE_PATH%""", 0, False >> "%VBS_FILE%"
echo.

:: Schedule the VBS script to run every hour (00, 01, 02, ..., 23)
setlocal enabledelayedexpansion
set count=0
for /l %%h in (0,1,23) do (
    set "hour=0%%h"
    set "time=!hour:~-2!:00"
    at !time! /interactive "%VBS_FILE%"
    set /a count+=1
)

if %errorlevel% equ 0 (
    echo.
    echo Successfully created !count! scheduled tasks!
    echo Tasks will run every hour (24 times per day)
    echo Tasks will run with current user: %USERNAME%
    echo Tasks will run without showing console window
    echo Executable: %EXE_PATH%
    echo VBS script: %VBS_FILE%
    echo.
    echo Note: The 'at' service must be running for scheduled tasks to work
    echo To view scheduled tasks:
    echo at
    echo.
    echo To delete all tasks, you'll need to delete each one individually using:
    echo at [JobID] /delete
) else (
    echo.
    echo Failed to create scheduled task, error level: %errorlevel%
    echo You may need to run this script with administrator privileges
    echo Or the Task Scheduler service may not be running
)

:end
pause