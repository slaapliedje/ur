/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Host tool: generate the Atari (A8 / 5200) inlaid-mosaic charset glyphs.
 *
 * The board (ANTIC mode 4, src/atari/atarihw.c) draws every square as a 2x2-char
 * 16x16 cell so it reads as an inlaid mosaic like the SMS: gold rosette flowers
 * (existing), a gold bullseye EYE down the shared lane, and a carved tile with a
 * white-stud quincunx (DOTS) on the private lanes. This tool emits the EYE and DOTS
 * glyph bytes (paste into atarihw.c). Build + run:
 *   cc -o gen tools/atari-mosaic-glyphs.c && ./gen
 *
 * Encoding: an 8-colour-pixel-wide x 16-tall cell of 2-bit codes — 0 = field (00),
 * 1 = COLOR0 white (01), 3 = "11" (COLOR2 lapis normally, or COLOR3 gold when the
 * char is drawn INVERSE). DOTS is drawn non-inverse (lapis tile + white studs); EYE
 * is drawn inverse (gold ring + white pearl). Packed to 4 chars (TL,TR / BL,BR);
 * each mode-4 char row = 4 pixels, 2 bits, MSB = leftmost pixel.
 */
#include <stdio.h>

static unsigned char gg[128];
#define S(x, y, v) gg[(y) * 8 + (x)] = (v)
#define G(x, y)    gg[(y) * 8 + (x)]

static void emit(const char *nm)
{
    const char *half[4] = { "tl", "tr", "bl", "br" };
    int q, r, bx, by, x, b;
    for (q = 0; q < 4; q++) {
        bx = (q & 1) ? 4 : 0; by = (q & 2) ? 8 : 0;
        printf("static const unsigned char g_%s_%s[8] = {", nm, half[q]);
        for (r = 0; r < 8; r++) {
            b = 0;
            for (x = 0; x < 4; x++) b = (b << 2) | (G(bx + x, by + r) & 3);
            printf("0x%02X%s", b, r < 7 ? "," : "");
        }
        printf("};\n");
    }
}
static void eye(void)                   /* gold bullseye: ring + white pearl */
{
    int x, y, dx, dy, r2, v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 8; x++) {
            dx = 2 * x - 7; dy = y - 8; r2 = dx * dx + dy * dy;
            v = 0;
            if (r2 > 10 && r2 <= 46) v = 3;   /* gold ring  */
            if (r2 <= 6) v = 1;               /* white pearl */
            S(x, y, v);
        }
}
static void dots(void)                  /* carved lapis tile + white quincunx studs */
{
    int x, y, v, i;
    int sx[5] = { 1, 6, 1, 6, 3 }, sy[5] = { 4, 4, 11, 11, 7 };
    for (y = 0; y < 16; y++)
        for (x = 0; x < 8; x++) {
            v = 3;                            /* lapis face */
            if (x == 0 || y == 0)  v = 1;     /* white bevel (top/left)   */
            if (x == 7 || y == 15) v = 0;     /* field shadow (bot/right) */
            S(x, y, v);
        }
    for (i = 0; i < 5; i++) { S(sx[i], sy[i], 1); if (i == 4) S(sx[i] + 1, sy[i], 1); }
    S(4, 7, 1); S(3, 8, 1); S(4, 8, 1);       /* centre stud (quincunx) */
}
int main(void)
{
    eye();  emit("eye");
    dots(); emit("dots");
    return 0;
}
