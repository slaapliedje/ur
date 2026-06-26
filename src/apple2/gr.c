/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Apple II lo-res graphics primitives.  Lo-res blocks live in text page 1
 * ($0400-$07FF): each text byte holds two vertically-stacked blocks — the low
 * nibble is the top block (even GR row), the high nibble the bottom (odd GR row).
 * The 24 text rows use the Apple II's interleaved layout, so addresses are
 * computed, not linear.
 */
#include <stdbool.h>
#include "gr.h"

/* Toggling a soft switch is any access; an explicit store is unambiguous and can't
 * be optimised away. cc65's conio re-enables text mode as it draws, so gr_show()
 * is re-asserted after the text panel is written (see main.c). */
#define SOFT(a)  (*(volatile unsigned char *)(unsigned int)(a) = 0)

void gr_show(void)
{
    SOFT(0xC056);   /* LORES   */
    SOFT(0xC054);   /* PAGE 1  */
    SOFT(0xC053);   /* MIXED   (40x40 graphics + 4 text lines) */
    SOFT(0xC050);   /* GRAPHICS (TXTCLR) */
}

void gr_on(void)  { gr_show(); }

void gr_off(void)
{
    SOFT(0xC051);   /* TEXT */
    SOFT(0xC054);   /* PAGE 1 */
}

/* Text-row base address in the interleaved page-1 layout. */
static unsigned int row_base(unsigned char tr)
{
    return (unsigned int)(0x0400u + ((unsigned int)(tr & 7) << 7)
                                  + ((unsigned int)(tr >> 3) * 40u));
}

void gr_plot(unsigned char x, unsigned char y, unsigned char color)
{
    unsigned char *p = (unsigned char *)(row_base((unsigned char)(y >> 1)) + x);
    if (y & 1) *p = (unsigned char)((*p & 0x0F) | (color << 4));   /* bottom block */
    else       *p = (unsigned char)((*p & 0xF0) | (color & 0x0F)); /* top block    */
}

/* Fill a block rectangle. Computes the row address ONCE per text row (not per
 * block) and writes whole/half bytes, so a full-screen fill is ~10x faster than
 * looping gr_plot — fast enough to redraw the board every turn without flicker. */
void gr_bar(unsigned char x0, unsigned char y0,
            unsigned char x1, unsigned char y1, unsigned char color)
{
    unsigned char tr0 = (unsigned char)(y0 >> 1), tr1 = (unsigned char)(y1 >> 1);
    unsigned char tr, x;
    unsigned char both = (unsigned char)(color | (color << 4));

    for (tr = tr0; tr <= tr1; tr++) {
        unsigned char *p = (unsigned char *)(row_base(tr) + x0);
        unsigned char ytop = (unsigned char)(tr << 1);
        bool top_in = (ytop     >= y0) && (ytop     <= y1);   /* even GR row */
        bool bot_in = (ytop + 1 >= y0) && (ytop + 1 <= y1);   /* odd  GR row */
        for (x = x0; x <= x1; x++, p++) {
            if (top_in && bot_in) *p = both;
            else if (top_in)      *p = (unsigned char)((*p & 0xF0) | color);
            else                  *p = (unsigned char)((*p & 0x0F) | (color << 4));
        }
    }
}

/* Apple II normal-video text = ASCII with the high bit set ($A0-$FF). */
void gr_text(unsigned char x, unsigned char textrow, const char *s)
{
    unsigned char *p = (unsigned char *)(row_base(textrow) + x);
    while (*s)
        *p++ = (unsigned char)((unsigned char)*s++ | 0x80);
}

void gr_clrrow(unsigned char textrow)
{
    unsigned char *p = (unsigned char *)row_base(textrow);
    unsigned char i;
    for (i = 0; i < 40; i++)
        p[i] = 0xA0;             /* normal-video space */
}
