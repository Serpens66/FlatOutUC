# Generates custom_src/multiplayer_overrides/data/scripts from the original Chloe multiplayer scripts
# (foucpack_gcp_multiplayer.bfs of the untouched 1.83 mod). Run once; the results are part of custom_src.
#   python custom_src/make_mp_scripts.py <folder with the extracted original data/scripts>
# Every change is marked with "ChloeCustom" and checked (exact text, expected number of hits).
import os, re, sys

src = sys.argv[1]
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'multiplayer_overrides', 'data', 'scripts')
os.makedirs(dst, exist_ok=True)

def load(name):
    with open(os.path.join(src, name), 'rb') as f:
        return f.read().decode('latin-1')

def save(name, text):
    assert '\r\n' in text and '\n' not in text.replace('\r\n', ''), name + ': mixed line endings'
    with open(os.path.join(dst, name), 'wb') as f:
        f.write(text.encode('latin-1'))

def replace(text, old, new, count=1):
    n = text.count(old)
    if n != count:
        raise SystemExit(f'expected {count} hit(s), found {n}: {old[:80]!r}')
    return text.replace(old, new)

def crlf(s):
    return s.replace('\r\n', '\n').replace('\n', '\r\n')

# --- car classes that only contain non-FO2 cars -------------------------------------------------------------
REMOVED_CLASSES = ['CARCLASS_FO1ANY', 'CARCLASS_FO1DERBY', 'CARCLASS_FO1RACING', 'CARCLASS_FO1STREET',
                   'CARCLASS_RETRODEMO', 'CARCLASS_TOUGHTRUCKS', 'CARCLASS_RTANY', 'CARCLASS_RTDERBY',
                   'CARCLASS_RTRACING', 'CARCLASS_RTSTREET', 'CARCLASS_PROPS', 'CARCLASS_PROPSLIGHT',
                   'CARCLASS_PROPSMEDIUM', 'CARCLASS_PROPSHEAVY', 'CARCLASS_PROPSRETROFOLK']

def split_top(s):
    """split a Lua list body at top-level commas"""
    items, depth, cur, quote = [], 0, '', None
    for ch in s:
        if quote:
            cur += ch
            if ch == quote:
                quote = None
            continue
        if ch in '"\'':
            quote = ch
        elif ch in '({':
            depth += 1
        elif ch in ')}':
            depth -= 1
        if ch == ',' and depth == 0:
            items.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip():
        items.append(cur.strip())
    return items

def filter_cartype_option(text, name):
    # labels, values and descriptions of the car type option are three parallel lists on consecutive lines
    m = re.search(r'\t\t\{(MULTIPLAYER_CARTYPE_ANY, [^\r\n]*)\},\r\n\t\t\{(CARCLASS_ANY, [^\r\n]*)\},\r\n'
                  r'((?:[^\r\n]*\r\n){3})\t\t\{(MULTIPLAYER_CARTYPE_ANY_DESCRIPTION, [^\r\n]*)\}\r\n', text)
    if not m:
        raise SystemExit(name + ': car type option not found')
    labels, values, descs = split_top(m.group(1)), split_top(m.group(2)), split_top(m.group(4))
    assert len(labels) == len(values) == len(descs), (name, len(labels), len(values), len(descs))
    keep = [i for i, v in enumerate(values) if v not in REMOVED_CLASSES]
    assert len(values) - len(keep) == len(REMOVED_CLASSES), name
    new = ('\t\t{' + ', '.join(labels[i] for i in keep) + '},\r\n'
           '\t\t{' + ', '.join(values[i] for i in keep) + '},\r\n' + m.group(3) +
           '\t\t{' + ', '.join(descs[i] for i in keep) + '}\r\n')
    marker = '\t\t-- ChloeCustom: car types with only non-FlatOut-2 cars (FO1, Retro Demo, Tough Trucks, Rally Trophy, props) removed\r\n'
    return text[:m.start()] + marker + new + text[m.end():]

AI_DRIVER_NAMES = ['JACK BENTON', 'KATIE JACKSON', 'SOFIA MARTINEZ', 'SALLY TAYLOR', 'JASON WALKER', 'RAY CARTER',
                   'FRANK MALCOV', 'CURTIS WOLFE', 'LEWIS DURAN', 'LEI BING', 'JILL RICHARDS']   # Config/AIConfig.toml AI1..AI11

