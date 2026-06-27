/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari 8-bit hi-res ziggurat BAND for the title (A8-only; empty under UR_A5200).
 *
 * A full-screen GR.8 bitmap (7.7 KB) won't fit alongside fujinet-lib, so only the
 * ziggurat *scene* is hi-res: a custom display list keeps the title and menu as
 * normal mode-2 text (so cc65 conio still draws them straight into the OS screen),
 * and drops an ANTIC mode-F band in the middle for a crisp procedurally-drawn
 * stepped ziggurat + cuneiform friezes. Mode F is one-hue-per-scanline, so the
 * sky colour is graded by the shared DLI (COLBK + COLPF2) the way Ultima V grades
 * its intro, and a player/missile disc adds the one gold accent (the sun).
 *
 * The band buffer lives in BSS with a small runtime pad (<=39 bytes) chosen so the
 * one 4K boundary it crosses lands on a scan-line boundary — that avoids the up-to
 * 4K alignment slack a 4K-aligned array would waste, so it fits a plain 48K build.
 *
 * atari_hires_band_on() installs the list + draws the scene; the caller (main.c's
 * title_screen) still does the conio text + the menu loop, then calls _off() to
 * restore the OS display before play.
 */
#ifndef UR_A5200

#include <stdint.h>
#include "atarihw.h"

#define BAND_LINES 88        /* mode-F scanlines (11 text-rows tall) */
#define BAND_TOPROW 5        /* first screen row the band covers     */
#define HR_W 40

#define SAVMSC (*(unsigned char **)0x0058)   /* OS screen RAM pointer */
#define SDLST  (*(unsigned char **)0x0230)   /* OS display-list shadow */

static unsigned char band[BAND_LINES * HR_W + 40];   /* +40 = room for the pad */
static unsigned char dlmem[320];
static unsigned char *bm;     /* padded bitmap base */
static unsigned char *hdl;    /* 256-aligned display list */
static unsigned char *save_dl;

extern void dli_handler(void);
extern unsigned char dli_table[16];
extern unsigned char dli_table2[16];
extern unsigned char dli_len;

