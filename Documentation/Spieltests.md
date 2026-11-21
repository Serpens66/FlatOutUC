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
- Am 20.11.2026 stürzte die Profilerstellung auch auf `LENOVO_SERP` ab. Der SHA-256-Vergleich ergab, dass `Savegame/player999.sav` auf dem Laptop bytegleich zu der Datei aus dem reproduzierten Host-Absturz `51fa3225555d` ist. Beim fehlgeschlagenen Speichern wurden zwei `customsave001.*`-Dateien geschrieben, `player001.sav` fehlte danach. Die Diagnose auf dem Laptop war rein lesend.
- 20.11.2026: Mit dem neu gebauten Steam-Paket wurde `xcxc` auf Profilplatz 1 neu angelegt (`7ef0317550f7`). Karriere, Party-Modus und Sofortaktion bestanden erneut (`e95afbd2c9fe`, `13fb8459fa41` und `791c1131b6bf`). Das alte Profil und die alte Arbeitsdatei liegen mit SHA-256-Dateiliste in `.work/t/flatout/profil-vor-reset-2026-11-20-final/`.
- Der kombinierte Lauf `48bb08eb886a` mit dem neuen Profil bestand die ersten 15 lokalen Fälle. Sofortaktion stürzte nach der Rückkehr aus dem Party-Modus auf `FlatOutUCChloeCollection_gcp.asi+0xB6A93` ab. Das ist die Modulo-Operation in `RandomizeInstantActionSkin`; die Skin-Anzahl des zufällig gewählten Fahrzeugs war null.
- Im isolierten Nachtest `a7f29c891c98` reproduziert. Sofortaktion wählte den aus dem Custom-Paket entfernten Volvo Amazon (Datenbank-ID 109, `Car_405`) für Pinegrove 3 (Level 6). Der Skin-Zähler war null.
- Am 20.11.2026 bestanden der Sofortstart nach einem Party-Stunt und der direkte Einstieg aus dem Hauptmenü jeweils mit elf KI-Fahrern und Rückkehr ins Menü (`658c7b94ec8a`, `46bc164309ea`). Der Runner prüfte alle 68 Fahrzeuge der Kandidatenliste und bestätigte jeweils, dass das tatsächlich geladene Auto und sein Skin aus der gültigen Auswahl kamen.
- Der direkte Sofortstart bestand zusätzlich mit der nun verpflichtenden Prüfung der Kandidatenliste (`24b425b5e07c`). Anschließend stimmten die SHA-256-Hashes aller 499 installierten Paketdateien mit dem Build überein. Die 58 zuvor erfassten Dateien für Profil, Spielstände und Einstellungen entsprachen dem Stand vor den Korrekturen.

## Weitere Szenarien

Am 20.11.2026 wurden im Steam-Build die Derby-Startplätze für zwei lokale Spielprozesse vermessen. Auf allen sechs Karten wurden mit zwei Spielern und vier KI-Fahrern sechs unterschiedliche Startkoordinaten gelesen. Die beiden Prozesse sahen jeweils ihre eigene und die anderen Positionen in derselben Zuordnung. Mit neun Autos auf Gas Station Derby waren erst die acht Plätze belegt, danach wurde Platz 1 wiederverwendet. Nachweise `1f07b4030caa`, `58eb1149963c`, `85d83efe9b73`, `797065eb295d`, `cfb6246b76a4`, `4d71f763e1e4` und `170ab3d0800a`.

Am 21.11.2026 prüfte Lauf `e3d56f32f0ff` nach dem Ausbau von San Juan Derby wieder sechs Derby-Karten in beiden Menüs. Auf Gas Station Derby simulierten beide lokalen Prozesse zwei Spieler und sieben KI-Fahrer. Das neunte Fahrzeug belegte wieder Startplatz 1, und beide Prozesse kehrten in die Lobby zurück. Die Testinstallation wurde anhand ihrer Hashes wiederhergestellt. Der frühere Test der San-Juan-Arena bleibt unter `a1388c9896e2` dokumentiert.

