/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Nintendo Game Boy / Game Boy Color (Sharp LR35902 / z88dk +gb) — Royal Game of Ur.
 *
 * One cart that runs on both: on a Game Boy Color it shows the "Standard of Ur"
 * colour board (lapis / gold / shell), on a plain DMG Game Boy the same tiles render
 * in 4 greys. The cart is marked CGB-compatible (header byte 0x143 = 0x80, patched
 * in makefiles/gb.mk since z88dk's crt0 hard-codes 0). It reuses the SMS port's
 * design — the authentic horizontal H-board (3 rows x 8 cols), procedurally drawn
 * carved cells, gold rosette stars, bullseye "eyes", five-dot quincunx studs, and
 * two-tone tokens — adapted to the GB's 2bpp tiles (4 colours per palette). The GB
 * screen is 160x144 = 20x18 tiles at map origin, so it uses the compact layout.
 *
 * Rendered via z88dk's GBDK-style API (<arch/gb/gb.h>, <arch/gb/cgb.h>). The shared
 * src/common core drops in unchanged. This is the bring-up: board + tokens + input
 * + full game (hot-seat + vs-AI). Sound and token glide animation are follow-ups.
 * Build: makefiles/gb.mk -> build/gb/ur.gb. Run: MAME gameboy (grey) / gbcolor.
 */
#include <stdint.h>
#include <arch/gb/gb.h>
#include <arch/gb/cgb.h>

#include "ur.h"
#include "ur_game.h"        /* shared local-game controller + plat.h interface */
#include "music.h"          /* the Hurrian Hymn title theme (shared melody) */
#include "sound.h"
#include "font8.h"          /* shared 1bpp font (from src/sms; -I in gb.mk) */

#define rBGP  (*(volatile uint8_t *)0xFF47)  /* DMG bg palette register     */
#define rOBP0 (*(volatile uint8_t *)0xFF48)  /* DMG sprite palette 0        */
#define rDIV  (*(volatile uint8_t *)0xFF04)  /* free-running timer (entropy) */

/* Sprite tile slots in the $8000 block for the gliding token (4 tiles each). */
#define SPR_TOKL 0
#define SPR_TOKD 4

/* ---- palette: one 4-colour palette does the whole board ---------------- *
 * 0 field (dark lapis)  1 face (lapis)  2 shell (cream/white)  3 gold.
 * Carved bevels use field/face/shell; the gold motifs + tokens use all four. */
enum { C_FIELD = 0, C_FACE, C_SHELL, C_GOLD };

/* GBC 15-bit RGB (5 bits each) for the four colours. */
static const uint16_t cgb_pal[4] = {
    RGB(2, 4, 11),      /* field: deep lapis        */
    RGB(7, 10, 22),     /* face:  lapis             */
    RGB(30, 28, 21),    /* shell: warm cream/white  */
    RGB(30, 23, 4)      /* gold                     */
};
/* DMG: map colour index -> grey so it reads (shell brightest, field darkest).
 * BGP bits: [i0 i1 i2 i3], each 0=white..3=black. field=black, face=dkgrey,
 * shell=white, gold=ltgrey. */
#define DMG_BGP 0x4B    /* i0=3 i1=2 i2=0 i3=1 */

/* tile allocation (font occupies 0..95); each board cell is 16x16 = 4 tiles */
#define TILE_CELL  FONT8_COUNT             /* 4: carved cell (fallback)  */
#define TILE_ROSE  (FONT8_COUNT + 4)       /* 4: gold rosette star       */
#define TILE_DOTS  (FONT8_COUNT + 8)       /* 4: five-dot quincunx        */
#define TILE_EYE   (FONT8_COUNT + 12)      /* 4: bullseye eye             */
#define TILE_TOKL  (FONT8_COUNT + 16)      /* 4: shell token              */
#define TILE_TOKD  (FONT8_COUNT + 20)      /* 4: lapis token              */
#define TILE_TRYL  (FONT8_COUNT + 24)      /* 1: shell tray bead          */
#define TILE_TRYD  (FONT8_COUNT + 25)      /* 1: lapis tray bead          */

/* ---- procedural tiles: a 16x16 colour grid baked into four 2bpp tiles --- */
static uint8_t grid[256];
static uint8_t t16[16];

static void load_quad(uint8_t sx, uint8_t sy, uint8_t tno)
{
    uint8_t r, c, p0, p1;
    for (r = 0; r < 8; r++) {
        p0 = 0; p1 = 0;
        for (c = 0; c < 8; c++) {
            uint8_t v = grid[((sy + r) << 4) + sx + c] & 3;
            if (v & 1) p0 |= (uint8_t)(0x80 >> c);
            if (v & 2) p1 |= (uint8_t)(0x80 >> c);
        }
        t16[r * 2] = p0; t16[r * 2 + 1] = p1;
    }
    set_bkg_data(tno, 1, t16);
}
static void load_cell(uint8_t first)
{
    load_quad(0, 0, first);
    load_quad(8, 0, (uint8_t)(first + 1));
    load_quad(0, 8, (uint8_t)(first + 2));
    load_quad(8, 8, (uint8_t)(first + 3));
}

/* Sprites always read tile data from $8000, but the BG uses the $8800 block (the
 * z88dk gb crt0 default), so the token's BG tiles can't double as sprite tiles —
 * pack a sprite copy of the current `grid` token into 4 sprite tiles at spr_first.
 * Used only for the gliding token (see plat_animate). */
static uint8_t sbuf[64];
static void pack_quad(uint8_t sx, uint8_t sy, uint8_t *dst)
{
    uint8_t r, c, p0, p1;
    for (r = 0; r < 8; r++) {
        p0 = 0; p1 = 0;
        for (c = 0; c < 8; c++) {
            uint8_t v = grid[((sy + r) << 4) + sx + c] & 3;
            if (v & 1) p0 |= (uint8_t)(0x80 >> c);
            if (v & 2) p1 |= (uint8_t)(0x80 >> c);
        }
        dst[r * 2] = p0; dst[r * 2 + 1] = p1;
    }
}
static void load_cell_sprite(uint8_t spr_first)
{
    pack_quad(0, 0, &sbuf[0]);   pack_quad(8, 0, &sbuf[16]);
    pack_quad(0, 8, &sbuf[32]);  pack_quad(8, 8, &sbuf[48]);
    set_sprite_data(spr_first, 4, sbuf);
}

static void grid_carved(void)
{
    uint8_t x, y, v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            v = C_FACE;
            if (x >= 14 || y >= 14) v = C_FIELD;   /* bottom/right shadow */
            if (x <= 1  || y <= 1)  v = C_SHELL;   /* top/left highlight  */
            grid[(y << 4) + x] = v;
        }
}
static void build_rosette(void)
{
    signed char x, y; int dx, dy, r2;
    grid_carved();
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            dx = 2 * x - 15; dy = 2 * y - 15; r2 = dx * dx + dy * dy;
            if ((x == 7 || x == 8 || y == 7 || y == 8 || dx == dy || dx == -dy) && r2 <= 150)
                grid[(y << 4) + x] = C_GOLD;
            if (r2 <= 12) grid[(y << 4) + x] = C_SHELL;
        }
}
static void stamp_dot(uint8_t cx, uint8_t cy, uint8_t rad, uint8_t c)
{
    signed char x, y;
    for (y = (signed char)(cy - rad); y <= (signed char)(cy + rad); y++)
        for (x = (signed char)(cx - rad); x <= (signed char)(cx + rad); x++)
            if (x >= 0 && x < 16 && y >= 0 && y < 16) {
                signed char dx = (signed char)(x - cx), dy = (signed char)(y - cy);
                if (dx < 0) dx = (signed char)-dx;
                if (dy < 0) dy = (signed char)-dy;
                if (dx + dy <= (signed char)rad) grid[(y << 4) + x] = c;
            }
}
static void build_dots(void)
{
    grid_carved();
    stamp_dot(4, 4, 1, C_SHELL);  stamp_dot(11, 4, 1, C_SHELL);
    stamp_dot(4, 11, 1, C_SHELL); stamp_dot(11, 11, 1, C_SHELL);
    stamp_dot(7, 7, 2, C_SHELL);  stamp_dot(7, 7, 1, C_GOLD);
}
static void build_eye(void)
{
    signed char x, y; int dx, dy, r2;
    grid_carved();
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            dx = 2 * x - 15; dy = 2 * y - 15; r2 = dx * dx + dy * dy;
            if (r2 > 60 && r2 <= 120) grid[(y << 4) + x] = C_GOLD;
            else if (r2 <= 14)        grid[(y << 4) + x] = C_SHELL;
        }
}
/* round token: outer rim + body + centre pip; field corners (transparent-ish) */
static void build_token(uint8_t body, uint8_t ring, uint8_t pip, uint8_t first)
{
    signed char x, y; int dx, dy, r2; uint8_t v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            dx = 2 * x - 15; dy = 2 * y - 15; r2 = dx * dx + dy * dy;
            v = C_FIELD;
            if (r2 <= 215) { v = ring; if (r2 <= 150) v = body; }
            if (r2 <= 16) v = pip;
            grid[(y << 4) + x] = v;
        }
    load_cell(first);
}
static void build_bead(uint8_t body, uint8_t ring, uint8_t tno)
{
    signed char x, y; int dx, dy, r2; uint8_t v;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            dx = 2 * x - 7; dy = 2 * y - 7; r2 = dx * dx + dy * dy;
            v = C_FIELD;
            if (r2 <= 49) { v = ring; if (r2 <= 28) v = body; }
            grid[(y << 4) + x] = v;
        }
    load_quad(0, 0, tno);
}