def add_ai_driver_rows(t):
    """Steam package (ChloeSteamNet.asi): the host chooses the AI drivers. Two rows instead of AI COUNT
    (the option list does not scroll): AI SLOT 1..12 selects the slot, AI DRIVER the driver of that slot.
    Without ChloeSteam_SetAIDriver (GFWL package) the rows stay hidden and AI COUNT works as before."""
    slot_row = ('\t{ -- ChloeCustom: AI slot (AI drivers, Steam package)\r\n\t\tL("AI SLOT"),\r\n'
                '\t\t{' + ', '.join('L"%d"' % i for i in range(1, 13)) + '},\r\n'
                '\t\t{' + ', '.join(str(i) for i in range(1, 13)) + '},\r\n'
                '\t\t1,\r\n\t\ttrue,\r\n\t\tfunction() ChloeCustom_AISlotChanged() end,\r\n\t\tL"",\r\n\t\ttrue\r\n\t},\r\n')
    driver_row = ('\t{ -- ChloeCustom: AI driver of the selected slot\r\n\t\tL("AI DRIVER"),\r\n'
                  '\t\t{L"NONE", ' + ', '.join('L"%s"' % n for n in AI_DRIVER_NAMES) + '},\r\n'
                  '\t\t{' + ', '.join(str(i) for i in range(0, 12)) + '},\r\n'
                  '\t\t1,\r\n\t\ttrue,\r\n\t\tfunction() ChloeCustom_AIDriverChanged() end,\r\n\t\tL"",\r\n\t\ttrue\r\n\t},\r\n')
    t = replace(t, '\t\t' + AI_CLASS_DESCS + ' -- ChloeCustom\r\n\t},\r\n}\r\n',
                '\t\t' + AI_CLASS_DESCS + ' -- ChloeCustom\r\n\t},\r\n' + slot_row + driver_row + '}\r\n')
    t = replace(t, 'local AIClass=13\r\n\r\nlocal NumOptions=13\r\n',
                'local AIClass=13\r\nlocal AIDriverSlot=14 -- ChloeCustom\r\nlocal AIDriverName=15\r\n\r\n'
                'local NumOptions=14 -- ChloeCustom: AI COUNT is replaced by AI SLOT + AI DRIVER\r\n')
    t = replace(t, '\tRowHeight=18,\r\n\tRowPadding=2,\r\n',
                '\tRowHeight=17, -- ChloeCustom: one more row fits above the description\r\n\tRowPadding=1,\r\n')
    t = replace(t, 'local function ChangeOptions(events, carindex, carskin)', crlf('''-- ChloeCustom: AI drivers chosen by the host (ChloeSteamNet.asi keeps the list, saves it and sends it to the clients)
function ChloeCustom_AIDriverRefresh()
\tlocal text = ChloeSteam_GetAIDriverText()
\tOptionItems[AIDriverSlot][Descriptions] = text
\tOptionItems[AIDriverName][Descriptions] = text
end
function ChloeCustom_AISlotChanged()
\tSetSelectedItemByValue(AIDriverName, ChloeSteam_GetAIDriver(GetSelectedItemValue(AIDriverSlot)))
\tChloeCustom_AIDriverRefresh()
end
function ChloeCustom_AIDriverChanged()
\tChloeSteam_SetAIDriver(GetSelectedItemValue(AIDriverSlot), GetSelectedItemValue(AIDriverName))
\tChloeCustom_AIDriverRefresh()
end

local function ChangeOptions(events, carindex, carskin)'''))
    t = replace(t, '\tChloeNet_Host_SetNumAI(GetSelectedItemValue(AICount))\r\n',
                crlf('''\tif ChloeSteam_GetAIDriverCount ~= nil then -- ChloeCustom: number of chosen AI drivers
\t\tChloeNet_Host_SetNumAI(ChloeSteam_GetAIDriverCount())
\telse
\t\tChloeNet_Host_SetNumAI(GetSelectedItemValue(AICount))
\tend
'''))
    t = replace(t, '\tSetSelectedItemByValue(AIClass, ChloeNet_GetAIClass())\r\n',
                crlf('''\tSetSelectedItemByValue(AIClass, ChloeNet_GetAIClass())
\tif ChloeSteam_SetAIDriver ~= nil then -- ChloeCustom: AI SLOT / AI DRIVER instead of AI COUNT
\t\tSetHidden(AICount, true)
\t\tSetEnabled(AIDriverSlot, true)
\t\tSetEnabled(AIDriverName, true)
\t\tSetSelectedItem(AIDriverSlot, 1)
\t\tSetSelectedItemByValue(AIDriverName, ChloeSteam_GetAIDriver(1))
\t\tChloeCustom_AIDriverRefresh()
\telse
\t\tSetHidden(AIDriverSlot, true)
\t\tSetHidden(AIDriverName, true)
\tend
'''))
    return t

