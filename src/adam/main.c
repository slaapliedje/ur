/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Coleco Adam (Z80 / z88dk) — Royal Game of Ur, local-play bring-up.
 *
 * Reuses the portable core (src/common/ur) unchanged; this file is the thin
 * platform layer. It renders a text board through z88dk's console (conio) and
 * reads the Adam keyboard. Vertical layout like the Atari: Light runs up the
 * left column, Dark up the right, the middle column is the shared lane.
 *
 * This first pass is local only (hot-seat + vs the AI). Still to come:
 * FujiNet online (fujinet-adam, same N: protocol as the Atari), SN76489 sound,
 * and TMS9928A colour/sprite polish. Run in MAME (adam) or ADAMEm.
 */
#include <stdint.h>
#include <stdbool.h>
#include <conio.h>          /* text output: cputs/cprintf/gotoxy + colour */
#include <video/tms99x8.h>  /* hardware sprites for the pieces (round tokens) */

#include "ur.h"
#include "ur_game.h"        /* shared local-game controller + plat.h interface */
#include "proto.h"          /* shared wire-protocol codec (cross-platform) */
#include "sound.h"          /* SN76489 sound effects                              */
#include "music.h"          /* the Hurrian Hymn title theme (shared melody)       */
#ifndef UR_COLECO           /* ColecoVision: no AdamNet/EOS/FujiNet (cartridge)   */
#include "fujinet-network.h"/* FujiNet N: device: network_init/open/read/write/... */
#include "fujinet-fuji.h"   /* fuji_*_appkey: persistent profile + lobby handoff   */
#endif

#define LIGHT_CH 'O'        /* Light pieces */
#define DARK_CH  'X'        /* Dark pieces  */
#define ROSE_CH  '*'        /* rosette      */
#define TILE_CH  '.'        /* empty cell   */
#define NO_ROLL  0xFF

/*
 * Input on the Adam: keyboard AND ColecoVision controller, polled together.
 *
 * z88dk's `coleco` conio has NO real keyboard: getk() is a stub that always
 * returns 0 ("the Colecovision doesn't have a keyboard"). The Adam *does* have a
 * keyboard, reached through EOS on the AdamNet bus via tschak909's eoslib. The
 * plain eos_read_keyboard() BLOCKS until a key arrives -- which would freeze out
 * the controller -- so get_key() instead drives the async non-blocking pair
 * (eos_start_read_keyboard + a raw-flag poll; see kbd_poll) and reads the
 * controller every loop, returning whichever fires first. A keyboard tap is
 * debounced with settle(): in MAME the EOS read is level-triggered and the game
 * loop outpaces a press, so without it one physical tap satisfies several reads
 * and blows through prompts (the UI therefore says "press").
 *
 * The controller is the SNAPPY path. The Adam and ColecoVision share the same
 * controller ports, read via z88dk's joystick() (= coleco_joypad). joystick(which)
 * returns the keypad ASCII ('1'-'9','0','*','#') in the high byte and the MOVE_*
 * bits in the low byte (which=3 = player-1 keypad+stick); we map keypad digits to
 * the same menu/move-number input the keyboard uses, and FIRE to roll/confirm. The
 * emulated EOS keyboard is laggy under MAME (AdamNet latency), so the joystick
 * sidesteps it entirely -- and the same read also drives the ColecoVision build.
 * (Verified in MAME's `adam` driver: controller keypad -> host numpad, FIRE ->
 * Left Ctrl; both menu select and in-game roll/move work via the controller.)
 */
#include <games.h>
#define CV_FIRE (MOVE_FIRE | MOVE_FIRE2)   /* 0x10 | 0x20 */
#ifndef UR_COLECO
#include <arch/z80.h>       /* AsmCall + Z80_registers: raw-flag non-blocking kbd poll */
extern unsigned char eos_read_keyboard(void);       /* blocking read (text entry)      */
extern unsigned char eos_start_read_keyboard(void); /* kick off a background read      */
#endif

/* Busy-wait ~0.4s on a 3.58MHz Z80 so a tap releases before the next read
 * (debounce). In MAME the EOS keyboard read is level-triggered -- it returns
 * the key for as long as it is held -- and the game loop is far faster than a
 * keypress, so without this a single press would satisfy several reads and blow
 * through prompts. ~2 passes measured ~0.4s in MAME: longer than a normal tap,
 * short enough to stay responsive. Real Adam hardware queues one keycode per
 * press, so this is a no-op there. A volatile sink keeps the loop from being
 * optimized away. (No usable free-running timer here.) */
