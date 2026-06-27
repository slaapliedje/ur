/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari 5200 (6502 / cc65 -t atari5200) — Royal Game of Ur, offline cartridge.
 *
 * The 5200 is Atari-8-bit hardware (ANTIC + GTIA + POKEY) minus the OS, keyboard
 * and disk — a cartridge console with analog controllers. So this layer reuses the
 * shared core and the Atari POKEY sound + Hurrian-Hymn approach, but on a compact
 * 20x12 conio screen with analog-stick + FIRE input (no keyboard, no FujiNet).
 * Output: build/a5200/ur.a52 (run in MAME `a5200` / `atari800 -5200`).
 *
 * Bring-up scaffold: confirm the toolchain (cc65 atari5200 conio + cartridge boot
 * + the shared core) before the full board / sound / input land.
 */
#include <stdint.h>
#include <conio.h>

#include "ur.h"

int main(void)
{
    clrscr();
    cputsxy(0, 0, "ROYAL GAME OF UR");
    cputsxy(0, 2, "ATARI 5200");
    cputsxy(0, 4, "MESOPOTAMIA c.2600BCE");
    cputsxy(0, 7, "(bring-up build)");
    for (;;) { /* idle */ }
    return 0;
}
