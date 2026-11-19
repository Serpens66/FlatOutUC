# Regenerates custom_src/steam_overrides/data/scripts (Steam multiplayer scripts) from the original scripts.
# Inputs (extract with tools/BfsTool first):
#   -ModMp   data/scripts of FlatOutUCChloeCollection_1.83/foucpack_gcp_multiplayer.bfs (mod main.bed)
#   -Vanilla data/scripts of data.bfs of the current Steam version (FOUC_steam_latest), identical to vanilla for these files
#   -Out     custom_src/steam_overrides/data/scripts
# Every change is marked with "ChloeCustom Steam" in the generated files. Each replacement must match exactly once.
param([string]$ModMp, [string]$Vanilla, [string]$Out)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null

function Read-Crlf($p) { [IO.File]::ReadAllText($p, [Text.Encoding]::GetEncoding(1252)) }
function Write-Crlf($p, $t) {
    $t = $t -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText($p, $t, [Text.Encoding]::GetEncoding(1252))
}
function Replace-Once([string]$text, [string]$old, [string]$new, [string]$what) {
    $old = $old -replace "`r?`n", "`r`n"; $new = $new -replace "`r?`n", "`r`n"
    $idx = $text.IndexOf($old)
    if ($idx -lt 0) { throw "$what : pattern not found" }
    if ($text.IndexOf($old, $idx + 1) -ge 0) { throw "$what : pattern found more than once" }
    return $text.Substring(0, $idx) + $new + $text.Substring($idx + $old.Length)
}

# ---------- main.bed (mod version + vanilla MP menu files + ReplicatedSession enum shim) ----------
$t = Read-Crlf (Join-Path $ModMp 'main.bed')
$t = Replace-Once $t @"
	"MultiplayerRaceMenu.bed",
	"MultiplayerBuildTournament.bed",
"@ @"
	--// ChloeCustom Steam: vanilla multiplayer menus (GFWL/Live flow, which ZOOM's xlive.dll maps to Steam)
	"MultiplayerCommonFunctions.bed",
	"LobbyChangeOptions.bed",
	"LobbySessionDetails.bed",
	"MultiplayerCreateGame.bed",
	"MultiplayerOptiMatch.bed",
	"MultiplayerOptiMatchResults.bed",
	"MultiplayerLobby.bed",
	"OnlineOptionsLayout.bed",
	"OnlineOptions.bed",
	"MultiplayerViewTournament.bed",
	"MultiplayerTournamentStandings.bed",
	"MultiplayerBuildTournament.bed",
	"MultiplayerRaceMenu.bed",
	"MultiplayerSearch.bed",
"@ 'main.bed menufiles'
$t = Replace-Once $t @"
function LoadMenuFiles()
	local k,v
"@ @"
--// ChloeCustom Steam: FlatOutUCChloeCollection_gcp.asi removes the Lua names of the engine enums
--// ReplicatedSession.CARCLASS_* and ReplicatedSession.GAMETYPE_* (Chloe multiplayer uses its own globals).
--// The vanilla multiplayer menus need them. Values = engine registration order (counter restarts at each NEW_ENUM, Fouc.exe 0x543D90).
local function ChloeCustomSteam_RestoreSessionEnums()
	if not ReplicatedSession then
		LOG("ChloeCustom Steam: ReplicatedSession not available")
		return
	end
	local enums = {
		CARCLASS_ANY=0, CARCLASS_DERBY=1, CARCLASS_RACING=2, CARCLASS_STREET=3,
		CARCLASS_STUNT=4, CARCLASS_NOBONUS=5, CARCLASS_BONUS=6, CARCLASS_SAMEASHOST=7,
		GAMETYPE_TOURNAMENT_RACE=0, GAMETYPE_TOURNAMENT_DERBY=1, GAMETYPE_STUNT=2, GAMETYPE_TOURNAMENT=3,
		GAMETYPE_RACE=4, GAMETYPE_PONGRACE=5, GAMETYPE_DERBY_WRECKING=6, GAMETYPE_DERBY_LMS=7,
		GAMETYPE_DERBY_FRAG=8, GAMETYPE_ARCADE_ADVENTURE=9, GAMETYPE_HUNTER_PREY=10,
	}
	local ok, err = pcall(function()
		for k,v in pairs(enums) do
			ReplicatedSession[k] = v
		end
	end)
	if not ok then
		LOG("ChloeCustom Steam: could not restore ReplicatedSession enums: %s", tostring(err))
	end
end

function LoadMenuFiles()
	local k,v

	ChloeCustomSteam_RestoreSessionEnums()
"@ 'main.bed LoadMenuFiles'
Write-Crlf (Join-Path $Out 'main.bed') $t

# ---------- multiplayermenu.bed (vanilla + Steam entry, Chloe multiplayer entry removed) ----------
$t = Read-Crlf (Join-Path $Vanilla 'multiplayermenu.bed')
$t = Replace-Once $t @"
MPMODE_NONE = 1
"@ @"
--// ChloeCustom Steam: Chloe's own multiplayer is not offered in this variant, but its constants and
--// menu_chloemultiplayer stay defined because mod scripts still reference them.
dofile("data/scripts/chloemultiplayer.bed")