#define SETTLE_PASSES 2u

static volatile uint16_t g_sink;
static uint16_t g_seed = 0xACE1u;   /* RNG entropy accumulator (folded from input) */

static void settle(void)
{
    uint16_t i, j;
    for (j = 0; j < SETTLE_PASSES; j++)
        for (i = 0; i < 60000u; i++)
            g_sink = i;                     /* volatile store: not optimizable */
}

/* Wait for a printable keypress, debounce it, and return it. Non-printable
 * results (EOS error/status codes < 0x20 and special SmartKeys >= 0x80) are
 * skipped. Each key is folded into the RNG seed (seeded at the first roll). */
#ifdef UR_COLECO
/* Controller input: a keypad digit returns '1'-'9'; FIRE returns RETURN (used by
 * the "press a key" prompts). Polls until release then press, so one tap = one key. */
static unsigned char get_key(void)
{
    unsigned int r;
    unsigned char kp;
    do { r = joystick(3); g_seed += 0x9E37u; }     /* wait for release  */
    while ((r >> 8) || (r & CV_FIRE));
    for (;;) {                                          /* wait for a press  */
        r = joystick(3);
        g_seed += 0x9E37u;
        kp = (unsigned char)(r >> 8);
        if (kp >= '1' && kp <= '9') { g_seed = (uint16_t)(g_seed * 31u + kp); return kp; }
        if (r & CV_FIRE)            { g_seed = (uint16_t)(g_seed * 31u + 1);  return '\r'; }
    }
}
#else
/* Non-blocking keyboard poll. eos_read_keyboard() BLOCKS, which would freeze out
 * the joystick, so we drive the async pair by hand: eos_start_read_keyboard() kicks
 * off a background read (above the loop), then this checks the AdamNet result via
 * the 0xFC4B EOS entry — honouring the CARRY flag (read finished) and ZERO flag (no
 * error -> A holds the key) that the eos_end_read_keyboard() wrapper throws away.
 * Returns the key, or 0 when nothing has arrived yet (NAK auto-reissues the read). */
static unsigned char kbd_poll(void)
{
    Z80_registers r;
    AsmCall(0xFC4B, &r, REGS_ALL, REGS_ALL);        /* eos_end_read_keyboard, raw flags */
    if ((r.Bytes.F & 0x01) && (r.Bytes.F & 0x40))   /* C: finished, Z: no error          */
        return r.Bytes.A;                           /* -> the key just read              */
    return 0;                                        /* still pending / NAK               */
}

/* Adam: poll BOTH the joystick and the keyboard, returning whichever fires first. */
static unsigned char get_key(void)
{
    unsigned int r;
    unsigned char kp, k;

    /* Debounce: wait for any held controller button to release (one tap = one key). */
    do { r = joystick(3); g_seed += 0x9E37u; }
    while ((r >> 8) || (r & CV_FIRE));

    eos_start_read_keyboard();                       /* begin a background keyboard read */
    for (;;) {
        /* Controller — snappy hardware read (same path as the ColecoVision). */
        r = joystick(3);
        g_seed += 0x9E37u;
        kp = (unsigned char)(r >> 8);
        if (kp >= '1' && kp <= '9') { g_seed = (uint16_t)(g_seed * 31u + kp); return kp; }
        if (r & CV_FIRE)            { g_seed = (uint16_t)(g_seed * 31u + 1);  return '\r'; }

        /* Keyboard — non-blocking AdamNet poll; accept printable keys only. */
        k = kbd_poll();
        if (k >= 0x20 && k < 0x7F) {
            g_seed = (uint16_t)(g_seed * 31u + k);
            settle();                               /* let the tap release (debounce) */
            return k;
        }
    }
}
#endif

/* Title music: the Hurrian Hymn, played once at boot. Skippable — the controller
 * (any keypad digit or FIRE), and on the Adam the keyboard too, ends it early so
 * the player can go straight to the menu. Drives SN76489 channel 0 (sfx-free on
 * the menu). Shared by the Adam and ColecoVision builds. */
static bool g_played_music = false;
static void play_hymn(void)
{
    uint16_t i;
    unsigned int r;
    if (g_played_music) return;       /* only on the first menu (not every return) */
    g_played_music = true;
    snd_silence();                     /* kill any boot-state PSG drone on other channels */
#ifndef UR_COLECO
    eos_start_read_keyboard();         /* begin a background keyboard read (skip) */
#endif
    for (i = 0; i < ur_hymn_len; i++) {
        r = joystick(3);
        if ((r >> 8) || (r & CV_FIRE)) break;        /* controller skip */
#ifndef UR_COLECO
        if (kbd_poll() >= 0x20) break;               /* keyboard skip (Adam) */
#endif
        adam_music_note(ur_hymn[i].note, ur_hymn[i].dur);
    }
    snd_silence();
}

