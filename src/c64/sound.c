/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Commodore 64 sound effects on the SID (6581/8580) at $D400 — voice 1, driven
 * directly with register writes. The C64 has the richest sound of the four
 * targets; this is still just simple blocking blips (no music player yet, see
 * docs/future-enhancements.md). SID frequency value Fn = Hz / 0.0596 (PAL); we
 * use precomputed values since exact pitch doesn't matter for effects.
 */
#include <stdint.h>

#include "ur.h"
#include "sound.h"

#define SIDR(off) (*(volatile unsigned char *)(0xD400u + (off)))

#define WAVE_TRI   0x10
#define WAVE_SAW   0x20
#define WAVE_NOISE 0x80

static void sid_init(void)
{
    SIDR(0x18) = 0x0F;          /* master volume = max */
}

/* Set voice-1 frequency + control (waveform | gate-bit0), with a punchy ADSR. */
static void sid_voice(unsigned int fn, unsigned char ctrl)
{
    SIDR(0x05) = 0x00;          /* attack 0, decay 0 */
    SIDR(0x06) = 0xF0;          /* sustain 15, release 0 */
    SIDR(0x00) = (unsigned char)(fn & 0xFF);
    SIDR(0x01) = (unsigned char)(fn >> 8);
    SIDR(0x04) = ctrl;
}

/* Crude busy-wait; `dur` units of ~tens of ms on a 1MHz 6510 (approximate). */
static void hold(unsigned char dur)
{
    volatile unsigned int i;
    unsigned char j;
    for (j = 0; j < dur; j++)
        for (i = 0; i < 2500u; i++) { /* spin */ }
}

static void note(unsigned int fn, unsigned char wave, unsigned char dur)
{
    sid_init();
    sid_voice(fn, (unsigned char)(wave | 0x01));   /* gate on  */
    hold(dur);
    SIDR(0x04) = wave;                              /* gate off -> release */
}

void snd_silence(void)
{
    SIDR(0x04) = 0x00;
    SIDR(0x18) = 0x00;
}

void sfx_roll(void)        /* dice rattle: a few noise bursts */
{
    unsigned char k;
    sid_init();
    for (k = 0; k < 3; k++) {
        sid_voice(8000u, WAVE_NOISE | 0x01); hold(3);
        sid_voice(4000u, WAVE_NOISE | 0x01); hold(2);
    }
    SIDR(0x04) = 0x00;
}

void sfx_move(void)    { note(13000u, WAVE_TRI, 3); }

void sfx_capture(void) /* downward saw + a noise thud */
{
    note(13000u, WAVE_SAW, 3);
    note(7000u,  WAVE_SAW, 4);
    note(3500u,  WAVE_NOISE, 5);
}

void sfx_rosette(void) { note(13000u, WAVE_TRI, 3); note(18000u, WAVE_TRI, 5); }

void sfx_score(void)   { note(8800u, WAVE_TRI, 4); note(11000u, WAVE_TRI, 4);
                         note(13000u, WAVE_TRI, 6); }

void sfx_win(void)     { note(8800u, WAVE_TRI, 5);  note(11000u, WAVE_TRI, 5);
                         note(13000u, WAVE_TRI, 5); note(18000u, WAVE_TRI, 12); }

void sfx_for_result(const ur_move_result *r)
{
    if      (r->won)      sfx_win();
    else if (r->captured) sfx_capture();
    else if (r->scored)   sfx_score();
    else if (r->rosette)  sfx_rosette();
    else                  sfx_move();
}
