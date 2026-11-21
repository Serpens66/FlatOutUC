# FlatOut UC Chloe Collection Custom (Steam)

A free, unofficial adaptation of **Chloe’s Collection 1.83** for the current Windows Steam version of **FlatOut: Ultimate Carnage Collector’s Edition**. It builds on Chloe’s quality-of-life improvements and multiplayer, with a focus on Ultimate Carnage and FlatOut 2 content. [Original mod and credits](https://gaycoderprincess.github.io/project/chloe-collection) · [Chloe’s main plugin source](https://github.com/gaycoderprincess/FlatOutUCChloeCollection)

## What’s different

- **Steam multiplayer.** Host with **Create Game** and find Steam lobbies with **Join Game** in Chloe’s multiplayer menu. Steam lobbies work without manual port forwarding. You can race against AI, run tournaments, and try experimental stunt events with synchronized scores and visible, non-colliding ghosts of other players.
- **Original single-player progression.** The original Career and Carnage events and driver portraits are restored. Single Race offers additional FlatOut 2 tracks, and the car dealer offers FlatOut 2 cars and variants.
- **Music and display.** Pick from revised playlists, including a combined FlatOut 2 and Ultimate Carnage soundtrack. DisplayFix addresses a startup issue on some multi-monitor or dual-GPU systems.

## Install or update

1. Install the game through Steam on Windows. Under **Properties → Betas**, select **None** to use the normal Steam branch.
2. For a first installation, extract the **Code** ZIP and the matching **Assets** ZIP from the release notes into the same new folder outside the game directory. [GitHub Releases](https://github.com/Serpens66/FlatOutUC-ChloeCollection-Custom/releases)
3. Close the game and run `Install or Update.bat`. If prompted, select the Steam game folder containing `Fouc.exe`.
4. Launch the game from the Steam Library. Use the same mod release on every PC for multiplayer.

For an update, extract the new Code ZIP into a new folder. Also extract the matching Assets ZIP there if the release lists a different one from the one you have installed. Run the same installer. Updates keep profiles, saves and in-game settings. Bundled mod configuration can change.

To play without the modpack, leave Steam open and run `FlatOut ohne Modpack starten.bat` from the game directory. The launcher switches to separate vanilla saves and restores the modpack when you exit. If the launcher was closed early, run it again to restore the modpack. DisplayFix and DXVK stay active in this mode.

## Kurzanleitung auf Deutsch

1. In Steam unter **Eigenschaften → Betas** den normalen Spielzweig **Keine** wählen.
2. Bei der Erstinstallation das **Code-ZIP** und das in den Release-Notizen genannte **Assets-ZIP** in denselben neuen Ordner außerhalb des Spielordners entpacken. [GitHub-Releases](https://github.com/Serpens66/FlatOutUC-ChloeCollection-Custom/releases)
3. Spiel schließen, `Install or Update.bat` starten und bei Bedarf den Ordner mit `Fouc.exe` auswählen. Anschließend wie gewohnt über Steam starten. Für Multiplayer brauchen alle denselben Release-Stand.
4. Für Updates das neue Code-ZIP in einen neuen Ordner entpacken. Falls der Release ein anderes Assets-ZIP nennt als das installierte, dieses ebenfalls dort entpacken. Danach denselben Installer starten.

Für das Originalspiel mit getrennten Spielständen bei geöffnetem Steam `FlatOut ohne Modpack starten.bat` im Spielordner ausführen. Nach Spielende aktiviert der Launcher das Modpack wieder. DisplayFix und DXVK bleiben aktiv.

## Credits and licenses

Chloe (gaycoderprincess) created the original overhaul. Thanks to Chloe and the contributors credited in the original project. The Steam adaptation is maintained by Serpens66. [Chloe’s full credits](https://gaycoderprincess.github.io/project/chloe-collection#credits)

The FlatOut 2 cars, track assets and soundtrack selections come from the original game by Bugbear Entertainment and are used here through Chloe’s Collection. The original game assets and music belong to their respective rights holders. Our contributions include the Steam adaptation and the playlist edits.

Original source code contributed to this Steam adaptation is available under the [Mozilla Public License 2.0](LICENSE). See the [third-party notices](THIRD-PARTY-NOTICES.md) for upstream sources, local modifications and separate terms for bundled components.
