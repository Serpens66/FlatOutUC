@echo off
setlocal
if "%~1"=="" (
    "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Install-Steam.ps1"
) else (
    "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Install-Steam.ps1" -GameDir "%~1"
)
set "result=%errorlevel%"
pause
exit /b %result%
