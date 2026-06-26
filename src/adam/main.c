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
#include <conio.h>

#include "ur.h"

#define LIGHT_CH 'O'        /* Light pieces */
#define DARK_CH  'X'        /* Dark pieces  */
#define ROSE_CH  '*'        /* rosette      */
#define TILE_CH  '.'        /* empty cell   */
#define NO_ROLL  0xFF

static ur_state game;

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

static unsigned char count_at(unsigned char pl, unsigned char pos)
{
    unsigned char i, n = 0;
    for (i = 0; i < UR_PIECES; i++)
        if (game.piece[pl][i] == pos)
            n++;
    return n;
}

/* Compact board geometry (kept within ~24 columns for the Adam console). */
static unsigned char cellx(unsigned char col) { return (unsigned char)(10 + col * 3); }
static unsigned char celly(unsigned char row) { return (unsigned char)(2 + row); }

static void draw_board(unsigned char roll, const char *msg)
{
    char grid[9][3];
    unsigned char row, col, pl, i, pos, rr, cc;

    clrscr();
    gotoxy(0, 0); cputs("Royal Game of Ur");
    gotoxy(0, 1); cprintf("Turn: %s", game.turn ? "Dark (X)" : "Light (O)");

    for (row = 1; row <= 8; row++)
        for (col = 0; col < 3; col++)
            grid[row][col] = (col == 1 || row <= 4 || row >= 7) ? TILE_CH : ' ';
    grid[1][0] = ROSE_CH; grid[7][0] = ROSE_CH;     /* Light rosettes */
    grid[4][1] = ROSE_CH;                            /* central rosette */
    grid[1][2] = ROSE_CH; grid[7][2] = ROSE_CH;     /* Dark rosettes */

    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = game.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                grid[rr][cc] = pl ? DARK_CH : LIGHT_CH;
        }

    for (row = 1; row <= 8; row++)
        for (col = 0; col < 3; col++)
            if (grid[row][col] != ' ') {
                gotoxy(cellx(col), celly(row));
                cputc(grid[row][col]);
            }

    gotoxy(0, 12); cprintf("O home:%u  X home:%u",
                           (unsigned)ur_score(&game, 0), (unsigned)ur_score(&game, 1));
    if (roll != NO_ROLL) { gotoxy(0, 13); cprintf("Roll: %u   ", roll); }
    if (msg) { gotoxy(0, 23); cputs(msg); }
}

/* List the legal moves (deduped by source) starting at row 15, prompt, and read
 * a 1..N choice. Returns the chosen piece index, or -1 if there is no move. */
static int8_t choose_move(unsigned char player, unsigned char roll)
{
    unsigned char pieces[UR_PIECES], srcs[UR_PIECES];
    unsigned char count, nsrc, i, j, pos, dest, sel;
    bool seen;
    int c;

    count = ur_legal_moves(&game, player, roll, pieces);
    if (count == 0)
        return -1;

    nsrc = 0;
    for (i = 0; i < count; i++) {
        pos = game.piece[player][pieces[i]];
        seen = false;
        for (j = 0; j < nsrc; j++)
            if (srcs[j] == pos) { seen = true; break; }
        if (!seen)
            srcs[nsrc++] = pos;
    }

    for (i = 0; i < nsrc; i++) {
        pos = srcs[i];
        dest = (unsigned char)(pos + roll);
        gotoxy(0, (unsigned char)(15 + i));
        if (pos == UR_POS_START) cprintf("%u) enter -> %u", i + 1, dest);
        else                     cprintf("%u) %u -> %u", i + 1, pos, dest);
        if (dest == UR_POS_HOME)        cputs("  home");
        else if (ur_is_rosette(dest))   cputs("  rose");
    }
    gotoxy(0, 23); cprintf("Pick a move (1-%u): ", nsrc);

    do { c = cgetc(); } while (c < '1' || c >= (int)('1' + nsrc));
    sel = (unsigned char)(c - '1');

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

    draw_board(NO_ROLL, "Press a key to roll.");
    cgetc();
    roll = ur_dice_roll();

    picked = choose_move(player, roll);
    if (picked < 0) {
        draw_board(roll, "No legal move. Key...");
        cgetc();
        ur_advance_turn(&game, (const ur_move_result *)0);
        return false;
    }

    ur_apply_move(&game, player, (unsigned char)picked, roll, &res);
    if (res.won)
        return true;
    if (res.captured || res.rosette) {
        draw_board(roll, res.captured ? "Capture! Key..." : "Rosette - again! Key...");
        cgetc();
    }
    ur_advance_turn(&game, &res);
    return false;
}

static bool computer_turn(unsigned char player)
{
    unsigned char pieces[UR_PIECES], roll;
    int8_t pick;
    ur_move_result res;

    draw_board(NO_ROLL, "Computer's turn. Key...");
    cgetc();
    roll = ur_dice_roll();

    if (ur_legal_moves(&game, player, roll, pieces) == 0) {
        draw_board(roll, "Computer: no move. Key...");
        cgetc();
        ur_advance_turn(&game, (const ur_move_result *)0);
        return false;
    }

    pick = ur_ai_pick(&game, player, roll);
    ur_apply_move(&game, player, (unsigned char)pick, roll, &res);
    draw_board(roll, "Computer moved. Key...");
    cgetc();
    if (res.won)
        return true;
    ur_advance_turn(&game, &res);
    return false;
}

int main(void)
{
    uint16_t seed = 0;
    int key;
    bool ai1;
    unsigned char player;
    bool over;

    for (;;) {
        clrscr();
        gotoxy(0, 0);  cputs("The Royal Game of Ur");
        gotoxy(0, 2);  cputs("Coleco Adam");
        gotoxy(0, 5);  cputs("1) Two players");
        gotoxy(0, 6);  cputs("2) One player vs computer");
        gotoxy(0, 8);  cputs("Select (1-2):");

        /* seed the RNG from how long the player takes to choose */
        while (!kbhit()) seed++;
        key = cgetc();
        if (key != '1' && key != '2')
            continue;
        ur_rng_seed((uint16_t)(seed | 1u));
        ai1 = (key == '2');

        ur_init(&game);
        for (;;) {
            player = game.turn;
            over = (player == 1 && ai1) ? computer_turn(player) : human_turn(player);
            if (over)
                break;
        }

        draw_board(NO_ROLL, player == 0 ? "Light (O) wins! Key..."
                                        : "Dark (X) wins! Key...");
        cgetc();
    }
    return 0;
}
