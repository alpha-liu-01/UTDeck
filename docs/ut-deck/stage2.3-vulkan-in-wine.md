# Stage 2.3: Vulkan inside Wine

Started 2026-11-18. Plan: [stage2-roadmap.md](stage2-roadmap.md). Goal: a Windows Vulkan program
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
| 5 | Follow Lomiri's rotation (patch 0003) | Next |

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
