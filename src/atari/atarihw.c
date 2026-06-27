/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Atari colour + POKEY sound helpers. See atarihw.h. */

#include "atarihw.h"
#include "music.h"          /* the Hurrian Hymn melody data (shared) */

/* POKEY audio registers (channel 1) + colour shadows. The A8 (under the OS) and
 * the 5200 (no OS, cc65 runtime) differ only in base addresses: POKEY is $D200 vs
 * $E800, and the colour shadows the per-frame VBI copies to GTIA are at $02C4-$02C8
 * (A8 OS) vs $0C-$10 (cc65 5200). ANTIC (incl. WSYNC $D40A) is at $D400 on both. */
#ifdef UR_A5200
#define AUDF1  (*(volatile unsigned char *)0xE800)
#define AUDC1  (*(volatile unsigned char *)0xE801)
#define AUDCTL (*(volatile unsigned char *)0xE808)
#define COLOR0 (*(volatile unsigned char *)0x000C)   /* COLPF0 shadow */
#define COLOR1 (*(volatile unsigned char *)0x000D)   /* COLPF1        */
#define COLOR2 (*(volatile unsigned char *)0x000E)   /* COLPF2        */
#define COLOR3 (*(volatile unsigned char *)0x000F)   /* COLPF3        */
#define COLOR4 (*(volatile unsigned char *)0x0010)   /* COLBK         */
#else
#define AUDF1  (*(volatile unsigned char *)0xD200)   /* frequency (divisor)   */
#define AUDC1  (*(volatile unsigned char *)0xD201)   /* distortion + volume   */
#define AUDCTL (*(volatile unsigned char *)0xD208)   /* audio control         */
/* OS colour shadow registers (copied to the hardware each vertical blank).
 * Shared by the mode-2 text rows and the mode-4 board rows:
 *   mode 2 text:  bg = COLOR2, text luminance = COLOR1
 *   mode 4 board: 00->COLOR4, 01->COLOR0, 10->COLOR1, 11->COLOR2 (COLOR3 if inverse)
 */
#define COLOR0 (*(volatile unsigned char *)0x02C4)   /* light pieces ("01")     */
#define COLOR1 (*(volatile unsigned char *)0x02C5)   /* dark pieces ("10") + text */
#define COLOR2 (*(volatile unsigned char *)0x02C6)   /* text bg + cell lines ("11") */
#define COLOR3 (*(volatile unsigned char *)0x02C7)   /* rosette ("11" inverse)  */
#define COLOR4 (*(volatile unsigned char *)0x02C8)   /* board bg + border        */
#endif
#define WSYNC  (*(volatile unsigned char *)0xD40A)   /* wait for horiz. sync (both) */

/* Display-list pointer shadow: A8 OS SDLSTL=$0230, cc65 5200 runtime SDLSTL=$05.
 * Both point at a GR.0-structured list (the 5200's is built in a5200scr.c), so the
 * board-row mode-4 patch is identical. */
#ifdef UR_A5200
#define DL_PTR 0x0005
#else
#define DL_PTR 0x0230
#endif

#define PURE_TONE  0xA0   /* distortion bits for a clean tone (OR in volume 0-15) */
#define BUZZ_TONE  0x40   /* buzzy distortion (for capture)                       */

/* Busy-wait `lines` scanlines via WSYNC. The volatile store is never optimised
 * away, so this gives a reliable, audible duration. ~15700 scanlines ~= 1 second. */
static void delay_lines(unsigned int lines)
{
    unsigned int i;
    for (i = 0; i < lines; i++)
        WSYNC = 0;
}

static void tone(unsigned char pitch, unsigned char vol, unsigned int lines)
{
    AUDCTL = 0;
    AUDF1  = pitch;
    AUDC1  = (unsigned char)(PURE_TONE | (vol & 0x0F));
    delay_lines(lines);
    AUDC1  = 0;
}

static void buzz(unsigned char pitch, unsigned char vol, unsigned int lines)
{
    AUDCTL = 0;
    AUDF1  = pitch;
    AUDC1  = (unsigned char)(BUZZ_TONE | (vol & 0x0F));
    delay_lines(lines);
    AUDC1  = 0;
}

