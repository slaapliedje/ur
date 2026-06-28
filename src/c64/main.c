/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Commodore 64 (6510 / cc65) — Royal Game of Ur, platform layer.
 *
 * Reuses the portable core (src/common/ur) unchanged; this is the thin platform
 * layer.  Two board renderers, selected at build time:
 *
 *   default          VIC-II multicolor SPRITE tokens on the traditional
 *                    horizontal 3x8 board.  The 8 hardware sprites are reused
 *                    across the three rows by a raster-interrupt multiplexer
 *                    (src/c64/mux.s) -- 8 sprites x 3 rows = 24 token slots, and
 *                    a row holds at most 8 pieces, so it never overflows.  Tokens
 *                    are genuinely two-tone (a multicolor sprite has 4 colours):
 *                    a bone body with a brown pip (Light) and a brown body with a
 *                    bone pip (Dark), like a real Ur set.  This is the C64 colour
 *                    showcase -- see docs/future-enhancements.md.
 *
 *   -DUR_CHARSET     The known-good fallback: a vertical board drawn with a
 *                    custom charset (round disc tokens, shaped rosettes, lane
 *                    dots) coloured from colour RAM.  No sprites, no raster IRQ.
 *
 *   -DUR_ONLINE      FujiNet online play (N:TCP, server-authoritative; same wire
 *                    protocol as the Atari/Adam).  Adds the lobby/profile menu
 *                    (set name/host, leaderboard) and the appkey profile + lobby
 *                    host pickup.  Linking fujinet-lib (~8K) fills VIC bank 0, so
 *                    online builds drop the $3800 custom charset and draw board
 *                    cells as ROM colour tiles (the multicolor sprite tokens stay).
 *
 * All paths reuse the same core, SID sound, menu, and turn loop.  SID sound
 * (src/c64/sound.c) fires on the same events as the Atari/Adam builds.  Run in
 * VICE (x64sc); online needs FujiNet (-PC) + the Ur server.  cc65 2.18.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <conio.h>

#include "ur.h"
#include "ur_game.h"        /* shared local-game controller + plat.h interface */
#include "sound.h"          /* SID sound effects                                 */
#include "music.h"          /* the Hurrian Hymn title theme (shared melody)       */
#ifdef UR_ONLINE
#include "proto.h"          /* the cross-platform wire protocol (same as Atari)  */
#include "fujinet-network.h"
#include "fujinet-fuji.h"   /* fuji_*_appkey: persistent profile on the FujiNet SD */
#endif

/*
 * Custom-charset glyphs need 2K of RAM at $3800 in VIC bank 0. The local builds
 * are small enough to leave that free, but an ONLINE build also links fujinet-lib
 * (~8K), filling bank 0 (the screen must stay at $0400 for conio) -- so online
 * builds use the ROM charset and draw board cells as colour tiles instead.
 */
#ifdef UR_ONLINE
#define CUSTOM_CHARSET 0
#else
#define CUSTOM_CHARSET 1
#endif

#define POKE(addr, val)  (*(volatile unsigned char *)(addr) = (unsigned char)(val))

/* C64 colour codes (VIC-II). */
#define C_BLACK 0
#define C_WHITE 1
#define C_RED   2
#define C_CYAN  3
#define C_GREEN 5
#define C_BLUE  6
#define C_YELLOW 7
#define C_ORANGE 8
#define C_BROWN 9
#define C_LRED  10
#define C_GRAY2 12
#define C_LGREEN 13
#define C_LBLUE 14
#define C_GRAY3 15

#define COL_BG    C_BLUE     /* lapis board background */
#define COL_TITLE C_YELLOW   /* gold */
#define COL_CELL  C_GRAY2    /* lane cell */
#define COL_ROSE  C_YELLOW   /* gold rosette cell */
#define COL_LABEL C_WHITE
#define COL_LIGHT C_GRAY3    /* Light piece body: bone / cream (light grey) */
#define COL_DARK  C_BROWN    /* Dark piece body: brown -- the C64 has a real one */

#define NO_ROLL    0xFF

static uint16_t  g_seed = 0xACE1u;   /* RNG entropy (accumulated in the menu) */

#ifdef UR_ONLINE
#define UR_DEFAULT_HOST "localhost"   /* server host; runtime-configurable (menu 5) */

/* FujiNet AppKey (persistent SD storage) for the local player profile. 0x5552='UR'.
 * Mirrors the Atari/Adam builds so a profile set on one shows on all. */
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

/*
 * Board glyphs (screen codes).  With the custom charset we copy the ROM charset to
 * RAM at $3800 so conio text still works, then redefine three otherwise-unused
 * codes into a round token disc, a shaped (8-point) rosette, and a small lane dot.
 * Without it (online builds) we use ROM screen codes: a filled ball for the disc
 * and the reverse-space (solid block) as a colour tile for rosette / lane cells.
 */
#if CUSTOM_CHARSET
#define G_DISC 0xE0
#define G_ROSE 0xE1
#define G_LANE 0xE2
#define G_DIE0 0xE3                /* unmarked tetrahedral die */
#define G_DIE1 0xE4                /* marked die (apex pip)    */
#define CHARSET 0x3800u
#define D018_BOARD 0x1E            /* screen $0400, charset $3800 */

