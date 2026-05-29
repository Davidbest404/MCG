@echo off
chcp 65001 >nul
title MCG Server Launcher
mode con: cols=60 lines=25
color 0A

:menu
cls
echo.
echo    ╔══════════════════════════════════╗
echo    ║      MCG SERVER LAUNCHING        ║
echo    ╚══════════════════════════════════╝
echo.
echo    ┌──────────────────────────────────┐
echo    │       Available Executable       │
echo    └──────────────────────────────────┘
echo.

if exist "Server/MCG_Server.exe" (
    echo    V Main Server (MCG_Server.exe)
) else (echo    X Main Server (MISSING!))

echo.
echo    ┌──────────────────────────────────┐
echo    │          Launch Options          │
echo    └──────────────────────────────────┘
echo.
echo    [A] Launch Server
echo    [R] Check and repair (verify files)
echo    [X] Exit
echo.
choice /c ARX /n /m "Select option: "

if errorlevel 3 goto exit
if errorlevel 2 goto repair
if errorlevel 1 goto launch

:launch
cls
echo.
echo Launching MCG Server window...
echo.
start "MCG Main Client" cmd /k "title MCG Main Client && Server\MCG_Server.exe"
echo.
echo Server launched!
echo.
pause
goto menu

:repair
cls
echo.
echo Checking files...
echo.
set MISSING=0
if not exist "MCG_Server.exe" (
    echo X MCG_Server.exe - MISSING
    set /a MISSING+=1
) else (
    echo V MCG_Server.exe - OK
)
echo.
if %MISSING% GTR 0 (
    echo Found %MISSING% missing file(s)
    echo You need to compile the missing executables
) else (
    echo All files are present and ready!
)
echo.
pause
goto menu

:exit
cls
echo.
echo Exiting MCG Client Launcher...
echo.
timeout /t 2 >nul
exit