/* SOUNDR ($41): the OS makes the "disk drive" I/O sound during SIO when this is
 * nonzero. FujiNet's constant SIO polling turns that into a continuous drone, so
 * silence it. Our own sound effects write POKEY directly and are unaffected. */
void atari_quiet_sio(void)
{
#ifndef UR_A5200
    *(volatile unsigned char *)0x0041 = 0;   /* SOUNDR (OS SIO drone) — A8 only */
#endif
}

/* The board field / border. Was black; now a dark lapis so the board reads as a
 * carved lapis tablet (the tile faces are a brighter lapis, COLOR2). */
#define BOARD_FIELD 0x90   /* deep lapis (dark) — tile faces (COLOR2 0x94) sit brighter */

void atari_setup_colors(void)
{
    /* Standard-of-Ur palette: lapis + gold, shell-white pieces. */
    COLOR0 = 0x0E;          /* shell white -> Light pieces + tile bevel highlight */
    COLOR1 = 0xCA;          /* green       -> Dark pieces + text                  */
    COLOR2 = 0x94;          /* lapis       -> text bg + tile faces ("11")         */
    COLOR3 = 0x1A;          /* gold        -> rosettes + ziggurat                 */
    COLOR4 = BOARD_FIELD;   /* dark lapis  -> board field + border                */
}

/* ---- custom character set ----------------------------------------------- *
 * We copy the 1 KB ROM font ($E000) into RAM and overwrite a few glyphs, then
 * point ANTIC at it via CHBAS ($02F4). Only the characters used to draw the
 * board are changed, so all normal text still uses the standard glyphs.
 * Glyph bytes are 8 rows, top first, MSB = leftmost pixel.
 *
 * Glyphs are ANTIC mode-4 encoded: 4 colour-pixels wide, 2 bits each
 * (00=bg, 01=COLOR0, 10=COLOR1, 11=COLOR2/COLOR3-if-inverse), 8 rows tall.
 * Each board cell is TWO characters wide (8 colour-pixels), so every element has
 * a left-half and right-half glyph. Internal (screen) codes, board rows only:
 *   tile    '+' 0x0B / '=' 0x1D   (filled lapis tile, COLOR2, with a white
 *                                  top/left bevel highlight = COLOR0, on the
 *                                  dark-lapis field = COLOR4 "00")
 *   rosette '*' 0x0A / '&' 0x06   (drawn inverse: gold petals "11"->COLOR3,
 *                                  white centre "01"->COLOR0, on the field)
 *   light   '#' 0x03 / '$' 0x04   ("01" white disc)
 *   dark    '@' 0x20 / '[' 0x3B   ("10" green ring)
 *
 * Tiles: a raised lapis inlay lit from the top-left — top row + left column are a
 * white highlight ("01"), the body is lapis ("11"), a 1px field gap ("00") all
 * round separates neighbouring tiles.  Rosette: a rounded 8-point gold flower with
 * a white (pearl) centre, clearly distinct from a plain lane.
 */
static const unsigned char g_tile_l[8]    = {0x00,0x15,0x1F,0x1F,0x1F,0x1F,0x3F,0x00};
static const unsigned char g_tile_r[8]    = {0x00,0x54,0xFC,0xFC,0xFC,0xFC,0xFC,0x00};
static const unsigned char g_rosette_l[8] = {0x0F,0x3F,0xFD,0xF5,0xF5,0xFD,0x3F,0x0F};
static const unsigned char g_rosette_r[8] = {0xF0,0xFC,0x7F,0x5F,0x5F,0x7F,0xFC,0xF0};
static const unsigned char g_light_l[8]   = {0x05,0x15,0x55,0x55,0x55,0x55,0x15,0x05};
static const unsigned char g_light_r[8]   = {0x50,0x54,0x55,0x55,0x55,0x55,0x54,0x50};
static const unsigned char g_dark_l[8]    = {0x0A,0x20,0x80,0x80,0x80,0x80,0x20,0x0A};
static const unsigned char g_dark_r[8]    = {0xA0,0x08,0x02,0x02,0x02,0x02,0x08,0xA0};

/* Dice glyphs are 1bpp (drawn on mode-2 text rows): a tetrahedral die,
 * '_' = unmarked (outline), '^' = marked (filled). Internal codes 0x3F / 0x3E. */
