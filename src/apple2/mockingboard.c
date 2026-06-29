/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Mockingboard / AY-3-8910 driver (see mockingboard.h).
 *
 * Card in slot n: 6522 VIA #1 at $Cn00, VIA #2 at $Cn80.  VIA registers used:
 *   +0 ORB  (port B = AY control: PB0 BC1, PB1 BDIR, PB2 /RESET)
 *   +1 ORA  (port A = AY data bus)
 *   +2 DDRB (port B direction)   +3 DDRA (port A direction)
 * AY command on port B:  $00 reset, $04 idle, $07 latch register#, $06 write data.
 * We drive BOTH AYs with identical data so sound is centred and audible regardless
 * of how the stereo channels are wired/routed.
 *
 * AY register map: R0/1,R2/3,R4/5 = tone period (fine,coarse) for voices A/B/C;
 * R6 = noise period; R7 = mixer (bit set = OFF; b0-2 tone A/B/C, b3-5 noise A/B/C);
 * R8/9/10 = voice A/B/C amplitude (bit4 = follow envelope); R11/12 = envelope
 * period; R13 = envelope shape (writing it retriggers the envelope).
 */
#include <stdint.h>
#include "mockingboard.h"
#include "music.h"          /* music_note_lo / MUSIC_REST */

static volatile uint8_t *g_via1;   /* VIA #1 / AY #1 ($Cn00) */
static volatile uint8_t *g_via2;   /* VIA #2 / AY #2 ($Cn80) */

/* AY tone-period table for the hymn range B4(71)..A5(81), indexed by midi-71.
 * period = AY_clock / (16*freq), AY_clock ~= 1.0205 MHz on the Mockingboard. */
static const uint8_t mb_period[11] = {
    129, 122, 115, 109, 103, 97, 91, 86, 81, 77, 72   /* B4 C5 .. A5 */
};

/* ---- low-level AY access ----------------------------------------------- */

/* Write `val` to AY register `reg` on one VIA (the BDIR/BC1 latch-then-write dance;
 * consecutive 6522 stores give the AY enough setup time). */
static void wr(volatile uint8_t *via, uint8_t reg, uint8_t val)
{
    via[1] = reg;       /* ORA = register number   */
    via[0] = 0x07;      /* latch address (BDIR,BC1) */
    via[0] = 0x04;      /* idle                     */
    via[1] = val;       /* ORA = data               */
    via[0] = 0x06;      /* write data (BDIR)        */
    via[0] = 0x04;      /* idle                     */
}

/* Mirror a register write to both AYs. */
static void mb_reg(uint8_t reg, uint8_t val)
{
    wr(g_via1, reg, val);
    wr(g_via2, reg, val);
}

/* Cycle-counted busy wait (~18 cyc/iter at ~1 MHz). */
static void busy(uint16_t n) { while (n--) __asm__ ("nop"); }
#define EIGHTH 9000u             /* one eighth-note tick (tune for tempo) */

static void set_tone(uint8_t ch, uint16_t per)   /* ch 0=A 1=B 2=C */
{
    uint8_t r = (uint8_t)(ch << 1);
    mb_reg(r,            (uint8_t)(per & 0xFF));
    mb_reg((uint8_t)(r+1), (uint8_t)((per >> 8) & 0x0F));
}

/* ---- detection + init -------------------------------------------------- */

/* Probe one slot for a 6522.  We only ever touch slots 4 and 5 (where Mockingboards
 * actually live) — NEVER the storage/firmware slots: poking the disk controller in
 * slot 6 or a SmartPort in slot 7 would wreck a live ProDOS.
 *
 * DDRA (register 3) is a pure read/write latch on a 6522.  We write a pattern, read a
 * DIFFERENT register (which changes what's on the data bus — this defeats an
 * emulator's / the floating bus's tendency to "hold" the last value, which would make
 * a naive write-then-read echo pass on an empty slot), then read DDRA back: a real
 * 6522 returns the pattern it latched; an empty slot does not.  Two distinct patterns
 * make a false positive vanishingly unlikely.  (mb_init resets DDRA afterward.) */
static uint8_t probe(uint8_t slot)
{
    volatile uint8_t *p = (volatile uint8_t *)(0xC000u + (uint16_t)slot * 0x100u);
    uint8_t junk, v1, v2;
    p[3] = 0x5A; junk = p[1]; v1 = p[3];   /* latch $5A, scrub the bus, read back */
    p[3] = 0xA5; junk = p[1]; v2 = p[3];   /* latch $A5, scrub the bus, read back */
    (void)junk;
    return (uint8_t)(v1 == 0x5A && v2 == 0xA5);
}

uint8_t mb_detect(void)
{
    if (probe(4)) return 4;
    if (probe(5)) return 5;
    return 0;
}

