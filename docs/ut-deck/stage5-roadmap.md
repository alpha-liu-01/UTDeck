# Stage 5: performance and resources, the plan

Written 2026-11-24, when 4.10 was parked (the maintainer: testing more games waits until the
performance overlay is ready). Goal (from [feasibility-and-roadmap.md](feasibility-and-roadmap.md)):
games get as much of the phone as possible, measured before and after each change, and the user can
see and limit what a game uses. CLAUDE.md's rule 4 is this stage's job: remove pointless losses
(overhead, debug leftovers, bad defaults).

Terms used here:
- *overlay* (or *HUD*) = numbers drawn over the game's own picture while it runs: frame rate, frame
  time, CPU and GPU load, clocks, temperatures, memory.
- *frame time* = the time between two pictures; steady frame times feel smoother than a higher but
  uneven frame rate.
- *Vulkan layer* = a library the Vulkan loader puts between a program and the GPU driver; it sees
  every picture before it reaches the screen, so it can draw on it (MangoHud does this). Every game
  here ends in Vulkan: Direct3D through DXVK or VKD3D-Proton, OpenGL through zink, both on Turnip.
- *KGSL* = the Android kernel's Adreno GPU driver (Halium phones have it instead of mainline's
  `msm`); its numbers are files in `/sys/class/kgsl/kgsl-3d0/`.
- *frame limiter* = a cap on the frame rate: the game waits instead of drawing pictures the screen
  cannot show, so it runs cooler and steadier.
- *PSS* = a process's share of the memory it uses, shared pages divided among their users (stage 3.4).
- *cgroup* = the kernel's group of processes for one systemd unit, with its own memory controls.

## What exists, from stages 3 and 4

- **Frame statistics** from inside Wine (patch 0007, `UTDECK_FRAMESTATS`): per game run,
  `wine/games/<label>-<time>.frames`, one line per second: frames, frame rate, mean and longest frame
  time, swapchain size.
- **A sensor sampler** in `game-run.sh`: `<label>-<time>.sensors` every 2 s: CPU load, big-core
  clock, GPU load and clock (KGSL), CPU, GPU, skin and battery temperatures, free memory, the game's
  memory, swap, the allowed maximum clocks, charging. **Broken on the Nothing Phone (1)**: it reads
  stone's CPU layout (`policy6`), and the failed read ends it, so all 19 of that phone's files are
  empty (found 2026-11-24, [lessons-learned.md](../lessons-learned.md)).
- **What a game gets from UTDeck already** (stages 3.4-3.5, 4.6): the boost (CPU clamp for the game,
  `utdeck-boost.py`), the pause behind other apps (`utdeck-pause.py`), keep-awake, fast x87 maths
  (FEX), fill (a smaller game window filling the screen), shader pre-caching off (4.7).
- **Memory measured** in stage 3.4: hidden Steam about 1 GB PSS when idle; Steam's switch (4.2) stops
  it between games, not during one.
- **Left by stage 4 for this stage**: Steam's shared memory left in `/dev/shm` after it exits (272 MB
  on the Nothing Phone (1)), the old runtime kept after an update (1.2 GB each time; the Nothing Phone
  has two since 2026-11-24), and a phone with a runtime does not update it by itself.

## Substages

