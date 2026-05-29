@echo off
chcp 65001 >nul
title MCG Client Launcher
mode con: cols=60 lines=25
color 0A

:menu
cls
echo.
echo    ╔══════════════════════════════════╗
echo    ║       MCG CLIENT LAUNCHER        ║
echo    ╚══════════════════════════════════╝
echo.
echo    ┌──────────────────────────────────┐
echo    │      Available Executables       │
echo    └──────────────────────────────────┘
echo.

if exist "Client/MCG_Client.exe" (
    echo    [1] V Main Client (MCG_Client.exe)
) else (echo    [1] X Main Client (MISSING!))

if exist "Client/ChatWindow.exe" (
    echo    [2] V Chat Window (ChatWindow.exe)
) else (echo    [2] X Chat Window (MISSING!))

if exist "Client/MapWindows.exe" (
    echo    [3] V Map Window (MapWindows.exe)
) else (echo    [3] X Map Window (MISSING!))

if exist "Client/StatusWindow.exe" (
    echo    [4] V Status Window (StatusWindow.exe)
) else (echo    [4] X Status Window (MISSING!))

echo.
echo    ┌──────────────────────────────────┐
echo    │          Launch Options          │
echo    └──────────────────────────────────┘
echo.
echo    [A] Launch ALL windows
echo    [C] Launch Main Client only
echo    [W] Launch Windows only (chat/map/status)
echo    [R] Check and repair (verify files)
echo    [X] Exit
echo.
choice /c ACWRX /n /m "Select option: "

if errorlevel 5 goto exit
if errorlevel 4 goto repair
if errorlevel 3 goto windows_only
if errorlevel 2 goto client_only
if errorlevel 1 goto launch_all

:launch_all
cls
echo.
echo Launching ALL MCG Client windows...
echo.
echo Step 1: Main Client...
echo U have 15 seconds to write local port for Main Client...
start "MCG Main Client" cmd /k "title MCG Main Client && Client\MCG_Client.exe"
timeout /t 10 >nul
echo Step 2: Chat Window...
start "MCG Chat Window" cmd /k "title MCG Chat Window && Client\ChatWindow.exe"
echo Step 3: Map Window...
start "MCG Map Window" cmd /k "title MCG Map Window && Client\MapWindows.exe"
echo Step 4: Status Window...
start "MCG Status Window" cmd /k "title MCG Status Window && Client\StatusWindow.exe"
echo.
echo All windows launched!
echo.
pause
goto menu

:client_only
cls
echo.
echo Launching Main Client only...
start "MCG Main Client" cmd /k "title MCG Main Client && MCG_Client.exe"
echo.
echo Main Client started!
echo.
pause
goto menu

:windows_only
cls
echo.
echo Launching Chat/Map/Status windows...
echo.
echo Note: Main Client must be running first!
echo.
timeout /t 2 >nul
start "MCG Chat Window" cmd /k "title MCG Chat Window && ChatWindow.exe"
start "MCG Map Window" cmd /k "title MCG Map Window && MapWindows.exe"
start "MCG Status Window" cmd /k "title MCG Status Window && StatusWindow.exe"
echo.
echo Windows launched!
echo.
pause
goto menu

:repair
cls
echo.
echo Checking files...
echo.
set MISSING=0
if not exist "MCG_Client.exe" (
    echo X MCG_Client.exe - MISSING
    set /a MISSING+=1
) else (
    echo V MCG_Client.exe - OK
)
if not exist "ChatWindow.exe" (
    echo X ChatWindow.exe - MISSING
    set /a MISSING+=1
) else (
    echo V ChatWindow.exe - OK
)
if not exist "MapWindows.exe" (
    echo X MapWindows.exe - MISSING
    set /a MISSING+=1
) else (
    echo V MapWindows.exe - OK
)
if not exist "StatusWindow.exe" (
    echo X StatusWindow.exe - MISSING
    set /a MISSING+=1
) else (
    echo V StatusWindow.exe - OK
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