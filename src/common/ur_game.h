/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ur_game.h — the shared local-game controller.
 *
 * One implementation of the Ur turn loop (roll → choose → apply → capture/rosette/
 * win → advance), reused by every platform so each port no longer re-implements
 * human_turn / computer_turn / play_local. The controller drives the flow through
 * the small platform interface in plat.h; the port supplies rendering, input,
 * sound, and RNG entropy.
 *
 * The port's main() typically: show its title/menu, call ur_run_game(vs_ai), then
 * present the result (the returned winner) and loop. Toolchain-neutral.
 */
#ifndef UR_GAME_H
#define UR_GAME_H

#include <stdint.h>
#include "ur.h"
#include "plat.h"

/* The active game state, OWNED by the controller. Platform draw/choose routines
 * read it (instead of each port keeping its own `ur_state game`). */
extern ur_state ur_g;

/* Play one local game to completion. vs_ai = 0 → hot-seat (two humans);
 * vs_ai = 1 → you are Light, the computer plays Dark. Returns the winning player
 * (0 = Light, 1 = Dark). Seeds the RNG from plat_seed() on the first call. */
uint8_t ur_run_game(uint8_t vs_ai);

#endif /* UR_GAME_H */
