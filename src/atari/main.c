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
#include "ur_game.h"        /* shared local-game controller + plat.h interface */
#include "proto.h"
#include "atarihw.h"
#include "music.h"             /* the Hurrian Hymn title theme (shared melody) */
#ifndef UR_A5200               /* the 5200 is a cartridge console: no FujiNet/SIO */
#include "fujinet-network.h"
#include "fujinet-fuji.h"      /* fuji_*_appkey: persistent profile on the FujiNet SD */
#endif

/* HORIZONTAL board (like the SMS): 3 rows x 8 cols, row 0=Light(top) 1=shared(mid)
 * 2=Dark(bottom). Each cell is 2x2 chars (16x16). Laid out inside the mode-4 board
 * band (char rows 3..18) so the display list + DLI are unchanged. */
#define BOARD_X    12       /* 8 cells x 2 chars = 16 wide, centred on 40 cols    */
#define BOARD_Y    6        /* 3 cells x 2 char-rows -> rows 6..11                */
#define LTRAY_Y    4        /* Light tray (above the board, in the mode-4 band)   */
#define DTRAY_Y    13       /* Dark tray  (below the board, in the mode-4 band)   */
#define TRAY_WX    12       /* waiting beads start col;  home beads at TRAY_HX    */
#define TRAY_HX    21
#define ROW_TURN   1
#define ROW_ROLL   2
#define ROW_SEAT   3        /* online: which seat you are */
#define ROW_MOVE   19       /* move list (rows 19..22, below the mode-4 board band) */
#define ROW_MSG    23

#define LIGHT_CH '#'        /* grid marker: light piece (rendered as a PMG disc) */
#define DARK_CH  '@'        /* grid marker: dark piece  (rendered as a PMG disc) */
#define PIP_L    '('        /* cream pip dot (l/r halves) shown under a Dark disc */
#define PIP_R    ')'
#define CURSOR_CH '\''      /* move cursor: gold pointer left of the selected cell */
#define ANIM_STEP 6         /* frames per cell while a piece glides (~0.1s/cell) */
#define ANIM_FLY  3         /* frames per cell for a captured piece flying back (faster) */
#define NO_ROLL  0xFF
#define UR_DEFAULT_HOST "localhost"   /* server host; runtime-configurable (menu 7) */
#define DIE_MARKED   '^'
#define DIE_UNMARKED '_'

/* POKEY RANDOM register: $D20A on the A8, $E80A on the 5200 (POKEY at $E800). */
#ifdef UR_A5200
#define POKEY_RND_ADDR 0xE80A
#else
#define POKEY_RND_ADDR 0xD20A
#endif

/* FujiNet AppKey (persistent SD storage) for the local player profile. Creator
 * IDs 0x0000-0x00FF are reserved by FujiNet for internal use; apps use > 0xFF.
 * 0x5552 = 'UR'. There is no official registry yet, so this should be confirmed
 * with the FujiNet project (same conversation as the lobby appkey). */
#define UR_CREATOR_ID  0x5552u
#define UR_APP_ID      0x01
#define UR_KEY_PROFILE 0x00

/* FujiNet lobby handoff: when the lobby boots our client it writes the chosen
 * server's URL into AppKey creator 0x0001 / app 0x01 / key = our lobby appkey.
 * Reading it lets us auto-connect to the lobby-selected server. (Convention
 * confirmed against the FujiNet lobby + 5 Card Stud client.) */
#define UR_LOBBY_CREATOR 0x0001u
#define UR_LOBBY_APP     0x01
#define UR_LOBBY_APPKEY  0x06     /* matches the server's lobby appkey (UR_APPKEY=6) */

static char     g_name[UR_NAME_LEN + 1] = "";  /* player name + NUL; empty = unset */
static uint16_t g_wins    = 0;   /* games won vs the computer (persisted)  */
static char     g_host[33] = UR_DEFAULT_HOST;  /* server host/IP (<=32, persisted) */
static char     g_net_url[64];   /* built: N:TCP://<host>:1234/            */
static char     g_top_url[64];   /* built: N:HTTP://<host>:8080/top        */

static unsigned char cellx(unsigned char col) { return (unsigned char)(BOARD_X + col * 2); }
static unsigned char celly(unsigned char row) { return (unsigned char)(BOARD_Y + row * 2); }

/* Draw a 16x16 carved cell box (2x2 chars) at board (col,row): a raised lapis
 * tile, or a gold rosette (drawn inverse). Tokens are overlaid centred on top so a
 * piece sits INSIDE its box rather than standing on a small button. */
