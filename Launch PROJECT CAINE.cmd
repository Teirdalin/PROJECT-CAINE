@echo off
if "%~1"=="" (
    powershell.exe -STA -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Launch-CAINE.ps1" -ChooseGame
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Launch-CAINE.ps1" %*
)
if errorlevel 1 (echo CAINE launch failed. & pause & exit /b 1)