void mb_init(uint8_t slot)
{
    g_via1 = (volatile uint8_t *)(0xC000u + (uint16_t)slot * 0x100u);
    g_via2 = g_via1 + 0x80;
    /* port B low bits = control outputs; port A = data outputs */
    g_via1[2] = 0x07; g_via1[3] = 0xFF;
    g_via2[2] = 0x07; g_via2[3] = 0xFF;
    /* reset both AYs (RESET low, then idle) */
    g_via1[0] = 0x00; g_via1[0] = 0x04;
    g_via2[0] = 0x00; g_via2[0] = 0x04;
    mb_silence();
}

void mb_silence(void)
{
    mb_reg(7, 0x3F);    /* all tone + noise channels OFF */
    mb_reg(8, 0);       /* amplitudes 0 */
    mb_reg(9, 0);
    mb_reg(10, 0);
}

/* ---- title music: melody + octave-down bass ---------------------------- */

void mb_music_note(unsigned char midi, unsigned char eighths)
{
    unsigned char e;

    if (midi == MUSIC_REST) {
        mb_reg(8, 0); mb_reg(10, 0);
    } else {
        uint8_t  idx = (uint8_t)(midi - music_note_lo);
        uint16_t per, per2;
        if (idx > 10) idx = 10;
        per  = mb_period[idx];
        per2 = (uint16_t)(per << 1);      /* an octave lower for the bass */
        set_tone(0, per);                 /* voice A: melody */
        set_tone(2, per2);                /* voice C: bass   */
        mb_reg(7, 0x3A);                  /* tone A + C on, no noise */
        mb_reg(8, 13);                    /* melody amplitude */
        mb_reg(10, 9);                    /* bass amplitude   */
    }
    for (e = 0; e < eighths; e++) busy(EIGHTH);
    mb_reg(8, 0); mb_reg(10, 0);          /* brief note-off so repeats articulate */
    busy(900);
}

/* ---- sound effects ----------------------------------------------------- */

void mb_sfx_roll(void)        /* dice rattle: two decaying noise bursts */
{
    mb_reg(7, 0x37);          /* noise on A only */
    mb_reg(11, 0x60); mb_reg(12, 0x00);  /* envelope period (~0.4s decay) */
    mb_reg(6, 15);            /* noise pitch */
    mb_reg(8, 0x10);          /* voice A follows the envelope */
    mb_reg(13, 0x00);         /* shape \___ : one-shot decay (retrigger) */
    busy(7000);
    mb_reg(6, 21);
    mb_reg(13, 0x00);
    busy(7000);
    mb_silence();
}

void mb_sfx_move(void)        /* a quick rising two-tone chirp */
{
    mb_reg(7, 0x3E);          /* tone A only */
    set_tone(0, 145); mb_reg(8, 13); busy(2600);
    set_tone(0, 109);                 busy(2600);
    mb_silence();
}

void mb_sfx_capture(void)     /* harsh: a falling tone with a noise edge */
{
    mb_reg(7, 0x36);          /* tone A + noise A */
    mb_reg(6, 18);
    set_tone(0, 300); mb_reg(8, 14); busy(3200);
    set_tone(0, 420);                 busy(3200);
    set_tone(0, 560);                 busy(3200);
    mb_silence();
}

void mb_sfx_rosette(void)     /* a bright C-major chord, arpeggiated in */
{
    mb_reg(7, 0x38);          /* all three tones on */
    set_tone(0, 122); mb_reg(8, 13); busy(2400);   /* C5 */
    set_tone(1,  97); mb_reg(9, 13); busy(2400);   /* E5 */
    set_tone(2,  81); mb_reg(10,13); busy(5000);   /* G5 — full chord rings */
    mb_silence();
}

void mb_sfx_score(void)       /* bear-off: two rising tones, then a chord */
{
    mb_reg(7, 0x38);
    set_tone(0, 145); mb_reg(8, 13); busy(2600);   /* A4 */
    set_tone(0, 109);                 busy(2600);   /* D5 */
    set_tone(0, 122); set_tone(1, 97); set_tone(2, 81);
    mb_reg(9, 11); mb_reg(10, 11);    busy(4500);   /* C-E-G */
    mb_silence();
}

void mb_sfx_win(void)         /* fanfare: arpeggio up, then the chord decays */
{
    mb_reg(7, 0x38);
    mb_reg(8, 13);
    set_tone(0, 122); busy(2400);     /* C5 */
    set_tone(0,  97); busy(2400);     /* E5 */
    set_tone(0,  81); busy(2400);     /* G5 */
    set_tone(0,  61); busy(2800);     /* C6 */
    /* hold the full chord and let the envelope ring it out */
    set_tone(0, 122); set_tone(1, 97); set_tone(2, 81);
    mb_reg(11, 0x00); mb_reg(12, 0x01);    /* longer envelope (~0.8s) */
    mb_reg(8, 0x10); mb_reg(9, 0x10); mb_reg(10, 0x10);
    mb_reg(13, 0x00);                 /* trigger the decay */
    busy(22000);
    mb_silence();
}