/* Cell kinds: every square is an inlaid mosaic (SMS parity). 0 = DOTS (carved tile
 * + white quincunx, private lanes), 1 = ROSE (gold flower), 2 = EYE (gold bullseye,
 * the shared lane). ROSE + EYE are gold, drawn inverse ("11"->COLOR3). */
#define CELL_DOTS 0
#define CELL_ROSE 1
#define CELL_EYE  2
static void draw_box(unsigned char col, unsigned char row, unsigned char kind)
{
    unsigned char x = cellx(col), y = celly(row);
    const char *g = (kind == CELL_ROSE) ? "*&\";"      /* rosette glyphs */
                  : (kind == CELL_EYE)  ? ",>?`"       /* eye glyphs     */
                                        : "+=<%";      /* dots glyphs    */
    if (kind != CELL_DOTS) revers(1);                  /* gold (inverse) for rose/eye */
    cputcxy(x,                    y,                    g[0]);   /* TL */
    cputcxy((unsigned char)(x+1), y,                    g[1]);   /* TR */
    cputcxy(x,                    (unsigned char)(y+1), g[2]);   /* BL */
    cputcxy((unsigned char)(x+1), (unsigned char)(y+1), g[3]);   /* BR */
    if (kind != CELL_DOTS) revers(0);
}

/* Path position (1..14) -> board cell. HORIZONTAL: row 0=Light, 1=shared, 2=Dark;
 * cols 0..7. (Same mapping as the SMS/C64/GB ports.) False if off-board. */
static bool pos_to_cell(unsigned char player, unsigned char pos,
                        unsigned char *row, unsigned char *col)
{
    if (pos < 1 || pos > 14)
        return false;
    if (pos <= 4) {                       /* private entry */
        *row = player ? 2 : 0;
        *col = (unsigned char)(4 - pos);
    } else if (pos <= 12) {               /* shared middle row */
        *row = 1;
        *col = (unsigned char)(pos - 5);
    } else {                              /* private exit */
        *row = player ? 2 : 0;
        *col = (pos == 13) ? 7 : 6;
    }
    return true;
}

/* H-shape: the shared middle row spans all 8 cols; the private rows skip the
 * bridge (cols 4-5). Rosettes at the 4 block corners + the shared centre. */
static bool cell_exists(unsigned char row, unsigned char col)
{
    return row == 1 || col <= 3 || col >= 6;
}
static bool is_rosette_cell(unsigned char row, unsigned char col)
{
    return (row != 1 && (col == 0 || col == 6)) || (row == 1 && col == 3);
}

static unsigned char count_at(unsigned char player, unsigned char pos)
{
    unsigned char i, n = 0;
    for (i = 0; i < UR_PIECES; i++)
        if (ur_g.piece[player][i] == pos)
            n++;
    return n;
}

static bool opp_on(unsigned char player, unsigned char pos)
{
    return count_at((unsigned char)(1 - player), pos) > 0;
}

static unsigned char cur_cx = 0xFF, cur_cy;   /* last cursor char cell; 0xFF = none */

void plat_draw(unsigned char roll, const char *msg)
{
    unsigned char row, col, pl, i, pos, rr, cc, k, n, x, y;

    clrscr();
    cur_cx = 0xFF;                    /* clrscr wiped any cursor glyph */
    cputsxy(0, 0, "The Royal Game of Ur");

    /* Carved, inlaid board cells (skip the H-shape cut-away): gold rosette flowers
     * at the rosette squares, a gold eye down the shared lane, carved quincunx dots
     * on the private lanes. Every square is decorated. */
    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++) {
            if (!cell_exists(row, col))
                continue;
            draw_box(col, row, is_rosette_cell(row, col) ? CELL_ROSE
                             : (row == 1) ? CELL_EYE : CELL_DOTS);
        }

    /* On-board tokens: 2x2 charset discs — white Light ('#$()'), green Dark
     * ('@[{|'). (Horizontal rows can't use PMG, which is per-column.) */
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = ur_g.piece[pl][i];
            if (!pos_to_cell(pl, pos, &rr, &cc))
                continue;
            x = cellx(cc); y = celly(rr);
            if (pl == 0) {
                cputcxy(x, y, '#'); cputcxy((unsigned char)(x+1), y, '$');
                cputcxy(x, (unsigned char)(y+1), '('); cputcxy((unsigned char)(x+1), (unsigned char)(y+1), ')');
            } else {
                cputcxy(x, y, '@'); cputcxy((unsigned char)(x+1), y, '[');
                cputcxy(x, (unsigned char)(y+1), '{'); cputcxy((unsigned char)(x+1), (unsigned char)(y+1), '|');
            }
        }

    /* Trays (charset beads, inside the board band): Light above ('}' white), Dark
     * below ('~' green); waiting clustered left, borne-off "home" to the right. */
    n = count_at(0, UR_POS_START);
    for (k = 0; k < n; k++) cputcxy((unsigned char)(TRAY_WX + k), LTRAY_Y, '}');
    n = (unsigned char)ur_score(&ur_g, 0);
    for (k = 0; k < n; k++) cputcxy((unsigned char)(TRAY_HX + k), LTRAY_Y, '}');
    n = count_at(1, UR_POS_START);
    for (k = 0; k < n; k++) cputcxy((unsigned char)(TRAY_WX + k), DTRAY_Y, '~');
    n = (unsigned char)ur_score(&ur_g, 1);
    for (k = 0; k < n; k++) cputcxy((unsigned char)(TRAY_HX + k), DTRAY_Y, '~');

    atari_board_tint(ur_g.turn);     /* frame the board in the active player's hue */
    gotoxy(0, ROW_TURN);
    cprintf("Turn: %s", ur_g.turn ? "Dark (green)" : "Light (white)");
    if (roll != NO_ROLL) {
        gotoxy(0, ROW_ROLL);
        cputs("Roll:");
        for (i = 0; i < 4; i++)
            cputc(i < roll ? DIE_MARKED : DIE_UNMARKED);
        cprintf(" %u", roll);
    }

    /* Start/home counts used to live here; the corner trays now show that, so
     * the rows below the board (19..22) are free for the move list. */
    if (msg && msg[0])
        cputsxy(0, ROW_MSG, msg);
}

