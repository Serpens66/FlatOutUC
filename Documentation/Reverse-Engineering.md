# Reverse Engineering: Fouc.exe und Chloes Plugins

Stand 2026-11-19. Gesammelte Ergebnisse aus Ghidra/rizin, damit andere Chats darauf aufbauen können, ohne neu zu analysieren.
Adressen gelten für `Fouc.exe` mit 4242504 Bytes (GFWL- und Steam-Version sind byte-identisch).

## Dateien im Workspace (`reverse_engineering/`)

| Pfad | Inhalt |
|---|---|
| `decompiled/Fouc.exe_all.c` | Ghidra-Decompile aller 7973 Funktionen von `Fouc.exe` (12 MB, Kopf je Funktion `//==== FUN_xxxxxxxx @ adresse`) |
| `decompiled/Fouc.exe_stunt_classes.c` | vtables und Methoden aller Klassen mit "Stunt" im Namen (`ScoreManagerStunt`, `PlayerScoreStunt`, `NETWORK::ReplicatedStunt*`, Ergebnis-Menüs, Stunt-Kamera) |
| `decompiled/FlatOutUCCustomMP_gcp.asi_all.c` | Decompile von Chloes MP-Plugin 1.83 (Quellcode nicht veröffentlicht) |
| `decompiled/FlatOutUCCustomMP_gcp.asi_hooks.c`, `..._joingame.c` | einzelne Hook-Ziele bzw. `ChloeNet_JoinGame`/`OpenIPEntry` |
| `ghidra_projects/fouc/` | Ghidra-Projekt `fouc` (Programm `Fouc.exe`, fertig analysiert, ~120 MB) |
| `ghidra_projects/chloe_custommp/` | Ghidra-Projekt `mp` (Programm `FlatOutUCCustomMP_gcp.asi`) |
| `ghidra_scripts/` | `DumpAll.java` (alle Funktionen decompilieren), `DecompAt.java` (Funktionen an Adressen anlegen und decompilieren), `DumpVtables.java` (vtables einer Klasse nach Namensfilter + Methoden) |

Skripte auf einem fertigen Projekt ausführen (ohne neue Analyse, dauert Sekunden statt ~30 min):

```
$env:JAVA_HOME = "D:\CDesktopLink\Portable\Native\temurin-jdk-21.0.12.1+1\jdk-21.0.12.1+1"
"" | & "D:\CDesktopLink\Portable\Native\ghidra-12.1.3\ghidra_12.1.3_PUBLIC\support\analyzeHeadless.bat" `
   "reverse_engineering\ghidra_projects\fouc" fouc -process Fouc.exe -noanalysis `
   -scriptPath "reverse_engineering\ghidra_scripts" -postScript DecompAt.java out.c 004693f0 0049b1e0
# vtables: -postScript DumpVtables.java out.c <Namensfilter> <max. Einträge>
# Chloes MP-Plugin: Projekt "reverse_engineering\ghidra_projects\chloe_custommp" mp -process FlatOutUCCustomMP_gcp.asi
```

Das `"" |` ist nötig, weil die Batch-Datei am Ende auf eine Taste wartet. Ghidra-Projekte nicht gleichzeitig aus zwei Prozessen öffnen.
Strukturen und viele Adressen stehen außerdem in Chloes öffentlicher Bibliothek `source/nya-common-fouc/types/*.h` (GameFlow, Player, ScoreManager, LUA …).

## Wichtige Strukturen und Adressen (Fouc.exe)

- `GameFlow*` bei `0x9298FAC`: `+0x4A4` nGameState (2 = Rennen), `+0x4B0` PreRace.nMode (5 = GM_ONLINE_MULTIPLAYER), `+0x4CC` PreRace.nLevel, `+0x514` nGameRules (2 = GR_STUNT), `+0x518` nStuntType, `+0x520` nStuntStartPosition (Startpunkt des Stunts aus `Levels[].StartPosition`), `+0xAE8` PlayerInfo[32] (je 0x50 Bytes: Name wchar[16] bei +0, Typ bei +0x2C: 1 lokal, 2 remote, 3 Zuschauer, 4 AI, 5 simuliert), `+0x2820` PlayerHost.
- `PlayerHost`: `+0x14` Vektor `Player*` (begin/end), `+0x2098C` nLevelId. `Player`: `+0x294` pCar, `+0x2C4` Spieler-ID, `+0x2C8` Typ, `+0x2CC` nStartPosition.
- `ScoreManager*` bei `0x846514`: `+0x8/+0xC` Vektor der PlayerScore-Einträge, `+0x34` aktuelle Runde (Stunts).
- EndScene-Hookpunkt `0x677448` (vtable-Slot, wie `NyaFO2Hooks::PlaceD3DHooks`), Lua-Registrierung `0x46256C` (CALL lua_pushcfunction).
- `GameFlow::InitializeAI` `0x4693F0` (siehe unten), Rennaufbau-Sprungtabelle `0x468D30` (siehe [Multiplayer-AI.md](Multiplayer-AI.md)).
- Spieler anlegen: Ein Spieler bekommt beim Erstellen `nStartPosition = GameFlow+0x520` (Fouc.exe_all.c Zeilen um 64607/64627).

