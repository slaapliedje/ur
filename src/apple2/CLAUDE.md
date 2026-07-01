# src/apple2 — Apple II platform layer (fourth target)

> **Status: local play + colour board (LO-RES default, DOUBLE-HI-RES via `DHGR=1`)
> + speaker sound + FujiNet online (`ONLINE=1`).** `src/apple2/main.c`
> reuses the shared core unchanged and draws the board in **lo-res graphics (GR)**:
> the Apple II's hi-res mode has only 6 position-dependent artifact colours (no
> brown/gold), but lo-res gives **16 solid colours**, so the board uses the same
> lapis/gold/cream palette as the C64/Adam. The board is the **horizontal 3×8
> Standard-of-Ur layout shared by every port** (the SMS showpiece look): a lapis
> field with an **inlaid mosaic** in every square — **gold flower rosettes** (a gold
> tile, brighter petals, a white pearl), **gold bullseye eyes** down the shared lane,
> and **white quincunx studs** on the private lanes — plus **round two-tone tokens**
> (an oval body + a centre pip: white body = Light, brown body = Dark). The motifs
> are drawn as filled-rectangle runs (no sprites/charset — the Apple II is a pure
> bitmap), so they're computed, not generated glyph tables. **MIXED mode** keeps 4
> text lines at the bottom for the turn / roll / move panel; the title menu and win
> screen use plain text mode. GR lives in [`gr.c`](gr.c)/[`gr.h`](gr.h). Hot-seat +
> vs-AI; `cgetc`/`kbhit` native. Sound auto-detects a **Mockingboard** (AY-3-8910 —
> richer music + SFX, `src/apple2/mockingboard.c`) and falls back to the **1-bit
> speaker** (`src/apple2/sound.c`, $C030). `make apple2` → `build/apple2/ur.system`
> (+ `ur.po` with AppleCommander).
>
> **Verified in MAME** (`apple2ee`, the enhanced //e, booting ProDOS 8): menu, the
> colour board, two-tone tokens, and a vs-AI turn all render and respond to the
> keyboard. cc65 Apple II programs run **under an OS**, so it boots from a **ProDOS**
> disk — see the run recipe below.
>
> **GR gotchas (learned the hard way):**
> - GR shares text page 1 ($0400-$07FF). **conio corrupts the GR rows** (its lazy
>   init / scrolling), so the panel text is poked directly (`gr_text`), not via conio
>   — conio is used only in text mode (menu/win).
> - `gr_bar` must compute the row address **once per text row** (not per block via
>   `gr_plot`); the naive per-block fill takes ~1s for the whole screen (a multiply
>   in the address calc × 1600 blocks) and you'll screenshot half-drawn frames.
> - Lo-res colour **8 ("brown")** renders as a dark **olive-green** in MAME's RGB
>   palette (browner on real NTSC composite); it's the canonical Apple II brown and
>   reads clearly as the dark side, so we keep it.
>
> **Double-hi-res — playable build (`make apple2 DHGR=1`).** A 140×192, 16-colour
> board, verified in MAME `apple2ee` on real ProDOS: deep **lapis field** with the
> same **inlaid Standard-of-Ur mosaic** as lo-res, at much higher resolution —
> **gold diamond flower rosettes** with white pearls, **gold bullseye eyes** down the
> shared lane, **white quincunx** on the private lanes (lane tiles **black-bordered**
> to hide DHGR's edge fringing), and **round two-tone tokens** (a tapered disc: white
> body + olive pip = Light, the inverse = Dark) on black cells, plus a clean narrow
> 80-col panel. The fixed-colour motifs are **table-driven** (a flat
> `{l,r,ya,yb,colour}` rect list run through one loop) rather than three unrolled
> functions — the DHGR layout pins CODE at `$6000`, so code that grows squeezes BSS;
> the table keeps it inside the budget. How it fits together:
> - **Page 2 (`$4000-$5FFF`)** + a custom SYSTEM config
>   ([`apple2-dhgr.cfg`](apple2-dhgr.cfg): startup `$2000`, code pinned `$6000`,
>   `$4000-$5FFF` page-2 hole) → keeps the clean `UR.SYSTEM` boot (no BASIC.SYSTEM;
>   the BRUN route dies with "NO BUFFERS AVAILABLE"). Needs the **enhanced //e**.
> - Aux writes use **RAMWRT** (reroutes all `$0200-$BFFF`, would corrupt the C
>   stack), so the aux half of each fill — and the panel's aux text — is asm
>   ([`dhgr_blit.s`](dhgr_blit.s): self-modifying stores, registers/ZP/hardware-stack
>   only); the main half is plain C. Mode + fills + page-2 text in [`dhgr.{c,h}`](dhgr.c).
> - Solid colour = a 4-bit nibble repeated; bytes cycle every 4 groups (28 px), so a
>   fill writes a phased 4-byte pattern (nibble→colour read off a 16-band calibration).
> - **Panel**: page-2 mixed-mode text is 80-col, so each line is written narrow —
>   even chars to aux, odd to main — for normal (not doubled) text.
> - **Speed**: `dh_fill` blits a whole aux rectangle in one call — RAMWRT toggled
>   ONCE (not per scanline) and each row base from a precomputed 192-entry table
>   (`rlo`/`rhi`, built in `dhgr_on`), so no per-scanline JSR or multiply; the main
>   half is C using the same table. (Aux self-modify must precede RAMWRT-on, or the
>   patch goes to aux — `dh_fill` uses the `ptr1` ZP pointer instead, since ZP is
>   exempt from RAMWRT.) The field is also filled once per game (`board_field`);
>   and `draw_board` keeps a **dirty-cell cache** (`prev_grid` in `main.c`, reset by
>   `board_field`): it rebuilds the cell grid from game state each call and repaints
>   only cells whose glyph changed, so a typical turn redraws ~1-2 cells (move source
>   + dest) instead of all 22 — and the repeated draws within a turn (roll prompt,
>   move list, result message) repaint nothing. Only the first paint is the full
>   board. Shared with lo-res, but DHGR is the big beneficiary.
> - `main.c` routes the renderer through `BOARD_ON/OFF` + `panel_*` + `draw_tile/
>   draw_token` macros (`#ifdef UR_DHGR`), sharing all board/turn logic with lo-res.
>   `apple2.mk` `DHGR=1` switches to `apple2enh` + `apple2-dhgr.cfg` + `-DUR_DHGR`
>   and adds `dhgr.{c,s}`; the default build is still lo-res.
>
> **Run:** `make apple2-bootdisk DHGR=1 PRODOS_DISK="…/prodos402.dsk"` then
> `mame apple2ee -flop1 build/apple2/ur-dhgr.dsk`. **Polish left:** "brown" is olive
> (DHGR has no true brown). FujiNet online uses the lo-res board (DHGR+ONLINE overflow).

Implements the `plat_*` interface for the Apple II family (II+, IIe, IIc, IIgs).

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). Networking model:
> [`src/net/CLAUDE.md`](../net/CLAUDE.md).

