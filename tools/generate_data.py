#!/usr/bin/env python3
"""Embed the Dance Dance Rotation 1.8.1 data and images into generated C++ headers.

Default songs are decoded here with the same rules as SongTranslator.cs; the native test
decodes the same JSON with the addon's C++ reader and checks that both agree.
"""
import json
from pathlib import Path

NOTE_TYPES = ['Dodge', 'WeaponSwap', 'WeaponStow', 'Weapon1', 'Weapon2', 'Weapon3', 'Weapon4', 'Weapon5',
              'Heal', 'Utility1', 'Utility2', 'Utility3', 'Elite',
              'Profession1', 'Profession2', 'Profession3', 'Profession4', 'Profession5', 'Unknown']
UNKNOWN = NOTE_TYPES.index('Unknown')
IMAGES = [
    'buttons/copyIcon.png', 'buttons/deleteIcon.png', 'buttons/menuIcon.png', 'buttons/menuSettings.png',
    'buttons/openLinkIcon.png', 'buttons/pauseIcon.png', 'buttons/playIcon.png', 'buttons/reloadIcon.png',
    'buttons/stopIcon.png', 'ddr_logo.png', 'ddr_logo_emblem.png',
    'help/pressPlay.png', 'help/selectSong.png', 'help/setHotkeys.png',
    'notes/ddr_note_green.png', 'notes/ddr_note_purple.png', 'notes/ddr_note_red.png',
    'notes/ddr_target_bottom.png', 'notes/ddr_target_circle.png', 'notes/ddr_target_left.png',
    'notes/ddr_target_right.png', 'notes/ddr_target_spacer.png', 'notes/ddr_target_top.png',
    'notes/notesBg.png', 'notes/notesControlsBg.png',
    'abilityIcons/custom/Dodge.png', 'abilityIcons/custom/octaveDecrease.png',
    'abilityIcons/custom/octaveIncrease.png', 'abilityIcons/custom/stowKit.png',
    'abilityIcons/custom/unknownAbilityIcon.png', 'abilityIcons/custom/weaponStow.png',
    'abilityIcons/custom/weaponSwap.png',
]


def cstr(text):
    out = []
    for byte in text.encode('utf-8'):
        ch = chr(byte)
        if ch == '"' or ch == '\\':
            out.append('\\' + ch)
        elif 0x20 <= byte < 0x7f and ch != '?':
            out.append(ch)
        else:
            out.append('\\%03o' % byte)
    return '"' + ''.join(out) + '"'


def to_number(value):
    """Newtonsoft accepts JSON numbers and numeric strings for numeric properties."""
    if value is None:
        return 0
    if isinstance(value, str):
        value = float(value.strip()) if value.strip() else None
    if value is None or isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError('Expected a number')
    return value


def to_int(value):
    value = to_number(value)
    if isinstance(value, float) and not value.is_integer():
        raise ValueError('Expected an integer')
    return int(value)


def decode_song(raw):
    """SongTranslator.FromJson; raises when the reference would fail to decode."""
    name = raw.get('name')
    if not isinstance(name, str) or not name:
        raise ValueError('Song name is missing')
    build = raw['decodedBuildTemplate']
    professions = {0: 0, 1: 1, 2: 3, 3: 4, 4: 5, 5: 6, 6: 7, 7: 8, 8: 9, 9: 2}
    profession = professions.get(to_int(build.get('profession')), 10)
    specs = build['specializations']
    if not specs:
        raise ValueError('Build template has no specializations')
    utilities = ((build.get('skills') or {}).get('terrestrial') or {}).get('utilities') or []
    utilities = [to_int(u) for u in utilities[:3]] + [0] * max(0, 3 - len(utilities))
    notes = []
    for note in raw['notes']:
        kind = note.get('noteType')
        kind = NOTE_TYPES.index(kind) if kind in NOTE_TYPES else UNKNOWN
        # Python round() and C# Math.Round both round half to even.
        notes.append((round(to_number(note.get('time'))), round(to_number(note.get('duration'))), kind,
                      to_int(note.get('abilityId')), bool(note.get('overrideAuto') is True)))
    notes.sort(key=lambda n: n[0])  # stable, like the C++ decoder
    return {
        'name': name, 'description': raw.get('description') or '', 'build_url': raw.get('buildUrl') or '',
        'build_code': raw.get('buildChatCode') or '', 'profession': profession,
        'specialization': to_int(specs[-1].get('id')), 'utility': utilities, 'notes': notes,
    }


