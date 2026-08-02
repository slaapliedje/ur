/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Commodore Amiga (68000 / OCS) platform layer — the third 16-bit port.
 *
 * A near-verbatim port of the Atari ST layer (src/st/main.c) to the machine the
 * ST spent the late 80s feuding with: the same 320x200 16-colour carved
 * Standard-of-Ur board and ziggurat title scene, drawn to an OCS custom screen's
 * four bitplanes. The Amiga picks its 16 from 4096 colours — exactly the STe's
 * palette space — so this build uses the STe's richer colour table, including
 * the deep-ember dusk band (C_DUSK2) the plain ST can't mix.
 *
 * OS-friendly and Kickstart 1.3-safe throughout (V33 calls only): a CUSTOMSCREEN
 * + backdrop window from intuition, rect fills through graphics.library's
 * RectFill (i.e. THE BLITTER — full redraws are a blink, not a 68000 crawl)
 * with direct bitplane writes for single pixels, IDCMP VANILLAKEY keyboard
 * input, WaitTOF timing, and Paula driven politely through audio.device: three
 * allocated channels looping a plucked-string wavetable, steered per-frame with
 * ADCMD_PERVOL — the hymn plays as melody + octave double + deep tonic drone,
 * with a real pluck envelope. Number-key menus + move picks, like the other
 * keyboard ports.
 *
 * Built with the AmigaPorts m68k-amigaos-gcc (bebbo's toolchain) -> AmigaDOS hunk
 * executable; packaged onto a self-booting ADF by makefiles/amiga.mk (xdftool).
 * Run in MAME (a500, KS1.3), amiberry, FS-UAE, or real hardware.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <devices/audio.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <clib/alib_protos.h>  /* CreatePort/CreateExtIO (amiga.lib, -lamiga) */

#include <stdint.h>
#include "ur_game.h"          /* shared controller + plat.h + ur.h */
#include "music.h"            /* the Hurrian Hymn melody data (shared) */
#include "font8.h"            /* shared 1bpp 8x8 font (from src/sms; -I in amiga.mk) */

struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;

#define SCRW 320
#define SCRH 200

/* ---- colours: the ST enum, with the STe's 4096-colour values ------------ */
enum { C_BG=0, C_SHELL, C_GOLD, C_FACE, C_HI, C_DARK, C_SH, C_WHITE, C_GREY, C_HILITE,
       C_SHADOW, C_DUSK, C_DUSK2, C_BRICK, C_BRICKL, C_SAND };
static const UBYTE ur_rgb[16][3] = {      /* 4 bits/channel (OCS = 4096 colours) */
    { 0, 2, 5},  {15,14,11},  {15,10, 2},  { 0, 6,11},
    { 4,10,15},  {12, 2, 0},  { 0, 2, 6},  {15,15,15},
    { 8, 9, 8},  { 2,14, 2},  { 0, 1, 2},  {13, 7, 2},
    { 9, 3, 1},  {11, 8, 2},  {15,13, 5},  {15,14,12}
};

/* ---- screen / bitplane primitives --------------------------------------- */
static struct Screen   *scr;
static struct Window   *win;
static struct RastPort *rport;            /* the screen's RastPort: blitter fills */
static UBYTE  *plane[4];
static UWORD   bpr;                       /* bytes per bitplane row (40) */

static void pix(int x, int y, uint16_t c)
{
    ULONG off = (ULONG)y * bpr + ((ULONG)x >> 3);
    UBYTE m = (UBYTE)(0x80u >> (x & 7));
    int p;
    for (p = 0; p < 4; p++) {
        if ((c >> p) & 1) plane[p][off] |= m; else plane[p][off] &= (UBYTE)~m;
    }
}
/* Rect fills go through graphics.library RectFill — i.e. THE BLITTER — which
 * turned the ~1-2s full-screen 68000 redraws into a blink. (KS1.3-safe; fills
 * all four planes per the pen, exactly the old per-plane word loop's result.) */
static void frectw(int x, int y, int w, int h, uint16_t c)
{
    SetAPen(rport, c);
    RectFill(rport, x, y, x + w - 1, y + h - 1);
}
static void frect(int x, int y, int w, int h, uint16_t c) { frectw(x, y, w, h, c); }
static void clr(uint16_t c) { frectw(0, 0, SCRW, SCRH, c); }
static void hline(int x, int y, int w, uint16_t c) { frectw(x, y, w, 1, c); }

/* filled circle + filled diamond (motifs / tokens) as blitter runs, one per
 * scanline — 25 RectFills beat ~500 four-plane read-modify-write pixels */
static void disc(int cx, int cy, int r, uint16_t c)
{
    int dx, dy;
    for (dy = -r; dy <= r; dy++) {
        for (dx = r; dx > 0 && dx*dx + dy*dy > r*r; dx--) ;
        hline(cx - dx, cy + dy, 2*dx + 1, c);
    }
}
static void diamond(int cx, int cy, int r, uint16_t c)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int w = r - (dy < 0 ? -dy : dy);
        hline(cx - w, cy + dy, 2*w + 1, c);
    }
}

