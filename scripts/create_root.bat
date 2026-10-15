@echo off
set SCRIPT_DIR=%~dp0
mkdir "%SCRIPT_DIR%..\root_dev" 2>nul
xcopy "%SCRIPT_DIR%..\root" "%SCRIPT_DIR%..\root_dev" /E /H /C /I /Y
echo Initialized root_dev from root.