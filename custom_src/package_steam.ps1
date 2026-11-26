# Packages the existing Steam build. Run build_steam.ps1 first when rebuilding game data.
#Requires -Version 7.3
[CmdletBinding()]
param(
    [ValidatePattern('^[0-9][0-9A-Za-z._-]*$')][string]$CodeVersion,
    [switch]$CodeOnly,
    [string]$OutputDir = (Join-Path $PSScriptRoot '../.work/steam-release')
)

$ErrorActionPreference = 'Stop'
$mod = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../FlatOutUCChloeCollection_1.83_custom_steam'))
$out = [IO.Path]::GetFullPath($OutputDir)
if (-not (Test-Path -LiteralPath (Join-Path $mod 'ChloeSteamNet.asi') -PathType Leaf)) { throw 'Build the Steam package first.' }
$version = (Get-Content -Raw (Join-Path $PSScriptRoot '../VERSION')).Trim()
if (-not $CodeVersion) { $CodeVersion = $version }
if ($CodeVersion -ne $version) { throw "CodeVersion $CodeVersion differs from VERSION $version." }
$builtVersion = Join-Path $mod 'ChloeCustom_version.txt'
if (-not (Test-Path -LiteralPath $builtVersion) -or (Get-Content -Raw $builtVersion).Trim() -ne $version) {
    throw "The Steam build is not version $version. Run build_steam.ps1 again."
}
$changelog = Join-Path $PSScriptRoot '../CHANGELOG.md'
if (-not (Select-String -LiteralPath $changelog -Pattern ('^## \[' + [regex]::Escape($version) + '\]') -Quiet)) {
    throw "CHANGELOG.md has no entry for $version."
}
if ($out -eq $mod -or $out.StartsWith($mod + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Choose an output directory outside the mod build.' }
[void][IO.Directory]::CreateDirectory($out)

$code = [ordered]@{}
$assets = [ordered]@{}
$files = Get-ChildItem -LiteralPath $mod -File -Recurse -Force | Sort-Object FullName
foreach ($file in $files) {
    $path = [IO.Path]::GetRelativePath($mod, $file.FullName).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $file.FullName).Hash
    if ($path -match '^(Data|Ghosts|SteamAssets)/' -or
        $path -match '^foucpack_(gcp_(cars|carskins|maps|mapimages)|ryan_carpanels|twobei_portraits)\.bfs$') {
        $assets.Add($path, $hash)
    } else {
        if ($path -notmatch '^(Config/|[^/]+$)' -or $path -match '^(Fouc\.exe|.*\.(sav|log|dmp))$') {
            throw "Unexpected file in the Steam build: $path"
        }
        $code.Add($path, $hash)
    }
}
foreach ($archive in Get-Content -LiteralPath (Join-Path $mod 'patch') | Where-Object { $_.Trim() }) {
    if (-not ($code.Contains($archive) -or $assets.Contains($archive))) { throw "Missing archive in patch: $archive" }
}
if (!$assets.Count) { throw 'The Steam build contains no assets.' }
$assetIndex = ($assets.Keys | ForEach-Object { "$_=$($assets[$_])" }) -join "`n"
$assetId = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($assetIndex))).Substring(0, 16).ToLowerInvariant()
$assetName = "FlatOutUC-Steam-Assets-$assetId.zip"
$codeName = "FlatOutUC-Steam-Code-$CodeVersion.zip"
$assetPath = Join-Path $out $assetName
$codePath = Join-Path $out $codeName

function Write-ReleaseZip([string]$Destination, $Inventory, [hashtable]$ExtraFiles, [string]$Metadata) {
    if (Test-Path -LiteralPath $Destination) { throw "Release file already exists: $Destination" }
    $temporary = "$Destination.$([guid]::NewGuid().ToString('N')).tmp"
    try {
        $zip = [IO.Compression.ZipFile]::Open($temporary, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($path in $Inventory.Keys) {
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $mod $path), "Mod/$path", 'Optimal')
            }
            foreach ($path in ($ExtraFiles.Keys | Sort-Object)) {
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $ExtraFiles[$path], $path, 'Optimal')
            }
            if ($Metadata) {
                $writer = [IO.StreamWriter]::new($zip.CreateEntry('release.json').Open(), [Text.Encoding]::UTF8)
                try { $writer.Write($Metadata) } finally { $writer.Dispose() }
            }
        } finally { $zip.Dispose() }
        if ((Get-Item -LiteralPath $temporary).Length -ge 2GB) { throw "$Destination exceeds GitHub's 2 GiB per-file limit." }
        [IO.File]::Move($temporary, $Destination)
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}

if (Test-Path -LiteralPath $codePath) { throw "Use a new code version or output directory: $codePath" }
if (-not $CodeOnly -and -not (Test-Path -LiteralPath $assetPath)) {
    Write-ReleaseZip $assetPath $assets @{} ''
}
if (Test-Path -LiteralPath $assetPath) {
    $zip = [IO.Compression.ZipFile]::OpenRead($assetPath)
    try {
        if ($zip.Entries.Count -ne $assets.Count -or (Get-Item -LiteralPath $assetPath).Length -ge 2GB) {
            throw "Unexpected asset ZIP inventory or size: $assetPath"
        }
        foreach ($path in $assets.Keys) {
            $entry = $zip.GetEntry("Mod/$path")
            if (-not $entry) { throw "Missing asset in $assetName`: $path" }
            $stream = $entry.Open()
            try { $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)) }
            finally { $stream.Dispose() }
            if ($hash -ne $assets[$path]) { throw "Asset ZIP hash mismatch: $path" }
        }
    } finally { $zip.Dispose() }
}

$metadata = @{ Format = 1; CodeVersion = $CodeVersion; AssetZip = $assetName; Code = $code; Assets = $assets } | ConvertTo-Json -Depth 4
Write-ReleaseZip $codePath $code @{
    'Install-Steam.ps1' = Join-Path $PSScriptRoot 'steam_release/Install-Steam.ps1'
    'Install or Update.bat' = Join-Path $PSScriptRoot 'steam_release/Install or Update.bat'
    'README-Steam.txt' = Join-Path $PSScriptRoot 'steam_release/README-Steam.txt'
    'LICENSE' = Join-Path $PSScriptRoot '../LICENSE'
    'THIRD-PARTY-NOTICES.md' = Join-Path $PSScriptRoot '../THIRD-PARTY-NOTICES.md'
    'CHANGELOG.md' = $changelog
    'LICENSE_UltimateASILoader.txt' = Join-Path $PSScriptRoot 'DisplayFix/package/LICENSE_UltimateASILoader.txt'
    'Setup/install.ps1' = Join-Path $PSScriptRoot 'install.ps1'
    'Setup/uninstall.ps1' = Join-Path $PSScriptRoot 'uninstall.ps1'
    'Setup/Transaction.ps1' = Join-Path $PSScriptRoot 'tools/flatout_game_test/Transaction.ps1'
    'Setup/default_settings/Savegame/options.cfg' = Join-Path $PSScriptRoot 'default_settings/Savegame/options.cfg'
} $metadata

if (-not $CodeOnly) { Get-Item -LiteralPath $assetPath | Select-Object Name,Length,FullName }
Get-Item -LiteralPath $codePath | Select-Object Name,Length,FullName
Write-Host "Required assets: $assetName"