static const unsigned char g_disc[8] = { 0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const unsigned char g_die0[8] = { 0x18,0x24,0x24,0x42,0x42,0x81,0xFF,0x00 };
static const unsigned char g_die1[8] = { 0x18,0x24,0x24,0x5A,0x5A,0x81,0xFF,0x00 };
#ifdef UR_CHARSET
/* Charset fallback (vertical board): hi-res single-colour glyphs. */
static const unsigned char g_rose[8] = { 0x99,0x5A,0x3C,0xFF,0xFF,0x3C,0x5A,0x99 };
static const unsigned char g_lane[8] = { 0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00 };
#else
/* Sprite showcase: MULTICOLOR carved cells (4 double-wide pixels, 2 bits each:
 * 00=$D021 blue field, 01=$D022 light-blue, 10=$D023 black, 11=Color-RAM per cell).
 * g_lane = a beveled lapis tile (light-blue body, white top/left highlight via the
 * "11" cell colour, black bottom/right shadow); g_rose = a gold cross/flower (the
 * "11" cell colour = gold). See board_enter() for the shared colours. */
static const unsigned char g_lane[8] = { 0xFF,0xD6,0xD6,0xD6,0xD6,0xD6,0xD6,0xAA };
static const unsigned char g_rose[8] = { 0x3C,0x3C,0xFF,0xFF,0xFF,0xFF,0x3C,0x3C };
#endif

static void setup_charset(void)
{
    __asm__ ("sei");
    *(unsigned char *)0x01 = 0x33;                 /* char ROM visible at $D000 */
    memcpy((void *)CHARSET, (void *)0xD800, 2048); /* lower-case set -> RAM     */
    *(unsigned char *)0x01 = 0x37;                 /* restore I/O               */
    __asm__ ("cli");
    memcpy((void *)(CHARSET + G_DISC * 8), g_disc, 8);
    memcpy((void *)(CHARSET + G_ROSE * 8), g_rose, 8);
    memcpy((void *)(CHARSET + G_LANE * 8), g_lane, 8);
    memcpy((void *)(CHARSET + G_DIE0 * 8), g_die0, 8);
    memcpy((void *)(CHARSET + G_DIE1 * 8), g_die1, 8);
    *(unsigned char *)0xD018 = D018_BOARD;
}
#else
#define G_DISC 0x51                /* ROM filled ball (charset-build token)   */
#define G_ROSE 0xA0                /* ROM reverse-space tile (gold rosette)   */
#define G_LANE 0xA0                /* ROM reverse-space tile (grey lane cell) */
static void setup_charset(void) { }   /* ROM charset: nothing to install */
#endif

/* Re-assert the board charset after a clrscr (no-op with the ROM charset). */
#if CUSTOM_CHARSET
#define select_board_charset()  (*(unsigned char *)0xD018 = D018_BOARD)
#else
#define select_board_charset()  ((void)0)
#endif

/* Poke a glyph + colour directly to screen / colour RAM. */
static void put_glyph(unsigned char x, unsigned char y, unsigned char code,
                      unsigned char color)
{
    unsigned int off = (unsigned int)y * 40 + x;
    *(unsigned char *)(0x0400u + off) = code;
    *(unsigned char *)(0xD800u + off) = color;
}

/* Draw the four dice (defined after the board sections; called by draw_board). */
static void draw_dice(unsigned char roll);

/* Write a status message on the bottom row, clearing any previous text. */
static void status(const char *msg)
{
    cclearxy(0, 24, 40);
    textcolor(COL_LABEL);
    cputsxy(0, 24, msg);
}

/* ======================================================================== */
#ifdef UR_CHARSET
/* ---- Charset board (the known-good fallback): vertical 8x3 layout. ------ */

#define INFO_COL 18

/* Path position (1..14) -> board cell (row 1..8, col 0..2). False if off-board. */
static bool pos_to_cell(unsigned char player, unsigned char pos,
                        unsigned char *row, unsigned char *col)
{
    if (pos < 1 || pos > 14)
        return false;
    if (pos <= 4)       { *col = player ? 2 : 0; *row = (unsigned char)(5 - pos); }
    else if (pos <= 12) { *col = 1;              *row = (unsigned char)(pos - 4); }
    else                { *col = player ? 2 : 0; *row = (pos == 13) ? 8 : 7; }
    return true;
}

static bool is_rosette_cell(unsigned char row, unsigned char col)
{
    return (row == 1 && col != 1) || (row == 7 && col != 1) ||
           (row == 4 && col == 1);
}

static unsigned char cellx(unsigned char col) { return (unsigned char)(4 + col * 3); }
static unsigned char celly(unsigned char row) { return (unsigned char)(2 + row); }

static void board_setup(void) { setup_charset(); }
static void board_enter(void) { }
static void board_leave(void) { }

void plat_draw(unsigned char roll, const char *msg)
{
    unsigned char row, col, pl, i, pos, rr, cc;
    char grid[9][3];          /* 0 empty, 'L'/'D' piece, '*' rosette, '.' lane */

    bgcolor(COL_BG);
    bordercolor(COL_BG);
    clrscr();
    select_board_charset();
    textcolor(COL_TITLE); cputsxy(0, 0, "Royal Game of Ur");

    for (row = 1; row <= 8; row++)
        for (col = 0; col < 3; col++) {
            if (col == 1 || row <= 4 || row >= 7)
                grid[row][col] = is_rosette_cell(row, col) ? '*' : '.';
            else
                grid[row][col] = 0;        /* cut-away corner */
        }
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = ur_g.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                grid[rr][cc] = pl ? 'D' : 'L';
        }

    for (row = 1; row <= 8; row++)
        for (col = 0; col < 3; col++) {
            char g = grid[row][col];
            unsigned char x = cellx(col), y = celly(row);
            if (g == 0) continue;
            if (g == 'L')      put_glyph(x, y, G_DISC, COL_LIGHT); /* bone token  */
            else if (g == 'D') put_glyph(x, y, G_DISC, COL_DARK);  /* brown token */
            else if (g == '*') put_glyph(x, y, G_ROSE, COL_ROSE);  /* gold rosette */
            else               put_glyph(x, y, G_LANE, COL_CELL);  /* lane dot     */
        }

    textcolor(COL_LABEL);
    gotoxy(INFO_COL, 2); cprintf("Turn: %s", ur_g.turn ? "Dark" : "Light");
    gotoxy(INFO_COL, 4); cprintf("Light home:%u", (unsigned)ur_score(&ur_g, 0));
    gotoxy(INFO_COL, 5); cprintf("Dark home:%u ", (unsigned)ur_score(&ur_g, 1));
    if (roll != NO_ROLL) {
        textcolor(COL_TITLE); gotoxy(INFO_COL, 7); cprintf("Roll: %u  ", roll);
        draw_dice(roll);
    }
    if (msg) status(msg);
}

/* ======================================================================== */
#else
/* ---- Horizontal 3x8 board. -----------------------------------------------
 * LOCAL build (CUSTOM_CHARSET): a DENSE multicolor-charset mosaic matching the
 * SMS — chunky 2x2 carved cells, gold rosette stars, bullseye eyes, five-dot
 * quincunx studs, and round two-tone tokens, all drawn as the custom charset (no
 * sprites). ONLINE build (ROM charset): the earlier sprite-token board (kept).   */

#if CUSTOM_CHARSET
#define INFO_COL 22
#else
#define INFO_COL 28
#endif

#if CUSTOM_CHARSET
/* ---- Dense multicolor charset cells (Standard of Ur). --------------------
 * Each 16x16 board cell = four multicolor chars (TL,TR / BL,BR). A multicolor
 * char is 4 fat-pixels x 8 rows, 2 bits each: 00 = field ($D021 lapis), 01 =
 * body ($D022 light-blue), 10 = shadow/outline ($D023 black), 11 = the per-cell
 * colour from colour RAM (gold/white/red). The glyphs (ROSE/EYE/DOTS/TOKEN, 4
 * chars each at the contiguous codes 0xC4..0xD3) are PRECOMPUTED on the host
 * (scratch/gen.c, the SMS-style motif maths) and just memcpy'd in — keeping the
 * binary clear of the charset RAM at $3800. Light & Dark tokens share the TOKEN
 * shape, differing only by colour RAM (white vs carnelian). */