/* plat.h: confirm wait, roll sound, result sound, RNG entropy (folded from input
 * timing in get_key). The shared controller (ur_game.c) owns the turn loop; the
 * Adam has no dice animation or token glide. */
void plat_wait(void) { get_key(); }
void plat_roll(unsigned char roll) { (void)roll; sfx_roll(); }
void plat_sfx_result(const ur_move_result *res) { sfx_for_result(res); }
uint16_t plat_seed(void) { return g_seed; }
void plat_animate(unsigned char player, unsigned char from, unsigned char to)
{ (void)player; (void)from; (void)to; }

/* plat.h: choose the AI difficulty (keypad 1/2/3, or FIRE = Normal). (Colour
 * macros are defined later in the file, so this prompt is plain text.) */
uint8_t plat_pick_level(void)
{
    unsigned char k;
    clrscr();
    gotoxy(0, 1); cputs("Difficulty");
    gotoxy(0, 4); cputs("1) Easy");
    gotoxy(0, 5); cputs("2) Normal");
    gotoxy(0, 6); cputs("3) Hard");
    gotoxy(0, 8); cputs("Keypad 1-3");
    for (;;) {
        k = get_key();
        if (k >= '1' && k <= '3') return (unsigned char)(k - '1');
        if (k == '\r')            return UR_AI_NORMAL;
    }
}

/* Path position (1..14) -> board cell. HORIZONTAL (like the SMS/Atari/C64): row
 * 0=Light, 1=shared, 2=Dark; cols 0..7. False if off-board. */
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

static unsigned char count_at(unsigned char pl, unsigned char pos)
{
    unsigned char i, n = 0;
    for (i = 0; i < UR_PIECES; i++)
        if (ur_g.piece[pl][i] == pos)
            n++;
    return n;
}

/* Board geometry (char cells, 8px each). HORIZONTAL board: 8 cols x 3 rows, 2x2-char
 * (16x16) cells, centred (cols 8..23, rows 4..9 — kept off the 8/16 screen-third
 * boundaries so each 2x2 cell stays within one Graphics-II third). HUD + move list
 * go above/below. */
static unsigned char cellx(unsigned char col) { return (unsigned char)(8 + col * 2); }
static unsigned char celly(unsigned char row) { return (unsigned char)(4 + row * 2); }
/* H-shape: shared middle row spans all cols; private rows skip the bridge (4-5). */
static bool cell_exists(unsigned char row, unsigned char col)
{
    return row == 1 || col <= 3 || col >= 6;
}

/* conio colours map to TMS9928A inks (see conio_map_colour): BLUE -> dark blue
 * (lapis), YELLOW -> light yellow (gold), LIGHTGREEN -> light green, WHITE.
 * Board cells are drawn as 2x2-char (16x16px) colour squares so they match the
 * 16x16 token sprites that sit on them. */
#define COL_BG      BLUE        /* lapis background, like the Atari build */
#define COL_TITLE   YELLOW      /* gold */
#define COL_LABEL   WHITE

/* ---- carved board cells (Graphics II custom patterns) -------------------- *
 * The Adam is already in Mode II (a 256x192 bitmap), so each 16x16 board cell can
 * be a real carved shape instead of a flat colour block. Each cell is 4 custom 8x8
 * patterns (TL,TR,BL,BR) written straight to the pattern + colour tables. The name
 * table is the standard mangled-mode ramp, so the pattern slot for screen (x,y) is
 * (y%8)*32 + x in screen-third y/8. */
#define ROSE_COLOR ((VDP_INK_DARK_YELLOW << 4) | VDP_INK_DARK_BLUE) /* gold on lapis  */

/* Inlaid mosaic motifs (SMS parity), gold/white on lapis. TMS9928A cells are 2-colour
 * (FG/BG per char), so the motif is the FG pattern. Generated by
 * tools/adam-mosaic-glyphs.c -- do not hand-edit. */
