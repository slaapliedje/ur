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
#include "fujinet-fuji.h"      /* fuji_*_appkey: persistent profile on the FujiNet SD */

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
#define UR_DEFAULT_HOST "localhost"   /* server host; runtime-configurable (menu 7) */
#define DIE_MARKED   '^'
#define DIE_UNMARKED '_'

/* FujiNet AppKey (persistent SD storage) for the local player profile. Creator
 * IDs 0x0000-0x00FF are reserved by FujiNet for internal use; apps use > 0xFF.
 * 0x5552 = 'UR'. There is no official registry yet, so this should be confirmed
 * with the FujiNet project (same conversation as the lobby appkey). */
#define UR_CREATOR_ID  0x5552u
#define UR_APP_ID      0x01
#define UR_KEY_PROFILE 0x00

static ur_state game;

static char     g_name[4] = "";  /* 3 initials + NUL; empty = not set yet  */
static uint16_t g_wins    = 0;   /* games won vs the computer (persisted)  */
static char     g_host[33] = UR_DEFAULT_HOST;  /* server host/IP (<=32, persisted) */
static char     g_net_url[64];   /* built: N:TCP://<host>:1234/            */
static char     g_top_url[64];   /* built: N:HTTP://<host>:8080/top        */

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
        if (network_status(g_net_url, &bw, &conn, &err) != FN_ERR_OK)
            return false;
        if (bw >= UR_STATE_MSG_LEN)
            break;
        if (conn == 0)
            return false;
        atari_wait_frames(3);          /* ~20 polls/s: gentle on NetSIO */
    }
    n = network_read(g_net_url, buf, UR_STATE_MSG_LEN);
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
    uint8_t cmd[6];             /* JOIN is 5 bytes (type, version, 3-char name) */
    int8_t picked;

    if (network_init() != FN_ERR_OK) {
        draw_all(NO_ROLL, "network_init failed. FIRE/key."); wait_action(); return;
    }
    if (network_open(g_net_url, OPEN_MODE_RW, 0) != FN_ERR_OK) {
        draw_all(NO_ROLL, "connect failed. FIRE/key."); wait_action(); return;
    }
    network_write(g_net_url, cmd, ur_proto_join(cmd, g_name));

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
            network_write(g_net_url, cmd, ur_proto_roll(cmd));
            continue;
        }
        /* your turn, choose a move (shared cursor chooser) */
        picked = choose_move(snap.seat, snap.roll);
        if (picked >= 0)
            network_write(g_net_url, cmd, ur_proto_move(cmd, (unsigned char)picked));
    }
    network_close(g_net_url);
}

/* ---- persistent player profile (FujiNet AppKey) ------------------------- */

static const char NAME_ALPH[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";   /* 27 chars */

static char *url_append(char *d, const char *s)
{
    while (*s) *d++ = *s++;
    return d;
}

/* Build the N:TCP / N:HTTP device specs from the configured host. Ports fixed. */
static void build_urls(void)
{
    char *p;
    p = g_net_url;
    p = url_append(p, "N:TCP://");
    p = url_append(p, g_host);
    p = url_append(p, ":1234/");
    *p = 0;
    p = g_top_url;
    p = url_append(p, "N:HTTP://");
    p = url_append(p, g_host);
    p = url_append(p, ":8080/top");
    *p = 0;
}

static bool is_host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '-';
}

/* Load name + win count from our appkey. False if no FujiNet/SD (keeps defaults). */
static bool profile_load(void)
{
    uint8_t  buf[MAX_APPKEY_LEN + 2];
    uint16_t cnt = 0;
    unsigned char i;

    fuji_set_appkey_details(UR_CREATOR_ID, UR_APP_ID, DEFAULT);
    if (!fuji_read_appkey(UR_KEY_PROFILE, &cnt, buf) || cnt < 5)
        return false;
    for (i = 0; i < 3; i++)
        g_name[i] = (buf[i] >= 'A' && buf[i] <= 'Z') ? (char)buf[i] : ' ';
    g_name[3] = 0;
    if (g_name[0] == ' ' && g_name[1] == ' ' && g_name[2] == ' ')
        g_name[0] = 0;                       /* all-blank == unset */
    g_wins = (uint16_t)(buf[3] | ((uint16_t)buf[4] << 8));
    if (cnt >= 6) {                          /* optional host string follows */
        unsigned char hl = buf[5];
        if (hl > 0 && hl <= 32 && (uint16_t)(6 + hl) <= cnt) {
            for (i = 0; i < hl; i++)
                g_host[i] = (char)buf[6 + i];
            g_host[hl] = 0;
        }
    }
    return true;
}

