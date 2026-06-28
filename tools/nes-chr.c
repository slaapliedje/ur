/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Host tool: generate the NES / Famicom CHR-ROM (pattern table 0) for Ur.
 *
 * The NES port bypasses cc65's conio and drives the PPU directly, so it needs its
 * own CHR: an 8x8 HUD font (placed at ASCII codes, so put_str can write characters
 * straight through) plus the carved Standard-of-Ur board cells. Each board cell is
 * 2x2 tiles (16x16 px) — like the SMS/Adam — generated procedurally here: a gold
 * flower rosette, a gold bullseye eye, a white quincunx, and round two-tone tokens.
 *
 * Tiles are 2bpp: 8 bytes of bit-plane 0 then 8 bytes of plane 1; a pixel's colour
 * index (0..3) is plane1:plane0. Colours come from the per-cell palette the renderer
 * assigns via the attribute table (see src/nes/main.c).
 *
 * Emits a ca65 source (the CHARS segment) for src/nes/chr.s. Build + run:
 *   cc -o gen tools/nes-chr.c && ./gen > src/nes/chr.s
 *
 * Tile map: 0x20 blank (space/field); font at ASCII 0x21..0x5F; board cells at
 *   0xC0 rosette, 0xC4 eye, 0xC8 quincunx, 0xCC Light token, 0xD0 Dark token
 *   (each a 2x2 block: TL,TR,BL,BR).
 */
#include <stdio.h>
#include <string.h>

#define NTILES 0xD4
static unsigned char p0[NTILES][8];   /* plane 0 */
static unsigned char p1[NTILES][8];   /* plane 1 */

/* ---- 8x8 HUD font (only the glyphs Ur actually prints) -------------------- *
 * Each glyph is 8 rows of an 8-char string ('#', 'o' = colour 1; ' ' = 0). */
