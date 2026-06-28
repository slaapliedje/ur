/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * NES / Famicom (Ricoh 2A03 = 6502 / cc65) — Royal Game of Ur.
 *
 * The NES is a 6502 machine, so it reuses the portable core (src/common/ur)
 * unchanged; this is the thin platform layer. There is NO FujiNet for the NES,
 * so this is a LOCAL build (hot-seat + vs-AI), like the ColecoVision cartridge.
 *
 * BRING-UP renderer: the cc65 `nes` conio (a built-in font flushed to the PPU in
 * vblank) draws the board + HUD as coloured text, and the NES controller drives
 * input (read straight off $4016). The board is the shared horizontal 3x8
 * Standard-of-Ur layout. A later pass replaces the text cells with custom CHR
 * tiles (carved rosette/eye/quincunx + round tokens) to match the other ports.
 *
 * Build: cc65 -t nes (see makefiles/nes.mk); run in MAME (nes) / Mesen / FCEUX.
 */
#include <stdint.h>
#include <stdbool.h>
#include <conio.h>
#include <nes.h>

#include "ur.h"

#define NO_ROLL 0xFF

/* ---- NES controller ($4016), read directly ---------------------------------
 * After a 1->0 strobe, eight reads return A,B,Select,Start,Up,Down,Left,Right
 * (one bit each). We shift them MSB-first into one byte. */
#define JOYPAD1 (*(volatile unsigned char *)0x4016)
#define PAD_A     0x80
#define PAD_B     0x40
#define PAD_SELECT 0x20
#define PAD_START 0x10
#define PAD_UP    0x08
#define PAD_DOWN  0x04
#define PAD_LEFT  0x02
#define PAD_RIGHT 0x01

static uint16_t g_seed = 0xACE1u;

static unsigned char read_pad_raw(void)
{
    unsigned char i, v = 0;
    JOYPAD1 = 1;                 /* strobe */
    JOYPAD1 = 0;
    for (i = 0; i < 8; i++)
        v = (unsigned char)((v << 1) | (JOYPAD1 & 1));
    return v;
}

/* DMA-safe read: the cc65 conio NMI (PPU flush + OAM DMA) can fire mid-read and
 * duplicate/drop a controller bit, returning phantom presses. Re-read until two
 * consecutive reads agree — standard NES practice. */
static unsigned char read_pad(void)
{
    unsigned char a, b;
    a = read_pad_raw();
    do { b = a; a = read_pad_raw(); } while (a != b);
    return a;
}

/* Block until a fresh button press (full release first), returning its bits. */
static unsigned char wait_pad(void)
{
    unsigned char p;
    while (read_pad()) waitvsync();           /* wait for release  */
    for (;;) {
        p = read_pad();
        if (p) { waitvsync(); return p; }     /* one-frame debounce */
        waitvsync();
    }
}

/* ---- board geometry (text cells) ------------------------------------------ */
/* Path position (1..14) -> board cell (row 0=Light,1=shared,2=Dark; col 0..7). */
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
/* Rosettes: (0,0)(0,6)(2,0)(2,6) on the private rows, (1,3) shared centre. */
static bool is_rosette_cell(unsigned char row, unsigned char col)
{
    if (row == 1) return col == 3;
    return col == 0 || col == 6;
}
/* Outer rows omit cols 4-5 (the cut-away bridge); the middle row has all 8. */
static bool cell_exists(unsigned char row, unsigned char col)
{
    if (row == 1) return true;
    return col <= 3 || col >= 6;
}

#define BOARD_X 4               /* left margin of the board (chars)  */
#define BOARD_Y 6               /* top of the board                  */
#define HUD_Y   13
static unsigned char cellx(unsigned char col) { return (unsigned char)(BOARD_X + col * 3); }
static unsigned char celly(unsigned char row) { return (unsigned char)(BOARD_Y + row * 2); }

/* Tiny string/number builders (cheaper than cprintf). */
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *put_u(char *p, unsigned char v)
{
    if (v >= 100) { *p++ = (char)('0' + v / 100); v = (unsigned char)(v % 100);
                    *p++ = (char)('0' + v / 10);  *p++ = (char)('0' + v % 10); }
    else if (v >= 10) { *p++ = (char)('0' + v / 10); *p++ = (char)('0' + v % 10); }
    else *p++ = (char)('0' + v);
    return p;
}