static const unsigned char g_die0[8]    = {0x18,0x24,0x24,0x42,0x42,0x81,0xFF,0x00}; /* pyramid outline */
static const unsigned char g_die1[8]    = {0x18,0x24,0x24,0x5A,0x5A,0x81,0xFF,0x00}; /* pyramid + center pip */

/* Title-screen decoration (mode 4): a solid block ("11" -> COLOR2 lapis, or
 * COLOR3 gold when drawn inverse) and a cuneiform wedge/nail. */
static const unsigned char g_solid[8]   = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; /* ']' */
static const unsigned char g_wedge[8]   = {0x3C,0x3C,0xFF,0x3C,0x3C,0x3C,0x0C,0x00}; /* '\' cuneiform nail */

/* Up-arrows marking each player's start/entry, full cell width (8px, left+right
 * halves) so they centre under the entry diamond: white (COLOR0), green (COLOR1). */
static const unsigned char g_arrow_w_l[8] = {0x01,0x05,0x15,0x55,0x01,0x01,0x01,0x00}; /* '{' */
static const unsigned char g_arrow_w_r[8] = {0x40,0x50,0x54,0x55,0x40,0x40,0x40,0x00}; /* '|' */
static const unsigned char g_arrow_g_l[8] = {0x02,0x0A,0x2A,0xAA,0x02,0x02,0x02,0x00}; /* '}' */
static const unsigned char g_arrow_g_r[8] = {0x80,0xA0,0xA8,0xAA,0x80,0x80,0x80,0x00}; /* '~' */

/* Token pip seen through the PMG donut hole on a DARK piece: a 2-clock cream
 * ("01"->COLOR0) dot at the cell centre (colour-pixels 3-4, scanlines 2-5).
 * Left half = pixel 3, right half = pixel 4.  Internal codes 0x08 / 0x09.        */
static const unsigned char g_pip_l[8] = {0x00,0x00,0x01,0x01,0x01,0x01,0x00,0x00}; /* '(' */
static const unsigned char g_pip_r[8] = {0x00,0x00,0x40,0x40,0x40,0x40,0x00,0x00}; /* ')' */

/* Move cursor: a right-pointing gold triangle ("11" -> COLOR3 when drawn inverse),
 * placed in the field column just left of the selected cell.  Internal code 0x07. */
static const unsigned char g_cursor[8] = {0x00,0xC0,0xF0,0xFC,0xFC,0xF0,0xC0,0x00}; /* ''' */

/* Over-allocated so we can align the active 1 KB to a $400 boundary at run time. */
static unsigned char font_ram[1024 + 1024];

static void put_glyph(unsigned char *font, unsigned char code, const unsigned char *g)
{
    unsigned char i;
    unsigned char *dst = font + (unsigned int)code * 8;
    for (i = 0; i < 8; i++)
        dst[i] = g[i];
}

void atari_setup_charset(void)
{
    unsigned int   base = ((unsigned int)font_ram + 0x3FF) & 0xFC00;  /* 1 KB align */
    unsigned char *font = (unsigned char *)base;
#ifdef UR_A5200
    const unsigned char *rom = (const unsigned char *)0xF800;  /* 5200 CHRORG (BIOS font) */
#else
    const unsigned char *rom = (const unsigned char *)0xE000;  /* A8 OS ROM font */
#endif
    unsigned int i;

    for (i = 0; i < 1024; i++)
        font[i] = rom[i];

    put_glyph(font, 0x0B, g_tile_l);    put_glyph(font, 0x1D, g_tile_r);    /* '+' '=' */
    put_glyph(font, 0x0A, g_rosette_l); put_glyph(font, 0x06, g_rosette_r); /* '*' '&' */
    put_glyph(font, 0x03, g_light_l);   put_glyph(font, 0x04, g_light_r);   /* '#' '$' */
    put_glyph(font, 0x20, g_dark_l);    put_glyph(font, 0x3B, g_dark_r);    /* '@' '[' */
    put_glyph(font, 0x3F, g_die0);     /* '_' unmarked die */
    put_glyph(font, 0x3E, g_die1);     /* '^' marked die   */
    put_glyph(font, 0x3D, g_solid);    /* ']' solid block (title art) */
    put_glyph(font, 0x3C, g_wedge);    /* '\' cuneiform wedge         */
    put_glyph(font, 0x7B, g_arrow_w_l); put_glyph(font, 0x7C, g_arrow_w_r); /* '{' '|' white up */
    put_glyph(font, 0x7D, g_arrow_g_l); put_glyph(font, 0x7E, g_arrow_g_r); /* '}' '~' green up */
    put_glyph(font, 0x08, g_pip_l);     put_glyph(font, 0x09, g_pip_r);     /* '(' ')' cream pip */
    put_glyph(font, 0x07, g_cursor);    /* ''' move cursor (drawn inverse = gold) */

#ifdef UR_A5200
    *(volatile unsigned char *)0xD409 = (unsigned char)(base >> 8);  /* ANTIC CHBASE (no OS) */
#else
    *(volatile unsigned char *)0x02F4 = (unsigned char)(base >> 8);  /* CHBAS shadow (OS VBI) */
#endif
}

