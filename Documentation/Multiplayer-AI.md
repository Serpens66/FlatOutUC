# AI-Gegner im Multiplayer

Stand 2026-11-18. Ziel: AI-Gegner im Steam-Multiplayer (Live-Modus des Spiels über ZOOMs `xlive.dll`), nicht in Chloes eigenem Multiplayer.

## Ausgangslage

- Vanilla-FOUC hat keine AI im Online-Modus. In `Fouc.exe` springt der Rennaufbau je nach `PreRace.nMode` (Sprungtabelle 0x468D30, Index-Bytes 0x468D44):
  Karriere, Einzelrennen, Arcade (`GM_ARCADE_CAREER`), Instant Action, Developer und Tournament rufen `GameFlow::InitializeAI` (0x4693F0) auf.
  `GM_ONLINE_MULTIPLAYER` (5), Party, Splitscreen, Crash-Test-Dummy und Test springen direkt zu 0x4682C8, also ohne AI.
  Die Spieler eines Online-Rennens trägt nur das Lua-Skript ein (`multiplayerlobby.bed`: `GameFlow.PlayerInfo[n]` mit `PLAYERTYPE_LOCAL`/`PLAYERTYPE_REMOTE`).
- `InitializeAI` hängt die AI-Fahrer hinter die vorhandenen `PlayerInfo`-Einträge (Typ `PLAYERTYPE_AI` = 4, in einigen Fällen `PLAYERTYPE_SIMULATED` = 5). Bei `nGameRules` (GameFlow+0x514) 11 (`GR_TEST`) und 8 (`GR_BEAT_THE_BOMB`) bricht sie ab.
- Chloes eigener Multiplayer (`FlatOutUCCustomMP_gcp.asi`, IP-basiert, Port 23756) hat AI ("AI COUNT" 0–12, "AI CLASS" im Lobby-Optionsmenü). Quellcode nicht veröffentlicht.
- Öffentlicher Code zu AI im FlatOut-Multiplayer existiert nicht (Suche 2026-11-18). Nützliche öffentliche Bausteine: `nya-common-fouc` (Strukturen) und das Ghost-Plugin
  (`FlatOut2TimeTrialGhosts/timetrialshared.h`: `tCarState` mit `Collect()`/`Apply()` für Matrix, Geschwindigkeit, Drehgeschwindigkeit, Quaternion, Nitro, Schaden, Pedale, Gang).

## Analyse von `FlatOutUCCustomMP_gcp.asi` (1.83)

Gemacht mit Ghidra headless (Decompile aller Funktionen) und rizin. Funktionsnamen sind Ghidra-Adressen, die Deutungen in Klammern sind aus dem Code abgeleitet.

**Grundprinzip:** Das Plugin ersetzt die Vanilla-Netzwerktechnik komplett. Das Rennen läuft für die Engine wie ein Einzelrennen:
Der lokale Spieler ist Spieler 1, **alle anderen Autos (Mitspieler und echte AI) sind AI-Autos** aus `InitializeAI`.
Beim Host steuert die AI die echten AI-Autos, bei den Clients werden sie wie Mitspieler aus Netzwerkdaten bewegt.

Hooks in `Fouc.exe` (beim Laden des Plugins gesetzt):

| Stelle in `Fouc.exe` | Ziel im Plugin | Wirkung |
|---|---|---|
| CALL 0x468291, 0x4682A2, 0x4682BA, 0x46A2B9 (`InitializeAI`) | 0x628DC060 | setzt `PreRace.nClass` (+0x4BC) = AI-Klasse (max. 3), Anzahl = verbundene Spieler + AI-Anzahl − 1 (max. 31), dann Original-`InitializeAI` |
| NOP 6 Byte bei 0x469409 | – | entfernt in `InitializeAI` den Abbruch bei `nGameRules == 8` |
| JMP 0x409388 (AI-Autowahl) | 0x628EE5A0 | AI-Slot eines Mitspielers: Auto, Skin, Fahrer, Pfeilfarbe aus Lobby-Daten. Echte AI: Auto nach AI-Klasse (Klasse 4 = Auto des Hosts) |
| CALL 0x409C7D (Verarbeitung AI-Auto, wie Ghost-Plugin) | 0x628F9A40 | Host (vermutlich `IsServer`): Zustand der echten AI-Autos sammeln (0x628E3EF0, entspricht `Collect`). Client: empfangenen Zustand mit Interpolation anwenden (0x628F6320, entspricht `Apply`). Für Mitspieler-Slots immer anwenden |
| CALL 0x47A010 (Verarbeitung Spieler-Auto) | 0x628F9C70 | Zustand des lokalen Autos sammeln, "has joined/left the game"-Meldungen |
| vtable 0x6DBCC0 (AI-Controller-Update) | 0x628DC380 | Client: für AI-Autos des Hosts die Eingaben nullen bzw. AI nicht ausführen (Autos sind reine Puppen) |
| vtable 0x6EB7DC | 0x628F8350 | Original aufrufen, danach eigene Verarbeitung |
| CALL 0x4E84D3/E4/F5 | 0x628DC1F0 | zählt Crash-Boni (0x38D–0x392) für Statistik |
| JMP 0x428841, 0x428024, 0x4E6D55 | 0x628D9F20, 0x628D9FE0, 0x628D9F50 | weitere Rennlogik (nicht im Detail geprüft) |

