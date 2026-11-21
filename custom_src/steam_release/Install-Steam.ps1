#Requires -Version 5.1
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    [string]$GameDir,
    [Parameter(ParameterSetName = 'Install')][switch]$InstallAssets,
    [Parameter(Mandatory, ParameterSetName = 'Restore')][switch]$RestoreUpdate
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Setup/Transaction.ps1')

function Find-SteamGameDirectory {
    $roots = @(
        (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
        (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath
        (Join-Path ${env:ProgramFiles(x86)} 'Steam')
    ) | Where-Object { $_ } | Select-Object -Unique
    $candidates = @($(foreach ($root in $roots) {
        $libraries = @($root)
        $config = Join-Path $root 'steamapps/libraryfolders.vdf'
        if (Test-Path -LiteralPath $config) {
            foreach ($entry in Select-String -LiteralPath $config -Pattern '^\s*"path"\s+"([^"]+)"') {
                $libraries += $entry.Matches[0].Groups[1].Value.Replace('\\', '\')
            }
        }
        foreach ($library in $libraries) {
            $game = Join-Path $library 'steamapps/common/FlatOut Ultimate Carnage CE'
            if ((Test-Path -LiteralPath (Join-Path $game 'Fouc.exe') -PathType Leaf) -and
                (Test-Path -LiteralPath (Join-Path $game 'zoom_platform.bfs') -PathType Leaf)) {
                [IO.Path]::GetFullPath($game)
            }
        }
    }) | Select-Object -Unique)
    if ($candidates.Count -eq 1) { return $candidates[0] }

    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    try {
        $dialog.Description = 'Select the Steam folder that contains FlatOut Ultimate Carnage CE (Fouc.exe).'
        $dialog.ShowNewFolderButton = $false
        if ($candidates) { $dialog.SelectedPath = $candidates[0] }
        if ($dialog.ShowDialog() -eq 'OK') { return $dialog.SelectedPath }
    } finally { $dialog.Dispose() }
}

if (-not $GameDir) { $GameDir = Find-SteamGameDirectory }
if (-not $GameDir) { Write-Host 'Installation cancelled.'; exit 0 }
$game = [IO.Path]::GetFullPath($GameDir).TrimEnd('\', '/')
$mod = Join-Path $PSScriptRoot 'Mod'
$manifestName = 'ChloeCustom_install_manifest.txt'
$manifest = Get-TestPath $game $manifestName
$recovery = Get-TestPath $game '_ChloeCustom_code_update'
$pending = Join-Path $recovery 'rollback.json'

if ($game -eq $PSScriptRoot -or $game.StartsWith($PSScriptRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $PSScriptRoot.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Extract the release ZIPs into a separate folder outside the game installation.'
}
if (-not (Test-Path -LiteralPath (Join-Path $game 'zoom_platform.bfs') -PathType Leaf) -or
    (Get-Item -LiteralPath (Join-Path $game 'Fouc.exe')).Length -ne 4242504) {
    throw 'Select the current FlatOut Ultimate Carnage Steam installation (public branch).'
}
if (Get-Process -Name Fouc, launcher -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and $_.Path.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)
}) { throw 'Close FlatOut before installing.' }
if (@('patch.modpack_off', '_ChloeCustom_vanilla.json', 'Savegame_modpack') | Where-Object { Test-Path -LiteralPath (Join-Path $game $_) }) {
    throw 'Vanilla mode needs to be restored first. Run the vanilla launcher in the game folder to restore the modpack.'
}

if ($RestoreUpdate) {
    if (Test-Path -LiteralPath $pending) {
        $state = Read-FlatOutJson $pending
        Restore-FlatOutTestState $game $recovery $state.Files @()
        Write-Host 'Restored the previous code files and verified their hashes.'
    }
    if (Test-Path -LiteralPath $recovery) { Remove-Item -LiteralPath $recovery -Recurse }
    return
}
if (Test-Path -LiteralPath $recovery) {
    throw "Pending code update in $recovery. Run this installer with -RestoreUpdate before updating again."
}

$release = Read-FlatOutJson (Join-Path $PSScriptRoot 'release.json')
if ($release.Format -ne 1 -or !$release.Code.Count -or !$release.Assets.Count) { throw 'Invalid release.json.' }
$codeFiles = @($release.Code.Keys)
$assetFiles = @($release.Assets.Keys)
$allFiles = @($codeFiles + $assetFiles | Sort-Object)
if (($allFiles | Sort-Object -Unique).Count -ne $allFiles.Count) { throw 'Duplicate release path.' }

function Assert-ReleaseFiles([string]$Root, $Inventory, [string]$Failure) {
    $actual = Get-TestInventory $Root @($Inventory.Keys) @()
    if (-not (Test-SameInventory $Inventory $actual)) {
        $bad = $Inventory.Keys | Where-Object { $actual[$_] -ne $Inventory[$_] } | Select-Object -First 1
        throw "$Failure ($bad)"
    }
}

Assert-ReleaseFiles $mod $release.Code 'Code files are missing or damaged. Extract the Code ZIP again'
if (-not $InstallAssets -and (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    $installedAssets = Get-TestInventory $game $assetFiles @()
    if (-not (Test-SameInventory $release.Assets $installedAssets)) {
        Assert-ReleaseFiles $mod $release.Assets "This code requires $($release.AssetZip). Extract both ZIPs into a fresh folder"
        $InstallAssets = $true
    }
}
if ($InstallAssets -or -not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    Assert-ReleaseFiles $mod $release.Assets "Extract $($release.AssetZip) alongside the Code ZIP"
    $extracted = Get-ChildItem -LiteralPath $mod -File -Recurse -Force | ForEach-Object {
        $_.FullName.Substring($mod.Length + 1).Replace('\', '/')
    }
    if (Compare-Object $allFiles @($extracted)) { throw 'Extract both ZIPs into a fresh folder. The current Mod folder contains extra files.' }
    & (Join-Path $PSScriptRoot 'Setup/install.ps1') -GameDir $game -ModDir $mod
    return
}

$owned = @([IO.File]::ReadAllLines($manifest) | Where-Object { $_ } | ForEach-Object { $_.Replace('\', '/') })
if (@($assetFiles | Where-Object { $_ -notin $owned }).Count -or 'ChloeSteamNet.asi' -notin $owned) {
    throw 'The installed file list does not match this Steam release. Install both ZIPs with -InstallAssets.'
}
$obsolete = @($owned | Where-Object { $_ -notin $allFiles })
foreach ($path in $obsolete) {
    if ($path -notmatch '^(Config/|[^/]+$)' -or $path -match '(^|/)\.{1,2}(/|$)|^(Fouc\.exe|.*\.(sav|log|dmp))$') {
        throw "Unexpected obsolete path: $path. Install both ZIPs with -InstallAssets."
    }
}
$current = Get-TestInventory $game $codeFiles @()
$changed = @($codeFiles | Where-Object { $current[$_] -ne $release.Code[$_] })
foreach ($path in $changed) {
    if ($path -notin $owned -and ((Test-Path -LiteralPath (Get-TestPath $game $path)) -or
        (Test-Path -LiteralPath (Get-TestPath $game "_ChloeCustom_backup/$path")))) {
        throw "Existing file is outside this installation's manifest: $path. Install both ZIPs with -InstallAssets."
    }
}
if (!$changed.Count -and !$obsolete.Count) { Write-Host "Code $($release.CodeVersion) is already installed."; return }

$originals = @($obsolete | Where-Object { Test-Path -LiteralPath (Get-TestPath $game "_ChloeCustom_backup/$_") })
$tracked = @($changed + $obsolete + $manifestName + @($originals | ForEach-Object { "_ChloeCustom_backup/$_" }))
New-Item -ItemType Directory -Path $recovery | Out-Null
try {
    Save-FlatOutTestState $game $recovery $tracked @()
    foreach ($path in $changed) {
        $target = Get-TestPath $game $path
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $target))
        Copy-Item -LiteralPath (Get-TestPath $mod $path) -Destination $target -Force
    }
    foreach ($path in $obsolete) {
        $target = Get-TestPath $game $path
        if ($path -in $originals) {
            $original = Get-TestPath $game "_ChloeCustom_backup/$path"
            [void][IO.Directory]::CreateDirectory((Split-Path -Parent $target))
            Copy-Item -LiteralPath $original -Destination $target -Force
            Remove-Item -LiteralPath $original
        } elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    }
    Assert-ReleaseFiles $game $release.Code 'Installed code did not match the release'
    [IO.File]::WriteAllLines($manifest, [string[]]($allFiles -replace '/', '\'))
    Move-Item -LiteralPath $pending -Destination (Join-Path $recovery 'committed.json')
} catch {
    if (Test-Path -LiteralPath $pending) { Restore-FlatOutTestState $game $recovery $tracked @() }
    throw
} finally {
    if (-not (Test-Path -LiteralPath $pending)) { Remove-Item -LiteralPath $recovery -Recurse }
}
Write-Host "Installed code $($release.CodeVersion) ($($changed.Count) changed files, $($obsolete.Count) removed)."