## Stunts (Vanilla)

**InitializeAI (0x4693F0):** Bei `nGameRules` 11 (GR_TEST) und 8 (GR_BEAT_THE_BOMB) passiert nichts. Bei GR_STUNT (2) und Regel 10 bekommen die neuen PlayerInfo-Einträge Typ **5 (simuliert)** statt 4 (AI), mit AI-Namen. Bei Stunts setzt die Funktion zusätzlich PlayerInfo+0x20 = Index % 5 + 0x22.
Spieler-Objekte entstehen nur für Typ 1 (lokal), 2 (remote, braucht Vanilla-Netzwerk) und 4 (AI) (`FUN_004694f0`). **Simulierte Spieler haben kein Auto**, sie stehen nur in PlayerInfo und in der Wertung.

**ScoreManagerStunt** (vtable `0x6E3E4C`, 23 Methoden, Klasse `PlayerScoreStunt` vtable `0x6E3E0C`):
- [5] `FUN_00499a60` Init: legt PlayerScore-Einträge für alle Spieler-Objekte plus `FUN_00469730(GameFlow, 5)` simulierte Spieler an (zählt PlayerInfo-Einträge mit Typ 5). Versuche pro Spieler (`+0x3C` bzw. param_1[0xF]): 3, bei nStuntType 6: 6, bei 8/0xB/0xC: 5, bei 9: 2, bei 0xE: 4.
- [8] `FUN_00499cc0` Update pro Frame: Ablauf eines Versuchs, Wechsel zum nächsten Versuch. Ruft nach dem Versuch des lokalen Spielers vtable+0x58 auf.
- [22] `FUN_0049b1e0` (= vtable+0x58) **"alle simulierten Spieler würfeln"**: für jeden simulierten Eintrag `FUN_00497e80(score, runde)`. Diese Funktion holt mit `FUN_00412a50(nStuntType, runde, …)` (Mersenne-Twister + Stunt-Daten) eine Punktzahl und schreibt sie in den Rundendatensatz des Spielers.
- Rundendatensätze: Jeder `PlayerScoreStunt` hat pro Versuch einen Datensatz von **0x114 Bytes ab +0x38** (Datensatz r: `+0x38 + (r-1)*0x114`). Welches Feld die Punkte enthält, hängt von nStuntType ab (`FUN_00497e80`: meist Datensatz+4 als float, bei 3/9 Datensatz+0, bei 8 und 0xB mehrere Felder).
- [21] `FUN_0049b190` nächster Spieler am Zug (überspringt PlayerInfo-Typ 2), [19] `FUN_0049b140` "fertig?".
- Ergebnis-Anzeige: `IngameMenuTabSingleStuntResults` (vtable `0x6E786C`), `IngameMenuTabMultiplayerStuntResults` (`0x6E7894`), `IngameMenuTabArcadeStuntResults` (`0x6E78BC`).
- Online (Vanilla, nur über die Live-Netzwerkschicht): `NETWORK::ReplicatedStuntScore` / `ReplicatedStuntRound` / `ReplicatedStuntPlayerScore` (vtables `0x6E3DC4`, `0x6E3D80`, `0x6E3D6C`), Sonderwege in ScoreManagerStunt für PreRace.nMode 5 und 6. In Chloes Multiplayer nicht nutzbar (eigenes ENet-Netzwerk).

## Stunts in Chloes Multiplayer (Befund)

- `FlatOutUCCustomMP_gcp.asi` `0x628F94AA` (`mov [eax+0x2CC], edx` in `FUN_628f8890`, läuft jeden Frame im Rennen, wenn verbunden und mehr als 1 Spieler): `Player0.nStartPosition = (eigener Session-Slot % Anzahl Startpunkte der Strecke) + 1`. Damit startet jeder an einem eigenen Gridplatz. Bei Stunts falsch: Host Startpunkt 1 (Curling), Client 2 (High Jump).
  **Behoben in `ChloeSteamNet.asi`:** Die 6 Bytes werden zur Laufzeit durch `call` auf einen Stub ersetzt, der auf den Stunt-Leveln 47–58 den Startpunkt des Stunts schreibt, sonst Chloes Wert (Originalbytes werden vorher geprüft).