/* plat.h: RNG entropy from the POKEY hardware random register (the controller
 * seeds the core with this on the first game). */
uint16_t plat_seed(void)
{
    volatile unsigned char *RANDOM = (volatile unsigned char *)POKEY_RND_ADDR;
    return (uint16_t)(((uint16_t)*RANDOM << 8) ^ (uint16_t)*RANDOM);
}

/* Centre a string on a text row. */
static void center(unsigned char y, const char *s)
{
    unsigned char n = 0;
    while (s[n]) n++;
    cputsxy((unsigned char)((40 - n) / 2), y, s);
}

/* Move cursor: a gold pointer in the field column just left of the selected cell.
 * (The four PMG players are all used for tokens now, so the cursor is charset.) */
static void cursor_hide(void)
{
    if (cur_cx != 0xFF) { cputcxy(cur_cx, cur_cy, ' '); cur_cx = 0xFF; }
}
static void cursor_at(unsigned char char_x, unsigned char char_y)
{
    cursor_hide();
    cur_cx = (unsigned char)(char_x - 1);
    cur_cy = char_y;
    revers(1); cputcxy(cur_cx, cur_cy, CURSOR_CH); revers(0);
}

/* Point the cursor at a piece's board cell (or hide it if borne off). */
static void highlight_dest(unsigned char player, unsigned char dest)
{
    unsigned char rr, cc;
    if (pos_to_cell(player, dest, &rr, &cc))
        cursor_at(cellx(cc), celly(rr));
    else
        cursor_hide();
}

static void sfx_for_result(const ur_move_result *r)
{
    if (r->won)           sfx_win();
    else if (r->captured) sfx_capture();
    else if (r->scored)   sfx_score();
    else if (r->rosette)  sfx_rosette();
    else                  sfx_move();
}

/* Wait for the player to acknowledge: a fresh trigger press or any key. Paces on
 * the per-frame heartbeat so the music keeps playing (and rosettes glint) while
 * we wait. */
static void wait_action(void)
{
    while (atari_trig()) atari_wait_frames(1);   /* drain a held trigger */
    for (;;) {
        if (kbhit()) { cgetc(); return; }
        if (atari_trig()) return;
        atari_wait_frames(1);
    }
}

/* List the legal moves below the board (rows ROW_MOVE..+3), highlight the
 * selected one, and place the cursor box on its board cell. Scrolls to keep the
 * selection visible when there are more moves than rows. */
