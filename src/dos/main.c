/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * MS-DOS / IBM PC platform layer — x86 real mode, the fourth CPU family.
 *
 * VGA mode 13h: 320x200, 256 colours, one byte per pixel, linear framebuffer at
 * A000:0000 — our exact shared layout size with the friendliest pixel format of
 * any port. The 256-colour DAC buys the gradient treatment the Atari TT build
 * pioneered (64-shade dusk sky, 32-shade lit cell faces, 16-shade sand) without
 * the TT's y-doubling. Sound is the PC speaker: PIT channel 2 square waves (the
 * 1-bit voice, like the Apple II port — but with a free-running timer, so no
 * cycle counting). Keyboard via DOS kbhit()/getch(); frame timing via the VGA
 * vertical-retrace flag (port 3DAh, ~70 Hz in mode 13h).
 *
 * Built with Open Watcom v2 (owcc -bdos, small model) into a real-mode MZ .exe;
 * runs in DOSBox or on anything PC-compatible with VGA. fujinet-lib has an msdos
 * target built with this same toolchain, so a FujiNet online build is a real
 * future option — this port ships local-only first, like every new port.
 */
#include <i86.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ur_game.h"          /* shared controller + plat.h + ur.h */
#include "music.h"            /* the Hurrian Hymn melody data (shared) */
#include "font8.h"            /* shared 1bpp 8x8 font (from src/sms; -I in dos.mk) */

#define SCRW 320
#define SCRH 200

/* base colours 0..15 (the STe pick of the shared lapis/gold scheme, at the VGA
 * DAC's 6 bits/channel), then the TT-style gradient ramps above them. */
enum { C_BG=0, C_SHELL, C_GOLD, C_FACE, C_HI, C_DARK, C_SH, C_WHITE, C_GREY, C_HILITE,
       C_SHADOW, C_DUSK, C_DUSK2, C_BRICK, C_BRICKL, C_SAND };
#define SKY0  16              /* 64 shades: night -> dusk amber      */
#define FACE0 80              /* 32 shades: C_HI -> C_SH (cell face) */
#define SAND0 112             /* 16 shades: sand -> brick (ground)   */

static const uint8_t base_pal[16][3] = {   /* 6-bit RGB */
    { 0,  8, 21}, {63, 59, 46}, {63, 42,  8}, { 0, 25, 46},
    {17, 42, 63}, {50,  8,  0}, { 0,  8, 25}, {63, 63, 63},
    {34, 38, 34}, { 8, 59,  8}, { 0,  4,  8}, {55, 29,  8},
    {38, 13,  4}, {46, 34,  8}, {63, 55, 21}, {63, 59, 50}
};

static uint8_t __far *fbuf;   /* A000:0000 — one byte per pixel */

static void pix(int x, int y, uint16_t c)
{ fbuf[(unsigned)y * 320u + (unsigned)x] = (uint8_t)c; }
static void frectw(int x, int y, int w, int h, uint16_t c)
{
    int yy;
    for (yy = y; yy < y + h; yy++)
        _fmemset(fbuf + (unsigned)yy * 320u + (unsigned)x, (int)c, (unsigned)w);
}
static void frect(int x, int y, int w, int h, uint16_t c) { frectw(x, y, w, h, c); }
static void clr(uint16_t c) { frectw(0, 0, SCRW, SCRH, c); }

/* filled circle + filled diamond (motifs / tokens) */
static void disc(int cx, int cy, int r, uint16_t c)
{
    int dx, dy;
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r) pix(cx + dx, cy + dy, c);
}
static void diamond(int cx, int cy, int r, uint16_t c)
{
    int dx, dy;
    for (dy = -r; dy <= r; dy++) {
        int w = r - (dy < 0 ? -dy : dy);
        for (dx = -w; dx <= w; dx++) pix(cx + dx, cy + dy, c);
    }
}

/* ---- title scene: the Great Ziggurat of Ur at dusk --------------------- *
 * Procedural, like all our art. Oblique-projection boxes: lit front face,
 * sand-lit top ledge, shaded right side. */
