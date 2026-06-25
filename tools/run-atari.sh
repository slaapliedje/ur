#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Launch the Atari build (build/atari/ur.xex) in Altirra.
#
#   tools/run-atari.sh [path-to-xex]
#
# Backends, in order of preference:
#   1. $ALTIRRA set         -> wine "$ALTIRRA" <win-path>   (a standalone Altirra .exe)
#   2. ALTIRRA_SDL=1        -> AltirraSDL <unix-path>        (force the native build)
#   3. `altirra` on PATH    -> altirra <win-path>           (AUR Wine wrapper; default)
#   4. `AltirraSDL` on PATH -> AltirraSDL <unix-path>        (native, no Wine)
#   5. search the Wine prefix for Altirra*.exe
#
# The Wine backends need a Windows-style path, so we convert with `winepath -w`.
# Extra Altirra command-line options: set ALTIRRA_OPTS.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
xex="${1:-$root/build/atari/ur.xex}"

[ -f "$xex" ] || { echo "error: '$xex' not found — run 'make atari' first." >&2; exit 1; }

need_wine() {
    command -v wine     >/dev/null 2>&1 || { echo "error: 'wine' not on PATH." >&2; exit 1; }
    command -v winepath >/dev/null 2>&1 || { echo "error: 'winepath' not on PATH." >&2; exit 1; }
}
to_win() { winepath -w "$1" 2>/dev/null; }   # 2>/dev/null hides wine's fixme noise

run_native()   { echo "Launching native AltirraSDL: $xex"
                 exec AltirraSDL ${ALTIRRA_OPTS:-} "$xex"; }
run_wine_exe() { need_wine; local w; w="$(to_win "$xex")"
                 echo "Launching (wine): $1"; echo "  -> $w"
                 exec wine "$1" ${ALTIRRA_OPTS:-} "$w"; }
run_wrapper()  { need_wine; local w; w="$(to_win "$xex")"   # `altirra` does the wine call itself
                 echo "Launching (altirra wrapper) -> $w"
                 exec altirra ${ALTIRRA_OPTS:-} "$w"; }

if [ -n "${ALTIRRA:-}" ]; then
    run_wine_exe "$ALTIRRA"
elif [ "${ALTIRRA_SDL:-0}" = 1 ] && command -v AltirraSDL >/dev/null 2>&1; then
    run_native
elif command -v altirra >/dev/null 2>&1; then
    run_wrapper
elif command -v AltirraSDL >/dev/null 2>&1; then
    run_native
else
    pfx="${WINEPREFIX:-$HOME/.wine}"
    exe="$(find "$pfx/drive_c" -maxdepth 5 -iname 'Altirra*.exe' 2>/dev/null | head -n1 || true)"
    [ -n "$exe" ] || { echo "error: no Altirra found. Set ALTIRRA=/path/to/Altirra64.exe" >&2; exit 1; }
    run_wine_exe "$exe"
fi
