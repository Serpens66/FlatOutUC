#Requires -Version 7.3
[CmdletBinding(DefaultParameterSetName = 'Run')]
param(
    [Parameter(ParameterSetName = 'Run')]
    [ValidateSet('Smoke', 'SinglePlayer', 'SteamHost', 'SteamPeer', 'All', 'Skins', 'Modes', 'LocalModes', 'Tournament')][string]$Scenario = 'Smoke',
    [Parameter(ParameterSetName = 'Run')]
    [ValidatePattern('^[A-Za-z0-9 _-]{1,23}$')][string]$SteamPeerName = 'Laptop',
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 10000)][int]$SkinFrom = 1,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 10000)][int]$SkinThrough = 10000,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 500)][int]$ModeFrom = 1,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 500)][int]$ModeThrough = 500,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$UseStarterFixtures,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 8)][int]$ProfileSlot = 1,
    [Parameter(ParameterSetName = 'Run')]
    [ValidatePattern('^[A-Za-z0-9 _-]{1,15}$')][string]$CreateProfile,
    [Parameter(ParameterSetName = 'Run')]
    [string]$ProfileFixture,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$VerifySteamMusic,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$MixedTournament,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$ForceTournamentTie,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$TrackSweep,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$FullCarnage,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$ParallelLaunch,
    [Parameter(ParameterSetName = 'Run')]
    [string]$LoopbackClientProfile,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$LoopbackRace,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(31, 36)][int]$LoopbackDerbyLevel,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(4, 7)][int]$LoopbackDerbyAI = 4,
    [Parameter(ParameterSetName = 'Run')]
    [ValidateSet('0', '0.1', '0.5', '1', '1.5', '2', '50')][string]$LoopbackDamage = '0.1',
    [Parameter(ParameterSetName = 'Run')]
    [ValidateSet('UC', 'FO2')][string]$DerbyHud,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$DerbyImpact,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$StuntPose,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$StuntGhost,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$StuntGhostProps,
    [Parameter(ParameterSetName = 'Run')]
    [switch]$StuntGhostAsset,
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
$profileFiles = @('player{0:000}.sav', 'customsave{0:000}.sav', 'customsave{0:000}.ach', 'customsave{0:000}.cht') |
    ForEach-Object { 'Savegame/' + ($_ -f $ProfileSlot) }

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
    if ($CreateProfile) { $wanted += 'profilesmenu.bed' }
    if ($Scenario -eq 'LocalModes') { $wanted += @('arcadecareermenu.bed', 'partymodemenu.bed', 'mainmenu.bed') }
    if ($LoopbackDerbyLevel) { $wanted += @('chloemultiplayer_lobby_changeoptions.bed', 'chloemultiplayer_lobby_sessiondetails.bed') }
    if ($LoopbackDerbyLevel) { $wanted += 'multiplayerracemenu.bed' }
    foreach ($archive in $archives) {
        $archive = $archive.Trim()
        if (-not $archive) { continue }
        $source = Get-TestPath $GameDir $archive
        foreach ($script in $wanted) {
            Invoke-Bfs @('extract', $source, $resolved, "data/scripts/$script")
        }
    }
    $encoding = [Text.Encoding]::Latin1
    $luaProfile = if ($LoopbackClientProfile) { "CommandLine['-flatouttestclient'] and 2 or $ProfileSlot" } else { "$ProfileSlot" }
    $extras = @{
        'chloemultiplayer_lobby_changeoptions.bed' = @'
function FlatOutTest_ChooseDamage(value)
    local options = OptionItems[DamageLevel][ItemValues]
    assert(table.getn(options) == 14 and options[11] == 1.0, "wrong Damage Level options")
    for i = 1, 11 do assert(math.abs(options[i] - (i - 1) / 10) < 0.0001, "missing Damage Level step " .. i) end
    SetSelectedItemByValue(DamageLevel, value)
    local chosen = GetSelectedItemValue(DamageLevel)
    assert(math.abs(chosen - value) < 0.0001)
    ChloeNet_Host_SetDamageMult(chosen)
end
'@
        'chloemultiplayer_lobby_sessiondetails.bed' = @'
function FlatOutTest_CheckDisplayedDamage(value)
    local options = OptionItems[DamageLevel][ItemValues]
    assert(table.getn(options) == 14 and options[11] == 1.0, "wrong session Damage Level options")
    for i = 1, 11 do assert(math.abs(options[i] - (i - 1) / 10) < 0.0001, "missing session Damage Level step " .. i) end
    SetSelectedItemByValue(DamageLevel, ChloeNet_GetDamageMult())
    assert(math.abs(GetSelectedItemValue(DamageLevel) - value) < 0.0001, "wrong displayed Damage Level")
end
'@
        'main.bed' = @"
FlatOutTest_Run = "$run"
FlatOutTest_Scenario = "$Scenario"
FlatOutTest_Profile = $luaProfile
FlatOutTest_CreateProfile = "$CreateProfile"
FlatOutTest_SkinFrom = $SkinFrom
FlatOutTest_SkinThrough = $SkinThrough
FlatOutTest_ModeFrom = $ModeFrom
FlatOutTest_ModeThrough = $ModeThrough
FlatOutTest_StarterFixtures = $($UseStarterFixtures.IsPresent.ToString().ToLowerInvariant())
FlatOutTest_MixedTournament = $($MixedTournament.IsPresent.ToString().ToLowerInvariant())
FlatOutTest_TrackSweep = $($TrackSweep.IsPresent.ToString().ToLowerInvariant())
FlatOutTest_FullCarnage = $($FullCarnage.IsPresent.ToString().ToLowerInvariant())
FlatOutTest_ParallelLaunch = $($ParallelLaunch.IsPresent.ToString().ToLowerInvariant())
FlatOutTest_LoopbackRace = $($LoopbackRace.IsPresent.ToString().ToLowerInvariant())
FlatOutTest_LoopbackDerbyLevel = $LoopbackDerbyLevel
FlatOutTest_LoopbackDerbyAI = $LoopbackDerbyAI
FlatOutTest_LoopbackDamage = $LoopbackDamage
FlatOutTest_SteamPeerName = "$SteamPeerName"
FlatOutTest_DerbyHud = "$DerbyHud"
FlatOutTest_StuntGhostProps = $($StuntGhostProps.IsPresent.ToString().ToLowerInvariant())
dofile("data/scripts/flatout_test_scenario.bed")
"@
        'singleracemenu.bed' = @'
function FlatOutTest_StartSingleRace(level, car, skin, derby, variant)
    if FlatOutTest_TrackSweep then
        local offered = false
        for _, track in ipairs(tracks) do
            if track.level_id == level then offered = true end
        end
        assert(offered, "track missing from the single-race selection: " .. level)
    end
    RaceSelectionState.Level = level
    RaceSelectionState.Laps = 1
    RaceSelectionState.Weather = 1
    RaceSelectionState.IsFO2Track = variant == true
    if derby then RaceSelectionState.DerbyType = derby end
    carselected(car, skin)
end
'@
        'profilesmenu.bed' = @'
function FlatOutTest_OpenProfileName(slot)
    new_profile.selectedButton = slot
    createNewProfile()
end
'@
        'arcadecareermenu.bed' = @'
function FlatOutTest_StartCarnage(row, item)
    SetFocusedItem(row, item)
    RaceArcadeCareer(row, item)
end
'@
        'partymodemenu.bed' = @'
FlatOutTest_SetPartyPlayers = SelectNumPlayers
function FlatOutTest_UsePartyCar(car)
    assert(IsStuntCar(car))
    PartyParams.car = car
    PartyParams.carName = L(GetCarData(car).Name)
end
'@
        'mainmenu.bed' = 'FlatOutTest_OpenCareer = check_career_garage'
        'chloemultiplayer.bed' = 'FlatOutTest_OpenBrowser = ChloeSteamBrowser_Open'
        'chloemultiplayer_lobby.bed' = @'
FlatOutTest_StartHostRace = StartRace
FlatOutTest_ChangeHostCar = ChangeCar
'@
    }
    foreach ($script in $wanted) {
        $source = Join-Path $resolved "data/scripts/$script"
        $body = [IO.File]::ReadAllText($source, $encoding)
        if ($LoopbackDerbyLevel -and $script -eq 'multiplayerracemenu.bed') {
            $anchor = 'end --//USE_NETWORK'
            if ([regex]::Matches($body, [regex]::Escape($anchor)).Count -ne 1) { throw 'Cannot attach to the multiplayer track list.' }
            $menuCheck = @'
function FlatOutTest_CheckDerbyTracks()
    SetSelectedItemByValue(Options, GameType, GAMETYPE_TOURNAMENT_DERBY)
    OnGameTypeChanged()
    SetSelectedItemByValue(Options, TrackType, TRACKTYPE_DERBY)
    OnTrackTypeChanged()
    assert(table.concat(ValidTracks, ",") == "31,32,33,34,35,36", "unexpected multiplayer Derby track list")
    FlatOutTest_CheckSingleDerbyTracks()
end
'@
            $body = $body.Replace($anchor, "$menuCheck`n$anchor")
        }
        if ($LoopbackDerbyLevel -and $script -eq 'singleracemenu.bed') {
            $body += @'

function FlatOutTest_CheckSingleDerbyTracks()
    local previous = RaceSelectionState
    RaceSelectionState = { Rules = GR_DERBY }
    local tracks = GetTracksByTypeUnlocked(TRACKTYPE_DERBY)
    RaceSelectionState = previous
    assert(table.getn(tracks) == 6, "unexpected single-player Derby track count")
    for index, entry in ipairs(tracks) do assert(entry.level_id == 30 + index) end
end
'@
        }
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
        } elseif ($script -eq 'chloemultiplayer_lobby_sessiondetails.bed') {
            $anchor = 'end --//USE_NETWORK'
            if ([regex]::Matches($body, [regex]::Escape($anchor)).Count -ne 1) { throw 'Unsupported session details script.' }
            $body = $body.Replace($anchor, $extras[$script] + "`n" + $anchor)
        } else {
            if ($script -eq 'profilesmenu.bed') {
                $anchor = 'local r=ChloeInput_GetInputText()'
                if ([regex]::Matches($body, [regex]::Escape($anchor)).Count -ne 1) {
                    throw 'Unsupported profile name callback.'
                }
                $body = $body.Replace($anchor, @'
local r
        if FlatOutTest_CreateProfile ~= "" and not GUI:IsMenuAnimating() then
            r = string.gsub(FlatOutTest_CreateProfile, ".", function(c) return c .. "\000" end) .. "\000\000"
            FlatOutTest_CreateProfile = ""
            ChloeInput_CloseInputWindow()
            ChloeSteam_Log("flatout-test:" .. FlatOutTest_Run .. ":submitted-profile-name")
        end
'@)
            }
            if ($script -eq 'partymodemenu.bed') {
                $anchor = 'local backfunc = function()'
                if ([regex]::Matches($body, [regex]::Escape($anchor)).Count -ne 1) {
                    throw 'Unsupported party setup callback.'
                }
                $body = $body.Replace($anchor, "FlatOutTest_StartParty = function(events) assert(PartyParams.NumPlayers == 2); okfunc(events) end`n`t$anchor")
            }
            $body += "`n" + $extras[$script] + "`n"
        }
        [IO.File]::WriteAllText((Join-Path $scripts $script), $body, $encoding)
    }
    $scenarioFile = Join-Path $scripts 'flatout_test_scenario.bed'
    $scenarioSource = [IO.File]::ReadAllText((Join-Path $support 'Scenario.bed'))
    if ($Scenario -eq 'SteamPeer') {
        # Fouc hashes the scripts it loads during main.bed. Both peers must load
        # the same file, although its test callbacks only run on the host.
        $scenarioSource = "if not CommandLine['-flatouttesthost'] then return end`n" + $scenarioSource
    }
    [IO.File]::WriteAllText($scenarioFile, $scenarioSource)
    $extension = @{
        Skins = 'SkinSweep.bed'; Modes = 'ModeSweep.bed'; LocalModes = 'LocalModeSweep.bed'; Tournament = 'Tournament.bed'; SteamPeer = 'SteamPeer.bed'
    }[$Scenario]
    if ($LoopbackClientProfile) { $extension = 'Loopback.bed' }
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
cl /nologo /O2 /MT /LD /W4 /EHsc /std:c++17 "%FOUC_TEST_PROBE_SOURCE%" /Fo"%FOUC_TEST_BUILD%\Probe.obj" /Fe"%FOUC_TEST_BUILD%\zz_FlatOutGameTest.asi" /link user32.lib gdi32.lib /IMPLIB:"%FOUC_TEST_BUILD%\Probe.lib"
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

