# Erkenntnisse: Chloe's Collection 1.83 (Modpack)

Hier steht, was für die Mod "FlatOut: Ultimate Carnage – Chloe's Collection" gilt.
Allgemeines zum Spiel und zu den Dateiformaten steht in [Vanilla-Spiel.md](Vanilla-Spiel.md). Unsere eigenen Anpassungen sind in [../AGENTS.md](../AGENTS.md) beschrieben.

Quelle: https://gaycoderprincess.github.io/project/chloe-collection (Readme, Changelog, Tools). Quellcode der Plugins liegt in `source/`.

## Voraussetzungen und Installation

- Benötigt den Steam-Branch `gfwl` (`Fouc.exe` mit 4242504 Bytes). Mit der neuesten Steam-Version ist die Mod laut Readme nicht kompatibel. Unsere angepasste Variante dafür: [Steam-Version.md](Steam-Version.md).
- Bringt `xlive.dll` (GFWL-Ersatz), `d3d9.dll` und `bass.dll` mit sowie die Plugins `*_gcp.asi`, `FreemanAPI_gcp.dll`, die Archive `foucpack_*.bfs` mit der Liste `patch`, und die Ordner `Config/`, `Data/` (lose Musik, Sounds, Pacenote-Texturen, Video), `Ghosts/` und `SteamAssets/`.
- Speichert Spielstände im Spielordner (`Savegame/`, `Profiles/`) und legt beim Spielen eigene Ghosts an.
- Ältere Versionen (z. B. Stand März 2025) hatten zusätzlich `FlatOutUCChloeCars_gcp.asi` und `FlatOutUCChloeMaps_gcp.asi`, die es in 1.83 nicht mehr gibt.

## Archive in 1.83

Alle Archive sind 4096-Byte-ausgerichtet. Chloes Archive nutzen die Flags `00`/`01` (CRC 0), die von Ryan und Twobei `05`.

| Archiv | Inhalt |
|---|---|
| `cars` | FO1-Autos (101–116), Retro Demo (151–154), Tough Trucks (161–166), FO2-Varianten (201–245), Custom/NFS (3xx), `car_37` (nur `camera.ini`), `sharedfo2`, Menümodelle `data/menu/cars/menucar_N.bgm` |
| `cars_extras` | Autos 341–381 (NFS, Re-Volt usw.) |
| `cars_extras_props` | Prop-Autos 301–361, `car_trolley` |
| `cars_rallytrophy` | Rally-Trophy-Autos 401–422 |
| `carskins` | zusätzliche Lackierungen (`skin6+`) für Vanilla-, FO2-, FO1- und weitere Autos, `zztest` |
| `carsounds` | Motorsounds `engine_fo1`, `engine_rt`, `engine_rt_old`, `engine_tt`, `extras`, `gearshift_fo1` |
| `maps` | FO2-Geometrie der Vanilla-Strecken (`geometryfo2`, `texturesfo2`), FO1-Strecken, Retro Demo, Tough Trucks, FO2-Garage-Testtrack, Arbeitsdateien (`test.sh` usw.) |
| `maps_extras` | Burnout, Drift, GT (Laguna Seca), Mario, NFS (Atlantica), ProStreet (Autopolis), Re-Volt, San Juan, URL (Bayview) |
| `maps_rallytrophy` | Rally-Trophy-Etappen (Karelia, Kenya, Finland, Sweden, Switzerland) |
| `maps_teddyator` | Kanal 4 (`canal1/d`, in `levels.bed` auskommentiert) und Änderungen an FO1-Strecken |
| `mapcols`, `mapsplines` | Kollisions- und AI-Daten nur für Nicht-FO2-Strecken |
| `mapimages` | Streckenbilder und Minimaps fürs Menü |
| `misc` | `game.db`, alle Lua-Skripte, FO1-Fahrermodelle, HUD-Elemente |
| `multiplayer`, `music` | Multiplayer-Menüs, Playlists |
| `ryan_carpanels` | `panels.ini` für FO1- und FO2-Autos |
| `twobei_portraits` | Anime-Portrait-Atlas, Schadensanzeige, Ladebildschirm-Bilder (`jack.tga` …) und `loadingscreens/ai*.bed` |

## IDs

