/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Mockingboard (and Phasor in MB-compat mode) support for the Apple II.
 *
 * A Mockingboard in slot n is two 6522 VIAs ($Cn00 and $Cn80), each driving an
 * AY-3-8910 PSG: 3 square-wave tone voices + a noise generator + a hardware
 * volume envelope.  We bit-bang the AY through VIA port A (the data bus) and the
 * low bits of port B (the BDIR/BC1/RESET control lines).  All plain memory-mapped
 * writes — no toolchain traps.  When a card is present the title music and every
 * sound effect play through the AY; otherwise the 1-bit speaker is used (see
 * sound.c, which detects via mb_detect() and dispatches).
 */
#ifndef UR_APPLE2_MOCKINGBOARD_H
#define UR_APPLE2_MOCKINGBOARD_H

#include <stdint.h>

/* Scan the slots for a 6522; returns the slot (1..7) or 0 if no card is found. */
uint8_t mb_detect(void);
/* Set up the VIAs + reset and silence the AYs.  Call once with mb_detect()'s slot. */
void    mb_init(uint8_t slot);
/* Quiet all voices (amplitudes 0, mixer off). */
void    mb_silence(void);

/* Play one Hurrian-Hymn note (MIDI number, or MUSIC_REST) for `eighths` ticks,
 * as a melody voice + an octave-down bass voice. */
void    mb_music_note(unsigned char midi, unsigned char eighths);

/* AY-voiced sound effects (mirror the speaker set in sound.c). */
void    mb_sfx_roll(void);
void    mb_sfx_move(void);
void    mb_sfx_capture(void);
void    mb_sfx_rosette(void);
void    mb_sfx_score(void);
void    mb_sfx_win(void);

#endif /* UR_APPLE2_MOCKINGBOARD_H */
