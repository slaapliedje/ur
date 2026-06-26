/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Apple II double-hi-res primitives — see dhgr.h for the model.
 *
 * Uses PAGE 2 ($4000-$5FFF main + aux) so the program can be a clean ProDOS
 * SYSTEM file (custom layout in apple2-dhgr.cfg keeps code out of $4000-$5FFF).
 * Aux writes go through RAMWRT (which corrupts the C stack), so the aux half of
 * each fill is the asm routine dh_fill() — it blits a whole rectangle with RAMWRT
 * toggled once, reading each scanline's base from the precomputed row tables (no
 * per-scanline JSR or multiply). The main half is plain C, also using the tables.
 */
#include "dhgr.h"

#define WR(a)  (*(volatile unsigned char *)(unsigned int)(a) = 0)

/* asm aux interface (dhgr_blit.s). */
extern unsigned char dh_dst[2];                 /* dh_mirror source/dest, lo/hi */
extern unsigned char dh_b0, dh_b1, dh_n, dh_y0, dh_y1, dh_col;
extern void dh_fill(void);        /* fill aux rectangle (rows y0..y1) */
extern void dh_mirror(void);      /* copy _dh_n bytes at _dh_dst MAIN -> AUX */

/* DHGR row base tables (one per scanline) — non-static so dh_fill can import them
 * as _rlo/_rhi. Built once in dhgr_on(); avoids a multiply per scanline. */
unsigned char rlo[192], rhi[192];
static unsigned char rows_built = 0;

/* Calibrated below after the on-screen test; identity for now (raw nibble). */
const unsigned char dhgr_nib[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };

static unsigned int row_base(unsigned char y);   /* forward (defined below) */

static void build_rows(void)
{
    unsigned char y = 0;
    if (rows_built) return;
    do { unsigned int b = row_base(y); rlo[y] = (unsigned char)b; rhi[y] = (unsigned char)(b >> 8); }
    while (++y != 192);
    rows_built = 1;
}

void dhgr_on(void)
{
    build_rows();
    WR(0xC057);   /* HIRES                                  */
    WR(0xC05E);   /* DHIRES on                              */
    WR(0xC00D);   /* 80COL on (DHGR needs the 80-col video) */
    WR(0xC000);   /* 80STORE OFF (we page aux via RAMWRT)   */
    WR(0xC055);   /* PAGE 2 (display $4000 graphics + $0800 text) */
    WR(0xC050);   /* GRAPHICS                               */
    WR(0xC053);   /* MIXED (4 text lines for the panel)     */
}

/* Page-2 text-row base ($0800 region, interleaved like $0400). */
static unsigned int p2_base(unsigned char tr)
{
    return (unsigned int)(0x0800u + ((unsigned int)(tr & 7) << 7)
                                  + ((unsigned int)(tr >> 3) * 40u));
}

/* Mirror a whole page-2 text row main->aux so 40-col text shows double-wide
 * (clean) under 80-col/DHGR. */
static void p2_mirror(unsigned char tr)
{
    unsigned int b = p2_base(tr);
    dh_dst[0] = (unsigned char)(b & 0xFF);
    dh_dst[1] = (unsigned char)(b >> 8);
    dh_n = 40;
    dh_mirror();
}

void dhgr_clrrow(unsigned char tr)
{
    unsigned char *p = (unsigned char *)p2_base(tr);
    unsigned char i;
    for (i = 0; i < 40; i++) p[i] = 0xA0;   /* normal-video space (MAIN) */
    p2_mirror(tr);
}

/* Write a string as proper (narrow) 80-column text: aux holds the even columns,
 * main the odd. We stage the even chars into main + mirror to aux, then overwrite
 * main with the odd chars — so 80-col shows char0,char1,char2,... in order. */
void dhgr_text(unsigned char col, unsigned char tr, const char *s)
{
    unsigned char *p = (unsigned char *)p2_base(tr);
    unsigned char i, len = 0;
    (void)col;                              /* the panel always starts at column 0 */
    while (s[len]) len++;

    for (i = 0; (unsigned char)(i * 2) < len; i++)        /* even chars -> aux */
        p[i] = (unsigned char)(s[i * 2] | 0x80);
    for (; i < 40; i++) p[i] = 0xA0;
    p2_mirror(tr);                                        /* aux[c] = even chars */

    for (i = 0; (unsigned char)(i * 2 + 1) < len; i++)    /* odd chars -> main */
        p[i] = (unsigned char)(s[i * 2 + 1] | 0x80);
    for (; i < 40; i++) p[i] = 0xA0;
}

void dhgr_off(void)
{
    WR(0xC051);   /* TEXT     */
    WR(0xC05F);   /* DHIRES off */
    WR(0xC00C);   /* 80COL off */
    WR(0xC054);   /* PAGE 1   */
}

/* DHGR page-2 scanline base (within either bank), the scrambled hi-res layout. */
static unsigned int row_base(unsigned char y)
{
    return (unsigned int)(0x4000u
        + ((unsigned int)(y & 7) << 10)
        + ((unsigned int)((y >> 3) & 7) << 7)
        + ((unsigned int)(y >> 6) * 40u));
}

void dhgr_fill(unsigned char g0, unsigned char g1,
               unsigned char y0, unsigned char y1, unsigned char nib)
{
    unsigned char p0 = (unsigned char)(nib & 1),       p1 = (unsigned char)((nib>>1)&1);
    unsigned char p2 = (unsigned char)((nib>>2)&1),    p3 = (unsigned char)((nib>>3)&1);
    unsigned char b[4];
    unsigned char acol0, acol1, mcol0, mcol1, y, c;
    unsigned int base;

    b[0] = (unsigned char)(p0|(p1<<1)|(p2<<2)|(p3<<3)|(p0<<4)|(p1<<5)|(p2<<6));
    b[1] = (unsigned char)(p3|(p0<<1)|(p1<<2)|(p2<<3)|(p3<<4)|(p0<<5)|(p1<<6));
    b[2] = (unsigned char)(p2|(p3<<1)|(p0<<2)|(p1<<3)|(p2<<4)|(p3<<5)|(p0<<6));
    b[3] = (unsigned char)(p1|(p2<<1)|(p3<<2)|(p0<<3)|(p1<<4)|(p2<<5)|(p3<<6));

    /* aux columns = even groups; main columns = odd groups. */
    acol0 = (unsigned char)((g0 + (g0 & 1)) >> 1);
    acol1 = (unsigned char)((g1 - (g1 & 1)) >> 1);
    mcol0 = (unsigned char)((g0 | 1) >> 1);
    mcol1 = (unsigned char)((g1 & 1) ? (g1 >> 1) : ((g1 - 1) >> 1));

    /* AUX: one asm call for the whole rectangle (RAMWRT toggled once, row table). */
    if (acol1 >= acol0) {
        dh_y0 = y0; dh_y1 = y1; dh_col = acol0;
        dh_n = (unsigned char)(acol1 - acol0 + 1);
        if (acol0 & 1) { dh_b0 = b[2]; dh_b1 = b[0]; }   /* col even -> b[0], odd -> b[2] */
        else           { dh_b0 = b[0]; dh_b1 = b[2]; }
        dh_fill();
    }
    /* MAIN: plain C (RAMWRT off), row base from the table — no multiply. */
    if (mcol1 >= mcol0)
        for (y = y0; y <= y1; y++) {
            base = ((unsigned int)rhi[y] << 8) | rlo[y];   /* col even -> b[1], odd -> b[3] */
            for (c = mcol0; c <= mcol1; c++)
                *(unsigned char *)(base + c) = (c & 1) ? b[3] : b[1];
        }
}
