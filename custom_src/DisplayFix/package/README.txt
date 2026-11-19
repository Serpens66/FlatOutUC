DisplayFix 1.0 for FlatOut: Ultimate Carnage
============================================

(English below)

DEUTSCH
-------

Was es behebt
  Auf manchen PCs ist das Spielfenster nach dem Start unsichtbar: nur 0-2 Pixel hoch, als winziges oder
  "minimiertes" Fenster, oder das Spiel startet mit einer Auflösung von 0x0. Betroffen sind vor allem Laptops
  mit zwei Grafikchips (z. B. AMD/Intel + NVIDIA), bei denen der Bildschirm nicht am ersten Windows-Anzeigegerät
  hängt. Mit Mods, die den Setup-Dialog überspringen (z. B. Chloe's Collection), passiert das bei jedem Start.

Ursache (Fehler in Fouc.exe)
  Fouc.exe ermittelt die Desktop-Auflösung über EnumDisplayDevicesA(NULL, <D3D-Adapternummer>) und
  EnumDisplaySettingsA. Dabei wird angenommen, dass die Direct3D-Adapternummer der Windows-Gerätenummer
  entspricht, und ein Fehlschlag wird ignoriert. Ist Windows-Anzeigegerät 0 inaktiv, übernimmt das Spiel
  uninitialisierten Speicher als Auflösung. Das passiert immer dann, wenn Savegame/device.cfg nicht zum
  Grafikadapter passt.

Was das Plugin macht
  Es ersetzt beim Start genau diesen einen Aufruf (Adresse 0x456DCC). Liefert das abgefragte Gerät keine
  Auflösung, wird der Hauptbildschirm verwendet (das ist immer Direct3D-Adapter 0), notfalls 1024x768 bei
  60 Hz. Auf PCs ohne das Problem ändert sich nichts. Das Plugin prüft vorher die Originalbytes und tut bei
  einer anderen Fouc.exe nichts. Es ändert keine Dateien.

Kompatibilität
  - FlatOut: Ultimate Carnage, Fouc.exe mit 4242504 Bytes: Steam-Version (Branch "public", ZOOM-Update) und
    die alte GFWL-Version (Steam-Branch "gfwl"). Beide haben dieselbe Fouc.exe.
  - Mit und ohne andere Mods. Getestet mit Chloe's Collection 1.83 auf der Steam-Version
    (Laptop mit AMD Radeon 610M + RTX 5060, Bildschirm 2560x1600: vorher 2 Pixel hoch, danach korrekt).

Installation
  1. DisplayFix.asi in den Spielordner kopieren (dort, wo Fouc.exe liegt).
  2. Ein ASI-Loader wird benötigt:
     - Liegen im Spielordner schon andere .asi-Dateien (z. B. von Chloe's Collection), ist bereits ein
       Loader vorhanden. Dann NUR DisplayFix.asi kopieren, NICHT winmm.dll.
     - Sonst zusätzlich winmm.dll kopieren (Ultimate ASI Loader 6.9.0, 32 Bit, von ThirteenAG).
  3. Spiel normal starten.

Entfernen
  DisplayFix.asi löschen (und winmm.dll, falls sie aus diesem Paket stammt).

Hinweis
  Steht in Savegame/device.cfg bereits die Auflösung 0x0 aus einem früheren Start, ist das kein Problem:
  Das Spiel verwirft diese Werte und ermittelt die Auflösung neu, jetzt korrekt.


ENGLISH
-------

What it fixes
  On some PCs the game window is invisible after start: only 0-2 pixels high, a tiny or "minimised" window,
  or the game starts with a resolution of 0x0. Mostly affected are laptops with two GPUs (e.g. AMD/Intel +
  NVIDIA) where the panel is not attached to the first Windows display device. With mods that skip the setup
  dialog (e.g. Chloe's Collection) this happens on every start.

Cause (bug in Fouc.exe)
  Fouc.exe reads the desktop resolution via EnumDisplayDevicesA(NULL, <D3D adapter ordinal>) and
  EnumDisplaySettingsA. It assumes that the Direct3D adapter ordinal equals the Windows display device index
  and ignores a failure. If Windows display device 0 is inactive, the game uses uninitialised memory as the
  resolution. This happens whenever Savegame/device.cfg does not match the graphics adapter.

What the plugin does
  It redirects exactly this one call at startup (address 0x456DCC). If the requested device has no current
  mode, the primary display is used (always Direct3D adapter 0), as a last resort 1024x768 at 60 Hz. On PCs
  without the problem nothing changes. The plugin checks the original bytes first and does nothing on a
  different Fouc.exe. It does not modify any files.

Compatibility
  - FlatOut: Ultimate Carnage, Fouc.exe with 4242504 bytes: current Steam version (branch "public", ZOOM
    update) and the old GFWL version (Steam branch "gfwl"). Both use the same Fouc.exe.
  - With or without other mods. Tested with Chloe's Collection 1.83 on the Steam version
    (laptop with AMD Radeon 610M + RTX 5060, 2560x1600 panel: 2 pixels high before, correct afterwards).

Installation
  1. Copy DisplayFix.asi into the game folder (where Fouc.exe is).
  2. An ASI loader is required:
     - If the game folder already contains other .asi files (e.g. from Chloe's Collection), a loader is
       already installed. Copy ONLY DisplayFix.asi, NOT winmm.dll.
     - Otherwise also copy winmm.dll (Ultimate ASI Loader 6.9.0, 32-bit, by ThirteenAG).
  3. Start the game as usual.

Uninstall
  Delete DisplayFix.asi (and winmm.dll if it came from this package).


Contents / Inhalt
  DisplayFix.asi                   the fix / der Fix
  winmm.dll                        Ultimate ASI Loader 6.9.0 (Win32), https://github.com/ThirteenAG/Ultimate-ASI-Loader
  LICENSE_UltimateASILoader.txt    MIT license of the ASI loader
  source/                          source code and build script (MSVC 2022, x86)
