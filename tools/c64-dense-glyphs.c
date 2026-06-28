/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Host tool: generate the C64 dense-board multicolor charset glyphs.
 *
 * The C64 local build (src/c64/main.c, CUSTOM_CHARSET) draws the Standard-of-Ur
 * board as 2x2 multicolor-char cells. Doing the SMS-style motif maths at runtime on
 * the 6510 made the binary overrun the charset RAM at $3800, so the glyph bytes are
 * PRECOMPUTED here on the host and pasted into main.c's `dense_chars[]`.
 *
 * Each 16x16 cell = four multicolor chars (TL,TR / BL,BR) at consecutive codes. A
 * multicolor char is 4 fat-pixels x 8 rows, 2 bits each: 00 = field ($D021 lapis),
 * 01 = body ($D022 light-blue), 10 = shadow/outline ($D023 black), 11 = the per-cell
 * colour from colour RAM (gold / white / carnelian). The motifs are built into an
 * 8x16 grid of 2-bit codes, then packed.  Build + run:  cc -o gen tools/c64-dense-glyphs.c && ./gen
 */
#include <stdio.h>

static unsigned char gg[128];          /* 8 wide x 16 tall, 2-bit codes 0..3 */
#define S(x, y, v) gg[(y) * 8 + (x)] = (v)
#define G(x, y)    gg[(y) * 8 + (x)]

/* Carved substrate: light-blue body with a black bottom/right shadow (lapis-
 * dominant — no highlight edge, so only the motif carries the "11" colour). */
static void carve(void)
{
    int x, y, v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 8; x++) { v = 1; if (x == 7 || y == 15) v = 2; S(x, y, v); }
}
static void rosette(void)               /* gold 8-point star (plus + X), clipped */
{
    int x, y, dx, dy, r2;
    carve();
    for (y = 0; y < 16; y++)
        for (x = 0; x < 8; x++) {
            dx = 2 * x - 7; dy = y - 8; r2 = dx * dx + dy * dy;
            if (r2 > 50) continue;
            if (x == 3 || x == 4 || y == 7 || y == 8 || dx + 2 * dy == 1 || dx - 2 * dy == 1)
                S(x, y, 3);
        }
}
static void eye(void)                   /* gold bullseye: ring + pearl */
{
    int x, y, dx, dy, r2;
    carve();
    for (y = 0; y < 16; y++)
        for (x = 0; x < 8; x++) {
            dx = 2 * x - 7; dy = y - 8; r2 = dx * dx + dy * dy;
            if ((r2 > 22 && r2 <= 46) || r2 <= 4) S(x, y, 3);
        }
}
static void dots(void)                  /* five-dot quincunx studs */
{
    carve();
    S(2, 4, 3); S(5, 4, 3); S(2, 11, 3); S(5, 11, 3);
    S(3, 7, 3); S(4, 7, 3); S(3, 8, 3); S(4, 8, 3);
}
static void token(void)                 /* round disc: body = "11" colour, black rim + pip */
{
    int x, y, dx, dy, r2, v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 8; x++) {
            dx = 2 * x - 7; dy = y - 8; r2 = dx * dx + dy * dy;
            v = 0;
            if (r2 <= 50) { v = 3; if (r2 > 34) v = 2; }
            if (r2 <= 3) v = 2;
            S(x, y, v);
        }
}
static void emit(const char *nm)
{
    int q, r, bx, by, x, b;
    printf("    /* %s */\n    ", nm);
    for (q = 0; q < 4; q++) {
        bx = (q & 1) ? 4 : 0; by = (q & 2) ? 8 : 0;
        for (r = 0; r < 8; r++) {
            b = 0;
            for (x = 0; x < 4; x++) b = (b << 2) | (G(bx + x, by + r) & 3);
            printf("0x%02X,", b);
        }
    }
    printf("\n");
}
int main(void)
{
    printf("static const unsigned char dense_chars[] = {\n");
    rosette(); emit("ROSE  C4-C7");
    eye();     emit("EYE   C8-CB");
    dots();    emit("DOTS  CC-CF");
    token();   emit("TOKEN D0-D3");
    printf("};\n");
    return 0;
}
