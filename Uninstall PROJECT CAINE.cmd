@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Uninstall-CAINE.ps1" %*
if errorlevel 1 (echo CAINE uninstall failed. & pause & exit /b 1)
pause