#define DC_ROSE 0xC4   /* gold rosette star  */
#define DC_EYE  0xC8   /* bullseye eye       */
#define DC_DOTS 0xCC   /* five-dot quincunx  */
#define DC_TOKL 0xD0   /* token shape (shell = white colour RAM)     */
#define DC_TOKD 0xD0   /* same shape (carnelian = red colour RAM)    */
#define DC_BEAD 0xD8   /* 1-char tray bead */
/* Multicolor cell colour RAM: bit 3 set = the char is multicolor; low 3 bits =
 * the "11" colour. (Bit 3 CLEAR would draw the char hi-res = wrong.) */
#define CRAM_GOLD  0x0F   /* MC, "11" = yellow (gold)    */
#define CRAM_WHITE 0x09   /* MC, "11" = white            */
#define CRAM_RED   0x0A   /* MC, "11" = red (carnelian)  */

static const unsigned char dense_chars[] = {
    /* ROSE  C4-C7 */
    0x55,0x57,0x57,0x57,0x57,0x77,0x5F,0xFF,0x56,0xD6,0xD6,0xD6,0xD6,0xD6,0xDE,0xFF,0xFF,0x57,0x5F,0x77,0x57,0x57,0x57,0xAB,0xFF,0xF6,0xDE,0xD6,0xD6,0xD6,0xD6,0xEA,
    /* EYE   C8-CB */
    0x55,0x55,0x5F,0x5F,0x7D,0x75,0x75,0x77,0x56,0x56,0xF6,0xF6,0x7E,0x5E,0x5E,0xDE,0x77,0x77,0x75,0x75,0x7D,0x5F,0x5F,0xAA,0xDE,0xDE,0x5E,0x5E,0x7E,0xF6,0xF6,0xAA,
    /* DOTS  CC-CF */
    0x55,0x55,0x55,0x55,0x5D,0x55,0x55,0x57,0x56,0x56,0x56,0x56,0x76,0x56,0x56,0xD6,0x57,0x55,0x55,0x5D,0x55,0x55,0x55,0xAA,0xD6,0x56,0x56,0x76,0x56,0x56,0x56,0xAA,
    /* TOKEN D0-D3 */
    0x00,0x02,0x0A,0x2F,0x2F,0x3F,0x3F,0xBE,0x00,0x80,0xA0,0xF8,0xF8,0xFC,0xFC,0xBE,0xBE,0xBE,0x3F,0x3F,0x2F,0x2F,0x0A,0x02,0xBE,0xBE,0xFC,0xFC,0xF8,0xF8,0xA0,0x80,
};
static const unsigned char g_bead[8] = { 0x3C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3C };

static void install_dense_glyphs(void)
{
    memcpy((void *)(CHARSET + DC_ROSE * 8), dense_chars, sizeof dense_chars);
    memcpy((void *)(CHARSET + DC_BEAD * 8), g_bead, 8);
}
#endif  /* CUSTOM_CHARSET */

/* The multiplexer (src/c64/mux.s) owns these band tables; we fill them. */
extern unsigned char band_n[3];      /* active sprite count per band  */
extern unsigned char band_y[3];      /* sprite Y register value per band */
extern unsigned char band_trig[3];   /* raster line to fire each band's IRQ */
extern unsigned char band_x[24];     /* [band*8 + i] sprite X */
extern unsigned char band_ptr[24];   /* [band*8 + i] sprite shape pointer */
extern unsigned char band_col[24];   /* [band*8 + i] sprite colour */
extern unsigned char band_en[3];     /* sprite-enable mask per band */
extern void mux_install(void);
extern void mux_stop(void);

/* Sprite shapes live in the cassette buffer (free, 64-byte aligned). */
#define SPR_LIGHT 13                 /* block 13 -> $0340 */
#define SPR_DARK  14                 /* block 14 -> $0380 */
#define SPR_ADDR(blk) ((unsigned char *)((unsigned int)(blk) * 64u))

#if !CUSTOM_CHARSET   /* sprite tokens are the ONLINE build only (local = charset) */
/* 12x21 multicolor token: 'B' body, 'P' pip, ' ' transparent. */
static const char *const g_tokmap[21] = {
    "    BBBB    ",
    "  BBBBBBBB  ",
    " BBBBBBBBBB ",
    " BBBBBBBBBB ",
    "BBBBBBBBBBBB",
    "BBBBBBBBBBBB",
    "BBBBBPPBBBBB",
    "BBBBPPPPBBBB",
    "BBBPPPPPPBBB",
    "BBBPPPPPPBBB",
    "BBBPPPPPPBBB",
    "BBBPPPPPPBBB",
    "BBBPPPPPPBBB",
    "BBBBPPPPBBBB",
    "BBBBBPPBBBBB",
    "BBBBBBBBBBBB",
    "BBBBBBBBBBBB",
    " BBBBBBBBBB ",
    " BBBBBBBBBB ",
    "  BBBBBBBB  ",
    "    BBBB    ",
};

/*
 * Build a 63-byte multicolor sprite into a sprite block.  Multicolor pixels are
 * 2 bits: 00 transparent, 01 = MC1 ($D025), 10 = the sprite's individual colour
 * ($D027+i), 11 = MC2 ($D026).  Body pixels are always 10 (the individual body
 * colour); the pip uses MC1 (brown) for Light or MC2 (bone) for Dark.
 */
static void build_token(unsigned char *dest, unsigned char pipval)
{
    unsigned char r, b, p, byte, px;
    const char *row;
    for (r = 0; r < 21; r++) {
        row = g_tokmap[r];
        for (b = 0; b < 3; b++) {
            byte = 0;
            for (p = 0; p < 4; p++) {
                char ch = row[b * 4 + p];
                if (ch == 'B')      px = 2;        /* 10 -> body (individual) */
                else if (ch == 'P') px = pipval;   /* 01 (Light) or 11 (Dark) */
                else                px = 0;        /* 00 -> transparent       */
                byte = (unsigned char)((byte << 2) | px);
            }
            dest[r * 3 + b] = byte;
        }
    }
    dest[63] = 0;
}
#endif  /* !CUSTOM_CHARSET */

/* Path position (1..14) -> board cell (row 0=Light, 1=shared, 2=Dark; col 0..7). */
static bool pos_to_cell(unsigned char player, unsigned char pos,
                        unsigned char *row, unsigned char *col)
{
    if (pos < 1 || pos > 14)
        return false;
    if (pos <= 4)       { *row = player ? 2 : 0; *col = (unsigned char)(4 - pos); }
    else if (pos <= 12) { *row = 1;              *col = (unsigned char)(pos - 5); }
    else                { *row = player ? 2 : 0; *col = (pos == 13) ? 7 : 6; }
    return true;
}

