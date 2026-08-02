/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Sega Mega Drive / Genesis (68000 / SGDK) — Royal Game of Ur, offline ROM.
 *
 * The console sibling of the SMS showpiece: the same "Standard of Ur" treatment
 * (lapis field, gold, shell-white; carved beveled stone cells, 8-point rosettes,
 * bullseye eyes, quincunx studs) on the authentic horizontal H-board, but with the
 * Genesis VDP's two tile planes doing it properly — the carved board is drawn ONCE
 * on plane B, resting tokens are transparent-cornered tiles on plane A floating
 * over the stonework, and the moving piece glides as a 16x16 hardware sprite.
 * 9-bit colour, H40 (320x224, 40x28 tiles), plus the SGDK font for text with a
 * two-palette white/gold ink trick (font glyphs use colour index 15).
 *
 * All art is generated procedurally at boot (the SMS 16x16 colour-grid builders,
 * repacked for the Genesis's linear 4bpp tiles) — no image files, no rescomp
 * resources. Sound is the Genesis's SN76489 PSG — the very same chip as the SMS —
 * via sound.c. Control pad input (D-pad + A/B/C). No FujiNet (cartridge console).
 * Reuses src/common via the shared controller (ur_game.c). Build:
 * makefiles/genesis.mk -> build/genesis/ur.bin (SGDK 2.11). See src/genesis/CLAUDE.md.
 *
 * NOTE: <genesis.h> must come BEFORE ur.h — SGDK typedefs `bool` (u8) without a
 * stdbool guard, so including <stdbool.h> first would break SGDK's typedef.
 */
#include <genesis.h>

/* SGDK's types.h remaps the <stdint.h> type names as macros (int8_t -> s8 etc.)
 * assuming the real header never follows. The shared core uses the real one, so
 * drop the remaps before including it (u8/s16/... stay available). */
#undef uint8_t
#undef int8_t
#undef uint16_t
#undef int16_t
#undef uint32_t
#undef int32_t
#undef size_t
#undef ptrdiff_t

#include "ur.h"
#include "ur_game.h"        /* shared local-game controller + plat.h interface */
#include "sound.h"
#include "music.h"          /* the Hurrian Hymn melody (shared) */

/* ---- palette (Standard of Ur materials) -------------------------------- *
 * PAL0 = board/text palette; PAL1 = sprite tokens + the gold text ink. The SGDK
 * font draws its glyphs with colour index 15, so index 15 of the *text palette*
 * is the ink: PAL0[15] = white, PAL1[15] = gold, switched per-string with
 * VDP_setTextPalette() (the Genesis twin of the SMS BKG_ATTR_SPRPAL trick). */
enum {
    C_FIELD = 0,    /* lapis field / backdrop (index 0 = transparent on plane A) */
    C_WHITE,        /* 1 - white               */
    C_FACE,         /* 2 - cell face (lapis)   */
    C_HI,           /* 3 - bevel highlight     */
    C_SH,           /* 4 - bevel shadow        */
    C_GOLD,         /* 5 - gold                */
    C_SHELL,        /* 6 - shell / cream       */
    C_RED,          /* 7 - carnelian           */
    C_GREY,         /* 8 - token shadow        */
    C_DGOLD,        /* 9 - dark gold           */
    C_GREEN2,       /* 10 - highlight cell face (green)  */
    C_GRNHI,        /* 11 - highlight bevel highlight    */
    C_GRNSH         /* 12 - highlight bevel shadow       */
};
static const u16 pal_board[16] = {
    RGB24_TO_VDPCOLOR(0x102040),    /* 0  field: deep lapis backdrop */
    RGB24_TO_VDPCOLOR(0xF8F8F8),    /* 1  white                      */
    RGB24_TO_VDPCOLOR(0x2850A0),    /* 2  cell face lapis            */
    RGB24_TO_VDPCOLOR(0x78A0E0),    /* 3  bevel highlight            */
    RGB24_TO_VDPCOLOR(0x081830),    /* 4  bevel shadow               */
    RGB24_TO_VDPCOLOR(0xE0A020),    /* 5  gold                       */
    RGB24_TO_VDPCOLOR(0xF0E0C0),    /* 6  shell / cream              */
    RGB24_TO_VDPCOLOR(0xB03020),    /* 7  carnelian red              */
    RGB24_TO_VDPCOLOR(0x687078),    /* 8  grey                       */
    RGB24_TO_VDPCOLOR(0x907010),    /* 9  dark gold                  */
    RGB24_TO_VDPCOLOR(0x30A048),    /* 10 green face: move-dest tint */
    RGB24_TO_VDPCOLOR(0x80D890),    /* 11 green bevel highlight      */
    RGB24_TO_VDPCOLOR(0x104820),    /* 12 green bevel shadow         */
    0, 0,
    RGB24_TO_VDPCOLOR(0xF8F8F8)     /* 15 text ink: WHITE            */
};
/* sprite/gold-ink palette: token colours for the gliding piece + gold at 15 */
enum { S_GOLD = 1, S_SHELL, S_LAPIS, S_HI, S_SH, S_GREY, S_WHITE };
static const u16 pal_spr[16] = {
    0,                              /* 0  transparent               */
    RGB24_TO_VDPCOLOR(0xE0A020),    /* 1  gold                      */
    RGB24_TO_VDPCOLOR(0xF0E0C0),    /* 2  shell                     */
    RGB24_TO_VDPCOLOR(0x2850A0),    /* 3  lapis                     */
    RGB24_TO_VDPCOLOR(0x78A0E0),    /* 4  highlight                 */
    RGB24_TO_VDPCOLOR(0x081830),    /* 5  shadow                    */
    RGB24_TO_VDPCOLOR(0x687078),    /* 6  grey                      */
    RGB24_TO_VDPCOLOR(0xF8F8F8),    /* 7  white                     */
    0, 0, 0, 0, 0, 0, 0,
    RGB24_TO_VDPCOLOR(0xE0A020)     /* 15 text ink: GOLD            */
};

#define INK_WHITE PAL0
#define INK_GOLD  PAL1

/* tile allocation (from TILE_USER_INDEX; the SGDK font lives at TILE_FONT_INDEX).
 * Each board cell is 16x16 = 4 tiles. */
#define T_CELL   (TILE_USER_INDEX + 0)    /* 4: plain carved cell            */
#define T_ROSE   (TILE_USER_INDEX + 4)    /* 4: gold 8-point rosette flower  */
#define T_DOTS   (TILE_USER_INDEX + 8)    /* 4: five-dot quincunx (lanes)    */
#define T_EYE    (TILE_USER_INDEX + 12)   /* 4: bullseye "eye" (shared lane) */
#define T_TOKL   (TILE_USER_INDEX + 16)   /* 4: 16x16 shell token (plane A)  */
#define T_TOKD   (TILE_USER_INDEX + 20)   /* 4: 16x16 lapis token (plane A)  */
#define T_TRYL   (TILE_USER_INDEX + 24)   /* 1: 8x8 shell tray bead          */
#define T_TRYD   (TILE_USER_INDEX + 25)   /* 1: 8x8 lapis tray bead          */
#define T_SPRL   (TILE_USER_INDEX + 26)   /* 4: shell token, sprite order    */
#define T_SPRD   (TILE_USER_INDEX + 30)   /* 4: lapis token, sprite order    */
/* green-tinted copies of the three motif cells — the move-destination highlight
 * (carved stone recoloured green, the gold motif kept on top). */
#define T_GROSE  (TILE_USER_INDEX + 34)   /* 4: green-tinted rosette  */
#define T_GDOTS  (TILE_USER_INDEX + 38)   /* 4: green-tinted quincunx */
#define T_GEYE   (TILE_USER_INDEX + 42)   /* 4: green-tinted eye      */

static u16 ink = INK_WHITE;         /* current text ink (a palette line) */
static void set_ink(u16 p) { ink = p; }

/* ---- procedural tiles: a 16x16 colour grid packed into linear 4bpp ------ *
 * Same builders as the SMS port, repacked: a Genesis tile is 8 u32 rows, one
 * nibble per pixel, leftmost pixel in the high nibble (linear, not planar). */
static u8  grid[256];               /* 16x16 colour indices (0..15) */
static u32 tbuf[8];                 /* one packed 4bpp tile         */

/* Pack the 8x8 region of `grid` at (sx,sy) into a tile and load it into VRAM. */
static void load_quad(u16 sx, u16 sy, u16 tno)
{
    u16 r, c;
    u32 row;
    for (r = 0; r < 8; r++) {
        row = 0;
        for (c = 0; c < 8; c++)
            row |= (u32)(grid[((sy + r) << 4) + sx + c] & 0x0F) << ((7 - c) << 2);
        tbuf[r] = row;
    }
    VDP_loadTileData(tbuf, tno, 1, CPU);
}
/* Load the 16x16 grid as four tiles in BG order (TL, TR, BL, BR). */
static void load_cell16(u16 first)
{
    load_quad(0, 0, first);
    load_quad(8, 0, first + 1);
    load_quad(0, 8, first + 2);
    load_quad(8, 8, first + 3);
}
/* Sprite copy: Genesis sprite tiles are COLUMN-major (TL, BL, TR, BR). */
static void load_cell16_spr(u16 first)
{
    load_quad(0, 0, first);
    load_quad(0, 8, first + 1);
    load_quad(8, 0, first + 2);
    load_quad(8, 8, first + 3);
}

/* Fill `grid` with the plain carved cell (base for the decorated cells). */
static void grid_carved(void)
{
    u16 x, y;
    u8 v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            v = C_FACE;
            if (x >= 14 || y >= 14) v = C_SH;     /* bottom/right shadow */
            if (x <= 1  || y <= 1)  v = C_HI;     /* top/left highlight  */
            grid[(y << 4) + x] = v;
        }
}

