/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari-specific hardware helpers: screen colours (ANTIC/GTIA) and POKEY sound
 * effects. Kept out of main.c so the game flow stays readable. Atari-only — not
 * compiled for the other platforms.
 */
#ifndef ATARIHW_H
#define ATARIHW_H

void atari_setup_colors(void);   /* set GR.0 background / text / border colours */
void atari_setup_charset(void);  /* install a custom font: board tiles + pieces */

/* Player-missile graphics: a highlight box (player 0) over a board cell. */
void atari_pmg_init(void);
void atari_pmg_highlight(unsigned char char_x, unsigned char char_y);
void atari_pmg_hide(void);

/* Joystick port 1 (via OS shadow registers). */
unsigned char atari_stick(void);   /* raw STICK0: bit0 up,1 down,2 left,3 right (0=pressed) */
unsigned char atari_trig(void);    /* 1 if the trigger is pressed */

void sfx_roll(void);
void sfx_move(void);
void sfx_capture(void);
void sfx_rosette(void);
void sfx_score(void);
void sfx_win(void);

#endif /* ATARIHW_H */
