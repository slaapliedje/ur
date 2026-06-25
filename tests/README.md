# tests — host unit tests for the portable core

These tests compile `src/common/` with the **host** C compiler (not cc65/z88dk)
and run on your machine, so the rules, AI, turn state machine, and protocol codec
can be verified fast and off-target. The `plat_*` interface
([`src/common/plat.h`](../src/common/plat.h)) is stubbed here.

Run them with:

```sh
make test
```

(See [`makefiles/host-test.mk`](../makefiles/host-test.mk).)

## What to cover

- Board path tables and move generation for every roll (0–4).
- Capture (shared row only), rosette safety + extra roll, exact-roll bear-off.
- Win detection (all 7 pieces home).
- Protocol round-trip: `encode(decode(x)) == x` for every message type.
- **Cross-engine agreement:** shared test vectors the game server (`server/`) also
  checks, so client and server provably apply the same rules.

> No framework required to start — a plain `assert.h`-based `main()` in a `tests/*.c`
> file is enough. Add a lightweight harness later if useful.
