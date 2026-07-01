/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari ST (68000) platform layer — the first 16-bit port.
 *
 * Low-res 320x200, 16 colours, word-interleaved 4-bitplane planar bitmap. We draw
 * the carved Standard-of-Ur board straight to the Shifter's framebuffer (Physbase),
 * reusing the shared board geometry every port uses, and run the shared local-game
 * controller (src/common/ur_game.c) through the plat.h interface. Keyboard input
 * (number-key menus + move picks), like the Atari 8-bit / C64 / Apple II ports.
 *
 * Built with m68k-atari-mint-gcc -> GEMDOS .prg; run in Hatari (EmuTOS) / MAME.
 * (Sound: YM2149 via Giaccess — added next; stubbed silent here.)
 */
#include <osbind.h>
#include <mint/falcon.h>       /* Falcon video mode constants (BPS16, COL40, …) */
#include <stdint.h>
#include "ur_game.h"          /* shared controller + plat.h + ur.h */
#include "music.h"            /* the Hurrian Hymn melody data (shared) */
#include "font8.h"            /* shared 1bpp 8x8 font (from src/sms; -I in st.mk) */

/* ---- colours + framebuffer: build-specific --------------------------------- *
 * The Falcon build (UR_FALCON) uses a 320x200 TRUECOLOR chunky bitmap (one RGB565
 * word per pixel) with the full Standard-of-Ur palette; the plain ST build uses a
 * 16-colour word-interleaved 4-bitplane bitmap. Everything below (geometry, board,
 * plat_draw, sound, the shared controller) is IDENTICAL for both — only the pixel
 * format and these colour constants differ. */
#define SCRW 320
#define SCRH 200
#ifdef UR_FALCON
#define RGB(r,g,b) ((uint16_t)(((r)<<11)|((g)<<5)|(b)))   /* 5:6:5  (r,b 0..31; g 0..63) */
#define C_BG    RGB(2,5,11)      /* deep lapis field      */
#define C_SHELL RGB(30,56,22)    /* shell / cream (Light) */
#define C_GOLD  RGB(30,42,6)     /* gold                  */
#define C_FACE  RGB(5,15,24)     /* lapis cell face       */
#define C_HI    RGB(13,30,31)    /* bright lapis (bevel+) */
#define C_DARK  RGB(25,6,3)      /* carnelian (Dark)      */
#define C_SH    RGB(2,7,14)      /* shadow (bevel-)       */
#define C_WHITE RGB(31,63,31)    /* white                 */
#define C_GREY  RGB(16,34,20)    /* stone                 */
#define C_HILITE RGB(6,52,12)    /* bright green move-destination marker */
static uint16_t *fbuf;           /* truecolor framebuffer (one word/pixel) */
static int16_t   old_mode;
static void pix(int x, int y, uint16_t c) { fbuf[(long)y * SCRW + x] = c; }
static void frectw(int x, int y, int w, int h, uint16_t c)
{
    int yy, xx;
    for (yy = y; yy < y + h; yy++) { uint16_t *r = fbuf + (long)yy * SCRW + x; for (xx = 0; xx < w; xx++) r[xx] = c; }
}
#else
enum { C_BG=0, C_SHELL, C_GOLD, C_FACE, C_HI, C_DARK, C_SH, C_WHITE, C_GREY, C_HILITE };
static const uint16_t ur_palette[16] = {   /* 0x0RGB, 3 bits/channel */
    0x0012, 0x0775, 0x0751, 0x0035, 0x0257, 0x0610, 0x0013, 0x0777,
    0x0444, 0x0070, 0x0333, 0x0555, 0x0666, 0x0540, 0x0762, 0x0776
    /*            ^ idx 9 = C_HILITE: bright green move-destination marker */
};
#define STRIDE 160               /* bytes per scanline (20 groups * 4 planes * 2) */
static uint8_t *scr;
static void pix(int x, int y, uint16_t c)
{
    uint16_t *grp = (uint16_t *)(scr + (long)y * STRIDE + (x >> 4) * 8);
    uint16_t m = (uint16_t)(0x8000u >> (x & 15));
    int p;
    for (p = 0; p < 4; p++) { if ((c >> p) & 1) grp[p] |= m; else grp[p] &= (uint16_t)~m; }
}
static void frectw(int x, int y, int w, int h, uint16_t c)   /* fast 16-px-aligned fill */
{
    uint16_t p0 = (c & 1) ? 0xFFFF : 0, p1 = (c & 2) ? 0xFFFF : 0;
    uint16_t p2 = (c & 4) ? 0xFFFF : 0, p3 = (c & 8) ? 0xFFFF : 0;
    int g0 = x >> 4, gw = w >> 4, yy, g;
    for (yy = y; yy < y + h; yy++) {
        uint16_t *r = (uint16_t *)(scr + (long)yy * STRIDE) + g0 * 4;
        for (g = 0; g < gw; g++) { r[0]=p0; r[1]=p1; r[2]=p2; r[3]=p3; r += 4; }
    }
}
#endif
/* shared higher-level fills (build on pix/frectw) */
static void frect(int x, int y, int w, int h, uint16_t c)
{
    int xx, yy;
    for (yy = y; yy < y + h; yy++) for (xx = x; xx < x + w; xx++) pix(xx, yy, c);
}
static void clr(uint16_t c) { frectw(0, 0, SCRW, SCRH, c); }

