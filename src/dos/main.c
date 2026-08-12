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
 * runs in DOSBox or on anything PC-compatible with VGA.
 *
 * FujiNet online (-DUR_ONLINE, the default build) makes DOS the FIFTH FujiNet
 * platform: fujinet-lib's msdos target (same Watcom toolchain) calls software
 * INT F5h, which the fujinet-msdos driver (FUJINET.SYS in CONFIG.SYS) bridges
 * to a FujiNet RS-232 adapter. Same N:TCP wire protocol and server as the
 * Atari/Adam/C64/Apple II. With no driver loaded the INT F5 vector check (and
 * the lib's own error paths) fail gracefully back to local play.
 */
#include <i86.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ur_game.h"          /* shared controller + plat.h + ur.h */
#include "music.h"            /* the Hurrian Hymn melody data (shared) */
#include "font8.h"            /* shared 1bpp 8x8 font (from src/sms; -I in dos.mk) */

#ifdef UR_ONLINE
#include <dos.h>              /* _dos_getvect: is an INT F5 handler installed? */
#include "proto.h"            /* the cross-platform wire protocol (same as Atari) */
#include "urnet.h"            /* N: network over INT F5 directly (see urnet.h why) */
#include "fujinet-fuji.h"     /* fuji_*_appkey: persistent profile on the FujiNet SD */
#endif

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

#ifdef UR_ONLINE
#define UR_DEFAULT_HOST "thefnords.com"   /* the Ur server; runtime-configurable (menu 5) */

/* FujiNet AppKey (persistent SD storage) for the local player profile. 0x5552='UR'.
 * Mirrors the Atari/Adam/C64/Apple II builds so a profile set on one shows on all. */
#define UR_CREATOR_ID  0x5552u
#define UR_APP_ID      0x01
#define UR_KEY_PROFILE 0x00
/* FujiNet lobby handoff: the lobby writes the chosen server URL into creator
 * 0x0001 / app 0x01 / key = our lobby appkey (UR_APPKEY=6). */
#define UR_LOBBY_CREATOR 0x0001u
#define UR_LOBBY_APP     0x01
#define UR_LOBBY_APPKEY  0x06

static char     g_name[UR_NAME_LEN + 1] = "";   /* player name + NUL; empty = unset */
static uint16_t g_wins  = 0;                     /* games won vs the computer        */
static char     g_host[33] = UR_DEFAULT_HOST;    /* server host/IP (<=32, persisted) */
static char     g_net_url[64];                   /* N:TCP://<host>:1234/             */
static char     g_top_url[64];                   /* N:HTTP://<host>:8080/top         */
#endif

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
#ifdef UR_ONLINE
static void text_u16(int px, int py, uint16_t v, uint16_t c)   /* 0..65535 */
{
    char b[6]; int n = 0, i;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (i = n - 1; i >= 0; i--, px += 8) glyph(px, py, b[i], c);
}
#endif

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
static uint8_t g_played_music = 0;
static void play_hymn(void)                /* once at boot; skippable by a key */
{
    uint16_t i;
    if (g_played_music) return;            /* not on every return to the title */
    g_played_music = 1;
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

static void sfx_capture(void)              /* falling clatter (no noise channel) */
{
    int i;
    for (i = 0; i < 8; i++) { spk_tone((uint16_t)(3600 + i * 260)); vbl(1); }
    spk_off();
}
static void sfx_score(void)   { sfx_tone(1814,7); sfx_tone(1212,12); }
static void sfx_rosette(void) { sfx_tone(2032,6); sfx_tone(1623,6); sfx_tone(1355,10); }

static void sfx_for_result(const ur_move_result *res)
{
    if (res->won)           { sfx_tone(2032,8); sfx_tone(1518,8); sfx_tone(1012,16); }
    else if (res->captured) sfx_capture();
    else if (res->scored)   sfx_score();
    else if (res->rosette)  sfx_rosette();
    else                    sfx_tone(1814,5);
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

#ifdef UR_ONLINE
/* ---- FujiNet online play (N:TCP, server-authoritative) ------------------ */
/*
 * Identical model and wire protocol to the Atari/Adam/C64/Apple II: the server
 * is authoritative, the client sends JOIN/ROLL/MOVE intents and renders the
 * STATE snapshots it sends back. fujinet-lib msdos rides software INT F5h,
 * provided by FUJINET.SYS (the fujinet-msdos RS-232 driver) — so before any
 * lib call we check that *something* hooks INT F5. On a machine without the
 * driver the vector is null (real DOS) or a dummy IRET (DOSBox); the null
 * check catches the first, and the lib's status-byte checks catch the second
 * (an IRET echoes the device byte back in AL, which is not a valid 'C'/'E'/'N'
 * result) — either way the Online option degrades to a message, never a hang.
 */

static int fn_present(void)               /* is an INT F5 handler installed? */
{
    return _dos_getvect(0xF5) != 0;
}

static char *url_append(char *d, const char *s)
{
    while (*s) *d++ = *s++;
    return d;
}

static int is_host_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

/* Build the N:TCP / N:HTTP device specs from the configured host. Ports fixed. */
static void build_urls(void)
{
    char *p;
    p = g_net_url;
    p = url_append(p, "N:TCP://");
    p = url_append(p, g_host);
    p = url_append(p, ":1234/");
    *p = 0;
    p = g_top_url;
    p = url_append(p, "N:HTTP://");
    p = url_append(p, g_host);
    p = url_append(p, ":8080/top");
    *p = 0;
}

/* Load name + wins + host from our appkey. 0 if no FujiNet/SD (keeps defaults). */
static int profile_load(void)
{
    uint8_t  buf[MAX_APPKEY_LEN + 2];
    uint16_t cnt = 0;
    unsigned char i, n;

    if (!fn_present()) return 0;
    fuji_set_appkey_details(UR_CREATOR_ID, UR_APP_ID, DEFAULT);
    if (!fuji_read_appkey(UR_KEY_PROFILE, &cnt, buf) || cnt < UR_NAME_LEN + 2)
        return 0;
    /* layout: name[UR_NAME_LEN] (NUL-padded), wins (2), hostlen (1), host[] */
    n = 0;
    for (i = 0; i < UR_NAME_LEN; i++) {
        char ch = (char)buf[i];
        if (ch == 0) break;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ')
            g_name[n++] = ch;
    }
    while (n > 0 && g_name[n - 1] == ' ') n--;
    g_name[n] = 0;
    g_wins = (uint16_t)(buf[UR_NAME_LEN] | ((uint16_t)buf[UR_NAME_LEN + 1] << 8));
    if (cnt >= UR_NAME_LEN + 3) {
        unsigned char hl = buf[UR_NAME_LEN + 2];
        if (hl > 0 && hl <= 32 && (uint16_t)(UR_NAME_LEN + 3 + hl) <= cnt) {
            for (i = 0; i < hl; i++)
                g_host[i] = (char)buf[UR_NAME_LEN + 3 + i];
            g_host[hl] = 0;
        }
    }
    return 1;
}

/* Persist name + wins + host. Silently no-ops if no FujiNet is attached. */
static void profile_save(void)
{
    uint8_t buf[UR_NAME_LEN + 3 + 32];
    unsigned char hl = 0, nl = 0, i;

    if (!fn_present()) return;
    while (g_name[nl] && nl < UR_NAME_LEN) nl++;
    for (i = 0; i < UR_NAME_LEN; i++)
        buf[i] = (i < nl) ? (uint8_t)g_name[i] : 0;
    buf[UR_NAME_LEN]     = (uint8_t)(g_wins & 0xFF);
    buf[UR_NAME_LEN + 1] = (uint8_t)(g_wins >> 8);
    while (g_host[hl] && hl < 32) hl++;
    buf[UR_NAME_LEN + 2] = hl;
    for (i = 0; i < hl; i++)
        buf[UR_NAME_LEN + 3 + i] = (uint8_t)g_host[i];
    fuji_set_appkey_details(UR_CREATOR_ID, UR_APP_ID, DEFAULT);
    fuji_write_appkey(UR_KEY_PROFILE, (uint16_t)(UR_NAME_LEN + 3 + hl), buf);
}

/* If the lobby launched us, parse the chosen server's host out of its handoff
 * AppKey (e.g. "tcp://host:1234/") into g_host. Returns 1 if one was found. */
static int lobby_host_from_appkey(void)
{
    uint8_t  buf[MAX_APPKEY_LEN + 2];
    uint16_t cnt = 0;
    unsigned char i, j, start = 0, found = 0;

    if (!fn_present()) return 0;
    fuji_set_appkey_details(UR_LOBBY_CREATOR, UR_LOBBY_APP, DEFAULT);
    if (!fuji_read_appkey(UR_LOBBY_APPKEY, &cnt, buf) || cnt == 0)
        return 0;
    for (i = 0; (uint16_t)(i + 2) < cnt; i++)
        if (buf[i] == ':' && buf[i + 1] == '/' && buf[i + 2] == '/') {
            start = (unsigned char)(i + 3); found = 1; break;
        }
    if (!found) return 0;
    j = 0;
    for (i = start; i < cnt && j < 32; i++) {
        if (buf[i] == ':' || buf[i] == '/') break;
        g_host[j++] = (char)buf[i];
    }
    if (j == 0) return 0;
    g_host[j] = 0;
    return 1;
}

/* Status line on the shared message row (works over the board + menu screens). */
static void ol_status(const char *s)
{
    frectw(0, 158, SCRW, 10, C_BG);
    text(8, 158, s, C_WHITE);
}

/* Field editor: ENTER confirms, BACKSPACE deletes. hostmode allows '.'/'-';
 * letters are upper-cased either way (the font is uppercase-only, and both DNS
 * names and player names are case-insensitive here). Saves the profile. */
static void edit_field(const char *prompt, char *dest, unsigned char maxlen,
                       int hostmode)
{
    char tmp[40];
    unsigned char len = 0, i;
    int c;

    while (dest[len] && len < maxlen) { tmp[len] = dest[len]; len++; }

    clr(C_BG);
    text(8, 8, prompt, C_GOLD);
    text(8, 24, hostmode ? "LETTERS DIGITS . -" : "A-Z 0-9 SPACE", C_GREY);
    text(8, 34, "ENTER = OK  BACKSPACE = DELETE", C_GREY);

    for (;;) {
        frectw(0, 56, SCRW, 10, C_BG);
        for (i = 0; i < len; i++) glyph(8 + i * 8, 56, tmp[i], C_WHITE);
        glyph(8 + len * 8, 56, '_', C_GOLD);
        c = waitkey();
        if (c == '\r' || c == '\n') break;
        if (c == 8) { if (len) len--; continue; }   /* backspace */
        if (len >= maxlen) continue;
        {
            char ch = (char)c;
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            if (hostmode) {
                if (is_host_char(ch)) tmp[len++] = ch;
            } else if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ') {
                tmp[len++] = ch;
            }
        }
    }
    tmp[len] = 0;
    for (i = 0; i <= len; i++) dest[i] = tmp[i];
    if (hostmode) build_urls();
    profile_save();
}

/* Fetch the compact /top leaderboard over N:HTTP and show it: a count byte then
 * up to 10 records of name[UR_NAME_LEN] + wins (uint16 LE). */
static void show_leaderboard(void)
{
    uint8_t  buf[128];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n = 0;
    unsigned char count, i, j, base;
    char name[UR_NAME_LEN + 1];
    uint16_t wins;

    clr(C_BG);
    text(8, 8, "LEADERBOARD", C_GOLD);

    if (!fn_present() || urnet_open(g_top_url, 4 /* HTTP GET */, 0) != URNET_OK) {
        text(8, 28, "COULD NOT REACH THE SERVER.", C_WHITE);
        text(8, 44, "NEEDS FUJINET.SYS + A FUJINET,", C_GREY);
        text(8, 54, "AND THE UR SERVER REACHABLE.", C_GREY);
        text(8, 190, "PRESS A KEY TO RETURN", C_SHELL);
        (void)waitkey();
        return;
    }
    for (i = 0; i < 100; i++) {
        if (urnet_status(&bw, &conn, &err) != URNET_OK) break;
        if (bw > 0) { n = urnet_read(buf, sizeof(buf)); break; }
        if (conn == 0) break;
        vbl(3);
    }
    urnet_close();

    if (n < 1) {
        text(8, 28, "NO REPLY FROM SERVER.", C_WHITE);
    } else if (buf[0] == 0) {
        text(8, 28, "NO GAMES RECORDED YET.", C_WHITE);
    } else {
        count = buf[0];
        text(8, 24, "#  NAME      WINS", C_GREY);
        for (i = 0; i < count && i < 10; i++) {
            int y = 36 + i * 10;
            base = (unsigned char)(1 + i * (UR_NAME_LEN + 2));
            if ((int16_t)(base + UR_NAME_LEN + 2) > n) break;
            for (j = 0; j < UR_NAME_LEN; j++) name[j] = (char)buf[base + j];
            name[UR_NAME_LEN] = 0;
            wins = (uint16_t)(buf[base + UR_NAME_LEN] |
                              ((uint16_t)buf[base + UR_NAME_LEN + 1] << 8));
            text_u(8, y, (uint8_t)(i + 1), C_GOLD);
            text(32, y, name, C_SHELL);
            text_u16(112, y, wins, C_WHITE);
        }
    }
    text(8, 190, "PRESS A KEY TO RETURN", C_SHELL);
    (void)waitkey();
}

/* Poll for the next STATE. 1 = got one, 0 = disconnected/error, -1 = key pressed. */
static int8_t read_state(ur_snapshot *snap)
{
    uint8_t  buf[UR_STATE_MSG_LEN];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n;

    for (;;) {
        if (kbhit()) { (void)getch(); return -1; }
        if (urnet_status(&bw, &conn, &err) != URNET_OK) return 0;
        if (bw >= UR_STATE_MSG_LEN) break;
        if (conn == 0) return 0;
        vbl(3);
    }
    n = urnet_read(buf, UR_STATE_MSG_LEN);
    if (n < (int16_t)UR_STATE_MSG_LEN) return 0;
    return ur_proto_decode_state(buf, (uint8_t)n, snap) ? 1 : 0;
}

/* Wait for the first STATE, counting down to the server's AI fallback. 1 = got a
 * snapshot, 0 = disconnected, -1 = key pressed (play the computer locally). */
static int8_t online_wait(ur_snapshot *snap)
{
    uint8_t  buf[UR_STATE_MSG_LEN];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n;
    unsigned char secs = 60, ticks = 0;

    text(8, 180, "COMPUTER JOINS IN", C_GREY);
    text_u(152, 180, secs, C_WHITE);
    for (;;) {
        if (kbhit()) { (void)getch(); return -1; }
        if (urnet_status(&bw, &conn, &err) != URNET_OK) return 0;
        if (bw >= UR_STATE_MSG_LEN) {
            n = urnet_read(buf, UR_STATE_MSG_LEN);
            return (n >= (int16_t)UR_STATE_MSG_LEN &&
                    ur_proto_decode_state(buf, (uint8_t)n, snap)) ? 1 : 0;
        }
        if (conn == 0) return 0;
        vbl(7);
        if (++ticks >= 10) {                    /* ~1 s at 70 Hz */
            ticks = 0;
            if (secs) secs--;
            frectw(152, 180, 24, 8, C_BG);
            text_u(152, 180, secs, C_WHITE);
        }
    }
}

/* Returns 1 if the player bailed out of waiting, to play the computer locally. */
static int online_game(void)
{
    ur_snapshot snap;
    uint8_t cmd[2 + UR_NAME_LEN + 2];
    int8_t picked, rc;

    clr(C_BG);
    text(72, 8, "THE ROYAL GAME OF UR", C_GOLD);

    if (!fn_present()) {
        text(8, 28, "NO FUJINET DRIVER (INT F5).", C_WHITE);
        text(8, 44, "LOAD FUJINET.SYS IN CONFIG.SYS", C_GREY);
        text(8, 54, "WITH A FUJINET RS232 ATTACHED.", C_GREY);
        text(8, 190, "PRESS A KEY TO RETURN", C_SHELL);
        (void)waitkey();
        return 0;
    }
    if (urnet_open(g_net_url, 12 /* read/write */, 0) != URNET_OK) {
        ol_status("CONNECT FAILED. KEY..."); (void)waitkey(); return 0;
    }
    urnet_write(cmd, ur_proto_join(cmd, g_name));

    text(8, 28, "CONNECTING TO:", C_WHITE);
    text(8, 38, g_host, C_SHELL);
    text(8, 60, "WAITING FOR AN OPPONENT...", C_WHITE);
    text(8, 76, "OR PRESS A KEY TO PLAY", C_GREY);
    text(8, 86, "THE COMPUTER LOCALLY", C_GREY);

    rc = online_wait(&snap);
    if (rc == -1) { urnet_close(); return 1; }
    if (rc == 0) {
        ol_status("DISCONNECTED. KEY..."); (void)waitkey();
        urnet_close(); return 0;
    }

    for (;;) {
        ur_g = snap.state;
        if (snap.flags & UR_FLAG_CAPTURED)      sfx_capture();
        else if (snap.flags & UR_FLAG_SCORED)   sfx_score();
        else if (snap.flags & UR_FLAG_ROSETTE)  sfx_rosette();

        if (snap.phase == UR_PHASE_OVER) {
            plat_draw(UR_NO_ROLL, snap.winner == (int8_t)snap.seat
                                ? "YOU WIN! KEY..." : "YOU LOSE. KEY...");
            (void)waitkey();
            break;
        }
        if (snap.state.turn != snap.seat) {
            plat_draw(snap.phase == UR_PHASE_MOVE ? snap.roll : UR_NO_ROLL,
                       "OPPONENTS TURN...");
        } else if (snap.phase == UR_PHASE_ROLL) {
            plat_draw(UR_NO_ROLL, "YOUR TURN - KEY TO ROLL");
            (void)waitkey();
            sfx_roll();
            urnet_write(cmd, ur_proto_roll(cmd));
        } else {
            plat_draw(snap.roll, (const char *)0);
            picked = plat_choose_move(snap.seat, snap.roll);
            if (picked >= 0)
                urnet_write(cmd, ur_proto_move(cmd, (unsigned char)picked));
        }
        rc = read_state(&snap);
        if (rc == -1) break;
        if (rc == 0) { ol_status("DISCONNECTED. KEY..."); (void)waitkey(); break; }
    }
    urnet_close();
    return 0;
}
#endif /* UR_ONLINE */

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

static int title_menu(void)        /* draws the title + menu; returns the key */
{
    int k;
    title_scene();                 /* the Great Ziggurat of Ur at dusk */
    text(80, 8, "THE ROYAL GAME OF UR", C_GOLD);
    text(64, 20, "MESOPOTAMIA - C.2600 BCE", C_SHELL);
#ifdef UR_ONLINE
    text(8, 148, "SERVER", C_GREY);
    text(64, 148, g_host, C_SHELL);
    if (g_name[0]) {
        text(200, 148, g_name, C_SHELL);
        glyph(200 + 8 * (int)strlen(g_name) + 8, 148, 'W', C_GREY);
        text_u16(200 + 8 * (int)strlen(g_name) + 16, 148, g_wins, C_WHITE);
    }
    text(24, 160,  "1) TWO PLAYERS", C_WHITE);
    text(24, 170,  "2) VS COMPUTER", C_WHITE);
    text(24, 180,  "3) ONLINE",      C_WHITE);
    text(184, 160, "4) SET NAME",    C_WHITE);
    text(184, 170, "5) SET HOST",    C_WHITE);
    text(184, 180, "6) TOP TEN",     C_WHITE);
    text(24, 192, "SELECT 1-6:", C_SHELL);
#else
    text(88, 152, "1) TWO PLAYERS", C_WHITE);
    text(60, 164, "2) ONE PLAYER VS COMPUTER", C_WHITE);
    text(104, 180, "SELECT 1 OR 2:", C_SHELL);
#endif
    text(272, 192, "IBM PC", C_GREY);
    play_hymn();                   /* the Hurrian Hymn, once, skippable by a key */
    k = waitkey();
    if (k == 27) {                 /* ESC: back to DOS (text mode restored) */
        snd_silence();
        set_mode(0x0003);
        exit(0);
    }
    return k;
}

/* Run a local game and show the result. ai1 = Dark is the computer (vs-AI);
 * beating the computer bumps the online build's persistent win count. */
static void run_and_show(uint8_t ai1)
{
    uint8_t winner = ur_run_game(ai1);
#ifdef UR_ONLINE
    if (ai1 && winner == 0) { g_wins++; profile_save(); }
#endif
    title_scene();                 /* victory beneath the ziggurat */
    text(winner ? 120 : 116, 156, winner ? "DARK WINS!" : "LIGHT WINS!", C_GOLD);
    text(108, 176, "PRESS ANY KEY", C_SHELL);
    plat_wait();
}

int main(void)
{
    int k;
    video_init();
#ifdef UR_ONLINE
    profile_load();                /* name/wins/host from the FujiNet appkey, if any */
    lobby_host_from_appkey();      /* launched from the lobby? use its server host   */
    build_urls();                  /* N: URLs from the resolved host                 */
#endif
    for (;;) {
        k = title_menu();
#ifdef UR_ONLINE
        if (k == '4') { edit_field("SET NAME", g_name, UR_NAME_LEN, 0); continue; }
        if (k == '5') { edit_field("SET SERVER HOST", g_host, 32, 1);   continue; }
        if (k == '6') { show_leaderboard(); continue; }
        if (k == '3') {            /* online (server-authoritative) */
            if (online_game())     /* bailed out of waiting -> play the computer */
                run_and_show(1);
            continue;
        }
#endif
        if (k == '1' || k == '2')
            run_and_show((uint8_t)(k == '2'));
    }
    return 0;
}
