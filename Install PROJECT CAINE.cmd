@echo off
if "%~1"=="" (
    powershell.exe -STA -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Install-CAINE.ps1" -ChooseGame
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Install-CAINE.ps1" %*
)
if errorlevel 1 (echo CAINE installation failed. & pause & exit /b 1)
pause