static void zbox(int cx, int ybase, int w, int h, int d,
                 uint16_t front, uint16_t top, uint16_t side)
{
    int i;
    frect(cx - w / 2, ybase - h, w, h, front);
    for (i = 3; i < h; i += 5)                       /* mud-brick coursing */
        frect(cx - w / 2, ybase - h + i, w, 1, side);
    for (i = 1; i <= d; i++) {
        frect(cx - w / 2 + i, ybase - h - i, w, 1, top);
        frect(cx + w / 2 + i - 1, ybase - h - i, 1, h, side);
    }
}

static void title_scene(void)
{
    static const uint16_t sx[] = { 12,40,70,95,130,183,210,245,275,300,55,118,225,290 };
    static const uint8_t  sy[] = { 30,52,24,44,58, 30,50,26,46,58,66,64,60,34 };
    int i, y;

    clr(C_BG);
    for (y = 0; y < 128; y++)                          /* 64-shade sky ramp       */
        frectw(0, y, SCRW, 1, (uint16_t)(SKY0 + y * 63 / 127));
    for (i = 0; i < (int)(sizeof sy / sizeof sy[0]); i++)
        pix(sx[i], sy[i], (i & 1) ? C_WHITE : C_GREY); /* stars                   */

    disc(262, 124, 16, C_DUSK);                        /* setting sun: halo,      */
    disc(262, 124, 13, C_GOLD);                        /*   disc,                 */
    disc(262, 124, 5,  C_SHELL);                       /*   hot core              */

    frectw(0, 128, SCRW, 1, C_BRICK);                  /* horizon line            */
    for (y = 129; y < 146; y++)                        /* 16-shade sand ramp      */
        frectw(0, y, SCRW, 1, (uint16_t)(SAND0 + (y - 129) * 15 / 16));

    /* the ziggurat: three brick terraces + the blue-glazed shrine on top */
    zbox(160, 140, 150, 24, 6, C_BRICKL, C_SAND, C_BRICK);
    zbox(160, 116, 106, 20, 6, C_BRICKL, C_SAND, C_BRICK);
    zbox(160,  96,  64, 16, 6, C_BRICKL, C_SAND, C_BRICK);
    zbox(160,  80,  30, 14, 5, C_FACE,   C_HI,   C_SH);
    frect(157, 72, 6, 8, C_GOLD);                      /* gilded doorway          */

    frect(152, 96, 16, 44, C_SAND);                    /* the grand stair         */
    for (y = 98; y < 140; y += 3) frect(152, y, 16, 1, C_BRICK);
    frect(152, 96, 1, 44, C_BRICK);
    frect(167, 96, 1, 44, C_BRICK);
}

/* font8 glyph (8x8, 1bpp) at pixel (px,py) in colour c; transparent background */
static void glyph(int px, int py, char ch, uint16_t c)
{
    const uint8_t *g;
    int row, col;
    if ((unsigned char)ch < FONT8_FIRST || (unsigned char)ch >= FONT8_FIRST + FONT8_COUNT) ch = ' ';
    g = &font8[((unsigned char)ch - FONT8_FIRST) * 8];
    for (row = 0; row < 8; row++) {
        uint8_t b = g[row];
        for (col = 0; col < 8; col++)
            if (b & (0x80 >> col)) pix(px + col, py + row, c);
    }
}
static void text(int px, int py, const char *s, uint16_t c)
{
    for (; *s; s++, px += 8) glyph(px, py, *s, c);
}
static void text_u(int px, int py, uint8_t v, uint16_t c)   /* 0..99 */
{
    char b[3]; int n = 0;
    if (v >= 10) b[n++] = (char)('0' + v / 10);
    b[n++] = (char)('0' + v % 10); b[n] = 0;
    text(px, py, b, c);
}

/* ---- board geometry (shared with every port) --------------------------- */
#define BX 32                    /* board left (px)                      */
#define BY 40                    /* board top (px)                       */
#define CELL 32                  /* cell size (px)                       */
static int cellx(int col) { return BX + col * CELL; }
static int celly(int row) { return BY + row * CELL; }

