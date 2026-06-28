/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Sega Master System sound effects (TI SN76489 PSG, write-only I/O port 0x7F).
 *
 * The SMS PSG is the same chip family as the Adam/ColecoVision, so this mirrors
 * src/adam/sound.c almost exactly — only the I/O port differs (0x7F vs 0xFF).
 * Short, blocking blips fired at discrete game events; no frame interrupt needed.
 */
#ifndef UR_SMS_SOUND_H
#define UR_SMS_SOUND_H

#include "ur.h"

void sfx_roll(void);                          /* dice rattle              */
void sfx_move(void);                          /* plain move click         */
void sfx_capture(void);                       /* opponent piece sent home */
void sfx_rosette(void);                       /* landed on a rosette      */
void sfx_score(void);                         /* a piece borne off        */
void sfx_win(void);                           /* game won                 */
void sfx_for_result(const ur_move_result *r); /* pick by move result      */
void snd_silence(void);                       /* all channels off         */

/* Play one Hurrian-Hymn note (MIDI number, or MUSIC_REST) for `eighths` ticks. */
void sms_music_note(unsigned char midi, unsigned char eighths);

#endif /* UR_SMS_SOUND_H */
