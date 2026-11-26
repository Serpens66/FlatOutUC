# Builds ChloeSteamNet.asi (32-bit DLL) with the Visual Studio 2022 Build Tools and stores it in custom_src/plugins_steam,
# from where build_steam.ps1 copies it into the Steam package (the only supported package).
#   pwsh -File custom_src/ChloeSteamNet/build.ps1
param(
    [string]$OutputDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'plugins_steam')
)

$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio Build Tools with the C++ x86 compiler not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$src = Join-Path $PSScriptRoot 'ChloeSteamNet.cpp'
$obj = Join-Path $env:TEMP "ChloeSteamNet_build_$PID"
New-Item -ItemType Directory -Force $obj | Out-Null
$outDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $outDir | Out-Null
$out = Join-Path $outDir 'ChloeSteamNet.asi'
cmd /c "`"$vcvars`" >nul && cl /nologo /O2 /MT /LD /W4 /EHsc /GS- /std:c++17 `"$src`" /Fo`"$obj\\`" /Fe`"$out`" /link /DLL /NOLOGO ws2_32.lib user32.lib kernel32.lib /IMPLIB:`"$obj\ChloeSteamNet.lib`""
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Remove-Item (Join-Path $outDir 'ChloeSteamNet.exp') -ErrorAction SilentlyContinue
Write-Host "Built $out"