static void build_rosette(void)             /* 8-point gold star on lapis */
{
    s16 x, y, dx, dy, r2;
    grid_carved();
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            dx = 2 * x - 15; dy = 2 * y - 15;
            r2 = dx * dx + dy * dy;
            /* bars + diagonals through centre, clipped to a radius -> 8 points */
            if ((x == 7 || x == 8 || y == 7 || y == 8 || dx == dy || dx == -dy)
                && r2 <= 150)
                grid[(y << 4) + x] = C_GOLD;
            if (r2 <= 12) grid[(y << 4) + x] = C_SHELL;   /* shell pearl centre */
        }
}

/* Stamp a small filled diamond of colour `c` centred at (cx,cy), radius ~rad. */
static void stamp_dot(s16 cx, s16 cy, s16 rad, u8 c)
{
    s16 x, y, dx, dy;
    for (y = cy - rad; y <= cy + rad; y++)
        for (x = cx - rad; x <= cx + rad; x++)
            if (x >= 0 && x < 16 && y >= 0 && y < 16) {
                dx = x - cx; if (dx < 0) dx = -dx;
                dy = y - cy; if (dy < 0) dy = -dy;
                if (dx + dy <= rad) grid[(y << 4) + x] = c;
            }
}

/* Five-dot quincunx — shell studs at centre + corners, echoing the dotted
 * squares of the real Standard of Ur board. */