Netzwerk: Importe aus `WS2_32.dll` (`WSASendTo`, `WSARecvFrom`, `select`, `__WSAFDIsSet`, `gethostbyname`, `gethostbyaddr`, `connect`, `listen`, `accept` …) passen genau zur Win32-Schicht von **ENet** (UDP). Das ist eine Vermutung, nicht bestätigt.

## Folgerungen für den Steam-Multiplayer

1. **Eigenes Plugin, Vanilla-Live-MP bleibt:** Die menschlichen Spieler laufen weiter über die Vanilla-Netzwerktechnik (ZOOM → Steam). Ein Plugin ergänzt nur die AI:
   - Rennstart: Host und Clients rufen `InitializeAI` auch im Modus `GM_ONLINE_MULTIPLAYER` auf (Hook bei 0x4682C8 bzw. im Lua-Aufbau), mit gleicher Anzahl, gleichen Autos und Skins (Host schickt die Liste).
   - Host: AI fährt normal, Zustand pro AI-Auto (`tCarState`) 20–30× pro Sekunde senden.
   - Clients: AI-Controller abschalten (wie vtable 0x6DBCC0 bzw. Ghost-Patch 0x409D90 → 0x40A6BE), Zustand anwenden (Hook 0x409C7D wie Ghost-Plugin).
   - Transport: `SteamNetworkingMessages` aus der `steam_api.dll` der Steam-Version (eigener Kanal), Empfänger = Mitglieder der Steam-Lobby.
   - Offen: Ergebnisliste/Platzierungen, Wracks und Schaden, Zusammenstöße mit den Puppen bei Clients, Derby-Modi, wie die Vanilla-Netzwerktechnik auf zusätzliche `PlayerInfo`-Einträge reagiert (Spieler-IDs!).
2. **Alternative: Chloes Multiplayer über Steam transportieren.** Die UDP-Aufrufe von `FlatOutUCCustomMP_gcp.asi` (IAT: `WSASendTo`, `WSARecvFrom`, `select`, `bind`) auf Steam-P2P umleiten und Mitspieler über eine Steam-Lobby finden. Dann wäre Chloes erprobte AI-Synchronisierung komplett nutzbar, ohne Portfreigabe. Lobby und Menüs wären aber Chloes, nicht die Vanilla-Live-Menüs.

## Entscheidung und Umsetzung (18.11.2026)

Gewählt: **Weg 2**. Wichtig waren AI, Chloes Lobby-Optionen und Spielmodi. Von Steam wird nur die Verbindung ohne Portfreigabe gebraucht, Einladungen nicht.
Umgesetzt mit dem eigenen Plugin `ChloeSteamNet.asi` und einer Spielliste im Menü. Details, Bedienung und Teststand: [Steam-Version.md](Steam-Version.md) (Abschnitt "Multiplayer").

Vergleich, der zur Entscheidung geführt hat (aus den Skripten abgeleitet, nicht alles im Spiel geprüft):
- Vanilla-Live-MP über ZOOM: Einladen über Steam-Freunde, Suche/OptiMatch, Turniere. Dafür wenig Optionen, keine AI, und jeder fährt mit seinem eigenen Handling-Modus.
- Chloes MP: AI-Gegner, viele Lobby-Optionen (Klassen/Tiers, Schaden, Nitro, Handling für alle, Tempolimit, Ghost-Modus), Spielmodi (Pong-Rennen, Derby-Varianten, Stunts, Hunter/Prey, Arcade-Events, umgekehrte Strecken, Wetter), Chat, Kick/Bann, Profile mit Statistik. Dafür ohne Umbau nur Beitritt per IP mit Portfreigabe.

Ergänzend für beide Pakete (`custom_src/multiplayer_overrides/`, erzeugt mit `custom_src/make_mp_scripts.py`): Chloes MP-Skripte zeigen nur FO2-Strecken und -Autoklassen (siehe [../AGENTS.md](../AGENTS.md)).
