/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Sega Master System (Z80 / z88dk +sms) — Royal Game of Ur, offline ROM.
 *
 * The SMS native display is VDP Mode 4 (a 4bpp tilemap). Plain conio text did NOT
 * appear (the earlier scaffold black-screened — the classic console's default ink
 * is invisible), so we drive the VDP directly with z88dk's classic <sms.h> Mode-4
 * API: load_palette (CRAM), load_tiles (our 8x8 font, 1bpp -> 4bpp), set_bkg_map
 * (write tiles into the name table), read_joypad1 (control pad). Only sccz80 +
 * sms_clib are installed here (no devkitSMS SMSlib, no newlib sms.lib), so this is
 * the path that actually links and renders. The CRT brings up Mode 4 but leaves
 * the SMS display-enable bit clear, so we set it via vdp_set_reg.
 *
 * No FujiNet (a cartridge console): local hot-seat + vs-AI, reusing src/common and
 * structured like src/adam/main.c (game.turn drives the loop; human_turn /
 * computer_turn each return `over`). Build: makefiles/sms.mk. Out: build/sms/ur.sms.
 */
#include <stdint.h>
#include <sms.h>

#include "ur.h"
#include "font8.h"
#include "sound.h"
#include "music.h"          /* the Hurrian Hymn melody (shared) */

/* ---- video: font tiles + palette + positioned text --------------------- */
/* Two CRAM banks. Font/disc pixels are colour index 1..3; the name-table
 * "use-sprite-palette" bit (BKG_ATTR_SPRPAL) picks bank 1 per tile, giving a
 * second ink (gold) for the title + rosettes without re-baking the font. */
#define INK_WHITE 0x0000
#define INK_GOLD  BKG_ATTR_SPRPAL          /* -> bank 1, entry 17 = gold */

#define TILE_LIGHT  FONT8_COUNT             /* cream disc token (index 2)        */
#define TILE_DARK   (FONT8_COUNT + 1)       /* red disc + cream pip (index 3/2)  */

/* bank 0: 0 field, 1 white, 2 cream (Light token), 3 red (Dark token) */
static unsigned char palette0[16] = {
    0x10, 0x3F, 0x2F, 0x03,
    0,0,0,0, 0,0,0,0, 0,0,0,0
};
/* bank 1 (reached via INK_GOLD): 16 field, 17 gold/amber */
static unsigned char palette1[16] = {
    0x10, 0x0B, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
};

/* Two custom 4bpp token tiles (32 bytes each = 8 rows x 4 bitplanes). A filled
 * 8x8 disc with a 2x2 centre hole; Light = cream body (index 2), Dark = red body
 * (index 3) with a cream pip (index 2) — the "Ur set" two-tone look as one tile. */
static const unsigned char disc_tiles[2 * 32] = {
    /* TILE_LIGHT: index 2 => plane1 only; pip hole = field (index 0) */
    0x00,0x3C,0x00,0x00,  0x00,0x7E,0x00,0x00,  0x00,0xFF,0x00,0x00,  0x00,0xE7,0x00,0x00,
    0x00,0xE7,0x00,0x00,  0x00,0xFF,0x00,0x00,  0x00,0x7E,0x00,0x00,  0x00,0x3C,0x00,0x00,
    /* TILE_DARK: body index 3 (plane0+plane1); pip index 2 (plane1 only) */
    0x3C,0x3C,0x00,0x00,  0x7E,0x7E,0x00,0x00,  0xFF,0xFF,0x00,0x00,  0xE7,0xFF,0x00,0x00,
    0xE7,0xFF,0x00,0x00,  0xFF,0xFF,0x00,0x00,  0x7E,0x7E,0x00,0x00,  0x3C,0x3C,0x00,0x00
};

/* SMS VDP R1: bit6 = display enable, bit7 = (legacy, kept set); no frame IRQ. */
#define display_on()   vdp_set_reg(0x01, 0xC0)
#define display_off()  vdp_set_reg(0x01, 0x80)

static unsigned int ink = INK_WHITE;        /* current text ink (OR'd into words) */
static void set_ink(unsigned int a) { ink = a; }

static void video_init(void)
{
    clear_vram();
    load_palette(palette0, 0, 16);                      /* BG palette bank 0 */
    load_palette(palette1, 16, 16);                     /* alt palette bank 1 */
    load_tiles(font8, 0, FONT8_COUNT, 1);               /* 1bpp font -> tiles */
    load_tiles((unsigned char *)disc_tiles, TILE_LIGHT, 2, 4);  /* 4bpp tokens */
    display_on();
}

/* set_bkg_map writes a w*h block of 16-bit map words (= tile | attr) at (x,y). */
static void put_tile(unsigned char x, unsigned char y, unsigned int word)
{
    set_bkg_map(&word, x, y, 1, 1);
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

/* small unsigned -> decimal (avoid pulling printf in for one or two numbers) */
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
    for (i = 0; i < 32; i++) blanks[i] = 0;             /* tile 0 = space */
    for (y = 0; y < 24; y++) set_bkg_map(blanks, 0, y, 32, 1);
}