static void build_dots(void)
{
    grid_carved();
    stamp_dot(4, 4, 1, C_SHELL);  stamp_dot(11, 4, 1, C_SHELL);
    stamp_dot(4, 11, 1, C_SHELL); stamp_dot(11, 11, 1, C_SHELL);
    stamp_dot(7, 7, 2, C_SHELL);  stamp_dot(7, 7, 1, C_GOLD);
}

/* Bullseye "eye" — concentric gold ring with a shell pearl centre, for the
 * shared middle lane so it reads distinctly from the private rows. */
static void build_eye(void)
{
    s16 x, y, dx, dy, r2;
    grid_carved();
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            dx = 2 * x - 15; dy = 2 * y - 15;
            r2 = dx * dx + dy * dy;
            if (r2 > 60 && r2 <= 120) grid[(y << 4) + x] = C_GOLD;   /* ring  */
            else if (r2 <= 14)        grid[(y << 4) + x] = C_SHELL;  /* pearl */
        }
}

/* A shaded round token in a 16x16 cell: outline ring around a body with a
 * top-left sheen, bottom-right shade, and a centre pip. Corners are colour 0 —
 * transparent on plane A and for sprites, so the carved stone shows around the
 * disc (the plane-B board needs no redraw when a token lands or leaves). */
