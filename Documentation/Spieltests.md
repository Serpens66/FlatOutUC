# Automatische Spieltests für FlatOut Ultimate Carnage

Der Runner startet die installierte Steam-Variante von Chloe Custom auf einem inaktiven Windows-Desktop. Er verwendet ein vorhandenes Profil. Das Spielfenster wird dem Testprozess zugeordnet und der aktive Eingabedesktop ungefähr alle 100 ms geprüft. Für den Lauf setzt der Runner die Audiopegel auf null und stellt Ladebildschirme auf automatisches Überspringen. Die Testskripte verwenden interne Menüaktionen.

## Starten

    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario Smoke
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario SinglePlayer
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario SteamHost
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario All

Steam muss in der Benutzersitzung angemeldet sein, FlatOut muss geschlossen sein. Im Codex-Sandboxprozess kann die Steam-Sitzung fehlen. Der Lauf braucht dann die Freigabe für die Ausführung außerhalb der Sandbox und Schreibzugriff auf die Spielinstallation. Der Runner verwendet standardmäßig Profilplatz 1. `-ProfileSlot 2` bis `-ProfileSlot 8` wählt einen anderen vorhandenen Platz. Den Build übernehmen .NET SDK 10 und MSVC x86. Der Runner überspringt die optionale Prüfung mit `luac50`, wenn Windows die EXE als Download markiert. Lua lädt und prüft das Szenario beim Spielstart.

| Szenario | Beobachtung |
|---|---|
| `Smoke` | Startbildschirm, Laden des vorhandenen Profils, Hauptmenü, fortlaufende EndScene-Aufrufe und regulärer Prozessausstieg |
| `SinglePlayer` | Optionen, Karriere und Carnage. Einzelrennen auf Midwest Ranch 1 mit Insetta, Skin 6 („Lei Bing“), zwei KI-Fahrern und mindestens zwei Sekunden Rennfortschritt. Rückkehr ins Menü. |
| `SteamHost` | Steam-Suche, öffentliche Host-Lobby, Midwest Ranch 1 mit drei KI-Fahrern, Fortschritt, Rückkehr in die Lobby und Steam-Lobbyaustritt |
| `All` | Die Einzelspieler- und Hostprüfungen in einem Spielprozess. |

Die Rennprüfungen senden nach belegtem Fortschritt das spielinterne Ereignis zum Verlassen des Rennens. Sie prüfen den Szenariostart, die Simulation und den Rückweg. Die native Probe speichert bei Rennfortschritt und hängenden Schritten aktuelle BMP-Aufnahmen aus dem D3D9-Renderpuffer. Sie dienen als Nachweis und für die Diagnose. Bildinhalte werden derzeit von Hand beurteilt.

## Protokoll und Wiederherstellung

Jeder vorbereitete Lauf schreibt `result.json`, `runner.log`, `probe.log` und vorhandene Spielprotokolle in `.work/t/flatout/<Lauf-ID>/`. Die drei Runner-Statuswerte sind `PASS` (Exit 0), `FAIL` (Exit 1) und `BLOCKED` (Exit 2). Auf dem inaktiven Desktop erlaubte Aktionen werden per Lua-Aktualisierung und über eine native Probe durchgeführt. Die Probe wird vorübergehend zusammen mit dem Test-BFS installiert und beim Aufräumen wieder entfernt. Sie prüft den bekannten EndScene-Aufrufweg und löst bei Rennen das normale Ereignis `0x2339` aus.

Der Runner sichert das ursprüngliche `patch`, die lokalen Einstellungen, Spielstände, Profile, Ghosts und betroffene Logs. Er überprüft die ursprünglichen Dateihashes nach dem Lauf. Zusätzlich vergleicht er die externen Steam-AutoCloud-Dateien unter `%LOCALAPPDATA%/FlatOut Ultimate Carnage/Savegame`. Die Steam-Cloud-Konfiguration bleibt erhalten. Szenarien, die Cloud-Fortschritt schreiben könnten, benötigen vor der Aufnahme in die Suite eine eigene Prüfung.

Liegt nach einer Unterbrechung noch `rollback.json` vor, die Installation vor einem weiteren Spielstart wiederherstellen.

    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -RestoreRun <Lauf-ID>

Die Start-, Timeout- und Wiederherstellungsprüfungen mit temporären Testdateien lassen sich unabhängig vom Spiel ausführen.

    pwsh -File custom_src/tools/flatout_game_test/Test-Runner.ps1

## Prüfstand

- 20.11.2026, Läufe `2b312d22f2de` und `a52e6c43ee0c`: Startbildschirm, vorhandenes Profil, Hauptmenü, fortlaufende Frames, regulärer Exit und Wiederherstellung geprüft.
- 20.11.2026, Lauf `994c5479a7d0`: Steam-Suche beantwortet, öffentliche Lobby erstellt, Midwest Ranch 1 mit drei KI-Fahrern simuliert, in die Lobby zurückgekehrt, Lobby geschlossen und Installation wiederhergestellt.
- 20.11.2026, Läufe `b4e627d6ff7e` und `900e232de231`: Jeweils `All` bestanden. Optionen, Karriere, Carnage, Einzelrennen mit „Lei Bing“ und zwei KI-Fahrern, Menü-Rückkehr, Steam-Host-Rennen mit drei KI-Fahrern und Lobby-Auflösung bestätigt. Je Rennen lag eine frische Renderpufferaufnahme vor. Die Wiederherstellung der Installation und der Vergleich der externen Cloud-Dateien wurden akzeptiert.
- 20.11.2026: Die vorbereiteten Runner-Tests für fehlenden Programmstart, Timeout und Wiederherstellung nach Unterbrechung bestanden.

## Weitere Szenarien

Derby-Zeitlimit, Stunt, Turnierwertung, Karriere- und Carnage-Events sowie weitere Fahrzeug-, Skin- und Streckenprüfungen benötigen jeweils eigene Szenarien. Die Hostprüfung belegt die lokale Hostseite. Ein Loopback-Test setzt zwei regulär parallel laufende Instanzen voraus. Für Steam-Beitritt und Synchronisation zwischen Spielern braucht es einen zweiten Steam-Teilnehmer.