static void load_font(void);            /* defined just below */

static void video_init(void)
{
    DISPLAY_OFF;
    if (_cpu == CGB_TYPE)
        set_bkg_palette(0, 1, (uint16_t *)cgb_pal);   /* GBC colour */
    else
        rBGP = DMG_BGP;                               /* DMG greys  */

    load_font();
    grid_carved();   load_cell(TILE_CELL);
    build_rosette(); load_cell(TILE_ROSE);
    build_dots();    load_cell(TILE_DOTS);
    build_eye();     load_cell(TILE_EYE);
    build_token(C_SHELL, C_FACE,  C_FIELD, TILE_TOKL);  /* Light: shell disc */
    load_cell_sprite(SPR_TOKL);                         /* + a sprite copy for the glide */
    build_token(C_FACE,  C_SHELL, C_GOLD,  TILE_TOKD);  /* Dark: lapis+gold  */
    load_cell_sprite(SPR_TOKD);
    build_bead(C_SHELL, C_FACE,  TILE_TRYL);
    build_bead(C_FACE,  C_SHELL, TILE_TRYD);

    /* sprite palette = the board palette (colour 0 is transparent for sprites). */
    if (_cpu == CGB_TYPE)
        set_sprite_palette(0, 1, (uint16_t *)cgb_pal);
    else
        rOBP0 = DMG_BGP;

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;
    enable_interrupts();         /* crt0 already set IE=VBL; the VBL ISR does OAM DMA */
}
static void load_font(void)
{
    uint16_t g; uint8_t r;
    for (g = 0; g < FONT8_COUNT; g++) {
        const uint8_t *s = &font8[g * 8];
        for (r = 0; r < 8; r++) { t16[r * 2] = 0; t16[r * 2 + 1] = s[r]; }  /* index 2 */
        set_bkg_data((uint8_t)g, 1, t16);
    }
}

