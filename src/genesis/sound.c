/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Genesis sound effects + title-music notes on the SN76489 PSG — the very same
 * chip (at the very same 3.579545 MHz clock) as the Master System's, so this is
 * src/sms/sound.c ported near-verbatim: the byte protocol and every precomputed
 * period constant carry over unchanged. Only the transport differs (SGDK's
 * PSG_write() instead of a Z80 `out`), and the crude Z80 spin-loops become
 * frame waits via SYS_doVBlankProcess().
 *
 * The Genesis also has the YM2612 FM chip — an obvious future upgrade for the
 * hymn (a proper lyre patch) — but PSG parity with the SMS comes first.
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

void snd_silence(void)
{
    PSG_write(0x9F);    /* ch0 tone off  */
    PSG_write(0xBF);    /* ch1 tone off  */
    PSG_write(0xDF);    /* ch2 tone off  */
    PSG_write(0xFF);    /* noise off     */
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

/* ---- title music: the Hurrian Hymn -------------------------------------- *
 * SN76489 tone period for each scale note the hymn uses (B4..A5), indexed by
 * (midi - music_note_lo). n = 111860 / Hz (the true octave) — the shared
 * tuning with the SMS/Adam/ColecoVision players. */
static const u16 hymn_sn[11] = {
    226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127  /* B4 C5 .. A5 */
};

/* NTSC frames per eighth-note tick: 17 frames ~= 283ms (~106bpm) — matches the
 * SMS tempo. */
#define GEN_MUS_EIGHTH 17u

/* Play one melody step (MIDI number or MUSIC_REST) for `eighths` ticks on tone
 * channel 0, with a short note-off tail so repeated pitches articulate. The
 * pad is polled EVERY FRAME (not just between notes like the Z80 ports): any
 * press is returned so the caller can skip the tune instantly. */
u16 gen_music_note(uint8_t midi, uint8_t eighths)
{
    u16 pressed = 0;
    u16 f, frames = (u16)eighths * GEN_MUS_EIGHTH;

    if (midi == MUSIC_REST) snd_silence();
    else sn_tone(0, hymn_sn[midi - music_note_lo], 3);
    for (f = 0; f < frames; f++) {
        if (midi != MUSIC_REST && f == frames - 3)
            sn_tone(0, hymn_sn[midi - music_note_lo], 15);   /* articulation gap */
        SYS_doVBlankProcess();
        pressed |= JOY_readJoypad(JOY_1)
                   & (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START
                      | BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT);
        if (pressed) break;
    }
    return pressed;
}
