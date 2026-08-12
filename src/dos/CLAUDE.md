# src/dos — MS-DOS / IBM PC platform layer (the FIFTH FujiNet online port)

> **Status: playable, local + FujiNet online — the fourth CPU family.** x86
> real mode joins the 6502, Z80, and 68000. `src/dos/main.c` is the Atari ST
> layer ported to **VGA mode 13h** (320×200, 256 colours, one byte per pixel
> at A000:0000 — the shared layout size with the friendliest pixel format of
> any port) and upgraded with the **Atari TT build's gradient ramps**, which
> mode 13h's DAC does natively: a 64-shade night→dusk-amber sky, 32-shade
> lit-from-top cell faces, a 16-shade sand ramp. Sound is the **PC speaker**
> (PIT channel 2 square waves — 1-bit like the Apple II, but with a
> free-running timer instead of cycle counting): the Hurrian Hymn on the
> title, roll/capture/rosette/win SFX. Keyboard number-key input like the
> other computer ports; green destination highlights; ESC at the title exits
> to DOS. **FujiNet online is baked into the default binary** (like the
> Atari/Adam): the full lobby menu (Online / Set name / Set host / Top ten),
> the shared AppKey profile, the `N:TCP` server-authoritative loop — **proven
> end-to-end against the live server** (see the rig below). Without a driver
> the Online option degrades to a message. `make dos` → `build/dos/ur.exe`
> (a ~24 KB real-mode MZ executable; `ONLINE=0` builds a lib-less local-only
> variant).

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). This layer implements the
> `plat_*` contract from [`src/common`](../common/CLAUDE.md) via the shared
> controller (`ur_game.c`). Art is procedural; the 8×8 font is shared from
> `src/sms` (`-I` in `dos.mk`).

## Hardware / API usage

- **CPU:** any 8086+ (compiled with default 8086 instructions, small model).
  **Video:** VGA mode 13h via `int86(0x10)`; palette via DAC ports
  `3C8h`/`3C9h` (6 bits/channel); framebuffer via a `__far` pointer from
  `MK_FP(0xA000, 0)`. **Sound:** PC speaker — PIT channel 2 (`43h`/`42h`,
  mode 3 square wave, divisor = 1193182/freq) gated by port `61h` bits 0–1.
- **Timing:** the VGA vertical-retrace flag (`3DAh` bit 3) — mode 13h refreshes
  at ~70 Hz, so `MUS_EIGHTH` is 15 frames (≈214 ms, matching the other ports'
  ~216 ms eighth).
- **Input:** Watcom `kbhit()`/`getch()` (BIOS-backed, no echo). The RNG seed
  accrues in the `kbhit()` wait loop, like the ST's `Cconis()` spin.

## Toolchain (Open Watcom v2)