/* ---- positioned tiles / text ------------------------------------------- */
static void put_tile(uint8_t x, uint8_t y, uint8_t tile)
{
    set_bkg_tiles(x, y, 1, 1, &tile);
}
/* A 16x16 cell = 4 tiles (TL,TR / BL,BR). z88dk's set_bkg_tiles misplaces a
 * small multi-tile array here (a pointer/codegen quirk: a w>1 block from a tiny
 * local buffer scatters), so place the four tiles with single-tile writes, which
 * are reliable. */
static void put_cell(uint8_t x, uint8_t y, uint8_t first)
{
    put_tile(x, y, first);
    put_tile((uint8_t)(x + 1), y, (uint8_t)(first + 1));
    put_tile(x, (uint8_t)(y + 1), (uint8_t)(first + 2));
    put_tile((uint8_t)(x + 1), (uint8_t)(y + 1), (uint8_t)(first + 3));
}
static void put_str(uint8_t x, uint8_t y, const char *s)
{
    uint8_t w[20], n = 0;
    while (s[n] && n < 20) { w[n] = (uint8_t)((uint8_t)s[n] - 0x20); n++; }
    if (n) set_bkg_tiles(x, y, n, 1, w);
}
static void put_u(uint8_t x, uint8_t y, uint8_t v)
{
    char buf[4]; signed char i = 3;
    buf[3] = 0;
    do { buf[--i] = (char)('0' + v % 10); v = (uint8_t)(v / 10); } while (v && i > 0);
    put_str(x, y, &buf[i]);
}
static void screen_clear(void)
{
    uint8_t blank[20], y;
    uint8_t i;
    for (i = 0; i < 20; i++) blank[i] = 0;       /* tile 0 = space = field */
    for (y = 0; y < 18; y++) set_bkg_tiles(0, y, 20, 1, blank);
}

