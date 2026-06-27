/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari 5200 display + conio + input  (compiled only for the 5200, -DUR_A5200).
 *
 * The 5200 is A8 hardware (ANTIC + GTIA + POKEY) with NO Atari OS — so there is no
 * OS-managed screen, no editor/conio, no keyboard. cc65's atari5200 runtime is a
 * minimal C environment with a VBI that mirrors the A8 OS shadow model (it copies
 * SDLSTL->DLISTL and the colour shadows $0C-$10 -> GTIA each frame). We exploit
 * that: we build our OWN 40-column display list (identical structure to the A8 OS
 * GR.0 list, so the SHARED atari_mode4_board() patch works) pointed at our own
 * screen RAM, and reimplement just the conio calls main.c uses, writing screen
 * codes straight into that RAM. The result: the A8 renderer + carved charset run
 * unchanged on the 5200. (This layer is written shared-ready so the A8 can adopt
 * it later — see ROADMAP phase 2.)
 *
 * Input maps to the 5200 controller exactly like the ColecoVision pattern: the
 * analog stick + FIRE drive the cursor/menus (via cc65's atr5200std joy driver),
 * and cgetc()/kbhit() are redirected to it so main.c's keyboard prompts just work.
 */
#ifdef UR_A5200

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>          /* vsprintf — for cprintf (NOT a conio symbol) */
#include <conio.h>          /* the signatures we (re)implement */
#include <joystick.h>

#include "atarihw.h"

/* ---- hardware (5200) ---------------------------------------------------- */
#define ANTIC_DMACTL (*(volatile unsigned char *)0xD400)
#define ANTIC_CHBASE (*(volatile unsigned char *)0xD409)
#define ANTIC_NMIEN  (*(volatile unsigned char *)0xD40E)
#define SH_SDLSTL    (*(volatile unsigned char *)0x0005)   /* DL ptr shadow (lo) */
#define SH_SDLSTH    (*(volatile unsigned char *)0x0006)   /* DL ptr shadow (hi) */
#define SH_SAVMSC    (*(unsigned char **)0x001B)            /* screen ptr shadow  */

#define SCR_W 40
#define SCR_H 24

/* Screen RAM (must not cross a 4K boundary within a mode line — 960B is fine in
 * BSS). The display list must not cross a 1K boundary, so over-allocate + align. */
static unsigned char screen[SCR_W * SCR_H];
static unsigned char dl_ram[64 + 32];

static unsigned char *dlist;     /* aligned display list */
static unsigned char curx, cury; /* conio cursor */
static unsigned char rev;        /* reverse-video flag (bit7) */

/* GR.0-structured 40x24 display list (matches the A8 OS list so the shared
 * atari_mode4_board() can patch board rows to mode 4 via dl[5+row]). */
static void build_dl(void)
{
    unsigned int s = (unsigned int)screen;
    unsigned int d = ((unsigned int)dl_ram + 0xFF) & 0xFF00;   /* 256-align: no 1K cross */
    unsigned char i, *p;
    dlist = (unsigned char *)d;
    p = dlist;
    *p++ = 0x70; *p++ = 0x70; *p++ = 0x70;          /* 24 blank scanlines */
    *p++ = 0x42;                                     /* LMS + mode 2, row 0 */
    *p++ = (unsigned char)(s & 0xFF);
    *p++ = (unsigned char)(s >> 8);
    for (i = 1; i < SCR_H; i++) *p++ = 0x02;         /* rows 1..23, mode 2 */
    *p++ = 0x41;                                     /* JVB -> loop to start */
    *p++ = (unsigned char)(d & 0xFF);
    *p++ = (unsigned char)(d >> 8);
}

/* Set up our screen + DL + DMA, and point the cc65 VBI's DL shadow at it. Called
 * once from main() before any drawing (5200 only). */
void atari_screen_init(void)
{
    unsigned int i;
    for (i = 0; i < SCR_W * SCR_H; i++) screen[i] = 0;   /* screen-code 0 = blank */
    build_dl();
    SH_SAVMSC = screen;
    ANTIC_DMACTL = 0x22;                          /* normal-width playfield + DL DMA */
    SH_SDLSTL = (unsigned char)((unsigned int)dlist & 0xFF);
    SH_SDLSTH = (unsigned char)((unsigned int)dlist >> 8);
    *(volatile unsigned char *)0xD402 = SH_SDLSTL;   /* also set ANTIC now */
    *(volatile unsigned char *)0xD403 = SH_SDLSTH;
    curx = cury = rev = 0;
    joy_install(joy_static_stddrv);
}

/* ATASCII -> Atari screen code (the mapping cc65's conio uses), + reverse bit. */
static unsigned char to_screen(char c)
{
    unsigned char u = (unsigned char)c;
    unsigned char sc;
    if (u < 0x20)      sc = (unsigned char)(u + 0x40);
    else if (u < 0x60) sc = (unsigned char)(u - 0x20);
    else               sc = u;
    return (unsigned char)(sc | rev);
}

/* ---- conio reimplementation (the exact set main.c references) ------------ */
void clrscr(void)
{
    unsigned int i;
    for (i = 0; i < SCR_W * SCR_H; i++) screen[i] = 0;
    curx = cury = 0;
}

void __fastcall__ gotoxy(unsigned char x, unsigned char y) { curx = x; cury = y; }

void __fastcall__ cputc(char c)
{
    if (cury < SCR_H && curx < SCR_W)
        screen[(unsigned int)cury * SCR_W + curx] = to_screen(c);
    if (++curx >= SCR_W) { curx = 0; if (cury < SCR_H - 1) cury++; }
}

void __fastcall__ cputcxy(unsigned char x, unsigned char y, char c)
{
    curx = x; cury = y; cputc(c);
}

void __fastcall__ cputs(const char *s) { while (*s) cputc(*s++); }

void __fastcall__ cputsxy(unsigned char x, unsigned char y, const char *s)
{
    curx = x; cury = y; cputs(s);
}

void __fastcall__ cclearxy(unsigned char x, unsigned char y, unsigned char length)
{
    unsigned char k;
    curx = x; cury = y;
    for (k = 0; k < length; k++) cputc(' ');
}

unsigned char __fastcall__ revers(unsigned char onoff)
{
    unsigned char old = rev ? 1 : 0;
    rev = onoff ? 0x80 : 0x00;
    return old;
}

unsigned char __fastcall__ cursor(unsigned char onoff) { (void)onoff; return 0; }

int cprintf(const char *format, ...)
{
    char buf[48];
    va_list ap;
    va_start(ap, format);
    vsprintf(buf, format, ap);
    va_end(ap);
    cputs(buf);
    return 0;
}

/* ---- input: 5200 controller (stick + FIRE), exposed as conio + atarihw ---- */
/* cgetc(): block until FIRE, return RETURN. kbhit(): FIRE currently pressed.
 * main.c uses these only for "press a key" prompts + as a fallback in the move
 * chooser; the menu + cursor use atari_stick()/atari_trig() (5200 versions). */
unsigned char kbhit(void)
{
    return (unsigned char)((joy_read(JOY_1) & JOY_BTN_1_MASK) ? 1 : 0);
}

char cgetc(void)
{
    while (joy_read(JOY_1) & JOY_BTN_1_MASK) { }   /* wait release first */
    while (!(joy_read(JOY_1) & JOY_BTN_1_MASK)) { }/* then a fresh press */
    return '\r';
}

/* atari_stick(): present the analog stick in the A8 STICK0 bit layout
 * (bit0 up, 1 down, 2 left, 3 right; 0 = pressed). */
unsigned char atari_stick(void)
{
    unsigned char j = joy_read(JOY_1);
    unsigned char s = 0x0F;
    if (j & JOY_UP_MASK)    s &= (unsigned char)~0x01;
    if (j & JOY_DOWN_MASK)  s &= (unsigned char)~0x02;
    if (j & JOY_LEFT_MASK)  s &= (unsigned char)~0x04;
    if (j & JOY_RIGHT_MASK) s &= (unsigned char)~0x08;
    return s;
}

unsigned char atari_trig(void)
{
    return (unsigned char)((joy_read(JOY_1) & JOY_BTN_1_MASK) ? 1 : 0);
}

#endif /* UR_A5200 */