Die Derby-Resetwerte in Vanilla und in der gepackten `game.db` stimmen überein. `DerbyStandingStillResetTimeMs` ist 5.000, `DerbyResetThrottleThreshold` 0,2 und `DerbyResetThrottleTimeMs` 750. Die native Reset-Prüfung berücksichtigt außerdem die Startphase `DerbyStartImmunityTimeMs` von 15.000 ms. Der native HUD-Code zeichnet die eigene Lebensleiste im Derby-Zweig. Das normale Renn-Tachometer zeichnet Drehzahl, Nitro, Geschwindigkeit und Gang. Die Warnung für kritischen Schaden ist ein eigener HUD-Bestandteil.

Am 20.11.2026 zeigte ein neu angelegtes Profil km/h, hohe Fahrzeugkamera und Automatik, auch nach erneutem Laden in einem frischen Prozess (`40c968bed2f3`, `4eb5255796df`). Ein separates, älteres Profil behielt mph und die niedrige Kamera (`469d2b46b76e`).

Am 20.11.2026 zeigten Renderaufnahmen im Derby beide Tachovarianten mit halb gefüllter Nitro-Leiste und aktivierter Flamme. Für die gerade Derby-Leiste verwendet jetzt auch der FlatOut-2-Tacho die passenden Flammenparameter (`a55439ae0b73`, `9a8f2d2be0d4`).

Beim kontrollierten Aufprall in Gas Station Derby verlor das getroffene KI-Auto bei `0x` keine Lebenspunkte, bei `0.1x` rund 5,9 Prozent und bei `1.0x` rund 59,3 Prozent (`ef9f7b24c59c`, `df66a3e62a5e`, `2c7fb9a63259`). Die Tests verwendeten die Menüauswahl des Hosts, prüften die Auswahl in den Client-Sitzungsdetails und lasen den aktiven Multiplikator in beiden Prozessen aus.

`-Scenario SteamPeer` öffnet eine Steam-Lobby für Gas Station Derby mit vier KI und `0.1x`. Der Test wartet auf den namentlich zugelassenen Teilnehmer (`-SteamPeerName`, Standard `Laptop`) und startet, sobald dieser sich auf bereit setzt. Der erstmalige Wartelauf `feb36318460c` wurde ohne Client-Beitritt beendet und stellte die Installationsdateien wieder her.

Der abschließende Lauf `All` (`ca48b90f33af`) bestätigte Optionen, Karriere- und Carnage-Menü, ein Einzelrennen mit zwei KI, den Steam-Host mit drei KI und die Wiederherstellung der Installation. Der Dachlieger im direkten Vanilla-Vergleich und eine Steam-Verbindung zwischen den zwei Rechnern sind noch offen. Stunt-Zuschauen benötigt eine geprüfte Darstellung des entfernten Autos und der Fahrerfigur. Die vorhandenen Stunt-Mitspieler sind reine Wertungseinträge, daher enthält dieser Build noch keinen Zuschauermodus.

### Lokaler Stunt-Pose-Test

```powershell
pwsh -NoProfile -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario LocalModes -ModeFrom 4 -ModeThrough 4 -StuntPose
```

Der Runner beschleunigt den Testwagen in High Jump und löst den nativen Abschuss aus. Anschließend zeichnet er aus interpolierten Aufnahmen visuelle Drahtmodelle für Anfahrt, Abschuss, Flug und Landung. Ein zweites synthetisches Ziel nutzt dieselbe Aufnahme mit 350 ms Verzögerung. Der Test prüft das Umschalten auf dieses Ziel, das Verwerfen von Daten beim Eventwechsel und unveränderte Stunt-Wertungsfelder während des Zeichnens.

Am 20.11.2026 bestanden die Hintergrundläufe mit je 73 Auto- und 54 Fahrerposen (`d4d49369dc77`, `ec0e5794d9a9`). `probe.log.stunt.csv` enthält die Messwerte. `probe.log.005.bmp` bis `probe.log.009.bmp` zeigen die Diagnoseansichten im Renderpuffer. Die Installationsprüfung nach den Tests bestand. Das Testbild verwendet Drahtmodelle und eine eigene Kamera. Die Anbindung der Spielmodelle, Steam-Übertragung und die Abnahme des Ergebnislisten-Buttons auf zwei PCs stehen aus.

