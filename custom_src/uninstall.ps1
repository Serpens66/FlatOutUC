# Removes everything install.ps1 copied into the game folder and restores overwritten game files.
#   pwsh -File custom_src/uninstall.ps1 [-GameDir <path>] [-RemoveUserData] [-WhatIf]
# -RemoveUserData also deletes what the mod creates while playing (savegames, profiles, ghosts, logs).
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$GameDir = 'E:\ProgrammeE\Steam\steamapps\common\FlatOut Ultimate Carnage CE',
    [switch]$RemoveUserData
)
$ErrorActionPreference = 'Stop'

if (Get-Process -Name 'Fouc', 'launcher' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($GameDir, 'OrdinalIgnoreCase') }) {
    throw 'FlatOut UC is running - close the game first (files are locked).'
}
$manifest  = Join-Path $GameDir 'ChloeCustom_install_manifest.txt'
$backupDir = Join-Path $GameDir '_ChloeCustom_backup'
if (-not (Test-Path $manifest)) { throw "No install manifest in $GameDir - nothing installed by install.ps1" }

$files = [IO.File]::ReadAllLines($manifest) | Where-Object { $_ }
foreach ($rel in $files) {
    $target = Join-Path $GameDir $rel
    if ((Test-Path $target) -and $PSCmdlet.ShouldProcess($target, 'delete')) { Remove-Item $target -Force }
    $bak = Join-Path $backupDir $rel
    if ((Test-Path $bak) -and $PSCmdlet.ShouldProcess($target, 'restore original')) { Move-Item $bak $target }
}

if ($RemoveUserData) {
    foreach ($p in 'Savegame', 'Profiles', 'Ghosts', 'Fouc_d3d9.log') {
        $t = Join-Path $GameDir $p
        if ((Test-Path $t) -and $PSCmdlet.ShouldProcess($t, 'delete user data')) { Remove-Item $t -Recurse -Force }
    }
}

# remove folders of mod files that are empty now (deepest first), never the game folder itself
$dirs = $files + @(Join-Path '_ChloeCustom_backup' 'x') | ForEach-Object {
    $d = Split-Path $_
    while ($d) { $d; $d = Split-Path $d }
} | Sort-Object -Unique | Sort-Object { $_.Length } -Descending
foreach ($d in $dirs) {
    $full = Join-Path $GameDir $d
    if ((Test-Path $full) -and -not (Get-ChildItem $full -Force) -and $PSCmdlet.ShouldProcess($full, 'remove empty folder')) { Remove-Item $full }
}
if ($PSCmdlet.ShouldProcess($manifest, 'delete manifest')) { Remove-Item $manifest -Force }
Write-Host "Removed $($files.Count) mod files from $GameDir"