/* ---- input: control pad, release-then-press (one tap = one action) ----- */
static uint8_t wait_press(void)
{
    waitpadup();
    return waitpad(J_A | J_B | J_START | J_UP | J_DOWN | J_LEFT | J_RIGHT);
}

/* ---- layout: horizontal H-board in the 160x144 (20x18) screen ----------- */
#define BX 2
#define BY 4
#define TITLE_Y 0
#define HUD_Y 1
#define HUD_TURN_X 5
#define HUD_ROLL_X 11
#define HUD_ROLLV_X 14
#define LTRAY_Y 2
#define DTRAY_Y 10
#define TRAY_WX 2
#define TRAY_HX 11
#define LIST_X 0
#define LIST_Y 11
#define MSG_X 0
#define MSG_Y 17
static uint8_t cellx(uint8_t col) { return (uint8_t)(BX + (col << 1)); }
static uint8_t celly(uint8_t row) { return (uint8_t)(BY + (row << 1)); }

static bool cell_exists(uint8_t row, uint8_t col) { return row == 1 || col <= 3 || col >= 6; }
static bool pos_to_cell(uint8_t player, uint8_t pos, uint8_t *row, uint8_t *col)
{
    if (pos < 1 || pos > UR_PATH_LEN) return false;
    if (pos <= 4)       { *row = player ? 2 : 0; *col = (uint8_t)(4 - pos); }
    else if (pos <= 12) { *row = 1;              *col = (uint8_t)(pos - 5); }
    else                { *row = player ? 2 : 0; *col = (pos == 13) ? 7 : 6; }
    return true;
}
static bool is_rosette_cell(uint8_t row, uint8_t col)
{
    return (row != 1 && (col == 0 || col == 6)) || (row == 1 && col == 3);
}

static uint16_t g_seed = 0xACE1u;     /* RNG entropy accumulator (the menu/hymn) */
static bool g_played_music = false;

static uint8_t count_at(uint8_t pl, uint8_t pos)
{
    uint8_t i, n = 0;
    for (i = 0; i < UR_PIECES; i++) if (ur_g.piece[pl][i] == pos) n++;
    return n;
}

#define NO_ROLL 0xFF

static void draw_tray(uint8_t x, uint8_t y, uint8_t n, uint8_t tile)
{
    uint8_t i;
    for (i = 0; i < n; i++) put_tile((uint8_t)(x + i), y, tile);
}

