/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * plat.h — the platform interface for Ur's shared game controller.
 *
 * The portable controller (src/common/ur_game.c) CALLS these; each platform layer
 * (src/atari, src/nes, src/gb, …) IMPLEMENTS them. The controller owns the game
 * flow (the turn loop, rolls, AI, win); the platform owns rendering, input, sound,
 * and its RNG entropy source. The core never includes platform headers and never
 * touches hardware — this header is the entire contract between the two sides.
 *
 * Must stay TOOLCHAIN-NEUTRAL: it compiles under cc65 (6502) and z88dk/SDCC (Z80 /
 * gbz80). Use only portable standard C here.
 *
 * (Networking is NOT part of this contract: the FujiNet ports drive fujinet-lib
 * directly in their own online loop — see src/net. This interface covers the
 * shared LOCAL game only.)
 */
#ifndef UR_PLAT_H
#define UR_PLAT_H

#include <stdint.h>
#include "ur.h"

#define UR_NO_ROLL 0xFFu      /* "no current roll" sentinel for plat_draw()/choose */

/* Draw the board + HUD for the active game (ur_g). `roll` is 0..4 or UR_NO_ROLL;
 * `msg` is a short status line (may be NULL). */
void     plat_draw(uint8_t roll, const char *msg);

/* Block until the player presses a confirm/continue button (one tap). */
void     plat_wait(void);

/* With the board already drawn for `roll`, render the legal-move list and let the
 * player pick one. Returns the chosen piece index (0..UR_PIECES-1), or -1 if there
 * is no legal move. */
int8_t   plat_choose_move(uint8_t player, uint8_t roll);

/* Animate the moving piece sliding from path position `from` to `to` (the move has
 * just been applied to ur_g; the board still shows the pre-move frame). Ports
 * without token animation provide an empty stub. */
void     plat_animate(uint8_t player, uint8_t from, uint8_t to);

/* The dice were just rolled (value = `roll`): play the roll sound and any dice
 * animation (e.g. a tumble settling on `roll`). Ports without a dice animation just
 * play the sound. */
void     plat_roll(uint8_t roll);
/* Sound for the result of an applied move (move/capture/rosette/score/win). */
void     plat_sfx_result(const ur_move_result *res);

/* Entropy to seed the dice RNG — a hardware source (e.g. POKEY/DIV/V-counter) or an
 * input-timing accumulator. Called once, at the first game. */
uint16_t plat_seed(void);

/* Let the player choose the AI difficulty for a vs-computer game. Return one of
 * UR_AI_EASY / UR_AI_NORMAL / UR_AI_HARD. Called by the controller at the start of
 * each vs-AI game (not for hot-seat). */
uint8_t  plat_pick_level(void);

#endif /* UR_PLAT_H */