static int cell_exists(int row, int col) { return row == 1 || col <= 3 || col >= 6; }
static int pos_to_cell(uint8_t player, uint8_t pos, int *row, int *col)
{
    if (pos < 1 || pos > UR_PATH_LEN) return 0;
    if (pos <= 4)       { *row = player ? 2 : 0; *col = 4 - pos; }
    else if (pos <= 12) { *row = 1;              *col = pos - 5; }
    else                { *row = player ? 2 : 0; *col = (pos == 13) ? 7 : 6; }
    return 1;
}
static int is_rosette_cell(int row, int col)
{
    return (row != 1 && (col == 0 || col == 6)) || (row == 1 && col == 3);
}
static uint8_t count_at(uint8_t pl, uint8_t pos)
{
    uint8_t i, n = 0;
    for (i = 0; i < UR_PIECES; i++) if (ur_g.piece[pl][i] == pos) n++;
    return n;
}

/* ---- board pieces ------------------------------------------------------ */
static void draw_cell(int col, int row)
{
    int x = cellx(col), y = celly(row), cx = x + CELL/2, cy = y + CELL/2, r;
    for (r = 0; r < CELL; r++)                 /* 32-shade lit-from-top face    */
        frectw(x, y + r, CELL, 1, (uint16_t)(FACE0 + r * 31 / (CELL - 1)));
    frectw(x, y, CELL, 1, C_WHITE);            /* crisp top edge                */
    frect(x + CELL - 2, y + 1, 2, CELL - 1, C_SH);      /* right-side form      */
    frectw(x, y + CELL - 1, CELL, 1, C_SHADOW);         /* dark seat line       */
    if (is_rosette_cell(row, col)) {           /* gold flower rosette           */
        diamond(cx, cy, 11, C_GOLD);
        diamond(cx, cy, 6, C_FACE);
        disc(cx, cy, 2, C_WHITE);
    } else if (row == 1) {                      /* bullseye "eye" (shared lane)  */
        disc(cx, cy, 10, C_GOLD);
        disc(cx, cy, 6, C_FACE);
        disc(cx, cy, 2, C_GOLD);
    } else {                                    /* quincunx studs (private lanes)*/
        disc(cx, cy, 2, C_WHITE);
        disc(cx - 8, cy - 8, 2, C_WHITE); disc(cx + 8, cy - 8, 2, C_WHITE);
        disc(cx - 8, cy + 8, 2, C_WHITE); disc(cx + 8, cy + 8, 2, C_WHITE);
    }
}
static void draw_token(int col, int row, uint8_t player)
{
    int cx = cellx(col) + CELL/2, cy = celly(row) + CELL/2;
    disc(cx + 2, cy + 2, 13, C_SHADOW);                    /* offset drop shadow */
    disc(cx, cy, 12, player ? C_DARK : C_SHELL);           /* body               */
    disc(cx - 4, cy - 4, player ? 2 : 3, C_WHITE);         /* specular glint     */
    disc(cx, cy, 4,  player ? C_GOLD : C_FACE);            /* centre pip         */
}
static void draw_bead(int px, int py, uint8_t player)
{
    disc(px + 1, py + 1, 4, C_SHADOW);                     /* drop shadow        */
    disc(px, py, 4, player ? C_DARK : C_SHELL);
}

