# Builds FlatOutUCChloeCollection_1.83_custom_steam from FlatOutUCChloeCollection_1.83_custom (run build.ps1 first).
# This variant is for the current Steam version of FOUC (branch 'public', ZOOM Platform update with Steam multiplayer).
#   pwsh -File custom_src/build_steam.ps1
#
# Why this works: the new Steam Fouc.exe is byte-identical to the GFWL one, so all plugin addresses stay valid.
# Differences handled here:
#   1. The new version ships ZOOM's xlive.dll (GFWL -> Steam bridge). The mod's xlive.dll is only the
#      Ultimate ASI Loader, so it is renamed to winmm.dll and ZOOM's xlive.dll stays.
#      (dinput8.dll does not work: Fouc.exe does not load it at startup, so no plugin would be loaded.)
#   2. Every *_gcp.asi refuses to run on the ZOOM version (IsFOUCZOOMVersion in source/nya-common-fouc/fo2versioncheck.h:
#      file names zoom_platform.bfs / zoom_flatout.ini and the sizes of ZOOM's data.bfs / streamed.bfs).
#      These markers are patched so the check never matches. Each must occur exactly once per ASI, otherwise the build fails.
#   3. zoom_platform.bfs replaces the fonts with huge high-res DDS (~1 GB, e.g. tradegothic_lt_bugbear_37.dds = 164 MB).
#      Fouc.exe is not large-address-aware (2 GB), so together with the mod content an allocation fails and the game
#      crashes in DXVK (memcpy from NULL), e.g. when opening Carnage Mode. custom_src/steam_overrides (vanilla fonts)
#      is packed as foucpack_chloecustom_steam.bfs and appended to 'patch', so it overrides zoom_platform.bfs.
#      The same archive carries steam_overrides/data/scripts/chloemultiplayer.bed: Chloe's multiplayer menu with a
#      Steam game browser (JOIN GAME) instead of the IP entry.
#   4. ChloeSteamNet.asi (custom_src/ChloeSteamNet, built into custom_src/plugins_steam) sends Chloe's multiplayer
#      traffic over Steam (SteamNetworkingMessages, no port forwarding) and creates/lists the Steam lobbies.
#      (The earlier variant with the game's own Live/Steam multiplayer is kept in custom_src/steam_live_mp_unused.)
#   5. steam_appid.txt lets Fouc.exe start from a folder outside the Steam library (test copy).
param([string]$BfsToolDll, [string]$TestOutputDir, [string]$SteamPluginDir)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'FlatOutUCChloeCollection_1.83_custom'
$dst  = Join-Path $root 'FlatOutUCChloeCollection_1.83_custom_steam'
$original = Join-Path $root 'FlatOutUCChloeCollection_1.83'
$hardlinkThreshold = 50MB   # same rule as build.ps1: large BFS are hardlinked, never edit them in place
if ($TestOutputDir) {
    $dst = [IO.Path]::GetFullPath($TestOutputDir)
    $testRoot = (Join-Path $root '.work') + [IO.Path]::DirectorySeparatorChar
    if (-not $dst.StartsWith($testRoot, [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $dst)) {
        throw 'TestOutputDir must be a new directory under .work.'
    }
}

if (-not (Test-Path (Join-Path $src 'patch'))) { throw "Build missing: run custom_src/build.ps1 first" }

$ascii = [Text.Encoding]::ASCII
$asiPatches = @(
    @{ Name = 'zoom_platform.bfs';      Old = $ascii.GetBytes('zoom_platform.bfs'); New = $ascii.GetBytes('zoom_platform.bf_') }
    @{ Name = 'zoom_flatout.ini';       Old = $ascii.GetBytes('zoom_flatout.ini');  New = $ascii.GetBytes('zoom_flatout.in_') }
    @{ Name = 'data.bfs size (ZOOM)';     Old = [BitConverter]::GetBytes([uint32]1987070590); New = [byte[]](0xFF, 0xFF, 0xFF, 0xFF) }
    @{ Name = 'streamed.bfs size (ZOOM)'; Old = [BitConverter]::GetBytes([uint32]1836064668); New = [byte[]](0xFF, 0xFF, 0xFF, 0xFF) }
)

function Find-Bytes([byte[]]$data, [byte[]]$pattern) {
    $hits = [System.Collections.Generic.List[int]]::new()
    $first = $pattern[0]
    $i = [Array]::IndexOf($data, $first)
    while ($i -ge 0 -and $i -le $data.Length - $pattern.Length) {
        $match = $true
        for ($j = 1; $j -lt $pattern.Length; $j++) { if ($data[$i + $j] -ne $pattern[$j]) { $match = $false; break } }
        if ($match) { $hits.Add($i) }
        $i = [Array]::IndexOf($data, $first, $i + 1)
    }
    return , $hits
}

if (Test-Path $dst) {
    $resolvedDestination = (Resolve-Path -LiteralPath $dst).ProviderPath
    if ($resolvedDestination -ne $dst -or (Get-Item -LiteralPath $dst).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
        throw "Unexpected Steam build output directory: $resolvedDestination"
    }
    Get-ChildItem -LiteralPath $dst -Force | Remove-Item -Recurse -Force
}
else { New-Item -ItemType Directory $dst | Out-Null }

# 1. copy / hardlink the GFWL build
Get-ChildItem $src -Recurse -File | ForEach-Object {
    $rel = [IO.Path]::GetRelativePath($src, $_.FullName)
    if ($rel -eq 'xlive.dll') { $rel = 'winmm.dll' }   # Ultimate ASI Loader under a name that does not clash with ZOOM's xlive.dll
    $target = Join-Path $dst $rel
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    if ($_.Length -ge $hardlinkThreshold) { New-Item -ItemType HardLink -Path $target -Target $_.FullName | Out-Null }
    else { Copy-Item $_.FullName $target }
}

# 2. neutralise the ZOOM version check in every plugin
foreach ($asi in Get-ChildItem $dst -File -Filter '*_gcp.asi') {
    $data = [IO.File]::ReadAllBytes($asi.FullName)
    foreach ($p in $asiPatches) {
        $hits = Find-Bytes $data $p.Old
        if ($hits.Count -ne 1) { throw "$($asi.Name): expected exactly 1 occurrence of '$($p.Name)', found $($hits.Count)" }
        [Array]::Copy($p.New, 0, $data, $hits[0], $p.New.Length)
    }
    if ($asi.Name -eq 'FlatOutUCChloeCollection_gcp.asi') {
        # The two inlined SetDefaultPlayerSettings copies run before reading an existing custom save.
        # Reuse the regional-unit cmp for the new high-camera default and replace its later SETE with km/h.
        foreach ($register in 0x86, 0x83) {
            $prefix = if ($register -eq 0x86) { '833D14938400016689' } else { '833D1493840001B801010000' }
            $edits = @(
                @{ Match = $prefix; New = [byte[]](0xC6, $register, 0x20, 0x56, 0, 0, 1) }
                @{ Match = ('0F94{0:X2}18560000' -f $register); New = [byte[]](0xC6, $register, 0x18, 0x56, 0, 0, 0) }
            )
            foreach ($edit in $edits) {
                $hits = Find-Bytes $data ([Convert]::FromHexString($edit.Match))
                if ($hits.Count -ne 1) { throw "Unexpected Chloe profile defaults code: $($edit.Match)" }
                [Array]::Copy($edit.New, 0, $data, $hits[0], $edit.New.Length)
            }
        }
    }
    [IO.File]::WriteAllBytes($asi.FullName, $data)
    Write-Host "Patched $($asi.Name)"
}

# 3. Steam-only overrides, loaded last
$bfsTool = Join-Path $root 'tools/BfsTool/BfsTool.cs'
function Invoke-BfsTool([string[]]$Arguments) {
    if ($BfsToolDll) { & dotnet $BfsToolDll @Arguments }
    else { & dotnet run $bfsTool -- @Arguments }
    if ($LASTEXITCODE -ne 0) { throw "BfsTool failed: $($Arguments -join ' ')" }
}

$overrideArchive = 'foucpack_chloecustom_steam.bfs'
$overridePath = Join-Path $dst $overrideArchive
$sanJuanArchive = 'foucpack_chloecustom_sanjuan_derby.bfs'
$sanJuanPath = Join-Path $dst $sanJuanArchive
# Keep the entire Derby stage and shared DDS textures with the original storage flags.
$exclude = '^(?!data/tracks/sanjuan/(?:sanjuan1/d/|textures/[^/]+\.dds$))'
Invoke-BfsTool @('filter', (Join-Path $original 'foucpack_gcp_maps_extras.bfs'), $sanJuanPath, $exclude)
Invoke-BfsTool @('verify', $sanJuanPath)
$work = [IO.Directory]::CreateTempSubdirectory('fouc_steam_').FullName
try {
    $overlay = Join-Path $work 'overlay'
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'steam_overrides') -Destination $overlay -Recurse
    $levelsScript = 'data/scripts/levels.bed'
    Invoke-BfsTool @('extract', (Join-Path $src 'foucpack_gcp_misc.bfs'), $overlay, $levelsScript)
    $levelsPath = Join-Path $overlay $levelsScript
    $levels = [IO.File]::ReadAllText($levelsPath, [Text.Encoding]::Latin1)
    [IO.File]::WriteAllText($levelsPath, $levels + "`r`n-- San Juan Derby is bundled only in the Steam package.`r`nLevels[LevelIndex.SanJuan4].ChloeCustomHidden = nil`r`nLevels[LevelIndex.SanJuan4].MusicPlaylist = nil`r`n", [Text.Encoding]::Latin1)
    $music = Join-Path $work 'base-music'
    Invoke-BfsTool @('extract', (Join-Path $src 'foucpack_gcp_music.bfs'), $music, 'data/music/playlist_')
    $playlistDir = Join-Path $overlay 'data/music'
    [void][IO.Directory]::CreateDirectory($playlistDir)
    # Steam ships these base soundtrack recordings under Data/Songs.
    $steamSongs = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'steam_songs.txt')
    $names = ($steamSongs | ForEach-Object { [regex]::Escape($_) }) -join '|'
    $songPath = '(?i)(?<=["''])data/music/(' + $names + ')(?=["''])'
    $encoding = [Text.Encoding]::Latin1
    $profiles = Join-Path $work 'base-profiles'
    $profilesScript = 'data/scripts/profilesmenu.bed'
    Invoke-BfsTool @('extract', (Join-Path $src 'foucpack_gcp_misc.bfs'), $profiles, $profilesScript)
    $profileSource = [IO.File]::ReadAllText((Join-Path $profiles $profilesScript), $encoding)
    $profileSave = 'ProfileSave(new_profile.slot,entermenu("menu_mainmenu"),entermenu("menu_mainmenu"))'
    if ([regex]::Matches($profileSource, [regex]::Escape($profileSave)).Count -ne 1) {
        throw 'Unexpected profile creation callback in profilesmenu.bed'
    }
    $profileDefaults = @(
        'local function applyNewProfileDefaults()'
        "`tSettings.Game.ImperialUnits = 0"
        "`tSettings.Game.HighCarCam = 1"
        "`tSettings.Control.Transmission = 0"
        'end'
        'applyNewProfileDefaults()'
        'ProfileSave(new_profile.slot,function()'
        "`tapplyNewProfileDefaults()"
        "`tSettings.SaveSettings()"
        "`tentermenu(`"menu_mainmenu`")()"
        'end,entermenu("menu_mainmenu"))'
    ) -join "`r`n`t"
    [IO.File]::WriteAllText((Join-Path $overlay $profilesScript), $profileSource.Replace($profileSave, $profileDefaults), $encoding)
    $mp = Join-Path $work 'base-multiplayer'
    $mpArchive = Join-Path $src 'foucpack_gcp_multiplayer.bfs'
    $damageValues = @('0.0', '0.1', '0.2', '0.3', '0.4', '0.5', '0.6', '0.7', '0.8', '0.9', '1.0', '1.5', '2.0', '50.0')
    $damageLabels = $damageValues | ForEach-Object { 'L"{0}x"' -f ($_ -replace '^(0|2|50)\.0$', '$1') }
    $existingDescriptions = @{ '0.0' = 0; '0.5' = 50; '1.0' = 100; '1.5' = 150; '2.0' = 200 }
    $damageHelp = foreach ($value in $damageValues) {
        if ($existingDescriptions.ContainsKey($value)) { 'MULTIPLAYER_RACEDAMAGELEVEL_{0}_DESCRIPTION' -f $existingDescriptions[$value] }
        else { 'L"{0} damage dealt on collision"' -f ($damageLabels[[Array]::IndexOf($damageValues, $value)].Substring(2).TrimEnd('"')) }
    }
    foreach ($name in 'chloemultiplayer_lobby_changeoptions.bed', 'chloemultiplayer_lobby_sessiondetails.bed') {
        Invoke-BfsTool @('extract', $mpArchive, $mp, "data/scripts/$name")
        $file = Join-Path $mp "data/scripts/$name"
        $source = [IO.File]::ReadAllText($file, $encoding)
        $row = [regex]'(?m)(\t\tL"DAMAGE LEVEL",\r?\n)\s*\{[^\r\n]+\},\r?\n\s*\{[^\r\n]+\},\r?\n\s*3,\r?\n\s*true,\r?\n\s*nil,\r?\n\s*\{[^\r\n]+\}'
        $comparison = "if j == Value then"
        if ($row.Matches($source).Count -ne 1 -or [regex]::Matches($source, $comparison).Count -ne 1) {
            throw "Unexpected Damage Level implementation in $name"
        }
        $replacement = @(
            ('{' + ($damageLabels -join ', ') + '},')
            ('{' + ($damageValues -join ', ') + '},')
            '11,'
            'true,'
            'nil,'
            ('{' + ($damageHelp -join ', ') + '}')
        )
        $source = $row.Replace($source, { param($match) $match.Groups[1].Value + "`t`t" + ($replacement -join "`r`n`t`t") })
        $source = $source.Replace($comparison, 'if j == Value or (RowNr == DamageLevel and type(Value) == "number" and math.abs(j - Value) < 0.0001) then')
        [IO.File]::WriteAllText((Join-Path $overlay "data/scripts/$name"), $source, $encoding)
    }
    $updated = 0
    foreach ($playlist in Get-ChildItem -LiteralPath (Join-Path $music 'data/music') -File -Filter 'playlist_*.bed') {
        $before = [IO.File]::ReadAllText($playlist.FullName, $encoding)
        $rewrites = [regex]::Matches($before, $songPath).Count
        if ($rewrites) {
            $after = [regex]::Replace($before, $songPath, 'data/songs/$1')
            [IO.File]::WriteAllText((Join-Path $playlistDir $playlist.Name), $after, $encoding)
            $updated += $rewrites
        }
    }
    if (-not $updated) { throw 'No Steam soundtrack paths matched in the source playlists.' }
    Write-Host "Updated $updated Steam soundtrack paths"
    Invoke-BfsTool @('pack', $overlay, $overridePath)
    Invoke-BfsTool @('verify', $overridePath)
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force
}
$patchFile = Join-Path $dst 'patch'   # small file, copied (not hardlinked) in step 1
$patch = @(Get-Content $patchFile | Where-Object { $_ }) + $sanJuanArchive + $overrideArchive
[IO.File]::WriteAllText($patchFile, ($patch -join "`n"))   # no trailing newline, like the original

# 4. Steam-only plugins (ChloeSteamNet.asi)
$steamPlugins = if ($SteamPluginDir) { $SteamPluginDir } else { Join-Path $PSScriptRoot 'plugins_steam' }
if (-not (Test-Path (Join-Path $steamPlugins 'ChloeSteamNet.asi'))) { throw 'ChloeSteamNet.asi missing: run custom_src/ChloeSteamNet/build.ps1 first' }
Copy-Item (Join-Path $steamPlugins '*.asi') $dst -Force

# 5. Steam app id for starting outside the Steam library
[IO.File]::WriteAllText((Join-Path $dst 'steam_appid.txt'), '12360')

Write-Host "Done: $dst"
