# Licenses, source code and credits

## This Steam adaptation

The project is intended to be distributed free of charge. Original source code contributed by this project is licensed under the [Mozilla Public License 2.0](LICENSE). That includes the original code in `ChloeSteamNet`, `DisplayFix`, the installer and launcher, and the local build and patch scripts. Files incorporating upstream material retain the applicable upstream terms.

For our `ChloeSteamNet.asi` and `DisplayFix.asi` binaries, use the source and x86 build scripts under `custom_src` at the Git tag for your release. GitHub offers source archives for that tag on the same release page. [ChloeSteamNet source and build script](custom_src/ChloeSteamNet/) · [DisplayFix source and build script](custom_src/DisplayFix/) · [Releases](https://github.com/Serpens66/FlatOutUC-ChloeCollection-Custom/releases)

## Chloe’s Collection 1.83

Chloe (gaycoderprincess) created the original mod and its companion plugins. The 1.83 download supplies the baseline binaries and mod data for this adaptation. Chloe’s project page and the original 1.83 readme list the individual contributors and link to the plugin source repositories. The plugin repositories identify the source code they cover. The original download does not establish a single license for all its files. [Original download and credits](https://gaycoderprincess.github.io/project/chloe-collection#credits) · [Upstream source list](https://gaycoderprincess.github.io/project/chloe-collection#source-code)

### MPL plugin source references

The revisions below are the MPL-2.0 upstream checkouts used as references for this project. The correspondence between these checkouts and the exact binaries in the 1.83 download is unverified.

| Bundled plugin | Upstream source reference |
| --- | --- |
| `FlatOutUCChloeCollection_gcp.asi` | [FlatOutUCChloeCollection at `755810e`](https://github.com/gaycoderprincess/FlatOutUCChloeCollection/tree/755810e825d780ed71395adb4cd873d80b723e1f) |
| `FlatOutUCFO2Handling_gcp.asi` | [FlatOutUCFO2Handling at `aeb0a07`](https://github.com/gaycoderprincess/FlatOutUCFO2Handling/tree/aeb0a07c25762c1a1e4beaf2917f3037d570e5fc) |
| `FlatOutUCNoDSP_gcp.asi` | [FlatOutUCNoDSP at `449d1d3`](https://github.com/gaycoderprincess/FlatOutUCNoDSP/tree/449d1d3c6dff0450646ec2c602d6a2fec9ec8792) |
| `FlatOutUCNoIntro_gcp.asi` | [FlatOutUCNoIntro at `a36778a`](https://github.com/gaycoderprincess/FlatOutUCNoIntro/tree/a36778ae9fd9b35699a9f90121b7dcee89f1437f) |
| `FlatOutUCNoLauncher_gcp.asi` | [FlatOutUCNoLauncher at `2e9d631`](https://github.com/gaycoderprincess/FlatOutUCNoLauncher/tree/2e9d6316859a8c061d7cf55ba4d0ddd4c309d70d) |
| `FlatOutUCNoWallriding_gcp.asi` | [FlatOutUCNoWallriding at `cf01484`](https://github.com/gaycoderprincess/FlatOutUCNoWallriding/tree/cf014844fedcc4712cd14911dc99688d86c70046) |
| `FlatOutUCTimeTrialGhosts_gcp.asi` | [FlatOutUCTimeTrialGhosts at `3243d19`](https://github.com/gaycoderprincess/FlatOutUCTimeTrialGhosts/tree/3243d19818ee0e026aa06f696e702225514b4cbc) |
| `FlatOutUCUltrawideSupport_gcp.asi` | [FlatOutUCUltrawideSupport at `14cfe60`](https://github.com/gaycoderprincess/FlatOutUCUltrawideSupport/tree/14cfe60ec5b1aa5424bcddbef8b7b7721f772525) |
| `FlatOutUCVerboseLUAErrors_gcp.asi` | [FlatOutUCVerboseLUAErrors at `07abba1`](https://github.com/gaycoderprincess/FlatOutUCVerboseLUAErrors/tree/07abba1abbc5043a438c1992a06bc693da4cc28c) |

The shared libraries referenced by the upstream build instructions include [nya-common at `31626fd`](https://github.com/gaycoderprincess/nya-common/tree/31626fdcfb458e3e9b74a1a2a258f95b92771072) and [nya-common-fouc at `c958743`](https://github.com/gaycoderprincess/nya-common-fouc/tree/c958743c09532c0ef519f6cacc949c1eea8be868). These are also reference revisions.

### Local changes and outstanding source matches

- The build patches the Steam-version detection in each bundled `*_gcp.asi`. It also adjusts defaults and disables Chloe’s custom achievements in the main plugin. The local build scripts contain the exact byte edits. The menu, database and playlist changes are maintained under `custom_src`. [Base build and edits](custom_src/build.ps1) · [Steam build and edits](custom_src/build_steam.ps1) · [Customization sources](custom_src/)
- `FlatOutUCCustomMP_gcp.asi` comes from the original 1.83 package and receives the Steam compatibility patch. Its corresponding source and license are still unverified. The separately published `ChloeSteamNet` source covers our Steam transport and lobby integration.
- `FlatOutUCHLMovementCC_gcp.asi` also receives the Steam compatibility patch. Chloe’s published `FlatOutUCHLMovement` project uses MPL 2.0. Its relationship to the exact `CC` binary is unverified. [FlatOutUCHLMovement](https://github.com/gaycoderprincess/FlatOutUCHLMovement)
- The plugin reference links and byte edits record what was checked. Completing the source offer for a binary distributed under MPL 2.0 requires the corresponding source and any local modifications in source form under MPL 2.0. [MPL 2.0, §§ 3.1–3.4](https://www.mozilla.org/en-US/MPL/2.0/)

## Other bundled components

| Component | Origin and terms |
| --- | --- |
| `winmm.dll` | ThirteenAG’s Ultimate ASI Loader 6.9.0, MIT. The Steam build renames the loader distributed as `xlive.dll` in Chloe’s package. Its bytes are unchanged. [Upstream release](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v6.9.0) · [Copyright and MIT license](custom_src/DisplayFix/package/LICENSE_UltimateASILoader.txt) |
| `d3d9.dll` | DXVK. The binary from Chloe’s package identifies itself as `v2.7.1`. DXVK’s upstream release uses the zlib/libpng license. [Upstream release](https://github.com/doitsujin/dxvk/releases/tag/v2.7.1) · [Copyright and license at v2.7.1](https://raw.githubusercontent.com/doitsujin/dxvk/v2.7.1/LICENSE) |
| `bass.dll` | BASS 2.4.17 by Un4seen Developments. BASS has its own license, including terms for non-commercial use. The conditions applicable to redistribution of this copy should be checked against that version’s distribution terms. [BASS licensing](https://www.un4seen.com/bass.html#license) |
| `FreemanAPI_gcp.dll` | Chloe’s FreemanAPI. Upstream identifies its original code as MPL 2.0 and Valve-derived code as subject to the Source SDK license. The source revision corresponding to this DLL and its exact notices are unverified. [FreemanAPI source and license information](https://github.com/gaycoderprincess/FreemanAPI) |
| FlatOut 2 game assets | The underlying car models, track geometry and textures used for FlatOut 2 content originate in Bugbear Entertainment’s FlatOut 2. Chloe’s Collection supplies the ports used by this project. Custom skins and other mod additions are credited to their respective creators. [Chloe’s asset and contributor credits](https://gaycoderprincess.github.io/project/chloe-collection#credits) |
| Music | The FlatOut 2 selections are recordings featured in the original FlatOut 2 soundtrack and supplied through Chloe’s Collection. The playlists also draw on Ultimate Carnage’s soundtrack and other selections supplied by Chloe’s Collection. This project edits the playlists. Rights in the compositions and recordings remain with the respective rights holders. |
| Other game and mod assets | Vanilla portraits, fonts and some scripts originate in FlatOut: Ultimate Carnage. Chloe’s Collection also supplies community-created material, including skins and artwork. Redistribution terms for game and mod assets need to be established with the relevant rights holders. [Chloe’s asset and contributor credits](https://gaycoderprincess.github.io/project/chloe-collection#credits) · [FlatOut: Ultimate Carnage on Steam](https://store.steampowered.com/app/12360/FlatOut_Ultimate_Carnage/) |

Thanks to the original mod contributors named by Chloe, including selo920, Mutacja, xInfamousRYANx, Cordo, Twobei, SwiftZC33S, Hane, teddyator, Mac21Sas, rowwise, Yellow_Fantom, DEVYATION and Bulko. [Original credits](https://gaycoderprincess.github.io/project/chloe-collection#credits)
