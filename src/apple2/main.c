/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Apple II (6502 / cc65) — Royal Game of Ur.
 *
 * Reuses the portable core (src/common/ur) unchanged; this is the thin platform
 * layer.  The board is drawn in LO-RES colour (GR): the Apple II's hi-res mode has
 * only 6 position-dependent artifact colours (no brown/gold), but lo-res gives 16
 * SOLID colours, so the board uses the same lapis/gold/cream/brown palette as the
 * C64/Adam.  Traditional horizontal 3x8 layout, two-tone tokens (a white body with
 * a brown pip = Light, the inverse = Dark), gold rosette tiles.  MIXED mode keeps
 * 4 text lines at the bottom for the turn / roll / move panel.  Title menu + the
 * win screen use plain text mode.  Hot-seat + vs-AI; 1-bit speaker (sound.c).
 *
 * Still to come: FujiNet online over the SmartPort bus (apple2 fujinet-lib, same
 * wire protocol).  Build: cc65 -t apple2 -C apple2-system.cfg; run under ProDOS in
 * MAME's enhanced //e (apple2ee) / AppleWin — see src/apple2/CLAUDE.md.
 */
#include <stdint.h>
#include <stdbool.h>
#include <conio.h>

#include "ur.h"
#include "ur_game.h"        /* shared local-game controller + plat.h interface */
#include "gr.h"
#include "sound.h"
#include "music.h"          /* the Hurrian Hymn title theme (shared melody)       */
#ifdef UR_DHGR
#include "dhgr.h"
#endif
#ifdef UR_ONLINE
#include "proto.h"          /* the cross-platform wire protocol (same as Atari)  */
#include "fujinet-network.h"
#include "fujinet-fuji.h"   /* fuji_*_appkey: persistent profile on the FujiNet SD */
#endif

#define NO_ROLL   0xFF
#define PANEL_TOP 20            /* mixed-mode text lines occupy rows 20-23 */
#define CELL_NONE 0xFF          /* "never drawn" sentinel for the dirty-cell cache */

#ifdef UR_DHGR
/* Double-hi-res: the board uses dhgr.{c,h}; the panel uses page-2 text. Lapis
 * field, gold (orange) rosettes, white/olive two-tone tokens (nibbles from the
 * calibration). Cells get a black border to hide DHGR edge fringing. */
#define BOARD_ON()        dhgr_on()
#define BOARD_OFF()       dhgr_off()
#define board_show()      ((void)0)
#define panel_clr(r)      dhgr_clrrow(r)
#define panel_text(c,r,s) dhgr_text((c),(r),(s))
#else
/* Lo-res (default): board via gr.{c,h}, panel via the 4 mixed-mode text lines. */
#define BOARD_ON()        gr_on()
#define BOARD_OFF()       gr_off()
#define board_show()      gr_show()
#define panel_clr(r)      gr_clrrow(r)
#define panel_text(c,r,s) gr_text((c),(r),(s))

#define COL_FIELD  GR_DBLUE
#define COL_LANE   GR_GREY
#define COL_ROSE   GR_ORANGE
#define COL_LIGHT  GR_WHITE
#define COL_LPIP   GR_BROWN
#define COL_DARK   GR_BROWN
#define COL_DPIP   GR_WHITE
#define CELL_W 4
#define CELL_H 10
static unsigned char cellx(unsigned char col) { return (unsigned char)(col * 5); }
static unsigned char celly(unsigned char row) { return (unsigned char)(1 + row * 14); }
#endif

static uint16_t  g_seed = 0xACE1u;   /* RNG entropy (accumulated in the menu) */

#ifdef UR_ONLINE
#define UR_DEFAULT_HOST "localhost"   /* server host; runtime-configurable (menu 5) */
/* FujiNet AppKey profile (creator 0x5552='UR'), shared with the Atari/Adam/C64. */
#define UR_CREATOR_ID  0x5552u
#define UR_APP_ID      0x01
#define UR_KEY_PROFILE 0x00
#define UR_LOBBY_CREATOR 0x0001u      /* lobby handoff appkey (server URL) */
#define UR_LOBBY_APP     0x01
#define UR_LOBBY_APPKEY  0x06
static char     g_name[UR_NAME_LEN + 1] = "";
static uint16_t g_wins  = 0;
static char     g_host[33] = UR_DEFAULT_HOST;
static char     g_net_url[64];        /* N:TCP://<host>:1234/  */
static char     g_top_url[64];        /* N:HTTP://<host>:8080/top */
#endif

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

