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

**Live project:** https://slaapliedje.itch.io/royal-game-of-ur (slug
`royal-game-of-ur`; currently a Draft — publish when ready). `tools/itch-push.sh`
defaults to this target.

## Page description

The polished, rich description lives in **[`page.html`](page.html)** (semantic HTML —
headings, a platforms table, lists). To use it: on the project's *Edit* page, in the
**Description** box, click the **`</>`** (HTML) toggle and paste the file's contents
(everything after the leading comment). Then delete each `[ add screenshot … ]` line
and drop a real image in with the editor's image button. itch keeps the structure and
strips inline CSS — the colours come from the theme below.

## Theme (colours — Edit theme)

Match the in-game *Standard of Ur* / leaderboard palette so the page feels of-a-piece:

| Role | Hex |
|---|---|
| Background (deep lapis) | `#0b1d3a` |
| Secondary panel | `#10254a` |
| Body text (shell/cream) | `#e8d8a0` |
| Headings & links (gold) | `#d8a23a` |
| Borders / rules | `#24406e` |
| Muted text | `#7f93b8` |

In *Edit theme*: set the **background** to `#0b1d3a` (flat colour, or a subtly tiled
lapis image), **text** to `#e8d8a0`, and **links** to gold `#d8a23a`. If you enable
the advanced CSS box, the same hexes give you the leaderboard's exact look (it uses
this palette in `server/web.go`).

## Cover & screenshots

- **Cover (630×500):** a clean board shot reads best — the **SMS** or **Apple II
  double-hi-res** board (most colourful), or the **Atari title** (the ziggurat). Crop
  tight on the board.
- **Screenshots (aim for 5–7, show the range):** SMS board (the showpiece) · Apple II
  lo-res *and* DHGR boards · NES board · Game Boy **Color** board · C64 board · the
  Atari title/menu. A **"CAPTURE!"** or **"ROSETTE — AGAIN!"** moment makes a great
  action shot, and a two-screen grab of **two Ataris playing each other over FujiNet**
  is a fun "it's really networked" shot.
- Capture from each emulator at integer scale (sharp pixels), PNG. The `run-ur` skill
  / the emulators set up during development produce these directly.

---

## Devlog posts

Devlogs (project dashboard → *Devlog* → *Create new post*) are great for a
work-in-progress project and surface in itch feeds. First post is drafted in
**[`devlog-01.html`](devlog-01.html)** — the "one game, eleven machines + they can
play each other" story, ending in a call for feedback/testers. Paste it the same way
as the description (post body → `</>` → paste), suggested title in the file's comment.

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
