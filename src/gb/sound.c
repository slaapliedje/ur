/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Game Boy / Game Boy Color sound — the LR35902 APU.
 *
 * Channel 1 ($FF10-$FF14) is a square tone: NR13/NR14 hold the 11-bit frequency
 * value x (pitch f = 131072 / (2048 - x) Hz, so HIGHER x = HIGHER note), NR12 the
 * volume, and writing NR14 bit7 retriggers the note. Channel 4 ($FF20-$FF23) is the
 * noise generator for the dice rattle / capture buzz. We use constant volume (no
 * envelope) and silence a channel by turning its DAC off (NR12/NR42 = 0) + retrigger.
 *
 * Durations are counted in frames by polling the LY scanline register ($FF44) for
 * the start of vblank (LY == 144); the LCD is always on when sound plays.
 */
#include <stdint.h>
#include "sound.h"
#include "music.h"          /* the Hurrian Hymn melody data (shared) */

#define rNR10 (*(volatile uint8_t *)0xFF10)
#define rNR11 (*(volatile uint8_t *)0xFF11)
#define rNR12 (*(volatile uint8_t *)0xFF12)
#define rNR13 (*(volatile uint8_t *)0xFF13)
#define rNR14 (*(volatile uint8_t *)0xFF14)
#define rNR41 (*(volatile uint8_t *)0xFF20)
#define rNR42 (*(volatile uint8_t *)0xFF21)
#define rNR43 (*(volatile uint8_t *)0xFF22)
#define rNR44 (*(volatile uint8_t *)0xFF23)
#define rNR50 (*(volatile uint8_t *)0xFF24)
#define rNR51 (*(volatile uint8_t *)0xFF25)
#define rNR52 (*(volatile uint8_t *)0xFF26)
#define rLY   (*(volatile uint8_t *)0xFF44)

/* Channel-1 frequency value x for the melody's MIDI range 71..81 (B4..A5):
 * x = 2048 - round(131072 / freq). (Index by midi - music_note_lo.) */
static const uint16_t hymn_x[11] = {
    1783, 1798, 1812, 1825, 1837, 1849, 1860, 1871, 1881, 1890, 1899
};

#define EIGHTH_FRAMES 13       /* frames per eighth-note (~0.22s at ~60fps) */

/* sfx square pitches (frequency value x; bigger = higher) */
#define LOW  1650
#define MID  1850
#define HIGH 1950
#define TOP  1990

static void waitframe(void)
{
    while (rLY >= 144) {}      /* leave any current vblank */
    while (rLY < 144)  {}      /* wait for the next        */
}
static void waitn(uint8_t n) { while (n--) waitframe(); }

static void ch1_on(uint16_t x, uint8_t vol)
{
    rNR10 = 0x00;                              /* no sweep        */
    rNR11 = 0x80;                              /* duty 50%        */
    rNR12 = (uint8_t)(vol << 4);               /* constant volume */
    rNR13 = (uint8_t)(x & 0xFF);
    rNR14 = (uint8_t)(0x80 | ((x >> 8) & 0x07));  /* trigger */
}
static void ch1_off(void) { rNR12 = 0x00; rNR14 = 0x80; }   /* DAC off + retrigger */

static void ch4_on(uint8_t poly, uint8_t vol)
{
    rNR41 = 0x00;
    rNR42 = (uint8_t)(vol << 4);
    rNR43 = poly;
    rNR44 = 0x80;                              /* trigger */
}
static void ch4_off(void) { rNR42 = 0x00; rNR44 = 0x80; }

void snd_silence(void) { ch1_off(); ch4_off(); }

void gb_sound_init(void)
{
    rNR52 = 0x80;              /* APU power on  */
    rNR50 = 0x77;              /* max volume L+R */
    rNR51 = 0xFF;              /* all channels to both speakers */
    snd_silence();
}

void sfx_roll(void)           /* dice rattle: noise bursts at jittering pitch */
{
    ch4_on(0x44, 10); waitn(2);
    ch4_on(0x36, 10); waitn(2);
    ch4_on(0x52, 9);  waitn(2);
    ch4_off();
}

void sfx_move(void) { ch1_on(MID, 12); waitn(4); ch1_off(); }

void sfx_capture(void)        /* low noise buzz, then a click */
{
    ch4_on(0x58, 12); waitn(8); ch4_off();
    waitn(1);
    ch1_on(LOW, 12);  waitn(3); ch1_off();
}

void sfx_rosette(void)        /* bright two-note flourish */
{
    ch1_on(HIGH, 12); waitn(4);
    ch1_on(TOP, 12);  waitn(6); ch1_off();
}

void sfx_score(void)          /* bear-off: two rising blips */
{
    ch1_on(MID, 12);  waitn(4);
    ch1_on(HIGH, 12); waitn(6); ch1_off();
}

void sfx_win(void)            /* a little rising fanfare */
{
    ch1_on(LOW, 13); waitn(6);
    ch1_on(MID, 13); waitn(6);
    ch1_on(TOP, 13); waitn(12); ch1_off();
}

void sfx_for_result(const ur_move_result *r)
{
    if (r->won)           sfx_win();
    else if (r->captured) sfx_capture();
    else if (r->scored)   sfx_score();
    else if (r->rosette)  sfx_rosette();
    else                  sfx_move();
}

/* Play one melody note (MIDI number, or MUSIC_REST) for `eighths` eighth-note
 * ticks (1/2/4). A short note-off gap articulates repeated pitches. The melody
 * loop + the skip poll live in main.c. */
void gb_music_note(unsigned char midi, unsigned char eighths)
{
    uint8_t f = (uint8_t)(eighths * EIGHTH_FRAMES);
    if (midi == MUSIC_REST) { ch1_off(); waitn(f); return; }
    ch1_on(hymn_x[midi - music_note_lo], 13);
    waitn((uint8_t)(f - 2));
    ch1_off();
    waitn(2);
}
