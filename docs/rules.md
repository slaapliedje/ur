# Rules of the Royal Game of Ur (Finkel ruleset)

> **Status.** The standard Finkel layout below is now **encoded and tested** in
> [`src/common/ur.c`](../src/common/ur.c) (see [`tests/test_ur.c`](../tests/test_ur.c)):
> a 14-step path, rosettes at steps 4/8/14, shared middle = steps 5–12, exact
> bear-off at 15. Verified against:
> - https://royalur.net/rules
> - https://en.wikipedia.org/wiki/Royal_Game_of_Ur
> - Irving Finkel / British Museum materials

The **Finkel ruleset** — reconstructed by Dr. Irving Finkel of the British Museum
from a cuneiform tablet — is the de-facto standard for modern play. It is the
ruleset this game implements.

## Equipment

- A **20-square board**: 3 rows of 8 with 4 squares cut away — a **4×3 block**
  joined to a **2×3 block** by a **2-square bridge**.
- **7 pieces** per player.
- **4 tetrahedral (4-sided) dice**, each with **2 of its 4 corners marked**.

## The board and the path

The board has three rows:

- **Top row** — Player 1's private squares (columns 1–4 and 7–8; columns 5–6 absent).
- **Middle row** — the **shared** row, all 8 columns present. This is the only place
  pieces can capture each other.
- **Bottom row** — Player 2's private squares (mirror of the top row).

Each player moves all 7 pieces along a **14-square path**:

1. **Private entry** — 4 squares in the player's own outer block (off-board → onto
   the board), heading toward the bridge.
2. **Shared middle** — all 8 squares of the central row.
3. **Private exit** — 2 squares in the player's own block, then **off** the board.

The two players' paths overlap only on the shared middle row; their entry and exit
sections are private and safe.

## Dice and movement

- Roll all four dice. Your move count = the number of **marked corners** showing =
  **0, 1, 2, 3, or 4**.
- Because each die is 50/50, the distribution over 16 equally-likely outcomes is:

  | Roll | 0 | 1 | 2 | 3 | 4 |
  |------|---|---|---|---|---|
  | Ways |1/16|4/16|6/16|4/16|1/16|

- A roll of **0** = no move; turn passes.
- You move **one** piece forward by the rolled amount (onto an empty square, a
  capture, a rosette, or off the board). If no legal move exists, the turn passes.

## Rosettes

There are **5 rosette squares**. In the standard Finkel/RoyalUr layout they fall on
each player's path at **steps 4, 8, and 14** (the 4th square of the private entry
block, the central shared square, and the final private square before bearing off).
This yields 5 distinct board squares: 2 private to each player + 1 shared center.
These indices are what `ur_is_rosette()` encodes (steps 4, 8, 14).

A piece that lands on a rosette:

- grants the player an **extra roll** (move again), and
- is **safe from capture** while it sits there.

The shared central rosette therefore acts as a safe spot in the combat zone.

## Capturing

- Capture happens **only on the shared middle row**.
- Landing exactly on a square occupied by an **opponent** piece sends that piece
  **back to the start** (it must re-enter from the beginning of its path).
- You **cannot** capture (or share) a piece sitting on the central **rosette** — it
  is safe; if the rosette is occupied by an opponent, you cannot land there.
- You cannot land on a square occupied by **your own** piece.

## Bearing off and winning

- To remove a piece from the board, you must roll the **exact** number needed to
  move it off the end of the path (overshooting is not allowed).
- The first player to **bear off all 7 pieces** wins.

## Implementation notes (for `src/common`)

- Represent the board and each player's 14-step path as fixed lookup tables; map
  path-step → board square and back.
- Mark rosette steps (4, 8, 14 pending verification) for the extra-roll + safety logic.
- Capture logic checks only shared-row squares and skips the safe central rosette.
- Keep all of this deterministic; the game server runs the same rules and must agree
  with the clients. See [`src/common/CLAUDE.md`](../src/common/CLAUDE.md).