- Chloes Collection-Plugin `CareerTimeTrial::OnTick` (`source/FlatOutUCChloeCollection/careertimetrial.h`, läuft in `HookLoop` im EndScene) setzt bei `bIsTimeTrial` jeden Frame `GetPlayer(0)->nStartPosition = 1`. `bIsTimeTrial` wird nur von `ChloeCollection_SetIsTimeTrial(1)` (Zeitfahren im Einzelrennen) gesetzt und **nie zurückgesetzt** (Chloe-Fehler: nach einem Zeitfahren gilt es für alle weiteren Rennen der Sitzung). `ChloeSteamNet.asi` schreibt deshalb nach dem Original-EndScene noch einmal den Stunt-Startpunkt.
- Im MP-Stunt gibt es pro PC **nur einen Spieler**, keine Mitspieler-Puppen (die sind bei Chloe AI-Slots, die es bei Stunts nicht gibt) und laut Test auch keine Mitspieler in der Wertung. Vermutung: keine PlayerInfo-Einträge mit Typ 5. Diagnose im Log seit 19.11.2026: "PlayerInfo[i] type … name …" und "ScoreManager entries …" beim Rennstart eines Stunts.
- Chloes `InitializeAI`-Hook (`0x628DC060`) setzt nur PreRace.nClass und springt ins Original.

## Konzept: zeitgleiche Stunts mit gemeinsamer Wertung (Schritte 1–3 umgesetzt am 19.11.2026, ungetestet)

Ziel des Nutzers: alle machen ihren Versuch gleichzeitig, danach sehen alle die Punkte aller, dann beginnt der nächste Versuch. Keine AI, Geister der Mitspieler wären nett, aber unwichtig.
Ansatz auf dem Vanilla-Einzelspieler-Stunt (simulierte Gegner) statt auf dem Vanilla-Online-Stunt (nacheinander, Live-Netzwerk):

1. **Einträge für Mitspieler:** Vor `ScoreManagerStunt` [5] (Init, vtable-Slot `0x6E3E60`) für jeden Mitspieler einen PlayerInfo-Eintrag mit Typ 5 und dessen Namen anlegen (Reihenfolge z. B. nach SteamID der Steam-Lobby-Mitglieder, auf allen PCs gleich). AI COUNT 0.
2. **Echte statt gewürfelte Punkte:** vtable-Eintrag [22] (`0x6E3EA4`, "alle simulierten würfeln") durch eigene Funktion ersetzen: für jeden Mitspieler den empfangenen 0x114-Byte-Rundendatensatz einsetzen statt `FUN_00497e80`.
3. **Eigenen Versuch senden:** Wenn [22] aufgerufen wird, ist der lokale Versuch fertig. Den Rundendatensatz des lokalen Spielers über Steam (`ChloeSteamNet`, neuer Nachrichtentyp) an alle schicken.
4. **Gemeinsam weiter:** Den Übergang zum nächsten Versuch in [8] (`FUN_0048bc10`-Aufruf nach Flag `+0x2459`) so lange blockieren, bis die Datensätze aller Mitspieler für diese Runde da sind. Fehlende Datensätze nachtragen, sobald sie eintreffen.
5. Offen: genaue Bedeutung der Felder je nStuntType (Gesamtpunkte, Sortierung), Verhalten der Ergebnis-Anzeige bei nachträglich eingetragenen Werten, Abbruch/Verlassen eines Spielers (Timeout), Geister (Positionen der Mitspieler-Autos) zunächst nicht.

### Umsetzung in `ChloeSteamNet.asi` (19.11.2026)

- vtable-Slots von `ScoreManagerStunt` werden beim Laden ersetzt (vorher geprüft: `0x6E3E60` = `0x499A60`, `0x6E3EA4` = `0x49B1E0`):
  - Init-Hook: Auf Stunt-Leveln in Chloes MP (Sockets offen, Steam-Lobby vorhanden) bekommen PlayerInfo[1..n] Typ 5, den Steam-Namen und +0x20 wie bei InitializeAI. n = übrige Lobby-Mitglieder, nach SteamID sortiert (max. 7). Weitere PlayerInfo-Einträge mit Typ 2/4/5 werden gelöscht (keine AI im MP-Stunt). Danach Original.
  - Hook für [22]: Der eigene Rundendatensatz (0x114 Bytes) geht an alle (Steam-Nachrichtentyp `T` = 0x54, Feld "Quellport" = Level, "Zielport" = Runde). Die Einträge der Mitspieler bekommen deren empfangenen Datensatz. Fehlt er noch, wird er im EndScene-Tick nachgetragen, sobald er da ist. Weitere simulierte Einträge (AI) würfeln wie im Original. In der letzten Runde wird `+0xCA8` (fertig) gesetzt wie im Original.
  - Beim Verlassen des Rennens werden alle Stunt-Daten verworfen.
