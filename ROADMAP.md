# Roadmap

Phased plan from scaffold to a released, cross-platform networked game. Order
follows the target priority **Atari → Adam → C64 → Apple II** (see
[`CLAUDE.md`](CLAUDE.md)). Each phase should leave the project in a working,
committable state.

## Phase 0 — Scaffolding ✅ (in progress / mostly done)
- [x] Project docs (root + per-subsystem `CLAUDE.md`), rules & protocol seeds.
- [x] Repo, license (GPLv3), README.
- [x] Build system skeleton (`Makefile` + `makefiles/`), host-test target.
- [x] Dev environment (Dockerfile + devcontainer) and `docs/development.md`.
- [x] CI (GitHub Actions) building the image + all targets.
- [ ] Architecture/design docs reviewed and agreed.

## Phase 1 — Toolchain bring-up ("hello, FujiNet")
- [x] Minimal Atari `cc65` program (`src/atari/main.c`) reading the FujiNet adapter
      config via `fujinet-lib`; compiles + links in CI. **Run `build/atari/ur.xex` in
      Altirra (with FujiNet-PC) to confirm it boots and reaches the device.**
- [x] `make deps` / the atari target download the pinned `fujinet-lib` release library.
- [x] `N:` TCP open/write/read echo against FujiNet-PC (`src/atari/main.c` +
      `tools/echo-server.py`); compiles in CI. **Run the echo server + FujiNet-PC +
      Altirra to confirm the round-trip.**
- [x] Minimal Adam `z88dk` build (`src/adam/main.c`) reading the FujiNet adapter
      config; compiles + links in CI. **Run it in MAME (`adam`) / ADAMEm to confirm.**

## Phase 2 — Portable core (`src/common`)
- [x] Verify the exact board path & rosette indices against sources (see [`docs/rules.md`](docs/rules.md)).
- [x] Board model + path positions; piece/game-state structs (`ur_state`).
- [x] Dice (RNG + binary-tetrahedral roll), move generation/validation.
- [x] Rules: capture, rosette (extra roll + safety), exact bear-off, win.
- [x] Host unit tests (`make test`) + core compiles on both toolchains (`make core-check`).
- [x] AI opponent — positional heuristic (`ur_ai_pick`), host-tested incl. an
      AI-vs-AI self-play game. (Expectimax lookahead is a future upgrade.)
- [x] Protocol codec (encode/decode) per [`docs/protocol.md`](docs/protocol.md) —
      `src/common/proto.{h,c}`, host-tested (round-trip). (See Phase 4.)

## Phase 3 — Atari single-player
- [x] Atari board / piece / dice rendering + input (conio text UI) in
      `src/atari/main.c`; compiles in CI. **Run `build/atari/ur.xex` in Altirra.**
- [x] Local hot-seat 2-player (roll, choose move, capture, rosette extra roll, win).
- [x] Single-player vs AI — selectable from the Atari title menu (you are Light,
      computer plays Dark).
- [x] Graphics + sound: POKEY effects; custom-charset drawn board; ANTIC mode-4
      per-side colour (Light=white, Dark=green, rosette=orange, cells=blue); PMG
      highlight cursor with joystick selection; title/menu screen.
- [x] Visual polish: carved beveled tiles + gold flower rosettes, DLI living-tablet
      board sheen, round two-tone **PMG** token pieces, piece glide + capture
      fly-back animation, and a dice tumble. (Atari is the visual reference for the
      other ports — see [`docs/visual-design.md`](docs/visual-design.md).)
- [x] Lift the game loop into a shared controller behind `plat.h`. **Done for ALL
      ports.** `src/common/ur_game.c` owns the turn loop and drives it through the
      real `plat.h` interface (`plat_draw` / `plat_wait` / `plat_choose_move` /
      `plat_roll` / `plat_animate` / `plat_sfx_result` / `plat_seed`); each port
      shrank to implementing those + its menu, and deleted its own
      human_turn/computer_turn/play_local. The four FujiNet ports keep their separate
      `online_game` loop (not part of this contract). The controller is opt-in per
      port (`$(UR_GAME_SRC)`). Verified playing via the controller on NES, SMS,
      **Atari**, and ColecoVision; all 10 targets build, `make test` + `make
      core-check` pass.

## Phase 4 — Networking + game server
- [x] Wire protocol — `src/common/proto.{h,c}` + `docs/protocol.md`, host-tested.
- [x] Game server — `server/` (Go), server-authoritative; rules + protocol + tests in CI.
- [x] Online 2-player on Atari — "Online" mode (`online_game` over `N:TCP`); compiles
      in CI. **End-to-end test needs FujiNet-PC + an emulator with FujiNet + two
      instances** (AltirraSDL has no FujiNet; use Wine Altirra or atari800).