/* Persist name + win count. Silently no-ops if no FujiNet is attached. */
static void profile_save(void)
{
    uint8_t buf[40];
    unsigned char hl = 0, i;
    buf[0] = (uint8_t)(g_name[0] ? g_name[0] : ' ');
    buf[1] = (uint8_t)(g_name[0] ? g_name[1] : ' ');
    buf[2] = (uint8_t)(g_name[0] ? g_name[2] : ' ');
    buf[3] = (uint8_t)(g_wins & 0xFF);
    buf[4] = (uint8_t)(g_wins >> 8);
    while (g_host[hl] && hl < 32)            /* append the configured host */
        hl++;
    buf[5] = hl;
    for (i = 0; i < hl; i++)
        buf[6 + i] = (uint8_t)g_host[i];
    fuji_set_appkey_details(UR_CREATOR_ID, UR_APP_ID, DEFAULT);
    fuji_write_appkey(UR_KEY_PROFILE, (uint16_t)(6 + hl), buf);
}

static void draw_initials(const unsigned char *idx, unsigned char slot)
{
    unsigned char i;
    for (i = 0; i < 3; i++) {
        if (i == slot) revers(1);
        cputcxy((unsigned char)(18 + i * 2), 11, NAME_ALPH[idx[i]]);
        if (i == slot) revers(0);
    }
}

/* Arcade-style 3-initial entry on the joystick; saves to the appkey on finish. */
static void enter_name(void)
{
    unsigned char idx[3], slot, s, centered, i;
    char c;

    atari_text_mode();
    clrscr();
    revers(1); cputsxy(9, 2, " ENTER YOUR INITIALS "); revers(0);
    cputsxy(3, 6,  "Stick up/down: change letter");
    cputsxy(3, 7,  "Stick left/right: move");
    cputsxy(3, 8,  "FIRE: set (on the 3rd = done)");
    cputsxy(3, 20, "(needs FujiNet to be remembered)");

    for (i = 0; i < 3; i++) {
        c = (g_name[0] && g_name[i] >= 'A' && g_name[i] <= 'Z') ? g_name[i] : 'A';
        idx[i] = (unsigned char)(c - 'A');
    }
    slot = 0; centered = 1;
    draw_initials(idx, slot);
    while (atari_trig()) { }

    for (;;) {
        if (atari_trig()) {
            while (atari_trig()) { }
            if (slot == 2) break;
            slot++;
            draw_initials(idx, slot);
            continue;
        }
        s = atari_stick();
        if ((s & 0x0F) == 0x0F) {
            centered = 1;
        } else if (centered) {
            if (!(s & 0x01))      idx[slot] = (unsigned char)((idx[slot] + 1)  % 27);  /* up   */
            else if (!(s & 0x02)) idx[slot] = (unsigned char)((idx[slot] + 26) % 27);  /* down */
            else if (!(s & 0x08)) { if (slot < 2) slot++; }                            /* right */
            else if (!(s & 0x04)) { if (slot > 0) slot--; }                            /* left  */
            draw_initials(idx, slot);
            centered = 0;
        }
    }

    for (i = 0; i < 3; i++)
        g_name[i] = NAME_ALPH[idx[i]];
    g_name[3] = 0;
    if (g_name[0] == ' ' && g_name[1] == ' ' && g_name[2] == ' ')
        g_name[0] = 0;
    profile_save();
    atari_mode4_board();
}

/* Keyboard entry of the server host/IP. Rebuilds the N: URLs and saves it.
 * Joystick is hopeless for a hostname, so this reads the keyboard. */
