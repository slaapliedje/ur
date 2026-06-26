#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a bootable ProDOS disk for the Apple II Ur build.
#
# cc65 Apple II programs run *under* an OS, and we can't ship Apple's copyrighted
# ProDOS. So this takes a ProDOS disk you already have (which supplies the PRODOS
# kernel + a real boot block), copies it, removes every *.SYSTEM launcher so ours
# is the only one ProDOS auto-runs, and adds UR.SYSTEM (the $2000 image).
#
# Usage:  AC=path/to/AppleCommander.jar \
#         tools/apple2-bootdisk.sh <prodos-disk.(dsk|po)> <ur.system> <out.po>
# Or via: make apple2-bootdisk PRODOS_DISK="/path/to/ProDOS.dsk"
#
# Run it on the ENHANCED //e (ProDOS 2.x needs it):
#   mame apple2ee -flop1 <out.po>
set -euo pipefail

SRC="${1:?need a source ProDOS disk (.dsk/.po) — it supplies the PRODOS kernel}"
SYS="${2:?need the ur.system image (the stripped \$2000 binary)}"
OUT="${3:?need an output disk path (e.g. build/apple2/ur-boot.po)}"
AC="${AC:-}"
[ -n "$AC" ] || { echo "set AC=path/to/AppleCommander.jar"; exit 1; }
ac() { java -jar "$AC" "$@"; }

[ -f "$SRC" ] || { echo "no such disk: $SRC"; exit 1; }
[ -f "$SYS" ] || { echo "no such image: $SYS (run 'make apple2' first)"; exit 1; }

# AppleCommander picks the disk's sector order from the file extension, so the
# output must keep the SOURCE's extension (e.g. a ProDOS filesystem in DOS order is
# .dsk/.do, not .po). MAME auto-detects either when booting.
ext="${SRC##*.}"
OUT="${OUT%.*}.${ext}"

cp -f "$SRC" "$OUT"

# Delete every SYS file whose name ends in .SYSTEM (the launchers) so ProDOS runs
# ours. PRODOS itself is type SYS but is named just "PRODOS" (the kernel) — kept.
ac -l "$OUT" | awk '$2=="SYS" && $1 ~ /\.SYSTEM$/ {print $1}' | while read -r f; do
    echo "  removing launcher: $f"
    ac -d "$OUT" "$f" || true
done

ac -p "$OUT" UR.SYSTEM sys 0x2000 < "$SYS"

echo "=== $OUT ==="
ac -l "$OUT"
echo "boot it on the enhanced //e:  mame apple2ee -flop1 $OUT"
