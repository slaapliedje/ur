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

/* Board cells: row 1..8, col 0=Light(left) 1=shared(mid) 2=Dark(right).
 * Each cell is two characters wide (8 mode-4 pixels) for detailed glyphs. */
#define BOARD_X    16       /* 3 cells, 2 chars each + gaps, centred on 40 cols */
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

static unsigned char cellx(unsigned char col) { return (unsigned char)(BOARD_X + col * 3); }
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
            char rch;                    /* right-half glyph of the cell */
            if (ch == ' ')
                continue;
            switch (ch) {
                case '+': rch = '='; break;
                case '*': rch = '&'; break;
                case '#': rch = '$'; break;
                case '@': rch = '['; break;
                default:  rch = ch;  break;
            }
            if (ch == '*') revers(1);
            cputcxy(cellx(col),     celly(row), ch);
            cputcxy(cellx(col) + 1, celly(row), rch);
            if (ch == '*') revers(0);
        }

    /* Off-board pieces: those waiting to enter at the top corners, those borne
     * off ("home") at the bottom corners. Light (white discs) left, Dark (green
     * rings) right. start+home <= 7 per side, so the stacks never collide. */
    {
        unsigned char k, n;
        n = count_at(0, UR_POS_START);          /* Light waiting   -> top-left    */
        for (k = 0; k < n; k++) { cputcxy(2, (unsigned char)(4 + k), '#'); cputcxy(3, (unsigned char)(4 + k), '$'); }
        n = (unsigned char)ur_score(&game, 0);  /* Light borne off -> bottom-left  */
        for (k = 0; k < n; k++) { cputcxy(2, (unsigned char)(18 - k), '#'); cputcxy(3, (unsigned char)(18 - k), '$'); }
        n = count_at(1, UR_POS_START);          /* Dark waiting    -> top-right    */
        for (k = 0; k < n; k++) { cputcxy(36, (unsigned char)(4 + k), '@'); cputcxy(37, (unsigned char)(4 + k), '['); }
        n = (unsigned char)ur_score(&game, 1);  /* Dark borne off  -> bottom-right */
        for (k = 0; k < n; k++) { cputcxy(36, (unsigned char)(18 - k), '@'); cputcxy(37, (unsigned char)(18 - k), '['); }
    }

    /* Start markers: a coloured up-arrow in the cut-away notch directly below each
     * player's entry square (pos 1 at celly(4)=row 10; the notch is row 12).
     * Pieces enter at the diamond above and run up their side column. */
    cputcxy(16, 12, '{'); cputcxy(17, 12, '|'); /* white: Light starts (left)  */
    cputcxy(22, 12, '}'); cputcxy(23, 12, '~'); /* green: Dark starts (right)  */

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

/* Centre a string on a text row. */
static void center(unsigned char y, const char *s)
{
    unsigned char n = 0;
    while (s[n]) n++;
    cputsxy((unsigned char)((40 - n) / 2), y, s);
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

    if (res.won)
        return true;
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

    if (res.won)
        return true;
    ur_advance_turn(&game, &res);
    return false;
}

/* End-of-game screen: the finished board (the winner's seven pieces sit borne-off
 * in the corner tray) under a result banner, with a play-again prompt. */
