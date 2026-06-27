/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Sega Master System (Z80 / z88dk +sms) — Royal Game of Ur, offline ROM port.
 *
 * The SMS is a close cousin of the ColecoVision/Adam (Z80 + a TMS9918-derived VDP
 * + SN76489 PSG), so this layer follows src/adam closely: conio text for menus/HUD,
 * custom VDP tiles for the carved board, the SN76489 for sound + the Hurrian Hymn,
 * and the SMS control pad for input. No FujiNet (a cartridge console) — local
 * hot-seat + vs-AI only. Output: build/sms/ur.sms (run in MAME `sms` / Emulicious).
 *
 * This file is the bring-up scaffold: it confirms the toolchain (conio + boot +
 * the shared core compiling under z88dk) before the full board/sound/input land.
 */
#include <stdint.h>
#include <conio.h>

#include "ur.h"

int main(void)
{
    clrscr();
    cputs("The Royal Game of Ur");
    gotoxy(0, 1); cputs("Sega Master System");
    gotoxy(0, 3); cputs("Ur - Mesopotamia - c.2600 BCE");
    gotoxy(0, 5); cputs("(bring-up build)");
    {
        volatile uint8_t t = 0;
        for (;;) t++;            /* idle (sccz80 dislikes an empty loop body) */
    }
    return 0;
}