/* ---- plat.h: draw the board + HUD + status ----------------------------- */
void plat_draw(uint8_t roll, const char *msg)
{
    int row, col, i, pl, rr, cc, n;

    clr(C_BG);
    text(72, 0, "THE ROYAL GAME OF UR", C_GOLD);
    text(8, 12, "TURN:", C_WHITE);
    text(56, 12, ur_g.turn ? "DARK " : "LIGHT", ur_g.turn ? C_DARK : C_SHELL);
    text(120, 12, "ROLL:", C_WHITE);
    if (roll != UR_NO_ROLL) text_u(168, 12, roll, C_GOLD);

    /* raised-slab drop shadow: dark strips along the H silhouette's south/east
     * edges (interior strips would be covered by the neighbour cell anyway).
     * cell_exists() doesn't bound row/col, so guard the board edges explicitly. */
    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++)
            if (cell_exists(row, col)) {
                int x = cellx(col), y = celly(row);
                int s = (row == 2 || !cell_exists(row + 1, col));
                int e = (col == 7 || !cell_exists(row, col + 1));
                if (s) frect(x + 4, y + CELL, CELL - (e ? 0 : 4), 4, C_SHADOW);
                if (e) frect(x + CELL, y + 4, 4, CELL - 4, C_SHADOW);
                if (s && e) frect(x + CELL, y + CELL, 4, 4, C_SHADOW);
            }
    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++)
            if (cell_exists(row, col)) draw_cell(col, row);

    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++)
            if (pos_to_cell((uint8_t)pl, ur_g.piece[pl][i], &rr, &cc))
                draw_token(cc, rr, (uint8_t)pl);

    /* trays: Light above the board, Dark below; waiting (left) + home (right) */
    text(8, 26, "LIGHT", C_SHELL);
    for (n = 0; n < count_at(0, UR_POS_START); n++) draw_bead(56 + n*10, 28, 0);
    for (n = 0; n < ur_score(&ur_g, 0); n++)        draw_bead(200 + n*10, 28, 0);
    text(8, 142, "DARK", C_DARK);
    for (n = 0; n < count_at(1, UR_POS_START); n++) draw_bead(56 + n*10, 144, 1);
    for (n = 0; n < ur_score(&ur_g, 1); n++)        draw_bead(200 + n*10, 144, 1);

    if (msg) text(8, 158, msg, C_WHITE);
}

/* ---- input (keyboard) -------------------------------------------------- */
static uint16_t g_seed = 0xACE1u;
static int waitkey(void)                  /* block for a key; accrue RNG entropy */
{
    while (!kbhit()) g_seed += 0x101u;
    return getch() & 0xFF;                 /* no echo; extended keys = 0/0xE0 + code */
}

void    plat_wait(void) { (void)waitkey(); }
uint16_t plat_seed(void) { return g_seed; }
void    plat_animate(uint8_t player, uint8_t from, uint8_t to) { (void)player; (void)from; (void)to; }

/* ---- PC speaker (PIT channel 2) + the Hurrian Hymn ---------------------- *
 * The speaker is gated by port 61h bits 0-1; PIT channel 2 (ports 42h/43h) runs
 * square-wave mode 3 at 1193182 Hz / divisor. One voice, one volume — the same
 * 1-bit instrument as the Apple II port, minus the cycle counting. Note timing
 * via the VGA vertical-retrace flag (3DAh bit 3, ~70 Hz in mode 13h). */
static void spk_tone(uint16_t divi)
{
    outp(0x43, 0xB6);                      /* ch2, lo/hi, mode 3 (square) */
    outp(0x42, divi & 0xFF);
    outp(0x42, (divi >> 8) & 0xFF);
    outp(0x61, inp(0x61) | 0x03);          /* gate ch2 + speaker on */
}
static void spk_off(void) { outp(0x61, inp(0x61) & ~0x03); }
static void snd_silence(void) { spk_off(); }
static void vsync_wait(void)
{
    while (inp(0x3DA) & 0x08) ;            /* wait until out of retrace */
    while (!(inp(0x3DA) & 0x08)) ;         /* wait for retrace start    */
}
static void vbl(int n) { while (n-- > 0) vsync_wait(); }

/* PIT divisor = 1193182 / freq, for the hymn range B4(493.9 Hz)..A5(880 Hz). */
static const uint16_t spk_note[11] = { 2416,2280,2152,2032,1918,1810,1708,1612,1522,1437,1356 };
#define MUS_EIGHTH 15                      /* frames per eighth at ~70 Hz */