## Hardware notes

- **CPU:** 6502 @ ~1.02 MHz (slower than the Atari — mind performance).
- **No hardware sprites.** Pieces must be drawn/blitted in software.
- **Graphics:** lo-res, hi-res, and (IIe/IIc) double-hi-res. Hi-res color is quirky
  (NTSC artifact color, 7-pixel byte alignment) — plan the board layout around it.
- **Sound:** 1-bit speaker (toggle-timed); effects require CPU cycle counting.
- **Input:** keyboard; paddles/joystick.

## Sound (`src/apple2/sound.c`, the $C030 speaker)

The speaker has no pitch register: each touch of the `$C030` soft-switch flips the
cone once, so a tone is a cycle-counted loop that toggles, delays (the half-period →
pitch), and repeats (the count → duration). On top of that single `tone()` the port
shapes the click *timing* for richer effects than flat beeps:

- **`sweep(p0,p1,hold)`** — ramps the pitch across the note → a glissando. Used for
  the move chirp (rising), the capture buzz (falling), and the bear-off/win rises.
- **`noise(base,spread,toggles)`** — jitters the half-period from a Galois LFSR
  (a platform-local rattle, *not* the game RNG) → a real percussive rattle/crash.
  Used for the dice roll and the capture crash.
- **`arp(pitches,n,rounds,hold)`** — cycles a few pitches faster than the ear
  resolves → they fuse into a chord. Used for the rosette/score/win fanfares
  (a calibrated C-major triad from the hymn scale).

