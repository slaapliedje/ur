/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * NES / Famicom sound — the 2A03 APU.
 *
 * Pulse channel 1 carries melodic blips + the Hurrian-Hymn title theme; the noise
 * channel carries the dice rattle / capture buzz. Short, blocking effects fired at
 * the same game events as the other ports (same API), so main.c calls them the same
 * way. Timing is frame-based (polls the PPU vblank flag), so the menu/board must be
 * displayed (rendering on) while sound plays — which it always is.
 */
#ifndef UR_NES_SOUND_H
#define UR_NES_SOUND_H

#include "ur.h"

void nes_sound_init(void);          /* enable APU channels (call once at boot) */

void sfx_roll(void);
void sfx_move(void);
void sfx_capture(void);
void sfx_rosette(void);
void sfx_score(void);
void sfx_win(void);
void sfx_for_result(const ur_move_result *r);
void snd_silence(void);

/* Play one Hurrian-Hymn note (MIDI number, or MUSIC_REST) for `eighths` ticks. */
void nes_music_note(unsigned char midi, unsigned char eighths);

#endif /* UR_NES_SOUND_H */
