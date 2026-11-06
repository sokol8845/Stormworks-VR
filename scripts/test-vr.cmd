@echo off
pwsh -NoProfile -File "%~dp0start-vr-test.ps1" %*
if errorlevel 1 pause
