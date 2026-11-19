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
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'FlatOutUCChloeCollection_1.83_custom'
$dst  = Join-Path $root 'FlatOutUCChloeCollection_1.83_custom_steam'
$hardlinkThreshold = 50MB   # same rule as build.ps1: large BFS are hardlinked, never edit them in place

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

if (Test-Path $dst) { Get-ChildItem $dst -Force | Remove-Item -Recurse -Force }
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
    [IO.File]::WriteAllBytes($asi.FullName, $data)
    Write-Host "Patched $($asi.Name)"
}

# 3. Steam-only overrides (vanilla fonts instead of ZOOM's high-res fonts), loaded last
$bfsTool = Join-Path $root 'tools/BfsTool/BfsTool.cs'
$overrideArchive = 'foucpack_chloecustom_steam.bfs'
$overridePath = Join-Path $dst $overrideArchive
& dotnet run $bfsTool -- pack (Join-Path $PSScriptRoot 'steam_overrides') $overridePath
if ($LASTEXITCODE -ne 0) { throw 'BfsTool pack failed' }
& dotnet run $bfsTool -- verify $overridePath
if ($LASTEXITCODE -ne 0) { throw 'BfsTool verify failed' }
$patchFile = Join-Path $dst 'patch'   # small file, copied (not hardlinked) in step 1
$patch = @(Get-Content $patchFile | Where-Object { $_ }) + $overrideArchive
[IO.File]::WriteAllText($patchFile, ($patch -join "`n"))   # no trailing newline, like the original

# 4. Steam-only plugins (ChloeSteamNet.asi)
$steamPlugins = Join-Path $PSScriptRoot 'plugins_steam'
if (-not (Test-Path (Join-Path $steamPlugins 'ChloeSteamNet.asi'))) { throw 'ChloeSteamNet.asi missing: run custom_src/ChloeSteamNet/build.ps1 first' }
Copy-Item (Join-Path $steamPlugins '*.asi') $dst -Force

# 5. Steam app id for starting outside the Steam library
[IO.File]::WriteAllText((Join-Path $dst 'steam_appid.txt'), '12360')

Write-Host "Done: $dst"