static void dos_music_note(unsigned char midi, unsigned char eighths)
{
    if (midi == MUSIC_REST) {
        spk_off();
    } else {
        uint8_t idx = (uint8_t)(midi - music_note_lo);
        if (idx > 10) idx = 10;
        spk_tone(spk_note[idx]);
    }
    vbl((int)eighths * MUS_EIGHTH);
    spk_off(); vbl(1);                     /* note-off gap (articulation) */
}
static void play_hymn(void)                /* once on the title; skippable by a key */
{
    uint16_t i;
    snd_silence();
    for (i = 0; i < ur_hymn_len; i++) {
        if (kbhit()) break;                /* key waiting -> skip (don't consume it) */
        dos_music_note(ur_hymn[i].note, ur_hymn[i].dur);
    }
    snd_silence();
}

static void sfx_tone(uint16_t divi, int v) { spk_tone(divi); vbl(v); spk_off(); }

static void sfx_roll(void)                 /* dice rattle: jittered low clatter */
{
    int i;
    for (i = 0; i < 10; i++) {
        g_seed = (uint16_t)(g_seed * 25173u + 13849u);
        spk_tone((uint16_t)(2600u + (g_seed & 0x0FFFu)));
        vbl(1);
    }
    spk_off();
}

static void sfx_for_result(const ur_move_result *res)
{
    int i;
    if (res->won)           { sfx_tone(2032,8); sfx_tone(1518,8); sfx_tone(1012,16); }
    else if (res->captured) {                   /* falling clatter (no noise channel) */
        for (i = 0; i < 8; i++) { spk_tone((uint16_t)(3600 + i * 260)); vbl(1); }
        spk_off();
    }
    else if (res->scored)   { sfx_tone(1814,7); sfx_tone(1212,12); }
    else if (res->rosette)  { sfx_tone(2032,6); sfx_tone(1623,6); sfx_tone(1355,10); }
    else                    { sfx_tone(1814,5); }
}

void    plat_roll(uint8_t roll) { (void)roll; sfx_roll(); }
void    plat_sfx_result(const ur_move_result *res) { sfx_for_result(res); }

/* Bright-green outline around a board cell — marks a legal move destination. */
static void border_cell(int col, int row, uint16_t c)
{
    int x = cellx(col), y = celly(row);
    frectw(x, y, CELL, 2, c);              /* top    */
    frectw(x, y + CELL - 2, CELL, 2, c);   /* bottom */
    frect(x, y, 2, CELL, c);               /* left   */
    frect(x + CELL - 2, y, 2, CELL, c);    /* right  */
}

/* plat.h: show the legal-move list, mark every legal destination square, and
 * return the chosen piece (-1 = none). Number keys pick directly. */
