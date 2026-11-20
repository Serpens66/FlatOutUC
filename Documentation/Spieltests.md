# Automatische Spieltests für FlatOut Ultimate Carnage

Der Runner startet die installierte Steam-Variante von Chloe Custom auf einem inaktiven Windows-Desktop. Er verwendet ein vorhandenes Profil. Das Spielfenster wird dem Testprozess zugeordnet und der aktive Eingabedesktop ungefähr alle 100 ms geprüft. Für den Lauf setzt der Runner die Audiopegel auf null und stellt Ladebildschirme auf automatisches Überspringen. Die Testskripte verwenden interne Menüaktionen.

## Starten

    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario Smoke
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario SinglePlayer
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario SinglePlayer -VerifySteamMusic
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario SteamHost
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario All
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario Skins
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario Modes
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario LocalModes
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario Tournament
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario Tournament -MixedTournament
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario LocalModes -TrackSweep
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario LocalModes -FullCarnage
    pwsh -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario Smoke -ParallelLaunch

Steam muss in der Benutzersitzung angemeldet sein, FlatOut muss geschlossen sein. Im Codex-Sandboxprozess kann die Steam-Sitzung fehlen. Der Lauf braucht dann die Freigabe für die Ausführung außerhalb der Sandbox und Schreibzugriff auf die Spielinstallation. Der Runner verwendet standardmäßig Profilplatz 1. `-ProfileSlot 2` bis `-ProfileSlot 8` wählt einen anderen vorhandenen Platz. Den Build übernehmen .NET SDK 10 und MSVC x86. Der Runner überspringt die optionale Prüfung mit `luac50`, wenn Windows die EXE als Download markiert. Lua lädt und prüft das Szenario beim Spielstart.

Mit `-Scenario Smoke -ProfileSlot 2 -CreateProfile Testprofil` legt das Spiel in einem freien Platz ein neues Profil an. Der Runner exportiert die erzeugten Slotdateien nach `created-profile/` im Laufordner. Während des Tests bleibt eine vorhandene `player999.sav` gesichert, damit die Profilanlage mit frisch initialisierten Spieldaten startet. Bei Abschluss stellt der Runner den ursprünglichen Zustand wieder her.

| Szenario | Beobachtung |
|---|---|
| `Smoke` | Startbildschirm, Laden des vorhandenen Profils, Hauptmenü, fortlaufende EndScene-Aufrufe und regulärer Prozessausstieg |
| `SinglePlayer` | Optionen, Karriere und Carnage. Einzelrennen auf Midwest Ranch 1 mit Insetta, Skin 6 („Lei Bing“), zwei KI-Fahrern und mindestens zwei Sekunden Rennfortschritt. Rückkehr ins Menü. |
| `SteamHost` | Steam-Suche, öffentliche Host-Lobby, Midwest Ranch 1 mit drei KI-Fahrern, Fortschritt, Rückkehr in die Lobby und Steam-Lobbyaustritt |
| `All` | Die Einzelspieler- und Hostprüfungen in einem Spielprozess. |
| `Skins` | Steam-Lobby mit Fahrzeugklasse „Alle“, Markieren jeder Basis- und Wechselvariante und jeder angebotenen Lackierung im echten Vorschau-Menü. Pro Skin werden Start und Abschluss mit Auto-ID, Datenordner und Skin-Nummer protokolliert. |
| `Modes` | Steam-Host mit neun Rennregeln, drei Derby-Varianten und den zwölf Stunts. Prüft Konfiguration, laufende Simulation und Lobby-Rückkehr. Bei Derby wird das Zeitlimit am ScoreManager gemessen. Bei Stunt werden erwartete und tatsächliche Startposition sowie der Versuch geprüft. |
| `LocalModes` | Einzelrennen, Spezialspielregeln, Beispiele aus Karriere und Carnage sowie Party-Modus und Sofortaktion. Pro Fall werden Start, laufende Simulation und Menü-Rückkehr geprüft. |
| `Tournament` | Zwei Deathmatch-Derbys bis zum jeweils eingestellten Zeitlimit. Prüft Rückkehr, Punktevergabe, Zwischenstand und Abschluss des Turniers. |
| `Tournament -MixedTournament` | Zeit-Knockout, Deathmatch-Derby und High Jump. Prüft natürliche Eventabschlüsse sowie Zwischen- und Endwertung. Die native Probe liefert dem Spielprozess Steuerwerte für High Jump. |
| `LocalModes -TrackSweep` | Lädt die 58 Strecken inklusive Werkstatt-Teststrecke sowie die vom Spiel erkannten FO2-Geometrievarianten. Prüft Simulation auf der erwarteten Level-ID und Menü-Rückkehr. |
| `LocalModes -FullCarnage` | Startet jeden der 36 Carnage-Einträge mit dem normalen Rennstart-Callback. Der Test ruft den Callback auch bei gesperrten Events auf und prüft dadurch deren Ladevorgang. |
| `Smoke -ParallelLaunch` | Startet eine zweite reguläre Spielinstanz und prüft, ob beide Instanzen auf ihren inaktiven Desktops rendern. |
| `Smoke -ParallelLaunch -LoopbackClientProfile <created-profile>` | Verbindet den zweiten Spielprozess über die direkte IP-Verbindung auf `127.0.0.1:23757`. Prüft Lobbybeitritt, Übernahme von drei KI-Fahrern und Professional-Handling, Austritt und Schließen der Lobby. Als Client dient ein mit `-CreateProfile FOUCLoopback -ProfileSlot 2` exportiertes Profil. |
| `Smoke -ParallelLaunch -LoopbackClientProfile <created-profile> -LoopbackRace` | Ergänzt einen gemeinsamen Rennstart auf Midwest Ranch 1. Beide Prozesse müssen zur selben Zeit je einen lokalen Fahrer und vier weitere Einträge simulieren, weil Chloe den Mitspieler in einem KI-Slot darstellt. Beide kehren in die Lobby zurück. |