- **Strecken** (`levels.bed`): 1–58 FO2/FOUC (46 = FO2-Garage-Testtrack, 47–58 Stunts), 59–94 FO1, 95–98 Retro Demo, 99–114 Tough Trucks, 115–146 Rally Trophy, 147–156 Rally Trophy Arcade, 157–180 Zusatzstrecken (FO1-Varianten, Re-Volt, GT, NFS, Mario, Drift, URL, San Juan, Burnout, ProStreet).
- **Autos**: Die Skripte arbeiten mit Datenordner-Nummern (`CarIndex.X = ChloeCollection_GetCarDBID(N)`). 1–47 Vanilla, 1xx FO1/Retro/TT, 2xx FO2-Varianten, 3xx Custom/Props/NFS, 4xx Rally Trophy. `GetCarDBID` sucht den ersten DB-Eintrag mit passendem `DataPath`.
- `Config/CarUnlocks.toml` und `CarMatchups.toml` verknüpfen Zusatzautos mit Vanilla-Autos (Freischaltung bzw. Umschalten im Autohändler).

## Mechanismen der Mod

- **Cheat-Codes**: Strecken (`CheatCode` in `levels.bed`) und Autos (`CheatCode` in `game.db`) mit Cheat-Code sind im Single Race verborgen, bis der Code unter Extras eingegeben wird. Liste: `cheats.txt`.
- **Autofilter**: `IsValidCar()` und `IsValidCarIncludeMatchups()` in `carselectiongenericmenu.bed` entscheiden über alle Autolisten. Prop-Autos sind zusätzlich über `ChloeCollection_ArePropCarsUnlocked()` (Cheat `Temp350`) gesperrt.
- **Single Race**: Die Kategorie-Buttons in `menu_singleplayer_racetype` entsprechen 1:1 den `TRACKTYPE`-IDs (Button-Index = Typ). Wer Buttons entfernt, braucht eine Zuordnung.
- **Karriere**: Zusätzlich zu den Cups gibt es Bonus-Events mit `ChloeTimeTrial = true`. Die Medaillenzeiten stammen aus Ghost-Dateien `Ghosts/Career/<STRECKE>_<Auto>_<Medaille>.foucrep`. Es gibt sie nur für die vorgesehenen Kombinationen aus Strecke und Auto.
- **Carnage**: 6×6-Raster `R1C1`–`R6C6` mit `GoalScores`, `PlatinumGoal` (4. Medaille), `CarUnlock`, `Car`, optional `AICar`/`AICar1`/`AICar2`. Die Mod hat das Vanilla-Raster stark umgebaut. Beim Laden ordnet `customsave.h` alte Ergebnisse per Kombination aus Auto und Strecke den Events zu.
  - Medaillen der 16 umgebauten Events (Custom-Version, 19.11.2026): Vanilla-Werte gibt es dafür praktisch nicht (Chloe hat das Raster neu aufgebaut, Drift und Stunt Show fehlen in Vanilla, nur R5C2 entspricht Vanilla und hat schon dessen Ziele). `PlatinumGoal` ist Chloes eigener Bestwert auf der alten Strecke bzw. mit dem alten Auto und wurde bei allen 16 auskommentiert (`PlatinumGoal = nil` wird unterstützt, `arcadecareermenu.bed`: keine Platin-Anzeige). Gold/Silber/Bronze **geschätzt** und im Spiel noch zu prüfen: R1C2 Beat the Bomb Desert1A/Chili 40/30/20k (Chloe 45/35/20k, Vanilla R1C5 mit Canyon 40/35/25k), R2C4 Stunt Show Desert1B 100/70/45k (150/100/70k auf Pit2B), Drift R2C6 City2C 100/75/50k (150/120/80k), R3C6 Arena3 110/80/55k (130/100/70k), R4C4 City1C 100/75/50k (150/120/80k), R5C4 Desert1C 120/90/60k (180/150/90k). Die Carnage-Rennen und übrigen Beat-the-Bomb-Events behalten Chloes Ziele, sie liegen im Bereich ihrer FO2-Events gleichen Typs (Rennen 60–100k Gold, Beat the Bomb 32–50k). Alle Änderungen sind mit `ChloeCustom` markiert.