#define MOVE_ROWS 4
static void show_option(unsigned char player, const unsigned char *srcs,
                        unsigned char nsrc, unsigned char sel, unsigned char roll)
{
    unsigned char src, dest, cell, rr, cc, k, idx, top;

    src  = srcs[sel];
    dest = (unsigned char)(src + roll);
    cell = (src == UR_POS_START) ? dest : src;     /* on-board 1..14 */
    if (pos_to_cell(player, cell, &rr, &cc))
        cursor_at(cellx(cc), celly(rr));

    top = 0;                                        /* scroll window */
    if (nsrc > MOVE_ROWS && sel >= MOVE_ROWS)
        top = (unsigned char)(sel - (MOVE_ROWS - 1));

    for (k = 0; k < MOVE_ROWS; k++) {
        cclearxy(0, (unsigned char)(ROW_MOVE + k), 39);   /* clear the line */
        idx = (unsigned char)(top + k);
        if (idx >= nsrc)
            continue;
        gotoxy(0, (unsigned char)(ROW_MOVE + k));
        src  = srcs[idx];
        dest = (unsigned char)(src + roll);
        if (idx == sel) revers(1);
        if (src == UR_POS_START)
            cprintf("%u) enter -> %u", idx + 1, dest);
        else
            cprintf("%u) %u -> %u", idx + 1, src, dest);
        if (dest == UR_POS_HOME)                              cprintf("  home");
        else if (ur_is_rosette(dest))                         cprintf("  rosette");
        else if (ur_is_shared(dest) && opp_on(player, dest))  cprintf("  capture!");
        if (idx == sel) revers(0);
    }
}

/* Joystick/number-key move chooser, shared by hot-seat and online play. The
 * highlight cursor moves over the legal options; FIRE (or 1..N) selects.
 * Returns the chosen piece index, or -1 if there is no legal move. */
int8_t plat_choose_move(unsigned char player, unsigned char roll)
{
    unsigned char pieces[UR_PIECES], srcs[UR_PIECES];
    unsigned char count, nsrc, i, j, pos, sel, centered, s;
    bool seen;
    char key;

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

    plat_draw(roll, "Stick: choose   FIRE/1-N: move");
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
        atari_wait_frames(1);                 /* pace the poll + pump the music */
    }

    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (ur_g.piece[player][pieces[i]] == pos)
            return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

/* Token glide is retired with the PMG tokens (the horizontal charset board draws
 * tokens as charset cells, which can't slide a single disc). Moves settle instantly
 * via the next draw_all; kept as a no-op so the turn code is unchanged. A brief
 * frame wait keeps the music heartbeat ticking. */
static void anim_glide(unsigned char player, unsigned char from, unsigned char to,
                       unsigned char frames)
{
    (void)player; (void)from; (void)to;
    atari_wait_frames(frames);
}

/* Rattle the four tetrahedral dice through random faces, then settle on `roll`.
 * Panel row, so it's monochrome (matches the text HUD); POKEY RANDOM drives the
 * tumble. The next draw_all repaints the settled dice, so this is just the lead-up. */
static void dice_tumble(unsigned char roll)
{
    volatile unsigned char *RND = (volatile unsigned char *)POKEY_RND_ADDR;
    unsigned char t, i, r;

    gotoxy(0, ROW_ROLL);
    cputs("Roll:");
    for (t = 0; t < 8; t++) {
        r = *RND;
        gotoxy(5, ROW_ROLL);
        for (i = 0; i < 4; i++)
            cputc((r & (unsigned char)(1u << i)) ? DIE_MARKED : DIE_UNMARKED);
        atari_wait_frames(4);
    }
    gotoxy(5, ROW_ROLL);                       /* settle on the real roll */
    for (i = 0; i < 4; i++)
        cputc(i < roll ? DIE_MARKED : DIE_UNMARKED);
    cprintf(" %u", roll);
}

/* plat.h: the shared controller (src/common/ur_game.c) owns the turn loop and calls
 * these. RNG entropy is plat_seed() above. The token "glide" is a no-op pause now
 * (the horizontal charset board settles tokens instantly), but plat_animate keeps
 * the Atari's cursor-hide + destination highlight. */
void plat_wait(void) { wait_action(); }
void plat_roll(unsigned char roll) { sfx_roll(); dice_tumble(roll); }
void plat_sfx_result(const ur_move_result *res) { sfx_for_result(res); }
void plat_animate(unsigned char player, unsigned char from, unsigned char to)
{
    cursor_hide();
    anim_glide(player, from, to, ANIM_STEP);
    highlight_dest(player, to);
}

/* plat.h: choose the AI difficulty (keyboard 1/2/3). */
uint8_t plat_pick_level(void)
{
    char c;
    clrscr();
    cputsxy(0, 1, "Difficulty:");
    cputsxy(2, 4, "1) Easy");
    cputsxy(2, 5, "2) Normal");
    cputsxy(2, 6, "3) Hard");
    cputsxy(0, 8, "Press 1-3");
    do { c = cgetc(); } while (c < '1' || c > '3');
    return (uint8_t)(c - '1');
}

/* End-of-game screen: the finished board (the winner's seven pieces sit borne-off
 * in the corner tray) under a result banner, with a play-again prompt. */