/* Switch the board's character rows to ANTIC mode 4 (multicolour) by patching the
 * OS display list in place. Standard GR.0 DL: 3x$70 (blank), $42+addr (char row 0),
 * then 23x$02 (char rows 1..23), $41+addr. Char row R lives at dl[5 + R], so we
 * set the board rows to $04. Text rows stay mode 2. The vertical board spans
 * screen char rows 4..18. */
void atari_mode4_board(void)
{
    unsigned char *dl = *(unsigned char **)DL_PTR;
    unsigned char r;
    for (r = 4; r <= 18; r++)
        dl[5 + r] = 0x04;
}

/* Inverse of atari_mode4_board: put the board rows back to mode-2 text, so the
 * whole screen can show ordinary text (e.g. the instructions pages). */
void atari_text_mode(void)
{
    unsigned char *dl = *(unsigned char **)DL_PTR;
    unsigned char r;
    for (r = 4; r <= 18; r++)
        dl[5 + r] = 0x02;
}

/* ---- title-screen lapis gradient (display list interrupt) --------------- *
 * A DLI rewrites COLBK per board-band row, fading dark->light lapis behind the
 * ziggurat. dli.s reads dli_table/dli_len; we flag rows 4..17 in the display
 * list (one DLI each, 14 == dli_len so the index wraps once per frame).        */
extern void dli_handler(void);      /* asm, src/atari/dli.s */
unsigned char dli_table[16];        /* COLBK (field) per board-band row (_dli_table) */
unsigned char dli_table2[16];       /* COLPF2 (tile face) per board-band row         */
unsigned char dli_len;              /* number of gradient entries (_dli_len)      */

void atari_title_sky_on(void)
{
#ifndef UR_A5200   /* 5200 v1: no DLI (no NMI/VDSLST routing yet) — flat field */
    /* Two-hue sky: deep indigo-violet (hue 7) easing through blue (hue 8) to
     * bright lapis (hue 9), luminance rising top->bottom. Blending hues yields
     * more apparent steps than one hue's 8 luminances, so the fade looks smooth
     * with no dithering (which flickered). Hue changes sit at equal luminance so
     * there's no brightness jump. One steady colour per board-band row. */
    static const unsigned char grad[14] = {
        0x72, 0x72, 0x74, 0x84, 0x86, 0x86, 0x88,   /* indigo-violet -> blue   */
        0x98, 0x9A, 0x9A, 0x9C, 0x9C, 0x9E, 0x9E    /* -> lapis (brightening)  */
    };
    unsigned char *dl = *(unsigned char **)0x0230;
    unsigned char i;

    for (i = 0; i < 14; i++) {
        dli_table[i]  = grad[i];
        dli_table2[i] = 0x94;                        /* tile faces unchanged on the title */
    }
    dli_len = 14;
    *(volatile unsigned char *)0x0200 = (unsigned char)((unsigned int)dli_handler & 0xFF);
    *(volatile unsigned char *)0x0201 = (unsigned char)((unsigned int)dli_handler >> 8);
    for (i = 4; i <= 17; i++)
        dl[5 + i] |= 0x80;                          /* DLI bit on each board-band row */
    *(volatile unsigned char *)0xD40E = 0xC0;       /* NMIEN: DLI + VBI (write-only)  */
#endif
}

