/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Apple II sound — the 1-bit speaker ($C030).
 *
 * Touching $C030 flips the speaker cone once.  A square-wave tone is therefore a
 * loop that touches $C030, waits a fixed spell (the half-period -> pitch), and
 * repeats (the count -> duration).  `pitch` is the delay between toggles (LOWER =
 * HIGHER note); `toggles` is how many half-cycles to emit.  The waits are
 * cycle-counted with nops so cc65's optimiser can't drop them.
 */
#include <stdint.h>
#include "sound.h"
#include "music.h"          /* the Hurrian Hymn melody data (shared) */

#define SPKR (*(volatile unsigned char *)0xC030)

/* One square-wave tone. pitch: smaller = higher; toggles: more = longer. */
static void tone(unsigned char pitch, unsigned int toggles)
{
    unsigned char d;
    while (toggles--) {
        (void)SPKR;                 /* click the cone (LDA $C030) */
        d = pitch;
        while (d--) {
            __asm__ ("nop");        /* delay -> half-period (keeps the loop alive) */
            __asm__ ("nop");
        }
    }
}

/* A short gap so chained tones are distinct (no toggles -> silence). */
static void rest(unsigned int n) { while (n--) __asm__ ("nop"); }

void snd_silence(void) { }          /* the speaker is silent unless toggled */

/* Dice rattle: a few quick bursts at jittering pitch. */
void sfx_roll(void)
{
    tone(60, 90);
    tone(40, 70);
    tone(70, 90);
}

void sfx_move(void) { tone(90, 110); }                 /* a single mid blip */

/* Capture: a harsh low buzz then a click. */
void sfx_capture(void)
{
    tone(150, 140);
    rest(2000);
    tone(110, 80);
}

void sfx_rosette(void) { tone(45, 160); }              /* bright, pleasant */

/* Score (bear-off): two rising blips. */
void sfx_score(void)
{
    tone(80, 110);
    rest(1500);
    tone(50, 130);
}

/* Win: a little rising fanfare. */
void sfx_win(void)
{
    tone(90, 120);
    rest(1200);
    tone(64, 130);
    rest(1200);
    tone(40, 200);
}

void sfx_for_result(const ur_move_result *r)
{
    if (r->won)            sfx_win();
    else if (r->captured)  sfx_capture();
    else if (r->scored)    sfx_score();
    else if (r->rosette)   sfx_rosette();
    else                   sfx_move();
}

/* ---- title music: the Hurrian Hymn -------------------------------------- *
 * The 1-bit speaker has no pitch register; pitch is the toggle delay (smaller =
 * higher) and duration is the toggle count. Per scale note (B4..A5, indexed by
 * midi - music_note_lo): hymn_pitch = the delay, hymn_tpe = half-cycles per
 * eighth-note (higher notes need more toggles for the same wall-clock time).
 * Values are computed from a ~9-cycle inner loop; if the constant is off the whole
 * tune just transposes by a fixed ratio (intervals are preserved), so it stays
 * recognizable. */
static const unsigned char hymn_pitch[11] = {
    115, 109, 103, 97, 91, 86, 81, 77, 72, 68, 65        /* B4 C5 .. A5 */
};
static const unsigned int hymn_tpe[11] = {
    277, 292, 309, 328, 350, 370, 393, 413, 442, 468, 490
};

/* Play one melody note (MIDI number, or MUSIC_REST) for `eighths` eighth-note
 * ticks (1/2/4), with a short gap so repeated pitches articulate. The melody loop
 * + the input poll live in main.c. */
void apple2_music_note(unsigned char midi, unsigned char eighths)
{
    unsigned char e;
    if (midi == MUSIC_REST) {
        for (e = 0; e < eighths; e++) { rest(9000); rest(9000); }  /* ~silence */
        return;
    }
    {
        unsigned char idx = (unsigned char)(midi - music_note_lo);
        tone(hymn_pitch[idx], (unsigned int)(hymn_tpe[idx] * eighths));
    }
    rest(1800);                 /* brief note-off gap (articulation) */
}