static void show_result(const char *banner)
{
    plat_draw(NO_ROLL, "");
    cclearxy(0, ROW_TURN, 40);          /* wipe the "Turn:" / roll lines so the */
    cclearxy(0, ROW_ROLL, 40);          /* banner sits on a clean row           */
    revers(1); center(ROW_TURN, banner); revers(0);
    center(ROW_MSG, "Press FIRE or a key to play again");
    wait_action();
}

#ifndef UR_A5200   /* ===== FujiNet online + profile + keyboard entry: A8 only ===== */
/* ---- online mode (FujiNet N:TCP, server-authoritative) ------------------ */

/* Poll for the next STATE. 1 = got one, 0 = disconnected/error, -1 = the player
 * pressed a key (cancel back to the menu). */
static int8_t read_state(ur_snapshot *snap)
{
    uint8_t  buf[UR_STATE_MSG_LEN];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n;

    for (;;) {
        if (kbhit()) { cgetc(); return -1; }       /* let the player bail out */
        if (network_status(g_net_url, &bw, &conn, &err) != FN_ERR_OK)
            return 0;
        if (bw >= UR_STATE_MSG_LEN)
            break;
        if (conn == 0)
            return 0;
        atari_wait_frames(3);          /* ~20 polls/s: gentle on NetSIO */
    }
    n = network_read(g_net_url, buf, UR_STATE_MSG_LEN);
    if (n < (int16_t)UR_STATE_MSG_LEN)
        return 0;
    return ur_proto_decode_state(buf, (uint8_t)n, snap) ? 1 : 0;
}

static void show_seat(const ur_snapshot *snap)
{
    gotoxy(0, ROW_SEAT);
    cprintf("You: %s", snap->seat ? "Dark (green)" : "Light (white)");
}

/* Wait for the first STATE, counting down to the server's AI fallback. Returns
 * 1 = got a snapshot, 0 = disconnected, -1 = the player pressed a key (wants to
 * play the computer locally rather than keep waiting). */
static int8_t online_wait(ur_snapshot *snap)
{
    uint8_t  buf[UR_STATE_MSG_LEN];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n;
    unsigned char secs = 60, ticks = 0;

    gotoxy(10, 21); cprintf("computer joins in %2u", secs);
    for (;;) {
        if (kbhit()) { cgetc(); return -1; }
        if (network_status(g_net_url, &bw, &conn, &err) != FN_ERR_OK)
            return 0;
        if (bw >= UR_STATE_MSG_LEN) {
            n = network_read(g_net_url, buf, UR_STATE_MSG_LEN);
            return (n >= (int16_t)UR_STATE_MSG_LEN &&
                    ur_proto_decode_state(buf, (uint8_t)n, snap)) ? 1 : 0;
        }
        if (conn == 0)
            return 0;
        atari_wait_frames(6);                 /* ~0.1s per poll */
        if (++ticks >= 10) {                  /* ~1 second elapsed */
            ticks = 0;
            if (secs) secs--;
            gotoxy(10, 21); cprintf("computer joins in %2u", secs);
        }
    }
}

/* Returns true if the player chose to bail out and play the computer locally. */
static bool online_game(void)
{
    ur_snapshot snap;
    uint8_t cmd[2 + UR_NAME_LEN + 2];  /* JOIN is type+version+name */
    int8_t picked, rc;

    if (network_init() != FN_ERR_OK) {
        plat_draw(NO_ROLL, "network_init failed. FIRE/key."); wait_action(); return false;
    }
    if (network_open(g_net_url, OPEN_MODE_RW, 0) != FN_ERR_OK) {
        plat_draw(NO_ROLL, "connect failed. FIRE/key."); wait_action(); return false;
    }
    network_write(g_net_url, cmd, ur_proto_join(cmd, g_name));

    clrscr();
    cputsxy(0, 0, "The Royal Game of Ur");
    center(2, "Connecting to");
    center(3, g_host);
    center(20, "Waiting for an opponent...");
    center(23, "or press a key to play the computer");

    rc = online_wait(&snap);
    if (rc == -1) { network_close(g_net_url); return true; }   /* play computer locally */
    if (rc == 0) {
        plat_draw(NO_ROLL, "Disconnected. FIRE/key."); wait_action();
        network_close(g_net_url); return false;
    }

    for (;;) {
        ur_g = snap.state;
        if (snap.flags & UR_FLAG_CAPTURED)      sfx_capture();
        else if (snap.flags & UR_FLAG_SCORED)   sfx_score();
        else if (snap.flags & UR_FLAG_ROSETTE)  sfx_rosette();

        if (snap.phase == UR_PHASE_OVER) {
            show_result(snap.winner == (int8_t)snap.seat ? " YOU WIN! " : " YOU LOSE ");
            break;
        }
        if (snap.state.turn != snap.seat) {
            plat_draw(snap.phase == UR_PHASE_MOVE ? snap.roll : NO_ROLL, "Opponent's turn...");
            show_seat(&snap);
        } else if (snap.phase == UR_PHASE_ROLL) {
            plat_draw(NO_ROLL, "Your turn - FIRE/key to roll");
            show_seat(&snap);
            wait_action();
            sfx_roll();
            network_write(g_net_url, cmd, ur_proto_roll(cmd));
        } else {
            picked = plat_choose_move(snap.seat, snap.roll);
            if (picked >= 0)
                network_write(g_net_url, cmd, ur_proto_move(cmd, (unsigned char)picked));
        }
        rc = read_state(&snap);
        if (rc == -1) break;                  /* player quit to the menu */
        if (rc == 0) { plat_draw(NO_ROLL, "Disconnected. FIRE/key."); wait_action(); break; }
    }
    network_close(g_net_url);
    return false;
}