static void build_token16(u8 body, u8 hi, u8 sh, u8 pip, u8 ring)
{
    s16 x, y, dx, dy, r2;
    u8 v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            dx = 2 * x - 15; dy = 2 * y - 15;
            r2 = dx * dx + dy * dy;
            v = 0;                                /* outside = transparent */
            if (r2 <= 215) {
                v = ring;                         /* outer rim */
                if (r2 <= 150) {
                    v = body;
                    if (dx + dy <= -8) v = hi;    /* top-left sheen    */
                    if (dx + dy >=  12) v = sh;   /* bottom-right edge */
                }
            }
            if (r2 <= 16) v = pip;                /* centre pip */
            grid[(y << 4) + x] = v;
        }
}

/* An 8x8 tray bead (single tile) with an outline ring. */
static void build_bead8(u8 body, u8 hi, u8 sh, u8 ring, u16 tno)
{
    s16 x, y, dx, dy, r2;
    u8 v;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            dx = 2 * x - 7; dy = 2 * y - 7;
            r2 = dx * dx + dy * dy;
            v = 0;
            if (r2 <= 49) {
                v = ring;
                if (r2 <= 28) {
                    v = body;
                    if (dx + dy <= -4) v = hi;
                    if (dx + dy >=  6) v = sh;
                }
            }
            grid[(y << 4) + x] = v;
        }
    load_quad(0, 0, tno);
}

/* Recolour a just-built motif cell's carved STONE to green (face/highlight/
 * shadow), keeping the gold/shell motif on top — the move-destination tint. */
static void greenify(void)
{
    u16 i;
    for (i = 0; i < 256; i++) {
        if      (grid[i] == C_FACE) grid[i] = C_GREEN2;
        else if (grid[i] == C_HI)   grid[i] = C_GRNHI;
        else if (grid[i] == C_SH)   grid[i] = C_GRNSH;
    }
}

/* ---- positioned tiles / text ------------------------------------------- */
static void put_cell(VDPPlane plane, u16 x, u16 y, u16 first)
{
    VDP_setTileMapXY(plane, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, first),     x,     y);
    VDP_setTileMapXY(plane, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, first + 1), x + 1, y);
    VDP_setTileMapXY(plane, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, first + 2), x,     y + 1);
    VDP_setTileMapXY(plane, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, first + 3), x + 1, y + 1);
}
static void put_tile_a(u16 x, u16 y, u16 tno)
{
    VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, tno), x, y);
}

static void put_str(u16 x, u16 y, const char *s)
{
    VDP_setTextPalette(ink);
    VDP_drawText(s, x, y);
}
static void put_ch(u16 x, u16 y, char c)
{
    char b[2];
    b[0] = c; b[1] = 0;
    put_str(x, y, b);
}
static void put_u(u16 x, u16 y, u8 v)
{
    char buf[4];
    s16 i = 3;
    buf[3] = 0;
    do { buf[--i] = (char)('0' + v % 10); v = (u8)(v / 10); } while (v && i > 0);
    put_str(x, y, &buf[i]);
}

/* ---- input: control pad, release-then-press (one tap = one action) ------ *
 * The Adam/SMS pad pattern: wait for all-released, then take the next press —
 * robust and gives one action per tap. JOY state refreshes inside
 * SYS_doVBlankProcess(), so every poll loop vsyncs. */
#define BTN_FIRE (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START)
#define BTN_ANY  (BTN_FIRE | BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT)

static u16 g_seed = 0xACE1u;        /* RNG entropy (accumulated while waiting) */

static u16 wait_press(void)
{
    u16 now;
    while (JOY_readJoypad(JOY_1) & BTN_ANY) {         /* wait for release */
        g_seed++;
        SYS_doVBlankProcess();
    }
    for (;;) {                                        /* wait for a press */
        g_seed += 0x101u;                             /* fold in wait time -> plat_seed() */
        now = JOY_readJoypad(JOY_1) & BTN_ANY;
        if (now) return now;
        SYS_doVBlankProcess();
    }
}

/* ---- layout: horizontal H-board, 3 rows x 8 cols, 16x16 cells ----------- *
 * H40 = 40x28 tiles (320x224). The SMS full-screen layout, given breathing
 * room: board centred at cols 12..27, rows 11..16. */
