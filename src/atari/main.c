/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari 8-bit — playable Royal Game of Ur (Phase 3, local hot-seat 2-player).
 *
 * Text-mode (conio) UI for now: draws the board, pieces and dice, and drives a
 * full hot-seat game. ALL rules come from the shared, tested core (src/common/ur)
 * — this file only renders and reads input. Prettier ANTIC / player-missile
 * graphics are a later polish pass.
 *
 * Build: make atari  ->  build/atari/ur.xex   (boot in Altirra)
 */
#include <stdint.h>
#include <stdbool.h>
#include <conio.h>

#include "ur.h"

/* Board rows and screen placement. Columns are 1..8. */
#define ROW_T    0          /* top    — Light's private rows */
#define ROW_M    1          /* middle — shared (capture zone) */
#define ROW_B    2          /* bottom — Dark's private rows */
#define BOARD_X  8
#define BOARD_Y  6

#define LIGHT_CH 'O'
#define DARK_CH  'X'
#define NO_ROLL  0xFF

static ur_state game;

static unsigned char cellx(unsigned char col) { return (unsigned char)(BOARD_X + (col - 1) * 3); }
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

/* Does the opponent of `player` sit on path position `pos`? (capture annotation) */
static bool opp_on(unsigned char player, unsigned char pos)
{
    return count_at((unsigned char)(1 - player), pos) > 0;
}

static void draw_all(unsigned char roll, const char *msg)
{
    char grid[3][9];
    unsigned char r, c, pl, i, pos, rr, cc;

    clrscr();
    cputsxy(0, 0, "The Royal Game of Ur  (hot-seat)");

    /* base board: '.' playable, ' ' the cut-away corners, '*' rosettes */
    for (r = 0; r < 3; r++)
        for (c = 1; c <= 8; c++)
            grid[r][c] = (r == ROW_M || c <= 4 || c >= 7) ? '.' : ' ';
    grid[ROW_T][1] = '*'; grid[ROW_T][7] = '*';
    grid[ROW_M][4] = '*';
    grid[ROW_B][1] = '*'; grid[ROW_B][7] = '*';

    /* overlay pieces */
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = game.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                grid[rr][cc] = pl ? DARK_CH : LIGHT_CH;
        }

    for (r = 0; r < 3; r++)
        for (c = 1; c <= 8; c++)
            if (grid[r][c] != ' ')
                cputcxy(cellx(c), celly(r), grid[r][c]);

    gotoxy(0, 2);
    cprintf("Turn: %s", game.turn ? "Dark (X)" : "Light (O)");
    if (roll != NO_ROLL) {
        gotoxy(22, 2);
        cprintf("Roll: %u", roll);
    }

    gotoxy(0, 13);
    cprintf("Light O  start:%u home:%u",
            count_at(0, UR_POS_START), ur_score(&game, 0));
    gotoxy(0, 14);
    cprintf("Dark  X  start:%u home:%u",
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

int main(void)
{
    unsigned char pieces[UR_PIECES], srcs[UR_PIECES];
    unsigned char player, roll, count, nsrc, picked, i, j, pos, dest;
    bool seen;
    char key;
    ur_move_result res;

    seed_rng();
    ur_init(&game);

    for (;;) {
        player = game.turn;

        draw_all(NO_ROLL, "Press any key to roll...");
        cgetc();
        roll = ur_dice_roll();

        count = ur_legal_moves(&game, player, roll, pieces);
        if (count == 0) {
            draw_all(roll, "No legal move. Press a key.");
            cgetc();
            ur_advance_turn(&game, (const ur_move_result *)0);
            continue;
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

        draw_all(roll, "Choose a move:");
        for (i = 0; i < nsrc; i++) {
            dest = (unsigned char)(srcs[i] + roll);
            gotoxy(0, (unsigned char)(17 + i));
            if (srcs[i] == UR_POS_START)
                cprintf("%u) enter -> %u", i + 1, dest);
            else
                cprintf("%u) %u -> %u", i + 1, srcs[i], dest);
            if (dest == UR_POS_HOME)
                cprintf(" (home)");
            else if (ur_is_rosette(dest))
                cprintf(" (rosette)");
            else if (ur_is_shared(dest) && opp_on(player, dest))
                cprintf(" (capture)");
        }

        do {
            key = cgetc();
        } while (key < '1' || key >= (char)('1' + nsrc));

        /* pick any legal piece sitting on the chosen source square */
        pos = srcs[key - '1'];
        picked = pieces[0];
        for (i = 0; i < count; i++)
            if (game.piece[player][pieces[i]] == pos) { picked = pieces[i]; break; }

        ur_apply_move(&game, player, picked, roll, &res);

        if (res.won) {
            draw_all(NO_ROLL, player ? "Dark (X) wins!  Press a key."
                                     : "Light (O) wins!  Press a key.");
            cgetc();
            break;
        }
        if (res.captured || res.rosette) {
            draw_all(NO_ROLL, res.captured ? "Capture!  Press a key."
                                           : "Rosette - roll again! Press a key.");
            cgetc();
        }
        ur_advance_turn(&game, &res);
    }

    return 0;
}