static const unsigned char g_rose_cell[32] = {   /* 8-petal gold rosette + pip */
    0x00,0x0F,0x07,0x03,0x4F,0x6F,0x7F,0x7E,   /* TL */
    0x00,0xF0,0xE0,0xC0,0xF2,0xF6,0xFE,0x7E,   /* TR */
    0x7E,0x7F,0x6F,0x4F,0x03,0x07,0x0F,0x00,   /* BL */
    0x7E,0xFE,0xF6,0xF2,0xC0,0xE0,0xF0,0x00   /* BR */
};
static const unsigned char g_eye_cell[32] = {   /* gold bullseye: ring + pearl */
    0x00,0x0F,0x1F,0x38,0x70,0x63,0x67,0x67,   /* TL */
    0x00,0xF0,0xF8,0x1C,0x0E,0xC6,0xE6,0xE6,   /* TR */
    0x67,0x67,0x63,0x70,0x38,0x1F,0x0F,0x00,   /* BL */
    0xE6,0xE6,0xC6,0x0E,0x1C,0xF8,0xF0,0x00   /* BR */
};
static const unsigned char g_dots_cell[32] = {   /* bordered five-dot quincunx */
    0xFF,0x80,0x80,0x98,0x98,0x80,0x80,0x81,   /* TL */
    0xFF,0x01,0x01,0x19,0x19,0x01,0x01,0x81,   /* TR */
    0x81,0x80,0x80,0x98,0x98,0x80,0x80,0xFF,   /* BL */
    0x81,0x01,0x01,0x19,0x19,0x01,0x01,0xFF   /* BR */
};
static const unsigned char g_tok_cell[32] = {   /* round token donut (hole = pip) */
    0x00,0x0F,0x1F,0x3F,0x7F,0x7C,0x78,0x78,   /* TL */
    0x00,0xF0,0xF8,0xFC,0xFE,0x3E,0x1E,0x1E,   /* TR */
    0x78,0x78,0x7C,0x7F,0x3F,0x1F,0x0F,0x00,   /* BL */
    0x1E,0x1E,0x3E,0xFE,0xFC,0xF8,0xF0,0x00   /* BR */
};
static const unsigned char g_bead[8] = { 0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C }; /* tray bead */

#define EYE_COLOR  ROSE_COLOR                                         /* gold on lapis  */
#define DOTS_COLOR ((VDP_INK_WHITE << 4)        | VDP_INK_DARK_BLUE)  /* white on lapis */
#define TOKL_COLOR ((VDP_INK_LIGHT_YELLOW << 4) | VDP_INK_DARK_BLUE)  /* cream donut    */
#define TOKD_COLOR ((VDP_INK_DARK_RED << 4)     | VDP_INK_DARK_BLUE)  /* brown donut    */

/* Draw a carved 16x16 cell (2x2 custom chars) at board (col,row). */
static void carve_cell(unsigned char col, unsigned char row,
                       const unsigned char *q, unsigned char color)
{
    unsigned char x = cellx(col), y = celly(row);
    unsigned int  place = (y < 8) ? place_1 : (y < 16) ? place_2 : place_3;
    int c = (int)((unsigned int)(y & 7) * 32 + x);
    vdp_set_char_form(c,      (void *)(q +  0), place);   /* TL */
    vdp_set_char_form(c + 1,  (void *)(q +  8), place);   /* TR */
    vdp_set_char_form(c + 32, (void *)(q + 16), place);   /* BL */
    vdp_set_char_form(c + 33, (void *)(q + 24), place);   /* BR */
    vdp_set_char_color(c,      color, place);
    vdp_set_char_color(c + 1,  color, place);
    vdp_set_char_color(c + 32, color, place);
    vdp_set_char_color(c + 33, color, place);
}

/* Draw a single 8x8 custom char (a tray bead) at (x,y). */
static void carve_char(unsigned char x, unsigned char y,
                       const unsigned char *p, unsigned char color)
{
    unsigned int place = (y < 8) ? place_1 : (y < 16) ? place_2 : place_3;
    int c = (int)((unsigned int)(y & 7) * 32 + x);
    vdp_set_char_form(c, (void *)p, place);
    vdp_set_char_color(c, color, place);
}

/* Pieces are charset donut tokens now (horizontal board — see draw_board), so no
 * hardware sprites for the board. Players are named by colour in the HUD. */
#define SPR_LIGHT_C   YELLOW                /* conio text colour for "Light" */
#define SPR_DARK_C    LIGHTRED              /* conio text colour for "Dark"  */
#define SPR_END_Y     209           /* stored y becomes 0xD0: sprite-list end */

/* Hide all sprites (used on the menu / non-board screens). */
static void hide_sprites(void)
{
    vdp_put_sprite_16(0, 0, SPR_END_Y, 0, 0);
}

/* Is (row,col) a rosette square? */
/* HORIZONTAL board: rosettes are the corners of the private lanes + the centre of
 * the shared lane (the authentic Standard-of-Ur layout, matching the SMS). */
