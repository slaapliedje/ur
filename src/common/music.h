/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Portable game music — the Hurrian Hymn (Hurrian cult hymn h.6, "a zaluzi to
 * the gods"), inscribed on a clay tablet at Ugarit c.1400 BCE: the oldest
 * substantially complete piece of NOTATED music known. A fitting title theme for
 * a game whose board dates to c.2600 BCE Mesopotamia — older still than the tune,
 * but kin to it. (Melody after the popular Dumbrill/Levy reconstruction; the
 * note table and sources are in music.c.)
 *
 * This is platform-neutral DATA, not a player: an ordered list of (note,duration)
 * steps. `note` is an absolute MIDI note number (60 = middle C); MUSIC_REST marks
 * a silent step. `dur` is in melody ticks (an eighth-note = MUSIC_EIGHTH). Each
 * platform owns a tiny play_hymn() that maps the MIDI number to its sound chip's
 * register (POKEY divisor / SID Fn / SN76489 period / speaker half-period), sets
 * the tempo, and polls input BETWEEN notes so any keypress skips the tune — no
 * input is ever trapped. Keep this file toolchain-neutral (cc65 + z88dk).
 */
#ifndef UR_MUSIC_H
#define UR_MUSIC_H

#include <stdint.h>

#define MUSIC_REST   0xFFu     /* a silent step (note value)             */

/* Duration units (melody ticks). The hymn is notated in even values; we keep a
 * simple eighth/quarter/half vocabulary so the tempo is one per-platform scalar. */
#define MUSIC_EIGHTH 1u
#define MUSIC_QUARTER 2u
#define MUSIC_HALF   4u

typedef struct {
    uint8_t note;             /* absolute MIDI note number, or MUSIC_REST */
    uint8_t dur;              /* duration in eighth-note ticks            */
} music_step;

/* Lowest/highest MIDI note the melody touches — platforms size their pitch
 * lookup tables to [MUSIC_NOTE_LO .. MUSIC_NOTE_HI]. (Filled in music.c.) */
extern const uint8_t  music_note_lo;
extern const uint8_t  music_note_hi;

extern const music_step ur_hymn[];   /* the melody                        */
extern const uint16_t   ur_hymn_len; /* number of steps in ur_hymn[]      */

#endif /* UR_MUSIC_H */
