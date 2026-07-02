#!/usr/bin/env bash
# Publish the OFFLINE builds to itch.io via butler — as BOTH a single
# all-platforms bundle AND an individual download per machine (each its own
# itch channel, so the page shows a download button for every platform).
#
# Usage:  tools/itch-push.sh [<user>/<game>]      (default: slaapliedje/royal-game-of-ur)
# One-time: install butler (https://itch.io/docs/butler/) + `butler login`.
# Set DRY_RUN=1 to stage everything and print what WOULD be pushed (no butler needed).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
TARGET="${1:-${ITCH_TARGET:-slaapliedje/royal-game-of-ur}}"
VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo dev)"
BUNDLE="build/release/ur-$VERSION"
ITCH="build/itch"

# channel  <space>  filename-prefix(es, comma-separated) in the bundle
PLATFORMS='
atari           ur-atari-
atari-5200      ur-a5200-
c64             ur-c64-
apple2          ur-apple2-
coleco-adam     ur-adam-
colecovision    ur-coleco-
master-system   ur-sms-
game-gear       ur-gamegear-
game-boy        ur-gb-
nes             ur-nes-
atari-st        ur-st-,ur-ste-,ur-tt-
atari-falcon    ur-falcon-
'

echo "[itch] building offline release bundle ($VERSION)…"
make release UR_VERSION="$VERSION" >/dev/null
[ -d "$BUNDLE" ] || { echo "error: $BUNDLE not found after build" >&2; exit 1; }

echo "[itch] staging per-platform downloads under $ITCH/…"
rm -rf "$ITCH"; mkdir -p "$ITCH"
while read -r chan prefix; do
  [ -z "${chan:-}" ] && continue
  d="$ITCH/$chan"; mkdir -p "$d"
  found=0
  for p in ${prefix//,/ }; do
    cp "$BUNDLE/$p"* "$d"/ 2>/dev/null && found=1
  done
  [ "$found" = 1 ] || { echo "  WARN: no files for $chan ($prefix*)"; continue; }
  cp "$BUNDLE/HOW-TO-PLAY.txt" "$BUNDLE/LICENSE" "$d"/ 2>/dev/null || true
  printf "  %-14s <- %s\n" "$chan" "$(cd "$d" && ls ur-* 2>/dev/null | tr '\n' ' ')"
done <<< "$PLATFORMS"

push() {  # <dir> <channel>
  echo "[itch] push $2  (version $VERSION)"
  butler push "$1" "$TARGET:$2" --userversion "$VERSION"
}

if [ "${DRY_RUN:-0}" = 1 ] || ! command -v butler >/dev/null 2>&1; then
  [ "${DRY_RUN:-0}" = 1 ] || echo "[itch] butler not found — staging only (install it + 'butler login' to push)."
  echo "[itch] would push: $TARGET:roms  + per-platform channels:"
  while read -r chan prefix; do [ -n "${chan:-}" ] && echo "          $TARGET:$chan"; done <<< "$PLATFORMS"
  echo "[itch] staged under $ITCH/ and the bundle at $BUNDLE/"
  exit 0
fi

push "$BUNDLE" roms
while read -r chan prefix; do
  [ -z "${chan:-}" ] && continue
  [ -d "$ITCH/$chan" ] && push "$ITCH/$chan" "$chan"
done <<< "$PLATFORMS"
echo "[itch] done. Channels: butler status $TARGET"
