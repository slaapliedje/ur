# itch.io publishing — The Royal Game of Ur (downloadable)

This is the paste-ready page copy + a checklist for putting the **offline**
builds on itch.io as a downloadable game. The actual upload is scripted in
[`tools/itch-push.sh`](../../tools/itch-push.sh) (via `butler`).

---

## Setup checklist (one-time, in the itch.io dashboard)

1. **Create the project** → *Dashboard → Create new project*.
   - **Kind of project:** Downloadable (Windows/macOS/Linux can stay unchecked —
     these are emulator ROMs; see "Platforms" note below).
   - **Title:** The Royal Game of Ur
   - **Short description / tagline:** *The 4,600-year-old Mesopotamian board game,
     reborn on a shelf of retro 8-bit machines.*
   - **Classification:** Game · **Genre:** Board game / Strategy
   - **Tags:** `board-game`, `retro`, `atari`, `commodore-64`, `nes`, `game-boy`,
     `sega-master-system`, `apple-ii`, `coleco`, `8-bit`, `open-source`, `multiplayer`
   - **Pricing:** Free, or "No payments / pay-what-you-want" (it's GPLv3).
2. **Visuals:**
   - **Cover image:** 630×500 (min 315×250). A clean board screenshot works well —
     the SMS/Apple II/Adam carved-board shots are the most striking.
   - **Screenshots:** add 4–6 across different machines (show the range). Animated
     GIFs of a capture/rosette read great.
3. **Upload the build** — use `tools/itch-push.sh` (below), or drag the
   `ur-<version>.zip` into the Uploads section and label it "All platforms (ROMs)".
4. **This is for emulators** — add a sentence near the top of the description (the
   copy below has it) so buyers aren't surprised they need an emulator.
5. **Publish** (set visibility to Public when ready).

> **butler API key:** `tools/itch-push.sh` needs you to run `butler login` once
> (opens a browser to grab your key), or set `BUTLER_API_KEY` in the environment.

---

## Page description (paste into the itch editor)

**The Royal Game of Ur** is one of the oldest board games in the world — played in
Mesopotamia ~2,600 BCE and excavated at the city of Ur. This is a faithful,
good-looking build of it for an entire shelf of retro machines, from one shared
game core.

> ⚠️ These are **emulator ROM / disk / cartridge images** — you'll need an emulator
> for your chosen machine (all free; suggestions are in the included
> `HOW-TO-PLAY.txt`). Or run them on real hardware.

**Play it on:** Atari 8-bit · Atari 5200 · Commodore 64 · Apple II · Coleco Adam ·
ColecoVision · Sega Master System · Game Gear · Game Boy / Color · NES / Famicom.

**Features**
- The authentic *Standard of Ur* board — carved cells, gold rosettes, two-tone
  tokens — rendered to each machine's strengths (sprites, charsets, palettes).
- 2-player hot-seat **and** vs-computer with selectable difficulty (Easy / Normal /
  Hard).
- Chip sound + the **Hurrian Hymn** title theme (the oldest notated melody known;
  the Apple II build even drives a Mockingboard if you have one).
- The Atari & Coleco Adam builds can also play **online over FujiNet** against the
  game server.

**Rules** deciphered by Dr Irving Finkel of the British Museum — with thanks.

**Free & open source** (GPLv3). Full source, every platform, and the networked
server: https://github.com/slaapliedje/ur

---

## Notes
- **License:** GPLv3 — distribution is fine; the page links to the source (required).
  Keep the GitHub link visible.
- **Platforms field:** itch's Windows/macOS/Linux checkboxes drive its app/installer
  features; since these are ROMs, leaving them unchecked (a plain download) is
  correct. The zip is OS-agnostic.
- **Channels:** `tools/itch-push.sh` pushes to one channel (`roms`); itch versions
  each push automatically. (Per-platform channels are an option later.) An HTML5
  browser build is intentionally **not** planned — these ports are for playing on
  real/emulated retro hardware, and web versions of Ur already exist.
