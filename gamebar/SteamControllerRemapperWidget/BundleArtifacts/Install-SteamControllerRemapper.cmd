@echo off
setlocal
set "SCRIPT_DIR=%~dp0"

REM The user consented by running this installer, so clear the Mark-of-the-Web
REM from the bundle files. This avoids Windows blocking the bundled script,
REM executable, and widget package without silently stripping markers from
REM anything the user did not explicitly choose to run.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%SCRIPT_DIR%' -Recurse -File | Unblock-File" 2>nul

REM Determine whether we already have administrator rights.
net session >nul 2>&1
if %errorlevel% equ 0 goto :run

REM Not elevated. Relaunch once with a guard flag so we can never loop forever.
if /i "%~1"=="/elevated" (
    echo.
    echo Could not obtain administrator rights automatically.
    echo Please right-click "Install-SteamControllerRemapper.cmd" and choose
    echo "Run as administrator".
    echo.
    pause
    exit /b 1
)

echo Requesting administrator privileges...
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -ArgumentList '/elevated' -Verb RunAs"
exit /b 0

:run
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%Install-SteamControllerRemapper.ps1"
set "INSTALL_RESULT=%errorlevel%"

if not "%INSTALL_RESULT%"=="0" (
    echo.
    echo ============================================================
    echo  Installation FAILED ^(exit code %INSTALL_RESULT%^).
    echo  Review the messages above for the cause.
    echo ============================================================
    echo.
    pause
)
endlocal