AI_CLASS_DESCS = ('{L"AI drives derby cars", L"AI drives race cars", L"AI drives street cars", '
                  'L"AI drives the same car as the host"}')
for name in ['chloemultiplayer_lobby_changeoptions.bed', 'chloemultiplayer_lobby_sessiondetails.bed']:
    t = filter_cartype_option(load(name), name)
    # ChloeCustom: descriptions for the AI options (empty in the original). English like the other mod texts
    t = replace(t, '\t\t{L"", L"", L"", L""}\r\n', '\t\t' + AI_CLASS_DESCS + ' -- ChloeCustom\r\n')
    if name == 'chloemultiplayer_lobby_changeoptions.bed':
        t = replace(t, '\t\t{L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L""}\r\n',
                    '\t\t{' + ', '.join(['L"Number of AI opponents. The host computes them, their strength is the DIFFICULTY in the host\'s options"'] * 13) + '} -- ChloeCustom\r\n')
        t = add_ai_driver_rows(t)
    save(name, t)

# --- lobby: every car list only with FO2 cars (tier lists name addon cars directly) ----------------------------
t = load('chloemultiplayer_lobby.bed')
# values that must survive the reload of the menu scripts after every race (Fouc.exe runs main.bed again when the
# menus start, all Lua globals are lost). Steam package: kept in ChloeSteamNet.asi (ChloeSteam_StoreSet/StoreGet).
t = replace(t, 'dofile("data/scripts/chloemultiplayer_customization.bed")\r\n', crlf('''dofile("data/scripts/chloemultiplayer_customization.bed")

-- ChloeCustom: values that must survive the reload of the menu scripts after every race (Fouc.exe runs main.bed
-- again). Steam package: kept in ChloeSteamNet.asi. GFWL package: only a global (lost after a race).
ChloeCustom_PersistentFallback = ChloeCustom_PersistentFallback or { }
function ChloeCustom_GetPersistent(key)
\tif ChloeSteam_StoreGet ~= nil then
\t\treturn ChloeSteam_StoreGet("lobby_" .. key)
\tend
\treturn ChloeCustom_PersistentFallback[key]
end
function ChloeCustom_SetPersistent(key, value)
\tif ChloeSteam_StoreSet ~= nil then
\t\tChloeSteam_StoreSet("lobby_" .. key, value)
\telse
\t\tChloeCustom_PersistentFallback[key] = value
\tend
end
'''))
t = replace(t, '\tSortCarsByClass(carlist)\r\n\t\r\n\t-- 157 cars crashes the game', crlf('''\t-- ChloeCustom: only FlatOut 2 / Ultimate Carnage cars (some classes list addon cars directly)
\tlocal fo2carlist = { }
\tfor _,c in ipairs(carlist) do
\t\tif c.index and ChloeCustom_IsFO2Car(c.index) then
\t\t\ttable.insert(fo2carlist, c)
\t\tend
\tend
\tif table.getn(fo2carlist) == 0 then
\t\tfor i = 0, db.FlatOut2.Cars:GetPropertyArraySize("Car") - 1 do
\t\t\tif IsValidCar(i) then
\t\t\t\ttable.insert(fo2carlist, {index = i})
\t\t\tend
\t\tend
\tend
\tcarlist = fo2carlist
\tSortCarsByClass(carlist)
\t
\t-- 157 cars crashes the game'''))
# Steam variant (ChloeSteamNet.asi): no port forwarding needed, so no warning about it
t = replace(t, '\tif ChloeNet_IsServer() and not ChloeNet_HasPortWarningBeenShown() then\r\n',
            '\tif ChloeNet_IsServer() and not ChloeNet_HasPortWarningBeenShown() and (ChloeSteam_IsAvailable == nil or not ChloeSteam_IsAvailable()) then -- ChloeCustom\r\n')