/* ---- persistent player profile (FujiNet AppKey) ------------------------- */

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
    unsigned char i, n;

    fuji_set_appkey_details(UR_CREATOR_ID, UR_APP_ID, DEFAULT);
    if (!fuji_read_appkey(UR_KEY_PROFILE, &cnt, buf) || cnt < UR_NAME_LEN + 2)
        return false;
    /* layout: name[UR_NAME_LEN] (NUL-padded), wins (2), hostlen (1), host[] */
    n = 0;
    for (i = 0; i < UR_NAME_LEN; i++) {
        char ch = (char)buf[i];
        if (ch == 0) break;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ')
            g_name[n++] = ch;
    }
    while (n > 0 && g_name[n - 1] == ' ') n--;   /* trim trailing spaces */
    g_name[n] = 0;
    g_wins = (uint16_t)(buf[UR_NAME_LEN] | ((uint16_t)buf[UR_NAME_LEN + 1] << 8));
    if (cnt >= UR_NAME_LEN + 3) {                /* optional host string follows */
        unsigned char hl = buf[UR_NAME_LEN + 2];
        if (hl > 0 && hl <= 32 && (uint16_t)(UR_NAME_LEN + 3 + hl) <= cnt) {
            for (i = 0; i < hl; i++)
                g_host[i] = (char)buf[UR_NAME_LEN + 3 + i];
            g_host[hl] = 0;
        }
    }
    return true;
}

/* Persist name + win count. Silently no-ops if no FujiNet is attached. */
static void profile_save(void)
{
    uint8_t buf[UR_NAME_LEN + 3 + 32];
    unsigned char hl = 0, nl = 0, i;
    while (g_name[nl] && nl < UR_NAME_LEN) nl++;
    for (i = 0; i < UR_NAME_LEN; i++)        /* name, NUL-padded */
        buf[i] = (i < nl) ? (uint8_t)g_name[i] : 0;
    buf[UR_NAME_LEN]     = (uint8_t)(g_wins & 0xFF);
    buf[UR_NAME_LEN + 1] = (uint8_t)(g_wins >> 8);
    while (g_host[hl] && hl < 32)            /* append the configured host */
        hl++;
    buf[UR_NAME_LEN + 2] = hl;
    for (i = 0; i < hl; i++)
        buf[UR_NAME_LEN + 3 + i] = (uint8_t)g_host[i];
    fuji_set_appkey_details(UR_CREATOR_ID, UR_APP_ID, DEFAULT);
    fuji_write_appkey(UR_KEY_PROFILE, (uint16_t)(UR_NAME_LEN + 3 + hl), buf);
}

/* If the FujiNet lobby launched us, it left the chosen server's URL in its
 * handoff AppKey. Parse the host out of it (e.g. "tcp://thefnords.com:1234/") into
 * g_host so we auto-connect. Returns true if a host was found. */
static bool lobby_host_from_appkey(void)
{
    uint8_t  buf[MAX_APPKEY_LEN + 2];
    uint16_t cnt = 0;
    unsigned char i, j, start = 0;
    bool found = false;

    fuji_set_appkey_details(UR_LOBBY_CREATOR, UR_LOBBY_APP, DEFAULT);
    if (!fuji_read_appkey(UR_LOBBY_APPKEY, &cnt, buf) || cnt == 0)
        return false;
    for (i = 0; (uint16_t)(i + 2) < cnt; i++)        /* find "://" */
        if (buf[i] == ':' && buf[i + 1] == '/' && buf[i + 2] == '/') {
            start = (unsigned char)(i + 3);
            found = true;
            break;
        }
    if (!found)
        return false;
    j = 0;                                            /* host = up to ':' or '/' */
    for (i = start; i < cnt && j < 32; i++) {
        if (buf[i] == ':' || buf[i] == '/')
            break;
        g_host[j++] = (char)buf[i];
    }
    if (j == 0)
        return false;
    g_host[j] = 0;
    return true;
}