static bool is_rosette_cell(unsigned char row, unsigned char col)
{
    return (row != 1 && (col == 0 || col == 6)) || (row == 1 && col == 3);
}

void plat_draw(unsigned char roll, const char *msg)
{
    unsigned char row, col, pl, i, pos, rr, cc, k, n;

    hide_sprites();                     /* board tokens are charset now, not sprites */
    textbackground(COL_BG);
    clrscr();
    textcolor(COL_TITLE); gotoxy(0, 0); cputs("Royal Game of Ur");

    /* Carved, inlaid 16x16 cells. Rosettes are gold flowers; the shared lane has a
     * gold bullseye eye; the private lanes a white quincunx. Cut-away corners
     * (the H-shape bridge) are simply not drawn. */
    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++) {
            if (!cell_exists(row, col)) continue;
            if (is_rosette_cell(row, col)) carve_cell(col, row, g_rose_cell, ROSE_COLOR);
            else if (row == 1)             carve_cell(col, row, g_eye_cell,  EYE_COLOR);
            else                           carve_cell(col, row, g_dots_cell, DOTS_COLOR);
        }

    /* On-board tokens: charset donut discs, cream Light / brown Dark. */
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = ur_g.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                carve_cell(cc, rr, g_tok_cell, pl ? TOKD_COLOR : TOKL_COLOR);
        }

    /* Off-board trays as small beads: Light above the board (row 3), Dark below
     * (row 10); waiting pieces on the left, borne-off pieces on the right. */
    n = count_at(0, UR_POS_START);
    for (k = 0; k < n; k++) carve_char((unsigned char)(8 + k),  3, g_bead, TOKL_COLOR);
    n = (unsigned char)ur_score(&ur_g, 0);
    for (k = 0; k < n; k++) carve_char((unsigned char)(16 + k), 3, g_bead, TOKL_COLOR);
    n = count_at(1, UR_POS_START);
    for (k = 0; k < n; k++) carve_char((unsigned char)(8 + k),  10, g_bead, TOKD_COLOR);
    n = (unsigned char)ur_score(&ur_g, 1);
    for (k = 0; k < n; k++) carve_char((unsigned char)(16 + k), 10, g_bead, TOKD_COLOR);

    /* HUD below the board (pieces are colour discs, so name players by colour). */
    textbackground(COL_BG);             /* back to lapis after the carved cells */
    textcolor(COL_LABEL); gotoxy(0, 12); cputs("Turn: ");
    textcolor(ur_g.turn ? SPR_DARK_C : SPR_LIGHT_C);
    cputs(ur_g.turn ? "Dark " : "Light");
    textcolor(COL_LABEL);
    gotoxy(0, 13); cprintf("Light home:%u  Dark home:%u ",
                           (unsigned)ur_score(&ur_g, 0), (unsigned)ur_score(&ur_g, 1));
    if (roll != NO_ROLL) {
        textcolor(COL_TITLE); gotoxy(0, 14); cprintf("Roll: %u  ", roll);
    }
    if (msg) { textcolor(COL_LABEL); gotoxy(0, 23); cputs(msg); }
}

/* List the legal moves (deduped by source) starting at row 15, prompt, and read
 * a 1..N choice. Returns the chosen piece index, or -1 if there is no move. */
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

    /* Move list below the board (the HUD occupies rows 12-14). */
    textcolor(COL_TITLE); gotoxy(0, 14); cprintf("Roll: %u  ", roll);
    textcolor(COL_LABEL);
    for (i = 0; i < nsrc; i++) {
        pos = srcs[i];
        dest = (unsigned char)(pos + roll);
        gotoxy(0, (unsigned char)(15 + i));
        if (pos == UR_POS_START) cprintf("%u) ent->%u", i + 1, dest);
        else                     cprintf("%u) %u->%u", i + 1, pos, dest);
        if (dest == UR_POS_HOME)        cputs(" H");
        else if (ur_is_rosette(dest))   cputs(" *");
    }
    textcolor(COL_TITLE);
    gotoxy(0, 23); cprintf("Pick a move (1-%u): ", nsrc);

    do { c = get_key(); } while (c < '1' || c >= (int)('1' + nsrc));
    sel = (unsigned char)(c - '1');

    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (ur_g.piece[player][pieces[i]] == pos)
            return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

/* The turn loop now lives in the shared controller (src/common/ur_game.c). */

