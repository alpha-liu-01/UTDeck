# Stage 5.2: the frame limiter, the plan

Started 2026-11-24, right after 5.1 (the maintainer). Goal (from [stage5-roadmap.md](stage5-roadmap.md)):
a frame rate cap per game and for all games, set in the app: steadier frame times, less heat, a
longer battery. Exit: on one game, uncapped against 30, 40 and 60 fps: the frame time spread, the
temperatures and the battery's draw over 10 minutes written down; the cap set in the app reaches the
game.

## Terms used here

- *frame limiter* (or *cap*) = something that makes a game wait between pictures so that it draws no
  more than N a second. It does not make a game faster; it stops it from drawing pictures nobody sees
  and lets the phone rest in between.
- *frame time* = the time between two pictures; *frame pacing* = how even those times are. 30 fps
  with every picture 33 ms apart feels smoother than 40 fps that swings between 16 and 50 ms.
- *refresh rate* = how often the screen shows a new picture (60 Hz = every 16.7 ms).
- *present mode* = how Vulkan hands pictures to the screen. *FIFO* waits for the screen (vsync: never
  more pictures than refreshes); *MAILBOX* never waits: a newer picture replaces an unshown one.
- *judder* = uneven motion from pictures that do not line up with the screen's refreshes (a cap of 40
  on a 60 Hz screen shows pictures for 1, 2, 1, 2 ... refreshes).

## What we know (2026-11-24)

- **The screen.** The Nothing Phone (1)'s panel offers 60, 90 and 120 Hz
  (`/sys/class/drm/card0-DSI-1/modes`: `1080x2400x60`, `x90`, `x120`); Lomiri sets 60 Hz (the kernel
  logs `fps=60` at each mode set). stone: still to read.
- **No vsync in our games.** UTDeck forces MAILBOX (`MESA_VK_WSI_PRESENT_MODE=mailbox`,
  `defaults.env`): FIFO deadlocks under Wine on Lomiri (stage 2.3: Mesa's Wayland WSI waits in
  `vkQueuePresentKHR` for Lomiri's frame callback, which Lomiri holds until the window acknowledges a
  `configure` that the same, blocked thread would have to handle). Stage 2.3 already said it: "the
  game's own vsync setting is overridden; the frame limiter sets the pace". Until now nothing did.
- **What that wastes.** A light scene draws as fast as it can: wiliwili's home page at about 100 fps
  on the 60 Hz screen (5.1), so about 40 % of its pictures were never shown; Stardew Valley's title
  screen at 51 fps.
- **Three limiters at hand**, all in the runtime already:
  - DXVK's (`DXVK_FRAME_RATE=N`, Direct3D 8-11): sleeps until the next picture's time
    (`util_fps_limiter.cpp`, `Sleep::sleepUntil`).
  - VKD3D-Proton's (`VKD3D_FRAME_RATE=N`, Direct3D 12, `swapchain.c`).
  - MangoHud's (`fps_limit=N` in its settings file; 5.1 put it in the runtime): for every game that
    reaches the screen through Vulkan, OpenGL through zink included; it sleeps either before the
    picture is handed over (`fps_limit_method=early`) or after (`late`, the default), and corrects for
    how late its sleeps wake. It needs MangoHud loaded, which costs about 0.06 ms a picture with the
    overlay hidden (5.1).
- **Measuring.** Wine's frame log (patch 0007) gives one line a second (count, mean and longest frame
  time), and none for OpenGL games (zink presents past it, 5.1). Pacing needs every frame's time:
  MangoHud's own log (`output_folder`, `log_interval=0`: one line per picture, its frame time) gives
  that for every API, used as a test tool here (whether UTDeck keeps such logs stays 5.4's question).
  The sampler (fixed in 5.1) gives clocks, temperatures and the battery's current every 2 s; the
  battery's draw needs the phone unplugged (charging hides it), and the Nothing Phone (1) reports its
  current (stone reports 0).

## Steps

1. **The limiters as they are.** One game above 60 fps on each API we have: wiliwili (Direct3D 11,
   about 100 fps; short runs on the Nothing Phone (1), whose screen flashed after about 17 minutes of
   it in 5.1) and Stardew Valley (OpenGL, about 51 fps on its title screen; caps of 30 and 40). Each
   cap through each limiter that applies (DXVK's against MangoHud's early and late; MangoHud's alone
   for OpenGL), MangoHud logging every frame: the mean frame time, its spread (standard deviation,
   the 1 % and 0.1 % longest), the CPU's load and clocks (a limiter should sleep, not spin). Written
   down per cap and limiter.
2. **The choice of limiter**: the evenest frame times at the least CPU, the same tool for every API
   if it is as good (one place to maintain), and what a cap off the screen's rate (40 or 45 on 60 Hz)
   looks like on screen (judder), judged by the maintainer.
3. **The exit measurement**: one game, uncapped against 30, 40 and 60, 10 minutes each, unplugged:
   the frame time spread, the temperatures, the battery's draw (the sampler's current).