The title **Hurrian Hymn** still plays through `tone()`/`apple2_music_note`.

> **cc65 gotcha that silenced the whole port (fixed):** the speaker toggle was
> `(void)*(volatile unsigned char *)0xC030;` — and cc65's optimiser **drops a
> cast-to-void volatile read as dead code** (the generated `tone` loop did the delay
> nops but never touched `$C030`), so the hymn *and* every effect were silent (only
> the boot-ROM beep — the Monitor's own code — made noise). The fix is to toggle via
> **inline asm**, which cc65 never removes or reorders: `#define SPK_CLICK()
> __asm__("bit $C030")` (`BIT` reads the switch, flipping the cone, without clobbering
> A). Verified by recording MAME's `-wavwrite`: silence (0.2 s of audio, boot beep
> only) → 40 s of pitched sound. **Writes** to soft-switches are safe (cc65 keeps
> them) — that's why the graphics `SOFT()`/`WR()` macros use `*(...)=0`, not a read.

## Mockingboard / AY-3-8910 (`src/apple2/mockingboard.c`)

When a **Mockingboard** (or a Phasor in MB-compat mode) is fitted, the title music and
every sound effect play through its **AY-3-8910 PSG** — 3 square voices + noise + a
hardware volume envelope — instead of the 1-bit speaker. **One binary handles both:**
`snd_init()` (called from `main()`) probes for the card at boot; if found, a runtime
flag routes `apple2_music_note`/`sfx_*` to the AY (`mb_*`), otherwise to the speaker
(`spk_*`). The Hurrian Hymn becomes a **melody voice + an octave-down bass voice**;
SFX use AY tones, the noise channel (dice rattle / capture crash), and the envelope
(decays, the win fanfare's ring-out).

- **Card = 6522 VIA + AY:** in slot *n*, VIA #1 at `$Cn00`, VIA #2 at `$Cn80`. Drive
  the AY through VIA port A (data) + port B (control: `$07` latch-reg#, `$06`
  write-data, `$04` idle, `$00` reset). We mirror writes to **both** AYs so output is
  centred/audible regardless of stereo routing. All **writes**, so no `(void)volatile`
  trap.
- **Detection (`mb_detect`)** scans **only slots 4 and 5** — where Mockingboards live
  — and NEVER the storage/firmware slots: poking the disk controller in slot 6 (or a
  SmartPort in 7) wrecks a live ProDOS (an early all-slots scan crashed to the
  monitor). The probe writes two patterns to the 6522's DDRA latch, each followed by a
  read of a *different* register (scrubbing the data bus so a floating/held bus can't
  echo the value), then reads DDRA back — robust against false positives, which would
  otherwise route sound to an absent AY and go silent on a speaker-only machine.
- **Build:** auto-detected, no flag — lo-res (default) and `ONLINE=1` builds include
  `mockingboard.c` + `-DUR_MOCKINGBOARD`; the **DHGR** build excludes it (CODE pinned
  at `$6000` has no room) and stays speaker-only. See `makefiles/apple2.mk`.
- **Verify in MAME:** `mame apple2ee -sl4 mockingboard …` (the `-listslots` device is
  `mockingboard`; `-sl4` is the option). **MAME's apple2ee already has a Mockingboard
  in slot 4 by default** (`-verbose` shows `:sl4:mockingboard:ay1/ay2`), so the AY path
  is what runs by default; the AY appears on **wav channels 1 & 2** (the speaker is
  ch0). Confirmed: the AY plays the hymn melody (F5-E5-D5-C5-B4 at pitch) and detection
  rejects an empty slot (game with `-sl4 "" -sl5 mockingboard` still plays — proves no
  slot-4 false positive). Note: a standalone test `SYSTEM` program with an *empty*
  `for(;;){}` is dropped by cc65 `-O` (main returns → ProDOS quit screen); give the
  loop a side effect.

## FujiNet online (`make apple2 ONLINE=1`)

> **Shipped as its own download.** `make apple2-online` (= `make apple2 ONLINE=1`)
> emits **`build/apple2/ur-online.system`** + **`ur-online.po`** — distinct names so
> they coexist with the local `ur.system`/`ur.po`. `make release` builds both and the
> itch **apple2** channel carries both. Online is the **lo-res** board (DHGR and
> ONLINE can't share the binary — the code region overflows), so the DHGR showcase
> stays local-only and the FujiNet build ships alongside it.

FujiNet attaches via the **SmartPort** bus, but the `N:` API + Ur wire protocol are
identical to the Atari/Adam/C64, so `online_game()` is a direct port: `network_init`
→ `network_open(N:TCP://host:1234/, RW)` → `ur_proto_join(name)`, then the
server-authoritative loop (render each STATE snapshot, send ROLL/MOVE, poll via
`read_state`). Plus the shared AppKey **profile** (creator `0x5552`='UR'), **lobby
host pickup** (creator `0x0001`, appkey 6), and a `/top` **leaderboard** over `N:HTTP`.

- `make apple2 ONLINE=1` → `apple2.mk` downloads `fujinet-apple2-<ver>.lib`, adds
  `-DUR_ONLINE`, and links the **c_sp shim** (`src/atari/csp_compat.s`) on old cc65.
  Online uses the **lo-res** board (connect/wait screens are plain text; the game
  uses the colour board). Frame pacing is a busy loop (no Apple II jiffy clock).
- **DHGR and ONLINE don't fit together** — fujinet-lib (~8K) + the DHGR code overflow
  the `$6000` code region, so `apple2.mk` errors on `DHGR=1 ONLINE=1`. Online =
  lo-res board; DHGR = local only.
- **Tested** (`mame apple2ee`, no FujiNet): builds, boots to the lobby menu, local
  play renders, and Online fails the connection **gracefully** ("NETWORK INIT
  FAILED" — `network_init` finds no SmartPort FujiNet). Full cross-play needs
  **FujiNet + the Ur server**, same bar as the other ports. Build a boot disk with
  `make apple2-bootdisk ONLINE=1 PRODOS_DISK=...`.

## Build & run

- **Build:** `make apple2` (`makefiles/apple2.mk`) runs
  `cl65 -t apple2 -C apple2-system.cfg`, then **strips the 58-byte (`$3A`) EXEHDR**
  cc65 prepends → `build/apple2/ur.system` (the bare ProDOS SYSTEM image whose first
  byte is the `$2000` entry — `LDX #$FF / TXS / ...`). ProDOS loads SYSTEM files at
  `$2000` and JMPs there, so the EXEHDR (meant for `$1FC6`) must not be in the file —
  forgetting this loads the header at `$2000` and BRKs immediately. With an
  **AppleCommander** jar present (`AC=` / a jar in `lib/` or `tools/`; from
  https://github.com/AppleCommander/AppleCommander/releases, needs `java`) it also
  writes `build/apple2/ur.po` with the program as `UR.SYSTEM`.
- **Make a bootable disk:** AppleCommander's blank `-pro140` disk has only a
  placeholder boot block (no ProDOS kernel). To get a self-booting disk, copy a real
  ProDOS disk (which supplies `PRODOS` + a real boot block) and drop our `UR.SYSTEM`
  in as the only `.SYSTEM` launcher — `tools/apple2-bootdisk.sh` does exactly that:
  `make apple2-bootdisk PRODOS_DISK="/path/to/ProDOS.dsk"` → `build/apple2/ur-boot.<ext>`.
  (Keep the source's extension — AppleCommander reads sector order from it; a ProDOS
  fs in DOS order is `.dsk`/`.do`, not `.po`.)
- **Run:** `mame apple2ee -flop1 build/apple2/ur-boot.dsk` — **the *enhanced* //e**
  (`apple2ee`, 65C02): ProDOS 8 v2.x refuses to boot on the plain `apple2e`. Also
  runs in AppleWin / on real hardware.
- **Network test:** FujiNet-PC or real hardware (SmartPort), once the online path lands.

## Notes for continuing this port

1. The `plat_*` interface fit unchanged — the shared core dropped straight in.
2. Display/input/sound live here only; keep `src/common` pure.
3. For a colour board, move to hi-res/double-hi-res (mind artifact colour + the
   7-pixel byte alignment); the text board is the bring-up baseline.
4. Reuse the exact same networking protocol + codec for online — no protocol changes.