#ifndef UR_COLECO   /* ---- networking is Adam-only (the ColecoVision has none) -- */
/* ---- online mode (FujiNet N:TCP, server-authoritative, cross-platform) ------
 *
 * Speaks the SAME wire protocol (src/common/proto) as the Atari build, so the
 * Adam joins the SAME games on the server and cross-plays with Atari (and future
 * C64/Apple II) clients. Needs a real FujiNet on the AdamNet bus -- MAME has no
 * FujiNet emulation, so network_init() just fails gracefully under the emulator.
 */
#define UR_DEFAULT_HOST "thefnords.com"
static char     g_host[33] = UR_DEFAULT_HOST;        /* server host (configurable) */
static char     g_name[UR_NAME_LEN + 1] = "ADAM";    /* player name for JOIN       */
static char     g_net_url[48];
static uint16_t g_wins = 0;                          /* kept for appkey-blob parity */
static bool     g_loaded = false;                    /* profile pulled from appkey? */

/* FujiNet AppKeys (same IDs/layout as the Atari build, so the profile blob is
 * interchangeable). Creator 0x5552 = 'UR'. The lobby leaves the chosen server's
 * URL in creator 0x0001 / app 1 / key = our lobby appkey (6). */
#define UR_CREATOR_ID    0x5552u
#define UR_APP_ID        0x01
#define UR_KEY_PROFILE   0x00
#define UR_LOBBY_CREATOR 0x0001u
#define UR_LOBBY_APP     0x01
#define UR_LOBBY_APPKEY  0x06

static char *url_append(char *d, const char *s) { while (*s) *d++ = *s++; return d; }

static void build_net_url(void)
{
    char *p = g_net_url;
    p = url_append(p, "N:TCP://");
    p = url_append(p, g_host);
    p = url_append(p, ":1234/");
    *p = 0;
}

/* Small spin between network polls so we don't hammer the AdamNet bus. */
static void net_delay(void)
{
    volatile uint16_t i;
    for (i = 0; i < 4000u; i++) { /* spin */ }
}

static bool is_host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '-';
}

/* ---- persistent profile + lobby handoff (FujiNet AppKey) ----------------- *
 * All of these talk to the FujiNet, so they are only ever called from the
 * online/settings paths (never at boot) -- on a FujiNet-less machine they would
 * block like network_init(). Blob layout matches the Atari build exactly:
 * name[UR_NAME_LEN] (NUL-padded) + wins(2) + hostlen(1) + host[]. */
static bool profile_load(void)
{
    uint8_t  buf[MAX_APPKEY_LEN + 2];
    uint16_t cnt = 0;
    unsigned char i, n;

    fuji_set_appkey_details(UR_CREATOR_ID, UR_APP_ID, DEFAULT);
    if (!fuji_read_appkey(UR_KEY_PROFILE, &cnt, buf) || cnt < UR_NAME_LEN + 2)
        return false;
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

/* If the FujiNet lobby launched us, it left the chosen server's URL in its
 * handoff AppKey. Parse the host out (e.g. "tcp://host:1234/") into g_host. */
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

/* Blocking key read for text entry: unlike get_key() it does NOT filter to
 * printable, so it also returns RETURN (0x0D) and DELETE. Same settle debounce. */
static unsigned char get_raw_key(void)
{
    unsigned char k = eos_read_keyboard();
    settle();
    return k;
}

/* Edit a string (A-Z/0-9/space for names; host chars for hosts), RETURN ends,
 * DELETE/backspace erases. Upper-cases names. */
static void edit_field(char *dst, unsigned char maxlen, const char *title,
                       bool host)
{
    char tmp[34];
    unsigned char len = 0, k;
    unsigned char c;

    while (dst[len] && len < maxlen) { tmp[len] = dst[len]; len++; }
    tmp[len] = 0;

    textbackground(COL_BG); clrscr();
    textcolor(COL_TITLE); gotoxy(0, 0); cputs(title);
    textcolor(COL_LABEL);
    gotoxy(0, 2); cputs("Type, then RETURN.");
    gotoxy(0, 3); cputs("DELETE = back.");

    for (;;) {
        gotoxy(0, 6); cputs("> "); cputs(tmp); cputc('_');
        for (k = (unsigned char)(len + 3); k < maxlen + 4; k++) cputc(' ');
        c = get_raw_key();
        if (c == 0x0D || c == 0x0A) break;                 /* RETURN */
        if ((c == 0x08 || c == 0x7F || c == 0x97) && len) { /* DELETE/backspace */
            tmp[--len] = 0; continue;
        }
        if (len >= maxlen) continue;
        if (host) {
            if (is_host_char((char)c)) { tmp[len++] = (char)c; tmp[len] = 0; }
        } else {
            if (c >= 'a' && c <= 'z') c -= 32;             /* upper-case */
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') {
                tmp[len++] = (char)c; tmp[len] = 0;
            }
        }
    }
    while (len > 0 && tmp[len - 1] == ' ') tmp[--len] = 0;  /* trim trailing */
    for (k = 0; k <= len; k++) dst[k] = tmp[k];
}