4. **In the app**: "Frame rate limit" on a game's settings page and in Settings for all games (the
   same way as the overlay: `UTDECK_FPS_LIMIT` in `utdeck-env` and `user.env`), turned into the
   chosen limiter's setting by `game-run.sh`.
5. **Tests**: the cap from the app reaching the game on each API; a runtime release only if the
   runtime changes (a MangoHud setting needs none).

## Questions for the maintainer

1. **The caps offered**: Off, 30, 40, 60? On a 60 Hz screen 30 and 60 line up with the refreshes; 40
   and 45 do not, and may judder (step 2 shows how much). The Steam Deck offers more steps, but its
   screen's rate follows the cap, which Lomiri's does not.
2. **The default for all games**: off (as now), or 60, the screen's own rate? A cap of 60 hides
   nothing (the screen shows no more than 60 anyway) and saves the pictures drawn for nothing (40 %
   of wiliwili's). Recommended: 60, if step 1 shows it costs no evenness.
3. **The screen's refresh rate**: Lomiri keeps the panel at 60 Hz although it offers 90 and 120. A
   higher rate would let fast games show more, at more power; it is a display setting (the vendor's
   own modes, so within the hardware rule) and a Lomiri question. Left out of 5.2, noted for later?

## Status

- 2026-11-24: planned (this file), from the facts above.

## Answers from the maintainer (2026-11-24)

1. **The caps**: Off, 28 (144/5), 30 (60/2), 40 (120/3), 45 (90/2), 48 (144/3), 60: each divides one
   of the refresh rates phones' panels offer (60, 90, 120, 144 Hz).
2. **The default: Off.**
3. **A cap that follows the screen, as on the Steam Deck.** The maintainer asked for a variable cap
   (30 to 60). The Deck's cap is smooth at every value because the panel's refresh rate follows it
   (40-60 Hz on the LCD model); it does not use FreeSync on its own screen. A cap alone can be any
   number (MangoHud takes any value), but on a screen held at one rate only its divisors show every
   picture for the same time; the others judder. Phones' "variable refresh" (LTPO panels) is the
   compositor choosing among the panel's fixed modes (Android's `setFrameRate`, through the display
   HAL), not per-frame sync like FreeSync. So the way to a Deck-like cap here is Lomiri/Mir switching
   the panel to a mode the cap divides while a game runs (45 fps on 90 Hz, 40 on 120, 48 on 144).
   Noted as an experiment for the stone port (the maintainer's stone-ut repository: Lomiri runs at
   120 Hz there), outside 5.2. The Nothing Phone (1) is not LTPO (60/90/120 only); stone's modes to
   be read when it is connected.

So 5.2 offers the fixed list above; step 2 measures how much the caps that do not divide 60
(28, 40, 45, 48) judder at the Nothing Phone (1)'s 60 Hz.

## Step 1: the limiters as they are (2026-11-24)

**How it is measured.** The game gets MangoHud loaded with the overlay hidden (`no_display=1`), its
log writing one row per picture (`log_interval=0`: the picture's frame time, the CPU's load), and a
script on the phone (`cycle.sh`) changes the cap in MangoHud's settings file every 40 seconds,
writing down when; MangoHud reloads the file and sets its limiter up again, so one run of the game
goes through every cap and both methods. The first 3 seconds of each segment are left out (the cap
settling). Two catches on the way:
- With the overlay hidden, MangoHud's automatic log (`autostart_log`) never starts: MangoHud skips its
  per-picture bookkeeping while hidden unless a log runs, and that bookkeeping is where the automatic
  log would start. The log is started through the control socket instead (`:logging=1;`).
- A reload of the settings file stops a running log (MangoHud writes its summary then). The script
  starts a new log after each change: one log file per segment.
