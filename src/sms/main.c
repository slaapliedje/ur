/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Sega Master System (Z80 / z88dk +sms) — Royal Game of Ur, offline ROM.
 *
 * The SMS is the most graphically capable of the Ur ports (64-colour palette, 32
 * on-screen, 16 colours per 8x8 4bpp tile, two palettes, 64 sprites), so this is
 * the designated showpiece: a "Standard of Ur" treatment — lapis-lazuli field,
 * gold, shell-white and carnelian red; carved beveled stone cells, 8-point gold
 * rosette flowers, and shaded round tokens, on the authentic horizontal H-board
 * (3 rows x 8 columns). See src/sms/CLAUDE.md.
 *
 * Rendered through z88dk's classic <sms.h> Mode-4 VDP API (the only path that links
 * on this z88dk: classic sms_clib + sccz80, no devkitSMS/newlib SMS lib). The board
 * tiles are generated procedurally at boot (a 16x16 colour grid -> four 4bpp tiles)
 * rather than stored as art. SN76489 sound + control-pad input. No FujiNet. Reuses
 * src/common; structured like src/adam (game.turn drives the loop; human_turn /
 * computer_turn each return `over`). Build: makefiles/sms.mk -> build/sms/ur.sms.
 */
#include <stdint.h>
#include <sms.h>

#include "ur.h"
#include "font8.h"
#include "sound.h"
#include "music.h"          /* the Hurrian Hymn melody (shared) */

/* ---- palette (Standard of Ur materials) -------------------------------- *
 * SMS colour byte = %00BBGGRR (2 bits each). Bank 0 is the board/text palette;
 * bank 1 (reached per-tile via the name-table BKG_ATTR_SPRPAL bit) gives a second
 * "ink" so the white font tiles can render gold for the title. Index 1 == white so
 * the 1bpp font (which always lands on colour index 1) reads as white text. */
enum {
    C_FIELD = 0,    /* lapis field / backdrop  */
    C_WHITE,        /* 1 - font text           */
    C_FACE,         /* 2 - cell face (lapis)   */
    C_HI,           /* 3 - bevel highlight     */
    C_SH,           /* 4 - bevel shadow        */
    C_GOLD,         /* 5 - gold                */
    C_SHELL,        /* 6 - shell / cream       */
    C_RED,          /* 7 - carnelian           */
    C_GREY,         /* 8 - token shadow        */
    C_DGOLD         /* 9 - dark gold           */
};
static const unsigned char palette0[16] = {
    0x10, 0x3F, 0x20, 0x35, 0x00, 0x0B, 0x2F, 0x03, 0x15, 0x06,
    0,0,0,0,0,0
};
/* bank 1: index 0 = field (backdrop), index 1 = gold (INK_GOLD title text) */
static const unsigned char palette1[16] = {
    0x10, 0x0B, 0,0,0,0,0,0, 0,0,0,0,0,0,0,0
};

#define INK_WHITE 0x0000
#define INK_GOLD  BKG_ATTR_SPRPAL          /* per-tile -> palette bank 1 */

/* tile allocation (font occupies 0..95). Each board cell is 16x16 = 4 tiles. */
#define TILE_CELL  FONT8_COUNT             /* 4: plain carved cell (fallback) */
#define TILE_ROSE  (FONT8_COUNT + 4)       /* 4: gold 8-point rosette flower  */
#define TILE_DOTS  (FONT8_COUNT + 8)       /* 4: five-dot quincunx (lanes)    */
#define TILE_EYE   (FONT8_COUNT + 12)      /* 4: bullseye "eye" (shared lane) */
#define TILE_TOKL  (FONT8_COUNT + 16)      /* 4: 16x16 shell token            */
#define TILE_TOKD  (FONT8_COUNT + 20)      /* 4: 16x16 lapis token            */
#define TILE_TRYL  (FONT8_COUNT + 24)      /* 1: 8x8 shell tray bead          */
#define TILE_TRYD  (FONT8_COUNT + 25)      /* 1: 8x8 lapis tray bead          */

