/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Commodore 64 sound effects (SID 6581/8580, $D400).
 *
 * Short, blocking blips fired at discrete game events (roll/move/capture/etc.),
 * mirroring the Atari/Adam effects. No raster-interrupt player needed.
 */
#ifndef UR_C64_SOUND_H
#define UR_C64_SOUND_H

#include "ur.h"

void sfx_roll(void);
void sfx_move(void);
void sfx_capture(void);
void sfx_rosette(void);
void sfx_score(void);
void sfx_win(void);
void sfx_for_result(const ur_move_result *r);
void snd_silence(void);

#endif /* UR_C64_SOUND_H */
