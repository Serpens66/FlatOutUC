@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-FlatOut-Vanilla.ps1" %*
set "result=%errorlevel%"
if not "%result%"=="0" pause
exit /b %result%