/* Cell/token drawing in (col,row) terms — palette + geometry differ per renderer,
 * the board logic is shared. A two-tone token = body fill + a centred pip. */
#ifdef UR_DHGR
#define C_FIELD 1                /* lapis field nibble */
#define C_ROSE 12                /* gold/orange */
#define C_LANE 5                 /* grey stone lane */
#define C_GOLD 12                /* eye ring / petals */
#define C_PEARL 15               /* white inlay */
#define C_LIGHT 15
#define C_LPIP 4
#define C_DARK 4
#define C_DPIP 15
static const unsigned char dh_ry[3] = { 4, 56, 108 };   /* row tops (mixed, y<160) */
/* Each square is an 8-group (~56px) x 44-scanline carved cell, built from filled
 * rectangles (DROW). The inlaid mosaic mirrors the SMS showpiece: gold flower
 * rosettes, a gold bullseye eye down the shared lane, a white quincunx on the
 * private lanes, and round two-tone disc tokens. */
#define DROW(l, r, ya, yb, c) dhgr_fill((unsigned char)(g0 + (l)), (unsigned char)(g0 + (r)), \
                                        (unsigned char)(y0 + (ya)), (unsigned char)(y0 + (yb)), (c))
/* The fixed-colour motifs (rosette/eye/dots) are TABLE-driven — a flat list of
 * {l,r,ya,yb,colour} rectangles run through one small loop. This is far smaller
 * than three unrolled functions (the DHGR layout pins CODE at $6000, so code that
 * grows squeezes BSS), at the cost of a few bytes of RODATA. */
static const unsigned char m_rose[] = {       /* gold flower bloom on lapis */
    0,7,  0,43, C_FIELD,  3,4,  2, 7, C_GOLD,  2,5,  8,13, C_GOLD,
    0,7, 14,29, C_GOLD,   2,5, 30,35, C_GOLD,  3,4, 36,41, C_GOLD,
    3,4, 18,25, C_PEARL };
static const unsigned char m_eye[] = {        /* gold bullseye on a grey lane tile */
    0,7,  0,43, 0,        1,6,  3,40, C_LANE,  2,5,  8,12, C_GOLD,
    1,2, 13,30, C_GOLD,   5,6, 13,30, C_GOLD,  2,5, 31,35, C_GOLD,
    3,4, 18,25, C_PEARL };
static const unsigned char m_dots[] = {       /* white quincunx on a grey lane tile */
    0,7,  0,43, 0,        1,6,  3,40, C_LANE,  1,2,  8,13, C_PEARL,
    5,6,  8,13, C_PEARL,  3,4, 19,24, C_PEARL, 1,2, 30,35, C_PEARL,
    5,6, 30,35, C_PEARL };
static void dh_motif(unsigned char col, unsigned char row,
                     const unsigned char *r, unsigned char n)
{
    unsigned char g0 = (unsigned char)(col * 10), y0 = dh_ry[row], i;
    for (i = 0; i < n; i++, r += 5)
        DROW(r[0], r[1], r[2], r[3], r[4]);
}
#define draw_rosette(c, r) dh_motif((c), (r), m_rose, 7)
#define draw_eye(c, r)     dh_motif((c), (r), m_eye,  7)
#define draw_dots(c, r)    dh_motif((c), (r), m_dots, 7)
/* A ROUND token: a black cell, then a tapered disc of the body colour (narrow at
 * top/bottom, full 6 groups in the middle), then the centre pip. */
