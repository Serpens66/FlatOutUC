# Builds FlatOutUCChloeCollection_1.83_custom from the untouched original + custom_src.
# Usage (from anywhere):  pwsh -File "custom_src/build.ps1"
# Re-running is safe: the target folder is rebuilt from scratch.
$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent $PSScriptRoot
$src      = Join-Path $root 'FlatOutUCChloeCollection_1.83'
$dst      = Join-Path $root 'FlatOutUCChloeCollection_1.83_custom'
$custom   = $PSScriptRoot
$bfsTool  = Join-Path $root 'tools/BfsTool/BfsTool.cs'
$work     = Join-Path ([IO.Path]::GetTempPath()) 'fouc_custom_build'

# Archives that are rebuilt from custom_src (not taken from the original)
$rebuilt = @('foucpack_gcp_misc.bfs', 'foucpack_gcp_multiplayer.bfs', 'foucpack_gcp_music.bfs', 'foucpack_twobei_portraits.bfs')

# Stage 2: archives that are dropped completely (removed from 'patch' and not copied).
# All of them only contain content that is hidden by the script changes (checked via game.db / car inis).
$dropped = @(
    'foucpack_gcp_maps_extras.bfs',          # Burnout, Drift, GT, Mario, NFS, ProStreet, Re-Volt, San Juan, URL
    'foucpack_gcp_maps_rallytrophy.bfs',     # Rally Trophy stages
    'foucpack_gcp_maps_teddyator.bfs',       # Canal 4 (unused) + FO1 track edits
    'foucpack_gcp_mapcols.bfs',              # collision data of FO1/Pit/Town/Winter/Retro tracks only
    'foucpack_gcp_mapsplines.bfs',           # AI splines of Rally/FO1/Retro tracks only
    'foucpack_gcp_cars_rallytrophy.bfs',     # cars 4xx
    'foucpack_gcp_cars_extras.bfs',          # cars 3xx (NFS etc.)
    'foucpack_gcp_cars_extras_props.bfs',    # prop cars 3xx
    'foucpack_gcp_carsounds.bfs'             # engine sounds FO1/RT/TT (FO2 cars use vanilla Engine/EngineBeta only)
)

# Stage 2: mixed archives that are repacked without non-FO2 content (stored data is copied 1:1).
$filtered = [ordered]@{
    'foucpack_gcp_cars.bfs'     = '^data/cars/car_[134]\d\d/|^data/menu/cars/menucar_[134]\d\d\.'
    'foucpack_gcp_carskins.bfs' = '^data/cars/(car_[134]\d\d|zztest)/'
    'foucpack_gcp_maps.bfs'     = '^data/tracks/(fo1forest|fo1racing|pit|pit_|retrodemo|toughtrucks|town|winter)/|^data/tracks/[^/]+$'
}

# Filtered archives that are also recompressed (zlib). The mod stores them uncompressed (cars 1.5 GB -> ~0.5 GB).
$recompress = @('foucpack_gcp_cars.bfs', 'foucpack_gcp_carskins.bfs')
# Stored uncompressed like in the game's own archives: textures used live (all .dds except *_damaged*) and the menu car
# models. Compressed versions of these crash the game (Chili skin 'Sofia Martinez', 2026-11-19).
$recompressStore = '^(?!.*_damaged).*\.dds$|menucar_\d+\.bgm$'

# Loose files that are not copied (Data\sound\rally = rally pacenotes, rally mode is disabled) (relative paths, regex). Medal ghosts exist only for the removed time trials,
# except the four FO2 track/car combinations that are still used in the career.
$looseExclude = '^Data\\sound\\rally\\|^Ghosts\\Rally\\|^Ghosts\\Career\\(?!(CITYCENTRAL1_ChiliPepper|DOWNTOWN2_RoadKing|MOTORRACEWAY2_(Speedevil|Sunray)|RIVERBAYCIRCUIT3_BulletGT)_)'