MPMODE_NONE = 1
"@ 'multiplayermenu.bed header'
$t = Replace-Once $t @"
	self.items={
		MENUITEM(TRANSLATOR(ICON_MAIN_PARTYMODE), function() EnterPartyModeMenu(false) end,	ICON(self,"icon_party_mode")),
		MENUITEM(TRANSLATOR(ICON_MULTIPLAYER_PC), function() MultiplayerMode = MPMODE_GAMESPY; EnterMenu("menu_multiplayer_gamespy_checkavailability"); end, ICON(self,"icon_online")),
		MENUITEM(TRANSLATOR(ICON_MULTIPLAYER_LAN), function() MultiplayerMode = MPMODE_LAN; EnterMenu("menu_lan_modeselect"); end, ICON(self,"icon_system_link")),
	}
"@ @"
	--// ChloeCustom Steam: official Steam multiplayer = the game's Live mode, mapped to Steam by ZOOM's xlive.dll
	self.items={
		MENUITEM(TRANSLATOR(ICON_MAIN_PARTYMODE), function() EnterMenu("menu_partymode"); end,	ICON(self,"icon_party_mode")),
		MENUITEM(L("ONLINE (STEAM)"), function() MultiplayerMode = MPMODE_XBOXLIVE; EnterMenu("menu_xboxlive"); end, ICON(self,"icon_online")),
	}
"@ 'multiplayermenu.bed items'
$t = Replace-Once $t @"
	GUI:SetBackgroundVideo("data/video/bg_main")
"@ @"
	SetCurrentMenuCar() --// ChloeCustom Steam: menu scene like the other mod menus
	MenuScene:CameraRotate(FALSE)
	MenuScene:AllowCameraRotate(FALSE)
"@ 'multiplayermenu.bed init'
Write-Crlf (Join-Path $Out 'multiplayermenu.bed') $t

# ---------- multiplayerracemenu.bed (vanilla + hide tracks removed by the custom build) ----------
$t = Read-Crlf (Join-Path $Vanilla 'multiplayerracemenu.bed')
$t = Replace-Once $t @"
			if l and l.TrackType == TRACKTYPE_STUNT then
"@ @"
			if l and l.TrackType == TRACKTYPE_STUNT and not l.ChloeCustomHidden then --// ChloeCustom Steam
"@ 'racemenu stunt loop'
$t = Replace-Once $t @"
			if l.TrackType and l.AllowReversed then
"@ @"
			if l.TrackType and l.AllowReversed and not l.ChloeCustomHidden then --// ChloeCustom Steam
"@ 'racemenu reversed loop'
$t = Replace-Once $t @"
			if l.TrackType and l.TrackType == tt then
"@ @"
			if l.TrackType and l.TrackType == tt and not l.ChloeCustomHidden then --// ChloeCustom Steam
"@ 'racemenu tracktype loop'
Write-Crlf (Join-Path $Out 'multiplayerracemenu.bed') $t

# ---------- xboxlive.bed (vanilla Live/Steam main menu without ranked match and scoreboards) ----------
$t = Read-Crlf (Join-Path $Vanilla 'xboxlive.bed')
$t = Replace-Once $t @"
		{ ICON_LIVE_RANKED_MATCH,
"@ @"
		--// ChloeCustom Steam: ranked match and scoreboards need the online statistics/highscores that
		--// FlatOutUCChloeCollection_gcp.asi disables (nonetwork.h), so they are not offered.
		--{ ICON_LIVE_RANKED_MATCH,
"@ 'xboxlive ranked'
$t = Replace-Once $t @"
		{ ICON_LIVE_SCOREBOARDS,
"@ @"
		--{ ICON_LIVE_SCOREBOARDS,
"@ 'xboxlive scoreboards'
$t = Replace-Once $t @"
		table.insert(self.items, 4, { TRANSLATOR(ICON_LIVE_JOINLISTPLAY),
"@ @"
		table.insert(self.items, 3, { TRANSLATOR(ICON_LIVE_JOINLISTPLAY),
"@ 'xboxlive listplay index 1'
$t = Replace-Once $t @"
		table.insert(self.items, 5, { TRANSLATOR(ICON_LIVE_CREATELISTPLAY),
"@ @"
		table.insert(self.items, 4, { TRANSLATOR(ICON_LIVE_CREATELISTPLAY),
"@ 'xboxlive listplay index 2'
Write-Crlf (Join-Path $Out 'xboxlive.bed') $t

# ---------- multiplayerbuildtournament.bed (vanilla, uses ReplicatedSession.* instead of Chloe globals) ----------
Copy-Item (Join-Path $Vanilla 'multiplayerbuildtournament.bed') (Join-Path $Out 'multiplayerbuildtournament.bed')

Write-Host "written to $Out"

