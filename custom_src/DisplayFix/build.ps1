# Builds DisplayFix.asi (32-bit DLL) with the Visual Studio 2022 Build Tools and stores it in custom_src/plugins,
# from where build.ps1 copies it into the mod package.
#   pwsh -File custom_src/DisplayFix/build.ps1
$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio Build Tools with the C++ x86 compiler not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$src = Join-Path $PSScriptRoot 'DisplayFix.cpp'
$obj = Join-Path $env:TEMP 'DisplayFix_build'
New-Item -ItemType Directory -Force $obj | Out-Null
$outDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'plugins'
New-Item -ItemType Directory -Force $outDir | Out-Null
$out = Join-Path $outDir 'DisplayFix.asi'
cmd /c "`"$vcvars`" >nul && cl /nologo /O2 /MT /LD /W4 /GS- `"$src`" /Fo`"$obj\\`" /Fe`"$out`" /link /DLL /NOLOGO user32.lib kernel32.lib /IMPLIB:`"$obj\DisplayFix.lib`""
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Remove-Item (Join-Path $outDir 'DisplayFix.exp') -ErrorAction SilentlyContinue
Write-Host "Built $out"
