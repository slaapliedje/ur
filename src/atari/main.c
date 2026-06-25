/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari 8-bit — playable Royal Game of Ur (Phase 3).
 *
 * Text-mode (conio) UI: draws the board, pieces and dice, and drives a full
 * game in two modes — local hot-seat 2-player, or one human (Light) vs the
 * computer (Dark). ALL rules and the AI come from the shared, tested core
 * (src/common/ur) — this file only renders and reads input. Prettier ANTIC /
 * player-missile graphics are a later polish pass.
 *
 * Build: make atari  ->  build/atari/ur.xex   (boot in Altirra)
 */
#include <stdint.h>
#include <stdbool.h>
#include <conio.h>

#include "ur.h"
#include "atarihw.h"

#define ROW_T    0          /* top    — Light's private rows */
#define ROW_M    1          /* middle — shared (capture zone) */
#define ROW_B    2          /* bottom — Dark's private rows */
#define BOARD_X  8
#define BOARD_Y  6

#define LIGHT_CH '#'   /* board glyph: mode-4 light piece (white) */
#define DARK_CH  '@'   /* board glyph: mode-4 dark piece (green)  */
#define NO_ROLL  0xFF

static ur_state game;

static unsigned char cellx(unsigned char col) { return (unsigned char)(BOARD_X + (col - 1) * 2); }
static unsigned char celly(unsigned char row) { return (unsigned char)(BOARD_Y + row * 2); }

/* Map a path position (1..14) for a player to a board cell (row, col 1..8).
 * Returns false for off-board positions (start / home). */
static bool pos_to_cell(unsigned char player, unsigned char pos,
                        unsigned char *row, unsigned char *col)
{
    if (pos < 1 || pos > 14)
        return false;
    if (pos <= 4) {                 /* private entry: cols 4,3,2,1 */
        *row = player ? ROW_B : ROW_T;
        *col = (unsigned char)(5 - pos);
    } else if (pos <= 12) {         /* shared middle: cols 1..8 */
        *row = ROW_M;
        *col = (unsigned char)(pos - 4);
    } else {                        /* private exit: 13->col8, 14->col7 */
        *row = player ? ROW_B : ROW_T;
        *col = (pos == 13) ? 8 : 7;
    }
    return true;
}

static unsigned char count_at(unsigned char player, unsigned char pos)
{
    unsigned char i, n = 0;
    for (i = 0; i < UR_PIECES; i++)
        if (game.piece[player][i] == pos)
            n++;
    return n;
}

static bool opp_on(unsigned char player, unsigned char pos)
{
    return count_at((unsigned char)(1 - player), pos) > 0;
}

static void draw_all(unsigned char roll, const char *msg)
{
    char grid[3][9];
    unsigned char r, c, pl, i, pos, rr, cc;

    clrscr();
    cputsxy(0, 0, "The Royal Game of Ur");

    for (r = 0; r < 3; r++)
        for (c = 1; c <= 8; c++)
            grid[r][c] = (r == ROW_M || c <= 4 || c >= 7) ? '+' : ' ';
    grid[ROW_T][1] = '*'; grid[ROW_T][7] = '*';
    grid[ROW_M][4] = '*';
    grid[ROW_B][1] = '*'; grid[ROW_B][7] = '*';

    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = game.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                grid[rr][cc] = pl ? DARK_CH : LIGHT_CH;
        }

    for (r = 0; r < 3; r++)
        for (c = 1; c <= 8; c++) {
            char ch = grid[r][c];
            if (ch == ' ')
                continue;
            if (ch == '*')
                revers(1);          /* rosette -> COLOR3 (orange) in mode 4 */
            cputcxy(cellx(c), celly(r), ch);
            if (ch == '*')
                revers(0);
        }

    gotoxy(0, 2);
    cprintf("Turn: %s", game.turn ? "Dark (green)" : "Light (white)");
    if (roll != NO_ROLL) {
        gotoxy(22, 2);
        cprintf("Roll: %u", roll);
    }

    gotoxy(0, 13);
    cprintf("Light  start:%u home:%u",
            count_at(0, UR_POS_START), ur_score(&game, 0));
    gotoxy(0, 14);
    cprintf("Dark   start:%u home:%u",
            count_at(1, UR_POS_START), ur_score(&game, 1));

    if (msg && msg[0])
        cputsxy(0, 16, msg);
}

