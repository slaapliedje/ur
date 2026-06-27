# Visual design — making each port look great

Deep, per-platform plan for turning the four Ur ports from "functional board" into
games that look genuinely good *on their own hardware*. The principle: **target each
machine's graphical strengths and design around its weaknesses** — not a
lowest-common-denominator look. The quality bar is FujiNet's **5 Card Stud** on the
Atari.

This is the research/design companion to the lighter backlog in
[`future-enhancements.md`](future-enhancements.md) (the "Visual parity" section) and
the per-platform notes in each `src/<platform>/CLAUDE.md`. Nothing here is
implemented yet — it's the plan.

## The shared visual language (what should feel the same everywhere)

- **Lapis + gold** palette (the Standard of Ur): a lapis-blue board, gold accents.
- A board with real **physical presence** — carved/beveled cells & lanes, not flat
  blocks or diamond outlines.
- **Rosettes that read as an actual gold flower shape**, not just a colored cell.
- **Round two-tone tokens** like a real Ur set: a cream piece with a dark pip, a
  dark piece with a cream pip.
- The **four tetrahedral dice** drawn as objects (marked/unmarked corners), with a
  tumble on roll — not just a number.
- The **7-piece journey visible**: waiting (start) and borne-off (home) pieces shown
  in trays, not only as a score.
- Motion: pieces **glide** between cells; captures **fly back** to start; rosettes
  **sparkle**.
- The same **sound events** (roll / move / capture / rosette / bear-off / win), each
  chip playing to its strength.

## What the research found (cross-cutting)

The single biggest finding is the same on all four: **the board has no physical
presence, and every platform is leaving its signature capability on the table.** The
fix is different on each machine but the *theme* is identical — "carve the board,
then make the pieces move on it."