/* Rosettes: (0,0)(0,6)(2,0)(2,6) on the private rows, (1,3) shared centre. */
static bool is_rosette_cell(unsigned char row, unsigned char col)
{
    if (row == 1) return col == 3;
    return col == 0 || col == 6;
}

/* Outer rows omit cols 4-5 (the cut-away bridge); the middle row has all 8. */
static bool cell_exists(unsigned char row, unsigned char col)
{
    if (row == 1) return true;
    return col <= 3 || col >= 6;
}

#if CUSTOM_CHARSET
/* Dense layout: 2x2-char cells, adjacent rows (cols 2..17, rows 3..8). */
static unsigned char tcol(unsigned char col) { return (unsigned char)(2 + col * 2); }
static unsigned char trow(unsigned char row) { return (unsigned char)(3 + row * 2); }
#else
/* Online (sprite) layout: 1-char cells spread for the raster multiplexer. */
static unsigned char tcol(unsigned char col) { return (unsigned char)(2 + col * 3); }
static unsigned char trow(unsigned char row) { return (unsigned char)(3 + row * 7); }
/* Centre the 24px sprite over the 8px cell glyph (see mux.s for the geometry). */
static unsigned char spr_x(unsigned char col) { return (unsigned char)(32 + col * 24); }
#endif

#if CUSTOM_CHARSET
/* ---- Dense local build: no sprites/multiplexer; tokens are charset cells. -- */
static void board_setup(void) { setup_charset(); install_dense_glyphs(); }
static void board_enter(void)
{
    /* Standard-of-Ur multicolor palette: 00 field = lapis ($D021), 01 body =
     * light blue, 10 shadow/outline = black; per-cell "11" = gold/white/red. */
    *(unsigned char *)0xD021 = C_BLUE;
    *(unsigned char *)0xD022 = C_LBLUE;
    *(unsigned char *)0xD023 = C_BLACK;
    *(unsigned char *)0xD016 |= 0x10;          /* MC char mode ON */
}
static void board_leave(void)
{
    *(unsigned char *)0xD016 &= (unsigned char)~0x10;  /* hi-res text for the menu */
    *(unsigned char *)0xD021 = COL_BG;
}

/* Place a 16x16 cell (four consecutive charset codes) with one colour RAM value. */
static void put_cell(unsigned char x, unsigned char y, unsigned char first,
                     unsigned char color)
{
    put_glyph(x, y, first, color);
    put_glyph((unsigned char)(x + 1), y, (unsigned char)(first + 1), color);
    put_glyph(x, (unsigned char)(y + 1), (unsigned char)(first + 2), color);
    put_glyph((unsigned char)(x + 1), (unsigned char)(y + 1), (unsigned char)(first + 3), color);
}

/* Draw on-board pieces as two-tone disc cells (shell = white, dark = red). */
static void draw_pieces(void)
{
    unsigned char pl, i, pos, row, col;
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = ur_g.piece[pl][i];
            if (!pos_to_cell(pl, pos, &row, &col))
                continue;
            put_cell(tcol(col), trow(row), pl ? DC_TOKD : DC_TOKL,
                     pl ? CRAM_RED : CRAM_WHITE);
        }
}
#else
/* ---- Online build: multicolor SPRITE tokens via the raster multiplexer. ---- */
static void sprite_hw_init(void)
{
    POKE(0xD01C, 0xFF);   /* all sprites multicolor      */
    POKE(0xD025, C_BROWN);/* MC1 shared: brown (Light pip / Dark body uses indiv) */
    POKE(0xD026, C_GRAY3);/* MC2 shared: bone  (Dark pip)                          */
    POKE(0xD017, 0x00);   /* no Y expand                 */
    POKE(0xD01D, 0x00);   /* no X expand                 */
    POKE(0xD01B, 0x00);   /* sprites in front of the board */
    POKE(0xD010, 0x00);   /* all sprite X < 256          */
    POKE(0xD015, 0x00);   /* start with sprites disabled */

    build_token(SPR_ADDR(SPR_LIGHT), 1);   /* pip = MC1 (brown) */
    build_token(SPR_ADDR(SPR_DARK),  3);   /* pip = MC2 (bone)  */

    /* Fixed per-band raster geometry (rows ~3/10/17, 56 lines apart). */
    band_y[0] = 69;  band_trig[0] = 55;
    band_y[1] = 125; band_trig[1] = 111;
    band_y[2] = 181; band_trig[2] = 167;
}

static void board_setup(void) { setup_charset(); sprite_hw_init(); }
static void board_enter(void)
{
    band_en[0] = band_en[1] = band_en[2] = 0;  /* no stray tokens until drawn */
    mux_install();
}
static void board_leave(void) { mux_stop(); }

/* Populate the multiplexer's per-row sprite tables from the game state. */
static void draw_pieces(void)
{
    unsigned char idx[3];
    unsigned char b, pl, i, pos, row, col, k;

    idx[0] = idx[1] = idx[2] = 0;
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = ur_g.piece[pl][i];
            if (!pos_to_cell(pl, pos, &row, &col))
                continue;                       /* off-board (start/home) */
            b = row;
            k = idx[b];
            if (k >= 8) continue;               /* can't happen (<=8 per row) */
            band_x[b * 8 + k]   = spr_x(col);
            band_ptr[b * 8 + k] = pl ? SPR_DARK : SPR_LIGHT;
            band_col[b * 8 + k] = pl ? COL_DARK : COL_LIGHT;
            idx[b] = (unsigned char)(k + 1);
        }
    for (b = 0; b < 3; b++) {
        band_n[b]  = idx[b];
        band_en[b] = (unsigned char)((1u << idx[b]) - 1u);
    }
}
#endif  /* CUSTOM_CHARSET */

/* Cell colour RAM. With the custom charset the cells are MULTICOLOR (bit 3 set =
 * MC; low 3 bits = the "11" colour): gold rosette, white-highlight lapis tile.
 * Online uses the ROM charset, so plain single colours. */
#if CUSTOM_CHARSET
#define CRAM_ROSE 0x0F   /* MC + "11" = gold (7)  */
#define CRAM_LANE 0x09   /* MC + "11" = white (1) */
#else
#define CRAM_ROSE COL_ROSE
#define CRAM_LANE COL_CELL
#endif

