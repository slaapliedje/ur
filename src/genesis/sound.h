/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Genesis PSG sound — see sound.c. Include after <genesis.h> (uses u8/u16). */
#ifndef GEN_SOUND_H
#define GEN_SOUND_H

#include <stdint.h>
#include "ur.h"             /* ur_move_result */

void snd_silence(void);
void sfx_roll(void);
void sfx_for_result(const ur_move_result *r);

/* Play one hymn step; returns the buttons pressed during it (0 = none), so the
 * caller can skip the tune. Polls the pad every frame. */
u16  gen_music_note(uint8_t midi, uint8_t eighths);

#endif /* GEN_SOUND_H */
