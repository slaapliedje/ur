# Future enhancements

A backlog of "nice to have" ideas, kept out of the per-phase [ROADMAP](../ROADMAP.md)
so the roadmap stays about shipping. Nothing here blocks a release.

## Visual parity across platforms — one look, four expressions

**Goal:** the four builds should *read* as the same game — a shared visual
language for the Royal Game of Ur — while each plays to its own machine's
graphical and audio strengths rather than to a lowest common denominator. The bar
to aim for is something like FujiNet's **5 Card Stud**, which looks genuinely good
on the Atari with strong, confident colour.

### The shared language (what should feel the same everywhere)
- **Lapis + gold** palette (the Standard of Ur): a lapis-blue board, gold accents.
- A board with real **physical presence** — distinct cells/lanes, not just dots.
- **Rosettes with an actual rosette/flower shape**, in gold, that read as special.
- **Round two-tone tokens** like a real Ur set: a cream piece with a dark pip and
  a dark piece with a cream pip.
- Consistent layout (vertical board, info to the side) and the same sound *events*
  (roll / move / capture / rosette / bear-off / win).

### Where each build is strong today, and the gap to close
We ended up with each platform leading in a different area — the enhancement is to
back-fill each one's weaker side using its own hardware:

- **Atari (ANTIC/GTIA, POKEY).** Has **shaped rosettes** (redefined charset) and a
  DLI lapis→gold sky gradient, but the board cells are just **diamonds** — little
  physical presence. *Close the gap:* give the cells body (shaded/bordered cells
  via the charset, like the Adam) and use **player/missile graphics** for round
  tokens + highlights. Only 8 lumas per hue, so lean on two-hue blends.
- **Coleco Adam (TMS9928A, SN76489).** Has a **physical cell board** (16×16 colour
  cells) and **round two-tone token sprites** + sound, but the **rosettes are just
  a cell colour** — no shape. *Close the gap:* give rosettes a real shape (a
  rosette glyph in the cell, or a small overlaid sprite/pattern).
- **Commodore 64 (VIC-II, SID).** Strongest colour story, and now the realized
  showcase: per-cell **colour RAM**, **8 hardware multicolor sprites**, custom
  charset, and a real **brown** in the palette (so genuinely beige+brown tokens, no
  red substitute like the TMS). SID gives the richest sound. **Done:** the default
  `make c64` build draws the traditional **horizontal 3×8 board** (charset rosettes
  + lane dots) with **multicolor sprite tokens** — bone body + brown pip (Light),
  the inverse for Dark, like a real Ur set. A **raster-interrupt sprite multiplexer**
  (`src/c64/mux.s`) reuses the 8 hardware sprites across the 3 rows (8 × 3 = 24 slots,
  a row holds ≤ 8 pieces), so all 14 pieces show with no flicker. A charset-only
  vertical board survives as `make c64 CHARSET=1`. This is the look the other
  targets should approximate. *Still:* confirm raster timing on real hardware.
- **Apple II (lo-res / hi-res, 1-bit speaker).** The constrained one: no sprites,
  beeper audio, and hi-res has only 6 position-dependent artifact colours (no brown
  or gold). **Done:** a **lo-res (GR) colour board** — 16 solid colours, so it gets
  the lapis field + gold rosette tiles + grey lanes + two-tone tokens directly (no
  artifact-colour fight), horizontal 3×8, MIXED mode with a 4-line text panel
  (`src/apple2/gr.{c,h}`). Chunky (40×48) but the right palette. **Also done:**
  **double-hi-res** (`make apple2 DHGR=1`, `src/apple2/dhgr.*`) — 140×192, the same
  16 colours, finer; page-2 bitmap via a RAMWRT asm aux-blit, black-bordered cells
  to hide fringing, narrow 80-col page-2 text panel, clean SYSTEM boot on the
  enhanced //e. "Brown" reads olive in both modes (no true brown in the palette);
  tokens are round discs (tapered fills); fills use an asm rectangle blit + a row
  table (RAMWRT toggled once, no per-scanline multiply), and `draw_board` only
  repaints cells whose glyph changed (a dirty-cell cache), so a turn redraws ~1-2
  cells instead of all 22.

### Concrete cross-pollination TODOs
- [ ] Atari: shaded/bordered board cells (charset) so squares have presence.
- [ ] Atari: PMG round tokens (two-tone) instead of `#`/`@` glyphs.
- [ ] Adam: shaped rosette (glyph or sprite) instead of a bare cell colour.
- [x] C64: multicolor sprite tokens (multiplexed) + charset board/rosettes; strong
      palette. *(Done — `src/c64/mux.s`; verify raster timing on real hardware.)*
- [x] Apple II: lo-res (GR) 16-colour board + **double-hi-res** (`DHGR=1`, 140×192)
      — lapis field, gold rosette tiles, two-tone tokens (`src/apple2/{gr,dhgr}.*`).
- [ ] Shared asset pipeline (charsets, sprite/PM shapes, palettes) — see
      ROADMAP Phase 8.

## Other ideas
- AI difficulty levels (the core `ur_ai_pick` is greedy today).
- Music/jingles per chip (title theme, win fanfare) beyond the current blips.
- Animation polish: piece movement, dice tumble, capture effect.
- In-game how-to / rules screen on every platform (the Atari has one).
- Configurable profile UX parity (the Atari/Adam AppKey profile, on C64/Apple II).