static ur_state game;

/* Draw a 2-char board cell at (col,row) in a colour. */
static void put_cell(unsigned char col, unsigned char row,
                     char a, char b, unsigned char color)
{
    textcolor(color);
    gotoxy(cellx(col), celly(row));
    cputc(a);
    cputc(b);
}

static void clrrow(unsigned char y)
{
    cclearxy(0, y, 30);
}

static void status(const char *msg)
{
    clrrow(23);
    textcolor(COLOR_WHITE);
    cputsxy(0, 23, msg);
}

static void draw_board(unsigned char roll, const char *msg)
{
    unsigned char row, col, pl, i, pos, rr, cc;
    char grid[3][8];          /* 0 cut, 'L'/'D' piece, '*' rosette, 'E' eye, '.' dots */
    char buf[33], *p;

    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++)
            grid[row][col] = cell_exists(row, col)
                           ? (is_rosette_cell(row, col) ? '*'
                              : (row == 1 ? 'E' : '.'))
                           : 0;
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = game.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                grid[rr][cc] = pl ? 'D' : 'L';
        }

    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++) {
            char g = grid[row][col];
            if (g == 0)        put_cell(col, row, ' ', ' ', COLOR_BLACK);
            else if (g == '*') put_cell(col, row, '*', '*', COLOR_YELLOW);
            else if (g == 'E') put_cell(col, row, '(', ')', COLOR_YELLOW);
            else if (g == '.') put_cell(col, row, '.', '.', COLOR_GRAY3);
            else if (g == 'L') put_cell(col, row, 'O', 'O', COLOR_WHITE);
            else               put_cell(col, row, 'X', 'X', COLOR_ORANGE);
        }

    textcolor(COLOR_WHITE);
    clrrow(HUD_Y);
    cputsxy(0, HUD_Y, game.turn ? "TURN: DARK (X)" : "TURN: LIGHT (O)");
    p = put_s(buf, "LIGHT:");
    p = put_u(p, (unsigned char)ur_score(&game, 0));
    p = put_s(p, "  DARK:");
    p = put_u(p, (unsigned char)ur_score(&game, 1));
    if (roll != NO_ROLL) { p = put_s(p, "   ROLL:"); p = put_u(p, roll); }
    *p = 0;
    clrrow((unsigned char)(HUD_Y + 1));
    cputsxy(0, (unsigned char)(HUD_Y + 1), buf);

    if (msg) status(msg);
}