Built with **owcc** (Watcom's POSIX-style driver): `-bdos -mcmodel=s -O2
-fno-stack-check` → a 16-bit real-mode MZ `.exe`. Install = untar
`ow-snapshot.tar.xz` anywhere (no root); `dos.mk` sets `WATCOM`, `INCLUDE`,
and `PATH` itself. Chosen deliberately: **fujinet-lib's `msdos` target and
the FUJINET.SYS driver are built with the same wcc/wlib** — one toolchain for
the whole stack.

## FujiNet online (how DOS talks to a FujiNet)

The PC has no SIO/IEC/AdamNet bus; FujiNet for the PC is the **RS-232
adapter** on a COM port. The stack:

- **`FUJINET.SYS`** (from [fujinet-msdos](https://github.com/FujiNetWIFI/fujinet-msdos);
  a prebuilt `.sys` is on its releases page) loads in `CONFIG.SYS`
  (`DEVICE=FUJINET.SYS FUJI_PORT=1 FUJI_BPS=9600`), speaks the FUJICOM SLIP
  protocol over the UART, and installs a software **INT F5h** API.
- The game calls INT F5 with the **canonical register contract**
  (`include/fuji_f5.h` upstream): DL=direction, **DH=field descriptor**
  (FUJI_FIELD_*), AL=device, AH=command, CX=aux12, SI=aux34, ES:BX/DI=payload.
- ⚠️ **fujinet-lib 4.11.2's msdos bus layer predates the DH field descriptor**
  — its network calls reach the FujiNet with zero aux fields ("Insufficient
  open paramaters: 0" in the firmware log) even though its payload-only appkey
  calls work fine. So `src/dos/urnet.c` drives the network device (0x71)
  directly with the canonical contract (patterns after upstream
  `ncopy/fujifs.c`: OPEN = write 256-byte devicespec with FIELD_A1_A2 aux
  mode/trans; READ/WRITE = FIELD_B12 with the length in aux12; STATUS = read
  the 4-byte {len,conn,err} block), while the **appkey profile still goes
  through fujinet-lib** (`fuji_*_appkey` — proven compatible).
- **Driver detection**: `_dos_getvect(0xF5) != 0` gates every FujiNet touch
  (a null vector on bare DOS would crash on call). Under emulators the vector
  may be a dummy IRET — then the INT F5 result byte ('C'/'E') check fails the
  call cleanly instead. Either way: message, no hang.

`online_game()` itself is the C64's, re-skinned for the VGA `text()` UI: JOIN →
render STATE snapshots → send ROLL/MOVE intents; profile + lobby-handoff
AppKeys; the `/top` leaderboard over `N:HTTP`.

### The emulated end-to-end rig (all pieces, no hardware)

Proven 2026-08-12 against the live server (`thefnords.com:1234`, the server's
60 s AI fallback as the opponent — full JOIN/ROLL/MOVE/STATE round trips):

1. **FujiNet-PC for RS232**: in fujinet-firmware, `./build.sh -p RS232 -b -y`
   → `build/dist/fujinet`. In `fnconfig.ini` set `[BOIP] enabled=1 port=1985`
   (leave `[Serial] port=` empty) — the RS232 bus then listens on TCP :1985
   as a raw byte pipe (BoIP, né BeckerSocket).
2. **Guest DOS floppy**: FreeDOS 1.3 boot floppy (`144m/x86BOOT.img` from
   FD13-FloppyEdition), then via mtools: replace `FDCONFIG.SYS` with our
   DEVICE line + `SHELL=\FREEDOS\BIN\COMMAND.COM ... /P=\FDAUTO.BAT`, add
   `FUJINET.SYS` + `UR.EXE` (CRLF line endings!).
3. **DOSBox-X** bridges guest COM1 to the BoIP listener:
   `serial1=nullmodem server:127.0.0.1 port:1985 transparent:1`, autoexec
   `imgmount a urboot.img -t floppy` + `boot a:`.
4. Boot messages prove the link: the driver banner reports the **firmware
   version read over the wire** and sets the DOS clock from FujiNet NTP.

Rig gotchas: DOSBox-X under Xvfb needs `output=surface` but its window-id
`import` captures go SOLID BLACK after the guest's mode switch (same trap as
Hatari) — **capture the root window**. Killed DOSBox-X instances can leave a
`kdialog` orphan holding the floppy image (`lsof`, kill it). The guest-boot
UART reset drops/reconnects the nullmodem once — harmless.

## Gotchas (all hit during bring-up)

- **16-bit `int`:** `y * 320` overflows a signed int for y ≥ 103 — every
  framebuffer offset must be computed as `(unsigned)y * 320u` (fits: max
  63999). This is the one real difference from the 68000 ports' arithmetic.
- **`-std=c89` hides Watcom extensions** (`_fmemset`'s prototype, `__far`
  semantics): compile with the default dialect, not a strict `-std` flag.
- **`_fmemset` is the fill primitive** — one call per scanline row makes
  `frectw`/`frect` a single flat helper (no planar split, no 16-px alignment
  rule; `frect` just aliases `frectw`).
- **DOSBox auto-exits** when the program given on its command line terminates
  — so ESC-quit makes `dosbox build/dos/ur.exe` come back on its own (handy
  for scripted smoke runs; no `-exit` flag needed).
- **The DOSBox window is titled after the DOS program** (`UR.EXE - …cycles…`),
  not "DOSBox" — `xdotool search --name 'UR.EXE'` on the headless rig.
- **SDL disk-audio pacing:** capturing audio with `SDL_AUDIODRIVER=disk`
  runs DOSBox ~2.2× faster than realtime (SDL 1.2's disk driver sleeps a
  fixed 10 ms per callback), and the "stereo" file interleaves a duplicated
  junk stream with the real one — analyse the **odd samples** by
  zero-crossing run-lengths, and trust interval *ratios*, not absolute Hz.
  (The hymn verified this way: periods 194.4/183.4/163.5/145.6/137.4 = the
  B4–C5–D5–E5–F5 tetrachord exactly.)

## Build & run

- **Build:** `make dos` → `build/dos/ur.exe`. Needs Open Watcom v2 at
  `~/dev/toolchains/watcom` (override `WATCOM_DIR`); skips with a notice when
  absent (so CI/release builds stay green).
- **Run:** `dosbox build/dos/ur.exe` — or DOSBox-X, 86Box, or a real PC with
  VGA. ESC at the title returns to DOS (text mode restored).
- **Headless rig** (same Xvfb pattern as the Genesis): `Xvfb :97 &`, then
  `DISPLAY=:97 SDL_VIDEODRIVER=x11 dosbox build/dos/ur.exe`; find the window
  with `xdotool search --name 'UR.EXE'`, focus, tap keys with
  `keydown`/150 ms/`keyup`, shoot with `import -display :97 -window <id>`.

### Still to do

1. **Adlib/OPL2 FM** as an optional upgrade over the speaker (we already speak
   FM from the Genesis YM2612 work), with speaker fallback.
2. Token glide / dice animation (`plat_animate` is a stub, like the ST's).
3. Real-hardware online test (a physical FujiNet RS-232 on a real PC's COM
   port) — the emulated chain is proven; hardware should be config-only.
