/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Coleco Adam sound effects (TI SN76489 PSG, I/O port 0xFF).
 *
 * Short, blocking blips fired at discrete game events (roll/move/capture/etc.),
 * mirroring the Atari's POKEY effects. No frame interrupt needed.
 */
#ifndef UR_ADAM_SOUND_H
#define UR_ADAM_SOUND_H

#include "ur.h"

void sfx_roll(void);                          /* dice rattle            */
void sfx_move(void);                          /* plain move click       */
void sfx_capture(void);                       /* opponent piece sent home */
void sfx_rosette(void);                       /* landed on a rosette    */
void sfx_score(void);                         /* a piece borne off      */
void sfx_win(void);                           /* game won               */
void sfx_for_result(const ur_move_result *r); /* pick by move result    */
void snd_silence(void);                       /* all channels off       */

/* Play one Hurrian-Hymn note (MIDI number, or MUSIC_REST) for `eighths` ticks. */
void adam_music_note(unsigned char midi, unsigned char eighths);

#endif /* UR_ADAM_SOUND_H */