/* SMS VDP R1: bit6 = display enable, bit7 = (legacy, kept set); no frame IRQ. */
#define display_on()   vdp_set_reg(0x01, 0xC0)
#define display_off()  vdp_set_reg(0x01, 0x80)

static unsigned int ink = INK_WHITE;        /* current text ink */
static void set_ink(unsigned int a) { ink = a; }

/* ---- procedural tiles: a 16x16 colour grid baked into four 4bpp tiles --- */
static unsigned char grid[256];             /* 16x16 colour indices (0..15) */
static unsigned char tbuf[32];              /* one packed 4bpp tile         */

/* Pack the 8x8 region of `grid` at (sx,sy) into a planar 4bpp tile and load it. */
static void load_quad(unsigned char sx, unsigned char sy, unsigned int tno)
{
    unsigned char r, c, p;
    unsigned int byte;
    for (r = 0; r < 8; r++)
        for (p = 0; p < 4; p++) {
            byte = 0;
            for (c = 0; c < 8; c++) {
                unsigned char v = grid[((sy + r) << 4) + sx + c];
                if ((v >> p) & 1) byte |= (0x80u >> c);
            }
            tbuf[(r << 2) + p] = (unsigned char)byte;
        }
    load_tiles(tbuf, tno, 1, 4);
}
/* Load the whole 16x16 grid as four consecutive tiles (TL, TR, BL, BR). */
static void load_cell16(unsigned int first)
{
    load_quad(0, 0, first);
    load_quad(8, 0, first + 1);
    load_quad(0, 8, first + 2);
    load_quad(8, 8, first + 3);
}

static void build_carved(void)              /* raised, beveled lapis tile */
{
    unsigned char x, y, v;
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
    signed char x, y;
    int dx, dy, r2;
    unsigned char v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            v = C_FACE;
            if (x >= 14 || y >= 14) v = C_SH;
            if (x <= 1  || y <= 1)  v = C_HI;
            dx = 2 * x - 15; dy = 2 * y - 15;
            r2 = dx * dx + dy * dy;
            /* bars + diagonals through centre, clipped to a radius -> 8 points */
            if ((x == 7 || x == 8 || y == 7 || y == 8 || dx == dy || dx == -dy)
                && r2 <= 150)
                v = C_GOLD;
            if (r2 <= 12) v = C_SHELL;           /* shell pearl centre */
            grid[(y << 4) + x] = v;
        }
}

/* Fill `grid` with the plain carved cell (used as the base for decorated cells). */
static void grid_carved(void)
{
    unsigned char x, y, v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            v = C_FACE;
            if (x >= 14 || y >= 14) v = C_SH;
            if (x <= 1  || y <= 1)  v = C_HI;
            grid[(y << 4) + x] = v;
        }
}

/* Stamp a small filled diamond of colour `c` centred at (cx,cy), radius ~rad. */
static void stamp_dot(unsigned char cx, unsigned char cy, unsigned char rad, unsigned char c)
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

/* Five-dot quincunx — the carved cell with shell studs at centre + corners and a
 * gold centre, echoing the dotted squares of the real Standard of Ur board. */
static void build_dots(void)
{
    grid_carved();
    stamp_dot(4, 4, 1, C_SHELL);  stamp_dot(11, 4, 1, C_SHELL);
    stamp_dot(4, 11, 1, C_SHELL); stamp_dot(11, 11, 1, C_SHELL);
    stamp_dot(7, 7, 2, C_SHELL);  stamp_dot(7, 7, 1, C_GOLD);
}

/* Bullseye "eye" — concentric gold ring with a shell pearl centre, for the shared
 * middle lane so it reads distinctly from the private rows. */
