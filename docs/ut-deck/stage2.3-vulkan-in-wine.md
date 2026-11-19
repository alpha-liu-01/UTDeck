# Stage 2.3: Vulkan inside Wine

Started and finished 2026-11-18. Plan: [stage2-roadmap.md](stage2-roadmap.md). Goal: a Windows Vulkan program
on screen through Wine's Vulkan layer (`winevulkan`) and our Turnip, closed and reopened several
times, windowed and fullscreen, without stale frames or crashes; the subsurface question answered.

Terms used here:
- *`winevulkan`* = Wine's Vulkan layer: it turns a Windows program's Vulkan calls into calls to
  the Linux Vulkan driver, and Windows surfaces (`VK_KHR_win32_surface`) into Wayland ones.
- *FIFO / MAILBOX* = Vulkan present modes: FIFO waits for the screen's refresh (vsync); MAILBOX
  does not wait and the compositor shows the newest frame (stage 1.4).

## Steps

| Step | What | Status |
| --- | --- | --- |
| 1 | A Windows Vulkan test program of our own (`vktest.exe`, x86-64 and ARM64) | **Done** |
| 2 | It runs through `winevulkan` on our Turnip, windowed and fullscreen, opened and closed repeatedly (screen off) | **Done**, with MAILBOX; FIFO deadlocks (below) |
| 3 | How the picture reaches Lomiri: subsurface, buffers, CPU per frame | **Done** |
| 4 | On screen: windowed, fullscreen three times, rotation, FIFO (manual run M2) | **Done**: works, no stale frames; rotation does not reach the program; FIFO deadlocks |
| 5 | Follow Lomiri's rotation (patch 0003) | **Done** (M3): fullscreen follows every rotation |
| 6 | Started as a Lomiri app with a landscape desktop file | **Done** (M4): landscape from the start, stays landscape; Lomiri's top bar over the picture |
| 7 | Ask again for fullscreen when Lomiri shrinks it (patch 0004) | **Done** (M5): the top bar goes within a second or two, also after app switches |

## Step 1: `vktest.exe`