void atari_title_sky_off(void)
{
#ifndef UR_A5200
    unsigned char *dl = *(unsigned char **)0x0230;
    unsigned char i;
    *(volatile unsigned char *)0xD40E = 0x40;       /* NMIEN: VBI only (DLI off) */
    for (i = 4; i <= 17; i++)
        dl[5 + i] &= 0x7F;                          /* clear DLI bits */
    COLOR4 = BOARD_FIELD;                           /* restore the lapis board field */
#endif
}

/* ---- in-game board sheen (display list interrupt) ----------------------- *
 * The "living lapis tablet": the same DLI engine, but during play it grades the
 * board FIELD (COLBK, the "00" pixels) down the board band — deep-blue shaded
 * top/bottom edges easing through a brighter lapis in the middle, so the board
 * reads as a raised tablet catching light.  Every shade stays darker than the
 * tile faces (COLOR2 = 0x94), so the carved tiles keep popping; the tiles, gold
 * rosettes, pieces, and text (COLOR0/1/2/3) are all untouched.  This is the
 * Atari's signature: per-scanline colour the flat 5-register board can't have.
 * Mirrors atari_title_sky_on (14 flagged rows == dli_len, so idx self-wraps).   */
void atari_board_dli_on(void)
{
#ifndef UR_A5200   /* 5200 v1: flat lapis field (no DLI yet) */
    static const unsigned char grad[14] = {
        0x82, 0x90, 0x92, 0x92, 0x92, 0x92,   /* dark-blue edge -> luminous lapis */
        0x92, 0x92, 0x92, 0x92, 0x92, 0x92,   /* uniform body (no mid-band stripe)*/
        0x90, 0x82                            /* -> deep lapis -> dark-blue edge  */
    };
    /* Tile faces (COLPF2) catch a soft highlight through the board's vertical
     * centre and dim toward the framed edges — the carved lapis reads as a raised
     * tablet lit from the front. Stays brighter than the field grad above, so the
     * white bevel + carve relief keep popping. (The "4½-colour" trick.) */
    static const unsigned char face[14] = {
        0x96, 0x96, 0x98, 0x98, 0x9A, 0x9A,
        0x9A, 0x9A, 0x9A, 0x9A, 0x98, 0x98,
        0x96, 0x96
    };
    unsigned char *dl = *(unsigned char **)0x0230;
    unsigned char i;

    for (i = 0; i < 14; i++) {
        dli_table[i]  = grad[i];
        dli_table2[i] = face[i];
    }
    dli_len = 14;
    *(volatile unsigned char *)0x0200 = (unsigned char)((unsigned int)dli_handler & 0xFF);
    *(volatile unsigned char *)0x0201 = (unsigned char)((unsigned int)dli_handler >> 8);
    for (i = 4; i <= 17; i++)
        dl[5 + i] |= 0x80;                          /* DLI bit on each board-band row */
    *(volatile unsigned char *)0xD40E = 0xC0;       /* NMIEN: DLI + VBI               */
#endif
}

void atari_board_dli_off(void)
{
#ifndef UR_A5200
    unsigned char *dl = *(unsigned char **)0x0230;
    unsigned char i;
    *(volatile unsigned char *)0xD40E = 0x40;       /* NMIEN: VBI only (DLI off) */
    for (i = 4; i <= 17; i++)
        dl[5 + i] &= 0x7F;                          /* clear DLI bits           */
    COLOR4 = BOARD_FIELD;                           /* flat lapis field again    */
#endif
}

/* Frame the board in the active player's hue — deep blue for Light, deep green
 * for Dark — a quiet ambient "whose turn" cue under the textual turn line. Only
 * the border (COLOR4) and the gradient's top/bottom edge bands tint; the lapis
 * board interior is untouched. (The set_color1 turn-tint idea from U5.) Called
 * from draw_all each redraw, so it tracks the turn for free. A8-only for now. */
void atari_board_tint(unsigned char player)
{
#ifndef UR_A5200
    unsigned char frame = player ? 0xC2 : 0x82;     /* green (Dark) / blue (Light) */
    COLOR4        = frame;                           /* board border / frame       */
    dli_table[0]  = frame;                           /* top board-row edge meets it */
    dli_table[13] = frame;                           /* bottom board-row edge       */
#else
    (void)player;
#endif
}

