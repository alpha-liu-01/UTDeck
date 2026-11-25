# Stage 5.1: the performance overlay, the plan

Written 2026-11-24, as the first substage of stage 5 ([stage5-roadmap.md](stage5-roadmap.md)). Goal:
while a game runs, its picture can show how it runs, as on a Steam Deck: frame rate and frame time
first, then CPU and GPU load and clocks, temperatures, memory and battery. Switched on from the app,
per game or for all games. It is what the next substages (the frame limiter, Steam's memory) and the
game round (4.10) are measured with.

Exit: on the Nothing Phone (1) and stone, the overlay switched on from the app in a Direct3D 11, a
Direct3D 12 and an OpenGL game; its numbers match the phone's own files; its cost on the frame rate
measured and small (the aim: under 3 %); the `.sensors` files filled on both phones.

## Terms used here

- *MangoHud* = an open source overlay for Linux games (MIT license), a Vulkan layer that draws with
  Dear ImGui (a small drawing library) on each picture before it is shown; the Steam Deck's overlay
  grew from it. Pinned: release **v0.8.4** (2026-05-27).
- *implicit layer* = a Vulkan layer the loader loads by itself when a variable says so (MangoHud's:
  `MANGOHUD=1`), found through a small JSON file; `VK_ADD_IMPLICIT_LAYER_PATH` adds a folder to look
  in (our loader, Khronos 1.3.275 in the runtime's `lib/gpu/loader`, knows it).
- *control socket* = a socket MangoHud opens when asked (`control=` in its settings), through which
  another program can show or hide it while the game runs.
- *thermal zone* = one temperature the kernel reports (`/sys/class/thermal/thermal_zone*/`), known by
  its `type` name; the numbers of the zones differ from phone to phone, the names less so.

## Why MangoHud, and why it needs work here

Every game here reaches the screen through Vulkan on Turnip: Direct3D 9-11 through DXVK, Direct3D 12
through VKD3D-Proton, OpenGL through zink, Vulkan games directly. So one Vulkan layer covers all of
them, with no hook per graphics API. MangoHud is that layer, with a frame limiter (5.2) and a logger
built in, and it is maintained for ARM devices that run Windows games through FEX (it reads FEX's
statistics, `src/fex.cpp`, and has code for Valve's Snapdragon-based "Steam Frame").

What it does not know (read in its source, `src/gpu.cpp`, `src/gpu_fdinfo.cpp`, and on the Nothing
Phone (1), 2026-11-24):
- **The GPU** (corrected in step 1: the release reads KGSL; this paragraph came from the master
  branch's code). MangoHud finds GPUs through `/sys/class/drm`; its Qualcomm support is for mainline's
  `msm_drm` driver. On a Halium phone the GPU is KGSL (`/dev/kgsl-3d0`), and `/sys/class/drm/card0` is
  the **display** (Qualcomm's `mdss_mdp`/`sde`), whose driver is also called `msm_drm`. So MangoHud
  would take the display for the GPU and read numbers that do not exist on this kernel. The real ones
  are in `/sys/class/kgsl/kgsl-3d0/`, readable by the user: `gpu_busy_percentage` (load),
  `gpuclk` and `max_gpuclk` (clock, and its allowed maximum, which drops when the phone is hot),
  `temp`, `throttling`, `gpu_model`.
- **The phone's temperatures** come from thermal zones by name (`cpuss-0-usr`, `gpuss-0-usr`,
  `sdm-skin-therm-usr` on the Nothing Phone (1)), not from the PC sensors MangoHud reads (`hwmon`).
- **The CPU's layout**: phones have two or three clusters of different cores (the Nothing Phone (1):
  `policy0`, `policy4`, `policy7`; stone: `policy0`, `policy6`).
- **Changing things while the game runs**: its control socket takes `hud` (show or hide), `logging`
  and `fcat` only (`src/control.cpp`); whether it reloads its settings file while running is checked
  in step 1. A live switch from inside a game is stage 6's in-game menu; here the overlay is chosen
  before the game starts (and a keyboard's Shift_R+F12 shows or hides it, if that works on Lomiri).

## Steps

1. **MangoHud as it is, on the phone (nothing installed for good).** Build v0.8.4 for ARM64 in our
   cross container (as Mesa's build: meson, the `cross-aarch64.ini` file; X11, D-Bus and the NVIDIA
   parts left out; `mangohudctl` built for the test). Copied by hand into a test folder on the Nothing
   Phone (1), loaded through `VK_ADD_IMPLICIT_LAYER_PATH` and `MANGOHUD=1` for one game at a time.
   Found out, in order: does it draw (vktest, then a DXVK game, then wiliwili through zink)? What
   each number shows against the phone's own files? Does the game start and quit as before? Its cost:
   the same scene with and without it, from patch 0007's `.frames` (frame rate and frame times) and the
   sampler. Do the keyboard toggle, the control socket and a settings reload work? Do FEX's statistics
   appear?
2. **The phone's numbers in MangoHud.** Patches in `tools/mangohud-build/patches/`:
   - the GPU from KGSL (load, clock, its current maximum, temperature, throttling, the model's name);
     Android's display-only `msm_drm` node ignored;
   - CPU and GPU temperatures from thermal zones found by name (a short list per name, with
     fallbacks; nothing written in for one phone), the skin temperature as an extra line;
   - the CPU per cluster (load and clock of each `policy*`), memory as one figure (phones share RAM
     between CPU and GPU: no VRAM line), the battery from `power_supply/battery`.
   **The sampler fixed the same way** (`game-run.sh`): clusters from the `policy*` folders, zones by
   name, each reading with a fallback, so one missing file costs one figure and not the whole file.
3. **Into the runtime.** A new part of `build-runtime.sh`, `overlay`, in the default runtime's
   `lib/gpu/layers/` (the library and its layer file with a relative path, like `lib/gpu`'s other
   manifests). `wine-env.sh` adds the folder to the loader's path; the layer stays unloaded unless a
   game asks for it (`MANGOHUD=1`), so a game without the overlay runs exactly as today. Both Wine
   bases use the default runtime's `lib/gpu`, so both get it. While developing, copied by hand; then a
   runtime release (the maintainer's OK).
4. **In the app.** On a game's settings page: "Performance overlay": Off, Frame rate, Frame rate and
   frame time, Full (CPU, GPU, temperatures, memory, battery). In Settings: the level for all games
   (each game can choose its own) and the corner it sits in. Saved as a line in the game's settings
   (`UTDECK_OVERLAY=...`), turned into MangoHud's variables at the start by `utdeck-compat` (Steam
   games) and `game-run.sh` (own games); a settings file per level, laid out for a phone held
   landscape (font size, compact).
5. **Tests and the exit run.** The three kinds of game on both phones, each level; the numbers against
   the phone's files and the sampler; the cost; a game started and quit 5 times with the overlay; the
   overlay off leaves nothing loaded (`/proc/<pid>/maps` without MangoHud).

## Questions for the maintainer

1. **The levels**: Off, Frame rate, Frame rate and frame time, Full, with **Off** as the default for
   all games? (The Steam Deck has five levels; four seem enough on a phone screen.)
2. **Test phones**: the Nothing Phone (1) first (it is connected; its sampler is the broken one), stone
   when it is connected next. Or stone first?
3. **4.10 right after 5.1**, before the frame limiter (5.2)?
4. **Logs**: keep UTDeck's `.frames` and `.sensors` files (fixed in step 2), and decide in 5.4 whether
   MangoHud's own logging replaces them?

## Answers from the maintainer (2026-11-24)

1. The levels as proposed (Off, Frame rate, Frame rate and frame time, Full), **Off** by default.
2. The Nothing Phone (1) first, stone when it is next connected.
3. 4.10 comes back **after 5.3**, not 5.1 (Ys IX already ran into memory trouble). 5.1-5.3 are tested
   with the games already on the phones; the wider game round waits.
4. UTDeck's `.frames` and `.sensors` stay; 5.4 decides about MangoHud's own logging.

## Status

- 2026-11-24: planned. Found while planning, on the Nothing Phone (1): the sampler's empty files (the
  `policy6` read, [lessons-learned.md](../lessons-learned.md)), Android's display driver named like
  mainline's GPU driver, the KGSL files readable by the user.
- 2026-11-24: **step 1 closed** (the maintainer). The overlay works through DXVK (Direct3D 11) and
  VKD3D-Proton (Direct3D 12) on the Nothing Phone (1) and stone, its numbers match the phone's files,
  a settings file reloads while the game runs, and its cost is measured (about 4 % in a light scene
  on stone, mostly from being loaded). Moved on: an OpenGL game is tested when one is installed
  (none on either phone); FEX's statistics go to step 3; the keyboard shortcut is dropped (the app
  controls the overlay through the settings file). Found on the way: the wiliwili flashing on the
  Nothing Phone (1) (parked, [lessons-learned.md](../lessons-learned.md)) and its thermal profile
  ([stage5-roadmap.md](stage5-roadmap.md)). Step 2 added: where MangoHud's 0.6 ms a frame goes while
  hidden. Step 2 begins with the sampler.
- 2026-11-24: **step 2 done**. The sampler works on both phones; MangoHud patches 0001 (KGSL without
  the display driver's name), 0002 (a phone's battery), 0003 (the overlay's buffers kept mapped,
  about 0.2 ms a frame less); the CPU temperature patch taken out (the original zone was right).
  MangoHud's time per frame measured: about 0.06 ms hidden, 0.55 ms at frame rate only, about 1 ms
  full. Next: step 3, MangoHud in the runtime (and FEX's statistics).

## Step 1: MangoHud as it is (2026-11-24)

**The build**: `tools/mangohud-build/build-mangohud.sh` (new), in `utdeck-mangohud-cross`, Mesa's cross
container plus xkbcommon (`tools/mangohud-build/Dockerfile`). The release's Source archive (sha256 as
GitHub lists it) carries Dear ImGui 1.91.6, ImPlot 0.16, spdlog 1.14.1 and Vulkan's headers 1.4.346, so
the build downloads nothing (`--wrap-mode=nodownload`). Options: Wayland on (keyboard shortcuts), X11,
D-Bus, NVIDIA, tests, mangoplot and docs off, `with_fex` and `mangohudctl` on. Built at the first try:
`libMangoHud.so` (6.2 MB, the Vulkan layer; it needs only `libwayland-client`, `libxkbcommon` and the C
library, both on the phone), `libMangoHud_opengl.so` (OpenGL hooking, not needed here: OpenGL games
reach Vulkan through zink), `mangohudctl`.

**On the Nothing Phone (1), for the test only**: the layer in `~/utdeck-probe/test/mangohud/` with its
own layer file (`library_path` `./libMangoHud.so`), switched on for wiliwili (OpenGL through zink) and
Plants vs. Zombies: Replanted (DXVK) by a `utdeck-env` file each (`VK_ADD_IMPLICIT_LAYER_PATH`,
`MANGOHUD=1`, `MANGOHUD_CONFIG=full,control=mangohud-%p`: every reading on, to see which ones work
here). Both files are new and are deleted after the test.

**First run, wiliwili (2026-11-24, 23:26)**: no overlay at first. Our Vulkan loader (Khronos 1.3.275)
predates `VK_ADD_IMPLICIT_LAYER_PATH` (its strings: `VK_ADD_LAYER_PATH`, `VK_LAYER_PATH`,
`VK_LOADER_LAYERS_ENABLE`, ...). With `VK_ADD_LAYER_PATH` (the folder) and
`VK_LOADER_LAYERS_ENABLE=VK_LAYER_MANGOHUD_overlay_aarch64` (the layer by name) it loads, and only in a
game that names it: better for UTDeck than an implicit layer anyway. wiliwili turned out to draw through
Vulkan (`winevulkan`, our loader, Turnip; no EGL or zink loaded), so it is not the OpenGL test.

What it showed (`full`), against the phone's files read at the same time:

| Reading | MangoHud | The phone | Verdict |
| --- | --- | --- | --- |
| GPU load | 74 % | `kgsl-3d0/gpu_busy_percentage` 73-74 % | right |
| GPU clock | 315 MHz | `gpuclk` 315 MHz | right |
| GPU temperature | 45 °C | `kgsl-3d0/temp` 45.3-45.7 °C | right |
| CPU temperature | 49 °C | `thermal_zone44` = `cpuss-0-usr` 49.8-51.8 °C | right (first noted here as "only the small cores' cluster": wrong, see step 2) |
| CPU per core | CPU0-3 691 MHz, CPU4-6 2400 MHz, CPU7 2515 MHz | three clusters | right |
| CPU power | 0.0 W | no `/sys/class/powercap` | missing; MangoHud looks 5 times (5 errors in the log), then stops |
| Battery | none | `power_supply/battery` | not found ("No battery found") |
| VRAM, process VRAM | 0 | phones share RAM | meaningless here |
| RAM | 3.5 GB | | to compare |

**The release already reads KGSL** (`src/gpu_fdinfo.cpp`, `init_kgsl`: `gpu_busy_percentage`, `temp`),
used when the DRM driver is named `msm_drm`. Android's display driver carries that name, so it works
here by that coincidence. Step 2 shrinks to: the CPU temperature over all clusters, the battery, no
power line and no repeated error, no VRAM lines, and a GPU check that does not depend on the display's
driver name.

**The control socket works**: `:hud;` on the socket `mangohud-<pid>` (abstract namespace) hides and shows
the overlay of the running game (screenshots: the corner's mean brightness 108, hidden 178, shown
again). The overlay greets with `:MangoHudControlVersion=1;:DeviceName=Adreno 7c+ Gen 3;...` (Turnip's
name for the Adreno 642L). `mangohudctl` does not reach a game's overlay: it writes to a System V
message queue meant for `mangoapp`, the Steam Deck's separate overlay program.

**Plants vs. Zombies: Replanted (DXVK), 23:30-23:35**: the overlay shows (`full`); its FPS (28) and frame
time (36.0 ms) match patch 0007's `.frames`; the GPU at 74-76 %. The game started and quit as before,
as did wiliwili. The cost, on the main menu left untouched (the last 30 s of each run, from `.frames`):

| Run | Frame rate | Frame time | Longest frame per second, median / worst |
| --- | --- | --- | --- |
| With the overlay (`full`) | 27.97 fps (sd 0.83) | 35.8 ms | 44.0 / 53.2 ms |
| Without (2 minutes later) | 26.34 fps (sd 2.38) | 38.3 ms | 46.2 / 61.7 ms |

Not measurable here: PvZ is held at about 28 fps by the GPU, and the run without the overlay was even
slower (the phone warmer by then). The cost is measured again where it could show: a light scene at a
high frame rate (wiliwili's menus, about 100 fps), runs on, off, on to cancel out the phone's heat.

### The cost, measured on wiliwili (2026-11-24)

wiliwili left untouched on its home page, three runs one after the other (the run without the overlay
in the middle, so the phone's warming cannot explain a difference). Seconds from 30 on; the first 30 s
are the start and are left out.

| Run | Overlay | Seconds | Frame rate | Mean frame | Longest frame (median second / worst) | GPU temperature after |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | full (`MANGOHUD_CONFIG`) | 76 | 80.73 fps | 12.39 ms | 16.8 / 21.5 ms | 40.3 °C |
| 2 | off (no layer loaded) | 29 | 96.91 fps | 10.32 ms | 14.7 / 17.5 ms | 42.2 °C |
| 3a | full (from a settings file) | 50 | 81.13 fps | 12.33 ms | 16.6 / 29.3 ms | |
| 3b | frame rate only (same run, file changed at 81 s) | 93 | 86.25 fps | 11.59 ms | 15.8 / 25.5 ms | 44.6 °C |

**The overlay is not free here**: the full level costs about 2.0 ms per frame (16-17 % of the frame
rate), frame rate only about 1.3 ms (11 %). Runs 1 and 3a agree, and run 2 was faster although the
phone was warmer, so this is the overlay, not heat.

What wiliwili is held by on this screen: the GPU runs at 315 of its 608 MHz, 73 % busy, and no thread
of the game uses more than 17 % of a core. So neither the GPU nor a CPU core is full; the frames wait on
each other, and any time added to a frame lowers the frame rate directly. A game held by a full GPU may
lose less (or more); this is the case where a small cost shows the most.

Where the cost most likely comes from (to check in step 2): MangoHud draws its overlay in a render pass
over **the whole screen image** (`renderArea` = the full 2400x1081 swapchain, `loadOp` LOAD, `storeOp`
STORE, `src/vulkan.cpp`). Adreno is a *tiler* (a GPU that draws the screen in small tiles in its fast
on-chip memory): such a pass reads the whole 10 MB picture into the tiles and writes it back every
frame, even when the overlay is a single line in a corner. That fits the light level still costing
about two thirds of the full one. The fix to try: a render area that covers only the overlay's
rectangle, so Turnip loads and stores only those tiles. Measured the same way before and after.

### Run 4: the redraw limited to the overlay, and the overlay hidden (2026-11-24)

The fix suggested above was built (a MangoHud patch: the render area = the bounding box of the
overlay's vertices, aligned to `vkGetRenderAreaGranularity`, the scissors narrowed to it; Turnip skips
the tiles outside, `tu_tile_config.cc`). Run 4 used it in three parts: full, frame rate only (file
changed at 91 s), and hidden through the control socket (`:hud;` at 155 s; MangoHud stays loaded and
keeps its hooks, but builds and draws nothing).

| Part of run 4 | Seconds | Frame rate | Mean frame |
| --- | --- | --- | --- |
| full, with the fix | 60 | 81.64 fps (81.13 without it, run 3a) | 12.25 ms |
| frame rate only, with the fix | 29 | 86.31 fps (86.25 without it, run 3b) | 11.59 ms |
| hidden (nothing drawn) | 55 | 86.46 fps | 11.57 ms |

**The drawing is not where the cost is.** The fix changed nothing measurable, so it was taken out
again (stage 5's rule); the patch is kept out of the tree. Hidden costs as much as frame rate only, so
about 10 % is lost with MangoHud loaded and doing nothing per frame but a few lookups, its keyboard
check and its control socket. Its own threads use 0.1 % of a core each (measured per thread over
10 s). Its hooks on the game's command buffers and submissions only pass the calls on, and its frame
limiter is off.

What the phone did meanwhile (hidden, 10 one-second readings): the GPU at 315 MHz all the time, 70-73 %
busy, as in run 1. The big cores' clocks jumped between 0.69 and 2.5 GHz from one second to the next.
wiliwili on this screen is held by no single part (no full GPU, no full core), so its frame rate
follows the clocks the CPU governor (`schedutil`, the kernel part that picks the CPU speed) happens to
choose, and a small change in timing can move it. So the 10 % may be the governor's reaction, not
MangoHud's work; the off run is also a single run (83 s, 96.10 fps over its whole length). To find
out: runs off, hidden from the start, off, with the CPU and GPU clocks recorded every 2 s in each
(the sampler fix planned for step 2, done first for this).

### Other findings in the wiliwili runs

- **Settings reload works**, from a settings file only: MangoHud watches its settings file (inotify) and
  applied the change from `full` to `fps_only` while the game ran (the maintainer saw the overlay shrink).
  With `MANGOHUD_CONFIG` in the environment it reads no file at all, so nothing can be reloaded. For
  step 4: the app writes a settings file per game run and points `MANGOHUD_CONFIGFILE` at it; changing
  the level while a game runs is then a file write.
- **The rounded screen corners hide the overlay**: at the top left, the frame rate line is barely
  readable on the Nothing Phone (1). For step 4: a default position and offset (`position=`,
  `offset_x`, `offset_y`) that clears the corners, chosen on both phones.
- **FEX's statistics** need two things the runtime does not have: FEX's Linux-side helper library
  (`libarm64ecfex.so` in `lib/wine/aarch64-unix/`, from FEX's `Source/Windows/UnixLib`, which creates
  the shared file `/dev/shm/fex-<pid>-stats`) and `FEX_PROFILESTATS=1`. Without the library, FEX in the
  game never writes the file (none was there during the runs). The library also gives FEX other things
  (memory advice, names for memory areas), so it is measured before it goes in: step 3, with the runtime.

### The cost, measured on stone (2026-11-24)

wiliwili on stone (Adreno 619, app 0.3.48, runtime `11.16-20261122`), untouched on its home page,
five runs of about 2.5 minutes, seconds 30-155 of each; stone's sensor sampler (which works there)
recorded the clocks every 2 s. The baseline sank from run A to run E as the phone warmed (skin
32 to 35 °C), so each run is compared with the baseline expected at its place in the order (a
straight line between A and E).

| Run | MangoHud | Frame rate | Expected without | Cost |
| --- | --- | --- | --- | --- |
| A | off | 58.25 fps | | |
| B | original, loaded, hidden from the start (`no_display`) | 55.64 fps | 57.70 | -3.6 % (0.6 ms a frame) |
| C | original, full, shown | 54.65 fps | 57.16 | -4.4 % (0.8 ms) |
| D | without its keyboard listener, full, shown | 54.56 fps | 56.61 | -3.6 % (0.7 ms) |
| E | off | 56.07 fps | | |

In every run the big cores were at their 2016 MHz and the GPU at its 700 MHz (both the maximum), CPU
load about 26 % and GPU load about 55 %: no governor effect here, unlike on the Nothing Phone (1).

- **Most of the cost is MangoHud being loaded**, not its drawing (B against C: about 0.2 ms more for
  the full overlay), as on the Nothing Phone (1), where hidden and frame-rate-only were equal. What it
  does per frame while hidden is small on paper (lookups in its tables, its keyboard check, its
  control socket), so where the 0.6 ms goes is still to be found (a profiler on the phone, step 2).
- **The keyboard-listener patch changes nothing measurable** (C against D), so it stays out.
- For step 4: with the overlay off, nothing is loaded (already the plan), so the cost is only paid by
  whoever turns it on; on stone that is about 4 % in a light scene.

### Stray: Direct3D 12 (2026-11-24)

Stray (Direct3D 12 through VKD3D-Proton, runtime `11.16-20261124`, the original MangoHud at the full
level from a settings file): the overlay shows, and its numbers match the phone's files (big cores
1516 MHz, the fastest core 1766 MHz, GPU 550 MHz at 71-75 % busy). The game ran at 30 fps at first and
fell to about 16 as the phone's thermal limits came in. Those limits are the vendor's own:
`thermal-engine` (Qualcomm's daemon) with Nothing's profiles in `/vendor/etc/thermal.*.conf`, all
watching `quiet-therm-usr` (a board sensor). At 43.1 °C on it (skin sensor 47.2 °C, the chip's cores
60-61.5 °C) the limits were exactly the second step of the "mid" profile: big cores 1516 MHz, the
fastest core 1766 MHz, GPU 550 MHz; the cooling devices showed `thermal-cpufreq-4` and `-7` at 5 of 9,
the GPU at 1 of 3, and the charging limited (`battery` 6 of 7, which is why the phone drained on the
PC's USB port). Nothing also ships game and performance profiles (`thermal.sgame.conf`,
`thermal.high.perf.conf`): at the same 43 °C, `high.perf` allows 2131 and 2208 MHz. Choosing one of
the vendor's own profiles stays within the manufacturer's values (CLAUDE.md, hardware safety); how
`thermal-engine` picks its profile on this port is still to be found out.

Still open in step 1: an OpenGL game (none installed on the Nothing Phone (1): wiliwili and PvZ
draw through DXVK, Stray through VKD3D-Proton); a keyboard's Shift_R+F12; the overlay's cost (the
wiliwili measurement could not go on on the Nothing Phone (1), see the flashing in
[lessons-learned.md](../lessons-learned.md); to be measured on stone).

## Step 2: the phone's numbers in MangoHud (2026-11-24)

**The sampler** (`game-run.sh`) first, as planned: one awk process that finds the clusters
(`policy*`) and the zones (by name) and reads each file with a fallback; on the Nothing Phone (1) it
fills its `.sensors` files again (PvZ: 135 lines, all three clusters, the board sensor of the thermal
profiles, the battery's current), and leaves no process behind. On stone too (2026-11-24, PvZ: 44
lines, its two clusters `c0-5` and `c6-7`, 12 CPU zones, a `quiet-therm` board sensor as well; no
process left). stone's battery reports 0 mA (its `current_now` stays 0, so its overlay will show no
battery power).

**MangoHud patches** (`tools/mangohud-build/patches/`), each checked against the phone's own files:

| Patch | What it does | Result |
| --- | --- | --- |
| `0001-kgsl-gpu-without-the-display-driver-s-name` | the GPU through KGSL whenever `/sys/class/kgsl/kgsl-3d0` exists, not only when a DRM node is named `msm_drm` (Android's display driver, which only happens to carry that name on both our phones); no scan of all the game's open files every 10 s on KGSL | kept; nothing changes on screen on our phones (the KGSL path was already reached), it matters for a phone whose display driver has another name, and it stops a useless scan |
| (CPU temperature from the hottest CPU zone) | read all 14 CPU zones and show the hottest instead of `cpuss-0-usr` | **taken out**: in PvZ the hottest zone was on average 0.3 °C above `cpuss-0-usr` (at most 4 °C for a moment, 15 readings), and in Stray `cpuss-0-usr` was itself the hottest listed. It is a CPU subsystem sensor that follows the hot spot, not "the small cores' cluster" as step 1 said |
| `0002-a-phone-s-battery` | a supply of type `Battery` (scope not `Device`, so a controller's battery is left out) besides MangoHud's `BAT*` names; current times voltage before `power_now` | kept: the battery's percentage now shows (91 %, the phone's own figure); its power shows only while discharging (MangoHud's design): unplugged in PvZ's menu the overlay showed about 4.7 W, and the sampler recorded 1.09-1.33 A at about 3.9-4.0 V, 4.6-4.9 W |

**Where MangoHud's time per frame goes.** A profiler on the phone (`simpleperf`) needs root
(`perf_event_paranoid` 3), so a timing build was used instead (a test-only patch, not in the
repository: counters around each part of MangoHud's present step and its hooks, averages written to
the game's log every 600 frames). PvZ's menu on the Nothing Phone (1), about 25 fps:

| Per frame | Full | Frame rate only | Hidden |
| --- | --- | --- | --- |
| reading the sensors, the keyboard check, the control socket | 0.15 ms | 0.11 ms | 0.045 ms (no sensors) |
| building the panel (ImGui, the library MangoHud draws with) | 0.37-0.46 ms | 0.10 ms | none |
| recording the overlay's drawing for the GPU | 0.40-0.46 ms | 0.29-0.31 ms | none |
| the overlay's own GPU submission | 0.19-0.32 ms | 0.25-0.29 ms | none |
| **MangoHud in all** | **about 1.2 ms** | **about 0.8 ms** | **about 0.06 ms** |
| its hooks on the game's command buffers (8 calls a frame) | 0.011 ms | 0.010 ms | 0.010 ms |

DXVK submits through `vkQueueSubmit2`, which MangoHud does not hook (its `vkQueueSubmit` hook saw no
call). **Hidden, MangoHud costs about 0.06 ms a frame**, a tenth of the 0.6 ms the frame-rate runs on
stone suggested: those runs cannot tell such small differences apart (runs A and E, both without
MangoHud, already differed by 2.2 fps), and MangoHud's one-time changes do not explain more (DXVK
already asks for `COLOR_ATTACHMENT` on its swapchain images).

The recording was slow because MangoHud mapped and unmapped its vertex buffers every frame, and
Turnip unmaps for real (`munmap`), so every frame paid two mmaps, two munmaps and page faults.
`0003-keep-the-overlay-s-buffers-mapped` maps them once:

| Recording per frame | Before | With 0003 |
| --- | --- | --- |
| full | 0.40-0.46 ms | 0.17-0.22 ms |
| frame rate only | 0.29-0.31 ms | 0.10-0.12 ms |

About 0.2 ms saved a frame; the frame-rate-only level costs about 0.55 ms instead of 0.8. The overlay
drew normally all through (kept). Left as it is: the overlay's own GPU submission (any overlay layer
needs one), the sensors (about 0.1 ms) and the panel.

Not patched, left to UTDeck's own levels (step 4) instead: the CPU power line (no source on a phone;
MangoHud gives up after 5 tries) and the VRAM lines (a phone shares its memory: the levels will not
list them).

## Step 3: into the runtime (2026-11-24)

**The overlay part.** `build-runtime.sh` has a new part, `overlay` (default runtime only, like
`gpu`; both Wine bases use the default runtime's `lib/gpu`): it runs `build-mangohud.sh` into
`build/runtime-obj/mangohud` and puts `libMangoHud.so`, its layer file (`MangoHud.json`, the path
made relative: `./libMangoHud.so`) and MangoHud's `BUILD-INFO` into `lib/gpu/layers/`; the runtime's
`BUILD-INFO` names MangoHud's version and patches and lists the library's checksum. The build is
reproducible: the runtime's `libMangoHud.so` has the same checksum (`51f4cf30...`) as the same
patches built alone. `wine-env.sh` lists the folder for the Vulkan loader
(`VK_ADD_LAYER_PATH=.../wine/runtime/lib/gpu/layers`, through the `runtime` link, so it follows the
runtime in use; a folder the caller already listed comes first). A layer found that way is only
loaded when a game enables it by name (`VK_LOADER_LAYERS_ENABLE=VK_LAYER_MANGOHUD_overlay_aarch64`,
with `MANGOHUD=1`); our loader (1.3.275) has no `VK_ADD_IMPLICIT_LAYER_PATH`, as step 1 found.

Checked on the Nothing Phone (1), the folder copied by hand into `runtime-11.16-20261124` and the
new `env.sh` installed (the old one kept as `env.sh.before-5.1`):

- **On**: PvZ with only `VK_LOADER_LAYERS_ENABLE`, `MANGOHUD=1` and a settings file in its
  `utdeck-env`: the full overlay showed, and the game's process had loaded
  `runtime-11.16-20261124/lib/gpu/layers/libMangoHud.so`; no errors in its log.
- **Off**: PvZ without those settings: no overlay, no MangoHud socket, and neither the drawing
  process (`Replanted.exe`) nor Wine's `explorer.exe` had MangoHud loaded, though both had the
  folder listed (`VK_ADD_LAYER_PATH`).

**FEX's statistics move out of 5.1** (the maintainer, 2026-11-24). They need FEX's Linux-side library
(`libarm64ecfex.so`, from FEX's `Source/Windows/UnixLib`), and with it FEX also changes how it manages
its memory: it controls transparent huge pages (2 MB pages) for its own memory and names its memory
areas (besides the code-cache file mapper, off by default, and a CPU-number source the phones'
kernels likely lack). The huge-page control can change speed and memory either way, so it is measured
on its own: an item of 5.4, "FEX's Linux-side library", with the statistics. The overlay does not
need them.

**No runtime release for step 3 alone** (the maintainer, 2026-11-24): the overlay part only matters
once the app can switch it on (step 4), and `env.sh` ships with the app; one runtime release with
step 4's app version.

## Step 4: in the app (2026-11-24)

**The setting.** `UTDECK_OVERLAY` = `off`, `fps`, `frametime` or `full`; `UTDECK_OVERLAY_POSITION` =
`top-left` (the default), `top-center` or `top-right`.
- A game's own level: its settings page, "Performance overlay" (As in Settings / Off / Frame rate /
  Frame rate and frame time / Full), a line in its `utdeck-env` only when it is not "As in Settings".
- For all games: Settings, "Performance overlay" ("For all games", "Where it sits"), saved at once to
  `wine/user.env` (`GameSettings::saveGlobal`). `defaults.env` is replaced with each app update,
  `user.env` is the user's and stays (the click's files do not include it).
- `game-run.sh` (own games and Steam games alike) reads `user.env` between the game's own settings and
  `defaults.env` (only `UTDECK_*` names, each checked to be a plain name before it reaches `eval`),
  and for a level other than Off writes a settings file for the run (`<log>.overlay.conf`) and enables
  the runtime's layer. Off, and Steam's install scripts, set nothing. The game's log says which level
  and place were used ("user settings: ...", "overlay: full, top-center").
- The levels, all with `legacy_layout=0` (exactly the listed items, in order) and the background at
  40 %: Frame rate = `fps`, `fps_only`; Frame rate and frame time = `fps`, `frametime`, `frame_timing`
  (the graph); Full = those and `cpu_stats`, `cpu_temp`, `cpu_mhz`, `gpu_stats`, `gpu_temp`,
  `gpu_core_clock`, `ram`, `battery`, `battery_watt`, `throttling_status`. No CPU power line (no source
  on a phone), no VRAM lines (shared memory).
- The place: left and right 100 pixels in from the side, clear of the rounded corners. MangoHud adds
  `offset_x` in every position, so the right side takes -90 (MangoHud keeps its own 10-pixel margin
  there): the first try, +100, pushed the panel further into the right corner. A MangoHud patch to
  read the offsets as signed numbers changed nothing in the library (the same checksum: it reads them
  with `strtol`, and the sign survives), so it is not kept.

App 0.3.52 (the maintainer's OK) on the Nothing Phone (1), checked in PvZ:

| Check | Result |
| --- | --- |
| the app puts the new `game-run.sh` and `env.sh` in place | both the same as the repository's |
| Settings: Frame rate, top center | `user.env` written; the frame rate at the top center; the log: "user settings: UTDECK_OVERLAY=fps UTDECK_OVERLAY_POSITION=top-center", "overlay: fps, top-center" |
| PvZ's own page: Full | its `utdeck-env` holds `UTDECK_OVERLAY=full`; the full panel at the top center (the level from the game, the place from Settings) |
| PvZ's own page: Off | `UTDECK_OVERLAY=off` (it differs from the level for all games); no overlay file for the run, no MangoHud socket, MangoHud loaded in neither the game nor Wine's `explorer.exe` |
| Full, top left and top right | both clear the rounded corners (top right after the -90 fix) |

Still for step 4: the fixed `game-run.sh` (top right) is on the phone by hand and goes into the next
click.