Ein Szenariostart prüft die ersten Sekunden und den Menü-Rückweg. Vollständige Rennen, Effektprüfungen während des Fahrens, Freischaltungen und alle Karriere- sowie Carnage-Events benötigen gezielte Läufe. Die Hostprüfung belegt die lokale Hostseite. Ein Loopback-Test setzt zwei regulär parallel laufende Instanzen voraus. Für Steam-Beitritt und Synchronisation zwischen Spielern braucht es einen zweiten Steam-Teilnehmer.

Die Steam-Vorschau zur Bild- und Tonübertragung wurde am 21.11.2026 entfernt. Host und Laptop erhielten dieselbe bereinigte Plugin-Datei und bytegleich das reguläre Multiplayer-Archiv. High Jump in der Steam-Host-Lobby bestand im Hintergrundlauf `2d08e06d5a48`. Der lokale Stunt-Pose-Test `ba478472dde8` zeichnete 73 Fahrzeug- und 54 Fahrerposen auf und spielte sie erfolgreich ab.

### Stunt-Ghosts

```powershell
pwsh -NoProfile -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario LocalModes -ModeFrom 4 -ModeThrough 4 -StuntGhostProps
pwsh -NoProfile -File custom_src/tools/Invoke-FlatOutGameTest.ps1 -Scenario LocalModes -ModeFrom 4 -ModeThrough 4 -StuntGhostAsset
```

Die beiden lokalen Prüfungen bleiben im separaten Test-Plugin. `-StuntGhostAsset` prüft auch Paketvalidierung, Interpolation, Vorhersage und Versuchswechsel. Für einen manuellen Steam-Test mit demselben Build auf beiden PCs High Jump starten und beide Richtungen prüfen. Erwartet werden farbige, durchscheinende Ghost-Autos, eine Figur nach dem Abschuss auch weit entfernt vom Auto sowie die letzte gültige Pose bis der sendende Spieler die Ergebnisliste öffnet. Beim nächsten Versuch muss wieder das Fahrzeug erscheinen. In Bowling kann derselbe Ablauf Gegenstände und Stunt-Wertung mit abdecken.