| Substage | Goal | Exit |
| --- | --- | --- |
| **5.1 The performance overlay** | Numbers in the game's picture, as on a Steam Deck: frame rate and frame time first, then CPU and GPU load and clocks, temperatures, memory and battery. MangoHud (an open source Vulkan layer, the base of the Steam Deck's overlay) built for ARM64, taught the KGSL GPU and the phone's sensors, carried in the runtime, switched on per game and for all games from the app. The sensor sampler fixed on the way (the same readings). Plan: [stage5.1-overlay.md](stage5.1-overlay.md) | On the Nothing Phone (1) and stone: the overlay on from the app in a Direct3D 11, a Direct3D 12 and an OpenGL game, its numbers matching the phone's own files, its cost on the frame rate measured and small; the `.sensors` files filled on both phones |
| 5.2 The frame limiter | A frame rate cap per game (and a default), in the game's settings page: steadier frame times, less heat, longer battery. MangoHud's limiter (early or late) against DXVK's and VKD3D's own (`DXVK_FRAME_RATE`, `VKD3D_FRAME_RATE`), judged by frame times; the screen's refresh rate (both phones' panels go up to 120 Hz; what Lomiri runs them at is still to be read) and the present modes looked at. Plan: [stage5.2-frame-limiter.md](stage5.2-frame-limiter.md) | On one game, uncapped against 30, 40 and 60 fps: frame time spread, temperatures, the battery's draw over 10 minutes, written down; the cap set from the app reaches the game |
| 5.3 Steam's memory during a game | Hidden Steam's 1 GB given back while a game runs, without breaking Steam: `memory.reclaim` on its unit's cgroup (the kernel pushes its pages into compressed memory), the web helper paused (Steam restarts a helper it thinks hung: `-cef-disable-hang-timeouts`); each measured for the game's memory, Steam's state afterwards, the game's start, cloud saves and quitting Plan: [stage5.3-steam-memory.md](stage5.3-steam-memory.md) | Steam's memory during a game measured before and after on both phones; cloud saves, quitting and the next start unchanged |
| *4.10, back* | The game round and stage 4's exit run (parked 2026-11-24), after 5.3 (the maintainer, 2026-11-24: Ys IX already ran into memory trouble, so new games wait for Steam's memory work); with the overlay, each new game's numbers written down as it is tried | As written in [stage4-roadmap.md](stage4-roadmap.md) |
| 5.5 Touch in games | Touch as a game controller, not only as a mouse: today the first finger acts as the mouse (patch 0002 on both bases), and multitouch and swipes are not responsive enough in games (the maintainer, 2026-11-25). A test program first, one that shows every touch point and gesture as Wine delivers it (`WM_POINTER`, `WM_TOUCH` and mouse messages), then the mapping through fill's rect (Proton's patch 0022 and our 0010: the picture in a rect of the screen, input mapped back through it; untested by touch since Need for Speed's menus take no mouse), then multitouch and gestures | On both bases, in a window filled with bars: taps land where the finger is, at the picture's edges too; two fingers and a swipe reach the test program as on Windows |
| 5.4 Other losses | What UTDeck itself leaves or takes: Steam's `/dev/shm` leftovers cleared, the old runtime removed after an update (one kept for going back), runtime updates offered by the app, the sampler's own cost (a shell loop starting processes every 2 s), debug leftovers (Wine's logging, our TRACE lines), defaults checked game by game; FEX's Linux-side library (`libarm64ecfex.so`: the statistics for the overlay, and FEX's control of transparent huge pages for its memory, measured on and off; moved here from 5.1 on 2026-11-24) | Each one measured before and after, or a reason it stays |

Stage 5's exit (from [feasibility-and-roadmap.md](feasibility-and-roadmap.md), test phone updated): one
game run with Steam's memory lowered (measured), the frame limiter and the overlay switched on from
the app, the numbers before and after written down.

## Order and why

The overlay comes first (the maintainer, 2026-11-24): every other substage is judged by numbers.
**4.10 returns after 5.3** (the maintainer, 2026-11-24): Ys IX already ran into memory trouble, so a
wider game round waits until Steam's memory during a game is dealt with; 5.1-5.3 are tested with the
games already on the phones. The frame limiter follows, because MangoHud brings one and the overlay shows its effect at
once. Steam's memory is third: it changes how Steam runs, so it waits until the measuring is in place.
The other losses are last, since some are found while doing the others.

## The Nothing Phone (1)'s thermal profile (researched 2026-11-24)

Found in Stray (5.1 step 1): the game fell from 30 to about 16 fps with the phone's clocks lowered by
its thermal management while it still felt cool. The limits are the vendor's (CLAUDE.md, hardware
safety: never beyond them), but the vendor ships more than one set.

- *thermal-engine* = Qualcomm's thermal daemon (`/vendor/bin/thermal-engine`, in the Android part of
  the port): it watches temperature sensors and lowers the CPU and GPU clocks in steps.
- Nothing's profiles: `/vendor/etc/thermal.<name>.conf` (`mid`, `low`, `benchmark`, `high`,
  `high.level1-4`, `super`, `sgame`, `high.perf`). All watch `quiet-therm-usr` (a board sensor).
