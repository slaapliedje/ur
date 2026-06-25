/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Atari colour + POKEY sound helpers. See atarihw.h. */

#include "atarihw.h"

/* POKEY audio registers (channel 1). */
#define AUDF1  (*(volatile unsigned char *)0xD200)   /* frequency (divisor)   */
#define AUDC1  (*(volatile unsigned char *)0xD201)   /* distortion + volume   */
#define AUDCTL (*(volatile unsigned char *)0xD208)   /* audio control         */
#define WSYNC  (*(volatile unsigned char *)0xD40A)   /* wait for horiz. sync  */

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
    *(volatile unsigned char *)0x0041 = 0;
}

void atari_setup_colors(void)
{
    COLOR0 = 0x0E;   /* white  -> Light pieces            */
    COLOR1 = 0xCA;   /* green  -> Dark pieces + text      */
    COLOR2 = 0x92;   /* blue   -> text bg + cell outlines */
    COLOR3 = 0x28;   /* orange -> rosettes                */
    COLOR4 = 0x00;   /* black  -> board bg + border       */
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
 *   tile    '+' 0x0B / '=' 0x1D   ("11" blue diamond outline)
 *   rosette '*' 0x0A / '&' 0x06   ("11", drawn inverse -> orange flower)
 *   light   '#' 0x03 / '$' 0x04   ("01" white disc)
 *   dark    '@' 0x20 / '[' 0x3B   ("10" green ring)
 */
static const unsigned char g_tile_l[8]    = {0x03,0x0C,0x30,0xC0,0xC0,0x30,0x0C,0x03};
static const unsigned char g_tile_r[8]    = {0xC0,0x30,0x0C,0x03,0x03,0x0C,0x30,0xC0};
static const unsigned char g_rosette_l[8] = {0x03,0x33,0x0F,0xFF,0xFF,0x0F,0x33,0x03};
static const unsigned char g_rosette_r[8] = {0xC0,0xCC,0xF0,0xFF,0xFF,0xF0,0xCC,0xC0};
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
    const unsigned char *rom = (const unsigned char *)0xE000;
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

    *(volatile unsigned char *)0x02F4 = (unsigned char)(base >> 8);  /* CHBAS */
}

/* Switch the board's character rows to ANTIC mode 4 (multicolour) by patching the
 * OS display list in place. Standard GR.0 DL: 3x$70 (blank), $42+addr (char row 0),
 * then 23x$02 (char rows 1..23), $41+addr. Char row R lives at dl[5 + R], so we
 * set the board rows to $04. Text rows stay mode 2. The vertical board spans
 * screen char rows 4..18. */
void atari_mode4_board(void)
{
    unsigned char *dl = *(unsigned char **)0x0230;   /* SDLSTL / SDLSTH */
    unsigned char r;
    for (r = 4; r <= 18; r++)
        dl[5 + r] = 0x04;
}

/* Inverse of atari_mode4_board: put the board rows back to mode-2 text, so the
 * whole screen can show ordinary text (e.g. the instructions pages). */
void atari_text_mode(void)
{
    unsigned char *dl = *(unsigned char **)0x0230;
    unsigned char r;
    for (r = 4; r <= 18; r++)
        dl[5 + r] = 0x02;
}

/* ---- player-missile graphics (highlight cursor) ------------------------- *
 * One player (P0), double-line resolution, used as a hollow box drawn around a
 * board cell. Additive over the playfield, so it can't disturb the drawn board.
 * If the box is misaligned, nudge PM_HLEFT / PM_VTOP.
 */
#define PMBASE_R (*(volatile unsigned char *)0xD407)   /* P/M base (high byte)   */
#define GRACTL_R (*(volatile unsigned char *)0xD01D)   /* P/M output enable      */
#define SDMCTL_R (*(volatile unsigned char *)0x022F)   /* DMACTL shadow          */
#define HPOSP0_R (*(volatile unsigned char *)0xD000)   /* P0 horizontal position */
#define SIZEP0_R (*(volatile unsigned char *)0xD008)   /* P0 width               */
#define PCOLR0_R (*(volatile unsigned char *)0x02C0)   /* P0 colour (shadow)     */

#define PM_HLEFT 48    /* HPOSP0 for screen char column 0 (tune if off); a normal
                          player is 8 colour clocks = one 2-char board cell wide  */
#define PM_VTOP  16    /* P0 byte offset for screen char row 0 (double-line)   */

static unsigned char pm_ram[2048];   /* 1 KB P/M area, aligned to 1 KB at run time */
static unsigned char *pm_p0;         /* -> player-0 strip                          */

void atari_pmg_init(void)
{
    unsigned int   base = ((unsigned int)pm_ram + 0x3FF) & 0xFC00;  /* 1 KB align */
    unsigned char *pm   = (unsigned char *)base;
    unsigned int   i;

    for (i = 0; i < 1024; i++)
        pm[i] = 0;
    pm_p0 = pm + 0x200;                 /* double-line player 0 */

    PMBASE_R = (unsigned char)(base >> 8);
    PCOLR0_R = 0x1A;                    /* bright yellow */
    SIZEP0_R = 0;                       /* normal width */
    HPOSP0_R = 0;                       /* hidden until placed */
    SDMCTL_R = (unsigned char)(SDMCTL_R | 0x0C);  /* + player & missile DMA */
    GRACTL_R = 0x03;                    /* enable P/M output */
}

void atari_pmg_hide(void)
{
    HPOSP0_R = 0;
}

void atari_pmg_highlight(unsigned char char_x, unsigned char char_y)
{
    unsigned char off, i;
    if (pm_p0 == 0)
        return;
    for (i = 0; i < 128; i++)
        pm_p0[i] = 0;
    /* A box one byte taller than the cell on each side, so its top/bottom bars
     * sit in the gaps above/below the piece and the glyph stays fully visible. */
    off = (unsigned char)(PM_VTOP + char_y * 4 - 1);
    pm_p0[off]     = 0xFF;     /* top bar (above the glyph)    */
    pm_p0[off + 1] = 0x81;     /* sides flank the glyph...     */
    pm_p0[off + 2] = 0x81;
    pm_p0[off + 3] = 0x81;
    pm_p0[off + 4] = 0x81;
    pm_p0[off + 5] = 0xFF;     /* bottom bar (below the glyph) */
    HPOSP0_R = (unsigned char)(PM_HLEFT + char_x * 4);
}

/* ---- joystick input (port 1, OS shadow registers) ----------------------- */
#define STICK0_R (*(volatile unsigned char *)0x0278)
#define STRIG0_R (*(volatile unsigned char *)0x0284)

unsigned char atari_stick(void) { return STICK0_R; }
unsigned char atari_trig(void)  { return (unsigned char)(STRIG0_R == 0); }

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
