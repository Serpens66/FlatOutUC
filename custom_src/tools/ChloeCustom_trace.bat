@echo off
rem Startet FlatOut und protokolliert 20 Sekunden lang das Spielfenster. Das Spiel vorher schliessen, Steam muss laufen.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ChloeCustom_trace.ps1"
echo.
echo Fertig. Das Protokoll steht in ChloeCustom_trace.txt im Spielordner.
pause