Mit `-SkinFrom` / `-SkinThrough` oder `-ModeFrom` / `-ModeThrough` lassen sich Bereiche wiederholen. Die Nummern stehen im Laufprotokoll. Beginnt ein Skin-Bereich mitten in einem Auto, werden die vorherigen Skins dieses Eintrags beim Durchschalten als Vorbereitung markiert.

`LocalModes -UseStarterFixtures` kann den Rennenstest hinter einem blockierten Karriere- oder Party-Einstieg ausführen. Dazu setzt der Runner im vorhandenen Profil für den Lauf ein Starterauto im Arbeitsspeicher. Ein solcher Lauf prüft das Rennen und den Menü-Rückweg ab dieser Stelle.

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
- 20.11.2026, Läufe `580011619f84` und `e8e9304bbe5e`: Alle 543 Vorschaupositionen einzeln belegt. Das sind 523 eindeutige Fahrzeug-Skin-Paare auf 90 Modellen, davon 44 FO2-Modelle. Für FO2 Lancea, Skin 2, fehlte im ersten Protokoll eine Abschlusszeile. Der isolierte Nachtest bestand. Der Runner liest das Skin-Protokoll inzwischen nach dem Spielausstieg vollständig aus, um Schreibkonflikte zu vermeiden.
- 20.11.2026, Läufe `31cdc10980f6`, `07c10a1cab3f`, `bee2bf2beddf` und `d9ae56642bea`: Alle 24 Multiplayer-Fälle mit Fortschritt und Rückkehr ins Lobby-Menü belegt. Bei allen drei Derbys lag das gewählte Limit bei 60.000 ms. Bei allen zwölf Stunts stimmte der reale Startplatz mit der Strecke überein und Versuch 1 lief.
- 20.11.2026, Lauf `daccb06c7c11`: Zwei Deathmatch-Derbys mit je 60.000 ms schlossen regulär ab. Die Zwischenwertung enthielt 29 Punkte über vier Fahrer, die Endwertung 58. Beide Wertungen erschienen nach dem Neuladen des Menüs. Das zweite Event wurde automatisch gewählt.
- 20.11.2026: Die neun lokalen Einzelspielregeln und je ein Carnage-Rennen, -Stunt, -Derby und -Beat-the-Bomb wurden mit laufender Simulation und Menü-Rückkehr geprüft. Nachweise in `57e492e2fbea`, `0c19335fbd6a`, `f7e1739a52c1`, `ad4173c51f92` und `f05d94c9165a`.
- 20.11.2026: Zehn Mod-BFS mit `BfsTool verify` fehlerfrei geprüft. Alle 62 vorgesehenen Vanilla- und FO2-Autopreise stimmen mit der installierten Datenbank überein. Für alle 58 vorgesehenen Level waren Geometrie- und Datenpfade in den geladenen Archiven vorhanden.
- 20.11.2026, aktuelles Steam-Paket mit dem neu angelegten Profil: `All` bestand (`80f91b806910`). Alle 543 Skin-Vorschauen über 100 Menüeinträge bestanden (`f881652839bf`). Alle 24 Steam-Hostfälle bestanden einschließlich der drei Minutenlimits und zwölf Stunt-Startpositionen (`ac2c30912c1a`). Alle zehn installierten Mod-BFS waren bytegleich zu den Paketdateien und bestanden `BfsTool verify`.
- 20.11.2026, Lauf `9459b378751e`: Das Turnier aus zwei Deathmatch-Derbys lief erneut bis zu beiden Minutenlimits. Die Zwischen- und Endwertungen ergaben insgesamt 29 und 58 Punkte.
- 20.11.2026, Lauf `da656b07cac3`: Der Einzelspieler-Lauf mit der stummen Prüfung der 20 korrigierten Menü- und Rennsongs bestand erneut.
- 20.11.2026, Lauf `3fb7f7f1c462`: Zwei reguläre Spielprozesse aus derselben Installation renderten gleichzeitig auf getrennten inaktiven Desktops. Die Spielstand- und Installationsdateien wurden anschließend per Hash geprüft.
- 20.11.2026, Lauf `82b527116279`: Alle 36 Carnage-Events erreichten laufende Simulation. Die fehlende Rückkehrmarkierung für Event 20 ist durch den protokollierten Hauptmenüwechsel und den Start von Event 21 belegt. Event 20 bestand zusätzlich im Einzellauf `202fee98f6b4`. Der Runner bestätigt die Rückkehr jetzt auch über den nächsten nachgewiesenen Testschritt.
- 20.11.2026, Lauf `749c73923b80`: Alle 116 Streckenstarts mit den 58 Level-IDs und ihren 58 FO2-Varianten erreichten laufende Simulation. Der Rückweg für Fall 21 ist über Hauptmenü und Start des Folgefalls belegt. Der Runner hatte zu diesem Zeitpunkt noch den einzelnen Protokolleintrag für jede Rückkehr verlangt. Die FO2-Variante der Werkstatt-Teststrecke bestand im gezielten Nachtest mit expliziter Umschaltung (`a87934f21f2e`).
- 20.11.2026, Lauf `6683203c38a0`: Beide Instanzen traten derselben lokalen IP-Lobby bei. Der Client übernahm drei KI-Fahrer und das abweichende Professional-Handling vom Host. Client-Austritt und Lobbyende wurden bestätigt.
- 20.11.2026, Lauf `a1a3a4720113`: Gemeinsamer lokaler Rennstart auf Midwest Ranch 1 mit zwei Spielinstanzen und drei KI-Fahrern, paralleler Rennfortschritt, Rückkehr beider Instanzen in die Lobby sowie Austritt und Schließen der Lobby bestanden.
- 20.11.2026, Lauf `79ce7b39712b`: Zeit-Knockout und Deathmatch-Derby eines gemischten Turniers endeten regulär. Beim zweiten Event lieferte die bisherige Rangabfrage wegen eines Gleichstands keinen Namen für Platz 2. Die Turnierwertung übersprang diesen Platz und stand bei 53 statt 58 Punkten.
- 20.11.2026, Lauf `d20d162fe521`: Das gemischte Turnier lief mit Zeit-Knockout, Deathmatch-Derby und High Jump bis zur Endwertung. Die drei High-Jump-Versuche endeten durch Zeitablauf. Das Spiel erzeugte alle drei Rundendatensätze und zählte die lokale Rangliste mit null fehlenden Datensätzen. Die Wertungen ergaben 29, 58 und 80 Punkte. Der Runner bewertet den Wechsel zum nächsten Stuntversuch als Fortschritt.
- Am 20.11.2026 bestand das gemischte Turnier auch mit erzwungenem Gleichstand im Derby (`db68e2ba6a6d`, `-Scenario Tournament -MixedTournament -ForceTournamentTie`). Im Derby erschienen alle vier Fahrer in der Wertung. Die Prüfungen bestätigten 29, 58 und 80 Punkte nach den drei Events sowie das Schließen der Steam-Lobby.
- Am 20.11.2026 bestand der Kontrolllauf `All` im neu gebauten Steam-Paket (`af83b0c4247a`). Der Runner bestätigte die Menüprüfungen, das Einzelrennen mit „Lei Bing“ und zwei KI-Fahrern sowie das Steam-Host-Rennen mit drei KI-Fahrern.

