#Requires -Version 7.3
[CmdletBinding(DefaultParameterSetName = 'Run')]
param(
    [Parameter(ParameterSetName = 'Run')]
    [ValidateSet('Smoke', 'SinglePlayer', 'SteamHost', 'All', 'Skins', 'Modes', 'LocalModes')][string]$Scenario = 'Smoke',
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 10000)][int]$SkinFrom = 1,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 10000)][int]$SkinThrough = 10000,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 100)][int]$ModeFrom = 1,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 100)][int]$ModeThrough = 100,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 8)][int]$ProfileSlot = 1,
    [Parameter(Mandatory, ParameterSetName = 'Restore')]
    [ValidatePattern('^[0-9a-f]{12}$')][string]$RestoreRun,
    [string]$GameDir = 'E:\ProgrammeE\Steam\steamapps\common\FlatOut Ultimate Carnage CE'
)

$ErrorActionPreference = 'Stop'
$support = Join-Path $PSScriptRoot 'flatout_game_test'
. (Join-Path $support 'Transaction.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runRoot = Join-Path $repo '.work/t/flatout'
$GameDir = [IO.Path]::GetFullPath($GameDir).TrimEnd('\')
$files = @('patch', 'FlatOutGameTest.bfs', 'zz_FlatOutGameTest.asi', 'FlatOutGameTest.attach', 'ChloeSteamNet.log', 'Fouc_d3d9.log')
$directories = @('Savegame', 'Profiles', 'Ghosts')
$run = if ($RestoreRun) { $RestoreRun } else { [guid]::NewGuid().ToString('N').Substring(0, 12) }
$work = Join-Path $runRoot $run

function Write-ProgressLine([string]$Message) {
    $line = '{0:yyyy-MM-dd HH:mm:ss.fff zzz} {1}' -f [DateTimeOffset]::Now, $Message
    Add-Content -LiteralPath (Join-Path $work 'runner.log') -Value $line
    Write-Host $line
}

function Assert-GameIdle {
    if (Get-Process -Name Fouc -ErrorAction SilentlyContinue) {
        throw 'BLOCKED: FlatOut is already running.'
    }
}

function Invoke-Bfs([string[]]$Arguments) {
    & dotnet (Join-Path $work 'bfs/Bfs.dll') @Arguments
    if ($LASTEXITCODE -ne 0) { throw "BfsTool exited with code $LASTEXITCODE." }
}

function New-TestBfs {
    $project = Join-Path $work 'Bfs.csproj'
    @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <OutputType>Exe</OutputType>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>annotations</Nullable>
    <NuGetAudit>false</NuGetAudit>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
  <ItemGroup><Compile Include="../../../../tools/BfsTool/BfsTool.cs" /></ItemGroup>
</Project>
'@ | Set-Content -LiteralPath $project
    & dotnet build $project --disable-build-servers -o (Join-Path $work 'bfs') "-p:RestoreSources=$work"
    if ($LASTEXITCODE -ne 0) { throw 'BfsTool build failed.' }

    $resolved = Join-Path $work 'resolved'
    $overlay = Join-Path $work 'overlay'
    $scripts = Join-Path $overlay 'data/scripts'
    [void][IO.Directory]::CreateDirectory($scripts)
    $archives = @(Get-Content -LiteralPath (Join-Path $GameDir 'filesystem')) +
        @(Get-Content -LiteralPath (Join-Path $GameDir 'patch'))
    $wanted = @('main.bed', 'startscreen.bed', 'singleracemenu.bed', 'chloemultiplayer.bed', 'chloemultiplayer_lobby.bed')
    if ($Scenario -eq 'LocalModes') { $wanted += 'arcadecareermenu.bed' }
    foreach ($archive in $archives) {
        $archive = $archive.Trim()
        if (-not $archive) { continue }
        $source = Get-TestPath $GameDir $archive
        foreach ($script in $wanted) {
            Invoke-Bfs @('extract', $source, $resolved, "data/scripts/$script")
        }
    }
    $encoding = [Text.Encoding]::Latin1
    $extras = @{
        'main.bed' = @"
FlatOutTest_Run = "$run"
FlatOutTest_Scenario = "$Scenario"
FlatOutTest_Profile = $ProfileSlot
FlatOutTest_SkinFrom = $SkinFrom
FlatOutTest_SkinThrough = $SkinThrough
FlatOutTest_ModeFrom = $ModeFrom
FlatOutTest_ModeThrough = $ModeThrough
dofile("data/scripts/flatout_test_scenario.bed")
"@
        'singleracemenu.bed' = @'
function FlatOutTest_StartSingleRace(level, car, skin, derby)
    RaceSelectionState.Level = level
    RaceSelectionState.Laps = 1
    RaceSelectionState.Weather = 1
    RaceSelectionState.IsFO2Track = false
    if derby then RaceSelectionState.DerbyType = derby end
    carselected(car, skin)
end
'@
        'arcadecareermenu.bed' = @'
function FlatOutTest_StartCarnage(row, item)
    SetFocusedItem(row, item)
    RaceArcadeCareer(row, item)
end
'@
        'chloemultiplayer.bed' = 'FlatOutTest_OpenBrowser = ChloeSteamBrowser_Open'
        'chloemultiplayer_lobby.bed' = @'
FlatOutTest_StartHostRace = StartRace
FlatOutTest_ChangeHostCar = ChangeCar
'@
    }
    foreach ($script in $wanted) {
        $source = Join-Path $resolved "data/scripts/$script"
        $body = [IO.File]::ReadAllText($source, $encoding)
        if ($script -eq 'main.bed') {
            $anchor = '[EVENT_MENU_MENUENTERED] = function(event)'
            if ([regex]::Matches($body, [regex]::Escape($anchor)).Count -ne 1) {
                throw 'Unsupported main menu event handler.'
            }
            $body = $body.Replace($anchor, $anchor + "`n    ChloeSteam_Log('flatout-test-menu:$run' .. ':' .. tostring(event.data))")
            $loaded = '[EVENT_MENU_LOADED] = function(event)'
            $body = $body.Replace($loaded, $loaded + "`n    ChloeSteam_Log('flatout-test-menu:$run' .. ':loaded result=' .. tostring(db.GameFlow.PostRace.Result))")
            $body += "`n" + $extras[$script] + "`n"
        } elseif ($script -eq 'startscreen.bed') {
            $anchor = '(?m)^(\s*)GUI:SetHelpButtons\(\)'
            if ([regex]::Matches($body, $anchor).Count -ne 1 -or $body -notmatch 'function input\.onKeyPressed') {
                throw 'Unsupported start-screen script.'
            }
            $body = $body -replace $anchor, '$1FlatOutTest_StartFromTitle = function() input.onKeyPressed(input, nil, KeyCodes.BUTTON_START, nil) end$1GUI:SetHelpButtons()'
        } else {
            $body += "`n" + $extras[$script] + "`n"
        }
        [IO.File]::WriteAllText((Join-Path $scripts $script), $body, $encoding)
    }
    $scenarioFile = Join-Path $scripts 'flatout_test_scenario.bed'
    Copy-Item -LiteralPath (Join-Path $support 'Scenario.bed') -Destination $scenarioFile
    $extension = @{ Skins = 'SkinSweep.bed'; Modes = 'ModeSweep.bed'; LocalModes = 'LocalModeSweep.bed' }[$Scenario]
    if ($extension) {
        [IO.File]::AppendAllText($scenarioFile, "`n" + [IO.File]::ReadAllText((Join-Path $support $extension)))
    }
    $lua = 'D:\CDesktopLink\Portable\lua\lua5_0_3_Win64_bin\luac50.exe'
    if (Test-Path -LiteralPath $lua) {
        if (Get-Item -LiteralPath $lua -Stream Zone.Identifier -ErrorAction SilentlyContinue) {
            Write-ProgressLine 'Skipping optional luac50 precheck: Windows marks the executable as downloaded.'
        } else {
            & $lua -p $scenarioFile
            if ($LASTEXITCODE -ne 0) { throw 'Lua 5.0 rejected the test scenario.' }
        }
    }
    Invoke-Bfs @('pack', $overlay, (Join-Path $work 'FlatOutGameTest.bfs'))
    Invoke-Bfs @('verify', (Join-Path $work 'FlatOutGameTest.bfs'))
}

function New-TestProbe {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'BLOCKED: MSVC x86 build tools are missing.' }
    # Keep cmd.exe input static. Path values are quoted environment variables.
    $env:FOUC_TEST_VCVARS = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
    $env:FOUC_TEST_PROBE_SOURCE = Join-Path $support 'Probe.cpp'
    $env:FOUC_TEST_BUILD = $work
    $batch = Join-Path $work 'build-probe.cmd'
    @'
@echo off
call "%FOUC_TEST_VCVARS%" >nul
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /LD /W4 /std:c++17 "%FOUC_TEST_PROBE_SOURCE%" /Fo"%FOUC_TEST_BUILD%\Probe.obj" /Fe"%FOUC_TEST_BUILD%\zz_FlatOutGameTest.asi" /link /IMPLIB:"%FOUC_TEST_BUILD%\Probe.lib"
exit /b %errorlevel%
'@ | Set-Content -LiteralPath $batch -Encoding ascii
    & $env:ComSpec /d /c ('"{0}"' -f $batch)
    if ($LASTEXITCODE -ne 0) { throw 'FlatOut test probe build failed.' }
}

function Get-CloudRoot([string]$SteamDir) {
    # Steam's appinfo record uses the AppID followed by its payload length.
    # Find the matching app record and validate the repeated AutoCloud
    # root/path tuples and their filename masks before using the mapping.
    $cache = [IO.File]::ReadAllBytes((Join-Path $SteamDir 'appcache/appinfo.vdf'))
    $view = [Text.Encoding]::Latin1.GetString($cache)
    $id = [Text.Encoding]::Latin1.GetString([BitConverter]::GetBytes([uint32]12360))
    $tuple = 'WinAppDataLocal\x00[\s\S]{5}FlatOut Ultimate Carnage/Savegame\x00[\s\S]{5}'
    $mappings = @()
    $position = $view.IndexOf($id, [StringComparison]::Ordinal)
    while ($position -ge 0 -and $position + 8 -lt $cache.Length) {
        $size = [BitConverter]::ToUInt32($cache, $position + 4)
        if ($size -ge 512 -and $size -le 65536 -and $position + 8 + $size -le $cache.Length) {
            $record = $view.Substring($position + 8, $size)
            if ($record.Contains('FlatOut')) {
                $mappings += @([regex]::Matches($record, $tuple + '([^\x00]+)\x00') | ForEach-Object { $_.Groups[1].Value })
            }
        }
        $position = $view.IndexOf($id, $position + 1, [StringComparison]::Ordinal)
    }
    $expectedMasks = @('*.dat', 'gfwl_id.txt', 'gfwl_username.txt', 'player_*.sav')
    if ((@($mappings | Sort-Object -Unique) -join '|') -ne ($expectedMasks -join '|')) {
        throw "BLOCKED: Cannot verify FlatOut's Steam AutoCloud mapping."
    }
    $cloud = [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'FlatOut Ultimate Carnage'))
    if ($cloud -eq $GameDir -or $GameDir.StartsWith($cloud + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'BLOCKED: The installation overlaps the Steam Cloud path.'
    }
    return $cloud
}

function Get-ProbeSamples {
    if (-not (Test-Path -LiteralPath $probeLog)) { return }
    Get-Content -LiteralPath $probeLog -Tail 50 | ForEach-Object {
        if ($_ -match '^\d+(?: -?\d+){16}$') {
            ,([int[]]($_ -split ' '))
        }
    }
}

function Get-RaceProof($Samples, [int]$ExpectedAi, [int]$Car) {
    $matching = @($Samples | Where-Object {
        $_[1] -eq 2 -and $_[2] -eq 8 -and $_[3] -ge 0 -and $_[4] -eq 19 -and $_[7] -eq 1 -and
        $_[9] -eq 0 -and $_[8] -eq $ExpectedAi -and
        ($Car -eq -1 -or ($_[5] -eq $Car -and $_[6] -eq 6))
    })
    if ($matching.Count -ge 2 -and $matching[-1][3] - $matching[0][3] -ge 2000) { return ,$matching[-1] }
}

function Invoke-TestProcess {
    $initialDesktop = [FlatOutDesktop]::InputDesktop()
    if ($initialDesktop -ne 'Default') { throw "BLOCKED: Current input desktop is $initialDesktop." }
    $env:FOUC_TEST_PROBE_LOG = $probeLog
    $env:FOUC_TEST_LEAVE_EVENT = "Local\FlatOut-Test-Leave-$run"
    $env:FOUC_TEST_FRAME_EVENT = "Local\FlatOut-Test-Frame-$run"
    $leaveRace = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::AutoReset, $env:FOUC_TEST_LEAVE_EVENT)
    $captureFrame = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::AutoReset, $env:FOUC_TEST_FRAME_EVENT)
    $instance = $null
    try {
        Assert-GameIdle
        $instance = [FlatOutDesktop]::new("FlatOut-Test-$run", (Join-Path $GameDir 'Fouc.exe'), $GameDir)
        Write-ProgressLine "Starting installed FlatOut on the inactive desktop (PID $($instance.Pid))."
        $instance.Resume()
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $window = $false
        $singlePassed = $false
        $steamPassed = $false
        $steamStartFrame = $null
        $markers = @()
        $lobby = $null
        $nextStatus = 15
        $lastStep = ''
        $stepTime = 0
        $capturedStall = $false
        $budget = if ($Scenario -in 'Skins', 'Modes', 'LocalModes') { 1800 } else { 150 }
        $modeProofs = [Collections.Generic.List[int]]::new()
        $modeFirstFrame = 0
        while (-not $instance.HasExited) {
            if ($watch.Elapsed.TotalSeconds -ge $budget) { throw "Timeout after $budget s. Last step: $lastStep." }
            if ([FlatOutDesktop]::InputDesktop() -ne $initialDesktop -or [FlatOutDesktop]::ForegroundPid() -eq $instance.Pid) {
                throw 'The input desktop or game foreground ownership changed.'
            }
            if (Get-Process -Name Fouc -ErrorAction SilentlyContinue | Where-Object Id -ne $instance.Pid) {
                throw 'Another FlatOut process appeared during the test.'
            }
            $dialog = $instance.GameDialog()
            if ($null -ne $dialog) { throw "FlatOut opened a Windows dialog on the test desktop: $dialog" }
            if (-not $window -and $instance.HasGameWindow()) {
                $window = $true
                Write-ProgressLine 'Found the game window on the inactive desktop.'
            }
            if (Test-Path -LiteralPath $steamLog) {
                $log = Get-Content -LiteralPath $steamLog -Raw
                $markers = @([regex]::Matches($log, "flatout-test:${run}:([^\r\n]+)") | ForEach-Object { $_.Groups[1].Value })
                if ($markers.Count -and $markers[-1] -ne $lastStep) {
                    $lastStep = $markers[-1]
                    $stepTime = $watch.Elapsed.TotalSeconds
                    $capturedStall = $false
                    if ('main-menu' -in $markers -and $Scenario -eq 'Smoke') { [void]$captureFrame.Set() }
                }
                if ($log -match 'Steam lobby (\d+) created') { $lobby = $Matches[1] }
                if ($markers -match '^ERROR ') { throw ($markers -match '^ERROR ' | Select-Object -Last 1) }
                if ($lobby -and $log -match 'accepted Steam session from') { throw 'A remote Steam participant connected.' }
                if ($Scenario -ne 'Skins') { $samples = @(Get-ProbeSamples) }
                if ($Scenario -in 'Modes', 'LocalModes' -and $lastStep -match '^mode begin (\d+) (\S+) (\d+) (\d+) (\d+) (\d+) (\d+) (-?\d+) (\d+)$') {
                    $step = [int]$Matches[1]
                    $label = $Matches[2]
                    $track = [int]$Matches[3]
                    $rules = [int]$Matches[4]
                    $derby = [int]$Matches[5]
                    $minutes = [int]$Matches[6]
                    $position = [int]$Matches[7]
                    $expectedAi = [int]$Matches[8]
                    $alternateRules = [int]$Matches[9]
                    if ($step -notin $modeProofs) {
                        $matching = @($samples | Where-Object {
                            $_[0] -gt $modeFirstFrame -and $_[1] -eq 2 -and $_[2] -eq 8 -and
                            $_[3] -ge 0 -and $_[4] -eq $track -and $_[7] -eq 1 -and ($expectedAi -lt 0 -or $_[8] -eq $expectedAi) -and
                            $_[9] -eq 0 -and $_[10] -in ($rules, $alternateRules) -and $_[11] -eq $derby -and
                            (-not $minutes -or $_[12] -eq $minutes * 60000) -and
                            (-not $position -or ($_[13] -eq $position -and $_[14] -eq $position -and $_[15] -ge 1 -and $_[16] -ge $_[15]))
                        })
                        if ($matching.Count -ge 2 -and $matching[-1][3] - $matching[0][3] -ge 1700) {
                            $modeFirstFrame = $matching[-1][0]
                            $modeProofs.Add($step)
                            Write-ProgressLine "Mode $step $label passed live checks. Sample: $($matching[-1] -join ' ')"
                            [void]$captureFrame.Set()
                            [void]$leaveRace.Set()
                        }
                    }
                }
                $singleStart = $markers -match '^single-start (\d+) 6$' | Select-Object -Last 1
                if (-not $singlePassed -and $singleStart -match '^single-start (\d+) 6$') {
                    $proof = Get-RaceProof $samples 2 ([int]$Matches[1])
                    if ($proof) {
                        $singlePassed = $true
                        Write-ProgressLine "Single-player simulation advanced, track $($proof[4]), skin $($proof[6]), $($proof[8]) AI."
                        [void]$captureFrame.Set()
                        [void]$leaveRace.Set()
                    }
                }
                if (-not $steamPassed -and $lobby -and 'steam-start' -in $markers -and $log -match 'lobby search:') {
                    if ($null -eq $steamStartFrame -and $samples.Count) { $steamStartFrame = $samples[-1][0] }
                    $proof = Get-RaceProof @($samples | Where-Object { $_[0] -gt $steamStartFrame }) 3 -1
                    if ($proof) {
                        $steamPassed = $true
                        Write-ProgressLine "Steam-host simulation advanced with $($proof[8]) AI."
                        [void]$captureFrame.Set()
                        [void]$leaveRace.Set()
                    }
                }
            }
            if (-not $capturedStall -and $watch.Elapsed.TotalSeconds - $stepTime -ge 15) {
                [void]$captureFrame.Set()
                $capturedStall = $true
            }
            if ($watch.Elapsed.TotalSeconds - $stepTime -ge 40) {
                $latest = @(Get-ProbeSamples) | Select-Object -Last 1
                throw "No progress for 40 s. Last step: $lastStep. Latest sample: $latest"
            }
            if ($watch.Elapsed.TotalSeconds -ge $nextStatus) {
                Write-ProgressLine "Waiting for FlatOut. Last step: $($markers | Select-Object -Last 1)"
                $nextStatus += 15
            }
            Start-Sleep -Milliseconds 100
        }
        if ($instance.ExitCode -ne 0) { throw "FlatOut exited with code $($instance.ExitCode). Last step: $lastStep." }
        if (-not $window -or @(Get-ProbeSamples).Count -lt 2) { throw 'No verified game window and render progress.' }
        $log = Get-Content -LiteralPath $steamLog -Raw
        $required = @('main-menu', 'DONE')
        if ($Scenario -in 'SinglePlayer', 'All') {
            $required += @('options-open', 'career-open', 'carnage-open', 'single-return')
            if (-not $singlePassed) { throw 'Single-player race proof is missing.' }
        }
        if ($Scenario -in 'SteamHost', 'All') {
            $required += @('steam-browser-ready', 'steam-return', 'steam-disconnected')
            if (-not $steamPassed -or $log -notmatch "server closed, Steam lobby $lobby left") {
                throw 'Steam race or lobby shutdown proof is missing.'
            }
        }
        if ($Scenario -eq 'Skins') {
            $required += @('steam-browser-ready', 'steam-disconnected')
            $prefix = "flatout-test:${run}:"
            $range = [regex]::Match($log, $prefix + 'skin range (\d+) (\d+) (\d+) (\d+)')
            $completed = @([regex]::Matches($log, $prefix + 'skin ok (\d+) ') | ForEach-Object { [int]$_.Groups[1].Value })
            if (-not $range.Success -or
                ($completed -join ',') -ne (([int]$range.Groups[1].Value..[int]$range.Groups[2].Value) -join ',') -or
                -not $lobby -or $log -notmatch "server closed, Steam lobby $lobby left") {
                throw 'Skin coverage or Steam lobby shutdown proof is missing.'
            }
            Write-ProgressLine "Rendered $($completed.Count) of $($range.Groups[3].Value) selectable skins across $($range.Groups[4].Value) car/variant entries."
        }
        if ($Scenario -in 'Modes', 'LocalModes') {
            if ($Scenario -eq 'Modes') {
                $required += @('steam-browser-ready', 'steam-disconnected')
                if (-not $lobby -or $log -notmatch "server closed, Steam lobby $lobby left") {
                    throw 'Steam lobby shutdown proof is missing.'
                }
            }
            $range = [regex]::Match($log, "flatout-test:${run}:mode range (\d+) (\d+)")
            $returns = @([regex]::Matches($log, "flatout-test:${run}:mode return (\d+)") | ForEach-Object { [int]$_.Groups[1].Value })
            if (-not $range.Success) { throw 'The multiplayer modes plan is missing.' }
            $planned = ([int]$range.Groups[1].Value..[int]$range.Groups[2].Value) -join ','
            if (($modeProofs -join ',') -ne $planned -or ($returns -join ',') -ne $planned) {
                throw 'Mode coverage or menu return proof is missing.'
            }
        }
        foreach ($marker in $required) {
            if ($log -notmatch "flatout-test:${run}:$marker\r?\n") { throw "Missing in-game result: $marker" }
        }
    } finally {
        $captureFrame.Dispose()
        $leaveRace.Dispose()
        if ($instance) { $instance.Dispose() }
    }
}

