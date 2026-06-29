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
#ifdef UR_MOCKINGBOARD
#include "mockingboard.h"   /* AY-3-8910 card: richer music/SFX when one is present */
static uint8_t g_mb = 0;    /* nonzero once a Mockingboard has been detected + inited */
#endif

/* Flip the speaker cone: any access to the $C030 soft-switch toggles it.  cc65's
 * optimiser DROPS a bare `(void)*(volatile unsigned char *)0xC030;` (it discards the
 * cast-to-void volatile read as dead), which silenced every tone — so we toggle with
 * inline asm, a guaranteed access.  `BIT` reads $C030 (toggling the cone) without
 * clobbering A; cc65 never reorders or removes an `__asm__` statement. */
#define SPK_CLICK() __asm__("bit $C030")

/* One square-wave tone. pitch: smaller = higher; toggles: more = longer. */
static void tone(unsigned char pitch, unsigned int toggles)
{
    unsigned char d;
    while (toggles--) {
        SPK_CLICK();                /* flip the cone (BIT $C030) */
        d = pitch;
        while (d--) {
            __asm__ ("nop");        /* delay -> half-period (keeps the loop alive) */
            __asm__ ("nop");
        }
    }
}

/* A short gap so chained tones are distinct (no toggles -> silence). */
static void rest(unsigned int n) { while (n--) __asm__ ("nop"); }

/* ---- richer 1-bit primitives ------------------------------------------- *
 * The speaker can only click, so "richer" means shaping the click TIMING:
 *  - sweep(): ramp the pitch across the note -> a glissando (laser/sparkle),
 *  - noise(): jitter the period from an LFSR -> a real percussive rattle,
 *  - arp():   cycle a few pitches fast -> the ear fuses them into a chord.
 * All three reuse the same ~9-cycle inner delay as tone(), so the calibrated
 * pitch values (≈40 highest .. ≈170 lowest) map identically. */

/* Glissando: hold each pitch for `hold` half-cycles while stepping p0 -> p1.
 * p0 > p1 rises (pitch value falls = frequency climbs); p0 < p1 falls. */
static void sweep(unsigned char p0, unsigned char p1, unsigned char hold)
{
    unsigned char p = p0, d, h;
    for (;;) {
        for (h = hold; h; h--) {
            SPK_CLICK();
            d = p;
            while (d--) { __asm__ ("nop"); __asm__ ("nop"); }
        }
        if (p == p1) break;
        if (p < p1) p++; else p--;
    }
}

/* Percussive noise: the half-period is `base` plus an LFSR-random offset masked
 * by `spread` (use 0x1F/0x3F), so the pitch dances -> a rattle/crash, not a tone.
 * Keep base + spread < 256.  (Galois LFSR, taps 0xB400 — a platform-local rattle,
 * NOT the game RNG, which lives deterministically in src/common.) */
static unsigned int sfx_lfsr = 0xACE1u;
static void noise(unsigned char base, unsigned char spread, unsigned int toggles)
{
    unsigned char d, bit;
    while (toggles--) {
        SPK_CLICK();
        bit = (unsigned char)(sfx_lfsr & 1u);
        sfx_lfsr >>= 1;
        if (bit) sfx_lfsr ^= 0xB400u;
        d = (unsigned char)(base + (unsigned char)(sfx_lfsr & spread));
        while (d--) { __asm__ ("nop"); __asm__ ("nop"); }
    }
}

/* Arpeggio: cycle `n` pitches, each held `hold` half-cycles, for `rounds` passes.
 * Cycling faster than the ear resolves makes the notes ring together as a chord. */
static void arp(const unsigned char *pitches, unsigned char n,
                unsigned char rounds, unsigned char hold)
{
    unsigned char r, i, h, d;
    for (r = 0; r < rounds; r++)
        for (i = 0; i < n; i++)
            for (h = hold; h; h--) {
                SPK_CLICK();
                d = pitches[i];
                while (d--) { __asm__ ("nop"); __asm__ ("nop"); }
            }
}

/* Calibrated triads (a C-major chord from the hymn scale: C5 / E5 / G5, low->high
 * frequency, plus the octave C6 for the win fanfare). */
static const unsigned char arp_chord[3]   = { 109, 86, 72 };
static const unsigned char arp_fanfare[4] = { 109, 86, 72, 54 };

/* ---- 1-bit speaker effects (spk_*) -------------------------------------- *
 * These are the fallback voices.  The public sfx_* below dispatch to the
 * Mockingboard (mb_sfx_*) when a card is present, else to these. */

/* Dice rattle: two LFSR noise bursts that climb and settle, like tumbling dice. */
static void spk_roll(void)
{
    noise(36, 0x3Fu, 460);
    rest(1200);
    noise(52, 0x2Fu, 320);
}

static void spk_move(void) { sweep(92, 74, 3); }       /* a quick rising chirp */

/* Capture: an ominous falling buzz that crashes into noise. */
static void spk_capture(void)
{
    sweep(70, 165, 3);
    noise(120, 0x3Fu, 220);
}

/* Rosette: a bright rising arpeggio that resolves to a sparkle upward. */
static void spk_rosette(void)
{
    arp(arp_chord, 3, 4, 7);
    sweep(70, 45, 6);
}

/* Score (bear-off): a rising glissando capped by a little chord sparkle. */
static void spk_score(void)
{
    sweep(95, 58, 5);
    arp(arp_chord, 3, 2, 6);
}

/* Win: the chord fanfare arpeggiated up, then a long bright held note. */
static void spk_win(void)
{
    arp(arp_fanfare, 4, 3, 8);
    rest(800);
    sweep(60, 42, 10);
}

/* ---- sound backend: Mockingboard if present, else the 1-bit speaker ------ */

/* Detect a Mockingboard once at boot; sets the dispatch path used below. */
void snd_init(void)
{
#ifdef UR_MOCKINGBOARD
    uint8_t slot = mb_detect();
    if (slot) { mb_init(slot); g_mb = 1; }
#endif
}

void snd_silence(void)
{
#ifdef UR_MOCKINGBOARD
    if (g_mb) { mb_silence(); return; }
#endif
    /* the speaker is silent unless toggled */
}

#ifdef UR_MOCKINGBOARD
#define DISPATCH(mbfn, spkfn)  do { if (g_mb) { mbfn(); return; } spkfn(); } while (0)
#else
#define DISPATCH(mbfn, spkfn)  spkfn()
#endif

void sfx_roll(void)    { DISPATCH(mb_sfx_roll,    spk_roll);    }
void sfx_move(void)    { DISPATCH(mb_sfx_move,    spk_move);    }
void sfx_capture(void) { DISPATCH(mb_sfx_capture, spk_capture); }
void sfx_rosette(void) { DISPATCH(mb_sfx_rosette, spk_rosette); }
void sfx_score(void)   { DISPATCH(mb_sfx_score,   spk_score);   }
void sfx_win(void)     { DISPATCH(mb_sfx_win,     spk_win);     }

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
 * ticks (1/2/4), with a short gap so repeated pitches articulate. */
static void spk_music_note(unsigned char midi, unsigned char eighths)
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

/* Public hymn-note entry: Mockingboard arrangement if present, else the speaker.
 * The melody loop + the skip-key poll live in main.c. */
void apple2_music_note(unsigned char midi, unsigned char eighths)
{
#ifdef UR_MOCKINGBOARD
    if (g_mb) { mb_music_note(midi, eighths); return; }
#endif
    spk_music_note(midi, eighths);
}
