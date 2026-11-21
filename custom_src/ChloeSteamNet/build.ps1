# Builds ChloeSteamNet.asi (32-bit DLL) with the Visual Studio 2022 Build Tools and stores it in custom_src/plugins_steam,
# from where build_steam.ps1 copies it into the Steam package (not into the GFWL package).
#   pwsh -File custom_src/ChloeSteamNet/build.ps1
param(
    [string]$OutputDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'plugins_steam'),
    [switch]$StuntVideoPreview,
    [string]$BfsToolDll,
    [string]$OpusSourceDir,
    [string]$OpusLibrary
)

$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio Build Tools with the C++ x86 compiler not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$src = Join-Path $PSScriptRoot 'ChloeSteamNet.cpp'
$obj = Join-Path $env:TEMP 'ChloeSteamNet_build'
New-Item -ItemType Directory -Force $obj | Out-Null
$outDir = [IO.Path]::GetFullPath($OutputDir)
if ($StuntVideoPreview) {
    $workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    if (-not $outDir.StartsWith((Join-Path $workspace '.work') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'StuntVideoPreview requires an output directory under .work.'
    }
}
New-Item -ItemType Directory -Force $outDir | Out-Null
$out = Join-Path $outDir 'ChloeSteamNet.asi'
if ([bool]$OpusSourceDir -ne [bool]$OpusLibrary) { throw 'Supply both OpusSourceDir and the x86 static OpusLibrary.' }
$audioCompile = ''
$audioLink = ''
if ($OpusSourceDir) {
    $OpusSourceDir = (Resolve-Path -LiteralPath $OpusSourceDir).Path
    $opusInclude = Join-Path $OpusSourceDir 'include'
    $audioCompile = "/DSTUNT_STREAM_OPUS /I`"$opusInclude`""
    $audioLink = "`"$((Resolve-Path -LiteralPath $OpusLibrary).Path)`" winmm.lib"
}
cmd /c "`"$vcvars`" >nul && cl /nologo /O2 /MT /LD /W4 /EHsc /GS- /std:c++17 $audioCompile `"$src`" /Fo`"$obj\\`" /Fe`"$out`" /link /DLL /NOLOGO ws2_32.lib user32.lib kernel32.lib gdi32.lib ole32.lib xinput.lib mfplat.lib mfuuid.lib wmcodecdspuuid.lib strmiids.lib $audioLink /IMPLIB:`"$obj\ChloeSteamNet.lib`""
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Remove-Item (Join-Path $outDir 'ChloeSteamNet.exp') -ErrorAction SilentlyContinue
Write-Host "Built $out"
if ($OpusSourceDir) { Copy-Item -LiteralPath (Join-Path $OpusSourceDir 'COPYING') -Destination (Join-Path $outDir 'Opus-LICENSE.txt') -Force }
if ($StuntVideoPreview) {
    $overlay = Join-Path $obj 'stunt-video/data/scripts'
    New-Item -ItemType Directory -Force $overlay, (Join-Path $outDir 'Config') | Out-Null
    $lobby = Join-Path (Split-Path -Parent $PSScriptRoot) 'multiplayer_overrides/data/scripts/chloemultiplayer_lobby.bed'
    Copy-Item -LiteralPath $lobby -Destination $overlay -Force
    $archive = Join-Path $outDir 'foucpack_chloecustom_stunt_video.bfs'
    function Invoke-PreviewBfs {
        if ($BfsToolDll) { & dotnet (Resolve-Path -LiteralPath $BfsToolDll).Path @args }
        else { & dotnet run --file (Join-Path $workspace 'tools/BfsTool/BfsTool.cs') -- @args }
    }
    Invoke-PreviewBfs pack (Join-Path $obj 'stunt-video') $archive
    if ($LASTEXITCODE -ne 0) { throw 'Building the preview BFS failed.' }
    Invoke-PreviewBfs verify $archive
    if ($LASTEXITCODE -ne 0) { throw 'The preview BFS failed verification.' }
    Set-Content -LiteralPath (Join-Path $outDir 'Config/StuntVideoPreview.enabled') -Value 'Local opt-in for Steam stunt video acceptance.'
}
