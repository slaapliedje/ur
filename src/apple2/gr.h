/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Apple II lo-res graphics (GR) — the 40x48, 16-colour block mode.
 *
 * Unlike hi-res (6 artifact colours, position-dependent), lo-res gives 16 SOLID,
 * position-independent colours including a real brown, orange, and blues — so the
 * board can use the same lapis/gold/cream/brown palette as the other platforms.
 * We use MIXED mode: 40x40 graphics on top + 4 text lines (rows 20-23) for the
 * turn/roll/move panel.  GR shares text page 1 ($0400-$07FF); blocks live in rows
 * 0-19, the 4 text lines in rows 20-23, so they never collide.
 */
#ifndef UR_APPLE2_GR_H
#define UR_APPLE2_GR_H

/* Lo-res colour codes (the standard Apple II GR palette). */
#define GR_BLACK   0
#define GR_DBLUE   2     /* dark blue   — lapis field        */
#define GR_DGREEN  4
#define GR_GREY    5
#define GR_MBLUE   6     /* medium blue                      */
#define GR_LBLUE   7     /* light blue                       */
#define GR_BROWN   8     /* a real brown — Dark piece        */
#define GR_ORANGE  9     /* gold-ish — rosettes              */
#define GR_GREY2   10
#define GR_PINK    11
#define GR_GREEN   12
#define GR_YELLOW  13    /* bright gold                      */
#define GR_AQUA    14
#define GR_WHITE   15    /* cream/bone — Light piece         */

void gr_on(void);                                    /* mixed lo-res, page 1 */
void gr_show(void);                                  /* re-assert gfx (after conio) */
void gr_off(void);                                   /* back to text mode    */
void gr_plot(unsigned char x, unsigned char y, unsigned char color);
void gr_bar(unsigned char x0, unsigned char y0,
            unsigned char x1, unsigned char y1, unsigned char color);

/* Text on the mixed-mode lines, poked straight into the page (NOT conio: conio's
 * lazy init / scrolling clobbers the GR rows that share text page 1). */
void gr_text(unsigned char x, unsigned char textrow, const char *s);
void gr_clrrow(unsigned char textrow);

#endif /* UR_APPLE2_GR_H */
