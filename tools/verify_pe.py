#!/usr/bin/env python3
"""Check the produced Nexus addon, using GNU objdump (no Python packages)."""
import re
import struct
import subprocess
import sys
from pathlib import Path


def verify(path):
    data = Path(path).read_bytes()
    assert data[:2] == b'MZ', 'Not a PE image'
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe+4] == b'PE\0\0', 'Invalid PE signature'
    machine, = struct.unpack_from('<H', data, pe+4)
    flags, = struct.unpack_from('<H', data, pe+22)
    assert machine == 0x8664 and flags & 0x2000, 'Expected Windows x64 DLL'
    assert struct.unpack_from('<H', data, pe+24)[0] == 0x20b, 'Expected PE32+'
    output = subprocess.check_output(['objdump', '-p', str(path)], text=True)
    imports = re.findall(r'DLL Name: (\S+)', output)
    allowed = {'kernel32.dll', 'ntdll.dll', 'user32.dll', 'shell32.dll', 'imm32.dll', 'msvcrt.dll', 'advapi32.dll'}
    unexpected = [name for name in imports if name.lower() not in allowed and not name.lower().startswith('api-ms-win-')]
    assert not unexpected, f'Unexpected external runtime dependencies: {unexpected}'
    assert re.search(r'\bGetAddonDef\s*$', output, re.M), 'Missing GetAddonDef export'
    print(f'PE verified: Windows x64 DLL, GetAddonDef exported, imports: {", ".join(imports)}')


if __name__ == '__main__':
    verify(sys.argv[1])