/* ---- bitmap primitives -------------------------------------------------- */
static void hline(int x0, int x1, int y)
{
    int x;
    unsigned char *row;
    if (y < 0 || y >= BAND_LINES) return;
    if (x0 < 0) x0 = 0;
    if (x1 > 319) x1 = 319;
    row = bm + (unsigned int)y * HR_W;
    for (x = x0; x <= x1; x++)
        row[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
}

static void fill_rect(int x0, int y0, int x1, int y1)
{
    int y;
    for (y = y0; y <= y1; y++) hline(x0, x1, y);
}

/* a centred stepped ziggurat: each tier a filled bar with a 1px sky gap so the
 * steps read; widths shrink toward the apex. */
static void draw_ziggurat(void)
{
    static const unsigned char hw[7] = { 12, 24, 36, 48, 60, 72, 84 }; /* top->bottom */
    int y = 18;          /* apex tier top */
    int th = 8;          /* tier height */
    unsigned char t;
    for (t = 0; t < 7; t++) {
        fill_rect(160 - hw[t], y, 160 + hw[t], y + th - 1);
        y += th + 1;     /* +1 = the step (sky) line */
    }
    fill_rect(24, y, 295, y + 2);    /* ground band */
}

/* a cuneiform frieze across the band at scanline y: little wedge marks */
static void draw_frieze(int y)
{
    int x;
    for (x = 12; x < 308; x += 12) {
        hline(x, x + 6, y);
        hline(x + 2, x + 4, y + 1);
        hline(x + 3, x + 3, y + 2);
    }
}

/* a filled disc (the sun), drawn into the bitmap — crisp, but mono like the rest */
static void draw_disc(int cx, int cy, int r)
{
    int dy, dx, rr = r * r;
    for (dy = -r; dy <= r; dy++) {
        dx = 0;
        while (dx * dx + dy * dy <= rr) dx++;
        hline(cx - dx + 1, cx + dx - 1, cy + dy);
    }
}

/* ---- sky gradient (DLI) on the band lines ------------------------------- */
static void sky_on(void)
{
    static const unsigned char sky[8] = { 0x72,0x84,0x88,0x98,0x9A,0x9C,0x9E,0x92 };
    unsigned char i;
    for (i = 0; i < 8; i++) { dli_table[i] = sky[i]; dli_table2[i] = sky[i]; }
    dli_len = 8;
    *(volatile unsigned char *)0x0200 = (unsigned char)((unsigned int)dli_handler & 0xFF);
    *(volatile unsigned char *)0x0201 = (unsigned char)((unsigned int)dli_handler >> 8);
    *(volatile unsigned char *)0xD40E = 0xC0;       /* NMIEN: DLI + VBI */
}

static void sky_off(void) { *(volatile unsigned char *)0xD40E = 0x40; }

/* ---- display list: text rows (OS screen) + a mode-F band (band buffer) --- */
static void build_dl(void)
{
    unsigned char *sm = SAVMSC;
    unsigned int a = (unsigned int)bm;
    unsigned int page = a & 0xF000;
    unsigned char *p = hdl;
    unsigned int line, flagstep, nextflag;
    unsigned char nflag = 0, r;

    *p++ = 0x70; *p++ = 0x70; *p++ = 0x70;          /* 24 blank scanlines */

    /* screen rows 0..(BAND_TOPROW-1): mode-2 text from the OS screen */
    *p++ = 0x42; *p++ = (unsigned char)((unsigned int)sm & 0xFF); *p++ = (unsigned char)((unsigned int)sm >> 8);
    for (r = 1; r < BAND_TOPROW; r++) *p++ = 0x02;

    /* the mode-F ziggurat band: LMS at line 0 and at each new 4K page, DLI on 8
     * evenly spaced lines (dli.s wraps idx at dli_len == 8). */
    flagstep = BAND_LINES / 8;                      /* 11 */
    nextflag = flagstep;
    for (line = 0; line < BAND_LINES; line++) {
        unsigned int la = a + line * HR_W;
        unsigned char instr;
        if (line == 0 || (la & 0xF000) != page) {   /* (re)issue LMS */
            page = la & 0xF000;
            instr = 0x4F;
            if (nflag < 8 && line >= nextflag) { instr |= 0x80; nflag++; nextflag += flagstep; }
            *p++ = instr;
            *p++ = (unsigned char)(la & 0xFF); *p++ = (unsigned char)(la >> 8);
        } else {
            instr = 0x0F;
            if (nflag < 8 && line >= nextflag) { instr |= 0x80; nflag++; nextflag += flagstep; }
            *p++ = instr;
        }
    }

    /* screen rows BAND_TOPROW+11 .. 23: mode-2 text again (the menu) */
    {
        unsigned char *m = sm + (unsigned int)(BAND_TOPROW + 11) * 40;
        *p++ = 0x42; *p++ = (unsigned char)((unsigned int)m & 0xFF); *p++ = (unsigned char)((unsigned int)m >> 8);
        for (r = (unsigned char)(BAND_TOPROW + 12); r < 24; r++) *p++ = 0x02;
    }

    *p++ = 0x41;                                     /* JVB -> loop */
    *p++ = (unsigned char)((unsigned int)hdl & 0xFF);
    *p++ = (unsigned char)((unsigned int)hdl >> 8);
}

/* Install the band: align buffers, pad so no mode-F line straddles a 4K boundary,
 * draw the scene, build + switch to our display list, light the sky + sun. The
 * caller has already drawn the conio text for rows 0-4 and the menu rows. */
void atari_hires_band_on(void)
{
    unsigned int b = (unsigned int)band;
    unsigned int base12 = b & 0x0FFF;
    unsigned char pad;
    unsigned int i;

    /* pad in [0,39] so the first 4K boundary after the bitmap falls on a line edge:
     * want (bm & 0xFFF) % 40 == 16, since (0x1000 - off) % 40 == 0 needs off%40==16
     * (4096 mod 40 == 16). That keeps any single mode-F line from straddling 4K, so
     * one clean LMS reload at the boundary suffices and we waste only <=39 bytes. */
    pad = (unsigned char)((56u - (base12 % 40u)) % 40u);
    bm  = band + pad;
    hdl = (unsigned char *)(((unsigned int)dlmem + 0x00FF) & 0xFF00);

    for (i = 0; i < BAND_LINES * HR_W; i++) bm[i] = 0;
    draw_frieze(1);
    draw_disc(160, 10, 6);                           /* the sun, above the apex */
    draw_ziggurat();
    draw_frieze(BAND_LINES - 4);

    save_dl = SDLST;
    *(volatile unsigned char *)0xD40E = 0x40;       /* DLI off during the swap */
    build_dl();
    *(volatile unsigned char *)0x02C5 = 0x0E;       /* COLPF1: bright ink luminance */
    SDLST = hdl;                                     /* OS VBI repoints ANTIC next frame */
    sky_on();
}

void atari_hires_band_off(void)
{
    sky_off();
    SDLST = save_dl;                                /* restore the OS display list */
}

#endif /* !UR_A5200 */
