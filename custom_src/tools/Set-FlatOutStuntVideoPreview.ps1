#Requires -Version 7.3
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Install')][string]$Package,
    [Parameter(ParameterSetName = 'Install')][string]$HostTestBfs,
    [Parameter(Mandatory, ParameterSetName = 'Restore')][switch]$Restore,
    [string]$GameDir = '\\LENOVO_SERP\common\FlatOut Ultimate Carnage CE'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'flatout_game_test/Transaction.ps1')
$GameDir = [IO.Path]::GetFullPath($GameDir).TrimEnd('\')
$packageFiles = @('ChloeSteamNet.asi', 'foucpack_chloecustom_stunt_video.bfs', 'Config/StuntVideoPreview.enabled')
$files = @('patch') + $packageFiles
$exe = Get-TestPath $GameDir 'Fouc.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Steam installation unavailable at $GameDir" }
if ((Get-FileHash -LiteralPath $exe).Hash -ne 'FAF3DFB0F6D408963A30B8059FEDE248BD63900F4E2AEEE7B54CEC6A9D9643E9') {
    throw 'Unsupported game executable.'
}
# A running game holds its executable open. Refuse the change before staging any files.
try { $check = [IO.File]::Open($exe, 'Open', 'Read', 'None'); $check.Dispose() }
catch { throw "Cannot stage the preview while FlatOut is running or its executable is locked. $($_.Exception.Message)" }

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$targetKey = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($GameDir.ToUpperInvariant()))).Substring(0, 12)
$state = Join-Path $root ".work/t/stunt-video-client/$targetKey"
$patch = Get-TestPath $GameDir 'patch'
$rollback = Join-Path $state 'rollback.json'
if ($Restore) {
    $prior = Get-Content -LiteralPath $rollback -Raw | ConvertFrom-Json
    if ($prior.Files -contains 'FlatOutGameTest.bfs') { $files += 'FlatOutGameTest.bfs' }
    Restore-FlatOutTestState $GameDir $state $files @()
    return
}

if (Test-Path -LiteralPath $rollback) { throw 'A preview is already staged. Restore it before starting another preview.' }
$Package = (Resolve-Path -LiteralPath $Package).ProviderPath
foreach ($name in $packageFiles) {
    if (-not (Test-Path -LiteralPath (Get-TestPath $Package $name) -PathType Leaf)) { throw "Preview file missing: $name" }
}
if ($HostTestBfs) {
    $HostTestBfs = (Resolve-Path -LiteralPath $HostTestBfs).ProviderPath
    $files += 'FlatOutGameTest.bfs'
}
if ($packageFiles[1] -in (Get-Content -LiteralPath $patch).Trim()) {
    throw 'The installed patch already contains the preview.'
}
$previous = Join-Path $state 'restored.json'
if (Test-Path -LiteralPath $previous) { Remove-Item -LiteralPath $previous }
Save-FlatOutTestState $GameDir $state $files @()
try {
    foreach ($name in $packageFiles) {
        $source = Get-TestPath $Package $name
        $target = Get-TestPath $GameDir $name
        Copy-Item -LiteralPath $source -Destination $target -Force
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
            throw "Preview file differs after copy: $name"
        }
    }
    $archives = @($packageFiles[1])
    if ($HostTestBfs) {
        $clientBfs = Get-TestPath $GameDir 'FlatOutGameTest.bfs'
        Copy-Item -LiteralPath $HostTestBfs -Destination $clientBfs -Force
        if ((Get-FileHash -LiteralPath $HostTestBfs).Hash -ne (Get-FileHash -LiteralPath $clientBfs).Hash) {
            throw 'The shared host test archive differs after copy.'
        }
        $archives += 'FlatOutGameTest.bfs'
    }
    # FlatOut treats an empty last entry as another archive to open.
    [IO.File]::AppendAllText($patch, "`n" + ($archives -join "`n"))
} catch {
    Restore-FlatOutTestState $GameDir $state $files @()
    throw
}
Write-Host "Staged the video preview in $GameDir"
