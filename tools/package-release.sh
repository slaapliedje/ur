#!/usr/bin/env bash
# Package every platform's distributable image into build/release/ with a manifest,
# checksums, and a zip.  Run via `make release` (which builds the targets first), or
# standalone after a build:  UR_VERSION=v0.1.0 tools/package-release.sh
#
# It copies whatever artifacts exist (a missing one is noted, not fatal) so a partial
# build still produces a usable bundle.
set -euo pipefail

VERSION="${UR_VERSION:-dev}"
BUILD="${BUILD_DIR:-build}"
STAGE="$BUILD/release/ur-$VERSION"
ZIP="$BUILD/release/ur-$VERSION.zip"

rm -rf "$STAGE"
mkdir -p "$STAGE"
MAN="$STAGE/MANIFEST.txt"

# src path | release filename | platform / how-to-run note
artifacts() {
cat <<EOF
$BUILD/atari/ur.xex|ur-atari-$VERSION.xex|Atari 8-bit (400/800/XL/XE) — Altirra or atari800 (run the executable)
$BUILD/a5200/ur.a52|ur-a5200-$VERSION.a52|Atari 5200 — atari800 -5200 or MAME a5200 (cartridge)
$BUILD/c64/ur.prg|ur-c64-$VERSION.prg|Commodore 64 — VICE x64sc (autostart the PRG)
$BUILD/apple2/ur.system|ur-apple2-$VERSION.system|Apple IIe (enhanced) — ProDOS SYSTEM file (copy onto a ProDOS disk)
$BUILD/apple2/ur.po|ur-apple2-$VERSION.po|Apple IIe (enhanced) — ProDOS disk image holding UR.SYSTEM
$BUILD/adam/ur.ddp|ur-adam-$VERSION.ddp|Coleco Adam — MAME adam (digital data pack)
$BUILD/coleco/ur.rom|ur-coleco-$VERSION.rom|ColecoVision — MAME coleco or real hardware (cartridge)
$BUILD/sms/ur.sms|ur-sms-$VERSION.sms|Sega Master System — MAME sms / Emulicious
$BUILD/sms/ur-gg.gg|ur-gamegear-$VERSION.gg|Sega Game Gear — MAME gamegear
$BUILD/gb/ur.gb|ur-gb-$VERSION.gb|Game Boy / Game Boy Color — MAME gameboy (grey) or gbcolor (colour)
$BUILD/nes/ur.nes|ur-nes-$VERSION.nes|NES / Famicom — MAME nes / Mesen / FCEUX (iNES NROM)
EOF
}

{
  echo "Ur — the Royal Game of Ur"
  echo "Release: $VERSION"
  echo "Built:   $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo
  printf '%-30s %9s  %s\n' "FILE" "BYTES" "PLATFORM / HOW TO RUN"
  printf '%s\n' "------------------------------------------------------------------------------"
} > "$MAN"

count=0; missing=0
while IFS='|' read -r src name note; do
  [ -z "${src:-}" ] && continue
  if [ -f "$src" ]; then
    cp "$src" "$STAGE/$name"
    sz=$(wc -c < "$STAGE/$name" | tr -d ' ')
    printf '%-30s %9s  %s\n' "$name" "$sz" "$note" >> "$MAN"
    count=$((count + 1))
  else
    printf '%-30s %9s  %s\n' "$name" "--" "$note  [NOT BUILT]" >> "$MAN"
    missing=$((missing + 1))
  fi
done < <(artifacts)

# checksums over the copied artifacts
( cd "$STAGE" && { sha256sum ur-* 2>/dev/null || shasum -a 256 ur-* ; } > SHA256SUMS.txt )

# self-contained: include the readme + licence + this manifest
cp README.md LICENSE "$STAGE/" 2>/dev/null || true

# bundle (zip preferred; tar.gz fallback if zip is absent)
( cd "$BUILD/release" && rm -f "ur-$VERSION.zip" "ur-$VERSION.tar.gz"
  if command -v zip >/dev/null 2>&1; then zip -qr "ur-$VERSION.zip" "ur-$VERSION"
  else tar czf "ur-$VERSION.tar.gz" "ur-$VERSION"; fi )

echo
echo "[release] $count artifacts packaged ($missing missing) -> $STAGE"
ls -1 "$BUILD/release/ur-$VERSION".zip "$BUILD/release/ur-$VERSION".tar.gz 2>/dev/null \
  | sed 's/^/[release] bundle: /' || true
echo
cat "$MAN"