static void draw_token(unsigned char col, unsigned char row,
                       unsigned char body, unsigned char pip)
{
    unsigned char g0 = (unsigned char)(col * 10), y0 = dh_ry[row];
    DROW(0, 7,  0, 43, 0);        /* black cell footprint */
    DROW(3, 4,  3,  6, body);     /* disc: top cap (2 groups)   */
    DROW(2, 5,  7, 11, body);     /*       shoulders (4 groups) */
    DROW(1, 6, 12, 31, body);     /*       middle (6 groups)    */
    DROW(2, 5, 32, 36, body);     /*       shoulders            */
    DROW(3, 4, 37, 40, body);     /*       bottom cap           */
    DROW(3, 4, 18, 27, pip);      /* centre pip */
}
#undef DROW
#else
#define C_LIGHT COL_LIGHT
#define C_LPIP COL_LPIP
#define C_DARK COL_DARK
#define C_DPIP COL_DPIP
/* Lo-res cells are 4 blocks wide x 10 tall (~28x40 px). The same inlaid-mosaic
 * motifs as the SMS/DHGR, drawn chunky: a filled-rect run within the cell. */
#define LBAR(a, b, ya, yb, c) gr_bar((unsigned char)(x0 + (a)), (unsigned char)(y0 + (ya)), \
                                     (unsigned char)(x0 + (b)), (unsigned char)(y0 + (yb)), (c))
/* Gold flower rosette: a gold tile with a brighter cross of petals + white pearl. */
static void draw_rosette(unsigned char col, unsigned char row)
{
    unsigned char x0 = cellx(col), y0 = celly(row);
    LBAR(0, 3, 0, 9, COL_ROSE);   /* gold tile        */
    LBAR(1, 2, 0, 9, GR_YELLOW);  /* N-S petals       */
    LBAR(0, 3, 4, 5, GR_YELLOW);  /* E-W petals       */
    LBAR(1, 2, 4, 5, GR_WHITE);   /* pearl centre     */
}
/* Gold bullseye eye on a grey lane tile (shared capture lane). */
static void draw_eye(unsigned char col, unsigned char row)
{
    unsigned char x0 = cellx(col), y0 = celly(row);
    LBAR(0, 3, 0, 9, COL_LANE);   /* grey tile        */
    LBAR(1, 2, 1, 1, COL_ROSE);   /* ring top         */
    LBAR(0, 0, 2, 7, COL_ROSE);   /* ring left        */
    LBAR(3, 3, 2, 7, COL_ROSE);   /* ring right       */
    LBAR(1, 2, 8, 8, COL_ROSE);   /* ring bottom      */
    LBAR(1, 2, 4, 5, GR_WHITE);   /* pearl centre     */
}
/* White quincunx on a grey lane tile (private lanes). */
static void draw_dots(unsigned char col, unsigned char row)
{
    unsigned char x0 = cellx(col), y0 = celly(row);
    LBAR(0, 3, 0, 9, COL_LANE);   /* grey tile        */
    LBAR(0, 0, 1, 1, GR_WHITE); LBAR(3, 3, 1, 1, GR_WHITE);   /* top studs    */
    LBAR(1, 2, 4, 5, GR_WHITE);                               /* centre stud  */
    LBAR(0, 0, 8, 8, GR_WHITE); LBAR(3, 3, 8, 8, GR_WHITE);   /* bottom studs */
}
/* Round two-tone token: an oval body on the lapis field with a centre pip. */
static void draw_token(unsigned char col, unsigned char row,
                       unsigned char body, unsigned char pip)
{
    unsigned char x0 = cellx(col), y0 = celly(row);
    LBAR(0, 3, 0, 9, COL_FIELD);  /* lapis footprint  */
    LBAR(1, 2, 0, 0, body);       /* top cap          */
    LBAR(0, 3, 1, 8, body);       /* middle           */
    LBAR(1, 2, 9, 9, body);       /* bottom cap       */
    LBAR(1, 2, 4, 5, pip);        /* centre pip       */
}
#undef LBAR
#endif

/* Tiny string/number builders (no conio: in GR mode we poke text directly). */
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *put_u(char *p, unsigned char v)
{
    if (v >= 100) { *p++ = (char)('0' + v / 100); v %= 100;
                    *p++ = (char)('0' + v / 10);  *p++ = (char)('0' + v % 10); }
    else if (v >= 10) { *p++ = (char)('0' + v / 10); *p++ = (char)('0' + v % 10); }
    else *p++ = (char)('0' + v);
    return p;
}

static void status(const char *msg)
{
    panel_clr(23);
    panel_text(0, 23, msg);
}