#ifdef UR_FALCON
/* truecolor-only: a vertical gradient rect (top colour -> bottom colour), for lit
 * lapis cell faces — the flourish the ST's 16 colours can't do. */
static uint16_t lerp565(uint16_t a, uint16_t b, int t, int n)
{
    int ar=(a>>11)&31, ag=(a>>5)&63, ab=a&31;
    int br=(b>>11)&31, bg=(b>>5)&63, bb=b&31;
    int r = ar + (br-ar)*t/n, g = ag + (bg-ag)*t/n, bl = ab + (bb-ab)*t/n;
    return (uint16_t)((r<<11)|(g<<5)|bl);
}
static void grad_v(int x, int y, int w, int h, uint16_t ctop, uint16_t cbot)
{
    int yy;
    for (yy = 0; yy < h; yy++) frectw(x, y + yy, w, 1, lerp565(ctop, cbot, yy, h - 1));
}
#endif

/* filled circle + filled diamond (motifs / tokens) */
static void disc(int cx, int cy, int r, uint16_t c)
{
    int dx, dy;
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r) pix(cx + dx, cy + dy, c);
}
static void diamond(int cx, int cy, int r, uint16_t c)
{
    int dx, dy;
    for (dy = -r; dy <= r; dy++) {
        int w = r - (dy < 0 ? -dy : dy);
        for (dx = -w; dx <= w; dx++) pix(cx + dx, cy + dy, c);
    }
}

/* font8 glyph (8x8, 1bpp) at pixel (px,py) in colour c; transparent background */
static void glyph(int px, int py, char ch, uint16_t c)
{
    const uint8_t *g;
    int row, col;
    if ((unsigned char)ch < FONT8_FIRST || (unsigned char)ch >= FONT8_FIRST + FONT8_COUNT) ch = ' ';
    g = &font8[((unsigned char)ch - FONT8_FIRST) * 8];
    for (row = 0; row < 8; row++) {
        uint8_t b = g[row];
        for (col = 0; col < 8; col++)
            if (b & (0x80 >> col)) pix(px + col, py + row, c);
    }
}
static void text(int px, int py, const char *s, uint16_t c)
{
    for (; *s; s++, px += 8) glyph(px, py, *s, c);
}
static void text_u(int px, int py, uint8_t v, uint16_t c)   /* 0..99 */
{
    char b[3]; int n = 0;
    if (v >= 10) b[n++] = (char)('0' + v / 10);
    b[n++] = (char)('0' + v % 10); b[n] = 0;
    text(px, py, b, c);
}

/* ---- board geometry (shared with every port) --------------------------- */
#define BX 32                    /* board left (px), 16-aligned          */
#define BY 40                    /* board top (px)                       */
#define CELL 32                  /* cell size (px), 16-aligned           */
static int cellx(int col) { return BX + col * CELL; }
static int celly(int row) { return BY + row * CELL; }

static int cell_exists(int row, int col) { return row == 1 || col <= 3 || col >= 6; }
static int pos_to_cell(uint8_t player, uint8_t pos, int *row, int *col)
{
    if (pos < 1 || pos > UR_PATH_LEN) return 0;
    if (pos <= 4)       { *row = player ? 2 : 0; *col = 4 - pos; }
    else if (pos <= 12) { *row = 1;              *col = pos - 5; }
    else                { *row = player ? 2 : 0; *col = (pos == 13) ? 7 : 6; }
    return 1;
}
static int is_rosette_cell(int row, int col)
{
    return (row != 1 && (col == 0 || col == 6)) || (row == 1 && col == 3);
}
static uint8_t count_at(uint8_t pl, uint8_t pos)
{
    uint8_t i, n = 0;
    for (i = 0; i < UR_PIECES; i++) if (ur_g.piece[pl][i] == pos) n++;
    return n;
}