struct glyph { char c; const char *rows[8]; };
static const struct glyph FONT[] = {
 {'A',{"  ##    "," #  #   "," #  #   "," ####   "," #  #   "," #  #   "," #  #   ","        "}},
 {'B',{" ###    "," #  #   "," #  #   "," ###    "," #  #   "," #  #   "," ###    ","        "}},
 {'C',{"  ###   "," #   #  "," #      "," #      "," #      "," #   #  ","  ###   ","        "}},
 {'D',{" ###    "," #  #   "," #  #   "," #  #   "," #  #   "," #  #   "," ###    ","        "}},
 {'E',{" ####   "," #      "," #      "," ###    "," #      "," #      "," ####   ","        "}},
 {'F',{" ####   "," #      "," #      "," ###    "," #      "," #      "," #      ","        "}},
 {'G',{"  ###   "," #   #  "," #      "," # ##   "," #  #   "," #   #  ","  ###   ","        "}},
 {'H',{" #  #   "," #  #   "," #  #   "," ####   "," #  #   "," #  #   "," #  #   ","        "}},
 {'I',{"  ###   ","   #    ","   #    ","   #    ","   #    ","   #    ","  ###   ","        "}},
 {'J',{"   ###  ","    #   ","    #   ","    #   "," #  #   "," #  #   ","  ##    ","        "}},
 {'K',{" #  #   "," # #    "," ##     "," ##     "," # #    "," #  #   "," #  #   ","        "}},
 {'L',{" #      "," #      "," #      "," #      "," #      "," #      "," ####   ","        "}},
 {'M',{" #   #  "," ## ##  "," # # #  "," #   #  "," #   #  "," #   #  "," #   #  ","        "}},
 {'N',{" #   #  "," ##  #  "," # # #  "," #  ##  "," #   #  "," #   #  "," #   #  ","        "}},
 {'O',{"  ###   "," #   #  "," #   #  "," #   #  "," #   #  "," #   #  ","  ###   ","        "}},
 {'P',{" ###    "," #  #   "," #  #   "," ###    "," #      "," #      "," #      ","        "}},
 {'Q',{"  ###   "," #   #  "," #   #  "," #   #  "," # # #  "," #  #   ","  ## #  ","        "}},
 {'R',{" ###    "," #  #   "," #  #   "," ###    "," # #    "," #  #   "," #  #   ","        "}},
 {'S',{"  ###   "," #   #  "," #      ","  ###   ","     #  "," #   #  ","  ###   ","        "}},
 {'T',{" #####  ","   #    ","   #    ","   #    ","   #    ","   #    ","   #    ","        "}},
 {'U',{" #   #  "," #   #  "," #   #  "," #   #  "," #   #  "," #   #  ","  ###   ","        "}},
 {'V',{" #   #  "," #   #  "," #   #  "," #   #  "," #   #  ","  # #   ","   #    ","        "}},
 {'W',{" #   #  "," #   #  "," #   #  "," # # #  "," # # #  "," ## ##  "," #   #  ","        "}},
 {'X',{" #   #  "," #   #  ","  # #   ","   #    ","  # #   "," #   #  "," #   #  ","        "}},
 {'Y',{" #   #  "," #   #  ","  # #   ","   #    ","   #    ","   #    ","   #    ","        "}},
 {'Z',{" #####  ","     #  ","    #   ","   #    ","  #     "," #      "," #####  ","        "}},
 {'0',{"  ###   "," #   #  "," #  ##  "," # # #  "," ##  #  "," #   #  ","  ###   ","        "}},
 {'1',{"   #    ","  ##    ","   #    ","   #    ","   #    ","   #    ","  ###   ","        "}},
 {'2',{"  ###   "," #   #  ","     #  ","   ##   ","  #     "," #      "," #####  ","        "}},
 {'3',{" ####   ","     #  ","    #   ","   ##   ","     #  "," #   #  ","  ###   ","        "}},
 {'4',{"    #   ","   ##   ","  # #   "," #  #   "," #####  ","    #   ","    #   ","        "}},
 {'5',{" #####  "," #      "," ###    ","     #  ","     #  "," #   #  ","  ###   ","        "}},
 {'6',{"  ###   "," #      "," #      "," ###    "," #   #  "," #   #  ","  ###   ","        "}},
 {'7',{" #####  ","     #  ","    #   ","   #    ","  #     ","  #     ","  #     ","        "}},
 {'8',{"  ###   "," #   #  "," #   #  ","  ###   "," #   #  "," #   #  ","  ###   ","        "}},
 {'9',{"  ###   "," #   #  "," #   #  ","  ####  ","     #  ","     #  ","  ###   ","        "}},
 {' ',{"        ","        ","        ","        ","        ","        ","        ","        "}},
 {':',{"        ","   #    ","   #    ","        ","   #    ","   #    ","        ","        "}},
 {'!',{"   #    ","   #    ","   #    ","   #    ","   #    ","        ","   #    ","        "}},
 {'.',{"        ","        ","        ","        ","        ","   #    ","   #    ","        "}},
 {'-',{"        ","        ","        ","  ###   ","        ","        ","        ","        "}},
 {'+',{"        ","   #    ","   #    "," #####  ","   #    ","   #    ","        ","        "}},
 {'*',{"        "," # # #  ","  ###   "," #####  ","  ###   "," # # #  ","        ","        "}},
 {'>',{" #      ","  #     ","   #    ","    #   ","   #    ","  #     "," #      ","        "}},
 {'(',{"   ##   ","  #     "," #      "," #      "," #      ","  #     ","   ##   ","        "}},
 {')',{"  ##    ","    #   ","     #  ","     #  ","     #  ","    #   ","  ##    ","        "}},
 {'/',{"     #  ","     #  ","    #   ","   #    ","  #     "," #      "," #      ","        "}},
};

static void set_font(void)
{
    int g, y, x;
    for (g = 0; g < (int)(sizeof FONT / sizeof FONT[0]); g++) {
        int t = (unsigned char)FONT[g].c;
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                if (FONT[g].rows[y][x] != ' ')
                    p0[t][y] |= (unsigned char)(0x80 >> x);   /* colour 1 */
    }
}

