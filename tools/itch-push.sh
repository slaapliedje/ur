#!/usr/bin/env bash
# Publish the OFFLINE builds to itch.io as a downloadable game, via butler.
#
# Usage:  tools/itch-push.sh <user>/<game>        (e.g. slaapliedje/royal-game-of-ur)
#   or set ITCH_TARGET=<user>/<game> in the environment.
#
# One-time: install butler (https://itch.io/docs/butler/) and `butler login`
# (or set BUTLER_API_KEY). This builds the release bundle (make release) and pushes
# the per-platform ROM/disk images + HOW-TO-PLAY to the `roms` channel; itch versions
# each push automatically. See docs/itch/itch-page.md for the page setup.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Defaults to the live project; override with an arg or ITCH_TARGET.
TARGET="${1:-${ITCH_TARGET:-slaapliedje/royal-game-of-ur}}"
CHANNEL="${ITCH_CHANNEL:-roms}"
command -v butler >/dev/null 2>&1 || {
  echo "error: butler not found. Install from https://itch.io/docs/butler/ and run 'butler login'." >&2
  exit 2
}

VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo dev)"
BUNDLE="build/release/ur-$VERSION"

echo "[itch] building the offline release bundle ($VERSION)…"
make release UR_VERSION="$VERSION" >/dev/null

[ -d "$BUNDLE" ] || { echo "error: bundle $BUNDLE not found after build" >&2; exit 1; }

echo "[itch] bundle contents:"
ls -1 "$BUNDLE"

echo "[itch] pushing $BUNDLE -> $TARGET:$CHANNEL (version $VERSION)…"
butler push "$BUNDLE" "$TARGET:$CHANNEL" --userversion "$VERSION"

echo "[itch] done. Check status with:  butler status $TARGET:$CHANNEL"
