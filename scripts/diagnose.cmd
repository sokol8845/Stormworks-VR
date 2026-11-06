@echo off
cd /d "%~dp0\.."
python tools\diagnose.py
echo.
echo Reports are in the artifacts folder.
pause
