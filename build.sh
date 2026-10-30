#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build dist
python3 tools/generate_data.py
if [[ -n "${ZIG:-}" ]]; then
  compiler=("$ZIG" c++)
elif command -v zig >/dev/null; then
  compiler=(zig c++)
elif command -v python-zig >/dev/null; then
  compiler=(python-zig c++)
elif [[ -x "$HOME/.local/bin/python-zig" ]]; then
  compiler=("$HOME/.local/bin/python-zig" c++)
else
  echo 'Zig is required. Set ZIG=/absolute/path/to/zig or install ziglang in user space.' >&2
  exit 1
fi
"${compiler[@]}" -std=c++17 -O2 -UNDEBUG -Isrc -Igenerated tests/engine_test.cpp -o build/engine_test
./build/engine_test assets/data/defaultSongs.json
python3 -m unittest discover -s tests -p 'test_*.py'
"${compiler[@]}" -target x86_64-windows-gnu -std=c++17 -O2 -s -Wno-nontrivial-memcall -shared \
  -Ivendor -Ivendor/imgui -Isrc -Igenerated \
  src/addon.cpp vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp \
  vendor/imgui/imgui_widgets.cpp vendor/imgui/imgui_tables.cpp \
  -luser32 -limm32 -lshell32 -o dist/dance-dance-rotation.dll
python3 tools/verify_pe.py dist/dance-dance-rotation.dll