# stunts: GHOST MODE on by default (all players share the stunt's start point), previous setting restored afterwards
t = replace(t, '\tif gameRules == GR_STUNT and ChloeNet_GetCarClass() >= CARCLASS_SAMEASHOST and not IsStuntCar(ChloeNet_GetLocalPlayerCar()) then\r\n',
            crlf('''\t-- ChloeCustom: ghost mode for stunts (players start at the same point), restore the previous setting afterwards
\tif gameRules == GR_STUNT then
\t\tif ChloeCustom_GetPersistent("ghostbeforestunt") == nil then
\t\t\tChloeCustom_SetPersistent("ghostbeforestunt", ChloeNet_GetNoContact() and "1" or "0")
\t\t\tChloeNet_Host_SetNoContact(1)
\t\tend
\telseif ChloeCustom_GetPersistent("ghostbeforestunt") ~= nil then
\t\tChloeNet_Host_SetNoContact(tonumber(ChloeCustom_GetPersistent("ghostbeforestunt")) or 0)
\t\tChloeCustom_SetPersistent("ghostbeforestunt", nil)
\tend
\tif gameRules == GR_STUNT and ChloeNet_GetCarClass() >= CARCLASS_SAMEASHOST and not IsStuntCar(ChloeNet_GetLocalPlayerCar()) then
'''))
# defaults for a new hosted game: handling FLATOUT 2 and AI class SAME AS HOST (once per hosted game)
t = replace(t, '\tif ChloeNet_IsServer() then\r\n\t\tif ChloeNet_IsUsingZMenu() or Settings["Network"]["AllowCheats"] then\r\n',
            crlf('''\t-- ChloeCustom: defaults for a new hosted game (handling FLATOUT 2, AI class SAME AS HOST)
\tif ChloeNet_IsServer() and ChloeCustom_GetPersistent("hostdefaults") == nil then
\t\tChloeCustom_SetPersistent("hostdefaults", "1")
\t\tChloeNet_Host_SetHandlingMode(HANDLING_NORMAL_FO2DOWNFORCE)
\t\tChloeNet_Host_SetAIClass(4)
\tend
\tif ChloeNet_IsServer() then
\t\tif ChloeNet_IsUsingZMenu() or Settings["Network"]["AllowCheats"] then
'''))
t = replace(t, '\t\tif value == TRUE then\r\n\t\t\tChloeNet_Disconnect()\r\n',
            '\t\tif value == TRUE then\r\n\t\t\tChloeCustom_SetPersistent("hostdefaults", nil) -- ChloeCustom\r\n\t\t\tChloeCustom_SetPersistent("ghostbeforestunt", nil)\r\n'
            '\t\t\tif ChloeCustomTourney_Reset then ChloeCustomTourney_Reset() end -- ChloeCustom Tourney\r\n\t\t\tChloeNet_Disconnect()\r\n')
# tournament mode (Steam package: chloecustom_tournament.bed from steam_overrides, loaded by chloemultiplayer.bed).
# In the GFWL package the ChloeCustomTourney_* functions do not exist, so every hook is skipped.
t = replace(t, '\tEnterMenu("menu_chloemultiplayer_lobby")\r\nend\r\n\r\nlocal function ChangeTrack()',
            '\tEnterMenu("menu_chloemultiplayer_lobby")\r\nend\r\nChloeCustomTourney_SetEvents = SetEvents -- ChloeCustom Tourney\r\n\r\nlocal function ChangeTrack()')
t = replace(t, 'local function StartRace()\r\n\tChloeNet_Host_StartGame()\r\n',
            'local function StartRace()\r\n\tif ChloeCustomTourney_OnStartRace then ChloeCustomTourney_OnStartRace() end -- ChloeCustom Tourney\r\n\tChloeNet_Host_StartGame()\r\n')