1. **Board cells (every platform, priority #1).** Atari draws lapis *diamond
   outlines* on a black field; C64 draws flat diamonds (or plain colored squares
   online); Adam paints *blank colored spaces* while sitting in a full bitmap mode;
   Apple II draws flat blocks. All four want **beveled/carved lapis tiles**.
2. **Rosettes are flat color, not a flower** — on all four. This is the most visible
   shared gap and is cheap to close on each.
3. **Dice are never drawn** — every port shows the roll as a number. The four dice
   are part of the shared language and are absent everywhere.
4. **No animation anywhere** — pieces teleport, captures don't fly back, dice don't
   tumble. Motion is the difference between "tech demo" and "finished game," and the
   sprite platforms (C64, Adam) get it almost for free.
5. **Off-board pieces are invisible** on most ports (start/home pieces show only as a
   count) — the 7-piece journey doesn't read at a glance.
6. **Title & HUD are plain text** — the first/last thing the player sees has no art,
   where 5 Card Stud opens with color.
7. **No visual cursor** — move selection is a typed `1)..N)` list on every port; a
   highlight/cursor on the board is the missing "what am I about to do" feedback.
8. **Sound is parity-complete but thin** — single-voice blips. Each chip (SID
   especially) has more to give.

Two reframes worth calling out, because they make big wins cheap:

- **Atari: the DLI engine already exists and is switched off during play.** The
  title's per-scanline color program (`dli.s` / `atari_title_sky_on`) is exactly what
  the board needs — wiring it back on is most of the "signature" win.
- **Adam: the build is already in Graphics Mode II** (the 256×192 bitmap), but using
  it like a colored-text screen. Redirecting that same canvas to real patterns costs
  pattern bytes, *not* a mode change.
- **Apple II: 2-color dithering is a superpower no other target has.** Lo-res color
  is solid and position-independent, so a checkerboard reads as a rock-stable third
  shade — fake bevels, gold sheen, and a less-olive brown, nearly free.

---

## Atari 8-bit (ANTIC / GTIA / PMG)

**Strengths that matter:** Display List Interrupts (unlimited colors *down* the
screen via per-scanline register reloads — and the vertical board is row-indexed, a
perfect fit); ANTIC mode 4/5 multicolor charset (5 colors/region, mode 5 = 8×16 for
detail); Player/Missile Graphics (4 players + 4 missiles floating over the playfield
with their own colors — ideal for round tokens, a cursor, gliding motion); the
fifth-player / multicolor-player tricks for extra colors; GTIA mode 9 (16-luma smooth
single-hue gradient).

**Signature move:** **Turn the board into a living lapis tablet with a full DLI color
program, and let PMG pieces glide across it.** Switch the field from black to lapis,
run one DLI down the board band giving a vertical lapis sheen + lighting the active
player's lane + gold rosette rows, and make the on-board pieces round two-tone PMG
discs that slide between cells and fly home on capture. The DLI half is *low effort*
— the engine already exists in `dli.s`, just disabled during play.

| Element | Change | Why (Atari strength) | Effort | Payoff |
|---|---|---|:--:|:--:|
| Board field | Black → lapis; per-row DLI sheen + lit active lane | DLI = colors beyond the register count | S–M | **H** |
| Board cells | Beveled lapis tiles (light/shadow edges) on black grout, not diamonds | Mode-4 charset 4 colors/region | S | **H** |
| Tokens | `#`/`@` glyphs → PMG round discs (frees COLPF0/1 for cells); multiplex players down rows | A player = free floating 8px object, own color, motion | L | **H** |
| Rosettes | 8-point gold flower glyph that shimmers | Inverse-char gold (COLPF3) + cheap color-cycle | S–M | M–H |
| Dice | Color mode-4 tetrahedral dice + short tumble on roll | Mode-4 color + char-frame animation | M | M |
| HUD | DLI-painted panel bands + gold rule | One more DLI band | S | M |
| Capture/move FX | PMG slide cell-to-cell; capture flash + fly-home; rosette sparkle | PMG reposition is smooth, never redraws board | M–L | **H** |
| Title | Upgrade sky to GTIA mode 9 (16-luma) + PMG sun/rosette | Mode 9 = smooth single-hue gradient | M | M |

**Anti-patterns:** only 8 luminances per hue (NTSC) — don't build luma-step gradients
and **don't dither** (it flickers — already learned on the title); blend two adjacent
hues at equal luma. Mode 4 is ≤4 colors per horizontal region — get extra colors
*vertically* (DLI) or via PMG, never by cramming the charset. One player = one color,
so two-tone pips need the missile/fifth-player route or a per-row COLPF3 swap. Keep
DLI handlers tiny (`WSYNC` + a couple `STA`s). Avoid GR.8 hi-res (artifact fringing,
no lapis/gold). *Current build fights the machine* with a black field
(`COLOR4=0x00`) and `COLOR1` double-booked for dark pieces **and** text — both resolve
once tokens move to PMG.

---

## Commodore 64 (VIC-II / SID)

**Strengths that matter:** a real palette for an Ur set (**true brown $09**, orange,
gold, lapis blues, cream — no other target can do beige-body/brown-pip honestly);
per-cell **Color RAM** (unique color per 8×8 cell, free); **multicolor character
mode** (beveled multicolor tiles + crisp hires HUD text on one screen); **8 hardware
multicolor sprites + a working multiplexer** (`mux.s` already shows all 14 two-tone
tokens flicker-free); smooth sprite positioning (cheap glide/fly-back); a raster IRQ
already in the loop (free color splits + open borders); SID (the best chip — layered
rattle, capture thud, win fanfare).

**Signature move:** **Carve the board** — beveled multicolor-lapis tiles with true
gold flower rosettes, full-bleed under an opened top/bottom border, the existing
two-tone sprites riding on top. The tokens are already excellent (`build_token`); the
board is the weak element. Multicolor char mode gets dimensional lapis tiles + a gold
rosette flower + crisp HUD text simultaneously, all driven by free Color RAM.
Runner-up: smooth token-glide + capture fly-back (sprites make motion trivially cheap
here).

| # | Element | Change | Why (C64 strength) | Effort | Payoff |
|--:|---|---|---|:--:|:--:|
| 1 | Board cells | Beveled multicolor lapis tiles (highlight/shadow/face) | MC char mode + Color RAM; HUD stays hires | M | **H** |
| 2 | Rosettes | True gold 8-point flower tile | Same MC trick; gold is native | S | **H** |
| 3 | Move/capture | Glide token cell-to-cell; capture arcs back to tray | Hardware-sprite positioning is near-free | M | **H** |
| 4 | Start/home trays | Off-board pieces as mini charset discs | Color RAM per-disc color; dodges sprite budget | S | **H** |
| 5 | Dice | 4 tetrahedral dice glyphs + tumble | Cheap charset, matches SID rattle | M | M |
| 6 | HUD | "Stone tablet" frame; score as 7 fill-pips | Color RAM + charset framing | M | M |
| 7 | Cursor | Flashing Color-RAM highlight (not a 9th sprite) | Free, avoids per-line sprite cap | S | M |
| 8 | Backdrop | Raster-split lapis gradient + open top/bottom border | Per-row $D021 in the existing IRQ | S | M |
| 9 | Title | "Standard of Ur" MC logo, raster sky, demo tokens | Shows palette+raster+sprites in 3s | L | M |
| 10 | SID | Multi-voice rattle/thud/win fanfare | Best chip of the four | M | M |
| 11 | Render path | Dirty-cell update + double-buffered band tables | Kills per-turn flash + IRQ tearing | M | M |

**Anti-patterns:** 8 sprites/line is a hard wall — don't add the cursor as a 9th
sprite on a full middle band (use a Color-RAM flash). Multicolor halves horizontal
resolution — keep petals/pips chunky; leave cells that need crisp 1px edges in hires
(Color-RAM nibble < 8). MC char mode is global ($D016 bit4) — keep all *text* cells
nibble < 8 so the HUD stays readable. Avoid multicolor *bitmap* and FLI (RAM/CPU cost,
fatal online where fujinet-lib fills bank 0). Don't open the *side* borders (fragile).
Sprite-shape RAM is scarce and **gone in the online build** — gate animation/extra
sprites on the local build. Stop the full clear-and-repaint each turn (it flashes and
opens a tearing window). Verify raster timing on real hardware (band constants are
PAL-tuned).

---

## Coleco Adam (TMS9928A / SN76489)

**The reframe:** the build is **already in Graphics Mode II** (z88dk's default screen
mode 2 = the 256×192 bitmap), but drawing the board out of blank colored spaces. The
highest-leverage change is to **draw the board out of custom Mode-II patterns** —
carved lapis cells and shaped gold rosette flowers — which the hardware renders for
free because we're *already paying for Mode II*.

**Strengths that matter:** Graphics Mode II is a true 256×192 bitmap (768
independently-drawable 8×8 cells) with **per-8-pixel-row 2-color choice** (far more
freedom than "2 colors per cell" — a gold-on-lapis flower fits trivially); 32 hardware
sprites (16×16, magnifiable) for tokens/cursor/animation floating over a static board;
two yellows (old-gold + cream-gold) and the lapis `DARK_BLUE`; backdrop register for
edge-to-edge field; z88dk gives the primitives (`vdp_set_char_form/_color/_char`,
`vdp_*sprite_16`, `vdp_get_status`).

**Signature move:** **Draw the board as a carved Standard-of-Ur inlay in Mode II** —
shaped gold rosette flowers on beveled lapis cells, instead of flat colored spaces —
then glide the moving/captured token sprite over that static board. This flips the
port from "colored text grid" to "ColecoVision-class game board" and closes the
rosette-shape gap that's the Adam's one stated weak spot.

| # | Element | Change | Why (TMS9928A strength) | Effort | Payoff |
|--:|---|---|---|:--:|:--:|
| 1 | Rosettes | Shaped 8-petal gold flower (2×2 cells) on lapis | Mode-II per-row 2-color; pure tile art, 0 sprites | S | **H** |
| 2 | Board cells | Carved/beveled bodies; distinguish shared lane | Same bitmap we already render | M | **H** |
| 3 | Tokens | Render *resting* tokens as Mode-II tiles; sprites only for the moving piece + cursor | Tiles have no per-line cap → two-tone never degrades | M | **H** |
| 4 | Dice | 4 tetrahedral dice tiles + tumble | Authentic; tile art is cheap; syncs `sfx_roll` | M | **H** |
| 5 | Title | Gold Mode-II logo + rosette band + token motif | First impression; reuses #1 tiles | M | **H** |
| 6 | Cursor | Gold bracket/chevron sprite over legal pieces | One spare sprite, smooth, flicker-free | M | M |
| 7 | Capture/move | Glide token sprite; capture flies back on an arc + flash | Sprites move with no board redraw | L | **H** |
| 8 | HUD | Gold-bordered box; score as token-icon pips | Tile-cheap; matches the inlaid look | S | M |

**Anti-patterns:** **4 sprites per scanline is a hard ceiling** — three pieces abreast
with body+pip sprites = 6/line and pips silently drop, flattening the two-tone. Move
resting tokens to *tiles* (#3) and budget sprites for *motion only* (1–2 movers + 1
cursor); use the 5th-sprite status flag to assert you never exceed 4. **2 colors per
8 horizontal pixels** — align art to the 8px grid and exploit the free *vertical*
color changes. **No brown, no orange** — `DARK_RED` is the warmest dark (reads
terracotta); dither `DARK_RED`+`DARK_YELLOW` for a board frame, but lean into
lapis+gold+cream. Mode-II pattern/color banks are *per screen-third* — a glyph used in
all three thirds must be written to all three banks. VRAM isn't CPU-addressable —
define static art once, then per-turn touch only changed cells + the sprite table;
don't re-upload sprite patterns every frame.

---

## Apple II (lo-res / DHGR, 1-bit speaker)

The constrained target — no sprites, beeper audio, no true brown/gold — but with two
real aces: **lo-res's 16 solid, position-independent colors** (the only mode across
all four that gets the lapis/gold palette with zero artifact-color fight) and **DHGR's
140×192 resolution**.

