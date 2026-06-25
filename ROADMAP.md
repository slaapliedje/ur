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
- [ ] AI opponent (heuristic + short expectimax over the dice distribution).
- [ ] Protocol codec (encode/decode) per [`docs/protocol.md`](docs/protocol.md).

## Phase 3 — Atari single-player
- [ ] Implement `plat_*` for Atari: board/piece/dice rendering, input, sound.
- [ ] Title/menu/game/win screen flow.
- [ ] Local hot-seat 2-player.
- [ ] Single-player vs AI.

## Phase 4 — Networking + game server
- [ ] Finalize the wire protocol.
- [ ] Game server (`server/`, Go) implementing rules + protocol, server-authoritative.
- [ ] FGS Lobby registration; lobby-client launch flow.
- [ ] Online 2-player on Atari (test with two emulator instances + FujiNet-PC).

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