/* Seed the core RNG from POKEY's hardware random register ($D20A). */
static void seed_rng(void)
{
    volatile unsigned char *RANDOM = (volatile unsigned char *)0xD20A;
    uint16_t s = (uint16_t)(((uint16_t)*RANDOM << 8) ^ (uint16_t)*RANDOM);
    ur_rng_seed(s);
}

static const char *win_msg(unsigned char player)
{
    return player ? "Dark (X) wins!  Press a key."
                  : "Light (O) wins!  Press a key.";
}

/* Move the PMG highlight box onto a piece's board cell (or hide it if home). */
static void highlight_dest(unsigned char player, unsigned char dest)
{
    unsigned char rr, cc;
    if (pos_to_cell(player, dest, &rr, &cc))
        atari_pmg_highlight(cellx(cc), celly(rr));
    else
        atari_pmg_hide();
}

static void sfx_for_result(const ur_move_result *r)
{
    if (r->won)           sfx_win();
    else if (r->captured) sfx_capture();
    else if (r->scored)   sfx_score();
    else if (r->rosette)  sfx_rosette();
    else                  sfx_move();
}

/* Wait for the player to acknowledge: a fresh trigger press or any key. */
static void wait_action(void)
{
    while (atari_trig()) { }            /* release a held trigger first */
    for (;;) {
        if (kbhit()) { cgetc(); return; }
        if (atari_trig()) return;
    }
}

/* Highlight move option `sel` (its cell) and print a one-line description. */
static void show_option(unsigned char player, const unsigned char *srcs,
                        unsigned char nsrc, unsigned char sel, unsigned char roll)
{
    unsigned char src  = srcs[sel];
    unsigned char dest = (unsigned char)(src + roll);
    unsigned char cell = (src == UR_POS_START) ? dest : src;   /* on-board 1..14 */
    unsigned char rr, cc;

    if (pos_to_cell(player, cell, &rr, &cc))
        atari_pmg_highlight(cellx(cc), celly(rr));

    gotoxy(0, 18);
    cprintf("Move %u/%u: ", sel + 1, nsrc);
    if (src == UR_POS_START)
        cprintf("enter -> %u  ", dest);
    else
        cprintf("%u -> %u  ", src, dest);
    if (dest == UR_POS_HOME)                              cprintf("(home)    ");
    else if (ur_is_rosette(dest))                         cprintf("(rosette) ");
    else if (ur_is_shared(dest) && opp_on(player, dest))  cprintf("(capture) ");
    else                                                  cprintf("          ");
}

/* The computer takes a full turn for `player`. Returns true if the game is over. */
static bool computer_turn(unsigned char player)
{
    unsigned char pieces[UR_PIECES];
    unsigned char roll, pos, dest;
    int8_t pick;
    ur_move_result res;

    draw_all(NO_ROLL, "Computer's turn - FIRE/key.");
    wait_action();
    roll = ur_dice_roll();
    sfx_roll();

    if (ur_legal_moves(&game, player, roll, pieces) == 0) {
        draw_all(roll, "Computer has no move. FIRE/key.");
        wait_action();
        ur_advance_turn(&game, (const ur_move_result *)0);
        return false;
    }

    pick = ur_ai_pick(&game, player, roll);
    pos  = game.piece[player][(unsigned char)pick];
    dest = (unsigned char)(pos + roll);
    ur_apply_move(&game, player, (unsigned char)pick, roll, &res);
    sfx_for_result(&res);
    highlight_dest(player, game.piece[player][(unsigned char)pick]);

    draw_all(roll, "Computer moved:");
    gotoxy(0, 17);
    if (pos == UR_POS_START)
        cprintf("CPU: enter -> %u", dest);
    else
        cprintf("CPU: %u -> %u", pos, dest);
    if (res.captured)      cprintf("  capture!");
    else if (res.scored)   cprintf("  home!");
    else if (res.rosette)  cprintf("  rosette!");
    cputsxy(0, 19, "Press FIRE or a key.");
    wait_action();

    if (res.won) {
        draw_all(NO_ROLL, win_msg(player));
        wait_action();
        return true;
    }
    ur_advance_turn(&game, &res);
    return false;
}