/* ---- input: control pad, release-then-press (one tap = one action) ------
 * Edge-tracking via a remembered previous state is fragile here: at SMS reset the
 * controller port floats and reads as "pressed", which a naive edge detector counts
 * as real input and blows through prompts. Instead we wait for a full release, then
 * for the next press — the same robust pattern the Adam/ColecoVision pad uses. */
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

/* ---- board geometry (3 cols x 8 rows, the Adam/Atari vertical layout) --- */
#define BX 13                        /* board origin in tiles */
#define BY 5
static unsigned char cellx(unsigned char col) { return (unsigned char)(BX + col * 3); }
static unsigned char celly(unsigned char row) { return (unsigned char)(BY + (row - 1) * 2); }

/* path position -> (row,col), modelled on src/adam. pos 1..14 only. */
static bool pos_to_cell(unsigned char player, unsigned char pos,
                        unsigned char *row, unsigned char *col)
{
    if (pos < 1 || pos > UR_PATH_LEN) return false;
    if (pos <= 4)       { *col = player ? 2 : 0; *row = (unsigned char)(5 - pos); }
    else if (pos <= 12) { *col = 1;              *row = (unsigned char)(pos - 4); }
    else                { *col = player ? 2 : 0; *row = (pos == 13) ? 8 : 7; }
    return true;
}
static bool is_rosette_cell(unsigned char row, unsigned char col)
{
    return (row == 1 && col != 1) || (row == 7 && col != 1) || (row == 4 && col == 1);
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

static void draw_board(unsigned char roll, const char *msg)
{
    unsigned char row, col, pl, i, pos, rr, cc, n;

    display_off();          /* blank during the full redraw -> no tearing */
    screen_clear();
    set_ink(INK_GOLD);
    put_str(6, 0, "THE ROYAL GAME OF UR");
    set_ink(INK_WHITE);
    put_str(0, 2, "Turn:");
    put_str(6, 2, game.turn ? "DARK" : "LIGHT");

    /* base board cells (skip the H-shape cut-away corners): gold rosettes,
     * white lane dots. */
    for (row = 1; row <= 8; row++)
        for (col = 0; col < 3; col++) {
            if (!(col == 1 || row <= 4 || row >= 7)) continue;
            if (is_rosette_cell(row, col)) {
                set_ink(INK_GOLD); put_ch(cellx(col), celly(row), '*'); set_ink(INK_WHITE);
            } else {
                put_ch(cellx(col), celly(row), '.');
            }
        }

    /* pieces on the board: two-tone disc tokens */
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = game.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                put_tile(cellx(cc), celly(rr), pl ? TILE_DARK : TILE_LIGHT);
        }

    /* trays: waiting (upper) + home (lower); Light left, Dark right */
    n = count_at(0, UR_POS_START);
    for (i = 0; i < n; i++) put_tile(4, (unsigned char)(BY + i), TILE_LIGHT);
    n = ur_score(&game, 0);
    for (i = 0; i < n; i++) put_tile(4, (unsigned char)(BY + 9 + i), TILE_LIGHT);
    n = count_at(1, UR_POS_START);
    for (i = 0; i < n; i++) put_tile(27, (unsigned char)(BY + i), TILE_DARK);
    n = ur_score(&game, 1);
    for (i = 0; i < n; i++) put_tile(27, (unsigned char)(BY + 9 + i), TILE_DARK);

    put_str(0, 3, "Roll:");
    if (roll != NO_ROLL) put_u(6, 3, roll);

    if (msg) put_str(0, 23, msg);
    display_on();
}

/* ---- move chooser: D-pad up/down over the legal moves, button picks ----- */
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

    put_str(0, 22, "U/D pick  FIRE go");
    sel = 0;
    for (;;) {
        for (i = 0; i < nsrc; i++) {
            unsigned char y = (unsigned char)(15 + i);
            unsigned char src = srcs[i], dest = (unsigned char)(src + roll);
            put_str(0, y, "            ");
            put_ch(0, y, i == sel ? '>' : ' ');
            if (src == UR_POS_START) put_str(2, y, "ent->");
            else { put_ch(2, y, 'p'); put_u(3, y, src); put_str(5, y, "->"); }
            if (dest == UR_POS_HOME) put_ch(7, y, 'H');
            else { put_u(7, y, dest); if (ur_is_rosette(dest)) put_ch(10, y, '*'); }
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
    put_str(6, 2, "THE ROYAL GAME OF UR");
    set_ink(INK_WHITE);
    put_str(3, 4, "Mesopotamia - c.2600 BCE");
    put_str(8, 9,  "Two Players");
    put_str(8, 11, "Vs Computer");
    put_str(3, 16, "D-pad to choose");
    put_str(3, 17, "FIRE to start");
    display_on();
    for (;;) {
        put_ch(6, 9,  sel == 0 ? '>' : ' ');
        put_ch(6, 11, sel == 1 ? '>' : ' ');
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
            /* explicit if/else, NOT `cond ? computer_turn() : human_turn()` —
             * sccz80 miscompiles a ternary whose both arms are function calls
             * (it ran computer_turn even on the human's turn). */
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