function Get-ProbeSamples([string]$Path = $probeLog) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    try {
        $lines = Get-Content -LiteralPath $Path -Tail 50 -ErrorAction Stop
    } catch [IO.IOException] {
        if (($_.Exception.HResult -band 0xffff) -in 32, 33) { return }
        throw
    }
    $lines | ForEach-Object {
        if ($_ -match '^\d+(?: -?\d+){16}$') {
            ,([int[]]($_ -split ' '))
        }
    }
}

function Read-TestSteamLog([string]$Path) {
    try {
        $sharing = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
        $file = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, $sharing)
        try {
            $reader = [IO.StreamReader]::new($file)
            try { $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $file.Dispose() }
    } catch [IO.IOException] {
        if (($_.Exception.HResult -band 0xffff) -notin 32, 33) { throw }
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
    $env:FOUC_TEST_MUSIC = if ($VerifySteamMusic) { '1' } else { $null }
    $env:FOUC_TEST_STUNT_INPUT = if ($MixedTournament) { '1' } else { $null }
    $env:FOUC_TEST_TOURNAMENT_TIE = if ($ForceTournamentTie) { '1' } else { $null }
    $env:FOUC_TEST_DERBY_GRID = if ($LoopbackDerbyLevel -or $Scenario -eq 'SteamPeer') { '1' } else { $null }
    $env:FOUC_TEST_DERBY_HUD = if ($DerbyHud) { '1' } else { $null }
    $env:FOUC_TEST_DERBY_IMPACT = if ($DerbyImpact) { '1' } else { $null }
    $env:FOUC_TEST_STUNT_POSE = if ($StuntPose) { '1' } else { $null }
    $env:FOUC_TEST_STUNT_GHOST = if ($StuntGhost) { '1' } else { $null }
    $env:FOUC_TEST_STUNT_GHOST_PROPS = if ($StuntGhostProps) { '1' } else { $null }
    $env:FOUC_TEST_STUNT_GHOST_ASSET = if ($StuntGhostAsset) { '1' } else { $null }
    $leaveRace = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::AutoReset, $env:FOUC_TEST_LEAVE_EVENT)
    $clientLeaveName = $env:FOUC_TEST_LEAVE_EVENT + '-client'
    $clientLeaveRace = if ($LoopbackRace) {
        [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::AutoReset, $clientLeaveName)
    }
    $captureFrame = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::AutoReset, $env:FOUC_TEST_FRAME_EVENT)
    $instance = $null
    $second = $null
    $secondWindow = $false
    $secondLog = Join-Path $work 'probe-second.log'
    $firstFrameAtSecondLaunch = 0
    $loopbackRacePassed = $false
    try {
        Assert-GameIdle
        $argument = if ($Scenario -eq 'SteamPeer') { '-flatouttesthost' } else { '' }
        $instance = [FlatOutDesktop]::new("FlatOut-Test-$run", (Join-Path $GameDir 'Fouc.exe'), $GameDir, $argument)
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
        $budget = if ($Scenario -in 'Skins', 'Modes', 'LocalModes', 'Tournament', 'SteamPeer') { 1800 } else { 150 }
        $stepBudget = if ($Scenario -eq 'Tournament') { 100 } else { 40 }
        $modeProofs = [Collections.Generic.List[int]]::new()
        $modeFailures = [Collections.Generic.List[string]]::new()
        $lastMode = 0
        $lastModeStart = 0
        $modeFirstFrame = 0
        $lastStuntAttempt = 0
        $finishedTournamentRaces = [Collections.Generic.List[int]]::new()
        $logSize = -1
        $log = ''
        while (-not $instance.HasExited) {
            if ($watch.Elapsed.TotalSeconds -ge $budget) { throw "Timeout after $budget s. Last step: $lastStep." }
            $owned = @($instance.Pid)
            if ($second) { $owned += $second.Pid }
            if ([FlatOutDesktop]::InputDesktop() -ne $initialDesktop -or [FlatOutDesktop]::ForegroundPid() -in $owned) {
                throw 'The input desktop or game foreground ownership changed.'
            }
            if (Get-Process -Name Fouc -ErrorAction SilentlyContinue | Where-Object Id -notin $owned) {
                throw 'Another FlatOut process appeared during the test.'
            }
            $dialog = $instance.GameDialog()
            if ($null -ne $dialog) { throw "FlatOut opened a Windows dialog on the test desktop: $dialog" }
            if (-not $window -and $instance.HasGameWindow()) {
                $window = $true
                Write-ProgressLine 'Found the game window on the inactive desktop.'
            }
            if ($second) {
                if ($second.HasExited -and (-not $secondWindow -or $second.ExitCode -ne 0)) {
                    throw "Second instance exited unexpectedly (exit $($second.ExitCode))."
                }
                $dialog = $second.GameDialog()
                if ($null -ne $dialog) { throw "The second instance opened a Windows dialog: $dialog" }
                if (-not $secondWindow -and $second.HasGameWindow()) {
                    $secondWindow = $true
                    Write-ProgressLine 'The second instance opened its own window on an inactive desktop.'
                }
            }
            if (Test-Path -LiteralPath $steamLog) {
                $size = (Get-Item -LiteralPath $steamLog).Length
                if ($Scenario -eq 'Skins') {
                    # Chloe's logger can drop a line if a live reader holds the file open.
                    # Monitor metadata here, then read the exact coverage once the game has exited.
                    if ($size -ne $logSize) {
                        $logSize = $size
                        $stepTime = $watch.Elapsed.TotalSeconds
                        $capturedStall = $false
                    }
                } elseif ($size -ne $logSize) {
                    $contents = Read-TestSteamLog $steamLog
                    if ($null -ne $contents) {
                        $log = $contents
                        $logSize = $size
                        $markers = @([regex]::Matches($log, "flatout-test:${run}:([^\r\n]+)") | ForEach-Object { $_.Groups[1].Value })
                    }
                }
                $launchMarker = if ($LoopbackClientProfile) { 'loopback-host-listening' } else { 'main-menu' }
                if ($ParallelLaunch -and -not $second -and $launchMarker -in $markers) {
                    $firstFrames = @(Get-ProbeSamples)
                    if ($firstFrames) { $firstFrameAtSecondLaunch = $firstFrames[-1][0] }
                    $env:FOUC_TEST_PROBE_LOG = $secondLog
                    $hostLeaveName = $env:FOUC_TEST_LEAVE_EVENT
                    if ($LoopbackRace) { $env:FOUC_TEST_LEAVE_EVENT = $clientLeaveName }
                    try {
                        $argument = if ($LoopbackClientProfile) { '-flatouttestclient' } else { '' }
                        $second = [FlatOutDesktop]::new("FlatOut-Test-$run-Second", (Join-Path $GameDir 'Fouc.exe'), $GameDir, $argument)
                        $second.Resume()
                    } finally {
                        $env:FOUC_TEST_PROBE_LOG = $probeLog
                        $env:FOUC_TEST_LEAVE_EVENT = $hostLeaveName
                    }
                    Write-ProgressLine "Started a second FlatOut process on its own inactive desktop (PID $($second.Pid))."
                }
                if ($markers.Count -and $markers[-1] -ne $lastStep) {
                    $lastStep = $markers[-1]
                    $stepTime = $watch.Elapsed.TotalSeconds
                    $capturedStall = $false
                    if ('main-menu' -in $markers -and $Scenario -eq 'Smoke') { [void]$captureFrame.Set() }
                }
                if ($log -match 'Steam lobby (\d+) created') { $lobby = $Matches[1] }
                if ($markers -match '^ERROR ') { throw ($markers -match '^ERROR ' | Select-Object -Last 1) }
                if ($Scenario -ne 'SteamPeer' -and $lobby -and $log -match 'accepted Steam session from') { throw 'A remote Steam participant connected.' }
                if ($Scenario -ne 'Skins') { $samples = @(Get-ProbeSamples) }
                if ($LoopbackRace -and -not $loopbackRacePassed -and 'loopback-host-racing' -in $markers) {
                    $expectedLevel = if ($LoopbackDerbyLevel) { $LoopbackDerbyLevel } else { 19 }
                    $expectedOthers = if ($LoopbackDerbyLevel) { $LoopbackDerbyAI + 1 } else { 4 }
                    # Chloe's ENet plugin represents the remote driver in one of the engine's AI slots.
                    $predicate = {
                        $_[1] -eq 2 -and $_[2] -eq 8 -and $_[3] -ge 1700 -and $_[4] -eq $expectedLevel -and
                        $_[7] -eq 1 -and ($_[8] + $_[9]) -eq $expectedOthers
                    }
                    $hostRace = $samples | Where-Object $predicate | Select-Object -Last 1
                    $clientRace = Get-ProbeSamples $secondLog | Where-Object $predicate | Select-Object -Last 1
                    if ($hostRace -and $clientRace -and [Math]::Abs($hostRace[3] - $clientRace[3]) -le 750) {
                        $loopbackRacePassed = $true
                        Write-ProgressLine "Both loopback processes simulated track $expectedLevel with $expectedOthers other drivers."
                        [void]$captureFrame.Set()
                        [void]$clientLeaveRace.Set()
                        [void]$leaveRace.Set()
                    }
                }
                if ($Scenario -in 'Modes', 'LocalModes', 'Tournament' -and $lastStep -match '^mode begin (\d+) (\S+) (\d+) (\d+) (\d+) (\d+) (\d+) (-?\d+) (\d+)$') {
                    $step = [int]$Matches[1]
                    $label = $Matches[2]
                    $track = [int]$Matches[3]
                    $rules = [int]$Matches[4]
                    $derby = [int]$Matches[5]
                    $minutes = [int]$Matches[6]
                    $position = [int]$Matches[7]
                    $expectedAi = [int]$Matches[8]
                    $alternateRules = [int]$Matches[9]
                    if ($step -ne $lastModeStart -and $samples.Count) {
                        $lastModeStart = $step
                        $modeFirstFrame = $samples[-1][0]
                    }
                    if ($MixedTournament -and $label -eq 'High-Jump') {
                        $attempt = $samples | Where-Object {
                            $_[0] -gt $modeFirstFrame -and $_[1] -eq 2 -and $_[4] -eq $track -and $_[15] -gt $lastStuntAttempt
                        } | Select-Object -Last 1
                        if ($attempt) {
                            $lastStuntAttempt = $attempt[15]
                            $stepTime = $watch.Elapsed.TotalSeconds
                            Write-ProgressLine "Tournament High Jump advanced to attempt $lastStuntAttempt of $($attempt[16])."
                        }
                    }
                    if ($step -ne $lastMode) {
                        $live = @($samples | Where-Object {
                            $_[0] -gt $modeFirstFrame -and $_[1] -eq 2 -and $_[2] -eq 8 -and
                            $_[3] -ge 0
                        })
                        $matching = @($live | Where-Object {
                            ($_[4] -eq $track -or ($track -eq 0 -and $_[4] -in (1..58))) -and
                            ($_[7] -eq 1 -or ($label -eq 'Party-Stunt' -and $_[7] -eq 2)) -and
                            ($expectedAi -lt 0 -or $_[8] -eq $expectedAi) -and
                            $_[9] -eq 0 -and $_[10] -in ($rules, $alternateRules) -and $_[11] -eq $derby -and
                            (-not $minutes -or $_[12] -eq $minutes * 60000) -and
                            (-not $position -or ($_[13] -eq $position -and $_[14] -eq $position -and $_[15] -ge 1 -and $_[16] -ge $_[15]))
                        })
                        if ($label -eq 'Instant-Action' -and $log -match "flatout-test-instant:${run}:allowed ([\d,]+)") {
                            $allowedCars = $Matches[1].Split(',') | ForEach-Object { [int]$_ }
                            $matching = @($matching | Where-Object { $_[5] -in $allowedCars -and $_[6] -gt 0 })
                        }
                        $passed = $matching.Count -ge 2 -and $matching[-1][3] - $matching[0][3] -ge 1700
                        $mismatch = $live.Count -ge 2 -and $live[-1][3] - $live[0][3] -ge 5000
                        if ($StuntPose -or $StuntGhost -or $StuntGhostAsset) {
                            $passed = $passed -and (Select-String -LiteralPath $probeLog -SimpleMatch 'stunt pose replay complete' -Quiet)
                            $mismatch = Select-String -LiteralPath $probeLog -SimpleMatch 'stunt pose replay failed' -Quiet
                        }
                        if ($StuntGhost -or $StuntGhostAsset) {
                            $passed = $passed -and (Select-String -LiteralPath $probeLog -SimpleMatch 'stunt ghost rendered and crossed' -Quiet)
                            $mismatch = $mismatch -or (Select-String -LiteralPath $probeLog -SimpleMatch 'stunt ghost overlap failed' -Quiet)
                        }
                        if ($StuntGhostProps) {
                            $passed = $passed -and (Select-String -LiteralPath $probeLog -SimpleMatch 'stunt ghost bowling pin unchanged' -Quiet)
                            $mismatch = $mismatch -or (Select-String -LiteralPath $probeLog -SimpleMatch 'stunt ghost bowling pin failed' -Quiet)
                        }
                        if ($passed -or $mismatch) {
                            $lastMode = $step
                            if ($passed) {
                                $modeProofs.Add($step)
                                Write-ProgressLine "Mode $step $label passed live checks. Sample: $($matching[-1] -join ' ')"
                            } else {
                                $modeFailures.Add("$step $label")
                                Write-ProgressLine "Mode $step $label failed live checks. Sample: $($live[-1] -join ' ')"
                            }
                            [void]$captureFrame.Set()
                            if ($Scenario -ne 'Tournament' -or -not $passed) { [void]$leaveRace.Set() }
                        }
                    }
                    if ($Scenario -eq 'Tournament' -and $step -in $modeProofs -and $step -notin $finishedTournamentRaces) {
                        $finished = $samples | Where-Object {
                            $_[0] -gt $modeFirstFrame -and $_[1] -eq 2 -and $_[2] -eq 9 -and
                            $_[3] -ge ($minutes * 60000 - 200) -and $_[4] -eq $track
                        } | Select-Object -Last 1
                        if ($finished) {
                            $finishedTournamentRaces.Add($step)
                            Write-ProgressLine "Tournament event $step $label finished."
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
                if ($Scenario -eq 'SteamPeer' -and -not $steamPassed -and 'steam-peer-racing' -in $markers) {
                    $proof = $samples | Where-Object {
                        $_[1] -eq 2 -and $_[2] -eq 8 -and $_[3] -ge 12000 -and $_[4] -eq 31 -and
                            $_[7] -eq 1 -and ($_[8] + $_[9]) -eq 5
                    } | Select-Object -Last 1
                    if ($proof) {
                        $steamPassed = $true
                        Write-ProgressLine 'Steam Derby simulated with one remote driver and four AI.'
                        [void]$captureFrame.Set()
                        [void]$leaveRace.Set()
                    }
                }
            }
            if (-not $capturedStall -and $watch.Elapsed.TotalSeconds - $stepTime -ge 15) {
                [void]$captureFrame.Set()
                $capturedStall = $true
            }
            $currentBudget = if ($Scenario -eq 'SteamPeer' -and $lastStep -eq 'steam-peer-lobby') { 900 } else { $stepBudget }
            if ($watch.Elapsed.TotalSeconds - $stepTime -ge $currentBudget) {
                $latest = @(Get-ProbeSamples) | Select-Object -Last 1
                throw "No progress for $stepBudget s. Last step: $lastStep. Latest sample: $latest"
            }
            if ($watch.Elapsed.TotalSeconds -ge $nextStatus) {
                if ($Scenario -eq 'Skins') {
                    Write-ProgressLine "Skin sweep log size: $logSize bytes."
                } else {
                    Write-ProgressLine "Waiting for FlatOut. Last step: $lastStep"
                }
                $nextStatus += 15
            }
            Start-Sleep -Milliseconds 100
        }
        if (Test-Path -LiteralPath $steamLog) { $log = Get-Content -LiteralPath $steamLog -Raw }
        if ($Scenario -eq 'Skins') {
            $lastStep = [regex]::Matches($log, "flatout-test:${run}:([^\r\n]+)") |
                ForEach-Object { $_.Groups[1].Value } | Select-Object -Last 1
            if ($log -match 'accepted Steam session from') { throw 'A remote Steam participant connected.' }
        }
        if ($instance.ExitCode -ne 0) { throw "FlatOut exited with code $($instance.ExitCode). Last step: $lastStep." }
        if ($ParallelLaunch) {
            if (-not $secondWindow -or -not (Test-Path -LiteralPath $secondLog) -or
                @(Get-Content -LiteralPath $secondLog | Where-Object { $_ -match '^\d+(?: -?\d+){16}$' }).Count -lt 2) {
                throw 'The second instance did not reach a verified render loop.'
            }
            if (-not @(Get-ProbeSamples | Where-Object { $_[0] -gt $firstFrameAtSecondLaunch })) {
                throw 'The first instance did not render after the second started.'
            }
            Write-ProgressLine 'Both game processes rendered on their inactive desktops.'
        }
        if (-not $window -or @(Get-ProbeSamples).Count -lt 2) { throw 'No verified game window and render progress.' }
        $required = @('main-menu', 'DONE')
        if ($LoopbackClientProfile) {
            $required += @('loopback-client-left', 'loopback-host-closed')
            if (-not $second.HasExited -and -not $second.WaitForExit(3000)) {
                throw 'The loopback client did not exit normally.'
            }
            if ($second.ExitCode -ne 0) { throw "The loopback client exited with code $($second.ExitCode)." }
            $log = Get-Content -LiteralPath $steamLog -Raw
            if ($LoopbackRace) {
                if (-not $loopbackRacePassed) { throw 'No concurrent race proof from both loopback processes.' }
                $required += 'loopback-client-returned'
                # A live race proves both joins. The host can close the lobby only after returning.
                # Both processes append to the same plugin log, so earlier markers can be overwritten.
                if ($LoopbackDerbyLevel) {
                    $grids = foreach ($path in $probeLog, $secondLog) {
                        $line = Get-Content -LiteralPath $path | Where-Object { $_ -like 'derby-grid *' } | Select-Object -First 1
                        if (-not $line) { throw "No grid readback from $path." }
                        $parts = $line.Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
                        $cars = @($parts | Select-Object -Skip 3)
                        $positions = @($cars | ForEach-Object { ($_ -split ',')[0] })
                        $coordinates = @($cars | ForEach-Object { ($_ -split ',', 2)[1] })
                        $number = $LoopbackDerbyAI + 2
                        $unique = [Math]::Min(8, $number)
                        if ([int]$parts[1] -ne $LoopbackDerbyLevel -or [Math]::Abs([double]$parts[2] - [double]$LoopbackDamage) -gt 0.0001 -or
                            $cars.Count -ne $number -or ($positions[0..($unique - 1)] | Sort-Object -Unique).Count -ne $unique -or
                            ($coordinates[0..($unique - 1)] | Sort-Object -Unique).Count -ne $unique) {
                            throw "Grid or active damage setting differs: $line"
                        }
                        if ($unique -lt $number -and $positions[$unique] -ne '1') { throw "The ninth car did not reuse slot 1: $line" }
                        $line
                    }
                    if (($grids[0].Split(' ')[3] -split ',')[0] -ne '1' -or ($grids[1].Split(' ')[3] -split ',')[0] -ne '2') {
                        throw 'Host and client did not receive the first and second grid slots respectively.'
                    }
                    foreach ($grid in $grids) { Write-ProgressLine $grid }
                    $required += 'derby-menu 6 multiplayer 6 single-player'
                    if ($DerbyImpact) {
                        $impact = Get-Content -LiteralPath $probeLog | Select-String '^derby impact (\S+) (\S+) (\S+)$' | Select-Object -First 1
                        if (-not $impact) { throw 'The controlled Derby impact did not complete.' }
                        $playerDamage = [double]$impact.Matches[0].Groups[2].Value
                        $aiDamage = [double]$impact.Matches[0].Groups[3].Value
                        if (($LoopbackDamage -eq '0' -and ($playerDamage -gt 0 -or $aiDamage -gt 0)) -or
                            ($LoopbackDamage -ne '0' -and ($playerDamage -le 0 -and $aiDamage -le 0))) {
                            throw "Unexpected damage in the controlled Derby impact: $($impact.Line)"
                        }
                        Write-ProgressLine $impact.Line
                    }
                }
            } else { $required += @('loopback-host-joined', 'loopback-client-joined') }
        }
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
        if ($Scenario -eq 'SteamPeer') {
            $required += @('steam-browser-ready', 'steam-peer-returned', 'steam-disconnected')
            $grid = Get-Content -LiteralPath $probeLog | Where-Object { $_ -like 'derby-grid 31 0.1000 *' } | Select-Object -First 1
            if (-not $steamPassed -or -not $grid) { throw 'Steam Derby progress or 0.1x damage proof is missing.' }
            if ($grid -notmatch '^derby-grid 31 0\.1000 1,\S+ 2,\S+ 3,\S+ 4,\S+ 5,\S+ 6,\S+$') { throw "Unexpected Steam Derby grid: $grid" }
            Write-ProgressLine $grid
        }
        if ($Scenario -eq 'Skins') {
            $required += @('steam-browser-ready', 'steam-disconnected')
            $prefix = "flatout-test:${run}:"
            $range = [regex]::Match($log, $prefix + 'skin range (\d+) (\d+) (\d+) (\d+)')
            $completed = @([regex]::Matches($log, $prefix + 'skin ok (\d+) ') | ForEach-Object { [int]$_.Groups[1].Value })
            if ($log -match 'Steam lobby (\d+) created') { $lobby = $Matches[1] }
            if (-not $range.Success -or
                ($completed -join ',') -ne (([int]$range.Groups[1].Value..[int]$range.Groups[2].Value) -join ',') -or
                -not $lobby -or $log -notmatch "server closed, Steam lobby $lobby left") {
                throw "Skin coverage or Steam lobby shutdown proof is missing. Last step: $lastStep."
            }
            Write-ProgressLine "Rendered $($completed.Count) of $($range.Groups[3].Value) selectable skins across $($range.Groups[4].Value) car/variant entries."
        }
        if ($Scenario -in 'Modes', 'LocalModes', 'Tournament') {
            if ($Scenario -in 'Modes', 'Tournament') {
                $required += @('steam-browser-ready', 'steam-disconnected')
                if (-not $lobby -or $log -notmatch "server closed, Steam lobby $lobby left") {
                    throw 'Steam lobby shutdown proof is missing.'
                }
            }
            $range = [regex]::Match($log, "flatout-test:${run}:mode range (\d+) (\d+)")
            $returns = @([regex]::Matches($log, "flatout-test:${run}:mode return (\d+)") | ForEach-Object { [int]$_.Groups[1].Value })
            if (-not $range.Success) { throw 'The multiplayer modes plan is missing.' }
            $cases = [int]$range.Groups[1].Value..[int]$range.Groups[2].Value
            $planned = $cases -join ','
            if (($modeProofs -join ',') -ne $planned) {
                throw "Mode coverage or menu return proof is missing. Live mismatches: $($modeFailures -join ', ')."
            }
            if ($Scenario -eq 'LocalModes' -and $log -match "flatout-test:${run}:mode begin \d+ Instant-Action " -and
                $log -notmatch "flatout-test-instant:${run}:allowed [\d,]+\r?\n") {
                throw 'The Instant Action run did not verify the filtered car list.'
            }
            if (($returns -join ',') -ne $planned) {
                if ($Scenario -eq 'Tournament' -or $log -notmatch "flatout-test:${run}:DONE\r?\n") {
                    throw 'The complete mode return sequence was not observed.'
                }
                # Both sweeps advance their case index only after re-entering the menu.
                # The next live case, or DONE for the last case, independently proves that return.
                $inferred = $cases | Where-Object { $_ -notin $returns }
                Write-ProgressLine "Verified menu return via the next test step for cases $($inferred -join ', ')."
            }
            if ($Scenario -eq 'Tournament' -and ($finishedTournamentRaces -join ',') -ne $planned) {
                throw 'The end of every tournament event was not observed.'
            }
            if ($Scenario -eq 'Tournament') {
                if ($ForceTournamentTie -and -not (Select-String -LiteralPath $probeLog -SimpleMatch 'tournament fixture: tied race positions')) {
                    throw 'The test did not create a tie in the derby results.'
                }
                $pointFailures = [regex]::Matches($log, "flatout-test:${run}:(tournament check failed [^\r\n]+)")
                if ($pointFailures.Count) {
                    throw (($pointFailures | ForEach-Object { $_.Groups[1].Value }) -join '. ')
                }
            }
        }
        foreach ($marker in $required) {
            if ($log -notmatch "flatout-test:${run}:$marker\r?\n") { throw "Missing in-game result: $marker" }
        }
        if ($VerifySteamMusic) {
            $music = Get-Content -LiteralPath $probeLog | Where-Object { $_ -like 'music *' }
            $menu = @($music | Where-Object { $_ -like 'music menu *' })
            $race = @($music | Where-Object { $_ -like 'music race *' })
            $actual = $music | ForEach-Object { $_ -replace '^music (menu|race) data/songs/', '' } | Sort-Object
            $expected = Get-Content -LiteralPath (Join-Path $repo 'custom_src/steam_songs.txt') | Sort-Object
            if ($menu.Count -ne 5 -or $race.Count -ne 15 -or ($actual -join '|') -ne ($expected -join '|')) {
                throw "The running game loaded $($menu.Count) expected Steam menu paths and $($race.Count) race paths."
            }
            Write-ProgressLine 'The running game loaded all 20 corrected Steam song paths into its playlists.'
        }
    } finally {
        if ($clientLeaveRace) { $clientLeaveRace.Dispose() }
        if ($second) { $second.Dispose() }
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
        if ($VerifySteamMusic -and $Scenario -ne 'SinglePlayer') {
            throw 'BLOCKED: The Steam music check requires SinglePlayer.'
        }
        if (($MixedTournament -or $ForceTournamentTie) -and $Scenario -ne 'Tournament') {
            throw 'BLOCKED: Tournament options require the Tournament scenario.'
        }
        if (($TrackSweep -or $FullCarnage) -and ($Scenario -ne 'LocalModes' -or ($TrackSweep -and $FullCarnage))) {
            throw 'BLOCKED: Select at most one expanded LocalModes sweep.'
        }
        if ($ParallelLaunch -and ($Scenario -ne 'Smoke' -or $CreateProfile)) {
            throw 'BLOCKED: The parallel launch check requires Smoke and an existing profile.'
        }
        $stagedProfileFiles = @()
        if ($LoopbackRace -and -not $LoopbackClientProfile) {
            throw 'BLOCKED: LoopbackRace requires a loopback client profile.'
        }
        if ($LoopbackDerbyLevel -and -not $LoopbackRace) { throw 'BLOCKED: Derby grid testing requires LoopbackRace.' }
        if ($DerbyImpact -and ($LoopbackDerbyLevel -ne 31 -or $LoopbackDerbyAI -ne 4)) {
            throw 'BLOCKED: The controlled Derby impact requires Gas Station Derby with four AI.'
        }
        if (($StuntPose -or $StuntGhost -or $StuntGhostProps -or $StuntGhostAsset) -and
            ($Scenario -ne 'LocalModes' -or $ModeFrom -ne 4 -or $ModeThrough -ne 4 -or $TrackSweep -or $FullCarnage)) {
            throw 'BLOCKED: The stunt pose and ghost tests require LocalModes case 4.'
        }
        if (@($StuntGhost, $StuntGhostProps, $StuntGhostAsset | Where-Object { $_ }).Count -gt 1 -or
            ($StuntGhostProps -and $StuntPose)) {
            throw 'BLOCKED: Select one ghost fixture per run.'
        }
        if ($DerbyHud -and ($Scenario -ne 'LocalModes' -or $ModeFrom -ne 5 -or $ModeThrough -ne 5 -or $TrackSweep -or $FullCarnage)) {
            throw 'BLOCKED: Derby HUD testing requires LocalModes case 5.'
        }
        if ($LoopbackClientProfile) {
            if (-not $ParallelLaunch -or $ProfileSlot -eq 2) {
                throw 'BLOCKED: Loopback requires ParallelLaunch and uses free slot 2 for the client.'
            }
            foreach ($name in 'player002.sav', 'customsave002.sav', 'customsave002.ach', 'customsave002.cht') {
                $target = Join-Path $GameDir "Savegame/$name"
                if (Test-Path -LiteralPath $target) { throw "BLOCKED: Loopback profile slot 2 contains $name." }
                $source = Join-Path $LoopbackClientProfile $name
                if (Test-Path -LiteralPath $source -PathType Leaf) {
                    $stagedProfileFiles += @{ Source = $source; Target = $target }
                } elseif ($name.EndsWith('.sav')) {
                    throw "BLOCKED: The generated loopback profile is missing $name."
                }
            }
        }
        if ($ProfileFixture) {
            if ($Scenario -ne 'Smoke' -or $CreateProfile -or $ParallelLaunch) {
                throw 'BLOCKED: A profile fixture requires a single Smoke process which loads an existing profile.'
            }
            foreach ($relative in $profileFiles) {
                $target = Get-TestPath $GameDir $relative
                if (Test-Path -LiteralPath $target) { throw "BLOCKED: The requested profile slot contains $relative." }
                $source = Join-Path $ProfileFixture (Split-Path $relative -Leaf)
                if (Test-Path -LiteralPath $source -PathType Leaf) {
                    $stagedProfileFiles += @{ Source = $source; Target = $target }
                } elseif ($relative.EndsWith('.sav')) {
                    throw "BLOCKED: The profile fixture is missing $relative."
                }
            }
        }
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
        $required = @('Savegame/options.cfg', 'Savegame/device.cfg', 'ChloeSteamNet.asi')
        if ($CreateProfile) {
            if ($Scenario -ne 'Smoke') { throw 'BLOCKED: Profile export requires the Smoke scenario.' }
            foreach ($name in $profileFiles) {
                if (Test-Path -LiteralPath (Get-TestPath $GameDir $name)) { throw "BLOCKED: Slot $ProfileSlot is not empty ($name)." }
            }
        } elseif (-not $ProfileFixture) {
            $required += $profileFiles[0..1]
        }
        foreach ($name in $required) {
            if (-not (Test-Path -LiteralPath (Get-TestPath $GameDir $name) -PathType Leaf)) { throw "BLOCKED: Missing $name." }
        }
        foreach ($name in @('FlatOutGameTest.bfs', 'zz_FlatOutGameTest.asi', 'fullscreen')) {
            if (Test-Path -LiteralPath (Get-TestPath $GameDir $name)) { throw "BLOCKED: Existing $name in the installation." }
        }
        $cloud = Get-CloudRoot (Split-Path $steam.Path -Parent)
        $cloudBefore = Get-TestInventory $cloud @() @('Savegame')
        [void][IO.Directory]::CreateDirectory($work)
        Write-ProgressLine "Preparing $Scenario with profile slot $ProfileSlot. Cloud files are outside the game installation."
        Add-Type -Path (Join-Path $support 'HiddenDesktop.cs')
        try { $desktopName = [FlatOutDesktop]::InputDesktop() }
        catch { throw "BLOCKED: Cannot verify the input desktop in this Windows session. $($_.Exception.Message)" }
        if ($desktopName -ne 'Default') { throw "BLOCKED: The input desktop is $desktopName." }
        New-TestBfs
        New-TestProbe
        Assert-GameIdle
        Save-FlatOutTestState $GameDir $work $files $directories
        $staged = $true
        foreach ($profileFile in $stagedProfileFiles) {
            Copy-Item -LiteralPath $profileFile.Source -Destination $profileFile.Target
        }
        if ($CreateProfile) {
            # An incompatible autosave can corrupt the profile already at startup.
            $startupSave = Join-Path $GameDir 'Savegame/player999.sav'
            if (Test-Path -LiteralPath $startupSave -PathType Leaf) {
                Remove-Item -LiteralPath $startupSave
            }
        }
        $steamLog = Join-Path $GameDir 'ChloeSteamNet.log'
        # The plugin appends to this log. The transaction restores any previous session's file.
        if (Test-Path -LiteralPath $steamLog -PathType Leaf) { Remove-Item -LiteralPath $steamLog }
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
        if ($VerifySteamMusic) {
            $testOptions['Game.MenuSoundtrack'] = 3
            $testOptions['Game.Soundtrack'] = 3
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
        $patchPath = Join-Path $GameDir 'patch'
        $testArchives = @('FlatOutGameTest.bfs')
        [IO.File]::AppendAllText($patchPath, "`n" + ($testArchives -join "`n"))
        Invoke-TestProcess
        if ($CreateProfile) {
            $export = Join-Path $work 'created-profile'
            [void][IO.Directory]::CreateDirectory($export)
            foreach ($name in $profileFiles) {
                $source = Get-TestPath $GameDir $name
                if (Test-Path -LiteralPath $source -PathType Leaf) {
                    $target = Join-Path $export (Split-Path $name -Leaf)
                    Copy-Item -LiteralPath $source -Destination $target
                    if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
                        throw "Profile export differs from $name."
                    }
                } elseif ($name.EndsWith('.sav')) {
                    throw "Profile creation did not write $name."
                }
            }
            Write-ProgressLine "Exported the new profile to $export."
        }
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