#define BX 12           /* board origin (tiles) */
#define BY 11
#define TITLE_X 10
#define TITLE_Y 1
#define HUD_Y 4
#define HUD_TURN_X 14
#define HUD_ROLL_X 22
#define HUD_ROLLV_X 28
#define LTRAY_Y 8
#define DTRAY_Y 19
#define TRAY_WX 12      /* waiting-bead start col */
#define TRAY_HX 28      /* home-bead start col    */
#define LBL_X 5
#define LIST_X 2
#define LIST_Y 20
#define MSG_X 2
#define MSG_Y 27        /* bottom row — clear of a full 7-entry move list */

static u16 cellx(u8 col) { return (u16)(BX + ((u16)col << 1)); }
static u16 celly(u8 row) { return (u16)(BY + ((u16)row << 1)); }

/* The 20-square H-shape: left 4-block (cols 0..3) + right 2-block (cols 6..7)
 * joined by the shared middle row (row 1, all cols). */
static bool cell_exists(u8 row, u8 col)
{
    return row == 1 || col <= 3 || col >= 6;
}
/* path position -> board (row,col). Light lane = row 0, Dark = row 2, shared = 1. */
static bool pos_to_cell(u8 player, u8 pos, u8 *row, u8 *col)
{
    if (pos < 1 || pos > UR_PATH_LEN) return FALSE;
    if (pos <= 4)       { *row = player ? 2 : 0; *col = (u8)(4 - pos); }   /* entry */
    else if (pos <= 12) { *row = 1;              *col = (u8)(pos - 5); }   /* shared */
    else                { *row = player ? 2 : 0; *col = (pos == 13) ? 7 : 6; }  /* exit */
    return TRUE;
}
static bool is_rosette_cell(u8 row, u8 col)
{
    return (row != 1 && (col == 0 || col == 6)) || (row == 1 && col == 3);
}

static u8 count_at(u8 pl, u8 pos)
{
    u8 i, n = 0;
    for (i = 0; i < UR_PIECES; i++)
        if (ur_g.piece[pl][i] == pos) n++;
    return n;
}

/* ---- sprite: the gliding token (one 16x16 hardware sprite) -------------- */
static void sprite_park(void)
{
    VDP_setSpriteFull(0, -32, -32, SPRITE_SIZE(2, 2),
                      TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, T_SPRL), 0);
    VDP_updateSprites(1, CPU);
}
static void sprite_show(s16 x, s16 y, u16 base)
{
    VDP_setSpriteFull(0, x, y, SPRITE_SIZE(2, 2),
                      TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, base), 0);
    VDP_updateSprites(1, CPU);
}

/* ---- video ---------------------------------------------------------------- */
static bool board_shown = FALSE;    /* the carved board is on plane B */

static void video_init(void)
{
    VDP_setScreenWidth320();                /* H40 */
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setTextPlane(BG_A);
    VDP_setBackgroundColor(0);              /* PAL0[0] = lapis field */

    PAL_setPalette(PAL0, pal_board, CPU);
    PAL_setPalette(PAL1, pal_spr, CPU);

    /* procedurally build + load the board art */
    grid_carved();   load_cell16(T_CELL);
    build_rosette(); load_cell16(T_ROSE);
    build_dots();    load_cell16(T_DOTS);
    build_eye();     load_cell16(T_EYE);
    /* Light = shell disc (grey rim); Dark = lapis disc with a shell rim + gold
     * pip so it reads clearly against the lapis stonework. */
    build_token16(C_SHELL, C_WHITE, C_GREY, C_SH,   C_GREY);  load_cell16(T_TOKL);
    build_token16(C_FACE,  C_HI,    C_SH,   C_GOLD, C_SHELL); load_cell16(T_TOKD);
    build_bead8(C_SHELL, C_WHITE, C_GREY, C_GREY,  T_TRYL);
    build_bead8(C_FACE,  C_HI,    C_SH,   C_SHELL, T_TRYD);
    /* sprite copies (sprite-palette colour indices, column-major tile order) */
    build_token16(S_SHELL, S_WHITE, S_GREY, S_SH,   S_GREY);  load_cell16_spr(T_SPRL);
    build_token16(S_LAPIS, S_HI,    S_SH,   S_GOLD, S_SHELL); load_cell16_spr(T_SPRD);
    /* green-tinted highlight cells (motif kept visible on the green stone) */
    build_rosette(); greenify(); load_cell16(T_GROSE);
    build_dots();    greenify(); load_cell16(T_GDOTS);
    build_eye();     greenify(); load_cell16(T_GEYE);

    sprite_park();
}

