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
- [ ] Further polish (optional): graphical dice, move animation, full sprite pieces.
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
- [ ] Real discoverability: an assigned appkey, the client binary hosted on TNFS,
      and a per-platform lobby-client app that lists games and launches ours.

## Phase 5 — Coleco Adam port
- [ ] Implement `plat_*` for Adam (TMS9928A video, SN76489 sound, AdamNet input).
- [ ] Confirm the shared core compiles unchanged under z88dk.
- [ ] **Adam ↔ Atari cross-play.**

## Phase 6 — Commodore 64 port
- [ ] Implement `plat_*` for C64 (VIC-II sprites, SID sound).

## Phase 7 — Apple II port
- [ ] Implement `plat_*` for Apple II (hi-res, speaker sound, SmartPort FujiNet).

## Phase 8 — Polish & release
- [ ] Asset pipeline (charsets, sprites/PM shapes, palettes, SFX/music).
- [ ] Sound/music per chip; animation polish; AI difficulty levels.
- [ ] Packaging: `.atr` / `.dsk` / `.ddp` / `.d64` / `.po`; GitHub Releases.
- [ ] Distribution (itch.io / AtariAge / FujiNet game listing).

---
*Track work as GitHub issues/milestones mapped to these phases.*