int8_t plat_choose_move(uint8_t player, uint8_t roll)
{
    uint8_t pieces[UR_PIECES], srcs[UR_PIECES], count, nsrc = 0, i, j, pos, dest;
    int k, sel, px;

    count = ur_legal_moves(&ur_g, player, roll, pieces);
    if (count == 0) return -1;
    for (i = 0; i < count; i++) {                 /* unique source squares */
        int seen = 0; pos = ur_g.piece[player][pieces[i]];
        for (j = 0; j < nsrc; j++) if (srcs[j] == pos) { seen = 1; break; }
        if (!seen) srcs[nsrc++] = pos;
    }
    px = 8;
    text(8, 172, "PICK:", C_WHITE); px = 56;
    for (i = 0; i < nsrc; i++) {                  /* "1)E>4*  2)5>9 …"      */
        pos = srcs[i]; dest = (uint8_t)(pos + roll);
        text_u(px, 172, (uint8_t)(i + 1), C_GOLD); px += (i + 1 < 10) ? 8 : 16;
        glyph(px, 172, ')', C_WHITE); px += 8;
        if (pos == UR_POS_START) { glyph(px, 172, 'E', C_SHELL); px += 8; }
        else { text_u(px, 172, pos, C_SHELL); px += (pos < 10) ? 8 : 16; }
        glyph(px, 172, '>', C_WHITE); px += 8;
        if (dest >= UR_POS_HOME) { glyph(px, 172, 'H', C_SHELL); px += 8; }
        else { text_u(px, 172, dest, C_SHELL); px += (dest < 10) ? 8 : 16; }
        if (dest < UR_POS_HOME && ur_is_rosette(dest)) { glyph(px, 172, '*', C_GOLD); px += 8; }
        else if (dest < UR_POS_HOME && ur_dest_captures(&ur_g, player, dest)) { glyph(px, 172, 'X', C_DARK); px += 8; }
        px += 8;
    }
    for (i = 0; i < nsrc; i++) {                   /* mark every legal landing square */
        int hr, hc; uint8_t d = (uint8_t)(srcs[i] + roll);
        if (d < UR_POS_HOME && pos_to_cell(player, d, &hr, &hc))
            border_cell(hc, hr, C_HILITE);
    }
    do { k = waitkey(); } while (k < '1' || k >= '1' + (int)nsrc);
    sel = k - '1';
    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (ur_g.piece[player][pieces[i]] == pos) return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

/* plat.h: choose AI difficulty (keys 1/2/3). Board is up; show a small panel. */
uint8_t plat_pick_level(void)
{
    int k;
    frectw(0, 170, SCRW, 24, C_BG);
    text(8, 172, "LEVEL:  1) EASY   2) NORMAL   3) HARD", C_WHITE);
    do { k = waitkey(); } while (k < '1' || k > '3');
    return (uint8_t)(k - '1');     /* UR_AI_EASY/NORMAL/HARD = 0/1/2 */
}

/* ---- video init + title / menu ----------------------------------------- */
static void set_mode(uint16_t m) { union REGS r; r.w.ax = m; int86(0x10, &r, &r); }
static void dacset(int i, int r, int g, int b)
{ outp(0x3C8, i); outp(0x3C9, r); outp(0x3C9, g); outp(0x3C9, b); }
static void dacramp(int i0, int n, int r0, int g0, int b0, int r1, int g1, int b1)
{
    int i;
    for (i = 0; i < n; i++)
        dacset(i0 + i, r0 + (r1-r0)*i/(n-1), g0 + (g1-g0)*i/(n-1), b0 + (b1-b0)*i/(n-1));
}

static void video_init(void)
{
    int i;
    set_mode(0x0013);                          /* VGA 320x200x256 */
    for (i = 0; i < 16; i++) dacset(i, base_pal[i][0], base_pal[i][1], base_pal[i][2]);
    dacramp(SKY0,  64,  0,  4, 17, 55, 29,  8);   /* night -> dusk amber */
    dacramp(FACE0, 32, 17, 42, 63,  0,  8, 25);   /* C_HI -> C_SH        */
    dacramp(SAND0, 16, 63, 59, 50, 46, 34,  8);   /* sand -> brick       */
    fbuf = (uint8_t __far *)MK_FP(0xA000, 0);
    snd_silence();                             /* quiet the speaker at boot */
}

static int title_menu(void)        /* returns vs_ai (1 = vs computer) */
{
    int k;
    title_scene();                 /* the Great Ziggurat of Ur at dusk */
    text(80, 8, "THE ROYAL GAME OF UR", C_GOLD);
    text(64, 20, "MESOPOTAMIA - C.2600 BCE", C_SHELL);
    text(88, 152, "1) TWO PLAYERS", C_WHITE);
    text(60, 164, "2) ONE PLAYER VS COMPUTER", C_WHITE);
    text(104, 180, "SELECT 1 OR 2:", C_SHELL);
    text(272, 192, "IBM PC", C_GREY);
    play_hymn();                   /* the Hurrian Hymn, once, skippable by a key */
    for (;;) {
        k = waitkey();
        if (k == '1') return 0;
        if (k == '2') return 1;
        if (k == 27) {             /* ESC: back to DOS (text mode restored) */
            snd_silence();
            set_mode(0x0003);
            exit(0);
        }
    }
}

int main(void)
{
    video_init();
    for (;;) {
        uint8_t winner = ur_run_game((uint8_t)title_menu());
        title_scene();             /* victory beneath the ziggurat */
        text(winner ? 120 : 116, 156, winner ? "DARK WINS!" : "LIGHT WINS!", C_GOLD);
        text(108, 176, "PRESS ANY KEY", C_SHELL);
        plat_wait();
    }
    return 0;
}
