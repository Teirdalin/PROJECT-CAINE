@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Build.ps1" %*
if errorlevel 1 (echo CAINE build failed. & pause & exit /b 1)
pause