/* ---- title scene: the Great Ziggurat of Ur at dusk ---------------------- *
 * Same procedural scene as the ST family; the dusk band uses the STe's
 * two-shade treatment since the Amiga has the same 4096-colour palette. */
static void zbox(int cx, int ybase, int w, int h, int d,
                 uint16_t front, uint16_t top, uint16_t side)
{
    int i;
    frect(cx - w / 2, ybase - h, w, h, front);
    for (i = 3; i < h; i += 5)                       /* mud-brick coursing */
        frect(cx - w / 2, ybase - h + i, w, 1, side);
    for (i = 1; i <= d; i++) {
        frect(cx - w / 2 + i, ybase - h - i, w, 1, top);
        frect(cx + w / 2 + i - 1, ybase - h - i, 1, h, side);
    }
}

static void title_scene(void)
{
    static const UWORD sx[] = { 12,40,70,95,130,183,210,245,275,300,55,118,225,290 };
    static const UBYTE sy[] = { 30,52,24,44,58, 30,50,26,46,58,66,64,60,34 };
    int i, y;

    clr(C_BG);
    for (y = 100; y < 128; y++) {                     /* two-shade dusk glow */
        uint16_t c = (y >= 120) ? C_DUSK : C_DUSK2;
        if (y >= 110 || (y & 1)) frectw(0, y, SCRW, 1, c);
    }
    for (i = 0; i < (int)(sizeof sy / sizeof sy[0]); i++)
        pix(sx[i], sy[i], (i & 1) ? C_WHITE : C_GREY); /* stars              */

    disc(262, 124, 16, C_DUSK);                        /* setting sun: halo, */
    disc(262, 124, 13, C_GOLD);                        /*   disc,            */
    disc(262, 124, 5,  C_SHELL);                       /*   hot core         */

    frectw(0, 128, SCRW, 1, C_BRICK);                  /* horizon line       */
    frectw(0, 129, SCRW, 17, C_SAND);

    /* the ziggurat: three brick terraces + the blue-glazed shrine on top */
    zbox(160, 140, 150, 24, 6, C_BRICKL, C_SAND, C_BRICK);
    zbox(160, 116, 106, 20, 6, C_BRICKL, C_SAND, C_BRICK);
    zbox(160,  96,  64, 16, 6, C_BRICKL, C_SAND, C_BRICK);
    zbox(160,  80,  30, 14, 5, C_FACE,   C_HI,   C_SH);
    frect(157, 72, 6, 8, C_GOLD);                      /* gilded doorway     */

    frect(152, 96, 16, 44, C_SAND);                    /* the grand stair    */
    for (y = 98; y < 140; y += 3) frect(152, y, 16, 1, C_BRICK);
    frect(152, 96, 1, 44, C_BRICK);
    frect(167, 96, 1, 44, C_BRICK);
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

/* ---- board geometry (shared with every port) ---------------------------- */
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

/* ---- board pieces -------------------------------------------------------- */
static void draw_cell(int col, int row)
{
    int x = cellx(col), y = celly(row), cx = x + CELL/2, cy = y + CELL/2;
    frectw(x, y, CELL, CELL, C_FACE);          /* face                          */
    frectw(x, y, CELL, 1, C_WHITE);            /* crisp lit rim                 */
    frectw(x, y + 1, CELL, 2, C_HI);           /* top highlight                 */
    frect(x, y + 1, 2, CELL - 1, C_HI);        /* left highlight                */
    frectw(x, y + CELL - 2, CELL, 2, C_SH);    /* bottom shadow                 */
    frect(x + CELL - 2, y + 1, 2, CELL - 1, C_SH);      /* right shadow         */
    frectw(x, y + CELL - 1, CELL, 1, C_SHADOW);         /* dark seat line       */
    frect(x + CELL - 1, y + 2, 1, CELL - 2, C_SHADOW);  /* dark right edge      */
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
    disc(cx + 2, cy + 2, 13, C_SHADOW);                    /* offset drop shadow */
    disc(cx, cy, 12, player ? C_DARK : C_SHELL);           /* body               */
    disc(cx - 4, cy - 4, player ? 2 : 3, C_WHITE);         /* specular glint     */
    disc(cx, cy, 4,  player ? C_GOLD : C_FACE);            /* centre pip         */
}
static void draw_bead(int px, int py, uint8_t player)
{
    disc(px + 1, py + 1, 4, C_SHADOW);                     /* drop shadow        */
    disc(px, py, 4, player ? C_DARK : C_SHELL);
}

/* ---- plat.h: draw the board + HUD + status ------------------------------ */
void plat_draw(uint8_t roll, const char *msg)
{
    int row, col, i, pl, rr, cc, n;

    clr(C_BG);
    text(72, 0, "THE ROYAL GAME OF UR", C_GOLD);
    text(8, 12, "TURN:", C_WHITE);
    text(56, 12, ur_g.turn ? "DARK " : "LIGHT", ur_g.turn ? C_DARK : C_SHELL);
    text(120, 12, "ROLL:", C_WHITE);
    if (roll != UR_NO_ROLL) text_u(168, 12, roll, C_GOLD);

    /* raised-slab drop shadow along the H silhouette's south/east edges */
    for (row = 0; row < 3; row++)
        for (col = 0; col < 8; col++)
            if (cell_exists(row, col)) {
                int x = cellx(col), y = celly(row);
                int s = (row == 2 || !cell_exists(row + 1, col));
                int e = (col == 7 || !cell_exists(row, col + 1));
                if (s) frect(x + 4, y + CELL, CELL - (e ? 0 : 4), 4, C_SHADOW);
                if (e) frect(x + CELL, y + 4, 4, CELL - 4, C_SHADOW);
                if (s && e) frect(x + CELL, y + CELL, 4, 4, C_SHADOW);
            }
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

/* ---- input: IDCMP VANILLAKEY on the backdrop window ---------------------- *
 * key_avail() is the Cconis() twin: it drains the port into a one-key pushback
 * buffer without blocking, so the hymn can poll for a skip without eating the
 * menu key. waitkey() folds the video beam position into the RNG entropy — WHEN
 * a human presses a key is genuinely random. */
static uint16_t g_seed = 0xACE1u;
static int pending_key = -1;

static int key_avail(void)
{
    struct IntuiMessage *im;
    if (pending_key >= 0) return 1;
    while ((im = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
        if (im->Class == IDCMP_VANILLAKEY && pending_key < 0)
            pending_key = (int)(im->Code & 0xFF);
        ReplyMsg((struct Message *)im);
    }
    return pending_key >= 0;
}
static int waitkey(void)
{
    int k;
    while (!key_avail()) WaitPort(win->UserPort);
    k = pending_key; pending_key = -1;
    g_seed = (uint16_t)((g_seed << 5) ^ (g_seed >> 3) ^ (uint16_t)VBeamPos() ^ (uint16_t)k);
    return k;
}

void     plat_wait(void) { (void)waitkey(); }
uint16_t plat_seed(void) { return (uint16_t)(g_seed ^ (uint16_t)VBeamPos()); }
void     plat_animate(uint8_t player, uint8_t from, uint8_t to) { (void)player; (void)from; (void)to; }

/* ---- Paula sound via audio.device (KS1.3-polite) ------------------------- *
 * THREE allocated channels, each endlessly looping a 16-sample plucked-string
 * wavetable (fundamental + 2nd + 3rd harmonics) at volume 0; every "register
 * poke" is an ADCMD_PERVOL steering one channel's period/volume. The hymn is a
 * three-voice arrangement: the melody with a per-frame pluck envelope, an
 * octave double underneath, and a deep tonic drone (B2) — drone accompaniment
 * being about as period-authentic as arrangement gets for ancient modal music.
 * Paula period = 3546895 / (freq * 16). SFX use the melody channel. */
#define CH_MEL   0
#define CH_OCT   1
#define CH_DRONE 2

static struct MsgPort *aport;
static struct IOAudio *awr[3], *apv;
static BYTE  *wave;
static UBYTE  amasks[4] = { 0x07, 0x0B, 0x0D, 0x0E };  /* any three channels */
static UBYTE  chbit[3];
static int    audio_ok = 0;

/* one cycle of the "string": fundamental + 2nd + 3rd harmonics, peak ±76 */
static const BYTE wave16[16] = {
    0, 53, 76, 67, 45, 30, 24, 16, 0, -16, -24, -30, -45, -67, -76, -53
};

static void audio_init(void)
{
    ULONG mask;
    int   i, n;

    aport = CreatePort(NULL, 0);
    if (!aport) return;
    awr[0] = (struct IOAudio *)CreateExtIO(aport, sizeof(struct IOAudio));
    apv    = (struct IOAudio *)CreateExtIO(aport, sizeof(struct IOAudio));
    wave   = (BYTE *)AllocMem(16, MEMF_CHIP);
    if (!awr[0] || !apv || !wave) return;
    for (i = 0; i < 16; i++) wave[i] = wave16[i];

    awr[0]->ioa_Request.io_Message.mn_Node.ln_Pri = 10;   /* don't get stolen */
    awr[0]->ioa_Data   = amasks;
    awr[0]->ioa_Length = sizeof(amasks);
    if (OpenDevice((STRPTR)"audio.device", 0, (struct IORequest *)awr[0], 0) != 0)
        return;

    /* which three channels did we get? (a stock machine always grants three) */
    mask = (ULONG)awr[0]->ioa_Request.io_Unit & 0x0F;
    n = 0;
    for (i = 0; i < 4; i++)
        if ((mask & (1u << i)) && n < 3) chbit[n++] = (UBYTE)(1u << i);
    while (n < 3) { chbit[n] = chbit[0]; n++; }   /* degrade: roles share      */

    *apv = *awr[0];                       /* same device + allocation key      */

    for (i = 0; i < 3; i++) {             /* one looping wave per channel      */
        if (i > 0) {
            awr[i] = (struct IOAudio *)CreateExtIO(aport, sizeof(struct IOAudio));
            if (!awr[i]) return;
            *awr[i] = *awr[0];
        }
        awr[i]->ioa_Request.io_Unit    = (APTR)(ULONG)chbit[i];
        awr[i]->ioa_Request.io_Command = CMD_WRITE;
        awr[i]->ioa_Request.io_Flags   = ADIOF_PERVOL;
        awr[i]->ioa_Data   = (UBYTE *)wave;
        awr[i]->ioa_Length = 16;
        awr[i]->ioa_Cycles = 0;                           /* loop forever     */
        awr[i]->ioa_Period = 500;
        awr[i]->ioa_Volume = 0;                           /* born silent      */
        SendIO((struct IORequest *)awr[i]);
    }
    audio_ok = 1;
}
static void pv(int ch, UWORD per, UWORD vol)   /* steer one channel */
{
    if (!audio_ok) return;
    apv->ioa_Request.io_Unit    = (APTR)(ULONG)chbit[ch];
    apv->ioa_Request.io_Command = ADCMD_PERVOL;
    apv->ioa_Request.io_Flags   = ADIOF_PERVOL;
    apv->ioa_Period = per;
    apv->ioa_Volume = vol;
    DoIO((struct IORequest *)apv);
}
static void snd_silence(void)
{ pv(CH_MEL, 500, 0); pv(CH_OCT, 500, 0); pv(CH_DRONE, 500, 0); }
static void vbl(int n) { while (n-- > 0) WaitTOF(); }

/* Paula periods for the hymn range B4(71)..A5(81): 3546895/(f*16). */
static const UWORD am_period[11] = { 449,424,400,377,356,336,317,300,283,267,252 };
#define PER_DRONE 1795                 /* B2 — two octaves under the tonic */
#define VOL_SFX 52

/* One melody step as a three-voice pluck: attack at 62, fast decay to a
 * sustain plateau, then a slow string-fade — octave double at 5/8 volume,
 * the drone humming untouched underneath. */
static void am_music_note(unsigned char midi, unsigned char eighths)
{
    int f, frames = (int)eighths * 13, v;
    UWORD per;
    if (midi == MUSIC_REST) {
        pv(CH_MEL, 500, 0); pv(CH_OCT, 500, 0);
        vbl(frames);
        return;
    }
    {
        uint8_t idx = (uint8_t)(midi - music_note_lo);
        if (idx > 10) idx = 10;
        per = am_period[idx];
    }
    for (f = 0; f < frames; f++) {
        v = 62 - f * 5;                            /* pluck attack->sustain  */
        if (v < 24) v = 24 - ((f - 8) >> 1);       /* ...then slow fade      */
        if (v < 12) v = 12;
        pv(CH_MEL, per, (UWORD)v);
        pv(CH_OCT, (UWORD)(per * 2), (UWORD)((v * 5) >> 3));
        WaitTOF();
    }
    pv(CH_MEL, per, 6); pv(CH_OCT, (UWORD)(per * 2), 4);
    vbl(1);                                        /* articulation dip       */
}
static void play_hymn(void)            /* once on the title; skippable by a key */
{
    uint16_t i;
    snd_silence();
    pv(CH_DRONE, PER_DRONE, 9);        /* the drone breathes in */
    for (i = 0; i < ur_hymn_len; i++) {
        if (key_avail()) break;        /* key waiting -> skip (don't consume it) */
        am_music_note(ur_hymn[i].note, ur_hymn[i].dur);
    }
    snd_silence();                     /* melody, octave and drone all out */
}

static void sfx_tone(UWORD per, UWORD vol, int v)
{ pv(CH_MEL, per, vol); vbl(v); snd_silence(); }

static uint16_t sfx_rng = 0xBEEF;      /* local xorshift — never touches the game RNG */
static uint16_t srnd(void)
{ sfx_rng ^= sfx_rng << 7; sfx_rng ^= sfx_rng >> 9; sfx_rng ^= sfx_rng << 8; return sfx_rng; }

static void sfx_roll(void)             /* dice rattle: jittered low clatter */
{
    int i;
    for (i = 0; i < 10; i++) { pv(CH_MEL, (UWORD)(650 + (srnd() & 255)), 30); vbl(1); }
    snd_silence();
}
static void sfx_for_result(const ur_move_result *res)
{
    if (res->won)           { sfx_tone(378,VOL_SFX,8); sfx_tone(283,VOL_SFX,8); sfx_tone(188,VOL_SFX,16); }
    else if (res->captured) { int i; for (i = 0; i < 6; i++) { pv(CH_MEL, (UWORD)(450 + (srnd() & 127)), 40); vbl(1); }
                              sfx_tone(674,VOL_SFX,8); }
    else if (res->scored)   { sfx_tone(337,VOL_SFX,7); sfx_tone(226,VOL_SFX,12); }
    else if (res->rosette)  { sfx_tone(378,VOL_SFX,6); sfx_tone(302,VOL_SFX,6); sfx_tone(252,VOL_SFX,10); }
    else                    { sfx_tone(337,40,5); }
}

void plat_roll(uint8_t roll) { (void)roll; sfx_roll(); }
void plat_sfx_result(const ur_move_result *res) { sfx_for_result(res); }

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

/* ---- video init + title / menu ------------------------------------------ */
static UWORD *blank_pointer;              /* chip RAM, all zero */

static int video_init(void)
{
    static struct NewScreen ns = {
        0, 0, SCRW, SCRH, 4,              /* 320x200, 16 colours          */
        0, 1, 0,                          /* pens; ViewModes 0 = lores    */
        CUSTOMSCREEN | SCREENQUIET,
        NULL, NULL, NULL, NULL
    };
    static struct NewWindow nw = {
        0, 0, SCRW, SCRH, 0, 1,
        IDCMP_VANILLAKEY,
        WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_RMBTRAP,
        NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 0, CUSTOMSCREEN
    };
    int i;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 33);
    GfxBase       = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 33);
    if (!IntuitionBase || !GfxBase) return 0;

    scr = OpenScreen(&ns);
    if (!scr) return 0;
    ShowTitle(scr, FALSE);
    nw.Screen = scr;
    win = OpenWindow(&nw);
    if (!win) return 0;

    blank_pointer = (UWORD *)AllocMem(12, MEMF_CHIP | MEMF_CLEAR);
    if (blank_pointer) SetPointer(win, blank_pointer, 1, 1, 0, 0);

    for (i = 0; i < 16; i++)
        SetRGB4(&scr->ViewPort, i, ur_rgb[i][0], ur_rgb[i][1], ur_rgb[i][2]);

    bpr = scr->BitMap.BytesPerRow;
    for (i = 0; i < 4; i++) plane[i] = (UBYTE *)scr->BitMap.Planes[i];
    rport = &scr->RastPort;
    SetDrMd(rport, JAM1);              /* plain pen fills for the blitter path */

    audio_init();
    snd_silence();
    return 1;
}

static int title_menu(void)        /* returns vs_ai (1 = vs computer) */
{
    int k;
    title_scene();                 /* the Great Ziggurat of Ur at dusk */
    text(80, 8, "THE ROYAL GAME OF UR", C_GOLD);
    text(64, 20, "MESOPOTAMIA - C.2600 BCE", C_SHELL);
    text(88, 152, "1) TWO PLAYERS", C_WHITE);
    text(60, 164, "2) ONE PLAYER VS COMPUTER", C_WHITE);
    text(104, 180, "SELECT 1 OR 2:", C_SHELL);
    text(200, 192, "COMMODORE AMIGA", C_GREY);
    play_hymn();                   /* the Hurrian Hymn, once, skippable by a key */
    for (;;) {
        k = waitkey();
        if (k == '1') return 0;
        if (k == '2') return 1;
    }
}

int main(void)
{
    if (!video_init()) return 20;
    for (;;) {
        uint8_t winner = ur_run_game((uint8_t)title_menu());
        title_scene();             /* victory beneath the ziggurat */
        text(winner ? 120 : 116, 156, winner ? "DARK WINS!" : "LIGHT WINS!", C_GOLD);
        text(108, 176, "PRESS ANY KEY", C_SHELL);
        plat_wait();
    }
    return 0;
}