/* ---- player-missile graphics: round two-tone tokens --------------------- *
 * The on-board pieces are PMG round discs, not charset glyphs — true colours (a
 * real brown for Dark, independent of the playfield) and smooth positioning for
 * future animation.  The board is VERTICAL, so a column's pieces share an X: one
 * player covers one column, no multiplexing.  The shared middle column holds both
 * colours, so it takes two players (same X, different rows):
 *   P0 = Light private column (col 0)   PCOLR0 cream
 *   P1 = Dark  private column (col 2)   PCOLR1 brown
 *   P2 = Light in the middle  (col 1)   PCOLR2 cream
 *   P3 = Dark  in the middle  (col 1)   PCOLR3 brown
 * Each disc is a donut: the centre hole shows the cell beneath — the lapis field
 * (a dark pip) under a cream Light disc, or a cream charset dot under a brown Dark
 * disc — giving the two-tone "real Ur set" look.  Off-board tray stacks stay
 * charset glyphs (fixed corners, no spare players).  The move cursor is a charset
 * pointer (main.c), so all four players are free for tokens.
 */
#ifndef UR_A5200   /* PMG tokens are A8-only for v1; the 5200 draws charset disc pieces */
#define PMBASE_R (*(volatile unsigned char *)0xD407)   /* P/M base (high byte)   */
#define GRACTL_R (*(volatile unsigned char *)0xD01D)   /* P/M output enable      */
#define SDMCTL_R (*(volatile unsigned char *)0x022F)   /* DMACTL shadow          */
#define HPOSP0_R (*(volatile unsigned char *)0xD000)   /* P0..P3 = $D000..$D003  */
#define SIZEP0_R (*(volatile unsigned char *)0xD008)   /* P0..P3 width           */
#define PCOLR0_R (*(volatile unsigned char *)0x02C0)   /* P0..P3 colour (shadow) */

#define PM_HLEFT 48    /* HPOSP for screen char column 0; a normal player is
                          8 colour clocks = one 2-char board cell wide           */
#define PM_VTOP  16    /* P/M byte offset for screen char row 0 (double-line)     */

#define TOK_LIGHT 0x0E /* cream  -> Light token bodies (P0/P2) */
#define TOK_DARK  0x24 /* brown  -> Dark  token bodies (P1/P3) */

static unsigned char pm_ram[2048];   /* 1 KB P/M area, aligned to 1 KB at run time */
static unsigned char *pm_pl[4];      /* -> the four double-line player strips       */

/* A donut disc: 4 double-line bytes (= 8 scanlines, one board cell tall), 8 colour
 * clocks (one 2-char cell) wide, with a 2-clock centre hole so the pip beneath
 * shows through. */
static const unsigned char tok_disc[4] = { 0x3C, 0x66, 0x66, 0x3C };

void atari_pmg_init(void)
{
    unsigned int   base = ((unsigned int)pm_ram + 0x3FF) & 0xFC00;  /* 1 KB align */
    unsigned char *pm   = (unsigned char *)base;
    unsigned int   i;

    for (i = 0; i < 1024; i++)
        pm[i] = 0;
    pm_pl[0] = pm + 0x200;             /* double-line players P0..P3 */
    pm_pl[1] = pm + 0x280;
    pm_pl[2] = pm + 0x300;
    pm_pl[3] = pm + 0x380;

    PMBASE_R = (unsigned char)(base >> 8);
    (&PCOLR0_R)[0] = TOK_LIGHT;        /* P0 cream, P1 brown, P2 cream, P3 brown */
    (&PCOLR0_R)[1] = TOK_DARK;
    (&PCOLR0_R)[2] = TOK_LIGHT;
    (&PCOLR0_R)[3] = TOK_DARK;
    for (i = 0; i < 4; i++) { (&SIZEP0_R)[i] = 0; (&HPOSP0_R)[i] = 0; }
    SDMCTL_R = (unsigned char)(SDMCTL_R | 0x0C);  /* + player & missile DMA */
    GRACTL_R = 0x03;                   /* enable P/M output */
}

/* Clear all four player strips + hide them — call before drawing a fresh board,
 * and when leaving the board so no stray discs hang over the menu/title. */
void atari_pmg_tokens_clear(void)
{
    unsigned char s, i;
    for (s = 0; s < 4; s++) {
        for (i = 0; i < 128; i++)
            pm_pl[s][i] = 0;
        (&HPOSP0_R)[s] = 0;
    }
}

