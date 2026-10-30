#!/usr/bin/env python3
"""Install the built addon into Guild Wars 2 and enable Nexus autoload.

While GW2 is closed the installer also sets automatic loading in Nexus' configuration.
With --allow-running it only copies the DLL (Nexus detects it; load it from the Nexus
addon list), because Nexus rewrites its configuration when the game exits.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile

SIGNATURE = 0x44445250
NAME = 'Dance Dance Rotation (Fedora port)'
DLL = 'dance-dance-rotation.dll'
BASE = Path(__file__).resolve().parent
DEFAULT_GAME = Path.home() / '.local/share/Steam/steamapps/common/Guild Wars 2'


def game_running(proc_root=Path('/proc')):
    for proc in proc_root.iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if (proc / 'comm').read_text().strip().lower() == 'gw2-64.exe':
                return True
        except OSError:
            pass
    return False


def copy_dll(binary, target, stamp):
    if target.exists():
        shutil.copy2(target, target.with_name(target.name + '.before-' + stamp + '.bak'))
    staged = target.with_suffix('.installing')
    try:
        shutil.copy2(binary, staged)
        if hashlib.sha256(staged.read_bytes()).digest() != hashlib.sha256(binary.read_bytes()).digest():
            raise ValueError('Installed DLL checksum differs from the build; nothing else was changed.')
        staged.replace(target)
    finally:
        staged.unlink(missing_ok=True)


def install(game, binary, check_running=True, allow_running=False):
    running = check_running and game_running()
    if running and not allow_running:
        raise ValueError('Close GW2 before installation so Nexus cannot overwrite its configuration '
                         '(or use --allow-running to only copy the DLL).')
    game = Path(game).expanduser().resolve()
    addons = game / 'addons'
    config = addons / 'Nexus/AddonConfig.json'
    if not (game / 'Gw2-64.exe').is_file() or not (game / 'd3d11.dll').is_file() or not config.is_file():
        raise ValueError('Nexus installation was not found. Select the GW2 directory with --game and launch Nexus once first.')
    if not binary.is_file():
        raise ValueError(f'Build dist/{DLL} first, or download it from this project\'s release.')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    target = addons / DLL
    if running:
        copy_dll(binary, target, stamp)
        return target, None
    records = json.loads(config.read_text(encoding='utf-8-sig'))
    if not isinstance(records, list) or any(not isinstance(record, dict) for record in records):
        raise ValueError('Unexpected Nexus configuration format; nothing was changed.')
    matches = [record for record in records if record.get('Signature') == SIGNATURE]
    if len(matches) > 1 or any(record.get('Name') != NAME for record in matches):
        raise ValueError('The addon signature is already used by an unexpected record; nothing was changed.')
    record = matches[0] if matches else {
        'Signature': SIGNATURE, 'Name': NAME, 'IsFavorite': False,
        'AllowPrereleases': False, 'IsPausingUpdates': False,
    }
    record['IsLoaded'] = True
    record['IsDisabledUntilUpdate'] = False
    if not matches:
        records.append(record)
    backup = config.with_name(config.name + '.before-dance-dance-rotation-' + stamp + '.bak')
    shutil.copy2(config, backup)
    copy_dll(binary, target, stamp)
    with tempfile.NamedTemporaryFile(mode='w', dir=config.parent, prefix='ddr-config-', suffix='.installing', delete=False) as output:
        staged_config = Path(output.name)
        json.dump(records, output, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    try:
        staged_config.chmod(stat.S_IMODE(config.stat().st_mode))
        staged_config.replace(config)
    finally:
        staged_config.unlink(missing_ok=True)
    return target, backup


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', type=Path, default=DEFAULT_GAME, help='Directory containing Gw2-64.exe')
    parser.add_argument('--allow-running', action='store_true', help='If GW2 is running, only copy the DLL')
    args = parser.parse_args()
    try:
        target, backup = install(args.game, BASE / 'dist' / DLL, allow_running=args.allow_running)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')
    print('Installed:', target)
    if backup is None:
        print('GW2 is running: open Nexus (Ctrl+O by default) > Addons and load "Dance Dance Rotation (Fedora port)".')
    else:
        print('Nexus automatic loading: enabled (IsLoaded=true). Start GW2 normally.')
        print('Configuration backup:', backup)


if __name__ == '__main__':
    main()