static void screen_clear(void)
{
    VDP_clearPlane(BG_A, TRUE);
}

#define NO_ROLL UR_NO_ROLL

static void draw_tray(u16 x, u16 y, u8 n, u16 tile)
{
    u8 i;
    for (i = 0; i < n; i++) put_tile_a((u16)(x + i), y, tile);
}

/* plat.h: draw the board + HUD + message for the active game. The carved board
 * lives on plane B and is drawn once; every call rebuilds plane A (tokens,
 * trays, HUD, text) with the display blanked so the redraw never tears. */
void plat_draw(uint8_t roll, const char *msg)
{
    u8 row, col, pl, i, pos, rr, cc;

    VDP_setEnable(FALSE);
    sprite_park();
    screen_clear();

    if (!board_shown) {
        VDP_clearPlane(BG_B, TRUE);
        /* carved, inlaid cells (skip the H-shape cut-aways): gold rosettes at
         * the 5 rosette squares, bullseye eyes down the shared lane, five-dot
         * quincunx on the private lanes. */
        for (row = 0; row < 3; row++)
            for (col = 0; col < 8; col++) {
                u16 base;
                if (!cell_exists(row, col)) continue;
                if (is_rosette_cell(row, col)) base = T_ROSE;
                else if (row == 1)             base = T_EYE;
                else                           base = T_DOTS;
                put_cell(BG_B, cellx(col), celly(row), base);
            }
        board_shown = TRUE;
    }

    set_ink(INK_GOLD);
    put_str(TITLE_X, TITLE_Y, "THE ROYAL GAME OF UR");
    set_ink(INK_WHITE);
    put_str(8, HUD_Y, "Turn:");
    put_str(HUD_TURN_X, HUD_Y, ur_g.turn ? "DARK " : "LIGHT");
    put_str(HUD_ROLL_X, HUD_Y, "Roll:");
    if (roll != NO_ROLL) put_u(HUD_ROLLV_X, HUD_Y, roll);

    /* on-board tokens: shaded discs on plane A, stone showing at the corners */
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = ur_g.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                put_cell(BG_A, cellx(cc), celly(rr), pl ? T_TOKD : T_TOKL);
        }

    /* trays: Light above the board, Dark below; waiting (left) + home (right) */
    put_str(LBL_X, LTRAY_Y, "Light");
    draw_tray(TRAY_WX, LTRAY_Y, count_at(0, UR_POS_START), T_TRYL);
    draw_tray(TRAY_HX, LTRAY_Y, ur_score(&ur_g, 0),        T_TRYL);
    put_str(LBL_X, DTRAY_Y, "Dark");
    draw_tray(TRAY_WX, DTRAY_Y, count_at(1, UR_POS_START), T_TRYD);
    draw_tray(TRAY_HX, DTRAY_Y, ur_score(&ur_g, 1),        T_TRYD);

    if (msg) put_str(MSG_X, MSG_Y, msg);
    VDP_setEnable(TRUE);
}

/* ---- token glide animation (hardware sprite) ---------------------------- */
static u16 base_for(u8 player) { return player ? T_SPRD : T_SPRL; }
/* the green-tinted twin of the cell motif — the move-destination highlight */
static u16 green_base_for(u8 row, u8 col)
{
    if (is_rosette_cell(row, col)) return T_GROSE;
    if (row == 1)                  return T_GEYE;
    return T_GDOTS;
}
/* pixel position of a path cell (pos 0 = a tray slot, pos > 14 = home tray) */
static void cell_px(u8 player, u8 pos, s16 *px, s16 *py)
{
    u8 r, c;
    if (pos == 0 || pos > UR_PATH_LEN) {
        *px = (s16)((pos == 0 ? TRAY_WX : TRAY_HX) * 8);
        *py = (s16)((player ? DTRAY_Y : LTRAY_Y) * 8);
        return;
    }
    pos_to_cell(player, pos, &r, &c);
    *px = (s16)(cellx(c) * 8);
    *py = (s16)(celly(r) * 8);
}
static void glide(s16 x0, s16 y0, s16 x1, s16 y1, u16 base)
{
    s16 x = x0, y = y0;
    for (;;) {
        sprite_show(x, y, base);
        SYS_doVBlankProcess();
        if (x == x1 && y == y1) break;
        if (x < x1) { x += 4; if (x > x1) x = x1; } else if (x > x1) { x -= 4; if (x < x1) x = x1; }
        if (y < y1) { y += 4; if (y > y1) y = y1; } else if (y > y1) { y -= 4; if (y < y1) y = y1; }
    }
}
/* Glide `player`'s piece from path position p0 to p1, one cell at a time. The
 * source token is cleared from plane A (the stone underneath just shows); the
 * controller's next plat_draw() paints the landed token. */