static void build_eye(void)
{
    signed char x, y;
    int dx, dy, r2;
    grid_carved();
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            dx = 2 * x - 15; dy = 2 * y - 15;
            r2 = dx * dx + dy * dy;
            if (r2 > 60 && r2 <= 120) grid[(y << 4) + x] = C_GOLD;   /* ring  */
            else if (r2 <= 14)        grid[(y << 4) + x] = C_SHELL;  /* pearl */
        }
}

/* A shaded round token filling the 16x16 cell: an outline ring (shell inlay,
 * so dark pieces pop on the lapis field) around a body with a top-left highlight,
 * bottom-right shadow, and a centre pip. */
static void build_token16(unsigned char body, unsigned char hi, unsigned char sh,
                          unsigned char pip, unsigned char ring, unsigned int first)
{
    signed char x, y;
    int dx, dy, r2;
    unsigned char v;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            dx = 2 * x - 15; dy = 2 * y - 15;
            r2 = dx * dx + dy * dy;
            v = C_FIELD;                          /* corners = field (lapis) */
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
    load_cell16(first);
}

/* An 8x8 tray bead (single tile) with an outline ring. */
static void build_bead8(unsigned char body, unsigned char hi, unsigned char sh,
                        unsigned char ring, unsigned int tno)
{
    signed char x, y;
    int dx, dy, r2;
    unsigned char v;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            dx = 2 * x - 7; dy = 2 * y - 7;
            r2 = dx * dx + dy * dy;
            v = C_FIELD;
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

static void video_init(void)
{
    clear_vram();
    load_palette((unsigned char *)palette0, 0, 16);     /* bank 0 */
    load_palette((unsigned char *)palette1, 16, 16);    /* bank 1 + backdrop */
    load_tiles(font8, 0, FONT8_COUNT, 1);               /* 1bpp font -> tiles */

    build_carved();  load_cell16(TILE_CELL);
    build_rosette(); load_cell16(TILE_ROSE);
    build_dots();    load_cell16(TILE_DOTS);
    build_eye();     load_cell16(TILE_EYE);
    /* Light = shell bead (grey rim); Dark = lapis bead with a shell rim + gold pip
     * so it reads clearly against the lapis field. */
    build_token16(C_SHELL, C_WHITE, C_GREY, C_SH,   C_GREY,  TILE_TOKL);
    build_token16(C_FACE,  C_HI,    C_SH,   C_GOLD, C_SHELL, TILE_TOKD);
    build_bead8(C_SHELL, C_WHITE, C_GREY, C_GREY,  TILE_TRYL);
    build_bead8(C_FACE,  C_HI,    C_SH,   C_SHELL, TILE_TRYD);

    vdp_set_reg(0x07, C_FIELD);                          /* backdrop = field  */
    display_on();
}

/* ---- positioned tiles / text ------------------------------------------- */
static void put_tile(unsigned char x, unsigned char y, unsigned int word)
{
    set_bkg_map(&word, x, y, 1, 1);
}
/* place a 16x16 cell (4 tiles: TL,TR / BL,BR) with top-left at (x,y) */
static void put_cell(unsigned char x, unsigned char y, unsigned int first)
{
    put_tile(x, y, first);     put_tile((unsigned char)(x + 1), y, first + 1);
    put_tile(x, (unsigned char)(y + 1), first + 2);
    put_tile((unsigned char)(x + 1), (unsigned char)(y + 1), first + 3);
}

static void put_ch(unsigned char x, unsigned char y, char c)
{
    unsigned int w = (unsigned int)((unsigned char)c - 0x20) | ink;
    set_bkg_map(&w, x, y, 1, 1);
}
static void put_str(unsigned char x, unsigned char y, const char *s)
{
    unsigned int w[32];
    unsigned char n = 0;
    while (s[n] && n < 32) { w[n] = ((unsigned int)((unsigned char)s[n] - 0x20)) | ink; n++; }
    if (n) set_bkg_map(w, x, y, n, 1);
}
static void put_u(unsigned char x, unsigned char y, unsigned char v)
{
    char buf[4];
    signed char i = 3;
    buf[3] = 0;
    do { buf[--i] = (char)('0' + v % 10); v = (unsigned char)(v / 10); } while (v && i > 0);
    put_str(x, y, &buf[i]);
}
static void screen_clear(void)
{
    unsigned int blanks[32];
    unsigned char i, y;
    for (i = 0; i < 32; i++) blanks[i] = 0;             /* tile 0 = space = field */
    for (y = 0; y < 24; y++) set_bkg_map(blanks, 0, y, 32, 1);
}

/* ---- input: control pad, release-then-press (one tap = one action) ------
 * Edge-tracking a previous state is fragile: at SMS reset the control port floats
 * and reads "pressed", which a naive edge detector counts as input. Instead wait
 * for a full release, then the next press — the Adam/ColecoVision pad pattern. */
#define JOY_ANY (JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT | JOY_FIREA | JOY_FIREB)

static void spin(unsigned int n) { volatile unsigned int i; for (i = 0; i < n; i++) { } }

static int wait_press(void)
{
    int now;
    while (read_joypad1() & JOY_ANY) spin(300);      /* wait for release */
    for (;;) {                                        /* wait for a press */
        now = read_joypad1() & 0xFF;
        if (now & JOY_ANY) { spin(4000); return now; }
        spin(300);
    }
}

/* ---- board geometry: horizontal H-board, 3 rows x 8 cols, 16x16 cells --- */
#define BX 8                         /* board tile origin (cols 8..23) */
#define BY 8                         /* board tile origin (rows 8..13) */
static unsigned char cellx(unsigned char col) { return (unsigned char)(BX + (col << 1)); }
static unsigned char celly(unsigned char row) { return (unsigned char)(BY + (row << 1)); }

/* The 20-square H-shape: left 4-block (cols 0..3, all rows) + right 2-block
 * (cols 6..7) joined by the shared middle row (row 1, all cols). */
static bool cell_exists(unsigned char row, unsigned char col)
{
    return row == 1 || col <= 3 || col >= 6;
}
/* path position -> board (row,col). Light lane = row 0, Dark = row 2, shared = 1. */
static bool pos_to_cell(unsigned char player, unsigned char pos,
                        unsigned char *row, unsigned char *col)
{
    if (pos < 1 || pos > UR_PATH_LEN) return false;
    if (pos <= 4)       { *row = player ? 2 : 0; *col = (unsigned char)(4 - pos); }   /* entry */
    else if (pos <= 12) { *row = 1;              *col = (unsigned char)(pos - 5); }   /* shared */
    else                { *row = player ? 2 : 0; *col = (pos == 13) ? 7 : 6; }        /* exit */
    return true;
}
static bool is_rosette_cell(unsigned char row, unsigned char col)
{
    return (row != 1 && (col == 0 || col == 6)) || (row == 1 && col == 3);
}

static ur_state game;
static bool ai1;                     /* player 1 is the computer */

static unsigned char count_at(unsigned char pl, unsigned char pos)
{
    unsigned char i, n = 0;
    for (i = 0; i < UR_PIECES; i++)
        if (game.piece[pl][i] == pos) n++;
    return n;
}

#define NO_ROLL 0xFF

static void draw_tray(unsigned char x, unsigned char y, unsigned char n, unsigned int tile)
{
    unsigned char i;
    for (i = 0; i < n; i++) put_tile((unsigned char)(x + i), y, tile);
}

static void draw_board(unsigned char roll, const char *msg)
{
    unsigned char row, col, pl, i, pos, rr, cc;

    display_off();          /* blank during the full redraw -> no tearing */
    screen_clear();

    set_ink(INK_GOLD);
    put_str(6, 1, "THE ROYAL GAME OF UR");
    set_ink(INK_WHITE);
    put_str(2, 3, "Turn:");
    put_str(8, 3, game.turn ? "DARK " : "LIGHT");
    put_str(18, 3, "Roll:");
    if (roll != NO_ROLL) put_u(24, 3, roll);

    /* carved, inlaid board cells (skip the H-shape cut-away corners): gold
     * rosette stars at the 5 rosette squares, a bullseye "eye" down the shared
     * lane, a five-dot quincunx on the private lanes. */
    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++) {
            unsigned int base;
            if (!cell_exists(row, col)) continue;
            if (is_rosette_cell(row, col)) base = TILE_ROSE;
            else if (row == 1)             base = TILE_EYE;
            else                           base = TILE_DOTS;
            put_cell(cellx(col), celly(row), base);
        }

    /* on-board tokens: shaded 16x16 disc cells */
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = game.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                put_cell(cellx(cc), celly(rr), pl ? TILE_TOKD : TILE_TOKL);
        }

    /* trays: Light above the board, Dark below; waiting (left) + home (right) */
    put_str(2, 6, "Light");
    draw_tray(8,  6, count_at(0, UR_POS_START), TILE_TRYL);
    draw_tray(24, 6, ur_score(&game, 0),        TILE_TRYL);
    put_str(2, 15, "Dark");
    draw_tray(8,  15, count_at(1, UR_POS_START), TILE_TRYD);
    draw_tray(24, 15, ur_score(&game, 1),        TILE_TRYD);

    if (msg) put_str(2, 22, msg);
    display_on();
}