# Music chosen by the user (custom_src/make_music.py): loose songs that are not copied
$musicRemove = @(Get-Content (Join-Path $custom 'music_remove.txt') | Where-Object { $_ -and -not $_.StartsWith('#') } |
    ForEach-Object { ('Data\music\' + $_).ToLowerInvariant() })

# Files above this size are hardlinked instead of copied (same drive, no extra disk space).
# Never edit hardlinked files in place - that would change the original too.
$hardlinkThreshold = 50MB

# Chloe 1.83 stores the initial Network.DefaultTickRate index in the settings table's backing variable.
# Change the initial value for runs without a saved setting. The game still loads saved values normally.
$chloePlugin = 'FlatOutUCChloeCollection_gcp.asi'
$chloeBytes = [IO.File]::ReadAllBytes((Join-Path $src $chloePlugin))
$chloeHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($chloeBytes))
$tickRateOffset = 0x49C640
if ($chloeHash -ne '2D40C7311B95BE36ED2796F35C650E46EDD41DC55CA2E2F7902B94F35707DEEE' -or
    [BitConverter]::ToUInt32($chloeBytes, $tickRateOffset) -ne 1) {
    throw "Unexpected Chloe 1.83 plugin or tickrate default in $chloePlugin"
}
$chloeBytes[$tickRateOffset] = 2 # zero-based index for 30 updates per second

function Invoke-BfsTool([string[]]$toolArgs) {
    & dotnet run $bfsTool -- @toolArgs
    if ($LASTEXITCODE -ne 0) { throw "BfsTool failed: $($toolArgs -join ' ')" }
}

if (Test-Path $dst) { Get-ChildItem $dst -Force | Remove-Item -Recurse -Force }
else { New-Item -ItemType Directory $dst | Out-Null }
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null

# 1. copy / hardlink the original
Get-ChildItem $src -Recurse -File | ForEach-Object {
    $rel = [IO.Path]::GetRelativePath($src, $_.FullName)
    if ($rel -eq $chloePlugin -or $rebuilt -contains $rel -or $dropped -contains $rel -or $filtered.Contains($rel)) { return }
    if ($rel -match $looseExclude) { return }
    if ($musicRemove -contains $rel.ToLowerInvariant()) { return }
    $target = Join-Path $dst $rel
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    if ($_.Length -ge $hardlinkThreshold) { New-Item -ItemType HardLink -Path $target -Target $_.FullName | Out-Null }
    else { Copy-Item $_.FullName $target }
}
[IO.File]::WriteAllBytes((Join-Path $dst $chloePlugin), $chloeBytes)

# 2. misc archive = original content + script overrides
$miscDir = Join-Path $work 'misc'
Invoke-BfsTool @('extract', (Join-Path $src 'foucpack_gcp_misc.bfs'), $miscDir)
Copy-Item (Join-Path $custom 'misc_overrides/*') $miscDir -Recurse -Force
#    vanilla car prices in game.db (generated by custom_src/make_vanilla_singleplayer.py)
& dotnet run (Join-Path $root 'tools/DbPatch/DbPatch.cs') -- (Join-Path $miscDir 'data/database/game.db') (Join-Path $custom 'gamedb_patch.txt')
if ($LASTEXITCODE -ne 0) { throw 'DbPatch failed' }
Invoke-BfsTool @('pack', $miscDir, (Join-Path $dst 'foucpack_gcp_misc.bfs'))

# 2b. multiplayer archive (Chloe's own multiplayer) = original content + script overrides
#     (generated by custom_src/make_mp_scripts.py: FO2 tracks and car classes only)
$mpDir = Join-Path $work 'multiplayer'
Invoke-BfsTool @('extract', (Join-Path $src 'foucpack_gcp_multiplayer.bfs'), $mpDir)
Copy-Item (Join-Path $custom 'multiplayer_overrides/*') $mpDir -Recurse -Force
Invoke-BfsTool @('pack', $mpDir, (Join-Path $dst 'foucpack_gcp_multiplayer.bfs'))

# 2c. music archive = original playlists + custom_src/music_overrides (generated by make_music.py:
#     playlists without the removed songs, combined playlist "FO1, FO2, FOUC, MODERN"); Music.toml is in config_overrides
$musicDir = Join-Path $work 'music'
Invoke-BfsTool @('extract', (Join-Path $src 'foucpack_gcp_music.bfs'), $musicDir)
Copy-Item (Join-Path $custom 'music_overrides/*') $musicDir -Recurse -Force
Invoke-BfsTool @('pack', $musicDir, (Join-Path $dst 'foucpack_gcp_music.bfs'))

# 3. portraits archive = vanilla FOUC portraits + vanilla loading screen scripts
Invoke-BfsTool @('pack', (Join-Path $custom 'portraits'), (Join-Path $dst 'foucpack_twobei_portraits.bfs'))

# 3b. mixed archives without non-FO2 content
foreach ($name in $filtered.Keys) {
    if ($recompress -contains $name) {
        # extract, drop the excluded paths, pack again (pack compresses every file where zlib is smaller)
        $dir = Join-Path $work ([IO.Path]::GetFileNameWithoutExtension($name))
        Invoke-BfsTool @('extract', (Join-Path $src $name), $dir)
        Get-ChildItem $dir -Recurse -File | Where-Object {
            ([IO.Path]::GetRelativePath($dir, $_.FullName) -replace '\\', '/').ToLowerInvariant() -match $filtered[$name]
        } | Remove-Item
        Invoke-BfsTool @('pack', $dir, (Join-Path $dst $name), $recompressStore)
        Remove-Item $dir -Recurse -Force
    } else {
        Invoke-BfsTool @('filter', (Join-Path $src $name), (Join-Path $dst $name), $filtered[$name])
    }
}

# 4. config overrides (AIConfig.toml etc.)
Copy-Item (Join-Path $custom 'config_overrides/*') $dst -Recurse -Force

# 4b. own plugins (DisplayFix.asi, built by custom_src/DisplayFix/build.ps1)
Copy-Item (Join-Path $custom 'plugins/*.asi') $dst -Force

# 4c. files for the game folder (e.g. "FlatOut ohne Modpack starten.bat"), batch files with CRLF
Get-ChildItem (Join-Path $custom 'root_files') -File | ForEach-Object {
    $target = Join-Path $dst $_.Name
    if ($_.Extension -eq '.bat') {
        $text = [IO.File]::ReadAllText($_.FullName) -replace "`r?`n", "`r`n"
        [IO.File]::WriteAllText($target, $text, [Text.Encoding]::ASCII)
    } else { Copy-Item $_.FullName $target }
}

# 5. patch list without dropped archives (no trailing newline, like the original)
$patch = Get-Content (Join-Path $src 'patch') | Where-Object { $_ -and ($dropped -notcontains $_) }
[IO.File]::WriteAllText((Join-Path $dst 'patch'), ($patch -join "`n"))

Remove-Item $work -Recurse -Force
Write-Host "Done: $dst"
Write-Host ("Dropped archives: " + ($(if ($dropped.Count) { $dropped -join ', ' } else { 'none' })))