/* List legal moves (deduped by source); pick one with Up/Down + A. */
static int8_t choose_move(unsigned char player, unsigned char roll)
{
    unsigned char pieces[UR_PIECES], srcs[UR_PIECES];
    unsigned char count, nsrc, i, j, pos, dest, sel;
    bool seen;
    unsigned char pad;

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

    for (i = 0; i < nsrc; i++) {                  /* render the move list once */
        char buf[20], *p = buf;
        pos = srcs[i];
        dest = (unsigned char)(pos + roll);
        p = put_s(p, "  ");
        if (pos == UR_POS_START) { *p++ = 'E'; }
        else                     { p = put_u(p, pos); }
        *p++ = '>';
        p = put_u(p, dest);
        if (dest == UR_POS_HOME)      *p++ = 'H';
        else if (ur_is_rosette(dest)) *p++ = '*';
        *p++ = ' '; *p++ = ' ';
        *p = 0;
        clrrow((unsigned char)(HUD_Y + 3 + i));
        textcolor(COLOR_WHITE);
        cputsxy(0, (unsigned char)(HUD_Y + 3 + i), buf);
    }
    status("UP/DOWN + A TO MOVE");

    sel = 0;
    for (;;) {
        for (i = 0; i < nsrc; i++) {              /* draw the selector cursor */
            gotoxy(0, (unsigned char)(HUD_Y + 3 + i));
            textcolor(COLOR_YELLOW);
            cputc(i == sel ? '>' : ' ');
        }
        pad = wait_pad();
        if ((pad & PAD_UP) && sel > 0) sel--;
        else if ((pad & PAD_DOWN) && sel + 1 < nsrc) sel++;
        else if (pad & (PAD_A | PAD_START)) break;
    }

    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (game.piece[player][pieces[i]] == pos)
            return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

static unsigned char roll_dice(void)
{
    static bool seeded = false;
    if (!seeded) { ur_rng_seed((uint16_t)(g_seed | 1u)); seeded = true; }
    return ur_dice_roll();
}

static bool human_turn(unsigned char player)
{
    unsigned char roll;
    int8_t picked;
    ur_move_result res;

    draw_board(NO_ROLL, "PRESS A TO ROLL");
    wait_pad();
    roll = roll_dice();

    picked = choose_move(player, roll);
    if (picked < 0) {
        draw_board(roll, "NO LEGAL MOVE - PRESS A");
        wait_pad();
        ur_advance_turn(&game, (const ur_move_result *)0);
        return false;
    }
    ur_apply_move(&game, player, (unsigned char)picked, roll, &res);
    if (res.won)
        return true;
    if (res.captured || res.rosette) {
        draw_board(roll, res.captured ? "CAPTURE! PRESS A" : "ROSETTE - AGAIN!");
        wait_pad();
    }
    ur_advance_turn(&game, &res);
    return false;
}

static bool computer_turn(unsigned char player)
{
    unsigned char pieces[UR_PIECES], roll;
    int8_t pick;
    ur_move_result res;

    draw_board(NO_ROLL, "COMPUTER'S TURN - PRESS A");
    wait_pad();
    roll = roll_dice();

    if (ur_legal_moves(&game, player, roll, pieces) == 0) {
        draw_board(roll, "COMPUTER: NO MOVE - A");
        wait_pad();
        ur_advance_turn(&game, (const ur_move_result *)0);
        return false;
    }
    pick = ur_ai_pick(&game, player, roll);
    ur_apply_move(&game, player, (unsigned char)pick, roll, &res);
    draw_board(roll, "COMPUTER MOVED - PRESS A");
    wait_pad();
    if (res.won)
        return true;
    ur_advance_turn(&game, &res);
    return false;
}

static void play_local(bool ai1)
{
    unsigned char player, y;
    bool over = false;

    ur_init(&game);
    bgcolor(COLOR_BLUE);            /* lapis field */
    clrscr();
    textcolor(COLOR_YELLOW);
    cputsxy(0, 0, "THE ROYAL GAME OF UR");

    for (;;) {
        player = game.turn;
        over = (player == 1 && ai1) ? computer_turn(player) : human_turn(player);
        if (over)
            break;
    }

    for (y = HUD_Y; y < 24; y++) clrrow(y);
    textcolor(COLOR_YELLOW);
    if (ai1)
        cputsxy(0, HUD_Y, player == 0 ? "YOU WIN!" : "YOU LOSE.");
    else
        cputsxy(0, HUD_Y, player == 0 ? "LIGHT (O) WINS!" : "DARK (X) WINS!");
    textcolor(COLOR_WHITE);
    cputsxy(0, (unsigned char)(HUD_Y + 2), "PRESS A");
    wait_pad();
}

int main(void)
{
    unsigned char pad;

    bgcolor(COLOR_BLACK);

    for (;;) {
        clrscr();
        textcolor(COLOR_YELLOW);
        cputsxy(2, 2, "THE ROYAL GAME OF UR");
        textcolor(COLOR_WHITE);
        cputsxy(2, 3, "NES / FAMICOM");
        cputsxy(2, 6, "A) TWO PLAYERS");
        cputsxy(2, 7, "B) ONE PLAYER VS COMPUTER");
        cputsxy(2, 10, "D-PAD + A SELECT MOVES");
        textcolor(COLOR_YELLOW);
        cputsxy(2, 13, "PRESS A OR B");
        textcolor(COLOR_GRAY2);
        cputsxy(2, 24, "RULES: DR IRVING FINKEL");

        /* Seed the RNG from how long the player takes to choose. */
        while (!read_pad()) { g_seed++; waitvsync(); }
        pad = wait_pad();

        if (pad & PAD_A)      play_local(false);   /* hot-seat       */
        else if (pad & PAD_B) play_local(true);    /* vs computer    */
    }
    return 0;
}