- How one is chosen (`/vendor/etc/init/init_thermal-engine.rc`, Nothing's lines): Android's system
  server sets `sys.system_server.thermal.config=<name>`, init turns it into
  `persist.vendor.thermal.config=/vendor/etc/thermal.<name>.conf` and restarts thermal-engine, which
  reads that property ("Read thermal config from persist"). With the property empty, as on this port
  (no Android system server), it falls back to the path built into it: `/vendor/etc/thermal.mid.conf`.
- The limits (big cores / fastest core in MHz, by `quiet-therm`):

| `quiet-therm` | `mid` (in use; `low` and `benchmark` are the same) | `high`, `super` | `sgame`, `high.level1-4` | `high.perf` |
| --- | --- | --- | --- | --- |
| from 39 °C | none | 2131 / 2208, small cores 1804, GPU 550 | 2131 / 2208, GPU 550 | none |
| from 40 °C | 2054 / 2208, small cores 1804 | | | |
| from 42 °C | | | | 2131 / 2208, small cores 1804, GPU 550 |
| from 43 °C | 1516 / 1766, GPU 550 | 1651 / 1766, small 1516, **GPU 450** | GPU 450 | |
| from 45 °C | | | | 1651 / 1766, small 1516, GPU 450 |
| from 46 °C | 1340 / 1516, small 1324, GPU 450 | | 1651 / 1766 | |
| from 48 °C | 940 / 1056, small 940, GPU 315 | 1228 / 1516, small 1152, GPU 315 | 1228 / 1516, small 1152, GPU 315 | |
| from 51 °C | | | | 1228 / 1516, small 1152, GPU 315 |

  `high.perf` is the most permissive everywhere (its steps come 2-3 °C later, and its last one is
  1228 / 1516 rather than 940 / 1056); `high`, `super` and `sgame` free the CPU but lower the GPU
  sooner than `mid` (450 MHz from 43 °C), worse for a game held by the GPU (Stray).
- Separate from these files: the charging is limited by heat too (`battery` cooling device 6 of 7 at
  `quiet-therm` 42-43 °C), so the phone can drain while plugged into a weak USB port.
- Choosing a profile on this port: `sudo setprop persist.vendor.thermal.config
  /vendor/etc/thermal.high.perf.conf` (libhybris-utils' `setprop`; init then restarts thermal-engine;
  being `persist.`, it stays after a restart). Back: the same with `/vendor/etc/thermal.mid.conf`.
  It writes to the phone's system settings, so only with the maintainer's OK at that moment.
- Set by the maintainer on 2026-11-24 (17:26, `high.perf`). Init then tried to restart thermal-engine
  and could not: it stops a service through its process group, which it failed to create at boot
  (`createProcessGroup ... Read-only file system`), so the old daemon (started at boot, with `mid`)
  went on running, and init shows the service as "stopping" since boot. The profile takes effect at
  the next phone restart, when thermal-engine reads the property first. The running daemon is not
  killed by hand: if init did not start it again, the phone would run without it.
- Measured 2026-11-24, Stray standing in one spot, no overlay, the frame rate every 15 s:

| Profile | `quiet-therm` | Limits seen (big / fastest core, GPU) | Stray |
| --- | --- | --- | --- |
| `mid` | 42.4-42.8 °C | 2054 / 2208, GPU 550 | 28.5-28.9 fps, GPU 93-95 % busy |
| `mid` | 43.0-43.3 °C (6 minutes) | 1516 / 1766, GPU 550 | **23.6-26.0 fps** (mean about 24.8), GPU 76-84 % |
| `high.perf` | 37.6-41.9 °C | 2131 / 2208, GPU 608 | 27.6-29.9 fps |
| `high.perf` | 42.0-42.9 °C | 2131 / 2208, GPU 550 | 28.3-29.0 fps, GPU 93-94 % |
| `high.perf` | 43.0-44.4 °C (3.5 minutes) | 2131 / 2208, GPU 550 | **28.1-29.6 fps** (mean about 28.8), GPU 90-95 % |

  At the same board temperature `high.perf` keeps Stray at about 29 fps where `mid` gave about 25
  (+16 %), the GPU staying the limit instead of the CPU. The cost: the phone runs warmer (skin sensor
  49.7 °C and the CPU cores about 69 °C after 11 minutes, against 47.5 °C and 62 °C under `mid`); still
  inside the vendor's own profile, whose next steps are at 45 and 51 °C on `quiet-therm`. Not
  explained yet: the big cores were held at 2131 MHz (of 2400) already at 37.6 °C, below `high.perf`'s
  first step (42 °C); some other rule of the daemon's sets that ceiling. Open: whether UTDeck should
  set this profile (it needs `sudo`, which the app does not have), and the long run where the phone
  reaches 45 °C and more.

## Test devices

- **The Nothing Phone (1)** (Adreno 642L, three CPU clusters: `policy0`, `policy4`, `policy7`): the
  faster GPU, where 4.10 continues.
- **stone** (Adreno 619, two clusters: `policy0`, `policy6`): the low end; its sensor files have
  worked so far. At its next connection: the `~/utdeck-probe/DEVELOPMENT` marker first (before an app
  with the uninstall page goes on it), then UTDeck 0.3.51 and runtime `11.16-20261124`.

Two phones with different CPU layouts and thermal names are what catches per-phone assumptions (the
sampler's).

## Rules for this stage

- **Measured before and after**: each change has its numbers written down in its substage's notes;
  a change that does not measure better is taken out again.
- **Hardware safety** (CLAUDE.md): the overlay and the sampler only read. The limiter lowers the load.
  Nothing in this stage writes clocks, voltages or thermal limits beyond what stages 3.5 and 4.6
  already do (the boost's CPU clamp within the vendor's range).
- **No account data**: Steam's rules from stage 3 apply to whatever is measured in Steam's processes.
- The overlay is drawn by the game's own process: it must not crash or slow a game when it is off
  (nothing loaded then).

## Status

- 2026-11-24: stage 5 planned (this file), 5.1 planned ([stage5.1-overlay.md](stage5.1-overlay.md)).
  4.10 parked until 5.1 is done.
- 2026-11-24 (the maintainer): 5.1's questions answered (below in its plan); 4.10 moved to after 5.3.
- 2026-11-24: 5.1 steps 1-4 done, step 5 on the Nothing Phone (1); runtime `11.16-20261124.2` (with the
  overlay) released, app 0.3.53. Left in 5.1: stone (parked), an OpenGL game (Stardew Valley).
- 2026-11-24 (the maintainer): **5.1 done**; stone's overlay run moves into stage 5's exit run. 5.2
  (the frame limiter) begins. Noted for later (not stage 5): support for Linux games, before UTDeck
  ships as a product (an x86-64 Linux build needs an x86-64 Linux emulator and its libraries; SDL2
  games such as Stardew Valley can draw on Wayland directly).
- 2026-11-24: 5.2 built: the frame rate limit in the app (0.3.54; Off by default; 28, 30, 40, 45, 48,
  60) through MangoHud's limiter (early), checked on the Nothing Phone (1). Its exit run waits for a
  game that runs above 60 fps without trouble (the maintainer); stone with the rest of stage 5's
  exit run. The Steam Deck's way (the screen's rate following the cap) noted for the stone port.
- 2026-11-25: 5.3 built: hidden Steam's own processes pushed into zram once a game is up
  (`/proc/<pid>/reclaim`; the kernel is 5.4, older than `memory.reclaim`), on by default, a switch in
  Settings (app 0.3.55): about 360-420 MB more for the game in Ys IX's town, Steam stays out, cloud
  saves sync as before. Stone with stage 5's exit run. Next: 4.10 comes back (the game round), then 5.4.
- 2026-11-25 (the maintainer): **5.5 added**, touch in games (from 4.10's item B: fill's input mapping
  could not be tested by touch, and multitouch and swipes are not responsive in games yet); it needs a
  test program first.
- 2026-11-25: 5.5's test programs written (stage 4.10, item 2: `kbmtest`, `touchtest`); a finger's
  drag fixed (patch 0025), raw mouse motion (0026) and the wheel (0027) too. Left in 5.5: more than
  one finger, pointer and `WM_TOUCH` messages, gestures, fill's mapping by touch.
- 2026-11-25 (the maintainer): **stage 5 parked** with 5.4, the rest of 5.5 and the exit run (stone),
  until stage 6 (a stronger phone, [stage6-new-phone.md](stage6-new-phone.md)) ends; measured again
  on the new phone.
