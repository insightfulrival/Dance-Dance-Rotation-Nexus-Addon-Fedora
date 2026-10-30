#!/usr/bin/env python3
"""Package tracked project files plus the prebuilt DLL, without local user data."""
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
dist = root / 'dist'
dll = dist / 'dance-dance-rotation.dll'
revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode().split('\0')
info = {
    'artifact': dll.name, 'sha256': hashlib.sha256(dll.read_bytes()).hexdigest(),
    'source_revision': revision, 'target': 'x86_64-windows-gnu',
    'nexus_api': 6, 'compiler': 'Zig 0.17.0', 'reference': 'Dance Dance Rotation 1.8.1 (Blish HUD)',
    'default_songs': 102, 'abilities': 25887,
    'validation': 'Native C++ engine/song tests (all 102 bundled songs decoded identically by Python and C++); Python generator/installer tests; PE verification; Wine (Proton Experimental) actual DLL load/render/unload x3 with a simulated Nexus API: clipboard import, key-start and song end, key capture, Toggle Play/Stop, chat blocking, horizontal/vertical/ability-bar windows. Not yet confirmed in a live GW2 session.',
}
