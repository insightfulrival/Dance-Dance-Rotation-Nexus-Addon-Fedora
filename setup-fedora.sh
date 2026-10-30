#!/usr/bin/env bash
# Compatibility alias for existing Fedora instructions and release links.
set -euo pipefail
cd -- "$(dirname -- "$0")"
exec ./setup-linux.sh "$@"
