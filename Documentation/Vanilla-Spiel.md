# Erkenntnisse: FlatOut Ultimate Carnage (Vanilla)

Hier steht, was für das Spiel selbst gilt, unabhängig von der Chloe Collection.
Mod-spezifisches steht in [Modpack.md](Modpack.md).

## Steam-Version und Installation

- Steam-App **12360** ("FlatOut: Ultimate Carnage Collector's Edition"), Ordner `steamapps/common/FlatOut Ultimate Carnage CE`.
- Branches (Quelle: `https://api.steamcmd.net/v1/info/12360`):
  - `public`: aktuelle, neuere Version (Build 26753292). Mit den gcp-Mods **inkompatibel**, unser angepasstes Paket dafür: [Steam-Version.md](Steam-Version.md).
  - `gfwl`: "old gfwl version", **Build 2753**. Diese Version brauchen die Mods. Auswahl: Steam > Eigenschaften > Betas > `gfwl`.
- Startkonfigurationen laut Steam: `Fouc.exe` (Standard), `Launcher.exe` (GFWL-Version), `Fouc.exe -launcher` (Konfiguration).
- Die GFWL-`Fouc.exe` ist **4242504 Bytes** groß. Daran erkennt man den richtigen Branch.
- Eine frisch installierte GFWL-Version startet **ohne Mod nicht**, weil Games for Windows Live fehlt. Die Mod bringt ein Ersatz-`xlive.dll` mit, damit startet das Spiel.
- Vanilla-Dateien im Spielordner: `Fouc.exe`, `launcher.exe`, `LiveUpdateLauncher.exe`, `data.bfs` (~3,7 GB), `streamed.bfs`, `filesystem`, `fmodex.dll`, `binkw32.dll`, `fouc.exe.cat`, `fouc.exe.cfg`, `boxart.png`, `EULA_*.txt`, `InstallScript.vdf`, `support/` (DirectX, XLive, VC-Redist) sowie `data/music/*.ogg` (lose Musikdateien).
- Beim Deinstallieren über Steam bleiben fremde Dateien (Mods, Spielstände) im Ordner liegen. Für eine saubere Neuinstallation den Ordner danach von Hand löschen.
- Windows unterscheidet keine Groß-/Kleinschreibung: Ein `Data/` der Mod landet im selben Ordner wie das vanilla `data/`.

## Archive (BFS) und Ladereihenfolge

- `filesystem` (Textdatei) listet die Basis-Archive: `data.bfs`, `streamed.bfs`.
- `patch` (Textdatei) listet weitere Archive. **Spätere Einträge überschreiben frühere** (die Mod nutzt das bewusst). Die Textdatei darf keine abschließende Leerzeile haben (Hinweis aus BFSpack-Readme).
- Format: `bfs1`, Version `0x20070310` (FOUC, auch Sega Rally Revo). Quelle: xNyaDev/bfstool (Branch `dev-2.0` zum Lesen, Branch `1.x` für Hash und Schreiben).
  - Header: `magic`, `version`, `header_end` (= Beginn der Daten), `file_count`, `hash_size` (immer 0x3E5 = 997).
  - Hash-Tabelle: 997 × (`offset` des ersten Datei-Headers im Bucket, `count`).
  - Metadaten-Header bei `997*8+20`: Offsets (relativ dazu) auf Datei-Header, Namens-Offset-Tabelle, Namens-Längen-Tabelle, Huffman-Baum, Huffman-Daten.
  - Dateinamen: Ordner- und Dateiname getrennt, als eindeutige Liste, Huffman-kodiert, je Name byte-ausgerichtet. Huffman-Baum in Preorder: Branch-Eintrag (`value` = Index des rechten Kinds, linkes Kind folgt direkt), Leaf (`0x80`, Zeichen). Linkes Kind = Bit 1, rechtes = Bit 0, Bits LSB-zuerst.
  - Datei-Header (24 Bytes + 4 je Kopie): `flags` (Bit 0 = zlib, Bit 2 = CRC gültig), Padding, `file_copies` (u16), `data_offset`, `unpacked_size`, `packed_size`, `crc32` (CRC-32/JAMCRC), `folder_id`, `file_id`.
  - Hash eines Pfads (Bucket): "lua_hash" über den vollen Pfad (`data/...`), Groß-/Kleinschreibung wie gespeichert, modulo 997.
  - **Jede Datei beginnt an einer 4096-Byte-Grenze.** Alle Original-Archive sind so gebaut. Ohne Ausrichtung laden viele Dateien trotzdem, aber das Spiel stürzt bei unkomprimierten Texturen ab (beobachtet bei einer Auto-Skin).