**Strengths that matter:** lo-res gives the exact lapis+gold palette as *solid* color;
lo-res blocks are cheap to fill (animation realistic at 1 MHz); DHGR resolution can
render an actually round token and a recognizable flower; and the big one — **solid
color + ordered dithering = stable fake shades** no other target gets (gold sheen,
bevels, a less-olive brown), nearly free. The perf groundwork is already laid (asm
rect-blit + row table + dirty-cell cache).

**Signature move:** **Carve the chunky lo-res board with 2-color dithering** —
bevel-shaded cells, dithered lapis field, rounded dithered tokens, a real gold flower
rosette. No other mode on any of the four platforms can checker-dither two colors into
a rock-stable third shade; lean all the way into it on the *most* constrained machine.
"Wait, that's the Apple II?" is the goal — and dithering, not resolution, is how you
get there. **Keep both modes but make lo-res the hero** (it's the default, the only
mode that ships with online, runs on every //e, has the warmer palette and cheaper
fills); DHGR stays a high-res local showcase.

| # | Element | Mode | Change | Effort | Payoff |
|--:|---|---|---|:--:|:--:|
| 1 | Board cells | lo-res→DHGR | Bevel edges + dithered lapis field = carved stone | S | **H** |
| 2 | Rosettes | both | Gold flower glyph; dither orange+yellow for sheen | S/M | **H** |
| 3 | Tokens | lo-res | Rounded disc (clip corners) + dithered body | S | **H** |
| 4 | Capture/move | both | Slide token along path; capture flies back | M | **H** |
| 5 | Dice | both | 4 tetrahedral dice + tumble on roll | M | **H** |
| 6 | Title | lo-res | Color splash: dithered board motif + gold flower + blocky "UR" | M | M |
| 7 | Cursor | both | Highlight source cell + flash destination | M | M |
| 8 | HUD | both | Inverse-video active line; off-board reservoirs | S/M | M |
| 9 | DHGR brown/gold | DHGR | Finish `dhgr_nib` calibration + dither toward brown/gold | M | M |
| 10 | Sound | both | Pitch sweeps (capture/bear-off/dice) + 2-voice win fanfare | S/M | M |
| 11 | Sound–anim sync | both | A tick per animation step (footstep/thud) | S | M |