## Befund zu den Steam-Soundtracks

Der Steam-Build schreibt die Pfade zu 20 Songs auf `Data/Songs` um. Betroffen sind jeweils 15 Einträge in `playlist_ingame`, `playlist_ingamecombined` und `playlist_ingamefouc` sowie jeweils fünf in `playlist_title`, `playlist_titlecombined` und `playlist_titlefouc`. Die korrigierten Playlists werden im Steam-Archiv am Ende von `patch` geladen.

- 20.11.2026: Alle 259 Verweise in den 14 konfigurierten Playlists und der zusätzlichen Carnage-Variante `playlist_ingamemodern2` in der installierten Archivfolge und den losen Dateien gefunden. Alle 20 betroffenen OGG-Dateien ließen sich für jeweils drei Sekunden dekodieren. Die Protokolle liegen unter `.work/t/flatout/current-audit/`.
- 20.11.2026, Lauf `76065a765aa6`: `SinglePlayer -VerifySteamMusic` wählte die FOUC-Playlists für Menü und Rennen. Das laufende Spiel übernahm alle fünf Menü- und 15 Rennsongs mit `data/songs/` in seine Playlists. Die Audiopegel standen für den Hintergrundtest auf null.

## Befunde beim Einstieg in Karriere, Party und Sofortaktion