- Das alte Tool `BFSpack_V2.1` (BFS3pack) kann diese Archive nicht lesen. Es liefert eine leere Liste. Eigenes Tool: `tools/BfsTool/BfsTool.cs`.

## Datenbank `data/database/game.db`

- Format "PDB1" (`0x1A424450`), Version 512. Quelle: gaycoderprincess/FlatOut2DBTool. Eigenes Tool zum Auslesen: `tools/DbDump/DbDump.cs`.
- Aufbau: Header (12 Bytes), dann ein Array von Knoten zu je 20 Bytes: `vtable`, `parentOffset` (i16, relativ in Knoten), `lastChildOffset`, `prevNodeOffset`, `dataCount` (u16), `pNameString`, `pValues` (beide relativ zur Knotenadresse).
- Werte: `pName` (relativ), `type` (u8), `size` (u16, Bytes), `arrayType` (u8), 4 Bytes Laufzeitzeiger, danach die Daten. Typen: 1 char, 2 string, 5 bool, 6 int, 7 float, 8 rgba, 9–11 vec2–4, 12 Knotenverweis (u16-Index).
- Wichtige Bereiche:
  - `root/Data/Cars/<Klasse>/<Auto>`: `DataPath` (z. B. `data/Cars/Car_10/`), `EngineSound`/`BetaEngineSound` (Verweise auf `root/Data/Sound/...`), `Body`, `Engine` usw.
  - `root/Data/Cars/<Klasse>/<Auto>Visuals`: Lackfarben `Skin1..5*` für die Spieler-Skins und `AiSkin1..3*` für die AI-Skins.
  - `root/Data/Sound/Engine/CarNN` bzw. `EngineBeta/...`: Motorsounds der Vanilla-Autos.
  - `root/Data/Profile/AI<n>_<Name>`: AI-Fahrer. `Class[0..3].CarNum` ist die **Datenordner-Nummer** (`car_N`), nicht der DB-Index.
- Die Reihenfolge der Autos in der DB entspricht **nicht** der Reihenfolge der Datenordner. Vanilla-Skripte nutzen DB-Indizes als Zahlen (z. B. `Car = 46` in `arcadeclasses.bed`).

## Lua-Skripte (`data/scripts/*.bed`)

- Lua 5.0 (z. B. `string.gfind`), Dateien mit CRLF-Zeilenenden.
- `levels.bed`: Tabelle `Levels[ID]` mit `Name`, `TrackType`, `StagePath` (wird auch für das DB-Mapping genutzt, Groß-/Kleinschreibung beachten).
- `classes.bed`: Karriere (`ClassC/B/A`, `SubClasses`, `Cups`, `Events`). `arcadeclasses.bed`: Carnage-Modus. `singleracemenu.bed`: Single Race.
- Auf Autos kann man per `db.FlatOut2.Cars:GetProperty("Car", idx)` zugreifen, mit `car.DataPath` und `car:GetFullPath()`.
- Komplett entpackte Vanilla-Daten zum Nachschlagen: `alte portraits/data FOUC/`.

## Sounds

- Die Motorsound-Knoten in der DB (`root/Data/Sound/Engine...`) verweisen auf Samples relativ zu `data/sound/` (z. B. `engine/...wav`, `gearshift/...`).
- Ein Teil davon (u. a. die AI-Samples `engine/ai_*.wav` der "Beta"-Motorsounds) existiert nicht als einzelne Datei in den Archiven. Vermutlich liegen sie in den FMOD-Bänken `data/sound/mono.fsb` / `stereo.fsb`. Bei Dateiprüfungen ist das kein Fehler.

## Portraits und Ladebildschirme

- `data/menu/character_images_small.tga/.dds` + `.bed`: Atlas der AI-Portraits (512×256, `ai_portrait_1..11_small`, Spieler-Portraits, `portrait_empty`).
- `data/global/overlay/ai_damage_meter.dds` + `.bed`: Gesichter in der Schadensanzeige im Rennen (`ai_face_N`).
- `data/scripts/loadingscreens/ai1..ai11.bed`: Ladebildschirme der AI-Fahrer, `Background = "data/menu/loading_aiN.tga"`. Die Bilder liegen in `data.bfs`.

## Skins

- Spieler-Skins sind `data/cars/car_N/skinK.dds`. Zu jeder Skin gehören 5 Dateien: `skinK.dds`, `_damaged`, `_damaged_normal`, `_normal`, `_specular`. Fehlt eine davon, endet die Zählung der Skins dort.
- Die AI-Skins (`skinai1..3.dds`) haben eigene Lackfarben in der DB (`AiSkinN*`).
