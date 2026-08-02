/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Genesis sound: the Hurrian Hymn on the YM2612 FM synth, SFX on the SN76489.
 *
 * The PSG side is src/sms/sound.c ported near-verbatim — the Genesis kept the
 * Master System's exact chip at the exact clock, so the byte protocol and every
 * precomputed period constant carry over; only the transport differs (SGDK's
 * PSG_write()) and the Z80 spin-loops became frame waits.
 *
 * The hymn, though, gets what this hardware is famous for: a plucked lyre-ish
 * FM voice programmed straight into the YM2612's registers from the 68000 — no
 * XGM, no VGM asset, just the shared melody table driving key-on/key-off, in
 * keeping with the everything-procedural rule. Each note retriggers the
 * envelope, so held notes ring and decay like a plucked string.
 *
 * YM2612 access notes (the gotchas):
 *  - The 68000 must HOLD THE Z80 BUS while touching the YM2612. SGDK boots the
 *    null Z80 driver (the Z80 just idles), so snd_init() requests the bus once
 *    and keeps it forever. (The PSG port lives in the VDP — no bus needed.)
 *  - Slot registers within $30-$9F are ordered S1,S3,S2,S4 at +0,+4,+8,+12.
 *    The patch below is SYMMETRIC (both modulators identical, both carriers
 *    identical, algorithm 4 = two mod->carrier pairs), which both fills the
 *    chip deterministically and sidesteps the classic S2/S3 ordering trap.
 */
#include <genesis.h>

/* Drop SGDK's <stdint.h> macro remaps before the shared headers (see main.c). */
#undef uint8_t
#undef int8_t
#undef uint16_t
#undef int16_t
#undef uint32_t
#undef int32_t
#undef size_t
#undef ptrdiff_t

#include "ur.h"
#include "sound.h"
#include "music.h"          /* the Hurrian Hymn melody data (shared) */

/* Precomputed periods (n = 223722 / Hz — the SMS/Adam tuning, same clock). */
#define N_C5  428u
#define N_E5  339u
#define N_G5  285u
#define N_C6  214u
#define N_C4  854u
#define N_LOW 600u

/* Tone on channel ch (0-2): 10-bit period n, attenuation atten (0 = loudest). */
static void sn_tone(u8 ch, u16 n, u8 atten)
{
    PSG_write((u8)(0x80 | (ch << 5) | (n & 0x0F)));
    PSG_write((u8)((n >> 4) & 0x3F));
    PSG_write((u8)(0x90 | (ch << 5) | (atten & 0x0F)));
}

/* Noise channel: ctrl = feedback<<2 | rate (0x05 = white, medium). */
static void sn_noise(u8 ctrl, u8 atten)
{
    PSG_write((u8)(0xE0 | (ctrl & 0x07)));
    PSG_write((u8)(0xF0 | (atten & 0x0F)));
}

/* ---- YM2612: the plucked-lyre hymn voice on FM channel 0 ----------------- */
static void ym(u8 reg, u8 val) { YM2612_writeReg(0, reg, val); }

#define FM_KEYON()  ym(0x28, 0xF0)     /* all four slots, channel 0 */
#define FM_KEYOFF() ym(0x28, 0x00)

/* Program the lyre patch once. Slots at +0/+4 are the modulators, +8/+12 the
 * carriers (algorithm 4: two mod->carrier pairs; symmetric, see header). */
void snd_init(void)
{
    static const u8 slot_off[2][2] = { { 0, 4 }, { 8, 12 } };  /* [mod|car][pair] */
    u8 i, o;

    Z80_requestBus(TRUE);              /* held forever — the Z80 stays idle */
    YM2612_reset();
    ym(0x22, 0x00);                    /* LFO off            */
    ym(0x27, 0x00);                    /* ch3 normal mode    */
    ym(0x2B, 0x00);                    /* DAC off            */
    for (i = 0; i < 2; i++) {          /* the two modulators */
        o = slot_off[0][i];
        ym(0x30 + o, 0x01);            /* DT=0  MUL=1 — the 1:1 string ratio
                                        * (MUL=2 gave odd-only harmonics: a
                                        * clarinet, not a lyre — FFT-caught) */
        ym(0x40 + o, 0x1E);            /* TL=30 (moderate FM depth)      */
        ym(0x50 + o, 0x5F);            /* RS=1  AR=31 (instant attack)   */
        ym(0x60 + o, 0x09);            /* D1R=9 (brightness fades)       */
        ym(0x70 + o, 0x00);            /* D2R=0                          */
        ym(0x80 + o, 0x28);            /* D1L=2 RR=8                     */
        ym(0x90 + o, 0x00);            /* SSG-EG off                     */
    }
    for (i = 0; i < 2; i++) {          /* the two carriers   */
        o = slot_off[1][i];
        ym(0x30 + o, 0x01);            /* DT=0  MUL=1        */
        ym(0x40 + o, 0x03);            /* TL=3 (two carriers summed)     */
        ym(0x50 + o, 0x5F);            /* RS=1  AR=31                    */
        ym(0x60 + o, 0x07);            /* D1R=7 (the pluck decay)        */
        ym(0x70 + o, 0x05);            /* D2R=5 (audible string fade)    */
        ym(0x80 + o, 0x19);            /* D1L=1 RR=9                     */
        ym(0x90 + o, 0x00);
    }
    ym(0xB0, 0x1C);                    /* FB=3, algorithm 4  */
    ym(0xB4, 0xC0);                    /* stereo L+R, no LFO sensitivity */
    FM_KEYOFF();
}