static void set_name(void)
{
    edit_field(g_name, UR_NAME_LEN, "Set your name", false);
    g_loaded = true;
    profile_save();             /* persist (needs FujiNet; no-op/blocks without) */
}

static void set_host(void)
{
    edit_field(g_host, 32, "Set server host", true);
    if (g_host[0] == 0) {       /* empty -> restore default */
        const char *d = UR_DEFAULT_HOST; unsigned char i = 0;
        while (*d) g_host[i++] = *d++;
        g_host[i] = 0;
    }
    g_loaded = true;
    profile_save();
}

/* Wait for the next STATE snapshot. 1 = got one, 0 = disconnected/error. */
static int8_t read_state(ur_snapshot *snap)
{
    uint8_t  buf[UR_STATE_MSG_LEN];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n;

    for (;;) {
        if (network_status(g_net_url, &bw, &conn, &err) != FN_ERR_OK)
            return 0;
        if (bw >= UR_STATE_MSG_LEN)
            break;
        if (conn == 0)
            return 0;
        net_delay();
    }
    n = network_read(g_net_url, buf, UR_STATE_MSG_LEN);
    if (n < (int16_t)UR_STATE_MSG_LEN)
        return 0;
    return ur_proto_decode_state(buf, (uint8_t)n, snap) ? 1 : 0;
}

static void show_seat(const ur_snapshot *snap)
{
    textcolor(snap->seat ? SPR_DARK_C : SPR_LIGHT_C);
    gotoxy(0, 11);
    cprintf("You: %s", snap->seat ? "Dark " : "Light");
}

static void online_game(void)
{
    ur_snapshot snap;
    uint8_t cmd[2 + UR_NAME_LEN + 2];
    int8_t picked, rc;

    hide_sprites();
    textbackground(COL_BG); clrscr();
    textcolor(COL_TITLE); gotoxy(0, 0); cputs("The Royal Game of Ur");
    textcolor(COL_LABEL);

    if (network_init() != FN_ERR_OK) {
        gotoxy(0, 3); cputs("No FujiNet found. Key...");
        get_key(); return;
    }
    /* FujiNet present: pull the saved profile once, then let a lobby-chosen
     * server override the host, then build the device spec. */
    if (!g_loaded) { profile_load(); g_loaded = true; }
    lobby_host_from_appkey();
    build_net_url();

    if (network_open(g_net_url, OPEN_MODE_RW, 0) != FN_ERR_OK) {
        gotoxy(0, 3); cprintf("Can't reach %s", g_host);
        gotoxy(0, 5); cputs("Key..."); get_key();
        return;
    }
    network_write(g_net_url, cmd, ur_proto_join(cmd, g_name));

    gotoxy(0, 3); cprintf("Connecting to %s", g_host);
    gotoxy(0, 5); cputs("Waiting for an opponent...");
    gotoxy(0, 6); cputs("(computer joins after ~60s)");

    rc = read_state(&snap);
    if (rc == 0) {
        gotoxy(0, 8); cputs("Disconnected. Key...");
        get_key(); network_close(g_net_url); return;
    }

    for (;;) {
        ur_g = snap.state;
        if (snap.flags & UR_FLAG_CAPTURED)     sfx_capture();   /* sound the last move */
        else if (snap.flags & UR_FLAG_SCORED)  sfx_score();
        else if (snap.flags & UR_FLAG_ROSETTE) sfx_rosette();
        if (snap.phase == UR_PHASE_OVER) {
            if (snap.winner == (int8_t)snap.seat) sfx_win();
            plat_draw(NO_ROLL, snap.winner == (int8_t)snap.seat
                                ? "You win! Key..." : "You lose. Key...");
            get_key();
            break;
        }
        if (snap.state.turn != snap.seat) {            /* opponent (or AI) to act */
            plat_draw(snap.phase == UR_PHASE_MOVE ? snap.roll : NO_ROLL,
                       "Opponent's turn...");
            show_seat(&snap);
        } else if (snap.phase == UR_PHASE_ROLL) {      /* our roll */
            plat_draw(NO_ROLL, "Your turn - key to roll");
            show_seat(&snap);
            get_key();
            sfx_roll();
            network_write(g_net_url, cmd, ur_proto_roll(cmd));
        } else {                                       /* our move */
            plat_draw(snap.roll, (const char *)0);
            show_seat(&snap);
            picked = plat_choose_move(snap.seat, snap.roll);
            if (picked >= 0)
                network_write(g_net_url, cmd, ur_proto_move(cmd, (unsigned char)picked));
        }
        rc = read_state(&snap);
        if (rc == 0) { plat_draw(NO_ROLL, "Disconnected. Key..."); get_key(); break; }
    }
    network_close(g_net_url);
}