/* Keyboard entry of the player name (up to UR_NAME_LEN chars: A-Z/0-9/space),
 * saved to the profile appkey. Letters are upper-cased. */
static void enter_name(void)
{
    char tmp[UR_NAME_LEN + 1];
    unsigned char len = 0, k;
    char c;

    while (g_name[len] && len < UR_NAME_LEN) { tmp[len] = g_name[len]; len++; }
    tmp[len] = 0;

    atari_text_mode();
    clrscr();
    revers(1); cputsxy(12, 2, " SET YOUR NAME "); revers(0);
    cputsxy(2, 5,  "Type a name (letters/digits),");
    cputsxy(2, 6,  "then press RETURN.  DELETE = back.");
    cputsxy(2, 8,  "Up to 8 chars; shown on the");
    cputsxy(2, 9,  "leaderboard. (needs FujiNet to save)");
    cputsxy(2, 12, "Name:");

    for (;;) {
        gotoxy(2, 14);
        cputs(tmp);
        cputc('_');
        for (k = len; k < UR_NAME_LEN + 2; k++)        /* clear leftovers */
            cputc(' ');
        c = cgetc();
        if (c == 0x9B || c == '\n' || c == '\r')
            break;
        if ((c == 0x7E || c == 0x08) && len > 0) {
            len--; tmp[len] = 0;
        } else if (len < UR_NAME_LEN) {
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);   /* upper-case */
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') {
                tmp[len++] = c; tmp[len] = 0;
            }
        }
    }

    while (len > 0 && tmp[len - 1] == ' ') tmp[--len] = 0;   /* trim trailing */
    for (k = 0; k <= len; k++)
        g_name[k] = tmp[k];
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
#endif /* !UR_A5200 (online + profile + keyboard entry) */

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

/* Idle one display frame on the title. atari_wait_frames() is the per-frame
 * heartbeat that pumps the Hurrian Hymn, so the tune plays (and loops) under the
 * menu instead of blocking before it. */
static void title_idle(void)
{
    atari_wait_frames(1);
}

/* Sumerian title screen + mode select. Title/menu text live on the mode-2 rows;
 * the ziggurat scene is drawn in the mode-4 colour band (rows 4..18). Returns the
 * chosen mode key ('1'..'3'). The ']' glyph is a solid block, '\' a cuneiform
 * wedge, '*'+'&' the rosette (drawn inverse = gold). */
static char title_screen(void)
{
    char key;

    clrscr();
    atari_pmg_tokens_clear();                /* clear any tokens left from a game */
#ifndef UR_A5200
    gotoxy(0, 0);
    cprintf("Server: %s", g_host);          /* where Online connects (menu 7) */
#endif
    revers(1);
    cputsxy(9, 1, " THE ROYAL GAME OF UR ");
    revers(0);
    cputsxy(5, 2, "Ur - Mesopotamia - c.2600 BCE");
    if (g_name[0]) {
        gotoxy(9, 3);
        cprintf("Player %s   Wins %u", g_name, g_wins);
    }

#ifdef UR_A5200
    /* 5200: charset ziggurat in the mode-4 board band (no GR.8); keypad menu. */
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

    /* no keyboard — the controller's numeric keypad stands in (digits read through
     * cgetc), and the stick + FIRE drive a highlight selector. */
    cputsxy(3, 19, "1) Two players");
    cputsxy(3, 20, "2) One player vs computer");
    cputsxy(3, 21, "4) How to play");
    cputsxy(3, 23, "Keypad 1/2/4  -or-  stick/FIRE");
    {
        unsigned char sel = 1;                /* 0 = two players, 1 = vs computer */
        unsigned char s;
        while (atari_trig()) { }              /* don't inherit a held trigger */
        for (;;) {
            cputcxy(1, 19, sel == 0 ? '>' : ' ');
            cputcxy(1, 20, sel == 1 ? '>' : ' ');
            if (kbhit()) {                    /* keypad digit picks a mode directly */
                key = cgetc();
                if (key == '1' || key == '2' || key == '4') break;
            }
            s = atari_stick();
            if (!(s & 0x01)) sel = 0;         /* up   -> two players */
            if (!(s & 0x02)) sel = 1;         /* down -> vs computer */
            if (atari_trig()) { key = (char)(sel == 0 ? '1' : '2'); break; }
            title_idle();                     /* keep the music going while waiting */
        }
        while (atari_trig()) { }
        while (kbhit()) { }                   /* drain the held key before play */
    }
    return key;
#else
    /* A8: charset ziggurat in the mode-4 board band + a lapis sky DLI; keyboard menu. */
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

    atari_title_sky_on();
    for (;;) {
        if (kbhit()) { key = cgetc(); if (key >= '1' && key <= '7') break; }
        title_idle();                         /* music heartbeat while waiting */
    }
    atari_title_sky_off();
    atari_setup_colors();
    return key;
#endif  /* 5200 charset/keypad title vs A8 charset title */
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
    cputsxy(0, 21, "Rules deciphered by Dr Irving Finkel,");
    cputsxy(0, 22, "British Museum - with thanks.");
    cputsxy(0, 23, "     FIRE or a key: see the path...");
    wait_action();

    show_path_demo();           /* page 3: animated white/green path */
}

