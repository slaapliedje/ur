/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * plat.h — the platform interface for Ur.
 *
 * The portable core (src/common) CALLS these functions; each platform layer
 * (src/atari, src/adam, src/c64, src/apple2) IMPLEMENTS them. The core never
 * includes platform headers and never touches hardware — this header is the
 * entire contract between the two sides. The core calls the platform, never the
 * reverse.
 *
 * Must stay TOOLCHAIN-NEUTRAL: this compiles under cc65 (6502: Atari/C64/Apple II)
 * and z88dk/SDCC (Z80: Coleco Adam). Use only portable standard C here.
 *
 * NOTE: this is an initial sketch of the interface. Refine the signatures as the
 * core takes shape (see docs/architecture.md). Keep it small and stable.
 */
#ifndef UR_PLAT_H
#define UR_PLAT_H

#include <stdint.h>

/* Opaque game state, defined by the core. The platform receives the pointer and
 * passes it to core read-only helpers for rendering; it does not poke inside. */
struct game_state;

/* ---- Lifecycle ---------------------------------------------------------- */
void     plat_init(void);          /* set up display, input, sound, RNG */
void     plat_shutdown(void);

/* ---- Timing / RNG ------------------------------------------------------- */
void     plat_wait_frame(void);    /* block until the next display frame (VBI) */
uint16_t plat_rng_seed(void);      /* hardware entropy to seed the core's RNG */

/* ---- Display ------------------------------------------------------------ */
void     plat_draw_board(void);                        /* static board, drawn once */
void     plat_draw_state(const struct game_state *gs); /* pieces, dice, scores */
void     plat_present(void);                           /* flush back buffer, if any */

/* ---- Input -------------------------------------------------------------- */
typedef enum {
    PLAT_INPUT_NONE = 0,
    PLAT_INPUT_LEFT,
    PLAT_INPUT_RIGHT,
    PLAT_INPUT_UP,
    PLAT_INPUT_DOWN,
    PLAT_INPUT_SELECT,   /* roll dice / confirm the highlighted move */
    PLAT_INPUT_CANCEL,
    PLAT_INPUT_MENU
} plat_input_t;

plat_input_t plat_input_poll(void);  /* non-blocking; returns an abstract action */

/* ---- Sound -------------------------------------------------------------- */
typedef enum {
    PLAT_SFX_ROLL = 0,
    PLAT_SFX_MOVE,
    PLAT_SFX_CAPTURE,
    PLAT_SFX_ROSETTE,
    PLAT_SFX_WIN,
    PLAT_SFX_ILLEGAL
} plat_sfx_t;

void     plat_sound_play(plat_sfx_t sfx);

/* ---- Networking --------------------------------------------------------- */
/* Thin shim over fujinet-lib's N: device (see src/net). Connections use a
 * devicespec such as "N:TCP://host:port/". Functions return < 0 on error. */
int8_t   plat_net_open(const char *devicespec);
int16_t  plat_net_read(uint8_t *buf, uint16_t len);   /* bytes read, or < 0 */
int16_t  plat_net_write(const uint8_t *buf, uint16_t len);
int8_t   plat_net_status(uint16_t *bytes_waiting);    /* 0 ok; sets *bytes_waiting */
void     plat_net_close(void);

#endif /* UR_PLAT_H */