**Anti-patterns:** DHGR fringing is permanent — keep art group-aligned (4px color
boundaries) and keep black borders (they cost resolution). No true brown/gold —
dither toward it or embrace "aged bronze"; lo-res is closer to brown, favor it for
warm tones. No sprites — keep animations to 1–2 cells/frame (the dirty-cell cache
enables this). **Page flipping is not freely available** (lo-res page 2 collides with
the load; DHGR's second page needs a main hole the cfg doesn't leave) — plan animation
around small dirty regions, not buffer swaps. Fills are the cost center, especially
DHGR — spend its budget only where resolution shows. 1-bit sound is blocking — keep
SFX short, interleave the toggle only for the win fanfare. Don't regress to conio in
graphics mode (it corrupts GR rows).

---

## Suggested cross-platform sequence

The research converges on one order that maximizes payoff per unit effort:

1. **Carve the board on every platform** (cells + gold flower rosettes). Biggest
   screenshot win, and on Atari/Adam it's mostly wiring up capability that already
   exists (DLI / Mode II). This is what gets judged against 5 Card Stud.
2. **Draw the dice + show off-board trays.** Cheap, and they complete the "this is the
   actual game of Ur" reading.
3. **Add motion** (piece glide + capture fly-back + dice tumble). The "alive" factor;
   nearly free on C64/Adam (sprites), affordable on Atari (PMG) and Apple II
   (small dirty-rect blits).
4. **Title screens + HUD polish + a visual cursor.** The framing.
5. **Sound polish** per chip (SID multi-voice first — biggest return).

Each step is a natural per-platform commit. The shared C core and turn logic don't
change — this is all in the platform layers.