/* Last-drawn glyph per cell, so draw_board can skip the (slow) redraw of cells
 * that didn't change between calls. CELL_NONE marks "must redraw". */
static unsigned char prev_grid[3][8];

static void board_field(void)
{
    unsigned char r, c;
#ifdef UR_DHGR
    dhgr_fill(0, 79, 0, 159, 1);     /* lapis field (graphics area above the panel) */
#else
    gr_bar(0, 0, 39, 39, COL_FIELD);
#endif
    for (r = 0; r < 3; r++)          /* the field wipe clears every cell -> all dirty */
        for (c = 0; c < 8; c++)
            prev_grid[r][c] = CELL_NONE;
}

void plat_draw(unsigned char roll, const char *msg)
{
    unsigned char row, col, pl, i, pos, rr, cc;
    char grid[3][8];          /* 0 cut, 'L'/'D' piece, '*' rosette, 'E' eye, '.' dots */

    /* The field (gaps) never changes and is slow to fill in DHGR, so it's drawn
     * once per game in play_local; here only the cells overwrite it. And since
     * draw_board runs several times a turn (roll prompt, move list, result) while
     * usually 0-2 cells actually move, we cache the last-drawn glyph per cell and
     * redraw only the ones that changed (prev_grid; reset by board_field). */

    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++)
            grid[row][col] = cell_exists(row, col)
                           ? (is_rosette_cell(row, col) ? '*'
                              : (row == 1 ? 'E' : '.'))      /* eye on the shared lane */
                           : 0;
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++) {
            pos = ur_g.piece[pl][i];
            if (pos_to_cell(pl, pos, &rr, &cc))
                grid[rr][cc] = pl ? 'D' : 'L';
        }

    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++) {
            char g = grid[row][col];
            if ((unsigned char)g == prev_grid[row][col])
                continue;                       /* unchanged -> skip the slow redraw */
            prev_grid[row][col] = (unsigned char)g;
            if (g == 0) continue;               /* cut-away cell: field shows through */
            if (g == '*')      draw_rosette(col, row);
            else if (g == 'E') draw_eye(col, row);
            else if (g == '.') draw_dots(col, row);
            else if (g == 'L') draw_token(col, row, C_LIGHT, C_LPIP);
            else               draw_token(col, row, C_DARK, C_DPIP);
        }

    /* Panel on the 4 mixed-mode text lines. */
    {
        char buf[41], *p;
        panel_clr(PANEL_TOP);
        panel_text(0, PANEL_TOP, ur_g.turn ? "TURN: DARK (X)" : "TURN: LIGHT (O)");
        p = put_s(buf, "LIGHT:");
        p = put_u(p, (unsigned char)ur_score(&ur_g, 0));
        p = put_s(p, "  DARK:");
        p = put_u(p, (unsigned char)ur_score(&ur_g, 1));
        if (roll != NO_ROLL) { p = put_s(p, "   ROLL:"); p = put_u(p, roll); }
        *p = 0;
        panel_clr(PANEL_TOP + 1);
        panel_text(0, PANEL_TOP + 1, buf);
        panel_clr(PANEL_TOP + 2);
    }
    if (msg) status(msg);

    board_show();   /* lo-res: re-assert gfx after conio-free pokes (no-op for DHGR) */
}

/* List legal moves (deduped by source) on the panel, read a 1..N choice. */
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

    {
        char buf[41], *p = buf;
        for (i = 0; i < nsrc && (p - buf) < 36; i++) {
            pos = srcs[i];
            dest = (unsigned char)(pos + roll);
            p = put_u(p, (unsigned char)(i + 1));
            *p++ = ')';
            if (pos == UR_POS_START) { *p++ = 'E'; }
            else                     { p = put_u(p, pos); }
            *p++ = '>';
            p = put_u(p, dest);
            if (dest == UR_POS_HOME)      *p++ = 'H';
            else if (ur_is_rosette(dest)) *p++ = '*';
            else if (ur_dest_captures(&ur_g, player, dest)) *p++ = 'X';   /* capture */
            *p++ = ' ';
        }
        *p = 0;
        panel_clr(PANEL_TOP + 2);
        panel_text(0, PANEL_TOP + 2, buf);
    }
    status("PICK A MOVE:");

    do { c = cgetc(); } while (c < '1' || c >= (int)('1' + nsrc));
    sel = (unsigned char)(c - '1');

    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (ur_g.piece[player][pieces[i]] == pos)
            return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

