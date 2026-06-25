# Game design document

How the game plays and presents, distinct from the rules ([`rules.md`](rules.md))
and the technical architecture ([`architecture.md`](architecture.md)). This is a
living document — items marked **DECIDE** are open.

## Vision

A faithful, good-looking, fun version of the ~4,600-year-old Royal Game of Ur for
8-bit machines, with the standout feature of **cross-platform online multiplayer**
over FujiNet (e.g. an Atari player vs a Coleco Adam player).

## Game modes

- **Local hot-seat (2 players)** — two humans, one machine, taking turns. Simplest;
  good first playable.
- **Single-player vs AI** — human vs the computer opponent in `src/common`.
- **Online (2 players)** — server-authoritative match over FujiNet/FGS.

**DECIDE (v1 scope):** recommended order is hot-seat → vs-AI → online. Online is the
marquee feature but depends on Phase 4; hot-seat + AI make the Atari fun first.

## Rules

The Finkel ruleset — see [`rules.md`](rules.md). Any house-rule toggles (e.g.
strict vs lenient bear-off) are **DECIDE** items; default to strict Finkel.

## Screen flow

```
Title  ->  Menu (mode select)  ->  [Lobby, if online]  ->  Game board  ->  Win screen
                ^------------------------------------------------------------/
```

## Board presentation

The 20-square board (4×3 block + bridge + 2×3 block) with rosettes marked, two
players' tracks, an off-board "start" and "home" area per player, the four dice,
and per-player score (pieces home: x/7).

Per-platform rendering (uses each machine's strengths; details in
[`architecture.md`](architecture.md)):

| Platform | Board | Pieces | Dice |
|----------|-------|--------|------|
| Atari | redefined charset / display list | player/missile graphics | charset or PMG |
| Adam | TMS9928A tiles | TMS9928A hardware sprites | tiles/sprites |
| C64 | charset/bitmap | VIC-II hardware sprites | sprites |
| Apple II | hi-res | software-blitted shapes | hi-res |

**DECIDE:** exact layout, color scheme, and piece art per platform.

## Feedback & animation

- Dice **roll** animation, **move** animation along the path, **capture** (piece
  flies back to start), **rosette** highlight (extra-roll cue).
- **Current player** indicator; **legal-move** highlighting for the rolled value;
  "no legal move / roll 0 → pass" messaging.
- **DECIDE:** animation speed / skip option.

## Input

Joystick + console/keyboard. Core action set (mapped from `plat_input_t` in
[`src/common/plat.h`](../src/common/plat.h)): move cursor among movable pieces,
**select** (roll / confirm move), **cancel**, **menu**. Same abstract scheme on
every platform; each platform layer maps it to its real controls.

## AI opponent

Lives in `src/common` (portable, deterministic). Ur has well-understood strategy
(rosette control, the safe central square, capture trades).

- **DECIDE:** approach — heuristic move scoring vs short expectimax lookahead over
  the dice distribution (1/4/6/4/1). Expectimax 1–2 plies is feasible even on a
  6502 given the tiny branching factor.
- **DECIDE:** difficulty levels (e.g. random / greedy / lookahead).

## Audio

Per-chip sound effects: roll, move, capture, rosette, win, illegal.

| Platform | Chip |
|----------|------|
| Atari | POKEY |
| Adam | SN76489 |
| C64 | SID |
| Apple II | 1-bit speaker |

**DECIDE:** title/background music (yes/no), and whether to share an abstract
"music score" format across the (very different) sound chips.

## Open decisions (summary)

- [ ] v1 mode scope and order.
- [ ] Board layout / color / art per platform.
- [ ] AI approach and difficulty tiers.
- [ ] Music: yes/no, and cross-platform representation.
- [ ] Any house-rule toggles beyond strict Finkel.
