/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * NES / Famicom sound — the 2A03 APU (registers at $4000..$4017).
 *
 * Pulse 1 ($4000-$4003) plays tones: $4000 = duty|halt|const-volume|volume,
 * $4002/$4003 = the 11-bit timer period (pitch; LOWER period = HIGHER note),
 * writing $4003 also restarts the note. The noise channel ($400C/$400E/$400F)
 * gives the dice rattle and capture buzz. We hold the length counter (length-halt
 * bit set) + constant volume, so a note sounds until we set its volume to 0.
 *
 * Durations are counted in frames via the PPU vblank flag (the game is always
 * showing the board/menu when sound plays, so the PPU is running).
 */
#include <stdint.h>
#include "sound.h"
#include "music.h"          /* the Hurrian Hymn melody data (shared) */

#define APU_P1CTL  (*(volatile unsigned char *)0x4000)
#define APU_P1SWP  (*(volatile unsigned char *)0x4001)
#define APU_P1LO   (*(volatile unsigned char *)0x4002)
#define APU_P1HI   (*(volatile unsigned char *)0x4003)
#define APU_NCTL   (*(volatile unsigned char *)0x400C)
#define APU_NLO    (*(volatile unsigned char *)0x400E)
#define APU_NHI    (*(volatile unsigned char *)0x400F)
#define APU_DMC    (*(volatile unsigned char *)0x4010)
#define APU_STATUS (*(volatile unsigned char *)0x4015)
#define APU_FRAME  (*(volatile unsigned char *)0x4017)
#define PPUSTATUS  (*(volatile unsigned char *)0x2002)

/* Pulse-1 timer periods for the melody's MIDI range 71..81 (B4..A5), NTSC:
 * period = round(1789773 / (16*freq)) - 1. (Index by midi - music_note_lo.) */
static const unsigned int hymn_period[11] = {
    225, 213, 201, 189, 179, 169, 159, 150, 142, 134, 126
};

#define EIGHTH_FRAMES 13       /* frames per eighth-note (~0.22s -> ~115 bpm) */

/* Wait one full frame using the PPU vblank flag (works with rendering on). */
static void waitframe(void)
{
    while (PPUSTATUS & 0x80) {}      /* leave any current vblank */
    while (!(PPUSTATUS & 0x80)) {}   /* wait for the next        */
}
static void waitn(unsigned char n) { while (n--) waitframe(); }

static void p1_on(unsigned int period, unsigned char vol)
{
    APU_P1CTL = (unsigned char)(0xB0 | (vol & 0x0F));         /* duty 50%, halt, const vol */
    APU_P1SWP = 0x08;                                         /* sweep off */
    APU_P1LO  = (unsigned char)(period & 0xFF);
    APU_P1HI  = (unsigned char)(((period >> 8) & 0x07) | 0xF8);
}
static void p1_off(void) { APU_P1CTL = 0x30; }               /* volume 0 */

static void noise_on(unsigned char period, unsigned char vol)
{
    APU_NCTL = (unsigned char)(0x30 | (vol & 0x0F));
    APU_NLO  = (unsigned char)(period & 0x0F);
    APU_NHI  = 0xF8;
}
static void noise_off(void) { APU_NCTL = 0x30; }

void snd_silence(void) { p1_off(); noise_off(); }

void nes_sound_init(void)
{
    APU_FRAME  = 0x40;          /* 4-step frame counter, IRQ off */
    APU_DMC    = 0x00;          /* no DMC IRQ */
    APU_STATUS = 0x0F;          /* enable pulse1 + pulse2 + triangle + noise */
    snd_silence();
}

/* sfx pulse periods: bigger = lower */
#define LOW  210
#define MID  150
#define HIGH 118
#define TOP   95

void sfx_roll(void)            /* dice rattle: noise bursts at jittering period */
{
    noise_on(7, 8);  waitn(2);
    noise_on(4, 8);  waitn(2);
    noise_on(9, 7);  waitn(2);
    noise_off();
}

void sfx_move(void) { p1_on(MID, 9); waitn(4); p1_off(); }   /* one mid blip */

void sfx_capture(void)         /* harsh low noise buzz, then a click */
{
    noise_on(13, 12); waitn(8); noise_off();
    waitn(1);
    p1_on(LOW, 9); waitn(3); p1_off();
}

void sfx_rosette(void)         /* bright two-note flourish */
{
    p1_on(HIGH, 10); waitn(4);
    p1_on(TOP, 10);  waitn(6); p1_off();
}

void sfx_score(void)           /* bear-off: two rising blips */
{
    p1_on(MID, 10);  waitn(4);
    p1_on(HIGH, 10); waitn(6); p1_off();
}

void sfx_win(void)             /* a little rising fanfare */
{
    p1_on(LOW, 11);  waitn(6);
    p1_on(MID, 11);  waitn(6);
    p1_on(TOP, 11);  waitn(12); p1_off();
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
 * ticks (1/2/4). A short note-off gap at the end articulates repeated pitches.
 * The melody loop + the input poll (to skip) live in main.c. */
void nes_music_note(unsigned char midi, unsigned char eighths)
{
    unsigned char f = (unsigned char)(eighths * EIGHTH_FRAMES);
    if (midi == MUSIC_REST) { p1_off(); waitn(f); return; }
    p1_on(hymn_period[midi - music_note_lo], 10);
    waitn((unsigned char)(f - 2));
    p1_off();
    waitn(2);
}