/* ---- board pieces ------------------------------------------------------ */
static void draw_cell(int col, int row)
{
    int x = cellx(col), y = celly(row), cx = x + CELL/2, cy = y + CELL/2;
#ifdef UR_FALCON
    grad_v(x, y, CELL, CELL, C_HI, C_SH);      /* truecolor lit-from-top face   */
    frectw(x, y, CELL, 1, C_WHITE);            /* crisp top edge                */
#else
    frectw(x, y, CELL, CELL, C_FACE);          /* face                          */
    frectw(x, y, CELL, 2, C_HI);               /* top highlight (word-aligned)  */
    frect(x, y, 2, CELL, C_HI);                /* left highlight                */
    frectw(x, y + CELL - 2, CELL, 2, C_SH);    /* bottom shadow                 */
    frect(x + CELL - 2, y, 2, CELL, C_SH);     /* right shadow                  */
#endif
    if (is_rosette_cell(row, col)) {           /* gold flower rosette           */
        diamond(cx, cy, 11, C_GOLD);
        diamond(cx, cy, 6, C_FACE);
        disc(cx, cy, 2, C_WHITE);
    } else if (row == 1) {                      /* bullseye "eye" (shared lane)  */
        disc(cx, cy, 10, C_GOLD);
        disc(cx, cy, 6, C_FACE);
        disc(cx, cy, 2, C_GOLD);
    } else {                                    /* quincunx studs (private lanes)*/
        disc(cx, cy, 2, C_WHITE);
        disc(cx - 8, cy - 8, 2, C_WHITE); disc(cx + 8, cy - 8, 2, C_WHITE);
        disc(cx - 8, cy + 8, 2, C_WHITE); disc(cx + 8, cy + 8, 2, C_WHITE);
    }
}
static void draw_token(int col, int row, uint8_t player)
{
    int cx = cellx(col) + CELL/2, cy = celly(row) + CELL/2;
    disc(cx, cy, 13, C_SH);                                /* drop shadow ring   */
    disc(cx, cy, 12, player ? C_DARK : C_SHELL);           /* body               */
    disc(cx, cy, 4,  player ? C_GOLD : C_FACE);            /* centre pip         */
}
static void draw_bead(int px, int py, uint8_t player)
{
    disc(px, py, 4, player ? C_DARK : C_SHELL);
}

/* ---- plat.h: draw the board + HUD + status ----------------------------- */
void plat_draw(uint8_t roll, const char *msg)
{
    int row, col, i, pl, rr, cc, n;

    clr(C_BG);
    text(72, 0, "THE ROYAL GAME OF UR", C_GOLD);
    text(8, 12, "TURN:", C_WHITE);
    text(56, 12, ur_g.turn ? "DARK " : "LIGHT", ur_g.turn ? C_DARK : C_SHELL);
    text(120, 12, "ROLL:", C_WHITE);
    if (roll != UR_NO_ROLL) text_u(168, 12, roll, C_GOLD);

    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++)
            if (cell_exists(row, col)) draw_cell(col, row);

    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++)
            if (pos_to_cell((uint8_t)pl, ur_g.piece[pl][i], &rr, &cc))
                draw_token(cc, rr, (uint8_t)pl);

    /* trays: Light above the board, Dark below; waiting (left) + home (right) */
    text(8, 26, "LIGHT", C_SHELL);
    for (n = 0; n < count_at(0, UR_POS_START); n++) draw_bead(56 + n*10, 28, 0);
    for (n = 0; n < ur_score(&ur_g, 0); n++)        draw_bead(200 + n*10, 28, 0);
    text(8, 142, "DARK", C_DARK);
    for (n = 0; n < count_at(1, UR_POS_START); n++) draw_bead(56 + n*10, 144, 1);
    for (n = 0; n < ur_score(&ur_g, 1); n++)        draw_bead(200 + n*10, 144, 1);

    if (msg) text(8, 158, msg, C_WHITE);
}

/* ---- input (keyboard) -------------------------------------------------- */
static uint16_t g_seed = 0xACE1u;
static int waitkey(void)                  /* block for a key; accrue RNG entropy */
{
    while (!Cconis()) g_seed += 0x101u;    /* Cconis(): -1 if a char is waiting   */
    return (int)(Crawcin() & 0xFF);        /* raw, no echo onto our bitmap        */
}