/* A human takes a full turn for `player`. Returns true if the game is over. */
static bool human_turn(unsigned char player)
{
    unsigned char pieces[UR_PIECES], srcs[UR_PIECES];
    unsigned char roll, count, nsrc, picked, i, j, pos;
    unsigned char sel, centered, s;
    bool seen;
    char key;
    ur_move_result res;

    draw_all(NO_ROLL, "Roll: press FIRE or a key");
    wait_action();
    roll = ur_dice_roll();
    sfx_roll();

    count = ur_legal_moves(&game, player, roll, pieces);
    if (count == 0) {
        draw_all(roll, "No legal move. FIRE/key.");
        wait_action();
        ur_advance_turn(&game, (const ur_move_result *)0);
        return false;
    }

    /* collapse identical source squares into one menu entry */
    nsrc = 0;
    for (i = 0; i < count; i++) {
        pos = game.piece[player][pieces[i]];
        seen = false;
        for (j = 0; j < nsrc; j++)
            if (srcs[j] == pos) { seen = true; break; }
        if (!seen)
            srcs[nsrc++] = pos;
    }

    /* Joystick cursor: move the highlight over the options, FIRE to pick.
     * Number keys 1..N still work as a fallback. */
    draw_all(roll, "Stick: choose   FIRE/1-N: move");
    sel = 0;
    show_option(player, srcs, nsrc, sel, roll);
    while (atari_trig()) { }            /* don't inherit the roll's trigger press */
    centered = 1;
    for (;;) {
        if (kbhit()) {
            key = cgetc();
            if (key >= '1' && key < (char)('1' + nsrc)) {
                sel = (unsigned char)(key - '1');
                break;
            }
        }
        if (atari_trig())
            break;
        s = atari_stick();
        if ((s & 0x0F) == 0x0F) {
            centered = 1;                          /* stick centred */
        } else if (centered) {
            if (!(s & 0x04) || !(s & 0x01))        /* left or up -> previous */
                sel = (unsigned char)((sel + nsrc - 1) % nsrc);
            else                                   /* right or down -> next  */
                sel = (unsigned char)((sel + 1) % nsrc);
            show_option(player, srcs, nsrc, sel, roll);
            centered = 0;
        }
    }

    pos = srcs[sel];
    picked = pieces[0];
    for (i = 0; i < count; i++)
        if (game.piece[player][pieces[i]] == pos) { picked = pieces[i]; break; }

    ur_apply_move(&game, player, picked, roll, &res);
    sfx_for_result(&res);
    highlight_dest(player, game.piece[player][picked]);

    if (res.won) {
        draw_all(NO_ROLL, win_msg(player));
        wait_action();
        return true;
    }
    if (res.captured || res.rosette) {
        draw_all(NO_ROLL, res.captured ? "Capture!  FIRE/key."
                                       : "Rosette - roll again! FIRE/key.");
        wait_action();
    }
    ur_advance_turn(&game, &res);
    return false;
}

int main(void)
{
    bool ai[UR_NUM_PLAYERS];
    char key;

    seed_rng();

    /* mode select */
    clrscr();
    cputsxy(0, 0, "The Royal Game of Ur");
    cputsxy(0, 3, "1) Two players (hot-seat)");
    cputsxy(0, 4, "2) One player vs computer");
    cputsxy(0, 6, "Select (1/2):");
    do {
        key = cgetc();
    } while (key != '1' && key != '2');
    ai[0] = false;              /* you are Light */
    ai[1] = (key == '2');       /* Dark is the computer in mode 2 */

    atari_setup_colors();
    atari_setup_charset();
    atari_mode4_board();
    atari_pmg_init();
    ur_init(&game);

    for (;;) {
        unsigned char player = game.turn;
        bool over = ai[player] ? computer_turn(player) : human_turn(player);
        if (over)
            break;
    }
    return 0;
}