- [x] FGS Lobby registration in the server (`server/lobby.go`, opt-in via
      `UR_LOBBY=1`; POSTs the `GameServer` JSON, heartbeat + player-count updates).
- [ ] Real discoverability: the server now advertises + serves **all four**
      platform clients (`atari`/`adam`/`c64`/`apple2` in `clients[]`, opt-in per
      `UR_CLIENT_<PLAT>`; one shared `UR_APPKEY`). Remaining (external): host the
      client binaries at the advertised URLs, and the per-platform lobby-client app
      that lists games and launches ours.

## Phase 5 — Coleco Adam port
- [x] `plat_*` for Adam (TMS9928A video, SN76489 sound, EOS keyboard over AdamNet)
      — local hot-seat + vs-AI, builds `build/adam/ur.ddp` (MAME `adam`).
- [x] Shared core compiles unchanged under z88dk.
- [x] **FujiNet online** (`online_game`, same `N:TCP` protocol/server as the Atari);
      builds + boots, fails the network gracefully under MAME (no FujiNet emulation).
- [x] **Carved Graphics-II board** — beveled lapis cells + shaped gold rosette
      flowers (custom Mode-II patterns), two-tone token sprites, + the Finkel credit.
- [x] **ColecoVision cartridge** (`make coleco` → `build/coleco/ur.rom`): the same
      Adam layer with controller (keypad + FIRE) input, online/EOS/AdamNet stripped;
      fits the CV's 1 KB RAM. Verified in MAME (`coleco`).
- [x] **Controller input on the Adam too** — `get_key()` polls the ColecoVision
      controller (`joystick(3)`: keypad digits + FIRE) *and* the keyboard
      (non-blocking EOS poll) each loop, so the Adam is playable with a joystick,
      sidestepping the laggy emulated EOS keyboard. Verified in MAME (`adam`).
- [ ] **Adam ↔ Atari cross-play** end-to-end (needs FujiNet + the server, real hw).

## Phase 6 — Commodore 64 port
- [x] Local play (colour conio board, hot-seat + vs-AI) — builds `ur.prg`, runs in VICE.
- [x] SID sound (roll/capture/rosette/score/win) — verified via VICE audio capture.
- [x] Custom charset: round disc tokens + shaped (8-point) rosettes + lane dots, on
      a vertical board. Kept as the `make c64 CHARSET=1` fallback build.
- [x] **VIC-II multicolor sprite tokens (the colour showcase)** — genuine two-tone
      pieces (bone body + brown pip for Light, the inverse for Dark, like a real
      Ur set) on the traditional **horizontal 3×8 board**. A raster-interrupt
      **sprite multiplexer** (`src/c64/mux.s`) reuses the 8 hardware sprites across
      the 3 rows (8 × 3 = 24 token slots; a row holds ≤ 8 pieces). This is the
      default `make c64` build. **Verified in VICE; raster timing wants a real-
      hardware check** (band separation is a generous 56 lines, so margins are wide).
- [x] **FujiNet online** (`make c64 ONLINE=1`) — links the c64 `fujinet-lib`, same
      `N:TCP` wire protocol + server as the Atari/Adam: lobby/profile menu (set
      name/host, leaderboard), appkey profile + lobby host pickup. Online builds use
      the ROM charset (board cells as colour tiles) since fujinet-lib fills VIC bank
      0; the multicolor sprite tokens remain. **Builds + boots + fails the network
      gracefully in VICE; full cross-play needs FujiNet(-PC) + the Ur server.**