- **AI-Fahrer**: `Config/AIConfig.toml` (`NumAIProfiles`, Namen). Die DB-Profile: AI12 = Neville (NFS Carbon, fährt FO1 Blocker `car_112` und FO2 Insetta), AI13 = Ryan Cooper (nur FO2-Varianten). Die anderen fahren Vanilla-Autos.
- **Skins**: `Config/CarSkins.toml` legt je Datenordner Namen und Autor der Zusatz-Skins fest (`skinNname`, `skinN`). Skins ab 6 nutzen die Lackfarben `AiSkin*` aus der DB (Plugin `skindbextender.h`). Die Skins mit AI-Namen ("Lei Bing" = Insetta, Insetta Sport, Grinder) sind Lackierungen der AI-Fahrer.
- **Motorsounds**: Vanilla- und FO2-Autos nutzen nur `Sound/Engine` und `Sound/EngineBeta` aus dem Basisspiel. `carsounds` und `gearshift_fo1` brauchen nur FO1-, Rally- und TT-Autos.
- **Pacenotes**: Beifahrer-Ansagen (Stimme und Symbole) für Rally-Etappen. `Config/Pacenotes.toml` und `Config/Pacenotes/*.pac`, Stimmen in `Data/sound/rally/`, Symbole in `Data/textures/pacenotes*`. Die Optionen dazu stehen in `optionsmenu.bed` (Sound > "Rally Mode"). Das Plugin liest `Pacenotes.toml`, die Dateien deshalb nicht löschen.
- **Rally-Modus**: eigener Karrieremodus (`rallyclasses.bed`, `rally*menu.bed`), Einstieg über den Hauptmenüeintrag "RALLY MODE" in `mainmenu.bed`.
- **Resets**: `Config/Resets/<STRECKE>.rst/.rst2`, eigene Reset-Punkte je Strecke. `Config/Sectors/` enthält AI-Sektoren für einzelne Strecken.
- **Fehlerausgabe**: Das Plugin "Verbose LUA errors" zeigt bei Skriptfehlern Datei und Zeile.

## Im Spiel geprüft (Custom-Version, 18.11.2026)

- Das Spiel startet über Steam und läuft, auch ohne die entfernten Archive (Stufe 2).
- Die Skin "Lei Bing" hat zunächst zum Absturz geführt. Ursache war die fehlende 4096-Byte-Ausrichtung in unserem neu gepackten `carskins.bfs`, nicht der Inhalt. Eingegrenzt durch schrittweises Austauschen einzelner Dateien in der Original-Installation (AIConfig → Portraits → misc → cars + carskins → carskins → 1:1-Kopie).
- Original 1.83 ohne Änderungen: Die Skin "Lei Bing" funktioniert.

## Statische Prüfung der installierten Custom-Version (18.11.2026)

So nach jeder Änderung wiederholen, um Probleme wie den Skin-Absturz früh zu finden:

1. **Installation = Build:** Alle 853 Dateien laut Manifest sind byte-identisch mit `FlatOutUCChloeCollection_1.83_custom`.
2. **Archiv-Integrität** (`BfsTool verify` für jedes Archiv in `patch`): Jede Datei ist 4096-ausgerichtet, liegt innerhalb der Archivdatei und lässt sich entpacken. Größen und CRCs stimmen, es gibt keine doppelten Pfade. Ergebnis: 0 Fehler in allen 9 Archiven.
3. **Gefilterte Archive** (`BfsTool entries`): `cars`, `carskins` und `maps` sind Eintrag für Eintrag (Flags, Größen, CRC, SHA1 der gespeicherten Daten) identisch mit dem Original, abzüglich genau der ausgeschlossenen Pfade (2497 / 466 / 2115 entfernt).
4. **Neu gepackte Archive:** Entpacktes `misc` = Original-misc + `misc_overrides`. Nur die 7 geänderten Skripte weichen ab. Entpacktes `twobei_portraits` = `custom_src/portraits`.
5. **Skripte:** Alle verwendeten `CarIndex.*`/`LevelIndex.*` existieren. Klammern und Blöcke sind gleich ausgeglichen wie im Original (Lua-Interpreter ist nicht vorhanden, daher nur heuristisch).
6. **Verweise gegen alle geladenen Archive** (`data.bfs`, `streamed.bfs`, Mod-Archive, lose Dateien):
   - Alle 58 sichtbaren Strecken haben `geometry/`- und `data/`-Dateien.
   - Alle 90 sichtbaren Autoordner haben `body.bgm`, ein Menümodell und vollständige Skin-Sätze (5 Dateien je Skin, lückenlos).
   - Motorsounds: 256 referenzierte `engine/ai_*.wav` gibt es nicht als einzelne Dateien. Genau dieselben fehlen auch beim unveränderten Original. Sie stammen vermutlich aus den FMOD-Bänken (`mono.fsb`/`stereo.fsb`), sind also kein Problem.
   - Die 4 verbliebenen Karriere-Zeitfahren haben ihre Medaillen-Ghosts vollständig.

## Stolperfallen beim Bearbeiten

- In Git Bash entfernen `sed -i` und `awk` teilweise CR-Zeichen. Danach mit `sed -i 's/\r*$/\r/'` wieder CRLF herstellen. Prüfen lässt sich das mit `tr -cd '\r' | wc -c`.
- Git Bash wandelt Argumente, die mit `/` beginnen, in Windows-Pfade um (z. B. ein Regex `/rally/`).
- In `arcadeclasses.bed` stehen Autos auch in `AICar`, `AICar1` und `AICar2`, nicht nur in `Car`.