void    plat_wait(void) { (void)waitkey(); }
uint16_t plat_seed(void) { return g_seed; }
void    plat_animate(uint8_t player, uint8_t from, uint8_t to) { (void)player; (void)from; (void)to; }
/* ---- YM2149 sound (XBIOS Giaccess) + the Hurrian Hymn ------------------ *
 * The ST's PSG is the AY-3-8910 family clocked at 2 MHz; Giaccess(val, reg|0x80)
 * pokes a register without supervisor mode. Note timing via Vsync (vblank ticks). */
static void psg(int reg, int val) { (void)Giaccess(val, reg | 0x80); }
static void snd_silence(void) { psg(7, 0x3F); psg(8, 0); psg(9, 0); psg(10, 0); }
static void vbl(int n) { while (n-- > 0) Vsync(); }
static void tone_a(uint16_t per) { psg(0, per & 0xFF); psg(1, (per >> 8) & 0x0F); }

/* tone period = 2 MHz / (16*freq) for the hymn range B4(71)..A5(81). */
static const uint16_t st_period[11] = { 253,239,225,213,201,190,179,169,159,150,142 };

void st_music_note(unsigned char midi, unsigned char eighths)
{
    if (midi == MUSIC_REST) {
        psg(8, 0);
    } else {
        uint8_t idx = (uint8_t)(midi - music_note_lo);
        if (idx > 10) idx = 10;
        tone_a(st_period[idx]);
        psg(7, 0x3E);                  /* tone A on, the rest off */
        psg(8, 12);
    }
    vbl((int)eighths * 13);
    psg(8, 0); vbl(1);                 /* note-off gap (articulation) */
}
static void play_hymn(void)            /* once on the title; skippable by a key */
{
    uint16_t i;
    snd_silence();
    for (i = 0; i < ur_hymn_len; i++) {
        if (Cconis()) break;           /* key waiting -> skip (don't consume it) */
        st_music_note(ur_hymn[i].note, ur_hymn[i].dur);
    }
    snd_silence();
}

static void sfx_tone(uint16_t per, uint8_t amp, int v)
{ tone_a(per); psg(7, 0x3E); psg(8, amp); vbl(v); snd_silence(); }

static void sfx_roll(void)             /* dice rattle: two short noise bursts */
{ psg(7, 0x37); psg(6, 12); psg(8, 13); vbl(6); psg(6, 20); vbl(6); snd_silence(); }

static void sfx_for_result(const ur_move_result *res)
{
    if (res->won)           { sfx_tone(213,13,8); sfx_tone(159,13,8); sfx_tone(106,13,16); }
    else if (res->captured) { psg(7,0x36); psg(6,18); tone_a(380); psg(8,14); vbl(10); snd_silence(); }
    else if (res->scored)   { sfx_tone(190,13,7); sfx_tone(127,13,12); }
    else if (res->rosette)  { sfx_tone(213,13,6); sfx_tone(170,13,6); sfx_tone(142,13,10); }
    else                    { sfx_tone(190,12,5); }
}

void    plat_roll(uint8_t roll) { (void)roll; sfx_roll(); }
void    plat_sfx_result(const ur_move_result *res) { sfx_for_result(res); }

/* Bright-green outline around a board cell — marks a legal move destination. */
static void border_cell(int col, int row, uint16_t c)
{
    int x = cellx(col), y = celly(row);
    frectw(x, y, CELL, 2, c);              /* top    (16-aligned) */
    frectw(x, y + CELL - 2, CELL, 2, c);   /* bottom              */
    frect(x, y, 2, CELL, c);               /* left                */
    frect(x + CELL - 2, y, 2, CELL, c);    /* right               */
}

/* plat.h: show the legal-move list, mark every legal destination square, and
 * return the chosen piece (-1 = none). Number keys pick directly. */
