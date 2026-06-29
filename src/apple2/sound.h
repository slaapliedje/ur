/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Apple II sound effects (the 1-bit speaker at $C030).
 *
 * The Apple II has no sound chip: the speaker cone moves once each time $C030 is
 * touched, so a tone is a square wave you generate by toggling it at the right
 * rate in a cycle-counted loop.  Short, blocking blips fired at the same game
 * events as the Atari/Adam/C64 builds (same API), so main.c calls them the same way.
 */
#ifndef UR_APPLE2_SOUND_H
#define UR_APPLE2_SOUND_H

#include "ur.h"

/* Detect the sound backend once at boot (a Mockingboard if present, else the
 * 1-bit speaker). Call before any other sound function. */
void snd_init(void);

void sfx_roll(void);
void sfx_move(void);
void sfx_capture(void);
void sfx_rosette(void);
void sfx_score(void);
void sfx_win(void);
void sfx_for_result(const ur_move_result *r);
void snd_silence(void);

/* Play one Hurrian-Hymn note (MIDI number, or MUSIC_REST) for `eighths` ticks. */
void apple2_music_note(unsigned char midi, unsigned char eighths);

#endif /* UR_APPLE2_SOUND_H */