/* (block<<11)|fnum for the hymn range B4(71)..A5(81) — the Sega-manual fnum
 * table (C=644..B=1214), B4 in block 4, C5..A5 in block 5. */
static const u16 fm_note[11] = {
    (4u<<11)|1214,                                            /* B4 */
    (5u<<11)|644, (5u<<11)|681, (5u<<11)|722, (5u<<11)|765,   /* C5 C#5 D5 D#5 */
    (5u<<11)|810, (5u<<11)|858, (5u<<11)|910, (5u<<11)|964,   /* E5 F5 F#5 G5  */
    (5u<<11)|1021, (5u<<11)|1081                              /* G#5 A5 */
};
static void fm_pluck(u8 idx)           /* retrigger = a fresh pluck */
{
    u16 v = fm_note[idx];
    FM_KEYOFF();
    ym(0xA4, (u8)(v >> 8));            /* block + fnum high FIRST (latch)... */
    ym(0xA0, (u8)(v & 0xFF));          /* ...then fnum low                   */
    FM_KEYON();
}

void snd_silence(void)
{
    PSG_write(0x9F);    /* ch0 tone off  */
    PSG_write(0xBF);    /* ch1 tone off  */
    PSG_write(0xDF);    /* ch2 tone off  */
    PSG_write(0xFF);    /* noise off     */
    FM_KEYOFF();        /* release the lyre string too */
}

/* Hold ~12ms per unit (the SMS tuning): 3/4 of a 60Hz frame per unit. */
static void hold(u16 dur)
{
    u16 f = (u16)((dur * 3 + 2) >> 2);
    if (!f) f = 1;
    while (f--) SYS_doVBlankProcess();
}

/* One tone note: period n held for `dur`, then silenced (channel 0). */
static void note(u16 n, u8 dur)
{
    sn_tone(0, n, 3);
    hold(dur);
    sn_tone(0, n, 15);
}

void sfx_roll(void)        /* dice rattle: a couple of white-noise bursts */
{
    u8 k;
    for (k = 0; k < 3; k++) {
        sn_noise(0x05, 4);  hold(3);
        sn_noise(0x05, 15); hold(2);
    }
    snd_silence();
}

static void sfx_move(void) { note(N_G5, 3); }                       /* soft click */

static void sfx_capture(void)   /* downward "whomp" + a noise thud */
{
    sn_tone(0, N_G5, 3);  hold(3);
    sn_tone(0, N_LOW, 3); hold(4);
    sn_tone(0, N_C4, 4);  hold(5);
    sn_tone(0, N_C4, 15);
    sn_noise(0x06, 6); hold(4); snd_silence();
}

static void sfx_rosette(void) { note(N_G5, 3); note(N_C6, 5); }     /* sparkle up */

static void sfx_score(void)   { note(N_C5, 4); note(N_E5, 4); note(N_G5, 6); }

static void sfx_win(void)       /* little victory run C-E-G-C */
{
    note(N_C5, 5); note(N_E5, 5); note(N_G5, 5); note(N_C6, 12);
    snd_silence();
}

void sfx_for_result(const ur_move_result *r)
{
    if      (r->won)      sfx_win();
    else if (r->captured) sfx_capture();
    else if (r->scored)   sfx_score();
    else if (r->rosette)  sfx_rosette();
    else                  sfx_move();
}

/* ---- title music: the Hurrian Hymn, on the FM lyre ----------------------- *
 * NTSC frames per eighth-note tick: 17 frames ~= 283ms (~106bpm) — matches the
 * SMS tempo. */
#define GEN_MUS_EIGHTH 17u

/* Play one melody step (MIDI number or MUSIC_REST) as a fresh pluck of the FM
 * voice, keying off near the end so repeated pitches articulate (the release
 * envelope fills the gap like a damped string). The pad is polled EVERY FRAME
 * (not just between notes like the Z80 ports): any press is returned so the
 * caller can skip the tune instantly. */
u16 gen_music_note(uint8_t midi, uint8_t eighths)
{
    u16 pressed = 0;
    u16 f, frames = (u16)eighths * GEN_MUS_EIGHTH;

    if (midi == MUSIC_REST) FM_KEYOFF();
    else fm_pluck((u8)(midi - music_note_lo));
    for (f = 0; f < frames; f++) {
        if (midi != MUSIC_REST && f == frames - 3)
            FM_KEYOFF();                                     /* articulation gap */
        SYS_doVBlankProcess();
        pressed |= JOY_readJoypad(JOY_1)
                   & (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START
                      | BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT);
        if (pressed) break;
    }
    return pressed;
}
