#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
# Keep compatibility with the original installer accepting one positional path.
if [[ $# -eq 1 && "$1" != -* ]]; then
  exec python3 install.py --game "$1"
fi
exec python3 install.py "$@"
