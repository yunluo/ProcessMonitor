@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "EXE_PATH=%SCRIPT_DIR%process_monitor.exe"

rem Detect Windows version by parsing the major version from "ver" output
rem (locale-independent: works with Chinese "版本" and English "Version").
rem - Windows XP (5.x): Task Scheduler 1.0 has no /du switch and minute schedules
rem   on XP repeat for only 24 hours, so fall back to an hourly schedule.
rem   XP has no UAC and its schtasks does not support /RL either.
rem - Vista+ (major >= 6): run every 5 minutes with the highest run level.
set "RL_FLAG="
set "SCHEDULE=/sc minute /mo 5"
set "FREQ_DESC=Every 5 minutes"
for /f "tokens=2 delims=[]" %%a in ('ver') do (
    for /f "tokens=2" %%b in ("%%a") do (
        for /f "tokens=1 delims=." %%c in ("%%b") do (
            if "%%c"=="5" (
                set "SCHEDULE=/sc hourly /mo 1"
                set "FREQ_DESC=Every hour (Windows XP compatibility)"
            ) else (
                set "RL_FLAG=/RL HIGHEST"
            )
        )
    )
)

rem Detect run mode from INI: [global] mode=resident switches deployment to
rem a logon launcher + daily guard task (the exe then loops internally).
set "MODE=once"
if exist "%SCRIPT_DIR%process_monitor.ini" (
    findstr /i /b /c:"mode" "%SCRIPT_DIR%process_monitor.ini" 2>nul | findstr /i "resident" >nul 2>&1
    if not errorlevel 1 set "MODE=resident"
)

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

schtasks /query /tn "ProcessMonitor_Guard" >nul 2>&1
if %errorlevel% equ 0 (
    schtasks /delete /tn "ProcessMonitor_Guard" /f >nul
)

if "%MODE%"=="resident" goto create_resident

schtasks /create /tn "ProcessMonitor" /tr """%EXE_PATH%""" %SCHEDULE% %RL_FLAG%
goto check_create_result

:create_resident
schtasks /create /tn "ProcessMonitor" /tr """%EXE_PATH%""" /sc onlogon %RL_FLAG%
if %errorlevel% neq 0 goto check_create_result
schtasks /create /tn "ProcessMonitor_Guard" /tr """%EXE_PATH%""" /sc daily /st 03:00 %RL_FLAG%

:check_create_result
if %errorlevel% equ 0 (
    echo.
    if "%MODE%"=="resident" (
        echo Success: Tasks created - resident mode
        echo   ProcessMonitor       - launches the monitor at logon
        echo   ProcessMonitor_Guard - daily safety check at 03:00
    ) else (
        echo Success: Task "ProcessMonitor" created
        echo Frequency: %FREQ_DESC%
    )
    if defined RL_FLAG (
        echo Run level: Highest privileges (requires an administrator account)
    ) else (
        echo Run level: Default (Windows XP, no UAC)
    )
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
    if "%MODE%"=="resident" (
        echo      schtasks /create /tn "ProcessMonitor" /tr """%EXE_PATH%""" /sc onlogon %RL_FLAG%
        echo      schtasks /create /tn "ProcessMonitor_Guard" /tr """%EXE_PATH%""" /sc daily /st 03:00 %RL_FLAG%
    ) else (
        echo      schtasks /create /tn "ProcessMonitor" /tr """%EXE_PATH%""" %SCHEDULE% %RL_FLAG%
    )
)

endlocal