- Log: "stunt: … other players added", "score entries …, simulated …, attempts …", "own record of round … sent", "record of … received", "filled in late".
- Noch nicht umgesetzt: Warten auf alle vor dem nächsten Versuch (Schritt 4). `+0xCA8` bedeutet "alle Versuche erledigt", nicht "dieser Versuch". Der Start des nächsten Versuchs muss noch gefunden werden (ScoreManagerStunt [8], Flag `+0x2459`, `+0x916`/`FUN_0049fc30`).
- Offene Risiken: Stunt-Typen 8 und 0xB schreiben bei simulierten Spielern zusätzlich globale Felder im ScoreManager (`+0x23BC + Runde*0xC`), die nicht übertragen werden. Ob die Gesamtwertung/Sortierung nachgetragene Werte übernimmt, ist ungetestet.

### Test 19.11.2026 und Korrekturen

- Log: Mitspieler-Einträge wurden angelegt (PlayerInfo[1] Typ 5 mit Steam-Namen, 2 Wertungseinträge, 3 Versuche), alle Rundendatensätze in beide Richtungen übertragen und eingetragen. Trotzdem sah jeder nur die eigenen Punkte, und der Startpunkt war weiter falsch.
- Startpunkt: Der Patch von Chloes MP-Plugin wurde übersprungen ("unexpected code"), weil die Adresse mit der ImageBase aus dem geladenen Header umgerechnet wurde. Das Plugin wird verschoben geladen und Windows trägt die tatsächliche Basis ein. Jetzt mit der Basis der Datei (`0x62840000`).
- Anzeige: Zwischen den Versuchen zeigt das Spiel `IngameMenuTabSingleStuntResults` (zeichnet nur die Versuche des lokalen Spielers, [3] `FUN_004b71c0`). `IngameMenuTabMultiplayerStuntResults` ([3] `FUN_004b7cc0`) listet alle Wertungseinträge mit PlayerInfo-Namen. Alle übrigen Methoden der beiden Klassen sind gleich. Im MP-Stunt ruft der Slot `0x6E7878` (Single [3]) jetzt die Multiplayer-Variante auf.
- Erneuter Test steht aus.

## Copyright-Bildschirm (Chloe)

`FlatOutUCChloeCollection_gcp.asi` (`skippablecopyright.h`): `0x55B97C` = `0xEB` (immer `copyright.tga`), `JMP` an `0x55BA6F` auf eine Schleife, die bis Enter/A/Start den Ladebildschirm zeichnet (Original an dieser Stelle: `call 0x41FF10`, Bytes `E8 9C 44 EC FF`). `ChloeSteamNet.asi` stellt das Original wieder her, sobald Chloes `JMP` dort steht (Thread, da Chloes Plugin nach `ChloeSteamNet.asi` geladen wird).

### Test 2 (19.11.2026) und Korrekturen

- Stunt im MP funktioniert: richtiger Startpunkt, beide Spieler mit Punkten in der Ergebnistafel.
- Namen: PlayerInfo[0] (lokal) trägt den Profilnamen, die Mitspieler-Einträge trugen den Steam-Namen. Jetzt schickt jeder PC beim Stunt-Start und mit jedem Rundendatensatz seinen Profilnamen (PlayerInfo[0], Nachrichtentyp `N` = 0x4E, wchar[16]). Der EndScene-Tick ersetzt damit den Steam-Namen im Eintrag des Mitspielers. Bekannte Namen bleiben für die Sitzung gespeichert.
- Absturz beim Beenden (Alt+F4, "Runtime error" des Spiels, abgestürzter Thread springt in Heap-Speicher, Ausnahme 0xC000001D): ZOOMs `xlive.dll` ruft beim Beenden `SteamAPI_Shutdown`, während Chloes ENet-Thread über `ChloeSteamNet.asi` weiter `SteamNetworkingMessages` benutzt. `ChloeSteamNet.asi` hängt sich jetzt in den Import `steam_api.dll!SteamAPI_Shutdown` von `xlive.dll` und macht danach keine Steam-Aufrufe mehr (auch nicht nach `DLL_PROCESS_DETACH`). Ungetestet.
