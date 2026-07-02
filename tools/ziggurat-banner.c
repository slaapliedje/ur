/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Host tool: render the Great-Ziggurat-of-Ur title scene (src/st/main.c's
 * title_scene, 24-bit, parametric size) as itch.io page art.
 *
 *   cc -O2 -o gen tools/ziggurat-banner.c -Isrc/sms
 *   ./gen banner 480 200 2 > banner.ppm     (960x400 page banner)
 *   ./gen cover  315 250 2 > cover.ppm      (630x500 cover image)
 *   magick banner.ppm docs/itch/banner.png
 *
 * Same composition as the 16-bit ports' title: gradient dusk sky, stars, setting
 * sun, mud-brick terraces in oblique projection, the grand stair, the blue-glazed
 * shrine, and the title in the shared font8.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font8.h"

typedef struct { unsigned char r, g, b; } rgb;
static rgb *img;
static int W, H;

static const rgb NIGHT  = {  4,  10,  40 };
static const rgb DUSK   = { 208, 176,  48 };
static const rgb SAND   = { 228, 204, 140 };
static const rgb BRICK  = { 148, 102,  40 };
static const rgb BRICKL = { 204, 152,  88 };
static const rgb FACE   = {  40,  62, 196 };
static const rgb HI     = { 108, 124, 248 };
static const rgb SH     = {  16,  30, 116 };
static const rgb GOLD   = { 240, 170,  48 };
static const rgb SHELL  = { 244, 228, 180 };
static const rgb WHITE  = { 250, 252, 250 };
static const rgb GREY   = { 150, 160, 160 };

static void px(int x, int y, rgb c)
{
    if (x >= 0 && x < W && y >= 0 && y < H) img[y * W + x] = c;
}
static void fr(int x, int y, int w, int h, rgb c)
{
    int xx, yy;
    for (yy = y; yy < y + h; yy++) for (xx = x; xx < x + w; xx++) px(xx, yy, c);
}
static rgb mix(rgb a, rgb b, int t, int n)
{
    rgb o;
    o.r = (unsigned char)(a.r + (b.r - a.r) * t / n);
    o.g = (unsigned char)(a.g + (b.g - a.g) * t / n);
    o.b = (unsigned char)(a.b + (b.b - a.b) * t / n);
    return o;
}
static void disc(int cx, int cy, int r, rgb c)
{
    int dx, dy;
    for (dy = -r; dy <= r; dy++) for (dx = -r; dx <= r; dx++)
        if (dx*dx + dy*dy <= r*r) px(cx + dx, cy + dy, c);
}
/* oblique box: lit front + top ledge + shaded right side + brick coursing */
static void zbox(int cx, int ybase, int w, int h, int d,
                 rgb front, rgb top, rgb side, int courses)
{
    int i;
    fr(cx - w/2, ybase - h, w, h, front);
    if (courses) for (i = 3; i < h; i += 5) fr(cx - w/2, ybase - h + i, w, 1, side);
    for (i = 1; i <= d; i++) {
        fr(cx - w/2 + i, ybase - h - i, w, 1, top);
        fr(cx + w/2 + i - 1, ybase - h - i, 1, h, side);
    }
}
static void glyph_s(int x, int y, char ch, rgb c, int s)
{
    const unsigned char *g;
    int row, col;
    if ((unsigned char)ch < FONT8_FIRST || (unsigned char)ch >= FONT8_FIRST + FONT8_COUNT) ch = ' ';
    g = &font8[((unsigned char)ch - FONT8_FIRST) * 8];
    for (row = 0; row < 8; row++)
        for (col = 0; col < 8; col++)
            if (g[row] & (0x80 >> col)) fr(x + col*s, y + row*s, s, s, c);
}
static void text_s(int x, int y, const char *t, rgb c, int s)
{
    for (; *t; t++, x += 8*s) glyph_s(x, y, *t, c, s);
}
static void text_c(int y, const char *t, rgb c, int s)   /* centred */
{
    text_s((W - (int)strlen(t) * 8 * s) / 2, y, t, c, s);
}

int main(int argc, char **argv)
{
    int scale = 2, gy, zb, cx, i, y;
    unsigned s = 0xC0FFEEu;
    int cover;
    if (argc < 5) { fprintf(stderr, "use: %s banner|cover W H scale\n", argv[0]); return 1; }
    cover = strcmp(argv[1], "cover") == 0;
    W = atoi(argv[2]); H = atoi(argv[3]); scale = atoi(argv[4]);
    img = malloc((size_t)W * H * sizeof *img);

    gy = H - (cover ? 60 : 55);            /* horizon line          */
    zb = gy + 12;                          /* ziggurat footing      */
    cx = W / 2;

    for (y = 0; y < gy; y++) fr(0, y, W, 1, mix(NIGHT, DUSK, y, gy - 1));
    for (i = 0; i < W / 6; i++) {          /* starfield (fixed LCG) */
        int sx, sy;
        s = s * 1103515245u + 12345u; sx = (int)(s >> 16) % W;
        s = s * 1103515245u + 12345u; sy = (int)(s >> 16) % (gy * 2 / 3);
        px(sx, sy, (i & 1) ? WHITE : GREY);
    }
    disc(cx + W/4 + 20, gy - 4, 20, mix(DUSK, GOLD, 1, 2));   /* sun: halo */
    disc(cx + W/4 + 20, gy - 4, 16, GOLD);
    disc(cx + W/4 + 20, gy - 4, 6,  SHELL);
    fr(0, gy, W, 1, BRICK);
    for (y = gy + 1; y < H; y++) fr(0, y, W, 1, mix(SAND, BRICK, y - gy, H - gy));

    zbox(cx, zb,      150, 24, 6, BRICKL, SAND, BRICK, 1);    /* terraces  */
    zbox(cx, zb - 24, 106, 20, 6, BRICKL, SAND, BRICK, 1);
    zbox(cx, zb - 44,  64, 16, 6, BRICKL, SAND, BRICK, 1);
    zbox(cx, zb - 60,  30, 14, 5, FACE,   HI,   SH,    0);    /* shrine    */
    fr(cx - 3, zb - 68, 6, 8, GOLD);                          /* doorway   */
    fr(cx - 8, zb - 44, 16, 44, SAND);                        /* stair     */
    for (y = zb - 42; y < zb; y += 3) fr(cx - 8, y, 16, 1, BRICK);
    fr(cx - 8, zb - 44, 1, 44, BRICK);
    fr(cx + 7, zb - 44, 1, 44, BRICK);

    if (cover) {
        text_c(12, "THE ROYAL",  GOLD, 2);
        text_c(32, "GAME OF UR", GOLD, 2);
        text_c(56, "MESOPOTAMIA - C.2600 BCE", SHELL, 1);
    } else {
        text_c(10, "THE ROYAL GAME OF UR", GOLD, 2);
        text_c(30, "MESOPOTAMIA - C.2600 BCE", SHELL, 1);
    }

    /* PPM out, nearest-neighbour scaled */
    printf("P6\n%d %d\n255\n", W * scale, H * scale);
    for (y = 0; y < H * scale; y++)
        for (i = 0; i < W * scale; i++) {
            rgb c = img[(y / scale) * W + (i / scale)];
            putchar(c.r); putchar(c.g); putchar(c.b);
        }
    return 0;
}