- A third catch: each reload opens MangoHud's control socket again under its name, which the old
  socket still holds, so after the first reload no socket was left, and the small socket tool hung
  on `connect` (it had a timeout only on reading). Each segment's settings now name the socket anew
  (`control=mangohud-%p-<n>`), the tool times out on `connect` too, and the script runs detached
  from `adb` (`setsid nohup`): the Nothing Phone (1) dropped off USB three times that day, each time
  during a test.

**Stardew Valley** (OpenGL through zink; Nothing Phone (1), title screen, untouched; MangoHud's
limiter; 40-second segments, 22:15-22:21):

| Cap | Method | Frame rate | Mean frame | Spread (SD) | 1 % / 0.1 % longest | CPU load |
| --- | --- | --- | --- | --- | --- | --- |
| off | | 49.05 | 20.38 ms | 1.20 ms | 23.30 / 25.57 ms | 21.0 % |
| 28 | late | 28.03 | 35.71 ms | 0.78 ms | 37.66 / 38.49 ms | 14.4 % |
| 28 | early | 28.02 | 35.72 ms | 0.28 ms | 36.19 / 39.06 ms | 14.3 % |
| 30 | late | 30.03 | 33.33 ms | 0.93 ms | 34.91 / 38.82 ms | 14.6 % |
| 30 | early | 30.02 | 33.33 ms | 0.18 ms | 33.80 / 34.07 ms | 14.4 % |
| 40 | late | 40.03 | 25.00 ms | 0.69 ms | 26.57 / 27.56 ms | 16.2 % |
| 40 | early | 40.02 | 25.00 ms | 0.22 ms | 25.46 / 27.11 ms | 16.1 % |
| 45 | late | 45.02 | 22.22 ms | 0.98 ms | 24.28 / 25.75 ms | 17.0 % |
| 45 | early | 44.97 | 22.24 ms | 0.62 ms | 22.54 / 29.19 ms | 16.4 % |

