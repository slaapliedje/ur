/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Game Boy / Game Boy Color sound — the LR35902 APU (regs at 0xFF10..0xFF26).
 *
 * Channel 1 (square) carries the melody + melodic blips; channel 4 (noise) carries
 * the dice rattle / capture buzz. Short, blocking effects fired at the same game
 * events as the other ports (same API). Timing is frame-based (polls the LY scanline
 * register), so the LCD must be ON while sound plays — which it is during the
 * menu/board (draw_board leaves it on; the title hymn runs with the menu shown).
 */
#ifndef UR_GB_SOUND_H
#define UR_GB_SOUND_H

#include "ur.h"

void gb_sound_init(void);           /* power on the APU (call once at boot) */

void sfx_roll(void);
void sfx_move(void);
void sfx_capture(void);
void sfx_rosette(void);
void sfx_score(void);
void sfx_win(void);
void sfx_for_result(const ur_move_result *r);
void snd_silence(void);

/* Play one Hurrian-Hymn note (MIDI number, or MUSIC_REST) for `eighths` ticks. */
void gb_music_note(unsigned char midi, unsigned char eighths);

#endif /* UR_GB_SOUND_H */
