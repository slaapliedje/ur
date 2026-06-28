/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Host tool: generate the Atari (A8 / 5200) 16x16 charset token discs.
 *
 * The board is horizontal (3 rows x 8 cols), so tokens can't be PMG (a P/M player
 * is one vertical column strip). They're drawn as 2x2-char ANTIC mode-4 charset
 * discs instead: Light = white ("01" -> COLOR0) disc, Dark = green ("10" -> COLOR1)
 * disc, each with a dark centre pip ("00" field). This emits the glyph bytes (paste
 * into src/atari/atarihw.c). Build + run: cc -o gen tools/atari-token-glyphs.c && ./gen
 *
 * 8 colour-pixels wide x 16 tall, codes 0=field(00) 1=COLOR0 white 2=COLOR1 green.
 * Packed to 4 chars (TL,TR / BL,BR); each mode-4 char row = 4 px, 2 bits, MSB=px0.
 */
#include <stdio.h>

static unsigned char gg[128];
#define S(x, y, v) gg[(y) * 8 + (x)] = (v)
#define G(x, y)    gg[(y) * 8 + (x)]

static void disc(int body)
{
    int x, y, dx, dy, r2, v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 8; x++) {
            dx = 2 * x - 7; dy = y - 8; r2 = dx * dx + dy * dy;
            v = 0;
            if (r2 <= 50) v = body;   /* disc body */
            if (r2 <= 4)  v = 0;      /* dark centre pip */
            S(x, y, v);
        }
}
static void emit(const char *nm)
{
    const char *h[4] = { "tl", "tr", "bl", "br" };
    int q, r, bx, by, x, b;
    for (q = 0; q < 4; q++) {
        bx = (q & 1) ? 4 : 0; by = (q & 2) ? 8 : 0;
        printf("static const unsigned char g_%s_%s[8] = {", nm, h[q]);
        for (r = 0; r < 8; r++) {
            b = 0;
            for (x = 0; x < 4; x++) b = (b << 2) | (G(bx + x, by + r) & 3);
            printf("0x%02X%s", b, r < 7 ? "," : "");
        }
        printf("};\n");
    }
}
int main(void)
{
    disc(1); emit("tokl");   /* white Light  */
    disc(2); emit("tokd");   /* green Dark   */
    return 0;
}
