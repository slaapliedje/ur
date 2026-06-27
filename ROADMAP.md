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
- [ ] Protocol codec (encode/decode) per [`docs/protocol.md`](docs/protocol.md).

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
- [ ] Lift the game loop into a shared controller behind `plat.h` so the Adam/C64/
      Apple ports reuse it instead of re-implementing.

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
- [ ] FujiNet online over **SmartPort** (apple2 `fujinet-lib`; same wire protocol).

## Phase 8 — Polish & release
- [ ] **Visual parity across platforms** — one shared look, each to its machine's
      strengths (see [`docs/future-enhancements.md`](docs/future-enhancements.md)).
- [ ] Asset pipeline (charsets, sprites/PM shapes, palettes, SFX/music).
- [ ] Sound/music per chip; animation polish; AI difficulty levels.
- [ ] Packaging: `.atr` / `.dsk` / `.ddp` / `.d64` / `.po`; GitHub Releases.
- [ ] Distribution (itch.io / AtariAge / FujiNet game listing).

---
*Track work as GitHub issues/milestones mapped to these phases.*