/* Pieces still waiting to enter (off-board at the start). */
static unsigned char count_start(unsigned char pl)
{
    unsigned char i, n = 0;
    for (i = 0; i < UR_PIECES; i++)
        if (ur_g.piece[pl][i] == UR_POS_START)
            n++;
    return n;
}

#if CUSTOM_CHARSET
/* Dense mosaic board: 2x2 carved cells — gold rosette stars at the 5 rosette
 * squares, bullseye eyes down the shared lane, quincunx studs on the private
 * lanes — with shell/carnelian tray beads above & below. */
static void draw_static_board(void)
{
    unsigned char row, col, k, n;
    bgcolor(COL_BG);
    bordercolor(COL_BG);
    clrscr();
    select_board_charset();
    textcolor(COL_TITLE); cputsxy(2, 0, "Royal Game of Ur");

    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++) {
            if (!cell_exists(row, col))
                continue;
            if (is_rosette_cell(row, col))
                put_cell(tcol(col), trow(row), DC_ROSE, CRAM_GOLD);
            else if (row == 1)
                put_cell(tcol(col), trow(row), DC_EYE,  CRAM_GOLD);
            else
                put_cell(tcol(col), trow(row), DC_DOTS, CRAM_WHITE);
        }

    /* Trays: Light above the board (white beads), Dark below (red beads);
     * waiting clustered left, borne-off "home" to the right of them. */
    n = count_start(0);
    for (k = 0; k < n; k++) put_glyph((unsigned char)(2 + k),  2, DC_BEAD, CRAM_WHITE);
    n = (unsigned char)ur_score(&ur_g, 0);
    for (k = 0; k < n; k++) put_glyph((unsigned char)(11 + k), 2, DC_BEAD, CRAM_WHITE);
    n = count_start(1);
    for (k = 0; k < n; k++) put_glyph((unsigned char)(2 + k),  10, DC_BEAD, CRAM_RED);
    n = (unsigned char)ur_score(&ur_g, 1);
    for (k = 0; k < n; k++) put_glyph((unsigned char)(11 + k), 10, DC_BEAD, CRAM_RED);
}
#else
static void draw_static_board(void)
{
    unsigned char row, col, k, n;
    bgcolor(COL_BG);
    bordercolor(COL_BG);
    clrscr();
    select_board_charset();
    textcolor(COL_TITLE); cputsxy(2, 0, "Royal Game of Ur");

    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++) {
            if (!cell_exists(row, col))
                continue;
            if (is_rosette_cell(row, col))
                put_glyph(tcol(col), trow(row), G_ROSE, CRAM_ROSE);
            else
                put_glyph(tcol(col), trow(row), G_LANE, CRAM_LANE);
        }

    /* Off-board trays (white/black discs; the multicolor sprite tokens carry the
     * bone/brown colour). */
    n = count_start(0);
    for (k = 0; k < n; k++)              put_glyph((unsigned char)(2 + k),  1, G_DISC, C_WHITE);
    n = (unsigned char)ur_score(&ur_g, 0);
    for (k = 0; k < n; k++)              put_glyph((unsigned char)(37 - k), 1, G_DISC, C_WHITE);
    n = count_start(1);
    for (k = 0; k < n; k++)              put_glyph((unsigned char)(2 + k),  21, G_DISC, C_BLACK);
    n = (unsigned char)ur_score(&ur_g, 1);
    for (k = 0; k < n; k++)              put_glyph((unsigned char)(37 - k), 21, G_DISC, C_BLACK);
}
#endif  /* CUSTOM_CHARSET */

void plat_draw(unsigned char roll, const char *msg)
{
    draw_static_board();
    draw_pieces();

    textcolor(COL_LABEL);
    gotoxy(INFO_COL, 2); cprintf("Turn: %s", ur_g.turn ? "Dark" : "Light");
    gotoxy(INFO_COL, 4); cprintf("Light: %u", (unsigned)ur_score(&ur_g, 0));
    gotoxy(INFO_COL, 5); cprintf("Dark: %u ", (unsigned)ur_score(&ur_g, 1));
    if (roll != NO_ROLL) {
        textcolor(COL_TITLE); gotoxy(INFO_COL, 7); cprintf("Roll: %u  ", roll);
        draw_dice(roll);
    }
    if (msg) status(msg);
}

#endif  /* UR_CHARSET */
/* ======================================================================== */

