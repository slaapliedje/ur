/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari ST (68000) platform layer — bring-up scaffold.
 *
 * The first 16-bit port. The Atari led our 8-bit era; it leads the 16-bit era too.
 * 68000 @ 8 MHz, the Shifter video chip (320x200, 16 colours from 512 in low-res,
 * word-interleaved 4-bitplane bitmap), a YM2149 PSG (the AY-3-8910 family — same
 * chip we drive on the Apple II Mockingboard), keyboard/mouse/joystick, TOS/GEMDOS.
 *
 * Built with m68k-atari-mint-gcc into a GEMDOS .prg; run in Hatari (EmuTOS) or MAME.
 * The shared src/common core compiles unchanged under GCC (68000) — the same brain
 * as the 6502 and Z80 ports.
 *
 * This scaffold proves the toolchain -> .prg -> emulator pipeline and our palette;
 * the carved Standard-of-Ur board (planar bitmap) + YM2149 sound + the shared
 * plat.h controller follow.
 */
#include <osbind.h>
#include <stdint.h>

/* Atari ST colour word: 0x0RGB, 3 bits per channel (0..7).  The Standard-of-Ur
 * palette — deep lapis field, gold, shell-white — shared in spirit with every port. */
static const uint16_t ur_palette[16] = {
    0x0012,  /*  0  deep lapis        (background / VT52 pen 0) */
    0x0775,  /*  1  shell / cream     (text)                    */
    0x0751,  /*  2  gold                                         */
    0x0024,  /*  3  lapis face                                   */
    0x0135,  /*  4  bright lapis                                 */
    0x0410,  /*  5  carnelian / brown (Dark tokens)              */
    0x0540,  /*  6  amber                                        */
    0x0333,  /*  7  stone grey                                   */
    0x0223,  /*  8  shadow                                       */
    0x0246,  /*  9                                               */
    0x0357,  /* 10                                               */
    0x0762,  /* 11                                               */
    0x0773,  /* 12                                               */
    0x0775,  /* 13                                               */
    0x0776,  /* 14                                               */
    0x0777   /* 15  white             (text fallback pen)        */
};

int main(void)
{
    Setscreen((void *)-1L, (void *)-1L, 0);   /* low-res: 320x200, 16 colours */
    Setpalette((void *)ur_palette);

    Cconws("\033E");                           /* VT52: clear screen, home cursor */
    Cconws("\r\n\r\n");
    Cconws("            THE ROYAL GAME OF UR\r\n\r\n");
    Cconws("            Atari ST  -  68000\r\n\r\n\r\n");
    Cconws("        The 16-bit port begins here.\r\n");
    Cconws("        Carved board + YM2149 next.\r\n\r\n\r\n");
    Cconws("            Press any key to exit.\r\n");

    (void)Crawcin();                           /* block until a key */
    return 0;                                  /* back to GEMDOS / the desktop */
}