No Windows Vulkan sample is at hand in a form we can use (the Khronos `vkcube` has no Windows
release; LunarG's SDK is large), so
[`tools/ut-probe/win/vktest.c`](../../tools/ut-probe/win/vktest.c) (new) does what a game does: a
Win32 window (windowed, or a borderless popup covering the screen as most games use for
"fullscreen"), `VK_KHR_win32_surface`, a swapchain sized from the surface, two frames in flight,
and recreation of the swapchain when the window changes size (resize, rotation). Each frame clears
to a slowly changing color and draws a white vertical bar that moves 8 pixels per frame (render-pass
clears only, no shaders), so a stale, frozen or torn picture is easy to see. `--cycles K` opens and
closes the window K times (the Dolphin port saw stale subsurface frames on Lomiri after closing).
It prints the surface's capabilities, the swapchain chosen, and the time between frames.
Built by [`win/build.sh`](../../tools/ut-probe/win/build.sh) for x86-64 (the case that matters:
games) and ARM64, against the Khronos Vulkan headers 1.4.335 (downloaded into `build/toolchains/`).
`vulkan-1.dll` is loaded at run time, as Wine provides it.

Problem met: under Wine, a Windows program's `stdout` going into a pipe is fully buffered, so a
program killed by a timeout loses all its output (it looked as if it had stopped before printing
anything). Both test programs now switch buffering off.

## Step 2: through `winevulkan` on our Turnip

Environment: stage 2.1's `env.sh` (our Khronos loader and Turnip) and `WAYLAND_DISPLAY=wayland-0`.
Wine's Vulkan layer finds our Turnip: "Turnip Adreno (TM) 619, Vulkan 1.3.354", from both the x86-64
build (through FEX) and the ARM64 build. The surface offers 4-16 images and **MAILBOX and FIFO**;
its `currentExtent` matches the window's client area (960x540 windowed, 1080x2400 fullscreen), so
the stage 0 problem of a 1x1 extent (libhybris' WSI) does not exist on this path.

| Run (screen off) | Result |
| --- | --- |
| windowed, FIFO, 300 frames (first try) | all presented |
| **fullscreen, FIFO** | **hangs after the first frame** |
| windowed, FIFO, 4 windows in a row (second try) | **hangs after the first frame** of the first window |
| fullscreen, MAILBOX (asked by the program), 3 windows in a row | all 3 x 240 frames presented, about 310 fps |
| fullscreen, FIFO asked, **`MESA_VK_WSI_PRESENT_MODE=mailbox`**, 3 windows | all presented, about 310 fps |
| windowed, MAILBOX, 4 windows in a row | all presented; 4 subsurfaces and 4 windows created and destroyed |

**The FIFO deadlock**, from `WAYLAND_DEBUG`: the game's subsurface commits its first frame and asks
for a frame callback (Lomiri's "frame done"); Mesa's Wayland WSI, in FIFO without `wp_fifo_v1`,
waits inside `vkQueuePresentKHR` for that callback before the next present (stage 1.4). The
callback never comes: meanwhile Lomiri sends the window a new size (`configure` 1080x2305, the
screen less the status bar), which Wine handles on the window's thread, the same thread that is
blocked in `vkQueuePresentKHR`; the `configure` is never acknowledged, and Lomiri seems to hold the
frame callback until it is. Whether it happens depends on whether that `configure` arrives before
or after the first present, which is why the same windowed run passed once and hung once. Most
games render on their window thread, so this would hit them.

What to do:
- **UTDeck forces MAILBOX** (`MESA_VK_WSI_PRESENT_MODE=mailbox`), already the default from stage
  1.4 for frame pacing: MAILBOX never waits for frame callbacks, and every MAILBOX run above
  passed. The game's own vsync setting is overridden; the frame limiter sets the pace.
- A safety net for later, in our Mesa: stop waiting for the frame callback after a short timeout in
  FIFO's wait loop (`wsi_wl_swapchain_queue_present`), so a compositor that withholds callbacks
  (this case, or a hidden window) cannot freeze a program. Not needed while MAILBOX is forced.

## Step 3: how the picture reaches Lomiri

From `WAYLAND_DEBUG` (fullscreen run): Wine creates the window (`xdg_toplevel`, fullscreen
requested), then a **subsurface** for the Vulkan picture (`wl_subcompositor.get_subsurface`,
`set_desync`), and our Turnip allocates the swapchain images as `android_wlegl` gralloc buffers
(5 x 1080x2400, format 5): the zero-copy path of stage 1.1, now inside Wine. Wine's own window
surface behind it gets one small `wl_shm` buffer. When the window closes, the subsurface and the
window are destroyed (4 of 4 in the cycles run).

CPU per frame (fullscreen, MAILBOX, output to a file, measured with
[`cputime.py`](../../tools/ut-probe/cputime.py), which leaves out `wineserver`): the difference
between a 1,500-frame and a 300-frame run is **0.81 ms per frame for the x86-64 build** (through
FEX) and 0.93 ms for the ARM64 build, about what native `vkcube` cost in stage 1.1 (0.98 ms):
Wine and FEX add little to presenting, because the work per frame is in the native driver.
Memory: 78 MB (x86-64) and 69 MB (ARM64) resident.

## Step 4: on screen

**Manual run M2 (screen on and unlocked, about 3 minutes):**
```
cd /home/phablet/utdeck-probe/wine && ./wine-m2.sh
```
([`wine-m2.sh`](../../tools/ut-probe/wine-m2.sh)) Four parts, one after the other; `vktest` shows a
color that slowly changes and a white bar moving across. Logs: `m2-<time>-a.log` to `-d.log`.

**M2 result (run by the maintainer, 2026-11-18).** What the maintainer saw, what `vktest` reported,
and what the logs show:

| Part | Seen on screen | `vktest` | From the logs |
| --- | --- | --- | --- |
| a: a window, 10 s | **whole, the bar moving smoothly** | 3,000 frames, 539 fps (MAILBOX), none lost | |
| b: fullscreen, 3 times | **only the first fills the screen**; in the next two Lomiri's top bar stays; the "unknown app" icon flashes between them; **no old picture left behind** | 3 x 1,000 frames, 221-234 fps | windows 2 and 3 open 30 ms after the previous one closed; for the third, Lomiri takes fullscreen away (`configure` 1080x2305, activated only) |
| c: fullscreen, rotated | **in landscape, the picture covers only the left ~45 % of the screen**, wallpaper beside it | 8,000 frames, 211 fps, **swapchain never recreated** | Lomiri asks for 2400x1080 at each rotation; Wine acknowledges it but keeps its 1080x2400 window (window geometry 1080x1080), so the program gets no resize |
| d: fullscreen, FIFO | **white at first**; after rotating, the picture appears and moves, more slowly | 600 frames, 46 fps | the FIFO deadlock of step 2; the rotation's new size unblocked it |

What it means:
- **The subsurface question is answered: no stale frames.** Three fullscreen windows and four
  windowed ones (step 2) opened and closed with nothing left behind; the picture goes through a
  subsurface with zero-copy buffers. The Dolphin port's trouble with subsurfaces does not appear
  with Wine's way of using them.
- **FIFO is out**, on screen as off: it deadlocks until some other event arrives, then runs at 46
  fps (Lomiri's late frame callbacks, stage 1.4). MAILBOX stays forced.
- **Rotation does not reach the program.** Wine takes the monitor's size only from `wl_output`'s
  mode, and Lomiri (Mir 1.8) keeps that at 1080x2400 with no rotation whatever the phone does (the
  Dolphin port found the same: "Lomiri does not report its rotation to the app", stage 6.6 there).
  Lomiri's only sign is the size it asks for in `configure`. So Wine keeps a portrait monitor and a
  portrait fullscreen window, and a landscape game gets a portrait picture in a landscape window.
  Games are landscape, so this must be fixed before 2.4 (below).
- **Lomiri does not know the program** ("unknown app" icon; app id `vktest-x86_64.exe`, with no
  desktop file). Probably also why it took fullscreen back from a window opened right after the
  previous one closed. In the product, Wine runs inside UTDeck's own app, which Lomiri knows; the
  quick re-open case matters little for games (one window), so this is noted, not chased now.

Next, to finish 2.3: **follow Lomiri's rotation in Wine's Wayland driver** (patch 0003): when
Lomiri configures a fullscreen window with the other orientation than the output reports (2400x1080
against 1080x2400), treat the output as rotated, so Wine's monitor becomes 2400x1080, the fullscreen
window is resized, and the game recreates its swapchain at the landscape size. To check: how Wine
resizes a fullscreen window when its monitor changes, and whether games see the change.

## Step 5: following Lomiri's rotation (patch 0003)

How Wine 11.16 handles it today, from its source:
- For a fullscreen window, `wayland_surface_config_is_compatible` accepts any size Lomiri asks
  for, and `wayland_configure_window` then keeps the window's size (`SWP_NOSIZE`), on purpose:
  "some applications are very insistent on a particular fullscreen size". It only crops what is
  shown (window geometry 1080x1080 in M2's landscape). So the program never hears of the rotation.
- Monitors come from `wl_output` modes only, and only the desktop process (`explorer`) updates
  them; the game's process is the one that receives the rotated `configure`. Rotating the monitor
  itself at run time would need the two processes to talk; left out for now.

[`0003-winewayland-follow-rotation.patch`](../../tools/wine-build/patches/0003-winewayland-follow-rotation.patch),
two small changes:
1. **A fullscreen window follows the orientation Lomiri asks for.** When the configured size has
   the other orientation than the window (wider than tall against taller than wide), Wine resizes
   the window to it instead of keeping its size, so the program gets `WM_SIZE` and recreates its
   swapchain. Same-orientation sizes are still left alone, as upstream intends.
2. **`UTDECK_WAYLAND_LANDSCAPE=1`** reports portrait output modes as landscape (1080x2400 becomes
   2400x1080), so Windows programs see a landscape monitor. Games pick their resolution from the
   monitor's modes, and a console is held in landscape; UTDeck will set it for games.

Result with the screen off (Lomiri treats the phone as portrait):

| `UTDECK_WAYLAND_LANDSCAPE` | Monitor and window | What happened |
| --- | --- | --- |
| 0 | 1080x2400 | as before: no rotation, nothing to follow |
| 1 | **2400x1080** | Lomiri asked for portrait; the window **followed the rotation to 1080x2400**; `vktest` got `WM_SIZE` and recreated its swapchain (2400x1080, then 1080x2305) |

1080x2305 is the screen less Lomiri's top bar: Lomiri sometimes keeps the bar over a fullscreen
window (part b of M2). Noted for later; it does not stop a game.

**Manual run M3 (screen on, rotation lock off; [`wine-m3.sh`](../../tools/ut-probe/wine-m3.sh)):**
a: start in portrait and rotate a few times; b: the landscape monitor (`UTDECK_WAYLAND_LANDSCAPE=1`),
phone held in landscape before the start, then rotate.

**M3 result (run by the maintainer, 2026-11-18):**

| Part | Seen on screen | `vktest` and the logs |
| --- | --- | --- |
| a | **the picture fills the screen after every rotation**, bar moving; the bar looks slower in landscape | 6 rotations, swapchain recreated 6 times (1080x2400 and 2400x1080 in turn), 8,000 frames, none lost |
| b | **at first only the left ~45 %** (wallpaper beside it); **after the first rotation to portrait, both orientations fill the screen** | started 2400x1080 as asked, but Lomiri configured the window **portrait** (1080x2400) at the start although the phone was held in landscape; the window followed that; from the first real rotation on, Lomiri's sizes were right and the window followed each one (4 recreations) |

- **Patch 0003 works**: a fullscreen Windows program follows every rotation and recreates its
  swapchain at the new size.
- The wrong start in b is Lomiri's: it starts a program it does not know (no desktop file: the
  "unknown app" splash) in portrait until the first rotation event, whatever way the phone is held.
- The bar looks slower in landscape for two reasons. It crosses 2,400 pixels instead of 1,080 at
  the same 8 pixels a frame, and the frame rate drops: counted from the log, **about 166 fps in
  landscape against about 215 fps in portrait**. The panel's native scanout is portrait, so in
  landscape Lomiri rotates every frame while compositing, which costs GPU time. With a 60 fps limit
  there is room to spare; to watch with real games in 2.4 (GPU load with and without rotation).

**Starting it as a Lomiri app.** The product runs Wine inside UTDeck's own app, which Lomiri knows,
so the test does the same: [`utdeck-vktest.desktop`](../../tools/ut-probe/utdeck-vktest.desktop)
(in `~/.local/share/applications`, next to `dolphin-ut.desktop`; it shows up in the app drawer as
"UTDeck vktest (test)") with `X-Lomiri-Supported-Orientations=landscape`, the key Lomiri's launcher
(`liblomiri-app-launch`) reads, running [`vktest-app.sh`](../../tools/ut-probe/vktest-app.sh):
`vktest` fullscreen, MAILBOX, landscape monitor, every Wayland message logged (`m4-<time>.log`).
Two problems met while making it work, both things UTDeck's launcher must do too:
- **`DISPLAY` must be unset.** Lomiri's launcher gives legacy apps an X11 display (Xwayland);
  with `DISPLAY` set, Wine picks its X11 driver, and `vkCreateInstance` failed with
  `VK_ERROR_EXTENSION_NOT_PRESENT` (our Turnip has no X11 surface). Wine must use its Wayland driver.
- **Wine's background processes must be ended** (`wineserver -k`) when the program exits: they
  live in the app's process group, so Lomiri saw the app as still running for a minute and refused
  to start it again (single instance) until systemd killed them.

Started with `lomiri-app-launch utdeck-vktest` (screen off): **Lomiri knew the app** (it tied
Wine's window to `utdeck-vktest`, although Wine still sets the app id `vktest-x86_64.exe`) and
**turned it to landscape by itself** (`configure` 2400x1017) without any rotation; `vktest` ran
6,000 frames at 2400x1080, 237 fps, and the app ended cleanly. 1017 is 1080 less 63 pixels: Lomiri's
top bar again, over a fullscreen window; the bottom 63 rows of the picture would be cut. To look at
on screen.

**Manual run M4 (screen on and unlocked):** open the app drawer and tap **"UTDeck vktest (test)"**
(it runs about 25 seconds and closes by itself). Do it twice: once holding the phone **upright**,
once **sideways**.
1. Does it start in landscape and fill the screen both times (no wallpaper beside it)? Yes. 
2. Is Lomiri's top bar over the picture? Yes. I can see the top bar both times.
3. While it runs, turning the phone: does it stay landscape (it should: the app allows only
   landscape)? Yes. 
Logs: `m4-<time>.log` in the `wine` folder, one per start.

**M4 result (run by the maintainer, 2026-11-18):** started from the drawer, upright and sideways:
**it starts in landscape and fills the screen both times, and stays landscape when the phone is
turned**; but **Lomiri's top bar is over the picture both times**. Wine's trace shows Lomiri
granted the fullscreen state (`0x8`) at 2400x1017, the screen less the 63 px panel.

The Dolphin port met exactly this (its `stage5-packaging.md`, "Lomiri keeps one window state per
app"): for apps it manages (started from the drawer, not from a shell), Lomiri saves a window state
per app and gives the next window that state, and a fullscreen window can come at 2400x1017 with
the panel over it. Dolphin's fix (`GameWindow::CheckFullscreen`): once the window's size has been
stable for 0.5 s, if it does not cover the screen, ask for fullscreen again (unset, then set), at
most three times, the count reset when it covers the screen.

## Step 7: asking again for fullscreen (patch 0004)

[`0004-winewayland-fullscreen-again-when-shrunk.patch`](../../tools/wine-build/patches/0004-winewayland-fullscreen-again-when-shrunk.patch)
does the same inside Wine's Wayland driver, so every game gets it: each `configure` pushes the
check 0.5 s ahead (sizes settle in steps; a rotation passes through intermediate sizes); when a
frame is presented after that (`wayland_client_surface_present`, the path of every Vulkan, zink or
other GL present), if the acknowledged fullscreen size does not cover the output (long and short
sides compared, so it works with either orientation), Wine sends `unset_fullscreen` and
`set_fullscreen`, at most three times; the count resets when the window covers the output. A first
version asked again at once, while still handling the `configure`; reworked to Dolphin's timing.

With the screen off, through Lomiri's launcher: Wine asked again three times, 0.5 s apart, and
Lomiri answered 2400x1017 each time. The Dolphin notes say Lomiri's sizes differ with the screen
off (stage 0 and 1 there: the 2305 reconfigure happened only with the screen off), so the answer
must come from the screen.

**Manual run M5 (screen on and unlocked):** from the app drawer, tap **"UTDeck vktest (test)"**
(about 25 seconds), once upright and once sideways. Each time:
1. Is Lomiri's top bar over the picture at the start, and does it go away within a second or two? Yes. 
2. While it runs, swipe to another app (or the app drawer) and back: is the picture full screen
   again, without the top bar? Yes. 
Logs: `m4-<time>.log`, one per start.

**M5 result (run by the maintainer, 2026-11-18): both yes.** From the drawer, upright and sideways:
Lomiri's top bar is over the picture at the start and **goes away within a second or two**, and
after switching to another app and back **the picture is full screen again** without the bar. The
logs (`m4-182458.log`, `m4-182537.log`): Wine asked again for fullscreen up to three times; each run
ends with `configure` 2400x1080, fullscreen. Switching away and back made Lomiri pass through
portrait sizes (1080x2400) and 2400x1017; the window followed them (patch 0003, four swapchain
recreations in the first run) and came back to 2400x1080. The longest frame, during a switch, was
179 ms; 6,000 frames presented each time.

## Result

**Stage 2.3 is done (2026-11-18).** A Windows Vulkan program (x86-64, through FEX) runs on Lomiri
through Wine's Vulkan layer and our Turnip: windowed and fullscreen, opened and closed repeatedly
with **no stale frames** (the subsurface question is answered), zero-copy `android_wlegl` buffers,
**0.81 ms of CPU per frame**, about 200-230 fps for a clear-only picture. Started as a Lomiri app,
it comes up landscape, full screen, and keeps it through rotation and app switches.

What a game launch needs, learned here (for UTDeck's launcher, stage 4, and the defaults, stage 3):

| Setting | Why |
| --- | --- |
| `MESA_VK_WSI_PRESENT_MODE=mailbox` | FIFO deadlocks under Wine on Lomiri (and paces badly, stage 1.4) |
| `UTDECK_WAYLAND_LANDSCAPE=1` | Windows sees a landscape monitor, as games expect |
| a desktop file with `X-Lomiri-Supported-Orientations=landscape` (UTDeck's own app) | Lomiri starts the game in landscape and keeps it there; an unknown app starts portrait |
| `DISPLAY` unset | otherwise Wine picks its X11 driver, and Vulkan has no surface for it |
| `wineserver -k` when the game exits | Wine's background processes keep the app "running" for Lomiri |
| our `winewayland.so` (patches 0001-0004) | no `wp_viewporter`, touch, rotation, Lomiri's shrunk fullscreen |

Open, for later: a timeout on Mesa's FIFO wait as a safety net; landscape costs about a quarter of
the frame rate in Lomiri's compositing (watch GPU load with real games, 2.4); rotating Wine's
monitor at run time (needs the desktop process to hear of the rotation); Lomiri's top bar over a
fullscreen window started from a shell (unmanaged), which the product does not do.

## Events

- 2026-11-18: `vktest.c` written (Win32 window, swapchain, moving bar, cycles, timing), built for
  x86-64 and ARM64 with the Khronos headers; runs through `winevulkan` on our Turnip from both. Wine
  buffers `stdout` into pipes: switched off. FIFO deadlock found and traced (frame callback held by
  Lomiri while the window thread is blocked in present); MAILBOX passes every run, forced or asked.
  Subsurface and `android_wlegl` buffers confirmed; 0.81 ms CPU per frame (x86-64). Manual run M2
  written.
- 2026-11-18: M2 run by the maintainer: windowed and fullscreen Vulkan drawn, no stale frames
  between windows (subsurface question answered); landscape shows the picture in the left 45 %
  (Lomiri never reports rotation in `wl_output`, Wine keeps a portrait monitor); FIFO deadlocks on
  screen too. Next: patch 0003, rotation from Lomiri's `configure` sizes.
- 2026-11-18: patch 0003 written (fullscreen windows follow a configure of the other orientation;
  `UTDECK_WAYLAND_LANDSCAPE`), built and installed; with the screen off the window follows Lomiri's
  portrait configure and the program recreates its swapchain. Manual run M3 written.
- 2026-11-18: M3 run by the maintainer: patch 0003 works (fullscreen follows every rotation);
  Lomiri starts an unknown app in portrait until the first rotation; landscape costs about a
  quarter of the frame rate in compositing (166 against 215 fps). Test launcher
  `utdeck-vktest.desktop` made: `DISPLAY` must be unset, `wineserver -k` at the end; started by
  `lomiri-app-launch`, Lomiri knows the app and turns it to landscape by itself. Manual run M4
  written.
- 2026-11-18: M4 run by the maintainer: landscape from the drawer, both ways the phone is held,
  stays landscape; Lomiri's top bar over the picture (fullscreen at 2400x1017, the Dolphin port's
  stage 5 problem). Patch 0004 written (ask again for fullscreen, Dolphin's timing). Screen off,
  Lomiri keeps 2400x1017; manual run M5 written.
- 2026-11-18: M5 run by the maintainer: the top bar goes away within a second or two, and the
  picture is full screen again after app switches (logs end at 2400x1080 fullscreen). Stage 2.3
  done.
