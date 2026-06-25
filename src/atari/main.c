/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari 8-bit Royal Game of Ur — playable client.
 *
 * Vertical board: 8 rows x 3 columns. Left column = Light (white), middle column
 * = the shared capture lane, right column = Dark (green). Info sits above and
 * below the board because ANTIC's colour (mode 4) is per screen-row. Modes:
 * local hot-seat, one player vs the AI, and online via FujiNet (N: TCP).
 *
 * All rules + the AI come from the shared core (src/common/ur); the protocol
 * from src/common/proto. This file only renders, reads input, and talks N:.
 */
#include <stdint.h>
#include <stdbool.h>
#include <conio.h>

#include "ur.h"
#include "proto.h"
#include "atarihw.h"
#include "fujinet-network.h"

/* Board cells: row 1..8, col 0=Light(left) 1=shared(mid) 2=Dark(right). */
#define BOARD_X    14
#define BOARD_Y    4
#define ROW_TURN   1
#define ROW_ROLL   2
#define ROW_SEAT   3        /* online: which seat you are */
#define ROW_MOVE   19       /* move description (below the board) */
#define ROW_LIGHT  20
#define ROW_DARK   21
#define ROW_MSG    23

#define LIGHT_CH '#'        /* mode-4 light piece glyph (white) */
#define DARK_CH  '@'        /* mode-4 dark piece glyph (green)  */
#define NO_ROLL  0xFF
#define UR_NET_URL "N:TCP://localhost:1234/"
#define DIE_MARKED   '^'
#define DIE_UNMARKED '_'

static ur_state game;

static unsigned char cellx(unsigned char col) { return (unsigned char)(BOARD_X + col * 4); }
static unsigned char celly(unsigned char row) { return (unsigned char)(BOARD_Y + (row - 1) * 2); }

/* Path position (1..14) -> board cell (row 1..8, col 0..2). False if off-board. */
static bool pos_to_cell(unsigned char player, unsigned char pos,
                        unsigned char *row, unsigned char *col)
{
    if (pos < 1 || pos > 14)
        return false;
    if (pos <= 4) {                       /* private entry: rows 4,3,2,1 */
        *col = player ? 2 : 0;
        *row = (unsigned char)(5 - pos);
    } else if (pos <= 12) {               /* shared middle column: rows 1..8 */
        *col = 1;
        *row = (unsigned char)(pos - 4);
    } else {                              /* private exit: 13->row8, 14->row7 */
        *col = player ? 2 : 0;
        *row = (pos == 13) ? 8 : 7;
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
    char grid[9][3];
    unsigned char row, col, pl, i, pos, rr, cc;

    clrscr();
    cputsxy(0, 0, "The Royal Game of Ur");

    /* base board: '+' playable, ' ' cut-away, '*' rosettes.
     * Middle column is the full shared lane; the side columns skip rows 5-6. */
    for (row = 1; row <= 8; row++)
        for (col = 0; col < 3; col++)
            grid[row][col] = (col == 1 || row <= 4 || row >= 7) ? '+' : ' ';
    grid[1][0] = '*'; grid[7][0] = '*';   /* Light rosettes  (entry, exit) */
    grid[4][1] = '*';                      /* central shared rosette */
    grid[1][2] = '*'; grid[7][2] = '*';   /* Dark rosettes */

    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = game.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                grid[rr][cc] = pl ? DARK_CH : LIGHT_CH;
        }

    for (row = 1; row <= 8; row++)
        for (col = 0; col < 3; col++) {
            char ch = grid[row][col];
            if (ch == ' ')
                continue;
            if (ch == '*')
                revers(1);
            cputcxy(cellx(col), celly(row), ch);
            if (ch == '*')
                revers(0);
        }

    gotoxy(0, ROW_TURN);
    cprintf("Turn: %s", game.turn ? "Dark (green)" : "Light (white)");
    if (roll != NO_ROLL) {
        gotoxy(0, ROW_ROLL);
        cputs("Roll:");
        for (i = 0; i < 4; i++)
            cputc(i < roll ? DIE_MARKED : DIE_UNMARKED);
        cprintf(" %u", roll);
    }

    gotoxy(0, ROW_LIGHT);
    cprintf("Light  start:%u home:%u", count_at(0, UR_POS_START), ur_score(&game, 0));
    gotoxy(0, ROW_DARK);
    cprintf("Dark   start:%u home:%u", count_at(1, UR_POS_START), ur_score(&game, 1));

    if (msg && msg[0])
        cputsxy(0, ROW_MSG, msg);
}