static void anim_move(u8 player, u8 p0, u8 p1)
{
    u16 base = base_for(player);
    s16 x, y, nx, ny;
    u8 p, r, c;
    if (p0 >= 1 && p0 <= UR_PATH_LEN && pos_to_cell(player, p0, &r, &c))
        VDP_clearTileMapRect(BG_A, cellx(c), celly(r), 2, 2);
    cell_px(player, p0, &x, &y);
    for (p = (u8)(p0 + 1); p <= p1 && p <= UR_POS_HOME; p++) {
        cell_px(player, p, &nx, &ny);
        glide(x, y, nx, ny, base);
        x = nx; y = ny;
    }
    sprite_park();
}

/* plat.h: move chooser — D-pad up/down over the legal moves, A/B/C picks. The
 * board for `roll` is already drawn (by the controller); we add the list on
 * top, and tint every legal landing square green (motif kept — cleared by the
 * next full redraw). */
int8_t plat_choose_move(uint8_t player, uint8_t roll)
{
    uint8_t pieces[UR_PIECES], srcs[UR_PIECES];
    u8 count, nsrc, i, j, pos, sel;
    bool seen;
    u16 k;

    count = ur_legal_moves(&ur_g, player, roll, pieces);
    if (count == 0) return -1;

    nsrc = 0;
    for (i = 0; i < count; i++) {
        pos = ur_g.piece[player][pieces[i]];
        seen = FALSE;
        for (j = 0; j < nsrc; j++)
            if (srcs[j] == pos) { seen = TRUE; break; }
        if (!seen) srcs[nsrc++] = pos;
    }

    {   /* tint every legal landing square green */
        u8 d, r, c;
        for (i = 0; i < nsrc; i++) {
            d = (u8)(srcs[i] + roll);
            if (pos_to_cell(player, d, &r, &c))
                put_cell(BG_A, cellx(c), celly(r), green_base_for(r, c));
        }
    }

    put_str(MSG_X, MSG_Y, "U/D pick   A/B/C go");
    sel = 0;
    for (;;) {
        for (i = 0; i < nsrc; i++) {
            u16 y = (u16)(LIST_Y + i);
            u8 src = srcs[i], dest = (u8)(src + roll);
            put_str(LIST_X, y, "          ");
            put_ch(LIST_X, y, i == sel ? '>' : ' ');
            if (src == UR_POS_START) put_str(LIST_X + 2, y, "ent->");
            else { put_ch(LIST_X + 2, y, 'p'); put_u(LIST_X + 3, y, src); put_str(LIST_X + 5, y, "->"); }
            if (dest == UR_POS_HOME) put_ch(LIST_X + 7, y, 'H');
            else { put_u(LIST_X + 7, y, dest);
                   if (ur_is_rosette(dest)) put_ch(LIST_X + 9, y, '*');
                   else if (ur_dest_captures(&ur_g, player, dest)) put_ch(LIST_X + 9, y, 'X'); }
        }
        k = wait_press();
        if (k & BUTTON_UP)   sel = (u8)((sel + nsrc - 1) % nsrc);
        if (k & BUTTON_DOWN) sel = (u8)((sel + 1) % nsrc);
        if (k & BTN_FIRE) break;
    }

    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (ur_g.piece[player][pieces[i]] == pos)
            return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

/* plat.h: confirm wait, token glide, sound, RNG entropy. The shared controller
 * (ur_game.c) owns the turn loop and calls these. */
void plat_wait(void) { wait_press(); }
void plat_animate(uint8_t player, uint8_t from, uint8_t to)
{
    anim_move(player, from, to);
}
void plat_roll(uint8_t roll) { (void)roll; sfx_roll(); }
void plat_sfx_result(const ur_move_result *res) { sfx_for_result(res); }
uint16_t plat_seed(void) { return g_seed ^ GET_HVCOUNTER; }

/* plat.h: choose the AI difficulty — a gold rosette '*' marks the choice. */
uint8_t plat_pick_level(void)
{
    static const char *const opt[3] = { "EASY", "NORMAL", "HARD" };
    u8 sel = UR_AI_NORMAL, i;
    u16 y, k;
    VDP_setEnable(FALSE);
    sprite_park();
    screen_clear();
    if (board_shown) { VDP_clearPlane(BG_B, TRUE); board_shown = FALSE; }
    set_ink(INK_GOLD); put_str(15, 4, "DIFFICULTY"); set_ink(INK_WHITE);
    put_str(11, 20, "U/D pick   A/B/C OK");
    VDP_setEnable(TRUE);
    for (;;) {
        for (i = 0; i < 3; i++) {
            y = (u16)(9 + i * 2);
            set_ink(INK_GOLD);  put_ch(14, y, (i == sel) ? '*' : ' ');
            set_ink(INK_WHITE); put_str(16, y, opt[i]);
        }
        k = wait_press();
        if (k & BUTTON_UP)   sel = (u8)((sel + 2) % 3);
        if (k & BUTTON_DOWN) sel = (u8)((sel + 1) % 3);
        if (k & BTN_FIRE) return sel;   /* 0/1/2 = UR_AI_EASY/NORMAL/HARD */
    }
}

/* ---- title music: the Hurrian Hymn, once at boot (skippable) ------------ */
static bool played_music = FALSE;
static void play_hymn(void)
{
    u16 i;
    if (played_music) return;
    played_music = TRUE;
    snd_silence();
    for (i = 0; i < ur_hymn_len; i++)
        if (gen_music_note(ur_hymn[i].note, ur_hymn[i].dur))
            break;                       /* any press skips (polled per frame) */
    snd_silence();
}

/* ---- title / menu ------------------------------------------------------ */
static bool title_menu(void)             /* returns TRUE = vs computer */
{
    u8 sel = 1;                          /* 0 = two players, 1 = vs computer */
    u16 k;

    VDP_setEnable(FALSE);
    sprite_park();
    screen_clear();
    VDP_clearPlane(BG_B, TRUE);
    board_shown = FALSE;                 /* next plat_draw recarves the board */
    set_ink(INK_GOLD);
    put_str(TITLE_X, TITLE_Y, "THE ROYAL GAME OF UR");
    set_ink(INK_WHITE);
    put_str(8, 5, "Mesopotamia - c.2600 BCE");
    put_cell(BG_A, 9, 10, T_ROSE);       /* two decorative rosettes flanking */
    put_cell(BG_A, 29, 10, T_ROSE);
    put_str(15, 10, "Two Players");
    put_str(15, 12, "Vs Computer");
    put_str(8, 20, "D-pad to choose");
    put_str(8, 21, "A / B / C to start");
    VDP_setEnable(TRUE);
    play_hymn();                         /* title on screen first, then music */
    for (;;) {
        put_ch(13, 10, sel == 0 ? '>' : ' ');
        put_ch(13, 12, sel == 1 ? '>' : ' ');
        k = wait_press();
        if (k & BUTTON_UP)   sel = 0;
        if (k & BUTTON_DOWN) sel = 1;
        if (k & BTN_FIRE) break;
    }
    return sel == 1;
}

int main(bool hardReset)
{
    u8 winner;
    (void)hardReset;

    video_init();
    snd_silence();

    for (;;) {
        /* The shared controller (ur_game.c) drives the turn loop; it seeds the
         * RNG from plat_seed() (input timing + the free-running HV counter). */
        winner = ur_run_game(title_menu() ? 1 : 0);
        plat_draw(NO_ROLL, winner == 0 ? "LIGHT WINS! - PRESS A"
                                       : "DARK WINS! - PRESS A");
        wait_press();
    }
    return 0;
}
