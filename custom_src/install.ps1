# Installs FlatOutUCChloeCollection_1.83_custom_steam into the Steam game folder and records every file it writes,
# so uninstall.ps1 can remove the mod again without touching the game files.
#   pwsh -File custom_src/install.ps1 [-GameDir <path>] [-WhatIf]
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$GameDir = 'E:\ProgrammeE\Steam\steamapps\common\FlatOut Ultimate Carnage CE',
    [string]$ModDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'FlatOutUCChloeCollection_1.83_custom_steam')
)
$ErrorActionPreference = 'Stop'

$modDir    = [IO.Path]::GetFullPath($ModDir).TrimEnd('\', '/')
$manifest  = Join-Path $GameDir 'ChloeCustom_install_manifest.txt'
$backupDir = Join-Path $GameDir '_ChloeCustom_backup'   # game files the mod overwrites (restored on uninstall)

if (-not (Test-Path (Join-Path $GameDir 'Fouc.exe'))) { throw "Fouc.exe not found in $GameDir" }
if (Get-Process -Name 'Fouc', 'launcher' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($GameDir, 'OrdinalIgnoreCase') }) {
    throw 'FlatOut UC is running - close the game first (files are locked).'
}
$exeSize = (Get-Item (Join-Path $GameDir 'Fouc.exe')).Length
if ($exeSize -ne 4242504) {
    throw "Fouc.exe has $exeSize bytes, expected 4242504 (current Steam version, branch public)."
}
if (-not (Test-Path (Join-Path $modDir 'patch'))) { throw "Build missing: run custom_src/build_steam.ps1 first" }

# an older install of this script is removed first
if (Test-Path $manifest) {
    Write-Host 'Previous install found, uninstalling it first'
    & (Join-Path $PSScriptRoot 'uninstall.ps1') -GameDir $GameDir -WhatIf:$WhatIfPreference
}

# refuse to mix with a mod install that was not done by this script (e.g. an old Chloe Collection)
$foreign = Get-ChildItem $GameDir -File -Filter '*_gcp.asi' -ErrorAction SilentlyContinue
if ($foreign -and -not (Test-Path $manifest)) {
    throw "Game folder already contains mod files ($($foreign[0].Name)) not installed by this script. Reinstall the game cleanly first."
}

$written = [System.Collections.Generic.List[string]]::new()
$files = Get-ChildItem $modDir -Recurse -File
$i = 0
foreach ($f in $files) {
    $i++
    $rel = $f.FullName.Substring($modDir.Length + 1)
    $target = Join-Path $GameDir $rel
    Write-Progress -Activity 'Installing Chloe Collection (custom)' -Status $rel -PercentComplete (100 * $i / $files.Count)
    if ($PSCmdlet.ShouldProcess($target, 'copy')) {
        if (Test-Path $target) {
            $bak = Join-Path $backupDir $rel
            New-Item -ItemType Directory -Force (Split-Path $bak) | Out-Null
            Move-Item $target $bak
        }
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item $f.FullName $target
    }
    $written.Add($rel)
}
Write-Progress -Activity 'Installing Chloe Collection (custom)' -Completed

if ($PSCmdlet.ShouldProcess($manifest, 'write manifest')) {
    [IO.File]::WriteAllLines($manifest, $written)
}
Write-Host "Installed $($written.Count) files into $GameDir"

# default settings for a new player (FO2 handling, automatic transmission, borderless window); never overwrites
# existing settings and is not part of the manifest (user data, kept on uninstall like the rest of Savegame/)
$defaults = Join-Path $PSScriptRoot 'default_settings/Savegame/options.cfg'
$options  = Join-Path $GameDir 'Savegame/options.cfg'
if (-not (Test-Path $options) -and $PSCmdlet.ShouldProcess($options, 'write default settings')) {
    New-Item -ItemType Directory -Force (Split-Path $options) | Out-Null
    Copy-Item $defaults $options
    Write-Host 'Wrote default settings (Savegame/options.cfg)'
}
