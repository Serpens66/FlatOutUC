#Requires -Version 7.3
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Transaction.ps1')
Add-Type -Path (Join-Path $PSScriptRoot 'HiddenDesktop.cs')

$base = Join-Path ([IO.Path]::GetTempPath()) ("FlatOutRunnerFixture-" + [guid]::NewGuid().ToString('N'))
$game = Join-Path $base 'game'
$runDir = Join-Path $base 'run'
$files = @('patch', 'FlatOutGameTest.bfs')
$directories = @('Savegame')
try {
    [void][IO.Directory]::CreateDirectory((Join-Path $game 'Savegame'))
    Set-Content -LiteralPath (Join-Path $game 'patch') -Value 'original'
    Set-Content -LiteralPath (Join-Path $game 'Savegame/options.cfg') -Value 'original'
    Save-FlatOutTestState $game $runDir $files $directories
    Set-Content -LiteralPath (Join-Path $game 'patch') -Value 'temporary'
    Set-Content -LiteralPath (Join-Path $game 'FlatOutGameTest.bfs') -Value 'temporary'
    Remove-Item -LiteralPath (Join-Path $game 'Savegame/options.cfg')
    Set-Content -LiteralPath (Join-Path $game 'Savegame/test.sav') -Value 'temporary'
    Restore-FlatOutTestState $game $runDir $files $directories
    if ((Get-Content -LiteralPath (Join-Path $game 'patch')) -ne 'original' -or
        (Get-Content -LiteralPath (Join-Path $game 'Savegame/options.cfg')) -ne 'original' -or
        (Test-Path -LiteralPath (Join-Path $game 'FlatOutGameTest.bfs')) -or
        (Test-Path -LiteralPath (Join-Path $game 'Savegame/test.sav'))) {
        throw 'Interrupted run was not restored.'
    }

    $desktop = "FlatOutFixture-$PID"
    $missingProcess = $null
    $startFailed = $false
    try { $missingProcess = [FlatOutDesktop]::new($desktop, (Join-Path $game 'Missing.exe'), $game) }
    catch { $startFailed = $_.Exception.InnerException -is [ComponentModel.Win32Exception] }
    finally { if ($missingProcess) { $missingProcess.Dispose() } }
    if (-not $startFailed) { throw 'A missing executable did not report a Win32 start failure.' }

    # The fixture stays suspended until the timeout expires, so it cannot show a window or run any commands.
    $fixture = [FlatOutDesktop]::new($desktop, $env:ComSpec, $game)
    $child = Get-Process -Id $fixture.Pid
    try {
        if ($fixture.WaitForExit(150)) { throw 'Expected a timeout for the suspended fixture.' }
    } finally { $fixture.Dispose() }
    if (-not $child.WaitForExit(1000)) { throw 'The timed-out fixture did not exit.' }
    Write-Host 'PASS interrupted restore, missing start and timeout cleanup.'
} finally {
    $resolved = [IO.Path]::GetFullPath($base)
    $parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ((Split-Path $resolved -Parent) -eq $parent -and (Split-Path $resolved -Leaf) -match '^FlatOutRunnerFixture-[0-9a-f]{32}$' -and
        (Test-Path -LiteralPath $resolved -PathType Container)) {
        Remove-Item -LiteralPath $resolved -Recurse
    }
}
