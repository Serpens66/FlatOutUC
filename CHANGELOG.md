# Changelog

All notable changes to the Steam release of FlatOut UC Chloe Collection Custom.
Versions follow `<Chloe version>-custom.<n>`. The current development version is in [VERSION](VERSION).
`custom_src/package_steam.ps1` refuses to package a version without an entry here.

## [1.83-custom.3] – unreleased

## [1.83-custom.2] – 2026-11-26

Code update. Uses the same Assets ZIP as 1.83-custom.1 (`FlatOutUC-Steam-Assets-13a694c6bbe99df4.zip`).

### Fixed

- Crash or memory corruption in Carnage-style races (Carnage race, Speedtrap, Beat the Bomb) after an earlier race of that kind in the same session, single player and multiplayer. Chloe’s Carnage medal display kept a pointer to the HUD of a previous race; `ChloeSteamNet.asi` now clears it when a race is left.

### Added

- `ChloeCustom_version.txt` in the game folder shows the installed version. The release ZIP contains this changelog.

### Changed

- Only the current Steam version (normal `public` branch) is supported. The build no longer produces a separate package for the old `gfwl` branch.

### Tests

- New automatic multiplayer sweep `-Scenario Modes -TrackSweep` (175 cases): every multiplayer track and FlatOut 2 track variant, plus every track of the Reversed, Carnage race, Speedtrap and Beat the Bomb lists, in a Steam host lobby with AI opponents, rotating through all 100 selectable cars (base cars and FlatOut 2 variants) and all 5 stunt cars. All cases and all 24 multiplayer game modes pass.

## [1.83-custom.1] – 2026-11-21

First release for the current Steam version (normal `public` branch).

- Steam multiplayer in Chloe’s multiplayer menu: host with **Create Game**, find Steam lobbies with **Join Game**, no port forwarding. AI opponents, tournaments, simultaneous stunt events with synchronized scores and non-colliding ghosts of the other players.
- Multiplayer fixes: derby time limit works, the host’s handling mode applies to everyone, race progress of other players is shown in the lobby.
- Original single-player progression: vanilla Career and Carnage events, car prices and driver portraits. Single Race and the car dealer additionally offer FlatOut 2 tracks and cars.
- No additional tracks, cars or portraits besides FlatOut 2 content. Music reduced to FlatOut 2, FlatOut UC and Modern playlists.
- DisplayFix for the zero-height window on some dual-GPU or multi-monitor systems.
- Installer with separate code and asset ZIPs, and a launcher for the unmodded game with separate saves.