int8_t plat_choose_move(uint8_t player, uint8_t roll)
{
    uint8_t pieces[UR_PIECES], srcs[UR_PIECES], count, nsrc = 0, i, j, pos, dest;
    int k, sel, px;

    count = ur_legal_moves(&ur_g, player, roll, pieces);
    if (count == 0) return -1;
    for (i = 0; i < count; i++) {                 /* unique source squares */
        int seen = 0; pos = ur_g.piece[player][pieces[i]];
        for (j = 0; j < nsrc; j++) if (srcs[j] == pos) { seen = 1; break; }
        if (!seen) srcs[nsrc++] = pos;
    }
    px = 8;
    text(8, 172, "PICK:", C_WHITE); px = 56;
    for (i = 0; i < nsrc; i++) {                  /* "1)E>4*  2)5>9 …"      */
        pos = srcs[i]; dest = (uint8_t)(pos + roll);
        text_u(px, 172, (uint8_t)(i + 1), C_GOLD); px += (i + 1 < 10) ? 8 : 16;
        glyph(px, 172, ')', C_WHITE); px += 8;
        if (pos == UR_POS_START) { glyph(px, 172, 'E', C_SHELL); px += 8; }
        else { text_u(px, 172, pos, C_SHELL); px += (pos < 10) ? 8 : 16; }
        glyph(px, 172, '>', C_WHITE); px += 8;
        if (dest >= UR_POS_HOME) { glyph(px, 172, 'H', C_SHELL); px += 8; }
        else { text_u(px, 172, dest, C_SHELL); px += (dest < 10) ? 8 : 16; }
        if (dest < UR_POS_HOME && ur_is_rosette(dest)) { glyph(px, 172, '*', C_GOLD); px += 8; }
        else if (dest < UR_POS_HOME && ur_dest_captures(&ur_g, player, dest)) { glyph(px, 172, 'X', C_DARK); px += 8; }
        px += 8;
    }
    for (i = 0; i < nsrc; i++) {                   /* mark every legal landing square */
        int hr, hc; uint8_t d = (uint8_t)(srcs[i] + roll);
        if (d < UR_POS_HOME && pos_to_cell(player, d, &hr, &hc))
            border_cell(hc, hr, C_HILITE);
    }
    do { k = waitkey(); } while (k < '1' || k >= '1' + (int)nsrc);
    sel = k - '1';
    pos = srcs[sel];
    for (i = 0; i < count; i++)
        if (ur_g.piece[player][pieces[i]] == pos) return (int8_t)pieces[i];
    return (int8_t)pieces[0];
}

/* plat.h: choose AI difficulty (keys 1/2/3). Board is up; show a small panel. */
uint8_t plat_pick_level(void)
{
    int k;
    frectw(0, 170, SCRW, 24, C_BG);
    text(8, 172, "LEVEL:  1) EASY   2) NORMAL   3) HARD", C_WHITE);
    do { k = waitkey(); } while (k < '1' || k > '3');
    return (uint8_t)(k - '1');     /* UR_AI_EASY/NORMAL/HARD = 0/1/2 */
}

/* ---- video init + title / menu ----------------------------------------- */
static void video_init(void)
{
#ifdef UR_FALCON
    long sz = VgetSize(BPS16 | COL40);         /* 320x200 truecolor (RGB monitor) */
    void *raw = (void *)Mxalloc(sz + 256, 0);
    fbuf = (uint16_t *)(((long)raw + 255) & ~255L);
    old_mode = VsetMode(-1);
    VsetMode(BPS16 | COL40);                   /* set truecolor bit-depth first… */
    VsetScreen((long)fbuf, (long)fbuf, -1, -1); /* …then show our buffer          */
#else
    Setscreen((void *)-1L, (void *)-1L, 0);    /* low-res 320x200x4 */
    Setpalette((void *)ur_palette);
    Cconws("\033f");                            /* VT52: hide the text cursor */
    scr = (uint8_t *)Physbase();
#endif
    snd_silence();                             /* quiet the PSG at boot */
}

static int title_menu(void)        /* returns vs_ai (1 = vs computer) */
{
    int k;
    clr(C_BG);
    text(72, 24, "THE ROYAL GAME OF UR", C_GOLD);
    text(80, 40, "ATARI ST  -  68000", C_SHELL);
    text(48, 80,  "1) TWO PLAYERS", C_WHITE);
    text(48, 96,  "2) ONE PLAYER VS COMPUTER", C_WHITE);
    text(48, 128, "SELECT 1 OR 2:", C_SHELL);
    play_hymn();                   /* the Hurrian Hymn, once, skippable by a key */
    for (;;) {
        k = waitkey();
        if (k == '1') return 0;
        if (k == '2') return 1;
    }
}

int main(void)
{
    video_init();
    for (;;) {
        uint8_t winner = ur_run_game((uint8_t)title_menu());
        clr(C_BG);
        text(96, 80, winner ? "DARK WINS!" : "LIGHT WINS!", C_GOLD);
        text(64, 112, "PRESS ANY KEY", C_SHELL);
        plat_wait();
    }
    return 0;
}
