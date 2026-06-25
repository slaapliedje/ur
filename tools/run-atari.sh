#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Launch the Atari build (build/atari/ur.xex) in Altirra running under Wine.
#
#   tools/run-atari.sh [path-to-xex]
#
# Altirra location:
#   - set ALTIRRA to the full path of the .exe, OR
#   - let this script find it under your Wine prefix / common folders.
# Extra Altirra command-line options: set ALTIRRA_OPTS (e.g. "/ntsc").
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
xex="${1:-$root/build/atari/ur.xex}"

command -v wine     >/dev/null 2>&1 || { echo "error: 'wine' not found on PATH." >&2; exit 1; }
command -v winepath >/dev/null 2>&1 || { echo "error: 'winepath' not found (part of Wine)." >&2; exit 1; }

if [ ! -f "$xex" ]; then
    echo "error: '$xex' not found — build it first with 'make atari'." >&2
    exit 1
fi

find_altirra() {
    local pfx="${WINEPREFIX:-$HOME/.wine}" c
    for c in \
        "${ALTIRRA:-}" \
        "$pfx/drive_c/Program Files/Altirra"*/Altirra64.exe \
        "$pfx/drive_c/Program Files/Altirra"*/Altirra.exe \
        "$pfx/drive_c/Program Files (x86)/Altirra"*/Altirra*.exe \
        "$HOME/Altirra"*/Altirra*.exe \
        "$HOME/Downloads/Altirra"*/Altirra*.exe \
        "$HOME/Downloads/Altirra"*/*/Altirra*.exe ; do
        [ -n "$c" ] && [ -f "$c" ] && { printf '%s\n' "$c"; return 0; }
    done
    # last resort: a bounded search of the Wine C: drive
    find "$pfx/drive_c" -maxdepth 5 -iname 'Altirra*.exe' 2>/dev/null | head -n1
}

altirra="$(find_altirra || true)"
if [ -z "${altirra:-}" ] || [ ! -f "$altirra" ]; then
    echo "error: could not locate Altirra. Point ALTIRRA at the executable, e.g.:" >&2
    echo "  ALTIRRA=\"\$HOME/Altirra/Altirra64.exe\" make run-atari" >&2
    exit 1
fi

winxex="$(winepath -w "$xex")"
echo "Altirra : $altirra"
echo "Booting : $xex"
echo "       -> $winxex"
exec wine "$altirra" ${ALTIRRA_OPTS:-} "$winxex"