t = replace(t, '\tGUI:InitMenuButtons(menu_chloemultiplayer_lobby.items)\r\n\tGUI:ShowMenuButtons(true)\r\n',
            '\tif ChloeCustomTourney_AddLobbyButtons then ChloeCustomTourney_AddLobbyButtons(menu_chloemultiplayer_lobby) end -- ChloeCustom Tourney\r\n'
            '\tGUI:InitMenuButtons(menu_chloemultiplayer_lobby.items)\r\n\tGUI:ShowMenuButtons(true)\r\n')
t = replace(t, '\tself.parent:create(self)\r\n\tGameFlow.ClearRace()\r\n',
            '\tself.parent:create(self)\r\n\tif ChloeCustomTourney_CaptureResults then ChloeCustomTourney_CaptureResults() end -- ChloeCustom Tourney: results of the last race\r\n\tGameFlow.ClearRace()\r\n')
t = replace(t, '\tlabelTemplate.Title=L"PING"\r\n\tStaticText(labelTemplate):SetColor(Layout.LabelColor, false)\r\n',
            '\tlabelTemplate.Title=L"PING"\r\n\tlabelTemplate.Name="chloecustom_pingheader" -- ChloeCustom Tourney: "PTS (PING)" during a tournament\r\n'
            '\tStaticText(labelTemplate):SetColor(Layout.LabelColor, false)\r\n\tlabelTemplate.Name=nil\r\n')
t = replace(t, '\t\t\tupgrades:SetTitle(ConvertToWString(ping))\r\n',
            '\t\t\tif ChloeCustomTourney_RowText then -- ChloeCustom Tourney: points of the player\r\n'
            '\t\t\t\tupgrades:SetTitle(ChloeCustomTourney_RowText(playerId, ping))\r\n\t\t\telse\r\n'
            '\t\t\t\tupgrades:SetTitle(ConvertToWString(ping))\r\n\t\t\tend\r\n')
t = replace(t, '\t\tUpdateGameInfo(self)\r\n',
            '\t\tUpdateGameInfo(self)\r\n\t\tif ChloeCustomTourney_LobbyUpdate then ChloeCustomTourney_LobbyUpdate(self, time) end -- ChloeCustom Tourney\r\n')
t = replace(t, '\t\tChloeNet_Stats_Save()\r\n\t\tEnterMenu("menu_chloemultiplayer")\r\n',
            '\t\tChloeNet_Stats_Save()\r\n\t\tif ChloeCustomTourney_Reset then ChloeCustomTourney_Reset() end -- ChloeCustom Tourney\r\n'
            '\t\tChloeCustom_SetPersistent("hostdefaults", nil) -- ChloeCustom\r\n\t\tChloeCustom_SetPersistent("ghostbeforestunt", nil)\r\n\t\tEnterMenu("menu_chloemultiplayer")\r\n')
save('chloemultiplayer_lobby.bed', t)