#endif /* !UR_COLECO (online block) */

int main(void)
{
    unsigned char key;
    unsigned char player;

    bordercolor(COL_BG);            /* lapis border around the screen */
    snd_silence();                  /* kill the SN76489 power-on drone at boot */

#if UR_SNDTEST  /* one-off: play every SFX in sequence (verify the SN76489) */
    textbackground(COL_BG); clrscr();
    textcolor(COL_TITLE); gotoxy(0, 0); cputs("Sound test");
    textcolor(COL_LABEL);
    gotoxy(0, 2); cputs("roll");    sfx_roll();    settle();
    gotoxy(0, 3); cputs("capture"); sfx_capture(); settle();
    gotoxy(0, 4); cputs("rosette"); sfx_rosette(); settle();
    gotoxy(0, 5); cputs("score");   sfx_score();   settle();
    gotoxy(0, 6); cputs("win");     sfx_win();     settle();
    gotoxy(0, 8); cputs("done");
    for (;;) settle();
#endif

#if UR_DEMO   /* boot straight into a representative board for visual iteration */
    ur_init(&ur_g);
    ur_g.piece[0][0] = 2;           /* a Light piece on the board */
    ur_g.piece[0][1] = 7;           /* Light piece on the shared lane */
    ur_g.piece[1][0] = 3;           /* a Dark piece on the board */
    ur_g.piece[1][1] = 8;           /* Dark piece on a central rosette */
    plat_draw(2, "Visual demo");
    plat_choose_move(0, 2);              /* draw the real move list, then block (stable) */
    for (;;) settle();
#endif

    for (;;) {
        hide_sprites();             /* no board tokens on the menu */
        textbackground(COL_BG);
        clrscr();
        textcolor(COL_TITLE); gotoxy(0, 0);  cputs("The Royal Game of Ur");
        textcolor(COL_LABEL); gotoxy(0, 1);  cputs("Ur - Mesopotamia - c.2600 BCE");
#ifdef UR_COLECO
        gotoxy(0, 2);  cputs("ColecoVision");
        gotoxy(0, 5);  cputs("1) Two players");
        gotoxy(0, 6);  cputs("2) One player vs computer");
        gotoxy(0, 8);  cputs("Keypad 1/2 picks; FIRE rolls.");
        textcolor(COL_TITLE); gotoxy(0, 13); cputs("Select (1-2):");
#else
        gotoxy(0, 2);  cputs("Coleco Adam");
        gotoxy(0, 5);  cputs("1) Two players");
        gotoxy(0, 6);  cputs("2) One player vs computer");
        gotoxy(0, 7);  cputs("3) Online (FujiNet)");
        gotoxy(0, 8);  cputs("4) Set name");
        gotoxy(0, 9);  cputs("5) Set server");
        textcolor(COL_LABEL); gotoxy(0, 11); cprintf("name:%s host:%s", g_name, g_host);
        textcolor(COL_TITLE); gotoxy(0, 13); cputs("Select (1-5):");
#endif
        /* With thanks to the scholar who reconstructed the rules. */
        textcolor(COL_LABEL);
        gotoxy(0, 22); cputs("Rules by Dr Irving Finkel,");
        gotoxy(0, 23); cputs("British Museum - with thanks.");

        play_hymn();                /* the Hurrian Hymn (once at boot, skippable) */

        /* Read the menu choice (get_key waits for one fresh press and folds it
         * + its timing into the RNG seed; the RNG is seeded at the first roll). */
        key = get_key();
#ifndef UR_COLECO
        if (key == '3') { online_game(); continue; }   /* cross-platform online */
        if (key == '4') { set_name();    continue; }
        if (key == '5') { set_host();    continue; }
#endif
        if (key != '1' && key != '2')
            continue;

        player = ur_run_game(key == '2' ? 1 : 0);   /* shared controller turn loop */

        plat_draw(NO_ROLL, player == 0 ? "Light wins! Key..."
                                        : "Dark wins! Key...");
        get_key();
    }
    return 0;
}
