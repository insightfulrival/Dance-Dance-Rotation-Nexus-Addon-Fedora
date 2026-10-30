#!/usr/bin/env bash
# Load the built DLL in a throwaway Wine prefix with a simulated Nexus API (does not touch GW2).
set -euo pipefail
cd -- "$(dirname -- "$0")"
zig_bin=${ZIG:-}
if [[ -z "$zig_bin" ]]; then
  if command -v zig >/dev/null; then zig_bin=$(command -v zig)
  elif command -v python-zig >/dev/null; then zig_bin=$(command -v python-zig)
  else zig_bin="$HOME/.local/bin/python-zig"; fi
fi
wine_bin=${WINE:-wine}
mkdir -p build/smoke
"$zig_bin" c++ -target x86_64-windows-gnu -std=c++17 -O1 -UNDEBUG -Wno-nontrivial-memcall \
  -Ivendor -Ivendor/imgui tests/nexus_smoke.cpp vendor/imgui/imgui.cpp \
  vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_widgets.cpp vendor/imgui/imgui_tables.cpp \
  -luser32 -limm32 -o build/smoke/nexus-smoke.exe
cp dist/dance-dance-rotation.dll build/smoke/
export WINEPREFIX="$PWD/build/wine-smoke"
export WINEDEBUG=-all
export WINEDLLOVERRIDES='winemenubuilder.exe=d'
cd build/smoke
"$wine_bin" ./nexus-smoke.exe