/* plat.h: confirm wait, roll sound, result sound, RNG entropy. The shared
 * controller (ur_game.c) owns the turn loop. No dice animation or token glide. */
void plat_wait(void) { cgetc(); }
void plat_roll(unsigned char roll) { (void)roll; sfx_roll(); }
void plat_sfx_result(const ur_move_result *res) { sfx_for_result(res); }
uint16_t plat_seed(void) { return g_seed; }
void plat_animate(unsigned char player, unsigned char from, unsigned char to)
{ (void)player; (void)from; (void)to; }

/* plat.h: choose the AI difficulty (keyboard 1/2/3). Shown on the mixed-mode panel
 * since the board graphics are already on when the controller asks. */
uint8_t plat_pick_level(void)
{
    int c;
    panel_clr(PANEL_TOP);     panel_text(0, PANEL_TOP,     "DIFFICULTY:");
    panel_clr(PANEL_TOP + 1); panel_text(0, PANEL_TOP + 1, "1 EASY 2 NORMAL 3 HARD");
    panel_clr(PANEL_TOP + 2);
    board_show();
    do { c = cgetc(); } while (c < '1' || c > '3');
    return (uint8_t)(c - '1');
}

/* ======================================================================== */
#ifdef UR_ONLINE
/* ---- FujiNet online play (N:TCP, server-authoritative) ----------------- */
/*
 * Same model + wire protocol as the Atari/Adam/C64: the server is authoritative;
 * we send JOIN/ROLL/MOVE and render its STATE snapshots with the board renderer.
 * FujiNet attaches over the Apple II SmartPort bus, but the N: API is identical.
 * Connect/wait screens are plain text; the game itself uses the colour board.
 * MAME has no FujiNet, so locally we confirm it builds, boots, and fails the
 * network gracefully; full cross-play needs FujiNet + the Ur server.
 */

/* The Apple II has no jiffy clock, so pace network polls with a busy loop. */
static void wait_frames(unsigned char n)
{
    unsigned int i;
    while (n--)
        for (i = 0; i < 1200; i++) __asm__ ("nop");
}

static char *url_append(char *d, const char *s) { while (*s) *d++ = *s++; return d; }

static bool is_host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '-';
}

static void build_urls(void)
{
    char *p = g_net_url;
    p = url_append(p, "N:TCP://");  p = url_append(p, g_host); p = url_append(p, ":1234/"); *p = 0;
    p = g_top_url;
    p = url_append(p, "N:HTTP://"); p = url_append(p, g_host); p = url_append(p, ":8080/top"); *p = 0;
}

/* Load name + wins + host from the appkey. False if no FujiNet/SD (keeps defaults). */
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
            for (i = 0; i < hl; i++) g_host[i] = (char)buf[UR_NAME_LEN + 3 + i];
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
    for (i = 0; i < UR_NAME_LEN; i++) buf[i] = (i < nl) ? (uint8_t)g_name[i] : 0;
    buf[UR_NAME_LEN]     = (uint8_t)(g_wins & 0xFF);
    buf[UR_NAME_LEN + 1] = (uint8_t)(g_wins >> 8);
    while (g_host[hl] && hl < 32) hl++;
    buf[UR_NAME_LEN + 2] = hl;
    for (i = 0; i < hl; i++) buf[UR_NAME_LEN + 3 + i] = (uint8_t)g_host[i];
    fuji_set_appkey_details(UR_CREATOR_ID, UR_APP_ID, DEFAULT);
    fuji_write_appkey(UR_KEY_PROFILE, (uint16_t)(UR_NAME_LEN + 3 + hl), buf);
}

