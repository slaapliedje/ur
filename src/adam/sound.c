/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Coleco Adam sound effects on the TI SN76489 PSG (ColecoVision/Adam sound chip,
 * write-only I/O port 0xFF). The chip has three square-wave tone channels and one
 * noise channel; each takes a 10-bit period and a 4-bit attenuation (0 = loudest,
 * 15 = silent). We drive it directly with port writes -- simpler than a VGM
 * player for a handful of blips, and no per-frame tick is required.
 *
 * Periods are precomputed (n = 223722 / Hz, the z88dk coleco psgT() value) to
 * avoid a runtime 32-bit divide; tone frequency is clock / (32 * n).
 */
#include <stdint.h>
#include <stdlib.h>     /* outp() */

#include "ur.h"
#include "sound.h"
#include "music.h"          /* the Hurrian Hymn melody data (shared) */

#define SN_PORT 0xFF

/* Precomputed periods (~ a C-major-ish set). Bigger n = lower pitch. */
#define N_C5  428u
#define N_E5  339u
#define N_G5  285u
#define N_C6  214u
#define N_A5  254u
#define N_C4  854u
#define N_LOW 600u

/* Tone on channel ch (0-2): 10-bit period n, attenuation atten. */
static void sn_tone(unsigned char ch, unsigned int n, unsigned char atten)
{
    outp(SN_PORT, (unsigned int)(0x80 | (ch << 5) | (n & 0x0F)));
    outp(SN_PORT, (unsigned int)((n >> 4) & 0x3F));
    outp(SN_PORT, (unsigned int)(0x90 | (ch << 5) | (atten & 0x0F)));
}

/* Noise channel: ctrl = feedback<<2 | rate (0x05 = white, medium). */
static void sn_noise(unsigned char ctrl, unsigned char atten)
{
    outp(SN_PORT, (unsigned int)(0xE0 | (ctrl & 0x07)));
    outp(SN_PORT, (unsigned int)(0xF0 | (atten & 0x0F)));
}

void snd_silence(void)
{
    outp(SN_PORT, 0x9F);    /* ch0 tone off  */
    outp(SN_PORT, 0xBF);    /* ch1 tone off  */
    outp(SN_PORT, 0xDF);    /* ch2 tone off  */
    outp(SN_PORT, 0xFF);    /* noise off     */
}

/* Crude busy-wait; `dur` units of ~12ms on a 3.58MHz Z80 (approximate). */
static void hold(unsigned char dur)
{
    volatile unsigned int i;
    unsigned char j;
    for (j = 0; j < dur; j++)
        for (i = 0; i < 7000u; i++) { /* spin */ }
}

/* One tone note: period n held for `dur`, then silenced (channel 0). */
static void note(unsigned int n, unsigned char dur)
{
    sn_tone(0, n, 3);
    hold(dur);
    sn_tone(0, n, 15);
}

void sfx_roll(void)        /* dice rattle: a couple of white-noise bursts */
{
    unsigned char k;
    for (k = 0; k < 3; k++) {
        sn_noise(0x05, 4); hold(3);
        sn_noise(0x05, 15); hold(2);
    }
    snd_silence();
}

void sfx_move(void)        { note(N_G5, 3); }                       /* soft click */

void sfx_capture(void)     /* downward "whomp" + a noise thud */
{
    sn_tone(0, N_G5, 3); hold(3);
    sn_tone(0, N_LOW, 3); hold(4);
    sn_tone(0, N_C4, 4);  hold(5);
    sn_tone(0, N_C4, 15);
    sn_noise(0x06, 6); hold(4); snd_silence();
}

void sfx_rosette(void)     { note(N_G5, 3); note(N_C6, 5); }        /* sparkle up */

void sfx_score(void)       { note(N_C5, 4); note(N_E5, 4); note(N_G5, 6); }

void sfx_win(void)         /* little victory run C-E-G-C */
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

/* ---- title music: the Hurrian Hymn -------------------------------------- *
 * SN76489 tone period for each scale note the hymn uses, indexed by
 * (midi - music_note_lo) over B4..A5. The chip divides its 3.579545MHz clock by
 * 32*n, so n = 111860 / Hz (this is the true octave; the sfx N_* constants above
 * use 2x that and so sound an octave low — fine for blips, wrong for a melody).
 * Shared by the Adam and the ColecoVision. */
static const unsigned int hymn_sn[11] = {
    226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127  /* B4 C5 .. A5 */
};

/* Fine-grained music delay (~10ms per unit). The sfx hold() is far too coarse for
 * melody — under the z88dk classic compiler (sccz80, unoptimised volatile loop) one
 * hold() unit is ~290ms, so a whole note vocabulary needs a finer base. */
static void mdelay(unsigned int units)
{
    volatile unsigned int i;
    unsigned int j;
    for (j = 0; j < units; j++)
        for (i = 0; i < 240u; i++) { /* spin */ }
}

/* ~10ms music units per eighth-note -> ~280ms eighth (~110bpm). */
#define ADAM_MUS_EIGHTH 28u

/* Play one melody note (MIDI number, or MUSIC_REST) for `eighths` eighth-note
 * ticks (1/2/4) on tone channel 0, with a short note-off gap so repeated pitches
 * articulate. The melody loop + the input poll live in main.c. */
void adam_music_note(unsigned char midi, unsigned char eighths)
{
    unsigned int units = (unsigned int)eighths * ADAM_MUS_EIGHTH;
    if (midi == MUSIC_REST) { snd_silence(); mdelay(units); return; }
    sn_tone(0, hymn_sn[midi - music_note_lo], 3);    /* tone on, mid volume */
    mdelay(units - 6u);
    sn_tone(0, hymn_sn[midi - music_note_lo], 15);   /* tone off (articulation) */
    mdelay(6u);
}