/* ---- procedural 2x2-tile (16x16) board cells ------------------------------ */
static int B[16][16];      /* colour index 0..3 for the cell being built */

static void clear16(void) { int x, y; for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) B[y][x] = 0; }

/* pack the 16x16 B[] into 4 tiles starting at code `base` (TL,TR,BL,BR) */
static void pack(int base)
{
    int q, ox, oy, y, x, c, t;
    for (q = 0; q < 4; q++) {
        ox = (q & 1) ? 8 : 0; oy = (q & 2) ? 8 : 0; t = base + q;
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++) {
                c = B[oy + y][ox + x];
                if (c & 1) p0[t][y] |= (unsigned char)(0x80 >> x);
                if (c & 2) p1[t][y] |= (unsigned char)(0x80 >> x);
            }
    }
}

static int r2c(int x, int y) { int dx = 2*x-15, dy = 2*y-15; return dx*dx + dy*dy; }

static void cell_rosette(void)   /* gold diamond bloom (c1) + white pearl (c2) */
{
    int x, y, d, dx, dy, ax, ay, diff;
    clear16();
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) {
        d = r2c(x, y);
        if (d > 225) continue;
        if (d > 100) { dx = 2*x-15; dy = 2*y-15; ax = dx<0?-dx:dx; ay = dy<0?-dy:dy;
                       diff = ax-ay; if (diff<0) diff=-diff; if (diff < 6) continue; }
        B[y][x] = 1;
    }
    for (y = 6; y < 10; y++) for (x = 6; x < 10; x++) B[y][x] = 2;  /* pearl */
}
static void cell_eye(void)       /* gold ring (c1) + white pearl (c2) */
{
    int x, y, d;
    clear16();
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) {
        d = r2c(x, y);
        if (d <= 225 && d >= 121) B[y][x] = 1;
        if (d <= 30) B[y][x] = 2;
    }
}
static void cell_dots(void)      /* white quincunx (c1) */
{
    static const int cx[5] = {3,11,7,3,11}, cy[5] = {3,3,7,11,11};
    int i, sx, sy;
    clear16();
    for (i = 0; i < 5; i++) for (sy = 0; sy < 3; sy++) for (sx = 0; sx < 3; sx++)
        B[cy[i]+sy][cx[i]+sx] = 1;
}
static void cell_token(void)     /* round donut: body c1, pip hole c2 */
{
    int x, y, d;
    clear16();
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) {
        d = r2c(x, y);
        if (d <= 225) B[y][x] = 1;     /* body */
        if (d <= 36)  B[y][x] = 2;     /* pip */
    }
}

static void emit(void)
{
    int t, i;
    printf("; SPDX-License-Identifier: GPL-3.0-or-later\n");
    printf("; Generated by tools/nes-chr.c -- do not hand-edit.\n");
    printf("; NES CHR-ROM pattern table 0: HUD font (ASCII) + carved board cells.\n");
    printf(".segment \"CHARS\"\n");
    for (t = 0; t < NTILES; t++) {
        printf(".byte ");
        for (i = 0; i < 8; i++) printf("$%02X%s", p0[t][i], i < 7 ? "," : "");
        printf("  ; tile $%02X plane0\n", t);
        printf(".byte ");
        for (i = 0; i < 8; i++) printf("$%02X%s", p1[t][i], i < 7 ? "," : "");
        printf("\n");
    }
}

int main(void)
{
    memset(p0, 0, sizeof p0); memset(p1, 0, sizeof p1);
    set_font();
    cell_rosette(); pack(0xC0);
    cell_eye();     pack(0xC4);
    cell_dots();    pack(0xC8);
    cell_token();   pack(0xCC);   /* Light token (palette colours differ at runtime) */
    cell_token();   pack(0xD0);   /* Dark token  (same shape, different palette)      */
    emit();
    return 0;
}