/* Lobby launched us? Parse the chosen server host out of its handoff appkey. */
static bool lobby_host_from_appkey(void)
{
    uint8_t  buf[MAX_APPKEY_LEN + 2];
    uint16_t cnt = 0;
    unsigned char i, j, start = 0;
    bool found = false;

    fuji_set_appkey_details(UR_LOBBY_CREATOR, UR_LOBBY_APP, DEFAULT);
    if (!fuji_read_appkey(UR_LOBBY_APPKEY, &cnt, buf) || cnt == 0) return false;
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

/* Conio field editor (text mode): RETURN confirms, DEL backspaces. */
static void edit_field(const char *prompt, char *dest, unsigned char maxlen, bool hostmode)
{
    char tmp[40];
    unsigned char len = 0, i;
    int c;
    while (dest[len] && len < maxlen) { tmp[len] = dest[len]; len++; }
    clrscr();
    cputsxy(0, 0, prompt);
    cputsxy(0, 2, hostmode ? "LETTERS DIGITS . -" : "A-Z 0-9 SPACE");
    cputsxy(0, 3, "RETURN=OK  DEL=BACK");
    for (;;) {
        gotoxy(0, 6);
        for (i = 0; i < len; i++) cputc(tmp[i]);
        cputc(' ');
        cclearxy((unsigned char)(len + 1), 6, (unsigned char)(maxlen - len + 1));
        c = cgetc();
        if (c == '\r' || c == '\n') break;
        if (c == 8 || c == 127 || c == 21) { if (len) len--; continue; }  /* back/DEL/left */
        if (len >= maxlen) continue;
        if (hostmode) { if (is_host_char((char)c)) tmp[len++] = (char)c; }
        else {
            char ch = (char)c;
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ')
                tmp[len++] = ch;
        }
    }
    tmp[len] = 0;
    for (i = 0; i <= len; i++) dest[i] = tmp[i];
    if (hostmode) build_urls();
    profile_save();
}

/* /top leaderboard over N:HTTP: count byte then up to 10 of name[8]+wins(LE). */
static void show_leaderboard(void)
{
    uint8_t  buf[128];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n = 0;
    unsigned char count, i, j, base;
    char name[UR_NAME_LEN + 1];
    uint16_t wins;

    clrscr();
    cputsxy(0, 0, "LEADERBOARD");
    if (network_init() != FN_ERR_OK ||
        network_open(g_top_url, 4 /* HTTP GET */, 0) != FN_ERR_OK) {
        cputsxy(0, 3, "COULD NOT REACH THE SERVER.");
        cputsxy(0, 5, "NEEDS FUJINET + THE UR SERVER");
        cputsxy(0, 6, "WEB PORT (8080) REACHABLE.");
        cputsxy(0, 23, "PRESS A KEY"); cgetc(); return;
    }
    for (i = 0; i < 100; i++) {
        if (network_status(g_top_url, &bw, &conn, &err) != FN_ERR_OK) break;
        if (bw > 0) { n = network_read(g_top_url, buf, sizeof(buf)); break; }
        if (conn == 0) break;
        wait_frames(3);
    }
    network_close(g_top_url);
    if (n < 1)            cputsxy(0, 3, "NO REPLY FROM SERVER.");
    else if (buf[0] == 0) cputsxy(0, 3, "NO GAMES RECORDED YET.");
    else {
        count = buf[0];
        cputsxy(0, 2, "#  NAME      WINS");
        for (i = 0; i < count; i++) {
            base = (unsigned char)(1 + i * (UR_NAME_LEN + 2));
            if ((int16_t)(base + UR_NAME_LEN + 2) > n) break;
            for (j = 0; j < UR_NAME_LEN; j++) name[j] = (char)buf[base + j];
            name[UR_NAME_LEN] = 0;
            wins = (uint16_t)(buf[base + UR_NAME_LEN] | ((uint16_t)buf[base + UR_NAME_LEN + 1] << 8));
            gotoxy(0, (unsigned char)(4 + i));
            cprintf("%-2u %-8s  %u", i + 1, name, wins);
        }
    }
    cputsxy(0, 23, "PRESS A KEY"); cgetc();
}

/* Poll for the next STATE. 1 = got one, 0 = disconnected/error, -1 = key pressed. */
static int8_t read_state(ur_snapshot *snap)
{
    uint8_t  buf[UR_STATE_MSG_LEN];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n;
    for (;;) {
        if (kbhit()) { cgetc(); return -1; }
        if (network_status(g_net_url, &bw, &conn, &err) != FN_ERR_OK) return 0;
        if (bw >= UR_STATE_MSG_LEN) break;
        if (conn == 0) return 0;
        wait_frames(3);
    }
    n = network_read(g_net_url, buf, UR_STATE_MSG_LEN);
    if (n < (int16_t)UR_STATE_MSG_LEN) return 0;
    return ur_proto_decode_state(buf, (uint8_t)n, snap) ? 1 : 0;
}

/* Wait (text mode) for the first STATE; counts down to the server's AI fallback. */
static int8_t online_wait(ur_snapshot *snap)
{
    uint8_t  buf[UR_STATE_MSG_LEN];
    uint16_t bw;
    uint8_t  conn, err;
    int16_t  n;
    unsigned char secs = 60, ticks = 0;
    gotoxy(0, 11); cprintf("COMPUTER JOINS IN %2u ", secs);
    for (;;) {
        if (kbhit()) { cgetc(); return -1; }
        if (network_status(g_net_url, &bw, &conn, &err) != FN_ERR_OK) return 0;
        if (bw >= UR_STATE_MSG_LEN) {
            n = network_read(g_net_url, buf, UR_STATE_MSG_LEN);
            return (n >= (int16_t)UR_STATE_MSG_LEN &&
                    ur_proto_decode_state(buf, (uint8_t)n, snap)) ? 1 : 0;
        }
        if (conn == 0) return 0;
        wait_frames(6);
        if (++ticks >= 10) { ticks = 0; if (secs) secs--;
                             gotoxy(0, 11); cprintf("COMPUTER JOINS IN %2u ", secs); }
    }
}

/* Returns true if the player bailed out of waiting to play the computer locally. */
static bool online_game(void)
{
    ur_snapshot snap;
    uint8_t cmd[2 + UR_NAME_LEN + 2];
    int8_t picked, rc;

    clrscr();                          /* text-mode connect / wait screen */
    cputsxy(0, 0, "THE ROYAL GAME OF UR");
    if (network_init() != FN_ERR_OK) {
        cputsxy(0, 3, "NETWORK INIT FAILED. KEY..."); cgetc(); return false;
    }
    if (network_open(g_net_url, OPEN_MODE_RW, 0) != FN_ERR_OK) {
        cputsxy(0, 3, "CONNECT FAILED. KEY..."); cgetc(); return false;
    }
    network_write(g_net_url, cmd, ur_proto_join(cmd, g_name));
    cputsxy(0, 2, "CONNECTING TO:");
    cputsxy(0, 3, g_host);
    cputsxy(0, 6, "WAITING FOR AN OPPONENT...");
    cputsxy(0, 8, "OR PRESS A KEY TO PLAY THE COMPUTER");

    rc = online_wait(&snap);
    if (rc == -1) { network_close(g_net_url); return true; }
    if (rc == 0) { cputsxy(0, 13, "DISCONNECTED. KEY..."); cgetc();
                   network_close(g_net_url); return false; }

    BOARD_ON();                        /* enter the colour board for play */
    board_field();
    for (;;) {
        ur_g = snap.state;
        if (snap.flags & UR_FLAG_CAPTURED)      sfx_capture();
        else if (snap.flags & UR_FLAG_SCORED)   sfx_score();
        else if (snap.flags & UR_FLAG_ROSETTE)  sfx_rosette();

        if (snap.phase == UR_PHASE_OVER) {
            plat_draw(NO_ROLL, snap.winner == (int8_t)snap.seat
                                ? "YOU WIN! KEY..." : "YOU LOSE. KEY...");
            cgetc();
            break;
        }
        if (snap.state.turn != snap.seat) {
            plat_draw(snap.phase == UR_PHASE_MOVE ? snap.roll : NO_ROLL,
                       "OPPONENT'S TURN...");
        } else if (snap.phase == UR_PHASE_ROLL) {
            plat_draw(NO_ROLL, "YOUR TURN - KEY TO ROLL");
            cgetc();
            sfx_roll();
            network_write(g_net_url, cmd, ur_proto_roll(cmd));
        } else {
            plat_draw(snap.roll, (const char *)0);
            picked = plat_choose_move(snap.seat, snap.roll);
            if (picked >= 0)
                network_write(g_net_url, cmd, ur_proto_move(cmd, (unsigned char)picked));
        }
        rc = read_state(&snap);
        if (rc == -1) break;
        if (rc == 0) { status("DISCONNECTED. KEY..."); cgetc(); break; }
    }
    BOARD_OFF();
    network_close(g_net_url);
    return false;
}
#endif /* UR_ONLINE */
/* ======================================================================== */

/* Run a local game (via the shared controller) and show the result. The board is on
 * for the controller's draws, then we switch back to text for the result screen. */
static void run_and_show(bool ai1)
{
    unsigned char winner;

    BOARD_ON();                    /* colour board for the game (lo-res or DHGR) */
    board_field();                 /* fill the field once (cells redraw each turn) */
    winner = ur_run_game(ai1 ? 1 : 0);
    BOARD_OFF();                   /* back to text for the result + menu */
    clrscr();
    if (ai1) {
#ifdef UR_ONLINE
        if (winner == 0) { g_wins++; profile_save(); }   /* record the win */
#endif
        cputsxy(0, 2, winner == 0 ? "YOU WIN!" : "YOU LOSE.");
    } else
        cputsxy(0, 2, winner == 0 ? "LIGHT (O) WINS!" : "DARK (X) WINS!");
    cputsxy(0, 4, "PRESS A KEY");
    cgetc();
}

/* Title music: the Hurrian Hymn, played once at boot. Skippable — returns as soon
 * as a key is waiting (left for the menu's cgetc), so the player can go straight to
 * a mode. (apple2_music_note scales eighth-ticks to a ~110bpm tempo.) */
static bool g_played_music = false;
static void play_hymn(void)
{
    uint16_t i;
    if (g_played_music) return;       /* only on the first title (not every return) */
    g_played_music = true;
    for (i = 0; i < ur_hymn_len; i++) {
        if (kbhit()) return;          /* skip; key left for the menu */
        apple2_music_note(ur_hymn[i].note, ur_hymn[i].dur);
    }
}

int main(void)
{
    unsigned char key;

    BOARD_OFF();                    /* ensure text mode for the menu */
    clrscr();
    snd_init();                     /* pick the sound backend (Mockingboard or speaker) */

#ifdef UR_ONLINE
    profile_load();                 /* name/wins/host from the appkey, if any */
    lobby_host_from_appkey();       /* launched from the lobby? use its server */
    build_urls();
#endif

    for (;;) {
        clrscr();
        cputsxy(0, 0, "THE ROYAL GAME OF UR");
        cputsxy(0, 1, "APPLE II");
#ifdef UR_ONLINE
        gotoxy(0, 3); cprintf("SERVER: %s", g_host);
        if (g_name[0]) { gotoxy(0, 4); cprintf("PLAYER %s  WINS %u", g_name, g_wins); }
        cputsxy(0, 6,  "1) TWO PLAYERS");
        cputsxy(0, 7,  "2) ONE PLAYER VS COMPUTER");
        cputsxy(0, 8,  "3) ONLINE");
        cputsxy(0, 9,  "4) SET NAME");
        cputsxy(0, 10, "5) SET SERVER HOST");
        cputsxy(0, 11, "6) LEADERBOARD");
        cputsxy(0, 13, "SELECT (1-6):");
#else
        cputsxy(0, 4, "1) TWO PLAYERS");
        cputsxy(0, 5, "2) ONE PLAYER VS COMPUTER");
        cputsxy(0, 7, "SELECT (1-2):");
#endif

        play_hymn();              /* the Hurrian Hymn (once at boot, skippable) */

        /* Seed the RNG from how long the player takes to choose. */
        while (!kbhit()) g_seed++;
        key = (unsigned char)cgetc();

#ifdef UR_ONLINE
        if (key == '4') { edit_field("SET NAME", g_name, UR_NAME_LEN, false); continue; }
        if (key == '5') { edit_field("SET SERVER HOST", g_host, 32, true); continue; }
        if (key == '6') { show_leaderboard(); continue; }
        if (key == '3') {                  /* online (server-authoritative) */
            if (online_game())             /* bailed out of waiting -> play the computer */
                run_and_show(true);
            continue;
        }
#endif
        if (key != '1' && key != '2')
            continue;

        run_and_show(key == '2');    /* 1 = hot-seat, 2 = vs computer */
    }
    return 0;
}