/* List legal moves (deduped by source) in the panel, read a 1..N choice. */
int8_t plat_choose_move(unsigned char player, unsigned char roll)
{
    unsigned char pieces[UR_PIECES], srcs[UR_PIECES];
    unsigned char count, nsrc, i, j, pos, dest, sel;
    bool seen;
    int c;

    count = ur_legal_moves(&ur_g, player, roll, pieces);
    if (count == 0)
        return -1;

    nsrc = 0;
    for (i = 0; i < count; i++) {
        pos = ur_g.piece[player][pieces[i]];
        seen = false;
        for (j = 0; j < nsrc; j++)
            if (srcs[j] == pos) { seen = true; break; }
        if (!seen)
            srcs[nsrc++] = pos;
    }

    textcolor(COL_TITLE); gotoxy(INFO_COL, 7); cprintf("Roll: %u  ", roll);
    textcolor(COL_LABEL);
    for (i = 0; i < nsrc; i++) {
        pos = srcs[i];
        dest = (unsigned char)(pos + roll);
        gotoxy(INFO_COL, (unsigned char)(9 + i));
        if (pos == UR_POS_START) cprintf("%u) ent->%u", i + 1, dest);
        else                     cprintf("%u) %u->%u", i + 1, pos, dest);
        if (dest == UR_POS_HOME)        cputs(" H");
        else if (ur_is_rosette(dest))   cputs(" *");
    }
    cclearxy(0, 24, 40);
    textcolor(COL_TITLE); cputsxy(0, 24, "Pick a move:");

    do { c = cgetc(); } while (c < '1' || c >= (int)('1' + nsrc));
    sel = (unsigned char)(c - '1');

    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (ur_g.piece[player][pieces[i]] == pos)
            return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

/* plat.h: confirm wait, dice roll (sound + tumble), result sound, RNG entropy.
 * The shared controller (ur_game.c) owns the turn loop and calls these; the C64 has
 * no token glide, so plat_animate is a no-op. */
void plat_wait(void) { cgetc(); }
void plat_sfx_result(const ur_move_result *res) { sfx_for_result(res); }
uint16_t plat_seed(void) { return g_seed; }
void plat_animate(unsigned char player, unsigned char from, unsigned char to)
{ (void)player; (void)from; (void)to; }
/* plat_roll (below) needs dice_tumble, which is defined after this point. */

/* Wait n video frames via the KERNAL jiffy ($A2), which keeps advancing under the
 * sprite multiplexer (band 0 chains to the KERNAL IRQ). Shared by the dice tumble
 * and the online network polling. */
static void wait_frames(unsigned char n)
{
    unsigned char prev = *(volatile unsigned char *)0x00A2;
    while (n) {
        unsigned char now = *(volatile unsigned char *)0x00A2;
        if (now != prev) { prev = now; n--; }
    }
}

#if CUSTOM_CHARSET
/* Four tetrahedral dice on the HUD (row 8): marked = apex pip; count = the roll. */
static void draw_dice(unsigned char roll)
{
    unsigned char i;
    for (i = 0; i < 4; i++)
        put_glyph((unsigned char)(INFO_COL + i), 8,
                  (unsigned char)((i < roll) ? G_DIE1 : G_DIE0), COL_TITLE);
}

/* Rattle the dice through random faces, then settle on the roll. */
static void dice_tumble(unsigned char roll)
{
    unsigned char t, i, r;
    for (t = 0; t < 8; t++) {
        r = *(volatile unsigned char *)0xD012;   /* raster line = cheap entropy */
        for (i = 0; i < 4; i++)
            put_glyph((unsigned char)(INFO_COL + i), 8,
                      (unsigned char)((r & (1u << i)) ? G_DIE1 : G_DIE0), COL_TITLE);
        wait_frames(3);
    }
    draw_dice(roll);
}
#else
static void draw_dice(unsigned char roll) { (void)roll; }   /* online: number only */
#define dice_tumble(r) ((void)0)
#endif

/* plat.h: roll = roll sound + the dice tumble (a no-op on the online build, where
 * `roll` is then unused). */
void plat_roll(unsigned char roll) { (void)roll; sfx_roll(); dice_tumble(roll); }

/* plat.h: choose the AI difficulty (keyboard 1/2/3). */
uint8_t plat_pick_level(void)
{
    int c;
    clrscr();
    textcolor(COL_TITLE); cputsxy(0, 2, "Difficulty:");
    textcolor(COL_LABEL);
    cputsxy(2, 5, "1) Easy");
    cputsxy(2, 6, "2) Normal");
    cputsxy(2, 7, "3) Hard");
    textcolor(COL_TITLE); cputsxy(0, 9, "Select (1-3):");
    do { c = cgetc(); } while (c < '1' || c > '3');
    return (uint8_t)(c - '1');
}

/* ======================================================================== */
#ifdef UR_ONLINE
/* ---- FujiNet online play (N:TCP, server-authoritative) ----------------- */
/*
 * Identical model and wire protocol to the Atari/Adam: the server is
 * authoritative, the client sends JOIN/ROLL/MOVE intents and renders the STATE
 * snapshots it sends back.  The board renderer (sprite or charset) is reused as
 * is.  Tested end-to-end needs FujiNet (-PC) + the Ur server; VICE has no FujiNet
 * emulation, so locally we can only confirm it builds, boots, and fails the
 * network calls gracefully.  The CHARSET=1 build runs online with no raster IRQ,
 * so it is the safer choice if the sprite build's IEC/raster timing ever fights.
 */

static char *url_append(char *d, const char *s)
{
    while (*s) *d++ = *s++;
    return d;
}

static bool is_host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '-';
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

/* Load name + wins + host from our appkey. False if no FujiNet/SD (keeps defaults). */
static bool profile_load(void)
{
    uint8_t  buf[MAX_APPKEY_LEN + 2];
    uint16_t cnt = 0;
    unsigned char i, n;

    fuji_set_appkey_details(UR_CREATOR_ID, UR_APP_ID, DEFAULT);
    if (!fuji_read_appkey(UR_KEY_PROFILE, &cnt, buf) || cnt < UR_NAME_LEN + 2)
        return false;
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
    return true;
}

/* Persist name + wins + host. Silently no-ops if no FujiNet is attached. */
static void profile_save(void)
{
    uint8_t buf[UR_NAME_LEN + 3 + 32];
    unsigned char hl = 0, nl = 0, i;
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
 * AppKey (e.g. "tcp://host:1234/") into g_host. Returns true if one was found. */
static bool lobby_host_from_appkey(void)
{
    uint8_t  buf[MAX_APPKEY_LEN + 2];
    uint16_t cnt = 0;
    unsigned char i, j, start = 0;
    bool found = false;

    fuji_set_appkey_details(UR_LOBBY_CREATOR, UR_LOBBY_APP, DEFAULT);
    if (!fuji_read_appkey(UR_LOBBY_APPKEY, &cnt, buf) || cnt == 0)
        return false;
    for (i = 0; (uint16_t)(i + 2) < cnt; i++)
        if (buf[i] == ':' && buf[i + 1] == '/' && buf[i + 2] == '/') {
            start = (unsigned char)(i + 3); found = true; break;
        }
    if (!found) return false;
    j = 0;
    for (i = start; i < cnt && j < 32; i++) {
        if (buf[i] == ':' || buf[i] == '/') break;
        g_host[j++] = (char)buf[i];
    }
    if (j == 0) return false;
    g_host[j] = 0;
    return true;
}

/* Simple conio field editor: RETURN confirms, DEL backspaces. hostmode allows
 * '.'/'-' (else A-Z/0-9/space, letters upper-cased). Saves the profile. */
static void edit_field(const char *prompt, char *dest, unsigned char maxlen,
                       bool hostmode)
{
    char tmp[40];
    unsigned char len = 0, i;
    int c;

    while (dest[len] && len < maxlen) { tmp[len] = dest[len]; len++; }

    bgcolor(COL_BG); bordercolor(COL_BG); clrscr();
    select_board_charset();
    textcolor(COL_TITLE); cputsxy(0, 0, prompt);
    textcolor(COL_LABEL);
    cputsxy(0, 2, hostmode ? "letters digits . -" : "A-Z 0-9 space");
    cputsxy(0, 3, "RETURN = ok   DEL = back");

    for (;;) {
        gotoxy(0, 6);
        for (i = 0; i < len; i++) cputc(tmp[i]);
        cputc(' ');
        cclearxy((unsigned char)(len + 1), 6, (unsigned char)(maxlen - len + 1));
        c = cgetc();
        if (c == '\r' || c == '\n') break;
        if (c == 20 || c == 8) { if (len) len--; continue; }   /* DEL / backspace */
        if (len >= maxlen) continue;
        if (hostmode) {
            if (is_host_char((char)c)) tmp[len++] = (char)c;
        } else {
            char ch = (char)c;
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ')
                tmp[len++] = ch;
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

    bgcolor(COL_BG); bordercolor(COL_BG); clrscr();
    select_board_charset();
    textcolor(COL_TITLE); cputsxy(0, 0, "Leaderboard");
    textcolor(COL_LABEL);

    if (network_init() != FN_ERR_OK ||
        network_open(g_top_url, 4 /* HTTP GET */, 0) != FN_ERR_OK) {
        cputsxy(0, 3, "Could not reach the server.");
        cputsxy(0, 5, "Needs FujiNet and the Ur server");
        cputsxy(0, 6, "web port (8080) reachable.");
        cputsxy(0, 24, "Press a key to return");
        cgetc();
        return;
    }
    for (i = 0; i < 100; i++) {
        if (network_status(g_top_url, &bw, &conn, &err) != FN_ERR_OK) break;
        if (bw > 0) { n = network_read(g_top_url, buf, sizeof(buf)); break; }
        if (conn == 0) break;
        wait_frames(3);
    }
    network_close(g_top_url);

    if (n < 1) {
        cputsxy(0, 3, "No reply from server.");
    } else if (buf[0] == 0) {
        cputsxy(0, 3, "No games recorded yet.");
    } else {
        count = buf[0];
        cputsxy(0, 2, "#  NAME      WINS");
        for (i = 0; i < count; i++) {
            base = (unsigned char)(1 + i * (UR_NAME_LEN + 2));
            if ((int16_t)(base + UR_NAME_LEN + 2) > n) break;
            for (j = 0; j < UR_NAME_LEN; j++) name[j] = (char)buf[base + j];
            name[UR_NAME_LEN] = 0;
            wins = (uint16_t)(buf[base + UR_NAME_LEN] |
                              ((uint16_t)buf[base + UR_NAME_LEN + 1] << 8));
            gotoxy(0, (unsigned char)(4 + i));
            cprintf("%-2u %-8s  %u", i + 1, name, wins);
        }
    }
    cputsxy(0, 24, "Press a key to return");
    cgetc();
}

/* Poll for the next STATE. 1 = got one, 0 = disconnected/error, -1 = key pressed. */
static int8_t read_state(ur_snapshot *snap)
{
    uint8_t  buf[UR_STATE_MSG_LEN];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n;

    for (;;) {
        if (kbhit()) { cgetc(); return -1; }
        if (network_status(g_net_url, &bw, &conn, &err) != FN_ERR_OK) return 0;
        if (bw >= UR_STATE_MSG_LEN) break;
        if (conn == 0) return 0;
        wait_frames(3);
    }
    n = network_read(g_net_url, buf, UR_STATE_MSG_LEN);
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

    gotoxy(0, 22); cprintf("Computer joins in %2u ", secs);
    for (;;) {
        if (kbhit()) { cgetc(); return -1; }
        if (network_status(g_net_url, &bw, &conn, &err) != FN_ERR_OK) return 0;
        if (bw >= UR_STATE_MSG_LEN) {
            n = network_read(g_net_url, buf, UR_STATE_MSG_LEN);
            return (n >= (int16_t)UR_STATE_MSG_LEN &&
                    ur_proto_decode_state(buf, (uint8_t)n, snap)) ? 1 : 0;
        }
        if (conn == 0) return 0;
        wait_frames(6);
        if (++ticks >= 10) {
            ticks = 0;
            if (secs) secs--;
            gotoxy(0, 22); cprintf("Computer joins in %2u ", secs);
        }
    }
}

/* Returns true if the player bailed out of waiting to play the computer locally. */
static bool online_game(void)
{
    ur_snapshot snap;
    uint8_t cmd[2 + UR_NAME_LEN + 2];
    int8_t picked, rc;

    bgcolor(COL_BG); bordercolor(COL_BG); clrscr();
    select_board_charset();
    textcolor(COL_TITLE); cputsxy(0, 0, "The Royal Game of Ur");
    textcolor(COL_LABEL);

    if (network_init() != FN_ERR_OK) {
        status("network_init failed. Key..."); cgetc(); return false;
    }
    if (network_open(g_net_url, OPEN_MODE_RW, 0) != FN_ERR_OK) {
        status("Connect failed. Key..."); cgetc(); return false;
    }
    network_write(g_net_url, cmd, ur_proto_join(cmd, g_name));

    cputsxy(0, 2, "Connecting to:");
    cputsxy(0, 3, g_host);
    cputsxy(0, 6, "Waiting for an opponent...");
    cputsxy(0, 8, "or press a key to play the computer");

    rc = online_wait(&snap);
    if (rc == -1) { network_close(g_net_url); return true; }
    if (rc == 0) {
        status("Disconnected. Key..."); cgetc();
        network_close(g_net_url); return false;
    }

    for (;;) {
        ur_g = snap.state;
        if (snap.flags & UR_FLAG_CAPTURED)      sfx_capture();
        else if (snap.flags & UR_FLAG_SCORED)   sfx_score();
        else if (snap.flags & UR_FLAG_ROSETTE)  sfx_rosette();

        if (snap.phase == UR_PHASE_OVER) {
            plat_draw(NO_ROLL, snap.winner == (int8_t)snap.seat
                                ? "You win! Key..." : "You lose. Key...");
            cgetc();
            break;
        }
        if (snap.state.turn != snap.seat) {
            plat_draw(snap.phase == UR_PHASE_MOVE ? snap.roll : NO_ROLL,
                       "Opponent's turn...");
        } else if (snap.phase == UR_PHASE_ROLL) {
            plat_draw(NO_ROLL, "Your turn - key to roll");
            cgetc();
            sfx_roll();
            network_write(g_net_url, cmd, ur_proto_roll(cmd));
        } else {
            plat_draw(snap.roll, (const char *)0);
            picked = plat_choose_move(snap.seat, snap.roll);
            if (picked >= 0)
                network_write(g_net_url, cmd, ur_proto_move(cmd, (unsigned char)picked));
        }
        rc = read_state(&snap);
        if (rc == -1) break;
        if (rc == 0) { status("Disconnected. Key..."); cgetc(); break; }
    }
    network_close(g_net_url);
    return false;
}

#endif /* UR_ONLINE */
/* ======================================================================== */

/* Run a local game (via the shared controller) and show the result. ai1 = Dark is
 * the computer (vs-AI); otherwise hot-seat. Beating the computer bumps the win count. */
static void run_and_show(bool ai1)
{
    unsigned char winner = ur_run_game(ai1 ? 1 : 0);
    if (ai1) {
#ifdef UR_ONLINE
        if (winner == 0) { g_wins++; profile_save(); }   /* record the win */
#endif
        plat_draw(NO_ROLL, winner == 0 ? "You win! Key..." : "You lose. Key...");
    } else {
        plat_draw(NO_ROLL, winner == 0 ? "Light wins! Key..." : "Dark wins! Key...");
    }
    cgetc();
}

/* Title decoration: a board-motif band (gold rosette flowers + beveled lapis tiles)
 * under the title. Multicolor mode is safe on the menu — all menu/HUD text uses
 * colours < 8, which stay hi-res. (No-op on the online ROM-charset build.) */
static void title_banner(void)
{
#if CUSTOM_CHARSET && !defined(UR_CHARSET)
    unsigned char x, rose;
    *(unsigned char *)0xD022 = C_LBLUE;
    *(unsigned char *)0xD023 = C_BLACK;
    *(unsigned char *)0xD016 |= 0x10;            /* MC char mode for the band */
    /* A continuous carved board-strip: beveled lapis tiles with a gold rosette
     * flower every fifth cell — previews the board's look across the title. */
    for (x = 3; x <= 36; x++) {
        rose = (unsigned char)(((x - 3) % 5) == 0);
        put_glyph(x, 3, (unsigned char)(rose ? G_ROSE : G_LANE),
                        (unsigned char)(rose ? CRAM_ROSE : CRAM_LANE));
    }
#endif
}

/* Showcase the C64's signature on the title: a row of two-tone token sprites
 * (bone Light, brown Dark — sprite colours, so the real bone/brown the char board
 * can't show). Shapes/MC colours come from sprite_hw_init (board_setup). */
static void title_sprites(void)
{
#ifndef UR_CHARSET
    static const unsigned char sx[4] = { 116, 152, 188, 224 };
    unsigned char i;
    *(unsigned char *)0x07F8 = SPR_LIGHT;
    *(unsigned char *)0x07F9 = SPR_DARK;
    *(unsigned char *)0x07FA = SPR_LIGHT;
    *(unsigned char *)0x07FB = SPR_DARK;
    for (i = 0; i < 4; i++) {
        *(unsigned char *)(0xD000u + i * 2)     = sx[i];   /* X */
        *(unsigned char *)(0xD000u + i * 2 + 1) = 150;     /* Y (mid-screen) */
    }
    *(unsigned char *)0xD027 = COL_LIGHT;   /* sprite 0 bone  */
    *(unsigned char *)0xD028 = COL_DARK;    /* sprite 1 brown */
    *(unsigned char *)0xD029 = COL_LIGHT;   /* sprite 2 bone  */
    *(unsigned char *)0xD02A = COL_DARK;    /* sprite 3 brown */
    *(unsigned char *)0xD010 = 0x00;        /* all sprite X < 256 */
    *(unsigned char *)0xD015 = 0x0F;        /* enable sprites 0-3 */
#endif
}

/* Title music: the Hurrian Hymn, played once at boot. Skippable — returns the
 * moment a key is waiting (left in the buffer for the menu's cgetc), so the player
 * can go straight to a mode. (c64_music_note scales eighth-ticks to a ~110bpm
 * tempo internally — a stately pace for the oldest written melody.) */
static bool g_played_music = false;
static void play_hymn(void)
{
    uint16_t i;
    if (g_played_music) return;       /* only on the first title (not every return) */
    g_played_music = true;
    for (i = 0; i < ur_hymn_len; i++) {
        if (kbhit()) return;          /* skip; key left for the menu */
        c64_music_note(ur_hymn[i].note, ur_hymn[i].dur);
    }
    snd_silence();
}

int main(void)
{
    unsigned char key;

    bordercolor(COL_BG);
    bgcolor(COL_BG);
    clrscr();                 /* let conio initialise the screen first */
    board_setup();            /* charset (+ sprite shapes on the sprite build) */

#if UR_SNDTEST   /* one-off: play every SFX in sequence (verify the SID) */
    clrscr();
    textcolor(COL_TITLE); cputsxy(0, 0, "Sound test");
    sfx_roll(); sfx_capture(); sfx_rosette(); sfx_score(); sfx_win();
    for (;;) { }
#endif

#ifdef UR_ONLINE
    profile_load();             /* name/wins/host from the FujiNet appkey, if any */
    lobby_host_from_appkey();   /* launched from the lobby? use its server host   */
    build_urls();               /* N: URLs from the resolved host                 */
#endif

    for (;;) {
        clrscr();
        title_banner();          /* carved board-strip band under the title (row 3) */
        title_sprites();         /* two-tone token sprites mid-screen (the showcase) */
        textcolor(COL_TITLE); cputsxy(10, 0, "The Royal Game of Ur");
        textcolor(COL_LABEL); cputsxy(5, 1, "Ur - Mesopotamia - c.2600 BCE");
#ifdef UR_ONLINE
        gotoxy(0, 3); cprintf("Server: %s", g_host);
        if (g_name[0]) { gotoxy(0, 4); cprintf("Player %s  Wins %u", g_name, g_wins); }
        cputsxy(0, 6,  "1) Two players");
        cputsxy(0, 7,  "2) One player vs computer");
        cputsxy(0, 8,  "3) Online");
        cputsxy(0, 9,  "4) Set name");
        cputsxy(0, 10, "5) Set server host");
        cputsxy(0, 11, "6) Leaderboard");
        textcolor(COL_TITLE); cputsxy(0, 13, "Select (1-6):");
#else
        cputsxy(0, 5, "1) Two players");
        cputsxy(0, 6, "2) One player vs computer");
        textcolor(COL_TITLE); cputsxy(0, 8, "Select (1-2):");
#endif

        /* With thanks to the scholar who reconstructed the rules. */
        textcolor(COL_LABEL);
        cputsxy(0, 22, "Rules deciphered by Dr Irving Finkel,");
        cputsxy(0, 23, "British Museum - with thanks.");

        play_hymn();              /* the Hurrian Hymn (once at boot, skippable) */

        /* Seed the RNG from how long the player takes to choose. */
        while (!kbhit()) g_seed++;
        key = (unsigned char)cgetc();
        *(unsigned char *)0xD015 = 0;   /* hide the title tokens before the choice */

#ifdef UR_ONLINE
        if (key == '4') { edit_field("Set name", g_name, UR_NAME_LEN, false); continue; }
        if (key == '5') { edit_field("Set server host", g_host, 32, true); continue; }
        if (key == '6') { show_leaderboard(); continue; }

        if (key == '3') {                 /* online (server-authoritative) */
            board_enter();
            if (online_game())            /* bailed out of waiting -> play the computer */
                run_and_show(true);
            board_leave();
            continue;
        }
#endif
        if (key != '1' && key != '2')
            continue;

        board_enter();                    /* sprite build: start the raster multiplexer */
        run_and_show(key == '2');           /* 1 = hot-seat, 2 = vs computer */
        board_leave();                    /* sprite build: stop the multiplexer */
    }
    return 0;
}