$status = 'FAIL'
$message = ''
$mutex = [Threading.Mutex]::new($false, 'Local\FlatOut-Installed-Game-Test')
$locked = $false
$staged = $false
$cloud = $null
try {
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (-not $locked) { throw 'BLOCKED: Another FlatOut test owns the installation.' }
    Assert-GameIdle
    if ($RestoreRun) {
        Restore-FlatOutTestState $GameDir $work $files $directories
        $status = 'PASS'
        $message = 'Restored interrupted run and verified hashes.'
    } else {
        $pending = Get-ChildItem -LiteralPath $runRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'rollback.json') }
        if ($pending) { throw "BLOCKED: Restore pending run $($pending[0].Name) first." }
        $steam = Get-Process -Name steam -ErrorAction SilentlyContinue |
            Where-Object SessionId -eq ([Diagnostics.Process]::GetCurrentProcess().SessionId) | Select-Object -First 1
        if (-not $steam -or -not $steam.Path) { throw 'BLOCKED: Steam is not visible in this Windows session.' }
        $exeHash = (Get-FileHash -LiteralPath (Join-Path $GameDir 'Fouc.exe')).Hash
        if ($exeHash -ne 'FAF3DFB0F6D408963A30B8059FEDE248BD63900F4E2AEEE7B54CEC6A9D9643E9') {
            throw "BLOCKED: Unsupported Fouc.exe hash: $exeHash"
        }
        if ((Get-Content -LiteralPath (Join-Path $GameDir 'steam_appid.txt') -Raw).Trim() -ne '12360') {
            throw 'BLOCKED: The installed game has no matching Steam App ID.'
        }
        foreach ($name in @('Savegame/options.cfg', 'Savegame/device.cfg', ('Savegame/customsave{0:000}.sav' -f $ProfileSlot), 'ChloeSteamNet.asi')) {
            if (-not (Test-Path -LiteralPath (Get-TestPath $GameDir $name) -PathType Leaf)) { throw "BLOCKED: Missing $name." }
        }
        foreach ($name in @('FlatOutGameTest.bfs', 'zz_FlatOutGameTest.asi', 'fullscreen')) {
            if (Test-Path -LiteralPath (Get-TestPath $GameDir $name)) { throw "BLOCKED: Existing $name in the installation." }
        }
        $cloud = Get-CloudRoot (Split-Path $steam.Path -Parent)
        $cloudBefore = Get-TestInventory $cloud @() @('Savegame')
        [void][IO.Directory]::CreateDirectory($work)
        Write-ProgressLine "Preparing $Scenario with existing profile slot $ProfileSlot. Cloud files are outside the game installation."
        Add-Type -Path (Join-Path $support 'HiddenDesktop.cs')
        try { $desktopName = [FlatOutDesktop]::InputDesktop() }
        catch { throw "BLOCKED: Cannot verify the input desktop in this Windows session. $($_.Exception.Message)" }
        if ($desktopName -ne 'Default') { throw "BLOCKED: The input desktop is $desktopName." }
        New-TestBfs
        New-TestProbe
        Assert-GameIdle
        Save-FlatOutTestState $GameDir $work $files $directories
        $staged = $true
        $steamLog = Join-Path $GameDir 'ChloeSteamNet.log'
        $probeLog = Join-Path $work 'probe.log'
        $options = Join-Path $GameDir 'Savegame/options.cfg'
        $encoding = [Text.Encoding]::Latin1
        $config = [IO.File]::ReadAllText($options, $encoding)
        $testOptions = @{
            'Game.LoadingSkip' = 1
            'Audio.InterfaceMusicVolume' = 0
            'Audio.InterfaceSfxVolume' = 0
            'Audio.IngameMusicVolume' = 0
            'Audio.IngameSfxVolume' = 0
            'Audio.RallyMusicVolume' = 0
            'Audio.PacenoteVolume' = 0
            'Network.VoiceOutputVolume' = 0
        }
        foreach ($key in $testOptions.Keys) {
            $pattern = '(?m)^(Settings\.' + [regex]::Escape($key) + '[ \t]*=[ \t]*)[0-9]+'
            if ([regex]::Matches($config, $pattern).Count -ne 1) { throw "Cannot set $key in options.cfg." }
            $config = $config -replace $pattern, ('${1}' + $testOptions[$key])
        }
        [IO.File]::WriteAllText($options, $config, $encoding)
        foreach ($name in @('FlatOutGameTest.bfs', 'zz_FlatOutGameTest.asi')) {
            Copy-Item -LiteralPath (Join-Path $work $name) -Destination (Get-TestPath $GameDir $name)
        }
        [IO.File]::AppendAllText((Join-Path $GameDir 'patch'), "`nFlatOutGameTest.bfs")
        Invoke-TestProcess
        $status = 'PASS'
        $message = 'Scenario and its in-game assertions passed.'
    }
} catch {
    $message = $_.Exception.Message
    if ($message.StartsWith('BLOCKED:')) { $status = 'BLOCKED' }
} finally {
    if ($staged) {
        try {
            Assert-GameIdle
            foreach ($name in @('ChloeSteamNet.log', 'Fouc_d3d9.log', 'FlatOutGameTest.attach')) {
                $path = Join-Path $GameDir $name
                if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $work $name) }
            }
            Restore-FlatOutTestState $GameDir $work $files $directories
            Write-ProgressLine 'Restored the installation and verified its hashes.'
        } catch {
            $status = 'FAIL'
            $message += " Restore failed: $($_.Exception.Message) Run ID $run."
        }
        try {
            if (-not (Test-SameInventory $cloudBefore (Get-TestInventory $cloud @() @('Savegame')))) {
                throw 'The external Steam Cloud save inventory changed during the run.'
            }
        } catch {
            $status = 'FAIL'
            $message += " Cloud check: $($_.Exception.Message)"
        }
    }
    if (Test-Path -LiteralPath $work -PathType Container) {
        @{ result = $status; run = $run; scenario = $Scenario; message = $message } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'result.json') -Encoding utf8
        Write-ProgressLine "$status $message"
    } else {
        Write-Host "$status $message"
    }
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
if ($status -eq 'BLOCKED') { exit 2 }
if ($status -ne 'PASS') { exit 1 }
