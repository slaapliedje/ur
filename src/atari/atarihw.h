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

void sfx_roll(void);
void sfx_move(void);
void sfx_capture(void);
void sfx_rosette(void);
void sfx_score(void);
void sfx_win(void);

#endif /* ATARIHW_H */
