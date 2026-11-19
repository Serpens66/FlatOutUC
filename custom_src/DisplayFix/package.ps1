# Builds the standalone DisplayFix package (without the mod pack) into DisplayFix_package/ and DisplayFix_1.0.zip
# in the workspace root. Run custom_src/DisplayFix/build.ps1 first.
#   pwsh -File custom_src/DisplayFix/package.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$asi = Join-Path $root 'custom_src/plugins/DisplayFix.asi'
$loader = Join-Path $root 'FlatOutUCChloeCollection_1.83/xlive.dll'   # Ultimate ASI Loader 6.9.0 (Win32), MIT
if (-not (Test-Path $asi)) { throw 'DisplayFix.asi missing: run custom_src/DisplayFix/build.ps1 first' }
$ver = (Get-Item $loader).VersionInfo
if ($ver.FileDescription -ne 'Ultimate ASI Loader') { throw "Unexpected loader: $($ver.FileDescription)" }

$dst = Join-Path $root 'DisplayFix_package'
if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $dst 'source') | Out-Null
Copy-Item $asi $dst
Copy-Item $loader (Join-Path $dst 'winmm.dll')
Copy-Item (Join-Path $PSScriptRoot 'package/*') $dst
Copy-Item (Join-Path $PSScriptRoot 'DisplayFix.cpp'), (Join-Path $PSScriptRoot 'build.ps1') (Join-Path $dst 'source')
# text files with CRLF for Notepad
Get-ChildItem $dst -Recurse -Include *.txt, *.cpp, *.ps1 | ForEach-Object {
    $t = [IO.File]::ReadAllText($_.FullName) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText($_.FullName, $t, [Text.UTF8Encoding]::new($true))
}
$zip = Join-Path $root 'DisplayFix_1.0.zip'
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $dst '*') -DestinationPath $zip
Write-Host "Done: $dst and $zip (loader $($ver.ProductVersion))"
