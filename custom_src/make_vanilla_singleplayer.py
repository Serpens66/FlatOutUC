"""Generates the vanilla single player structure for the custom modpack (ChloeCustom).

Career (classes.bed) and Carnage mode (arcadeclasses.bed) are taken 1:1 from vanilla FOUC
("alte portraits/data FOUC/scripts"). Vanilla scripts use vanilla game.db car indices and level IDs
as plain numbers, the modpack has a different order, so they are rewritten to CarIndex.* / LevelIndex.*.
Chloe's AI difficulty option (Easy/Normal/Hard) reads AIUpgradeLevel<Diff> / CupWinnings<Diff> per cup
(source/FlatOutUCChloeCollection/aifudge.h), so classes.bed gets a post pass that fills them:
Normal = vanilla, Easy/Hard = vanilla + Chloe's offsets for the cup of the same name, winnings = vanilla.
Car prices in the modpack game.db are lower than vanilla (Chloe's economy). gamedb_patch.txt restores
the vanilla prices for vanilla cars (data folder 1-47) and the FO2 variants (folder 201-247, price of the
base car). build.ps1 applies it with tools/DbPatch.

Usage: python custom_src/make_vanilla_singleplayer.py <extracted foucpack_gcp_misc.bfs of the original mod>
       (the folder that contains data/scripts and data/database)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VANILLA = os.path.join(ROOT, 'alte portraits', 'data FOUC')
OUT_SCRIPTS = os.path.join(ROOT, 'custom_src', 'misc_overrides', 'data', 'scripts')
OUT_PATCH = os.path.join(ROOT, 'custom_src', 'gamedb_patch.txt')

# vanilla game.db index (order of root/FlatOut2/Cars.Car, 0-based) -> CarIndex name in the modpack cars.bed.
# Checked against vanilla data paths and Chloe's own conversions (e.g. career cup unlocks {3,4,5} =
# Shaker, BlasterXL, Banger; InitialGarage {0,1,2,34} = Chili, Malice, Roamer, Starflight).
# Index 43 is vanilla bonus car_44 "Rocket", which the modpack replaced with RocketSS (Chloe does the same).
CARS = [
    'Chili', 'Malice', 'Roamer', 'Shaker', 'BlasterXL', 'Banger', 'Splitter', 'Switchblade', 'Venom',          # 0-8
    'CTR', 'Boxer', 'MadRash', 'Nevada', 'Lancea', 'Fortune', 'Daytana', 'Bullet', 'Lentus', 'Ventura',        # 9-18
    'Insetta', 'ChiliPepper', 'Scorpion', 'InsettaSport', 'Sparrowhawk', 'Crusader', 'CTRSport', 'VexterXS',   # 19-26
    'Speedshifter', 'Canyon', 'Terrator', 'Sunray', 'Speedevil', 'RoadKing', 'BulletGT',                       # 27-33
    'Starflight', 'Trailblazer', 'Afterburner', 'Rocket', 'Nucleon',                                           # 34-38
    'Pimpster', 'Flatmobile', 'MobCar', 'SchoolBus', 'RocketSS', 'Truck', 'Bonecracker', 'Grinder',            # 39-46
]


def read(path):
    with open(path, 'rb') as f:
        return f.read().decode('latin-1').replace('\r\n', '\n')


def write(path, text):
    with open(path, 'wb') as f:
        f.write(text.replace('\n', '\r\n').encode('latin-1'))
    print('wrote', os.path.relpath(path, ROOT))


def level_index(text):
    body = re.search(r'^LevelIndex = \{(.*?)^\}', text, re.S | re.M).group(1)
    return {name: int(num) for name, num in re.findall(r'(\w+)\s*=\s*(\d+)', body)}


def car(n):
    n = int(n)
    if not 0 <= n < len(CARS):
        raise SystemExit(f'unknown vanilla car index {n}')
    return 'CarIndex.' + CARS[n]


def convert(text, vanilla_levels, mod_levels, mod_cars):
    by_num = {v: k for k, v in vanilla_levels.items()}

    def lvl(num):
        name = by_num.get(int(num))
        if name is None:
            raise SystemExit(f'unknown vanilla level id {num}')
        return 'LevelIndex.' + name

    text = re.sub(r'\bLevel\s*=\s*(\d+)', lambda m: 'Level = ' + lvl(m.group(1)), text)
    text = re.sub(r'\bLevels\[(\d+)\]', lambda m: 'Levels[' + lvl(m.group(1)) + ']', text)
    text = re.sub(r'\b(Car|CarUnlock)(\s*=\s*)(\d+)', lambda m: m.group(1) + m.group(2) + car(m.group(3)), text)
    text = re.sub(r'\b(CarUnlock|InitialGarage)(\s*=\s*)\{([\d,\s]*)\}',
                  lambda m: m.group(1) + m.group(2) + '{' + ', '.join(car(n) for n in re.findall(r'\d+', m.group(3))) + '}',
                  text)

    # every level and car must exist in the modpack and be FO2/FOUC content
    for name in set(re.findall(r'LevelIndex\.(\w+)', text)):
        if name not in mod_levels or mod_levels[name] > 58:
            raise SystemExit(f'level {name} missing in modpack levels.bed or not FO2/FOUC')
    for name in set(re.findall(r'CarIndex\.(\w+)', text)):
        if name not in mod_cars:
            raise SystemExit(f'car {name} missing in modpack cars.bed')
    if re.search(r'\b(Level|Car|CarUnlock)\s*=\s*\d', text):
        raise SystemExit('unconverted number left')
    return text


def add_carindex(text):
    marker = 'if not Levels then\n\t-- levels.bed is needed for stunt names\n\tdofile( "data/scripts/levels.bed" )\nend\n'
    if marker not in text:
        raise SystemExit('levels.bed header not found')
    return text.replace(marker, marker + '\nif not CarIndex then\n\t-- cars.bed is needed for car indices (ChloeCustom)\n'
                        '\tdofile( "data/scripts/cars.bed" )\nend\n', 1)


def class_segments(text):
    """class name -> text of that class (ClassC, ClassB, ClassA, Finals)"""
    heads = [(m.start(), m.group(1)) for m in re.finditer(r'^(ClassC|ClassB|ClassA|Finals) = \{', text, re.M)]
    return {name: text[start:(heads[i + 1][0] if i + 1 < len(heads) else len(text))] for i, (start, name) in enumerate(heads)}


def cup_names(segment):
    return re.findall(r'Name\s*=\s*"([^"]+)"', segment)


def chloe_offsets(chloe_classes, vanilla_classes):
    """vanilla cup name -> (easy - medium, hard - medium) of Chloe's AIUpgradeLevel values.
    Matched by cup name, cups Chloe renamed by their position in the same class."""
    pattern = (r'Name\s*=\s*"([^"]+)",\s*AIUpgradeLevelEasy\s*=\s*([\d.]+),\s*'
               r'AIUpgradeLevelMedium\s*=\s*([\d.]+),\s*AIUpgradeLevelHard\s*=\s*([\d.]+)')
    by_name, by_pos = {}, {}
    for cls, seg in class_segments(chloe_classes).items():
        for i, m in enumerate(re.finditer(pattern, seg)):
            e, n, h = float(m.group(2)), float(m.group(3)), float(m.group(4))
            by_name.setdefault(m.group(1), (round(e - n, 3), round(h - n, 3)))
            by_pos[(cls, i)] = (m.group(1), by_name[m.group(1)])
    offsets = {}
    for cls, seg in class_segments(vanilla_classes).items():
        for i, name in enumerate(cup_names(seg)):
            if name in by_name:
                offsets[name] = by_name[name]
            elif (cls, i) in by_pos:
                print(f'note: {name} uses the offsets of Chloe\'s cup at the same position ({by_pos[(cls, i)][0]})')
                offsets[name] = by_pos[(cls, i)][1]
    return offsets


DIFFICULTY_PASS = '''
-- ChloeCustom: AI difficulty option (Chloe's plugin, aifudge.h) reads AIUpgradeLevel<Diff> and
-- CupWinnings<Diff> per cup. NORMAL = vanilla, EASY/HARD = vanilla + Chloe's offsets for this cup,
-- cup winnings = vanilla on every difficulty. Generated by custom_src/make_vanilla_singleplayer.py.
ChloeCustom_AIUpgradeOffsets = {
%s
}

local function ChloeCustom_Clamp(v)
	if v < 0 then return 0 end
	if v > 1 then return 1 end
	return v
end

for _, class in pairs({ ClassC, ClassB, ClassA, Finals }) do
	for _, list in pairs({ class.Cups or {}, class.Events or {} }) do
		for _, cup in pairs(list) do
			if cup.AIUpgradeLevel then
				local offs = ChloeCustom_AIUpgradeOffsets[cup.Name] or { 0, 0 }
				cup.AIUpgradeLevelEasy = ChloeCustom_Clamp(cup.AIUpgradeLevel + offs[1])
				cup.AIUpgradeLevelMedium = cup.AIUpgradeLevel
				cup.AIUpgradeLevelHard = ChloeCustom_Clamp(cup.AIUpgradeLevel + offs[2])
			end
			if cup.CupWinnings then
				cup.CupWinningsEasy = cup.CupWinnings
				cup.CupWinningsMedium = cup.CupWinnings
				cup.CupWinningsHard = cup.CupWinnings
			end
		end
	end
end

-- Chloe's career reward menu shows the grand final comments only with this flag
Finals.Cups[4].UseGrandFinalComments = true
'''


def dbdump(db):
    out = subprocess.run(['dotnet', 'run', os.path.join(ROOT, 'tools', 'DbDump', 'DbDump.cs'), '--', db],
                         capture_output=True, check=True).stdout.decode('latin-1')
    cars = {}
    for line in out.splitlines():
        m = re.match(r'(root/Data/Cars/(?!AI/)[^.]+)\.(Price|DataPath) = (.*)', line)
        if m:
            cars.setdefault(m.group(1), {})[m.group(2)] = m.group(3).strip('"')
    return cars


def price_patch(mod_db):
    vanilla = dbdump(os.path.join(VANILLA, 'database', 'game.db'))
    mod = dbdump(mod_db)
    folder = lambda p: int(re.search(r'Car_(\d+)/', p).group(1))
    vprice = {}
    for node, v in vanilla.items():
        if 'DataPath' in v and 'Price' in v and '/Menu/' not in node:
            vprice[folder(v['DataPath'])] = int(v['Price'])
    lines = ['# ChloeCustom: vanilla car prices (generated by custom_src/make_vanilla_singleplayer.py, applied by build.ps1)',
             '# vanilla cars = vanilla price, FO2 variants (car_2xx) = vanilla price of the base car']
    for node, v in sorted(mod.items(), key=lambda kv: (folder(kv[1].get('DataPath', 'Car_0/')), kv[0])):
        if 'DataPath' not in v or 'Price' not in v or '/Menu/' in node:
            continue
        f = folder(v['DataPath'])
        base = f if f < 100 else f - 200 if 200 < f < 300 else None
        if base is None or base not in vprice:
            continue
        if int(v['Price']) != vprice[base]:
            lines.append(f'{node}.Price = {vprice[base]}  # modpack {v["Price"]}')
    return '\n'.join(lines) + '\n'


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    chloe = sys.argv[1]
    mod_levels = level_index(read(os.path.join(chloe, 'data', 'scripts', 'levels.bed')))
    mod_cars = set(re.findall(r'^\t(\w+)\s*=\s*ChloeCollection_', read(os.path.join(chloe, 'data', 'scripts', 'cars.bed')), re.M))
    vanilla_levels = level_index(read(os.path.join(VANILLA, 'scripts', 'levels.bed')))

    # career
    text = read(os.path.join(VANILLA, 'scripts', 'classes.bed'))
    text = add_carindex(convert(text, vanilla_levels, mod_levels, mod_cars))
    offsets = chloe_offsets(read(os.path.join(chloe, 'data', 'scripts', 'classes.bed')), text)
    cups = re.findall(r'Name\s*=\s*"([^"]+)"', text)
    missing = [c for c in cups if c not in offsets]
    if missing:
        print('note: no Chloe offsets for', missing)
    table = '\n'.join(f'\t["{c}"] = {{ {offsets[c][0]:g}, {offsets[c][1]:g} }},' for c in cups if c in offsets)
    text = ('-- ChloeCustom: vanilla FOUC career (classes.bed from the original game), cars/levels converted\n'
            '-- to CarIndex/LevelIndex. Generated by custom_src/make_vanilla_singleplayer.py, do not edit.\n'
            + text.rstrip('\n') + '\n' + DIFFICULTY_PASS % table)
    write(os.path.join(OUT_SCRIPTS, 'classes.bed'), text)

    # carnage mode
    text = read(os.path.join(VANILLA, 'scripts', 'arcadeclasses.bed'))
    text = add_carindex(convert(text, vanilla_levels, mod_levels, mod_cars))
    text = ('-- ChloeCustom: vanilla FOUC Carnage mode (arcadeclasses.bed from the original game), cars/levels converted\n'
            '-- to CarIndex/LevelIndex, no platinum medals. Generated by custom_src/make_vanilla_singleplayer.py, do not edit.\n'
            + text)
    write(os.path.join(OUT_SCRIPTS, 'arcadeclasses.bed'), text)

    # car prices
    with open(OUT_PATCH, 'w', encoding='latin-1', newline='\n') as f:
        f.write(price_patch(os.path.join(chloe, 'data', 'database', 'game.db')))
    print('wrote', os.path.relpath(OUT_PATCH, ROOT))


if __name__ == '__main__':
    main()