static void enter_host(void)
{
    char tmp[33];
    unsigned char len = 0, k;
    char c;

    while (g_host[len] && len < 32) { tmp[len] = g_host[len]; len++; }
    tmp[len] = 0;

    atari_text_mode();
    clrscr();
    revers(1); cputsxy(10, 2, " SET SERVER HOST "); revers(0);
    cputsxy(2, 5,  "Type the host name or IP, then");
    cputsxy(2, 6,  "press RETURN.  DELETE = back.");
    cputsxy(2, 8,  "Ports are fixed: 1234 (game),");
    cputsxy(2, 9,  "8080 (leaderboard).");
    cputsxy(2, 12, "Host:");

    for (;;) {
        gotoxy(2, 14);
        cputs(tmp);
        cputc('_');
        for (k = len; k < 34; k++)               /* clear leftovers after edits */
            cputc(' ');
        c = cgetc();
        if (c == 0x9B || c == '\n' || c == '\r')
            break;
        if ((c == 0x7E || c == 0x08) && len > 0) {
            len--; tmp[len] = 0;
        } else if (len < 32 && is_host_char(c)) {
            tmp[len++] = c; tmp[len] = 0;
        }
    }

    if (len == 0) {                              /* empty -> back to default */
        const char *d = UR_DEFAULT_HOST;
        while (*d) tmp[len++] = *d++;
        tmp[len] = 0;
    }
    for (k = 0; k <= len; k++)
        g_host[k] = tmp[k];
    build_urls();
    profile_save();
    atari_mode4_board();
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
    gotoxy(0, 0);
    cprintf("Server: %s", g_host);          /* where Online connects (menu 7) */
    revers(1);
    cputsxy(9, 1, " THE ROYAL GAME OF UR ");
    revers(0);
    cputsxy(5, 2, "Ur - Mesopotamia - c.2600 BCE");
    if (g_name[0]) {
        gotoxy(9, 3);
        cprintf("Player %s   Wins %u", g_name, g_wins);
    }

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

    cputsxy(3, 19, "1) Two players");    cputsxy(22, 19, "2) vs Computer");
    cputsxy(3, 20, "3) Online");         cputsxy(22, 20, "4) How to play");
    cputsxy(3, 21, "5) Set name");       cputsxy(22, 21, "6) Leaderboard");
    cputsxy(3, 22, "7) Set server host");
    cputsxy(3, 23, "Select 1-7:");

    do { key = cgetc(); } while (key < '1' || key > '7');
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

/* Fetch the compact /top leaderboard over N:HTTP and show it. The body is
 * 1 count byte then up to 10 records of name[3] + wins (uint16 LE). */
static void show_leaderboard(void)
{
    uint8_t  buf[64];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n = 0;
    unsigned char count, i, base;
    char name[4];
    uint16_t wins;

    atari_text_mode();
    clrscr();
    revers(1); cputsxy(13, 0, " LEADERBOARD "); revers(0);

    if (network_init() != FN_ERR_OK ||
        network_open(g_top_url, 4 /* HTTP GET */, 0) != FN_ERR_OK) {
        cputsxy(2, 4, "Could not reach the server.");
        cputsxy(2, 6, "Needs FujiNet and the Ur server's");
        cputsxy(2, 7, "web port (8080) reachable.");
        cputsxy(2, 23, "FIRE or a key to return");
        wait_action();
        atari_mode4_board();
        return;
    }

    for (i = 0; i < 100; i++) {                 /* wait briefly for the body */
        if (network_status(g_top_url, &bw, &conn, &err) != FN_ERR_OK)
            break;
        if (bw > 0) { n = network_read(g_top_url, buf, sizeof(buf)); break; }
        if (conn == 0)
            break;
        atari_wait_frames(3);
    }
    network_close(g_top_url);

    if (n < 1) {
        cputsxy(2, 4, "No reply from server.");
    } else if (buf[0] == 0) {
        cputsxy(2, 4, "No games recorded yet.");
    } else {
        count = buf[0];
        cputsxy(3, 2, "#   NAME   WINS");
        for (i = 0; i < count; i++) {
            base = (unsigned char)(1 + i * 5);
            if ((int16_t)(base + 5) > n)
                break;
            name[0] = (char)buf[base];
            name[1] = (char)buf[base + 1];
            name[2] = (char)buf[base + 2];
            name[3] = 0;
            wins = (uint16_t)(buf[base + 3] | ((uint16_t)buf[base + 4] << 8));
            gotoxy(3, (unsigned char)(4 + i));
            cprintf("%-2u  %s    %u", i + 1, name, wins);
        }
    }
    cputsxy(2, 23, "FIRE or a key to return");
    wait_action();
    atari_mode4_board();
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

    profile_load();             /* name/wins/host from the FujiNet appkey, if any */
    build_urls();               /* N: URLs from the saved (or default) host */

    for (;;) {                  /* play again forever; never falls out to Memo Pad */
        do {
            key = title_screen();
            if (key == '4')      show_instructions();
            else if (key == '5') enter_name();
            else if (key == '6') show_leaderboard();
            else if (key == '7') enter_host();
        } while (key == '4' || key == '5' || key == '6' || key == '7');

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
        if (ai[1]) {
            if (player == 0) {          /* you beat the computer: record it */
                g_wins++;
                profile_save();
            }
            show_result(player == 0 ? " YOU WIN! " : " YOU LOSE ");
        } else {
            show_result(player == 0 ? " LIGHT (WHITE) WINS! " : " DARK (GREEN) WINS! ");
        }
    }
    return 0;                   /* not reached: the play-again loop never exits */
}
