#!/usr/bin/env bash
# Download the pinned Linux compiler locally, build, and install into Nexus.
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
  echo 'This setup script supports x86_64 Linux only.' >&2
  exit 1
fi
build_only=false
install_args=()
while (($#)); do
  case "$1" in
    --build-only) build_only=true; shift ;;
    --game)
      if (($# < 2)); then echo '--game requires a directory.' >&2; exit 1; fi
      install_args+=(--game "$2"); shift 2 ;;
    -h|--help)
      echo 'Usage: ./setup-linux.sh [--game "/path/to/Guild Wars 2"] [--build-only]'
      exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done
for dependency in python3 curl tar xz sha256sum objdump; do
  if ! command -v "$dependency" >/dev/null 2>&1; then
    echo "Missing dependency: $dependency. Install the prerequisites for your distribution listed in README.md." >&2
    exit 1
  fi
done
version=0.17.0
if [[ -z "${ZIG:-}" ]]; then
  if command -v zig >/dev/null 2>&1 && [[ "$(zig version)" == "$version" ]]; then
    ZIG="$(command -v zig)"
  elif command -v python-zig >/dev/null 2>&1 && [[ "$(python-zig version)" == "$version" ]]; then
    ZIG="$(command -v python-zig)"
  elif [[ -x "$HOME/.local/bin/python-zig" && "$("$HOME/.local/bin/python-zig" version)" == "$version" ]]; then
    ZIG="$HOME/.local/bin/python-zig"
  else
    tool_dir="$PWD/.tools/zig-x86_64-linux-$version"
    if [[ ! -x "$tool_dir/zig" ]]; then
      mkdir -p .tools
      archive="$(mktemp "$PWD/.tools/zig-download.XXXXXXXX.tar.xz")"
      trap 'rm -f -- "$archive"' EXIT
      curl --fail --location --retry 3 --connect-timeout 30 --output "$archive" \
        "https://ziglang.org/download/$version/zig-x86_64-linux-$version.tar.xz"
      printf '%s  %s\n' '1cbe9df9f27e6b78d14ccbca43b6703a404ef79ef1c463de901d7f088d4e2026' "$archive" | sha256sum --check --status
      tar -xJf "$archive" -C .tools
    fi
    ZIG="$tool_dir/zig"
  fi
fi
export ZIG
if [[ "$("$ZIG" version)" != "$version" ]]; then
  echo 'This project is tested with Zig 0.17.0. Select it with ZIG=/path/to/zig.' >&2
  exit 1
fi
./build.sh
if $build_only; then
  echo 'Built dist/dance-dance-rotation.dll. Install later with: python3 install.py'
else
  python3 install.py "${install_args[@]}"
fi