/* ---- move chooser: D-pad up/down over the legal moves, button picks ----- */
#define LIST_X 1
#define LIST_Y 18
static int8_t choose_move(unsigned char player, unsigned char roll)
{
    unsigned char pieces[UR_PIECES], srcs[UR_PIECES];
    unsigned char count, nsrc, i, j, pos, sel;
    bool seen;
    int k;

    count = ur_legal_moves(&game, player, roll, pieces);
    if (count == 0) return -1;

    nsrc = 0;
    for (i = 0; i < count; i++) {
        pos = game.piece[player][pieces[i]];
        seen = false;
        for (j = 0; j < nsrc; j++)
            if (srcs[j] == pos) { seen = true; break; }
        if (!seen) srcs[nsrc++] = pos;
    }

    put_str(2, 22, "U/D pick  FIRE go ");
    sel = 0;
    for (;;) {
        for (i = 0; i < nsrc; i++) {
            unsigned char y = (unsigned char)(LIST_Y + i);
            unsigned char src = srcs[i], dest = (unsigned char)(src + roll);
            put_str(LIST_X, y, "          ");
            put_ch(LIST_X, y, i == sel ? '>' : ' ');
            if (src == UR_POS_START) put_str(LIST_X + 2, y, "ent->");
            else { put_ch(LIST_X + 2, y, 'p'); put_u(LIST_X + 3, y, src); put_str(LIST_X + 5, y, "->"); }
            if (dest == UR_POS_HOME) put_ch(LIST_X + 7, y, 'H');
            else { put_u(LIST_X + 7, y, dest); if (ur_is_rosette(dest)) put_ch(LIST_X + 9, y, '*'); }
        }
        k = wait_press();
        if (k & JOY_UP)   sel = (unsigned char)((sel + nsrc - 1) % nsrc);
        if (k & JOY_DOWN) sel = (unsigned char)((sel + 1) % nsrc);
        if (k & (JOY_FIREA | JOY_FIREB)) break;
    }

    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (game.piece[player][pieces[i]] == pos)
            return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

static bool human_turn(unsigned char player)
{
    unsigned char roll;
    int8_t picked;
    ur_move_result res;

    draw_board(NO_ROLL, "Press FIRE to roll");
    wait_press();
    roll = ur_dice_roll();
    sfx_roll();
    draw_board(roll, "");

    picked = choose_move(player, roll);
    if (picked < 0) {
        draw_board(roll, "No legal move - FIRE");
        wait_press();
        ur_advance_turn(&game, (const ur_move_result *)0);
        return false;
    }

    ur_apply_move(&game, player, (unsigned char)picked, roll, &res);
    sfx_for_result(&res);
    if (res.won) return true;
    if (res.captured || res.rosette) {
        draw_board(roll, res.captured ? "Capture! - FIRE" : "Rosette - again!");
        wait_press();
    }
    ur_advance_turn(&game, &res);
    return false;
}

static bool computer_turn(unsigned char player)
{
    unsigned char pieces[UR_PIECES], roll;
    int8_t pick;
    ur_move_result res;

    draw_board(NO_ROLL, "Computer's turn - FIRE");
    wait_press();
    roll = ur_dice_roll();
    sfx_roll();
    draw_board(roll, "");

    if (ur_legal_moves(&game, player, roll, pieces) == 0) {
        draw_board(roll, "Computer: no move");
        wait_press();
        ur_advance_turn(&game, (const ur_move_result *)0);
        return false;
    }

    pick = ur_ai_pick(&game, player, roll);
    ur_apply_move(&game, player, (unsigned char)pick, roll, &res);
    sfx_for_result(&res);
    draw_board(roll, "Computer moved - FIRE");
    wait_press();
    if (res.won) return true;
    ur_advance_turn(&game, &res);
    return false;
}

/* ---- title music: the Hurrian Hymn, once at boot (skippable) ------------ */
static bool played_music = false;
static void play_hymn(void)
{
    uint16_t i;
    if (played_music) return;
    played_music = true;
    snd_silence();
    for (i = 0; i < ur_hymn_len; i++) {
        if (read_joypad1() & JOY_ANY) break;     /* any press skips */
        sms_music_note(ur_hymn[i].note, ur_hymn[i].dur);
    }
    snd_silence();
}

/* ---- title / menu ------------------------------------------------------ */
static bool title_menu(void)         /* returns ai1 (true = vs computer) */
{
    unsigned char sel = 1;           /* 0 = two players, 1 = vs computer */
    int k;

    display_off();
    screen_clear();
    set_ink(INK_GOLD);
    put_str(6, 3, "THE ROYAL GAME OF UR");
    set_ink(INK_WHITE);
    put_str(4, 5, "Mesopotamia - c.2600 BCE");
    /* two decorative rosettes flanking the menu */
    put_cell(6, 9, TILE_ROSE);
    put_cell(24, 9, TILE_ROSE);
    put_str(11, 10, "Two Players");
    put_str(11, 12, "Vs Computer");
    put_str(4, 18, "D-pad to choose");
    put_str(4, 19, "FIRE to start");
    display_on();
    for (;;) {
        put_ch(9, 10, sel == 0 ? '>' : ' ');
        put_ch(9, 12, sel == 1 ? '>' : ' ');
        k = wait_press();
        if (k & JOY_UP)   sel = 0;
        if (k & JOY_DOWN) sel = 1;
        if (k & (JOY_FIREA | JOY_FIREB)) break;
    }
    return sel == 1;
}

int main(void)
{
    unsigned char player;
    bool over;

    video_init();
    snd_silence();

    ur_rng_seed(0xA537);             /* TODO: fold in a frame counter for variety */

    for (;;) {
        play_hymn();                 /* the Hurrian Hymn (once at boot, skippable) */
        ai1 = title_menu();
        ur_init(&game);
        over = false;
        for (;;) {
            player = game.turn;
            /* explicit if/else, NOT a ternary of two calls — sccz80 miscompiles
             * `cond ? computer_turn() : human_turn()` (ran the wrong branch). */
            if (player == 1 && ai1)
                over = computer_turn(player);
            else
                over = human_turn(player);
            if (over)
                break;
        }
        draw_board(NO_ROLL, player == 0 ? "LIGHT WINS! - FIRE"
                                        : "DARK WINS! - FIRE");
        wait_press();
    }
    return 0;
}