static void show_result(const char *banner)
{
    draw_all(NO_ROLL, "");
    revers(1); center(ROW_TURN, banner); revers(0);
    center(ROW_MSG, "Press FIRE or a key to play again");
    wait_action();
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
            show_result(snap.winner == (int8_t)snap.seat ? " YOU WIN! " : " YOU LOSE ");
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

/* Draw a horizontal run of `n` copies of `ch` at (x,y); `inv` selects the gold
 * (COLOR3) variant of a mode-4 glyph instead of lapis (COLOR2). */
static void hrun(unsigned char x, unsigned char y, unsigned char n, char ch, bool inv)
{
    unsigned char i;
    if (inv) revers(1);
    gotoxy(x, y);
    for (i = 0; i < n; i++)
        cputc(ch);
    if (inv) revers(0);
}

/* Sumerian title screen + mode select. Title/menu text live on the mode-2 rows;
 * the ziggurat scene is drawn in the mode-4 colour band (rows 4..18). Returns the
 * chosen mode key ('1'..'3'). The ']' glyph is a solid block, '\' a cuneiform
 * wedge, '*'+'&' the rosette (drawn inverse = gold). */
static char title_screen(void)
{
    char key;

    clrscr();
    revers(1);
    cputsxy(9, 1, " THE ROYAL GAME OF UR ");
    revers(0);
    cputsxy(5, 2, "Ur - Mesopotamia - c.2600 BCE");

    hrun(6, 5, 28, '\\', false);              /* upper cuneiform frieze (lapis) */

    revers(1);                                /* rosette "sun" over the apex (gold) */
    cputcxy(19, 7, '*'); cputcxy(20, 7, '&');
    revers(0);

    hrun(19,  8,  2, ']', true);              /* ziggurat: gold stepped tiers */
    hrun(18,  9,  4, ']', true);
    hrun(17, 10,  6, ']', true);
    hrun(16, 11,  8, ']', true);
    hrun(15, 12, 10, ']', true);
    hrun(14, 13, 12, ']', true);
    hrun( 8, 14, 24, ']', false);             /* lapis ground band */

    hrun(6, 16, 28, '\\', false);             /* lower cuneiform frieze */

    cputsxy(8, 19, "1) Two players (hot-seat)");
    cputsxy(8, 20, "2) One player vs computer");
    cputsxy(8, 21, "3) Online (FujiNet)");
    cputsxy(8, 22, "4) How to play");
    cputsxy(8, 23, "Select 1-4:");

    do { key = cgetc(); } while (key < '1' || key > '4');
    return key;
}

/* Draw an empty board (tiles + rosettes, no pieces) for the path demo. */
static void demo_board(void)
{
    unsigned char row, col;
    char ch;
    for (row = 1; row <= 8; row++)
        for (col = 0; col < 3; col++) {
            if (!(col == 1 || row <= 4 || row >= 7))
                continue;                       /* cut-away cell */
            ch = ((col != 1 && (row == 1 || row == 7)) || (col == 1 && row == 4)) ? '*' : '+';
            if (ch == '*') revers(1);
            cputcxy(cellx(col),     celly(row), ch);
            cputcxy(cellx(col) + 1, celly(row), ch == '*' ? '&' : '=');
            if (ch == '*') revers(0);
        }
}

/* Manual page 3: animate a growing line along each player's 14-step path - white
 * up the left and across the shared middle, then green up the right - looping
 * until the player presses a key/FIRE. Uses the mode-4 colour band. */
static void show_path_demo(void)
{
    unsigned char p, pos, r, c;
    char done = 0;

    atari_mode4_board();
    while (atari_trig()) { }                     /* drop the trigger that got us here */

    while (!done) {
        for (p = 0; p < 2 && !done; p++) {
            clrscr();
            revers(1); cputsxy(0, 0, " THE PATH                         3/3 "); revers(0);
            cputsxy(0, 1, p == 0 ? "WHITE: up the left, across the"
                                 : "GREEN: up the right, across the");
            cputsxy(0, 2, "shared middle, then off home.");
            cputsxy(0, 23, "FIRE or a key: back to menu");
            demo_board();
            for (pos = 1; pos <= 14; pos++) {
                if (pos_to_cell(p, pos, &r, &c)) {
                    cputcxy(cellx(c),     celly(r), p == 0 ? '#' : '@');
                    cputcxy(cellx(c) + 1, celly(r), p == 0 ? '$' : '[');
                }
                atari_wait_frames(16);
                if (kbhit() || atari_trig()) { done = 1; break; }
            }
            if (!done) atari_wait_frames(40);
            if (kbhit() || atari_trig()) done = 1;
        }
    }
    if (kbhit()) cgetc();                         /* consume the exit key */
}

/* Three manual pages: two of rules text, then an animated path demo. Flips the
 * board rows to mode-2 text for the text pages; the demo restores the colour band. */
static void show_instructions(void)
{
    atari_text_mode();

    clrscr();
    revers(1); cputsxy(0, 0, " HOW TO PLAY                      1/3 "); revers(0);
    cputsxy(0, 2,  "Race your 7 pieces from your start,");
    cputsxy(0, 3,  "along the track and off the far end.");
    cputsxy(0, 4,  "First to bear all 7 off wins!");
    cputsxy(0, 6,  "THE BOARD (vertical)");
    cputsxy(0, 7,  " Light (white) runs up the left,");
    cputsxy(0, 8,  " Dark (green) up the right; the");
    cputsxy(0, 9,  " middle column is shared ground.");
    cputsxy(0, 11, "THE DICE");
    cputsxy(0, 12, " Roll 4 dice (FIRE or a key). The");
    cputsxy(0, 13, " marked corners showing, 0 to 4,");
    cputsxy(0, 14, " is how far you move. 0 = no move.");
    cputsxy(0, 16, "YOUR MOVE");
    cputsxy(0, 17, " Push the stick to pick a legal move");
    cputsxy(0, 18, " (the box shows it), FIRE to play it.");
    cputsxy(0, 19, " Bring on a new piece, or move one");
    cputsxy(0, 20, " already on the track.");
    cputsxy(0, 23, "        FIRE or a key for more...");
    wait_action();

    clrscr();
    revers(1); cputsxy(0, 0, " HOW TO PLAY                      2/3 "); revers(0);
    cputsxy(0, 2,  "ROSETTES (the flower squares)");
    cputsxy(0, 3,  " Land exactly on a rosette to earn");
    cputsxy(0, 4,  " an extra roll - and there you are");
    cputsxy(0, 5,  " safe: you cannot be captured.");
    cputsxy(0, 7,  "CAPTURE");
    cputsxy(0, 8,  " In the shared middle column, land");
    cputsxy(0, 9,  " on an opponent's piece to send it");
    cputsxy(0, 10, " home to their start. (Rosettes are");
    cputsxy(0, 11, " always safe.)");
    cputsxy(0, 13, "BEARING OFF");
    cputsxy(0, 14, " To take a piece off the board you");
    cputsxy(0, 15, " must roll the EXACT count needed.");
    cputsxy(0, 17, "WINNING");
    cputsxy(0, 18, " The first to bear off all seven");
    cputsxy(0, 19, " pieces wins. A game fit for kings!");
    cputsxy(0, 23, "     FIRE or a key: see the path...");
    wait_action();

    show_path_demo();           /* page 3: animated white/green path */
}

int main(void)
{
    bool ai[UR_NUM_PLAYERS];
    char key;
    unsigned char player;
    bool over;

    seed_rng();

    atari_setup_colors();
    atari_setup_charset();
    atari_mode4_board();
    atari_pmg_init();
    atari_quiet_sio();          /* no OS SIO "drive" drone during FujiNet polling */

    for (;;) {                  /* play again forever; never falls out to Memo Pad */
        do {
            key = title_screen();
            if (key == '4')
                show_instructions();
        } while (key == '4');

        if (key == '3') {
            online_game();      /* shows its own result, then back to the menu */
            continue;
        }

        ai[0] = false;          /* you are Light */
        ai[1] = (key == '2');   /* Dark is the computer in mode 2 */

        ur_init(&game);
        for (;;) {
            player = game.turn;
            over = ai[player] ? computer_turn(player) : human_turn(player);
            if (over)
                break;
        }

        /* `player` is the winner */
        if (ai[1])
            show_result(player == 0 ? " YOU WIN! " : " YOU LOSE ");
        else
            show_result(player == 0 ? " LIGHT (WHITE) WINS! " : " DARK (GREEN) WINS! ");
    }
    return 0;                   /* not reached: the play-again loop never exits */
}