/* Place a token disc for player `slot` (0..3) at board cell (char_x, char_y). */
void atari_pmg_token(unsigned char slot, unsigned char char_x, unsigned char char_y)
{
    unsigned char off = (unsigned char)(PM_VTOP + char_y * 4);
    unsigned char k;
    (&HPOSP0_R)[slot] = (unsigned char)(PM_HLEFT + char_x * 4);
    for (k = 0; k < 4; k++)
        pm_pl[slot][off + k] = tok_disc[k];
}

/* Clear one token disc (the 4 PM bytes at char_y) without touching the other
 * pieces in that player strip — used by the glide / fly-back animation. */
void atari_pmg_token_clear(unsigned char slot, unsigned char char_y)
{
    unsigned char off = (unsigned char)(PM_VTOP + char_y * 4);
    unsigned char k;
    for (k = 0; k < 4; k++)
        pm_pl[slot][off + k] = 0;
}
#else  /* UR_A5200: no PMG — main.c draws pieces as charset disc glyphs */
void atari_pmg_init(void) { }
void atari_pmg_tokens_clear(void) { }
void atari_pmg_token(unsigned char slot, unsigned char char_x, unsigned char char_y)
{ (void)slot; (void)char_x; (void)char_y; }
void atari_pmg_token_clear(unsigned char slot, unsigned char char_y)
{ (void)slot; (void)char_y; }
#endif /* !UR_A5200 (PMG) */

#ifndef UR_A5200
/* ---- joystick input (port 1, OS shadow registers) ----------------------- */
#define STICK0_R (*(volatile unsigned char *)0x0278)
#define STRIG0_R (*(volatile unsigned char *)0x0284)

unsigned char atari_stick(void) { return STICK0_R; }
unsigned char atari_trig(void)  { return (unsigned char)(STRIG0_R == 0); }
#endif /* !UR_A5200 (input is in a5200scr.c) */

/* Busy-wait roughly `frames` display frames (~262 scanlines each, NTSC). */
void atari_wait_frames(unsigned char frames)
{
    unsigned int i, lines = (unsigned int)frames * 262u;
    for (i = 0; i < lines; i++)
        WSYNC = 0;
}

/* Lower AUDF1 = higher pitch (it's a divisor). Durations in scanlines. */
void sfx_roll(void)    { tone(96, 8, 500);  tone(72, 8, 500); }
void sfx_move(void)    { tone(80, 6, 600); }
void sfx_capture(void) { buzz(200, 12, 2400); }
void sfx_rosette(void) { tone(64, 8, 500);  tone(48, 8, 900); }
void sfx_score(void)   { tone(72, 8, 400);  tone(50, 8, 400);  tone(36, 8, 600); }
void sfx_win(void)
{
    tone(121, 8, 700);
    tone(96,  8, 700);
    tone(72,  8, 700);
    tone(60,  8, 1200);
}

/* ---- title music: the Hurrian Hymn -------------------------------------- *
 * POKEY AUDF divisor for each scale note the hymn uses, indexed by
 * (midi - music_note_lo) over B4..A5. AUDF is a divisor (lower = higher pitch);
 * values computed from f = 63921 / (2*(AUDF+1)) for the equal-tempered pitches. */
static const unsigned char hymn_pokey[11] = {
    64, 60, 57, 53, 50, 47, 45, 42, 40, 37, 35   /* B4 C5 C#5 D5 D#5 E5 F5 F#5 G5 G#5 A5 */
};

/* Play one melody note (or a rest if MUSIC_REST) for `lines` scanlines, ending
 * with a short note-off gap so repeated pitches articulate. The melody loop +
 * input polling live in main.c (which has conio/joystick). */
void atari_music_note(unsigned char midi, unsigned int lines)
{
    unsigned int gap = (lines > 1200u) ? 400u : (lines >> 2);
    if (midi == MUSIC_REST) { AUDC1 = 0; delay_lines(lines); return; }
    AUDCTL = 0;
    AUDF1  = hymn_pokey[midi - music_note_lo];
    AUDC1  = (unsigned char)(PURE_TONE | 8);     /* clean tone, mid volume */
    delay_lines(lines - gap);
    AUDC1  = 0;                                   /* note off (articulation gap) */
    delay_lines(gap);
}