#ifndef UR_A5200   /* leaderboard is online (N:HTTP) — A8 only */
/* Fetch the compact /top leaderboard over N:HTTP and show it. The body is
 * 1 count byte then up to 10 records of name[3] + wins (uint16 LE). */
static void show_leaderboard(void)
{
    uint8_t  buf[128];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n = 0;
    unsigned char count, i, j, base;
    char name[UR_NAME_LEN + 1];
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
        cputsxy(3, 2, "#   NAME       WINS");
        for (i = 0; i < count; i++) {
            base = (unsigned char)(1 + i * (UR_NAME_LEN + 2));
            if ((int16_t)(base + UR_NAME_LEN + 2) > n)
                break;
            for (j = 0; j < UR_NAME_LEN; j++)
                name[j] = (char)buf[base + j];
            name[UR_NAME_LEN] = 0;
            wins = (uint16_t)(buf[base + UR_NAME_LEN] | ((uint16_t)buf[base + UR_NAME_LEN + 1] << 8));
            gotoxy(3, (unsigned char)(4 + i));
            cprintf("%-2u  %-8s   %u", i + 1, name, wins);
        }
    }
    cputsxy(2, 23, "FIRE or a key to return");
    wait_action();
    atari_mode4_board();
}
#endif /* !UR_A5200 (leaderboard) */

/* Run a local game to completion and show the result. ai1 = player 1 (Dark) is
 * the computer; otherwise hot-seat. */
/* Run a local game (via the shared controller) and show the result. */
static void run_and_show(bool ai1)
{
    unsigned char winner;

    atari_board_dli_on();               /* living-lapis board sheen during play */
    winner = ur_run_game(ai1 ? 1 : 0);  /* shared controller turn loop */
    if (ai1) {
        if (winner == 0) {              /* you beat the computer: record it */
            g_wins++;
#ifndef UR_A5200
            profile_save();             /* persisted to the FujiNet appkey (A8) */
#endif
        }
        show_result(winner == 0 ? " YOU WIN! " : " YOU LOSE ");
    } else {
        show_result(winner == 0 ? " LIGHT (WHITE) WINS! " : " DARK (GREEN) WINS! ");
    }
    atari_board_dli_off();              /* flat field back for the menu/title */
    atari_pmg_tokens_clear();           /* no stray token discs over the menu */
}

int main(void)
{
    char key;


#ifdef UR_A5200
    atari_screen_init();        /* 5200: build our own 40-col display (no OS) */
#endif
    atari_setup_colors();
    atari_setup_charset();
    atari_mode4_board();
    atari_pmg_init();
    atari_quiet_sio();          /* no OS SIO "drive" drone during FujiNet polling */
    music_start();              /* the Hurrian Hymn plays throughout (OPTION toggles it) */

#ifndef UR_A5200
    profile_load();             /* name/wins/host from the FujiNet appkey, if any */
    lobby_host_from_appkey();   /* launched from the lobby? use its server host */
    build_urls();               /* N: URLs from the resolved host */
#endif

    for (;;) {                  /* play again forever; never falls out to Memo Pad */
        do {
            key = title_screen();
            if (key == '4')      show_instructions();
#ifndef UR_A5200
            else if (key == '5') enter_name();
            else if (key == '6') show_leaderboard();
            else if (key == '7') enter_host();
#endif
        } while (key == '4'
#ifndef UR_A5200
                 || key == '5' || key == '6' || key == '7'
#endif
                );

#ifndef UR_A5200
        if (key == '3') {
            if (online_game())  /* bailed out of waiting -> play the computer */
                run_and_show(true);
            continue;
        }
#endif
        run_and_show(key == '2'); /* 1 = hot-seat, 2 = vs computer */
    }
    return 0;                   /* not reached: the play-again loop never exits */
}