- MangoHud's limiter holds every cap almost exactly.
- **Early** (the sleep before the picture is handed over) is three to five times evener than late
  (MangoHud's default) at the same CPU load; at 45 the gap narrows, as Stardew reaches only about 49
  uncapped (little room left).
- The CPU's load falls from 21 % uncapped to 14-17 %: the limiter sleeps, it does not spin.
- These are the times at which the game hands its pictures over. What the 60 Hz screen shows of 28,
  40 and 45 (judder, MAILBOX) is step 2's question, by eye.

**wiliwili** (Direct3D 11 through DXVK; Nothing Phone (1), home page, untouched; about 88 fps
uncapped). MangoHud's limiter (early) in one run (40-second segments, 22:25-22:28); DXVK's own
(`DXVK_FRAME_RATE`) in three runs, one per value (DXVK reads it at the start), MangoHud loaded only to
log (44 seconds each; the 60 run after a restart of the phone, which wiliwili's flashing had made
necessary):

| Cap | Limiter | Frame rate | Mean frame | Spread (SD) | 1 % / 0.1 % longest | Longest | CPU load |
| --- | --- | --- | --- | --- | --- | --- | --- |
| off | | 87.78 | 11.39 ms | 2.49 ms | 15.68 / 18.76 ms | 20.04 ms | 22.0 % |
| 30 | MangoHud, early | 30.03 | 33.33 ms | 0.15 ms | 33.83 / 34.04 ms | 34.26 ms | 12.4 % |
| 30 | DXVK | 30.02 | 33.33 ms | 1.83 ms | 36.26 / 36.89 ms | 72.05 ms | 16.3 % |
| 45 | MangoHud, early | 45.03 | 22.22 ms | 0.12 ms | 22.63 / 22.88 ms | 23.17 ms | 15.0 % |
| 45 | DXVK | 45.02 | 22.22 ms | 1.08 ms | 24.80 / 25.85 ms | 26.24 ms | 16.7 % |
| 60 | MangoHud, early | 60.02 | 16.66 ms | 0.13 ms | 17.09 / 17.43 ms | 17.57 ms | 18.2 % |
| 60 | DXVK | 60.05 | 16.66 ms | 1.90 ms | 21.85 / 26.79 ms | 29.31 ms | 21.2 % |

- Both hold the cap; **MangoHud's (early) is 7 to 15 times evener than DXVK's**, with a lower CPU load
  (1.7-3.9 points), and its longest frame stays within 1 ms of the target where DXVK's reaches 29-72 ms.
- With room above the cap (wiliwili), MangoHud's early pacing is almost perfect (0.12-0.15 ms); with
  little room (Stardew at 45, 49 uncapped) it is still the evenest.

**The choice (step 2's first half): MangoHud's limiter, early**, for every game: the evenest, the
least CPU, and one limiter for all the APIs (Direct3D 8-12 and OpenGL alike), so one place to look
after. Its cost: MangoHud loaded in every capped game, about 0.06 ms a picture while hidden (5.1).
DXVK's and VKD3D-Proton's own limiters are not used. Left for step 2's second half: how the caps that
do not divide 60 (28, 40, 45, 48) look on the 60 Hz screen, by eye.

**By eye** (step 2's second half; Stardew Valley, walking on the farm, the frame rate shown at the top
center, MangoHud's limiter early, 30 seconds per cap: 30, 40, 45, 48, 28, 30, then uncapped at about
55; the Nothing Phone (1)'s screen at 60 Hz). The maintainer: from 48 to 28 it felt significantly
less smooth, from 30 to uncapped much smoother; the other steps (30, 40, 45, 48) mattered less. So
what shows is mostly the frame rate itself; the caps that do not divide 60 (40, 45, 48) showed no
judder that stood out against 30. The cap works on screen. (A first try did nothing: the maintainer
had set Stardew's overlay to a level in the app meanwhile, so `game-run.sh` wrote its own settings
file for the run and pointed MangoHud at it, past the test's file. To keep in mind for step 4: a
game's own MangoHud settings file in its `utdeck-env` is replaced when the overlay is on, and the
layer's name is then listed twice in `VK_LOADER_LAYERS_ENABLE`; harmless, tidied in step 4.)

## Step 4: in the app (2026-11-24)

The exit run (step 3) is left for later: Stardew Valley reaches only about 55 fps, so a cap of 60
would change nothing there, and wiliwili flashes on the Nothing Phone (1) within one 10-minute run;
the maintainer looks for a better game first.

- **The setting**: `UTDECK_FPS_LIMIT` = `off` (the default), `28`, `30`, `40`, `45`, `48` or `60`, built
  like the overlay's: a game's own on its settings page ("Frame rate limit": As in Settings, Off, then
  each value), a line in its `utdeck-env` only when it differs from "As in Settings"; for all games in
  Settings ("Frame rate limit", "For all games"), in `user.env`.
- **`game-run.sh`** writes the overlay and the limit into one settings file for the run: with a limit,
  `fps_limit=N` and `fps_limit_method=early` (step 1's choice); with a limit and the overlay off,
  MangoHud loaded hidden (`no_display=1`); both off, nothing set and nothing loaded, as before. The
  game's log says "frame rate limit: N". Tidied on the way: the layer's name once in
  `VK_LOADER_LAYERS_ENABLE` (a game's own settings could list it a second time), and the file's
  `printf '%s\n'` lines (they held a real line break where `\n` was meant; the shell read both the same).
- Checked on the PC (the block cut out of the script, every combination): a limit alone (hidden
  MangoHud, `fps_limit` and `early`), both together, neither (nothing set), an unknown value (logged and
  left off), the layer named twice (once afterwards, other layers kept), Steam's install scripts
  (neither).
- App 0.3.54; no runtime release (MangoHud's limiter is in `runtime-11.16-20261124.2` already).

App 0.3.54 (the maintainer's OK) on the Nothing Phone (1), checked in Stardew Valley. The frame rate
is counted without the overlay too: Lomiri logs one `wl_subsurface.visiblity not implemented` line
for every picture a game hands over (5.1), so its journal over 10 seconds counts the pictures.

| Check | Result |
| --- | --- |
| the app puts the new `game-run.sh` in place | the same as the repository's |
| Stardew's own limit 40, its overlay "Frame rate and frame time" | the overlay showed 40 fps; the log "frame rate limit: 40"; the run's file `fps_limit=40`, `fps_limit_method=early`; the layer's name once in `VK_LOADER_LAYERS_ENABLE` |
| Stardew's own limit 30, its overlay Off | no overlay; MangoHud loaded, hidden (`no_display=1`, `fps_limit=30`); Lomiri: 300 pictures in 10 s, 30 fps |
| Stardew "As in Settings", Settings 45 for all games | `user.env` `UTDECK_FPS_LIMIT=45`; the log "user settings: ... UTDECK_FPS_LIMIT=45", "frame rate limit: 45"; Lomiri: 450 pictures in 10 s, 45 fps |

## The panel follows the limit (2026-11-27, with giuliac-ut's stage 3 item 17)

The Steam Deck's way from the answers above, built on the OnePlus Ace 5 (giuliac-ut, the maintainer's
port: 60, 90 and 120 Hz modes, switched live by its display service). The details and every
measurement are in giuliac-ut's `docs/stage3-display-performance.md`, item 17; in short:

- **The interface**: `io.github.alphaliu01.utdeck.DisplayTiming1` on the system bus, in UTDeck's
  namespace (the maintainer's choice), so any port can offer it and UTDeck never names a phone. A
  phone's display service answers `RequestCap(fps, pid)` by setting the panel to its lowest mode that
  is a whole multiple of the limit, for as long as process pid lives, and gives the exact limit for
  that mode and a timing file (`/run/utdeck/display-timing`: the panel's period, a recent vsync, the
  hand-over offset). giuliac-ut's service is the reference implementation.
- **A panel's real rates are not round**: the Ace 5's run 0.15 % fast (90.131 Hz), so a limit of
  45.000 drifts across the refreshes (one frame in nine shown a refresh off, in bursts every 8 s). The
  service measures the rates; 45 becomes 45.0658 there. MangoHud's `fps_limit` in its settings file is
  a float, so the exact value can be given.
- **MangoHud's limiter random-walks**: it sleeps relative to its last sleep, so even at the exact limit
  its phase wanders (vkcube on Turnip at 45.0665: 34 % of frames off). Patch 0004
  (`tools/mangohud-build/patches/0004-fps-limit-on-the-display-s-refresh-grid.patch`) puts each
  deadline on the panel's grid from the timing file (`UTDECK_DISPLAY_TIMING`), re-read every 250 ms;
  without the file nothing changes.
- **The phase per game**: Lomiri commits a frame about 19.5 ms after it is handed over, so the hand-over's
  phase decides which refresh shows it, and the right phase depends on the game's own present path
  (5.5 ms apart between two test clients). The service finds it from the frames retired to the panel
  (a search of about 14 s at the game's start).
- **`game-run.sh`**: with a limit and the service present, calls `RequestCap` with its own process (it
  waits for the game), writes the exact limit as `fps_limit`, exports `UTDECK_DISPLAY_TIMING`, and logs
  "display: limit 45 as 45.0609 fps at 90.1217 Hz ...". No service (the Nothing Phone (1), stone): as before.

Measured on the Ace 5 after the search (vkcube on Turnip, MangoHud hidden with patch 0004): 30 and 60
fps on 60 Hz, 40 on 120 Hz and 45 on 90 Hz with 0 to 2 % of frames a refresh off. 28 and 48 fit no mode
there (nothing changes, no file). Not yet run: a real game through the app, which needs UTDeck on the
Ace 5 (giuliac-ut's item 13) and a runtime release with MangoHud built with patch 0004; Steam's launch
(`userspace/steam/steam-in-container.sh`) does not ask for the panel yet.

## Status

- 2026-11-24: planned; the maintainer's answers (the caps, Off by default, the Steam Deck's way noted
  for the stone port). Step 1: MangoHud's limiter (early) chosen over its late method and DXVK's own.
  Step 2: by eye, the frame rate itself shows, 40/45/48 show no judder that stands out at 60 Hz. Step
  4: in the app (0.3.54), checked on the Nothing Phone (1). Left: step 3, the exit run, when the
  maintainer has found a game that runs above 60 fps without trouble; stone.
- 2026-11-27: the panel follows the limit where the phone offers `io.github.alphaliu01.utdeck.DisplayTiming1`
  (giuliac-ut, the OnePlus Ace 5): MangoHud patch 0004 and `game-run.sh`; 0 to 2 % of frames off at 30, 40,
  45 and 60 fps with a test client. Left: a game through the app there, and a runtime release with the patch.
- 2026-11-28: runtime `11.16-20261128` published (pre-release at 14d0254: the 2026-11-25 build, its Wine tree
  checked against every patch, with MangoHud rebuilt with patch 0004; SHA-256 `232582fa...`); `runtimes.list`
  points at it; installed on the OnePlus Ace 5 with `get-runtime.sh default` (downloaded and checked).
