/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The Hurrian Hymn (h.6) as portable melody data — see music.h.
 *
 * Transcribed from the popular Richard Dumbrill-based performable score (the
 * interpretation Michael Levy's well-known lyre recording uses; arr. Clint Goss,
 * flutopedia.com/song_hurrian_hymn.htm). The piece is a single diatonic melodic
 * line on the seven naturals (B C D E F G A), notated ♩=120; it cadences on B,
 * which reads as the tonic. The recognizable material is the descending-tetrachord
 * theme (F5 E5 D5 C5 B4) and its rising B4->D5 answer, which recur throughout.
 *
 * What follows states that theme and answer (high-confidence, straight from the
 * score) and then a short rising line + cadence ARRANGED to round the tune into an
 * ~11-second loop for a title screen. Pitches are absolute MIDI numbers; B4 = 71.
 *   B4=71  C5=72  D5=74  E5=76  F5=77  G5=79  A5=81
 * Durations are in eighth-note ticks (MUSIC_EIGHTH/QUARTER/HALF); each platform
 * scales them to a tempo and maps the MIDI number to its sound chip.
 */
#include "music.h"

const uint8_t music_note_lo = 71;   /* B4 — lowest pitch the melody touches */
const uint8_t music_note_hi = 81;   /* A5 — highest                          */

const music_step ur_hymn[] = {
    /* Phrase 1 — the descending-tetrachord theme (the recognizable opening) */
    {77, MUSIC_EIGHTH}, {76, MUSIC_EIGHTH}, {74, MUSIC_EIGHTH}, {72, MUSIC_EIGHTH},
    {71, MUSIC_HALF},
    /* Phrase 2 — the rising B answer */
    {71, MUSIC_EIGHTH}, {MUSIC_REST, MUSIC_EIGHTH}, {71, MUSIC_EIGHTH},
    {72, MUSIC_EIGHTH}, {74, MUSIC_QUARTER}, {MUSIC_REST, MUSIC_EIGHTH},
    /* Phrase 3 — theme restated */
    {77, MUSIC_EIGHTH}, {76, MUSIC_EIGHTH}, {74, MUSIC_EIGHTH}, {72, MUSIC_EIGHTH},
    {71, MUSIC_HALF},
    /* Phrase 4 — the answer, opened a step higher */
    {71, MUSIC_EIGHTH}, {MUSIC_REST, MUSIC_EIGHTH}, {72, MUSIC_EIGHTH},
    {74, MUSIC_EIGHTH}, {76, MUSIC_QUARTER}, {MUSIC_REST, MUSIC_EIGHTH},
    /* Phrase 5 — a rising line (arranged) climbing to the top of the scale */
    {71, MUSIC_EIGHTH}, {72, MUSIC_EIGHTH}, {74, MUSIC_EIGHTH}, {76, MUSIC_EIGHTH},
    {77, MUSIC_QUARTER}, {79, MUSIC_QUARTER},
    /* Phrase 6 — a gentle descent (the tamed ornamental figuration) */
    {81, MUSIC_EIGHTH}, {79, MUSIC_EIGHTH}, {77, MUSIC_EIGHTH}, {76, MUSIC_EIGHTH},
    {74, MUSIC_EIGHTH}, {72, MUSIC_EIGHTH},
    /* Phrase 7 — cadence home to the tonic */
    {74, MUSIC_EIGHTH}, {72, MUSIC_EIGHTH}, {71, MUSIC_EIGHTH}, {71, MUSIC_HALF},
};

/* 38 steps — must match ur_hymn[] above. (A literal, not sizeof/N: the z88dk
 * classic compiler sccz80 rejects sizeof in a file-scope const initializer.) */
const uint16_t ur_hymn_len = 38u;