def load_abilities(root):
    abilities = {}
    for name in ['abilityInfoApi.json', 'abilityInfoCustom.json']:
        table = json.loads((root / 'assets/data' / name).read_text(encoding='utf-8-sig'))
        for key, info in table.items():
            if not isinstance(info.get('name'), str):
                raise ValueError(f'{name}: ability {key} has no name')
            abilities[int(key)] = info
    return abilities


def render_data(root):
    raw_songs = json.loads((root / 'assets/data/defaultSongs.json').read_text(encoding='utf-8-sig'))
    songs = {}
    for raw in raw_songs:
        song = decode_song(raw)
        songs.pop(song['name'], None)
        songs[song['name']] = song  # Later definitions replace earlier ones with the same name.
    abilities = load_abilities(root)
    palette = {int(k): int(v) for k, v in json.loads((root / 'assets/data/paletteSkillLookup.json').read_text(encoding='utf-8-sig')).items()}
    custom_icons = sorted({info['icon'] for info in abilities.values() if info.get('icon')})
    for icon in custom_icons:
        if 'abilityIcons/' + icon not in IMAGES:
            raise ValueError(f'Custom ability icon is not embedded: {icon}')

    lines = ['// Generated by tools/generate_data.py from Dance Dance Rotation 1.8.1 data; do not edit.',
             '#pragma once', '#include <cstddef>', 'namespace bundled {',
             'struct NoteRecord { int time; int duration; int ability; unsigned char type; unsigned char override_auto; };',
             'struct SongRecord { const char* name; const char* description; const char* build_url; const char* build_code;',
             '                    int profession; int specialization; int utility[3]; int first_note; int note_count; };',
             'struct AbilityRecord { int id; int asset; int custom_icon; const char* name; };',
             'struct PaletteRecord { int palette; int ability; };',
             f'constexpr int raw_song_count = {len(raw_songs)};',
             'inline const NoteRecord notes[] = {']
    records, first = [], 0
    for song in songs.values():
        for t, d, kind, ability, override_auto in song['notes']:
            lines.append(f'    {{{t},{d},{ability},{kind},{int(override_auto)}}},')
        u = song['utility']
        records.append(f'    {{{cstr(song["name"])}, {cstr(song["description"])}, {cstr(song["build_url"])}, '
                       f'{cstr(song["build_code"])}, {song["profession"]}, {song["specialization"]}, '
                       f'{{{u[0]},{u[1]},{u[2]}}}, {first}, {len(song["notes"])}}},')
        first += len(song['notes'])
    lines += ['};', 'inline const SongRecord songs[] = {', *records, '};',
              'inline const char* custom_icons[] = {' + ', '.join(cstr('abilityIcons/' + i) for i in custom_icons) + '};',
              'inline const AbilityRecord abilities[] = {']
    for key in sorted(abilities):
        info = abilities[key]
        custom = custom_icons.index(info['icon']) if info.get('icon') else -1
        lines.append(f'    {{{key},{to_int(info.get("assetId"))},{custom},{cstr(info["name"])}}},')
    lines += ['};', 'inline const PaletteRecord palette[] = {']
    lines += [f'    {{{k},{palette[k]}}},' for k in sorted(palette)]
    lines += ['};', '} // namespace bundled', '']
    return '\n'.join(lines), len(songs), first, len(abilities)


def render_images(root):
    lines = ['// Generated by tools/generate_data.py; do not edit.', '#pragma once', '#include <cstddef>',
             'namespace bundled_images {',
             'struct Image { const char* path; const char* identifier; const unsigned char* data; std::size_t size; };']
    entries = []
    for index, path in enumerate(IMAGES):
        data = (root / 'assets/images' / path).read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n':
            raise ValueError(f'Not a PNG: {path}')
        lines.append(f'inline const unsigned char image_{index}[] = {{')
        for offset in range(0, len(data), 40):
            lines.append('    ' + ','.join(str(b) for b in data[offset:offset + 40]) + ',')
        lines.append('};')
        entries.append(f'    {{{cstr(path)}, {cstr("DDR_IMG_" + path)}, image_{index}, sizeof(image_{index})}},')
    lines += ['inline const Image images[] = {', *entries, '};', '} // namespace bundled_images', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    (root / 'generated').mkdir(exist_ok=True)
    data, song_count, note_count, ability_count = render_data(root)
    (root / 'generated/data.hpp').write_text(data)
    (root / 'generated/images.hpp').write_text(render_images(root))
    print(f'Embedded {song_count} default songs ({note_count} notes), {ability_count} abilities, {len(IMAGES)} images.')
