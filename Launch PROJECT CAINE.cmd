@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Launch-CAINE.ps1" %*
if errorlevel 1 (echo CAINE launch failed. & pause & exit /b 1)