Am 21.11.2026 zeigte High Jump im Lauf `e3faaf874a79` die aufgezeichnete Bewegung mit Rockets nativem Modell und Skin. Die Renderaufnahmen belegen die Darstellung neben dem lokalen Wagen und die Überlappung. Der lokale Wagen fuhr dabei mit 22,95 bis 21,04 m/s weiter. Der Schaden blieb null und der lokale Stunt-Datensatz unverändert. In Bowling blieben Position und Ausrichtung des echten Pins sowie der lokale Stunt-Datensatz über je 17 gerenderte Überlappungsframes unverändert. Das bestand mit Rocket (Lauf `086d810a9814`, Fahrzeug-ID 37) und mit einem zweiten Stuntwagen (Lauf `fffdd0c9a529`, Fahrzeug-ID 34). Beide verwendeten die jeweils verfügbare Lackierung 1. Alle drei Läufe stellten die Installation hashgeprüft wieder her. [Renderpfad und Grenzen der Vorschau](Reverse-Engineering.md#stunt-ghosts-im-nativen-renderer)

Im Lauf `5b2d337165b2` fuhr der Rocket neben dem separat aus dem Spielarchiv geladenen Fahrzeug `car_1` mit Skin 2. Die Probe zeigte 38 Frames des Ghosts, der aus lokal serialisierten Fahrzeugposen animiert wurde. Die Renderaufnahme `probe.log.002.bmp` zeigt Modell und Lackierung. Beim Überlappen blieben Fahrzeugschaden und Stunt-Datensatz unverändert. Lauf `f12e6ef3fcd5` prüfte außerdem das Entladen von `car_1`, den Wechsel zu `car_2` und das erneute Laden von `car_1`.

Am 21.11.2026 zeigte High Jump in einer echten Steam-Host-Lobby gleichzeitig drei Ghosts mit künstlichen Eingabedaten für Rocket, `car_1` mit Skin 2 und `car_201` mit Skin 1. Lauf `9bc00a00d4d1` bestätigte die drei Draw-Pfade, die Rückkehr in die Lobby und die Freigabe beider separat geladenen Fahrzeugressourcen. Der Runner prüfte die Dateihashes nach der Wiederherstellung. Die Empfangslogik wird mit getrennten Teilnehmern, Versuchswechsel, Paketverlust, verspäteten und doppelten Updates sowie neuen Eventkennungen geprüft. Der Zwei-PC-Test muss den tatsächlichen Steam-Empfang und die Anzeige im jeweils anderen Spiel bestätigen.

Der inzwischen entfernte Kamera-Prototyp wurde im Lauf `4541b4359fe8` geprüft. `probe.log.002.bmp` dokumentiert die versetzte Perspektive auf High Jump.

Der Zwei-PC-Lauf `9d294b91f7a1` bestätigte den Steam-Poseaustausch in beide Richtungen. Der Host zeichnete das Fahrzeug des Laptops (`car_39`, Skin 1), der Laptop den Rocket des Hosts (`car_37`, Skin 1). Beide Logs enthalten dieselbe Eventkennung und die Freigabe der separat geladenen Modelle nach der Rückkehr ins Menü. Der Nutzer bestätigte die Sichtbarkeit am Laptop und meldete, dass die Darstellung deutlich von einem Zeitfahr-Ghost abweicht. Die automatische Prüfung umfasst Empfang, Draw-Aufruf und Rückkehr, die optische Abnahme bleibt offen. Host und Laptop wurden nach Spielende hashgeprüft wiederhergestellt.

Die überarbeitete Darstellung rendert mit den nativen Transparenzfeldern für Ghost-Fahrzeuge. Die Aufnahme `2d728213b721/probe.log.003.bmp` zeigt den durchscheinenden Ghost während der Überlappung. Der lokale Lauf prüfte außerdem vier Räder, unveränderten Schaden und Stunt-Datensatz sowie das Freigeben und erneute Laden der separat geladenen Modelle. Im Steam-Hostlauf `ddd04bcd8fb4` wurde der einheitliche Renderpfad mit dem zum lokalen Wagen identischen Rocket sowie `car_1` und der FO2-Variante `car_201` geprüft. Alle drei Ghosts wurden mit je vier Rädern gerendert und nach der Rückkehr ins Menü freigegeben. `probe.log.001.bmp` zeigt die durchscheinenden Autos neben dem eigenen Rocket. Beide Läufe stellten die Installation hashgeprüft wieder her. Die neue Optik wurde auf einem PC geprüft.

Der Wiederholungslauf `86a888c61182` reproduzierte die drei durchscheinenden Ghosts. Die Aufnahme lässt sich mit der früheren deckenden Darstellung aus `9bc00a00d4d1` vergleichen. Im Bowling-Lauf `dba3e353bb88` verwendete die Überlappungsprüfung nun den separat geladenen Ghost des aktuellen Renderers einschließlich Rädern. Position und Ausrichtung des Pins sowie der lokale Stunt-Datensatz blieben über 17 gerenderte Überlappungsframes unverändert. Beide Läufe stellten die Installation hashgeprüft wieder her.

Beim erneuten Zwei-PC-Lauf `6ddf497c42f9` meldeten beide Rechner denselben Stunt und jeweils vier gerenderte Räder. Der Nutzer sah am Laptop weiterhin die alte Anmutung. Das automatische Ergebnis bestätigt weiterhin nur die Daten- und Renderpfade. Beide Installationen verwenden dieselbe DXVK-Datei und dasselbe Grafikpreset, die NVIDIA-GPUs und Treiberversionen unterscheiden sich. Host und Laptop wurden hashgeprüft wiederhergestellt.

Im Diagnoselauf `379b0b950620` wurden erstmals die Originalauflösungen beider Rechner erfasst. Die Aufnahmen zeigen transparente Fahrzeuge, die sich auf dem gemeinsamen Startplatz überlagern. Die zwei Aufnahmen im Abstand von 800 ms liegen noch vor dem ausreichenden Herausfahren des Clients. Der Nutzer beobachtete starkes Flackern auf dem Laptop. Beide Installationen wurden hashgeprüft wiederhergestellt.

Die damalige Vorschau erfasste pro Rechner bis zu vier `FlatOutGhostView*.bmp` in Originalauflösung im Abstand von 1,5 Sekunden. Das zeigte die Rastertransparenz auch nach dem Auseinanderfahren. Die Ein-PC-Variante speiste künstliche Posen für drei Autos und zwei Fahrer ein. Die Schalter `-StuntGhostSteam` und `-StuntGhostCapture` wurden für den Release entfernt. Der allgemeine Test-Probe kann weiterhin Bilder auf Anforderung speichern.

Der Ein-PC-Lauf `5a1513703dda` bestätigte das Ausblenden bei überlappender Startpose und das fortgesetzte Zeichnen bei absichtlich ausgelassenem Steam-Update. Er erzeugte vier Bilder in 3840×2160 mit jeweils rund 1,5 Sekunden Abstand und stellte die Installation hashgeprüft wieder her. Die praktische Flackerprüfung am Client bleibt Teil der Zwei-PC-Abnahme.

Im Zwei-PC-Lauf `e5f5324c3580` entstanden auf beiden Rechnern vier Aufnahmen im Abstand von 1,5 Sekunden. Die ersten drei Laptopbilder zeigen den eigenen Wagen ohne sichtbar abgezeichneten Ghost. Bild 4 zeigt den Rocket durchscheinend und getrennt. Die Hostbilder zeigen seinen eigenen Rocket. Der Host meldete die Einsendung des Client-Wagens an den Renderer. Der Nutzer bestätigte, dass das Flackern weg war, und benannte den Kontaktschutz beim Zurücksetzen in Rennen und Derby als gewünschte Referenz. Host und Laptop wurden hashgeprüft wiederhergestellt.

Der Ein-PC-Lauf `17f04cfd1bb8` dokumentiert beide originalen Materialzustände am selben Wagen. `probe.log.002.bmp` zeigt den Kontaktschutz und `probe.log.003.bmp` den gerasterten Distanz-Effekt. Der Diagnosecode für diesen einmaligen Vergleich wurde nach der optischen Abnahme entfernt.

In den Läufen `79eff690ea4a` und `96f065f64bf5` verwendete die Vorschau die Materialparameter des Kontaktschutzes an den separat geladenen Stunt-Fahrzeugen. Sie zeichnete überlappende Startwagen und hielt sie bei übersprungenen Updates sichtbar. Beide Läufe erfassten vier Bilder und stellten die Installation wieder her. Diese Variante leuchtete im separaten Renderpfad wesentlich stärker als die native Referenz.

Die nächste Vorschau verwendet eine höhere Rasterdeckkraft bei beibehaltener Originallackierung. Im Ein-PC-Steam-Hostlauf `2e0418312cd2` blieben Rocket, `car_1` und FO2-Variante `car_201` farbig und durchscheinend. Bild 1 zeigt die Überlappung, Bild 4 die getrennten Fahrzeuge. Übersprungene Steam-Updates, jeweils vier Räder und Freigabe aller drei Modelle wurden geprüft. Die Installation wurde hashgeprüft wiederhergestellt. Die Laptop-Sichtprüfung für diese Variante steht aus.

Im lokalen Lauf `d7c28fc740aa` zeigte derselbe Renderer den Ghost auch während der Fahrt und beim Durchfahren des eigenen Autos. Schaden und Stunt-Datensatz blieben unverändert. Der Test prüfte vier Räder sowie das Entladen und erneute Laden der Fahrzeugressourcen und stellte die Installation hashgeprüft wieder her.

Im Zwei-PC-Lauf `c78eb6862a23` blieben beide Ghosts sichtbar, ihre Lackierungen wirkten jedoch bläulich. Der lokale Vergleich `6023af7c4313` zeigte für die beiden nachgeladenen Farbtexturen Ladezustand 0 und keine Direct3D-Textur. Der bereits geladene Rocket hatte Ladezustand 5. Seit dem Fix bindet der Renderer die Farb-, Glanz- und Schadenstexturen des Ghost-Skins und lädt fehlende Texturen wie das Spiel in den Grafikspeicher. Im Hintergrundlauf `a265060e370b` bestätigte das Log den Upload für alle drei Ghosts. Die Seitenansicht des transparenten `car_39` in `FlatOutGhostView4.bmp` zeigt seine Sterne und den Schriftzug. Host und Laptop wurden nach dem Zwei-PC-Lauf hashgeprüft wiederhergestellt.

Der Wiederholungslauf `d83a37c2ef16` verlangt zusätzlich den nachgewiesenen Upload beider fremden Farbtexturen. Der Einzelspielerlauf `3dd6ad409de2` prüfte den Wechsel zwischen separat geladenen Modellen. Beim Durchfahren des eigenen Wagens blieben Schaden und Stunt-Datensatz unverändert. Beide Läufe stellten die Installation hashgeprüft wieder her.

Der Zwei-PC-Lauf `b21689191d50` bestand. Host und Laptop lieferten je vier Aufnahmen in Originalauflösung. Der Nutzer bestätigte die korrigierte Darstellung auf dem Laptop. Beide Installationen wurden hashgeprüft wiederhergestellt.

Der Steam-Hostlauf `4eb66be3ccf4` bestätigte die Ghosts nach Entfernen des Testschalters. Anschließend wurde das reguläre Steam-Paket gebaut. Alle zehn in `patch` aufgeführten BFS-Archive bestanden die Integritätsprüfung. Von den 499 Paketdateien wich vor dem Update nur `ChloeSteamNet.asi` auf dem Host ab. Nach dem Update hatten beide installierten Plugins denselben SHA-256-Hash wie das Paket. Der Hintergrundlauf `af23dc6ad366` bestand auf dem aktualisierten Host mit Einzelspieler und Steam-Lobby samt KI-Rennen sowie hashgeprüfter Wiederherstellung.

Der frühere kombinierte Lauf `d763be3f0c1d` bestätigte nacheinander Fahrzeugüberlappung, Modellwechsel, alternative Kamera und die Wiedergabe der aufgezeichneten Posen. In `d9c8c79d767a` hatte die alternative Kamera den lokalen Wagen aus dem Farbpass genommen, den der Fahrzeug-Ghost beim Zeichnen als Referenz verwendet.

Der Kamera-Prototyp und die Diagnose für den Vergleich der alten Materialeffekte wurden nach der Entscheidung für die normale Stuntansicht entfernt. Die Prüfung mit den separat geladenen Fahrzeugmodellen ersetzt den früheren Test mit Grafiken des eigenen Wagens.

Der Hintergrundlauf `872cf6e2ea13` hat in der Steam-Host-Lobby einen echten High-Jump-Abschuss ausgelöst. Zwei künstliche Mitspieler wechselten vom Fahrzeug zum nativen Fahrer und verwendeten je elf Gelenkposen. `FlatOutGhostView2.bmp` bis `FlatOutGhostView4.bmp` zeigen die beiden durchscheinenden Figuren bei Abschuss, Flug und Landung. Der Test prüfte auch den Textur-Upload der Autos und die Freigabe aller drei Fahrzeug- und beider Fahrerressourcen. Die Installation wurde hashgeprüft wiederhergestellt.

Der Einzelspielerlauf `f76a910b744f` prüfte zusätzlich die Paketgrenzen, verspätete und ungültige Posen, den Übergang zum Fahrer, Gelenkinterpolation, Kollisionsfreiheit des Fahrzeugs und den Wechsel der Fahrzeugressourcen. In Bowling blieben der überlappte Pin und der lokale Stunt-Datensatz im Lauf `1ad909ce2402` unverändert.

Alle zehn BFS-Archive des neu gebauten Steam-Pakets bestanden die Integritätsprüfung. Nach dem Update auf Host und Laptop stimmte der SHA-256-Hash beider installierten Plugins mit der Build-Ausgabe überein. Auf dem aktualisierten Host bestand der Hintergrundlauf `43f7cb4d554f` mit Einzelspieler, Steam-Lobby und KI-Rennen.

Beim damaligen Zweirechner-Test mit `-SteamGhostPeerDir` trat der Laptop bei und schleuderte seinen Fahrer in High Jump ab. Nach dem ersten bestätigten Rendern löste der Host-Probe den eigenen Abschuss aus. Beide Logs mussten die Figur mit derselben Eventkennung und elf Gelenken melden. Die temporären Client-Dateien wurden nach Spielende zurückgesichert. Eine noch offene Rücksicherung aus einem alten Lauf lässt sich weiterhin mit `-RestoreRun <Lauf-ID> -RestoreSteamGhostPeer -GameDir <Client-Spielordner>` ausführen. Das reguläre `SteamPeer`-Szenario prüft weiterhin Derby mit einem Client und vier KI-Fahrern.

Der Zweirechner-Lauf `4bd1cef8ed0c` bestätigte den echten Steam-Empfang und die gezeichnete Fahrerfigur auf beiden Rechnern mit Eventkennung `1164750241308`. Der frühe automatische Ausstieg des Hosts unterbrach seinen Flug kurz nach dem Nachweis. Der Runner wartet nach dem Nachweis jetzt fünf Sekunden. Beide Installationen wurden hashgeprüft wiederhergestellt.

Der Hintergrundlauf `4ff8fd7de800` prüfte einen Fahrer über 100 m von der übertragenen Autoposition entfernt, auch mit ungültiger Fahrzeugrotation. Nach dem simulierten Ende der nativen Gelenk-Updates zeichnete das Spiel die letzte gültige Pose weiter. Beim Öffnen der echten Stunt-Ergebnisliste wurde die gehaltene Pose freigegeben. Die Wiederherstellung der Installation bestand.

Der manuelle Test am 21.11.2026 bestätigte in beiden Logs Ghost-Auto und Fahrerfigur für die High-Jump-Events `1198194021865` und `1198976803736`. Beide Rechner zeichneten je vier Bilder auf und gaben die Modelle bei der Rückkehr in die Lobby frei. Die einzelnen Aufnahmen erlauben keine Aussage über den Bewegungsverlauf. Auf beiden Rechnern folgte erst nach `SteamAPI_Shutdown` je eine Sendemeldung mit Fehler 10035. Die Logs und Originalaufnahmen liegen unter `.work/manual-ghost-2026-11-21-193302/`.

Beim Ruckeln half erst die Kombination aus höher aufgelöster Uhr, getrennten Sperren für Transport und Darstellung sowie Positionsvorhersage nach dem Vorbild von Chloes Renn-KI. Der Sendeabstand für Autos beträgt mindestens 25 ms. Beim Empfänger gilt ein Zielpuffer von 100 ms und höchstens 80 ms Vorhersage. Neue Posen korrigieren eine laufende Vorhersage über 100 ms. Die Prüfung bei 216 km/h deckte schwankende Paketabstände, Paketverlust und die Korrektur ohne sofortigen Rücksprung ab. Der anschließende manuelle Zwei-PC-Test bewertete die Bewegung als deutlich besser und gelegentlich noch ruckelig.

Die entfernte Zeile `stunt ghost: car stream` meldete je Mitspieler in rund zwei Sekunden die angenommenen Pakete, berechneten Fahrzeugposen, Vorhersagen, am Vorhersagelimit gehaltenen Posen und die größte Empfangslücke in Millisekunden. Für eine erneute Diagnose liefert der Vergleich die Zustandsfelder und den Wiedergabeablauf. Die Release-Version protokolliert weiterhin die Ghost-Eventkennung und allgemeine Steam-Fehler. [Renderpfad und Bewegung](Reverse-Engineering.md#stunt-ghosts-im-nativen-renderer)

Am 21.11.2026 wurde das bereinigte Plugin mit SHA-256 `AFE5B043396C4897DBF64D3BF80C642ABB06358E580E218157BBCC640EE11601` auf Host und Laptop installiert und nach den Tests erneut hashgeprüft. Auf dem inaktiven Desktop des Hosts bestanden vier Läufe. Die Logs liegen unter `.work/t/flatout/`.

| Lauf | Geprüft |
|---|---|
| `e8527c4a966d` | Hauptmenü, Optionen, Karriere und Carnage, Einzelrennen mit zwei KI-Fahrern und Steam-Lobby mit drei KI-Fahrern samt Lobbyaustritt |
| `69fa4e817ae3` | Wrecking, Deathmatch und Survivor Derby mit drei KI-Fahrern und 60 Sekunden Zeitlimit. Steam High Jump erreichte den ersten von drei Versuchen und protokollierte den Ghost-Eventstart. |
| `cd85d3ed7a10` | High Jump mit lokalem Ghost. Eigenes Fahrzeugmodell mit Skin und vier Rädern gerendert, Überlappung ohne Schaden, Posenaufzeichnung, Paketprüfung und Laden der Fahrzeugressourcen bestanden. |
| `9d2cef8c6912` | Bowling. Pin und Stunt-Datensatz blieben über 18 Überlappungsframes unverändert. |

Alle vier Läufe stellten die Installation hashgeprüft wieder her. Die Ghost-Übertragung zwischen zwei Steam-Teilnehmern wurde in dieser Runde nicht erneut geprüft.