- Mit dem vorherigen Profil auf Platz 1 waren null Autos gekauft und null Starter im Karriereshop auswählbar. Der normale Karriere-Callback reichte eine leere Liste durch. `CarSelectionGenericMenu.bed:852` griff beim Öffnen auf das fehlende erste Auto zu. Reproduziert im Modesweep und isoliert im Lauf `f797df0b981b`.
- Mit im Test bereitgestelltem Chili startete der erste Karriere-Cup auf Pinegrove 1 mit elf KI-Fahrern und kehrte ins Menü zurück. Nachweis im Lauf `917dc53adf79`.
- Mit demselben Profil greift der Party-Einstieg in `PartyModeMenu.bed:68` auf das fehlende erste freigeschaltete Stuntauto zu. Der Test endet am Lua-Fehlerdialog. Nachweis im Lauf `ad4173c51f92`.
- Mit im Test bereitgestelltem Rocket startete High Jump mit zwei lokalen Spielern und kehrte ins Menü zurück. Nachweis im Lauf `f05d94c9165a`.
- Sofortaktion beendet `Fouc.exe` beim Rennenstart mit `0xC0000094` (Integer-Division durch null). Der Fehler trat nach dem Party-Stunt auf und wurde über den normalen Menüeintrag aus einem frischen Spielstart reproduziert. Nachweise in `f05d94c9165a` und `187315c24977`.
- Die alte `player999.sav` führte bereits beim Speichern eines neuen Profils zum Absturz im nativen Carnage-Fortschrittscheck (`Fouc.exe+0x87D13`, Lauf `51fa3225555d`). Ohne diese Arbeitsdatei legte das unveränderte Spiel ein neues Profil an (`b8da13304db7`). Damit bestanden der erste Karriere-Cup inklusive Chili-Kauf für 4.500 Dollar und elf KI-Fahrern, High Jump mit zwei lokalen Spielern und Sofortaktion mit elf KI-Fahrern. Nachweise `e6668f60f5b2`, `126e0ad4f22f` und `ec2bd79f5e92`.
- 20.11.2026: Mit dem neu gebauten Steam-Paket wurde `xcxc` auf Profilplatz 1 neu angelegt (`7ef0317550f7`). Karriere, Party-Modus und Sofortaktion bestanden erneut (`e95afbd2c9fe`, `13fb8459fa41` und `791c1131b6bf`). Das alte Profil und die alte Arbeitsdatei liegen mit SHA-256-Dateiliste in `.work/t/flatout/profil-vor-reset-2026-11-20-final/`.
- Der kombinierte Lauf `48bb08eb886a` mit dem neuen Profil bestand die ersten 15 lokalen Fälle. Sofortaktion stürzte nach der Rückkehr aus dem Party-Modus auf `FlatOutUCChloeCollection_gcp.asi+0xB6A93` ab. Das ist die Modulo-Operation in `RandomizeInstantActionSkin`; die Skin-Anzahl des zufällig gewählten Fahrzeugs war null.
- Im isolierten Nachtest `a7f29c891c98` reproduziert. Sofortaktion wählte den aus dem Custom-Paket entfernten Volvo Amazon (Datenbank-ID 109, `Car_405`) für Pinegrove 3 (Level 6). Der Skin-Zähler war null.
- Am 20.11.2026 bestanden der Sofortstart nach einem Party-Stunt und der direkte Einstieg aus dem Hauptmenü jeweils mit elf KI-Fahrern und Rückkehr ins Menü (`658c7b94ec8a`, `46bc164309ea`). Der Runner prüfte alle 68 Fahrzeuge der Kandidatenliste und bestätigte jeweils, dass das tatsächlich geladene Auto und sein Skin aus der gültigen Auswahl kamen.
- Der direkte Sofortstart bestand zusätzlich mit der nun verpflichtenden Prüfung der Kandidatenliste (`24b425b5e07c`). Anschließend stimmten die SHA-256-Hashes aller 499 installierten Paketdateien mit dem Build überein. Die 58 zuvor erfassten Dateien für Profil, Spielstände und Einstellungen entsprachen dem Stand vor den Korrekturen.

## Weitere Szenarien

Ein Szenariostart prüft die ersten Sekunden und den Menü-Rückweg. Vollständige Rennen, Effektprüfungen während des Fahrens, Freischaltungen und alle Karriere- sowie Carnage-Events benötigen gezielte Läufe. Die Hostprüfung belegt die lokale Hostseite. Ein Loopback-Test setzt zwei regulär parallel laufende Instanzen voraus. Für Steam-Beitritt und Synchronisation zwischen Spielern braucht es einen zweiten Steam-Teilnehmer.