## Phase 7 — Apple II port
- [x] Local play (monochrome 40-col conio text board: O=Light / X=Dark, inverse
      rosettes; hot-seat + vs-AI) + **1-bit speaker** sound (`src/apple2/sound.c`).
      `make apple2` → `ur.system` (ProDOS SYSTEM image) + `ur.po`; `make apple2-bootdisk
      PRODOS_DISK=...` (`tools/apple2-bootdisk.sh`) makes a self-booting disk.
      **Verified in MAME `apple2ee` booting ProDOS 8** — menu, board, vs-AI turn, and
      keyboard all work. (cc65 apple2 programs run under an OS; needs the *enhanced* //e.)
- [x] **Lo-res colour board** (default; `src/apple2/gr.{c,h}`): 16-colour GR,
      horizontal 3×8, lapis field + gold rosette tiles + grey lanes + two-tone tokens;
      MIXED mode with a 4-line text panel. Verified in MAME `apple2ee`.
- [x] **Double-hi-res colour board** (`make apple2 DHGR=1`; `src/apple2/dhgr.{c,h}`
      + `dhgr_blit.s` + `apple2-dhgr.cfg`): 140×192, 16 colours, page-2 bitmap via a
      RAMWRT asm aux-blit, clean SYSTEM boot, black-bordered cells (low fringing),
      narrow 80-col page-2 text panel. Playable + verified in MAME `apple2ee`.
      *(Round disc tokens; asm rectangle blit + row table for fast fills; dirty-cell
      redraw repaints only changed cells per turn; polish left: "brown" is olive.)*
- [x] **FujiNet online** (`make apple2 ONLINE=1`): links the apple2 `fujinet-lib`,
      same `N:TCP` wire protocol + server as the other ports (over the SmartPort bus);
      lobby/profile menu, appkey profile + lobby host pickup, leaderboard. Uses the
      lo-res board (DHGR+ONLINE don't fit). Builds + boots + fails the network
      gracefully in MAME; full cross-play needs FujiNet + the Ur server.
- [ ] Richer speaker sound (optional polish).

## Additional console & handheld ports (bonus)

Added after the original Atari→Apple II plan; each reuses the shared core + the
horizontal **Standard-of-Ur** look. All are **local-only** (these machines have no
FujiNet, so no online path — like the ColecoVision cartridge).

- [x] **Sega Master System** (`make sms`; z88dk, VDP Mode 4 + SN76489) — the
      graphical showpiece: carved tile board, two-tone tokens, SFX + the Hurrian
      Hymn. Verified in MAME (`sms`).
- [x] **Game Gear** (`make gamegear`, `-DUR_GG`) — the SMS code with a compact
      20×18 layout for the smaller screen; shares the SMS sound.
- [x] **Game Boy / Game Boy Color** (`make gb`; z88dk gbz80) — ONE dual-mode cart:
      colour on GBC, four greys on DMG; carved board + two-tone tokens. Verified in
      MAME (`gameboy`/`gbcolor`).
- [x] **Atari 5200** (`make a5200`, `-DUR_A5200`) — the Atari layer carved into a
      5200 cartridge (controller-keypad input); POKEY sound + hardware RNG. Verified
      in atari800.
- [x] **NES / Famicom** (`make nes`; cc65) — custom CHR-tile board (gold rosette/eye,
      white quincunx, shell-white/carnelian tokens) via direct PPU; 2A03 APU sound
      (Hurrian Hymn + SFX); controller input. iNES NROM cart, verified in MAME (`nes`).

**Known gaps in these ports (tracked):**
- [x] **Game Boy / GBC sound** — done: GB APU player (Hurrian Hymn on channel 1 +
      noise/square SFX), `src/gb/sound.c`. Verified by recording in MAME.
- [x] **Game Boy entropy-seeded dice** — done: seeds from the DIV timer + the
      title-hymn/menu timing (replaced the fixed `0xA537`).
- [x] **SMS + Game Gear fixed-seed bug fixed** — done as part of the Phase-3
      controller refactor: `plat_seed()` (entropy folded from the menu's input timing
      in `wait_press`) replaced the fixed `0xA537`. The standardized hook means every
      converted port seeds the same way.
- [ ] Game Boy token glide animation (deferred).

## Phase 8 — Polish & release
- [x] **Visual parity across platforms** — all 11 targets now share the horizontal
      Standard-of-Ur board (carved cells, gold rosette/eye, white quincunx, two-tone
      tokens), each rendered to its machine's strengths
      (see [`docs/future-enhancements.md`](docs/future-enhancements.md)).
- [ ] Asset pipeline (charsets, sprites/PM shapes, palettes, SFX/music).
- [x] **Title music — the Hurrian Hymn** (h.6, Ugarit c.1400 BCE, the oldest
      notated melody; Dumbrill/Levy reconstruction). One shared melody table
      (`src/common/music.{h,c}`: MIDI notes + eighth-note durations) with a per-
      platform player mapping it to each chip — POKEY (Atari), SID (C64), SN76489
      (Adam/ColecoVision), 1-bit speaker (Apple II). Plays once on the title/menu,
      skippable by any key/FIRE. **Verified by capturing emulator audio and
      extracting the pitch sequence** on Atari/C64/ColecoVision (exact melody, right
      octave + tempo ~110bpm); Adam shares the CV code; Apple II plays via the
      speaker (clean pitch capture inconclusive in-harness — verify on real hw).
- [ ] More sound/music per chip (in-game underscore?); animation polish; AI levels.
- [ ] Packaging: `.atr` / `.dsk` / `.ddp` / `.d64` / `.po`; GitHub Releases.
- [ ] Distribution (itch.io / AtariAge / FujiNet game listing).

---
*Track work as GitHub issues/milestones mapped to these phases.*
