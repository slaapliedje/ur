/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari-specific hardware helpers: screen colours (ANTIC/GTIA) and POKEY sound
 * effects. Kept out of main.c so the game flow stays readable. Atari-only — not
 * compiled for the other platforms.
 */
#ifndef ATARIHW_H
#define ATARIHW_H

#ifdef UR_A5200
void atari_screen_init(void);    /* 5200 only: build our own 40-col DL + screen (no OS) */
#endif
void atari_setup_colors(void);   /* set the playfield colour registers          */
void atari_quiet_sio(void);      /* SOUNDR=0: silence the OS SIO I/O "drive" drone */
void atari_setup_charset(void);  /* install a custom font: board tiles + pieces */
void atari_mode4_board(void);    /* switch the board rows to ANTIC mode 4 (colour) */
void atari_text_mode(void);      /* revert the board rows to mode-2 text (full-screen text) */
void atari_title_sky_on(void);   /* DLI: lapis gradient behind the title board band */
void atari_title_sky_off(void);  /* tear down the title DLI (restore plain background) */
void atari_board_dli_on(void);   /* DLI: in-game lapis sheen down the board field   */
void atari_board_dli_off(void);  /* tear down the board DLI (flat lapis field again) */
void atari_board_tint(unsigned char player);  /* tint the board frame to whose turn it is */

/* Player-missile graphics: round two-tone token discs (4 players, one per board
 * colour-column). Off-board trays + the move cursor stay charset (main.c). */
void atari_pmg_init(void);
void atari_pmg_tokens_clear(void);  /* clear/hide all four player strips */
void atari_pmg_token(unsigned char slot, unsigned char char_x, unsigned char char_y);
void atari_pmg_token_clear(unsigned char slot, unsigned char char_y);  /* one disc */

/* Joystick port 1 (via OS shadow registers). */
unsigned char atari_stick(void);   /* raw STICK0: bit0 up,1 down,2 left,3 right (0=pressed) */
unsigned char atari_trig(void);    /* 1 if the trigger is pressed */

void atari_wait_frames(unsigned char frames);  /* busy-wait ~frames/60 s (WSYNC) */

void sfx_roll(void);
void sfx_move(void);
void sfx_capture(void);
void sfx_rosette(void);
void sfx_score(void);
void sfx_win(void);

/* Hurrian-Hymn 3-voice POKEY player: start playback, advance one frame (call once
 * per display frame from a wait loop — non-blocking), and stop/silence. */
void music_start(void);
void music_tick(void);
void music_stop(void);

#endif /* ATARIHW_H */