# --- track selection: no FO1 / Tough Trucks / Rally Trophy categories, removed tracks hidden -------------------
t = load('multiplayerracemenu.bed')
t = replace(t, ', L(RACETRACKTYPE_FO1_TOWN), L(RACETRACKTYPE_FO1_PIT), L(RACETRACKTYPE_FO1_WINTER), L(RACETRACKTYPE_TOUGHTRUCKS), L(RACETRACKTYPE_RALLYTROPHY), L(RACETRACKTYPE_RALLYTROPHYARCADE)', '')
t = replace(t, ', TRACKTYPE_FO1_TOWN, TRACKTYPE_FO1_PIT, TRACKTYPE_FO1_WINTER, TRACKTYPE_TOUGHTRUCKS, TRACKTYPE_RALLYTROPHY, TRACKTYPE_RALLYTROPHYARCADE', '')
t = replace(t, 'local TrackTypeRaces = \r\n', '-- ChloeCustom: FO1, Tough Trucks and Rally Trophy track types removed, ChloeCustomHidden tracks skipped\r\nlocal TrackTypeRaces = \r\n')
t = replace(t, '\t\t\tif l.TrackType and ', '\t\t\tif l.TrackType and not l.ChloeCustomHidden and ', 6)
t = replace(t, '\t\t\tif l and l.TrackType == TRACKTYPE_STUNT then', '\t\t\tif l and l.TrackType == TRACKTYPE_STUNT and not l.ChloeCustomHidden then -- ChloeCustom')
# descriptions of Chloe's game modes (empty in the original), texts from the mode code in source/FlatOutUCChloeCollection
t = replace(t, 'L(""), L(""), L(""), L(""), L(""), L(""), L("") },', ', '.join([
    'L"Circuit race. After 10 seconds the last racer is knocked out every 15 seconds"',      # KNOCKOUT
    'L"Circuit race. At the end of each lap the slowest racers are knocked out"',            # LAP KNOCKOUT
    'L"Slide through the corners as fast as possible and connect your drifts. Hitting a wall ends the drift"',  # DRIFT
    'L"Race with points as in Carnage mode"',                                                # CARNAGE RACE
    'L"As in Carnage mode: drive through the checkpoints before the bomb timer runs out"',   # BEAT THE BOMB
    'L"Score points with tricks: spins, rolls, flips, big air, two-wheeling, ejecting. Air control is on"',  # STUNT SHOW
    'L"Get the highest speed through each checkpoint. Highest total speed wins"',            # SPEEDTRAP
]) + ' }, -- ChloeCustom descriptions')
# Stunt (FlatOut 2 style ragdoll events) as third game type. Chloe commented it out, the lobby still handles GR_STUNT
# (stunt cars, level Rules = GR_STUNT, OnGameTypeChanged has the GAMETYPE_STUNT branch). Experimental.
t = replace(t, '\t\t{MULTIPLAYER_GAMETYPE_RACE, MULTIPLAYER_GAMETYPE_DERBY },\r\n\t\t{GAMETYPE_TOURNAMENT_RACE, GAMETYPE_TOURNAMENT_DERBY},\r\n',
            '\t\t{MULTIPLAYER_GAMETYPE_RACE, MULTIPLAYER_GAMETYPE_DERBY, MULTIPLAYER_GAMETYPE_STUNT }, -- ChloeCustom: Stunt added\r\n'
            '\t\t{GAMETYPE_TOURNAMENT_RACE, GAMETYPE_TOURNAMENT_DERBY, GAMETYPE_STUNT},\r\n')
t = replace(t, '\t\t{MULTIPLAYER_GAMETYPE_RACE_DESCRIPTION, MULTIPLAYER_GAMETYPE_DERBY_DESCRIPTION },\r\n',
            '\t\t{MULTIPLAYER_GAMETYPE_RACE_DESCRIPTION, MULTIPLAYER_GAMETYPE_DERBY_DESCRIPTION, MULTIPLAYER_GAMETYPE_STUNT_DESCRIPTION },\r\n')
# GameModes is indexed like the game types: Race, Derby, Stunt (hidden, single value)
t = replace(t, '\t\t{TRANSLATOR(MULTIPLAYER_GAMEMODE_WRECKINGDERBY_DESCRIPTION), TRANSLATOR(MULTIPLAYER_GAMEMODE_FRAGDERBY_DESCRIPTION), TRANSLATOR(MULTIPLAYER_GAMEMODE_SURVIVORDERBY_DESCRIPTION)},\r\n\t\tfalse\r\n\t},\r\n}\r\n',
            crlf('''\t\t{TRANSLATOR(MULTIPLAYER_GAMEMODE_WRECKINGDERBY_DESCRIPTION), TRANSLATOR(MULTIPLAYER_GAMEMODE_FRAGDERBY_DESCRIPTION), TRANSLATOR(MULTIPLAYER_GAMEMODE_SURVIVORDERBY_DESCRIPTION)},
\t\tfalse
\t},
\t{ -- ChloeCustom: Stunt
\t\tL"",
\t\t{L""},
\t\t{GAMETYPE_STUNT},
\t\t1,
\t\tfalse,
\t\tfunction()end,
\t\t{L"Stunt events with the driver flying out of the car (experimental in multiplayer, use AI COUNT 0)"},
\t\ttrue
\t},
}
'''))
save('multiplayerracemenu.bed', t)

print('written to', dst)
