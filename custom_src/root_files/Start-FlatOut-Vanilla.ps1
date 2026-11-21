#Requires -Version 5.1
param([switch]$RestoreOnly)

$ErrorActionPreference = 'Stop'
$game = $PSScriptRoot
$exe = Join-Path $game 'Fouc.exe'
$marker = Join-Path $game '_ChloeCustom_vanilla.json'
$save = Join-Path $game 'Savegame'
$modSave = Join-Path $game 'Savegame_modpack'
$vanillaSave = Join-Path $game 'Savegame_vanilla'

function Restore-VanillaFile([string]$Path) {
    $parked = "$Path.modpack_off"
    if (Test-Path -LiteralPath $parked) {
        if (Test-Path -LiteralPath $Path) { throw "Both $Path and $parked exist. No file was overwritten." }
        Move-Item -LiteralPath $parked -Destination $Path
    }
}

function Restore-Modpack {
    $state = $null
    if (Test-Path -LiteralPath $marker) { $state = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json }
    # The former batch launcher used the same parked names without a marker.
    if ((Test-Path -LiteralPath $modSave) -or ($state -and -not $state.HadModSave)) {
        if (Test-Path -LiteralPath $save) {
            if (Test-Path -LiteralPath $vanillaSave) { throw 'Both vanilla save folders exist. No save was overwritten.' }
            Move-Item -LiteralPath $save -Destination $vanillaSave
        }
        if (Test-Path -LiteralPath $modSave) { Move-Item -LiteralPath $modSave -Destination $save }
    }
    Restore-VanillaFile (Join-Path $game 'patch')
    Restore-VanillaFile (Join-Path $game 'ChloeSteamNet.asi')
    foreach ($file in Get-ChildItem -LiteralPath $game -File -Filter '*_gcp.asi.modpack_off') {
        Restore-VanillaFile ($file.FullName -replace '\.modpack_off$', '')
    }
    if ($state) { Remove-Item -LiteralPath $marker }
}

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Place the launcher in the FlatOut game folder.' }
if (Get-Process -Name Fouc -ErrorAction SilentlyContinue) { throw 'Close FlatOut first.' }
if (Test-Path -LiteralPath (Join-Path $game '_ChloeCustom_code_update')) { throw 'Finish or restore the pending modpack update first.' }

# Prevent two launchers from switching files or starting the exe concurrently.
$lock = [Threading.Mutex]::new($false, 'Local\FlatOut-Chloe-Vanilla')
$locked = $false
try {
    try { $locked = $lock.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (-not $locked) { throw 'A vanilla launcher is already running.' }
    $recovering = (Test-Path -LiteralPath $marker) -or (Test-Path -LiteralPath (Join-Path $game 'patch.modpack_off'))
    Restore-Modpack
    if ($RestoreOnly -or $recovering) { Write-Host 'The modpack is active again.'; return }
    $patch = Join-Path $game 'patch'
    if (-not (Test-Path -LiteralPath $patch -PathType Leaf)) { throw 'Modpack patch file missing.' }
    if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) { throw 'Start Steam before launching FlatOut.' }
    $plugins = @(Get-ChildItem -LiteralPath $game -File -Filter '*_gcp.asi' | ForEach-Object FullName)
    $steamPlugin = Join-Path $game 'ChloeSteamNet.asi'
    if (Test-Path -LiteralPath $steamPlugin -PathType Leaf) { $plugins += $steamPlugin }

    $hadModSave = Test-Path -LiteralPath $save -PathType Container
    [IO.File]::WriteAllText($marker, (@{ HadModSave = $hadModSave } | ConvertTo-Json -Compress))
    try {
        foreach ($path in @($patch) + $plugins) { Move-Item -LiteralPath $path -Destination "$path.modpack_off" }
        if ($hadModSave) { Move-Item -LiteralPath $save -Destination $modSave }
        if (Test-Path -LiteralPath $vanillaSave) { Move-Item -LiteralPath $vanillaSave -Destination $save }
        Write-Host 'Starting FlatOut in vanilla mode. Keep this window open until the game closes.'
        Start-Process -FilePath $exe -WorkingDirectory $game -Wait
    } finally {
        Restore-Modpack
    }
    Write-Host 'The modpack is active again.'
} finally {
    if ($locked) { $lock.ReleaseMutex() }
    $lock.Dispose()
}
