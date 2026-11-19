@echo off
rem Startet FlatOut: Ultimate Carnage ohne Modpack (Vanilla). Nach dem Beenden des Spiels wird das Modpack
rem automatisch wieder aktiviert. Normal ueber Steam gestartet laeuft immer das Modpack.
rem
rem Was umgeschaltet wird:
rem   patch          -> patch.modpack_off        (Archivliste des Modpacks)
rem   *_gcp.asi      -> *_gcp.asi.modpack_off    (Plugins des Modpacks)
rem   Savegame       <-> Savegame_modpack / Savegame_vanilla (getrennte Spielstaende und Einstellungen)
rem Aktiv bleiben DisplayFix.asi (Fix fuer unsichtbares Fenster), der ASI-Loader und d3d9.dll (DXVK).
rem Steam muss laufen. Dieses Fenster bis zum Ende des Spiels offen lassen.
setlocal
cd /d "%~dp0"

if not exist "Fouc.exe" (
    echo Fouc.exe nicht gefunden. Diese Datei muss im Spielordner liegen.
    pause
    exit /b 1
)
tasklist /fi "imagename eq Fouc.exe" 2>nul | find /i "Fouc.exe" >nul
if not errorlevel 1 (
    echo FlatOut laeuft bereits. Bitte zuerst beenden.
    pause
    exit /b 1
)

rem falls ein frueherer Lauf abgebrochen wurde: zuerst den Modpack-Zustand wiederherstellen
call :modpack_on

if not exist "patch" (
    echo Datei "patch" fehlt - ist das Modpack installiert?
    pause
    exit /b 1
)
call :vanilla_on
if errorlevel 1 (
    echo Umschalten auf Vanilla fehlgeschlagen. Modpack wird wiederhergestellt.
    call :modpack_on
    pause
    exit /b 1
)

echo FlatOut startet OHNE Modpack. Dieses Fenster offen lassen.
echo Nach dem Beenden des Spiels wird das Modpack automatisch wieder aktiviert.
start "" /wait "Fouc.exe"

call :modpack_on
echo Modpack ist wieder aktiv.
timeout /t 3 >nul
exit /b 0


:vanilla_on
ren "patch" "patch.modpack_off" || exit /b 1
for %%F in (*_gcp.asi) do ren "%%F" "%%F.modpack_off" || exit /b 1
if exist "Savegame" ren "Savegame" "Savegame_modpack" || exit /b 1
if exist "Savegame_vanilla" ren "Savegame_vanilla" "Savegame" || exit /b 1
exit /b 0

:modpack_on
if exist "patch.modpack_off" if not exist "patch" ren "patch.modpack_off" "patch"
for %%F in (*_gcp.asi.modpack_off) do ren "%%F" "%%~nF"
if exist "Savegame_modpack" (
    if exist "Savegame" ren "Savegame" "Savegame_vanilla"
    ren "Savegame_modpack" "Savegame"
)
exit /b 0