/* plat.h: draw the board + HUD + message for the active game. */
void plat_draw(uint8_t roll, const char *msg)
{
    uint8_t row, col, pl, i, pos, rr, cc;

    DISPLAY_OFF;            /* blank during the full redraw: z88dk's set_bkg_tiles
                            * isn't VRAM-safe during active display (writes drop /
                            * corrupt), so draw with the LCD off, then back on. */
    screen_clear();
    put_str(0, TITLE_Y, "THE ROYAL GAME OF UR");
    put_str(0, HUD_Y, "Turn:");
    put_str(HUD_TURN_X, HUD_Y, ur_g.turn ? "DARK " : "LIGHT");
    put_str(HUD_ROLL_X, HUD_Y, "Rl:");
    if (roll != NO_ROLL) put_u(HUD_ROLLV_X, HUD_Y, roll);

    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++) {
            uint8_t base;
            if (!cell_exists(row, col)) continue;
            if (is_rosette_cell(row, col)) base = TILE_ROSE;
            else if (row == 1)             base = TILE_EYE;
            else                           base = TILE_DOTS;
            put_cell(cellx(col), celly(row), base);
        }

    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = ur_g.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                put_cell(cellx(cc), celly(rr), pl ? TILE_TOKD : TILE_TOKL);
        }

    put_str(0, LTRAY_Y, "L");
    draw_tray(TRAY_WX, LTRAY_Y, count_at(0, UR_POS_START), TILE_TRYL);
    draw_tray(TRAY_HX, LTRAY_Y, ur_score(&ur_g, 0), TILE_TRYL);
    put_str(0, DTRAY_Y, "D");
    draw_tray(TRAY_WX, DTRAY_Y, count_at(1, UR_POS_START), TILE_TRYD);
    draw_tray(TRAY_HX, DTRAY_Y, ur_score(&ur_g, 1), TILE_TRYD);

    if (msg) put_str(MSG_X, MSG_Y, msg);
    DISPLAY_ON;
}

/* plat.h: with the board for `roll` already drawn, render the move list + selector
 * and read the pick. */