static void seed_rng(void)
{
    volatile unsigned char *RANDOM = (volatile unsigned char *)0xD20A;
    uint16_t s = (uint16_t)(((uint16_t)*RANDOM << 8) ^ (uint16_t)*RANDOM);
    ur_rng_seed(s);
}

static const char *win_msg(unsigned char player)
{
    return player ? "Dark (green) wins!  FIRE/key."
                  : "Light (white) wins! FIRE/key.";
}

/* Move the PMG highlight onto a piece's board cell (or hide it if home). */
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
    while (atari_trig()) { }
    for (;;) {
        if (kbhit()) { cgetc(); return; }
        if (atari_trig()) return;
    }
}

/* Highlight move option `sel` and describe it on ROW_MOVE. */
static void show_option(unsigned char player, const unsigned char *srcs,
                        unsigned char nsrc, unsigned char sel, unsigned char roll)
{
    unsigned char src  = srcs[sel];
    unsigned char dest = (unsigned char)(src + roll);
    unsigned char cell = (src == UR_POS_START) ? dest : src;   /* on-board 1..14 */
    unsigned char rr, cc;

    if (pos_to_cell(player, cell, &rr, &cc))
        atari_pmg_highlight(cellx(cc), celly(rr));

    gotoxy(0, ROW_MOVE);
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

/* Joystick/number-key move chooser, shared by hot-seat and online play. The
 * highlight cursor moves over the legal options; FIRE (or 1..N) selects.
 * Returns the chosen piece index, or -1 if there is no legal move. */
static int8_t choose_move(unsigned char player, unsigned char roll)
{
    unsigned char pieces[UR_PIECES], srcs[UR_PIECES];
    unsigned char count, nsrc, i, j, pos, sel, centered, s;
    bool seen;
    char key;

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

    draw_all(roll, "Stick: choose   FIRE/1-N: move");
    sel = 0;
    show_option(player, srcs, nsrc, sel, roll);
    while (atari_trig()) { }            /* don't inherit the roll's trigger press */
    centered = 1;
    for (;;) {
        if (kbhit()) {
            key = cgetc();
            if (key >= '1' && key < (char)('1' + nsrc)) { sel = (unsigned char)(key - '1'); break; }
        }
        if (atari_trig())
            break;
        s = atari_stick();
        if ((s & 0x0F) == 0x0F) {
            centered = 1;
        } else if (centered) {
            if (!(s & 0x04) || !(s & 0x01))   /* left/up -> previous */
                sel = (unsigned char)((sel + nsrc - 1) % nsrc);
            else                              /* right/down -> next */
                sel = (unsigned char)((sel + 1) % nsrc);
            show_option(player, srcs, nsrc, sel, roll);
            centered = 0;
        }
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

    draw_all(NO_ROLL, "Roll: press FIRE or a key");
    wait_action();
    roll = ur_dice_roll();
    sfx_roll();

    picked = choose_move(player, roll);
    if (picked < 0) {
        draw_all(roll, "No legal move. FIRE/key.");
        wait_action();
        ur_advance_turn(&game, (const ur_move_result *)0);
        return false;
    }

    ur_apply_move(&game, player, (unsigned char)picked, roll, &res);
    sfx_for_result(&res);
    highlight_dest(player, game.piece[player][(unsigned char)picked]);

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

    draw_all(roll, "Computer moved - FIRE/key.");
    gotoxy(0, ROW_MOVE);
    if (pos == UR_POS_START)
        cprintf("CPU: enter -> %u", dest);
    else
        cprintf("CPU: %u -> %u", pos, dest);
    if (res.captured)      cprintf("  capture!");
    else if (res.scored)   cprintf("  home!");
    else if (res.rosette)  cprintf("  rosette!");
    wait_action();

    if (res.won) {
        draw_all(NO_ROLL, win_msg(player));
        wait_action();
        return true;
    }
    ur_advance_turn(&game, &res);
    return false;
}

/* ---- online mode (FujiNet N:TCP, server-authoritative) ------------------ */

static bool read_state(ur_snapshot *snap)
{
    uint8_t  buf[UR_STATE_MSG_LEN];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n;

    for (;;) {
        if (network_status(UR_NET_URL, &bw, &conn, &err) != FN_ERR_OK)
            return false;
        if (bw >= UR_STATE_MSG_LEN)
            break;
        if (conn == 0)
            return false;
        atari_wait_frames(3);          /* ~20 polls/s: gentle on NetSIO */
    }
    n = network_read(UR_NET_URL, buf, UR_STATE_MSG_LEN);
    if (n < (int16_t)UR_STATE_MSG_LEN)
        return false;
    return ur_proto_decode_state(buf, (uint8_t)n, snap);
}

static void show_seat(const ur_snapshot *snap)
{
    gotoxy(0, ROW_SEAT);
    cprintf("You: %s", snap->seat ? "Dark (green)" : "Light (white)");
}

static void online_game(void)
{
    ur_snapshot snap;
    uint8_t cmd[4];
    int8_t picked;

    if (network_init() != FN_ERR_OK) {
        draw_all(NO_ROLL, "network_init failed. FIRE/key."); wait_action(); return;
    }
    if (network_open(UR_NET_URL, OPEN_MODE_RW, 0) != FN_ERR_OK) {
        draw_all(NO_ROLL, "connect failed. FIRE/key."); wait_action(); return;
    }
    network_write(UR_NET_URL, cmd, ur_proto_join(cmd));

    for (;;) {
        if (!read_state(&snap)) {
            draw_all(NO_ROLL, "Disconnected. FIRE/key."); wait_action(); break;
        }
        game = snap.state;
        if (snap.flags & UR_FLAG_CAPTURED)      sfx_capture();
        else if (snap.flags & UR_FLAG_SCORED)   sfx_score();
        else if (snap.flags & UR_FLAG_ROSETTE)  sfx_rosette();

        if (snap.phase == UR_PHASE_OVER) {
            draw_all(NO_ROLL, (snap.winner == (int8_t)snap.seat)
                              ? "You win!  FIRE/key." : "You lose.  FIRE/key.");
            show_seat(&snap);
            wait_action();
            break;
        }
        if (snap.state.turn != snap.seat) {
            draw_all(snap.phase == UR_PHASE_MOVE ? snap.roll : NO_ROLL, "Opponent's turn...");
            show_seat(&snap);
            continue;                          /* loop reads the next STATE */
        }
        if (snap.phase == UR_PHASE_ROLL) {
            draw_all(NO_ROLL, "Your turn - FIRE/key to roll");
            show_seat(&snap);
            wait_action();
            sfx_roll();
            network_write(UR_NET_URL, cmd, ur_proto_roll(cmd));
            continue;
        }
        /* your turn, choose a move (shared cursor chooser) */
        picked = choose_move(snap.seat, snap.roll);
        if (picked >= 0)
            network_write(UR_NET_URL, cmd, ur_proto_move(cmd, (unsigned char)picked));
    }
    network_close(UR_NET_URL);
}

int main(void)
{
    bool ai[UR_NUM_PLAYERS];
    char key;

    seed_rng();

    clrscr();
    cputsxy(0, 0, "The Royal Game of Ur");
    cputsxy(0, 3, "1) Two players (hot-seat)");
    cputsxy(0, 4, "2) One player vs computer");
    cputsxy(0, 5, "3) Online (FujiNet)");
    cputsxy(0, 7, "Select (1-3):");
    do {
        key = cgetc();
    } while (key < '1' || key > '3');
    ai[0] = false;              /* you are Light */
    ai[1] = (key == '2');       /* Dark is the computer in mode 2 */

    atari_setup_colors();
    atari_setup_charset();
    atari_mode4_board();
    atari_pmg_init();
    atari_quiet_sio();          /* no OS SIO "drive" drone during FujiNet polling */

    if (key == '3') {
        online_game();
        return 0;
    }

    ur_init(&game);
    for (;;) {
        unsigned char player = game.turn;
        bool over = ai[player] ? computer_turn(player) : human_turn(player);
        if (over)
            break;
    }
    return 0;
}