int8_t plat_choose_move(uint8_t player, uint8_t roll)
{
    uint8_t pieces[UR_PIECES], srcs[UR_PIECES];
    uint8_t count, nsrc, i, j, pos, sel;
    bool seen;
    uint8_t k;

    count = ur_legal_moves(&ur_g, player, roll, pieces);
    if (count == 0) return -1;
    nsrc = 0;
    for (i = 0; i < count; i++) {
        pos = ur_g.piece[player][pieces[i]];
        seen = false;
        for (j = 0; j < nsrc; j++) if (srcs[j] == pos) { seen = true; break; }
        if (!seen) srcs[nsrc++] = pos;
    }

    put_str(MSG_X, MSG_Y, "U/D pick A go ");
    sel = 0;
    for (;;) {
        for (i = 0; i < nsrc; i++) {
            uint8_t y = (uint8_t)(LIST_Y + i);
            uint8_t src = srcs[i], dest = (uint8_t)(src + roll);
            put_str(LIST_X, y, "          ");
            put_tile(LIST_X, y, (uint8_t)((i == sel ? '>' : ' ') - 0x20));
            if (src == UR_POS_START) put_str(LIST_X + 2, y, "ent->");
            else { put_tile(LIST_X + 2, y, (uint8_t)('p' - 0x20)); put_u(LIST_X + 3, y, src); put_str(LIST_X + 5, y, "->"); }
            if (dest == UR_POS_HOME) put_tile(LIST_X + 7, y, (uint8_t)('H' - 0x20));
            else { put_u(LIST_X + 7, y, dest);
                   if (ur_is_rosette(dest)) put_tile(LIST_X + 9, y, (uint8_t)('*' - 0x20));
                   else if (ur_dest_captures(&ur_g, player, dest)) put_tile(LIST_X + 9, y, (uint8_t)('X' - 0x20)); }
        }
        k = wait_press();
        if (k & J_UP)   sel = (uint8_t)((sel + nsrc - 1) % nsrc);
        if (k & J_DOWN) sel = (uint8_t)((sel + 1) % nsrc);
        if (k & (J_A | J_B | J_START)) break;
    }

    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (ur_g.piece[player][pieces[i]] == pos) return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

/* plat.h: wait for one confirm press; sound + RNG entropy. */
void plat_wait(void) { wait_press(); }
void plat_roll(uint8_t roll) { (void)roll; sfx_roll(); }
void plat_sfx_result(const ur_move_result *res) { sfx_for_result(res); }
uint16_t plat_seed(void) { return (uint16_t)(g_seed ^ ((uint16_t)rDIV << 3)); }
/* ---- token glide animation (hardware sprites) -------------------------- *
 * A move plays as a four-sprite 16x16 token sliding cell-to-cell along the path.
 * Static pieces stay BG tiles; only the mover becomes a sprite. The board's BG uses
 * the $8800 tile block, sprites the $8000 block, so the token has a sprite copy
 * (SPR_TOKL/SPR_TOKD, loaded in video_init). OAM updates are DMA'd by the VBL ISR
 * each frame; we pace by polling LY (robust — never hangs). */
#define rLY (*(volatile uint8_t *)0xFF44)
static void gb_waitframe(void) { while (rLY >= 144) {} while (rLY < 144) {} }

static uint8_t base_for(uint8_t row, uint8_t col)
{
    if (is_rosette_cell(row, col)) return TILE_ROSE;
    if (row == 1)                  return TILE_EYE;
    return TILE_DOTS;
}
/* Pixel position of a path cell (pos 0 = the waiting tray, pos>14 = the home tray). */
static void cell_px(uint8_t player, uint8_t pos, uint8_t *px, uint8_t *py)
{
    uint8_t r, c;
    if (pos == 0 || pos > UR_PATH_LEN) {
        *px = (uint8_t)((pos == 0 ? TRAY_WX : TRAY_HX) * 8);
        *py = (uint8_t)((player ? DTRAY_Y : LTRAY_Y) * 8);
        return;
    }
    pos_to_cell(player, pos, &r, &c);
    *px = (uint8_t)(cellx(c) * 8);
    *py = (uint8_t)(celly(r) * 8);
}
/* Place the 4-sprite token at screen pixel (px,py). GB OAM is offset by (8,16).
 * (Tiles are assigned once via set_sprite_tile before the glide; this only moves.) */
static void put_token_sprite(uint8_t px, uint8_t py)
{
    move_sprite(0, (uint8_t)(px + 8),     (uint8_t)(py + 16));
    move_sprite(1, (uint8_t)(px + 8 + 8), (uint8_t)(py + 16));
    move_sprite(2, (uint8_t)(px + 8),     (uint8_t)(py + 16 + 8));
    move_sprite(3, (uint8_t)(px + 8 + 8), (uint8_t)(py + 16 + 8));
}
static void hide_token_sprite(void)
{
    uint8_t i;
    for (i = 0; i < 4; i++) move_sprite(i, 0, 0);   /* off-screen */
    gb_waitframe();
}

/* plat.h: glide `player`'s token from path position `from` to `to`. */
void plat_animate(uint8_t player, uint8_t from, uint8_t to)
{
    uint8_t base = player ? SPR_TOKD : SPR_TOKL;
    uint8_t r, c, x, y, nx, ny, p;

    if (from == to) return;
    /* clear the BG token at the source so it doesn't ghost behind the slide */
    if (from >= 1 && from <= UR_PATH_LEN) {
        pos_to_cell(player, from, &r, &c);
        DISPLAY_OFF;                         /* set_bkg_tiles isn't safe mid-frame */
        put_cell(cellx(c), celly(r), base_for(r, c));
        DISPLAY_ON;
    }
    set_sprite_tile(0, base);     set_sprite_tile(1, (uint8_t)(base + 1));
    set_sprite_tile(2, (uint8_t)(base + 2)); set_sprite_tile(3, (uint8_t)(base + 3));

    cell_px(player, from, &x, &y);
    for (p = (uint8_t)(from + 1); p <= to && p <= UR_POS_HOME; p++) {
        cell_px(player, p, &nx, &ny);
        while (x != nx || y != ny) {
            if (x < nx)      { x += 4; if (x > nx) x = nx; }
            else if (x > nx) { x -= 4; if (x < nx) x = nx; }
            if (y < ny)      { y += 4; if (y > ny) y = ny; }
            else if (y > ny) { y -= 4; if (y < ny) y = ny; }
            put_token_sprite(x, y);
            gb_waitframe();
        }
    }
    hide_token_sprite();
}

/* plat.h: choose the AI difficulty (D-pad Up = Easy, Down = Hard, A = Normal). */
uint8_t plat_pick_level(void)
{
    uint8_t k;
    DISPLAY_OFF;
    screen_clear();
    put_str(2, 2, "DIFFICULTY");
    put_str(1, 6,  "Up:   Easy");
    put_str(1, 8,  "A:    Normal");
    put_str(1, 10, "Down: Hard");
    DISPLAY_ON;
    for (;;) {
        k = wait_press();
        if (k & J_UP)                  return UR_AI_EASY;
        if (k & J_DOWN)                return UR_AI_HARD;
        if (k & (J_A | J_B | J_START)) return UR_AI_NORMAL;
    }
}

/* Title music: the Hurrian Hymn, once, skippable. The per-note loop is also where
 * we gather RNG entropy — the GB's blocking waitpad gives no idle loop to count in,
 * so we mix the free-running DIV timer per note and let the player's skip timing add
 * variance. (The final seed in main() also samples DIV at menu-confirm time.) */
static void play_hymn(void)
{
    uint16_t i;
    if (g_played_music) return;
    g_played_music = true;
    for (i = 0; i < ur_hymn_len; i++) {
        if (joypad()) break;                          /* any button skips */
        g_seed = (uint16_t)((g_seed << 1) ^ rDIV);
        gb_music_note(ur_hymn[i].note, ur_hymn[i].dur);
    }
    snd_silence();
}

static bool title_menu(void)
{
    uint8_t sel = 1, k;
    screen_clear();
    put_str(0, 0, "THE ROYAL GAME OF UR");
    put_str(3, 2, "Mesopotamia");
    put_cell(0, 5, TILE_ROSE);
    put_cell(18, 5, TILE_ROSE);
    put_str(5, 6, "Two Players");
    put_str(5, 8, "Vs Computer");
    put_str(2, 11, "D-pad: pick");
    put_str(2, 12, "A: start");
    play_hymn();                /* the Hurrian Hymn (once at boot, skippable) */
    for (;;) {
        put_tile(3, 6, (uint8_t)((sel == 0 ? '>' : ' ') - 0x20));
        put_tile(3, 8, (uint8_t)((sel == 1 ? '>' : ' ') - 0x20));
        k = wait_press();
        if (k & J_UP)   sel = 0;
        if (k & J_DOWN) sel = 1;
        if (k & (J_A | J_B | J_START)) break;
    }
    return sel == 1;
}

void main(void)
{
    uint8_t vs_ai, winner;

    video_init();
    gb_sound_init();

    for (;;) {
        vs_ai = title_menu() ? 1 : 0;      /* plays the hymn + gathers entropy */
        winner = ur_run_game(vs_ai);       /* shared controller drives the turns */
        if (vs_ai) plat_draw(NO_ROLL, winner == 0 ? "YOU WIN! - A" : "YOU LOSE - A");
        else       plat_draw(NO_ROLL, winner == 0 ? "LIGHT WINS! - A" : "DARK WINS! - A");
        wait_press();
    }
}
