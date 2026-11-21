# Stage 3.5: CPU boost, heat and memory

Goal (roadmap): `uclamp_min` for the game's threads as in the Dolphin port; long runs with CPU and
GPU load together; memory under hidden Steam plus a game on this phone; the vendor's thermal service
stays in charge, no limit is raised. Exit: frame rate, temperature and free memory over 30 minutes
with and without the boost.

Started 2026-11-20, after 3.4's items 0-7 (its items 8-11, which need games, are left open; 3.5
does not depend on them).

## What the boost is, and why it is safe

`uclamp_min` (the scheduler's "utilization clamp") tells the kernel that a thread needs at least a
given share of a CPU's performance, from 0 to 1024. The frequency governor (`schedutil`) then picks
a higher clock from the CPU's own table for the core that thread runs on, and the scheduler prefers
the big cores for it. It sets no clock directly and changes no limit: the highest clock stays the
vendor's (2016 MHz on the big cores, 1804.8 MHz on the little ones), and the vendor's thermal
service can still lower the allowed maximum at any time. A normal user may set it on their own
threads (no root): the Dolphin port did so on this phone and doubled its speed
(`dolphin-ut-probe/uclamp.py`: one `sched_setattr` call per thread, keeping each thread's
scheduling class and priority). Hardware safety: within the manufacturer's values by construction;
the risk is heat and battery, measured here.

## The phone (read on 2026-11-20, read-only)

- Kernel: `CONFIG_UCLAMP_TASK=y` (stage 0); system-wide limits
  `sched_util_clamp_min`/`_max` both 1024, so a task's own clamp is not capped below.
- CPU clocks: little cores (cpu0-5) 300-1804.8 MHz in 10 steps; big cores (cpu6-7) 691.2-2016 MHz
  in 9 steps. GPU: 266, 390, 490, 650, 700 MHz. Qualcomm's core parking (`core_ctl`): 4 to 6 of
  the little cores active.
- Memory: 7.3 GB (`MemTotal` 7612536 kB), 4.1 GB available at idle with hidden Steam not yet
  running, zram swap 2 GB.
- Heat control: **`mi_thermald`**, Xiaomi's thermal service, runs in Halium's Android container
  (`/system/vendor/bin/mi_thermald`), with profiles in `/vendor/etc/thermal-*.conf` (normal,
  `mgame` for games, `nolimits`, camera, charging and others; the files are encrypted, so their
  thresholds cannot be read; there is no profile switch, `thermal_message/sconfig`, on this system).
  The kernel's own trips are far above: CPU step at 110 °C, GPU step at 95 °C, 115-125 °C critical.
  Skin sensors: `sdm-skin-therm`, `quiet-therm`, `xo-therm`, `battery`. When `mi_thermald` acts,
  it lowers the clusters' allowed maximum clock (`scaling_max_freq`) and the GPU's; logging those
  shows when it steps in.

## Plan

1. **The logger**: `game-run.sh`'s sensor line gains the clusters' and the GPU's allowed maximum
   clock (the vendor's throttling made visible), the skin temperature, the battery's temperature
   and whether it is charging (a phone on USB heats up from charging too).
2. **The boost**: `utdeck-boost.py`, applied by `game-run.sh` with `UTDECK_BOOST=N` (0-1024; off by
   default until measured): every 2 s, `uclamp_min` on every thread of the game's processes (the
   descendants of `game-run.sh`, and the prefix's `wineserver`), so new threads get it too.
3. **A first check without the maintainer**: a CPU-bound test program with and without the boost
   (clocks, rate).
4. **The exit runs, with the maintainer**: a CPU-bound game (Ys: Memories of Celceta or ATRI),
   unplugged, 30 minutes without and 30 minutes with the boost (cooled down in between): frame
   rate, temperatures, the vendor's clock limits, free memory and swap, with hidden Steam running.
5. **Memory**: hidden Steam (about 1 GB, 3.4) plus the largest game (Goat Simulator 2.2 GB, Ys IX
   2.1 GB) over the same runs; whether swap is touched.

## Made

- **The logger** (`game-run.sh`'s `.sensors` line, every 2 s), added at the end so earlier readers
  keep working: the big cluster's temperature (`big_t`; `cpu=` stays the little cluster's, as
  before), swap in use, the allowed maximum clocks of the little and big clusters and the GPU
  (`max=1804/2016/700MHz` when nothing is throttled), the skin temperature, the battery's
  temperature and its state (charging, full, discharging). The sensor files are found once per
  game, by name.
- **The boost**: [`utdeck-boost.py`](../../tools/ut-probe/utdeck-boost.py) `PID UTIL_MIN [LOG]`,
  started by `game-run.sh` when `UTDECK_BOOST` is set (0-1024; off by default, a commented line in
  `defaults.env`). Every 2 s until the game ends: `uclamp_min` on each new thread of the game's
  `wine` process and its descendants, and of the prefix's `wineserver` (found by name and by
  `WINEPREFIX` in its environment); a summary line in the game's log. Wine's service processes
  (`services`, `explorer`, `winedevice`, ...) hang off the system's first process and are not
  boosted: they are not the game. The game's `wine` now runs in the background of `game-run.sh`,
  which waits for it (and for the helper's summary) and keeps its exit code. `runtime-push.sh`
  installs the helper.

## 3. First check, no game (2026-11-20, 20:07-20:12)

`syncbench-x86_64.exe 200000` (stage 3.3's test: requests to Wine's server, and two threads
handing work back and forth, as a game's main and render threads do) through `game-run.sh`,
without and with `UTDECK_BOOST=1024`, alternated:

| round | hand-over, no boost | with boost | server request, no boost | with boost |
| --- | --- | --- | --- | --- |
| 1 | 70.7 µs | 51.9 µs | 16.1 µs | 11.7 µs |
| 2 | 66.3 µs | 56.7 µs | 20.3 µs | 19.5 µs |
| 3 | 84.9 µs | 64.9 µs | 16.5 µs | 20.1 µs |

The hand-over is 15-27 % faster with the boost in every round; single requests vary too much to
say. The big cores were at their top clock (2016 MHz) either way: the gain comes from the
scheduler keeping boosted threads on big, fast cores and from faster clock rises after a wait,
not from a higher clock. The boost reached 3 threads (the program's two and the `wineserver`). A
first sign; the game runs decide.

## 4. The 30-minute runs: ATRI, autoscroll

The maintainer's choice: plugged in (the battery was full; its state is logged, and it was
charging again near the end of run 1), "this Snapdragon 4 Gen 1 will not get hot" (earlier tests
and the Dolphin port). ATRI, our base, from the same save each time, the game's autoscroll, the
screen kept on, hidden Steam running.

**Run 1, no boost** (20:24:43-20:57:31, 1853 s with frames):

| | |
| --- | --- |
| frame rate | mean 57.7 fps; median 65.6; 5 % of seconds below 18.6; lowest 2.3 (scene changes); 95th percentile 82.8 |
| load | CPU 42 % busy, big cores 1910 MHz, GPU 48 % busy (means) |
| hottest | little cores 53 °C, big 52 °C, GPU 49 °C, skin 40 °C (from 30), battery 36 °C |
| memory | at least 2831 MB available, swap up to 138 MB, game up to 1129 MB |
| the vendor | at 20:41:13 (16.5 min in, skin 38 °C) `mi_thermald` lowered the little cores' allowed maximum from 1804.8 to 1708.8 MHz, one step, and kept it there (445 of 898 readings); big cores and GPU never limited |

**Run 2, boost 1024** (21:08:18-21:37:42, 1711 s with frames; the maintainer saw no point in a
cool-down, and the skin was already back to 32 °C, against 30 at run 1's start):

| | run 1, no boost | run 2, boost 1024 |
| --- | --- | --- |
| frame rate, mean | 57.7 fps | **61.7 fps** (+7 %) |
| median / 95th percentile | 65.6 / 82.8 | 69.0 / 91.6 |
| 5th percentile / lowest | 18.6 / 2.3 | 18.7 / 1.7 |
| big cores, mean clock | 1910 MHz | 2013 MHz |
| CPU / GPU busy (means) | 42 / 48 % | 40 / 56 % |
| hottest: little / big / GPU | 53 / 52 / 49 °C | 55 / 54 / 53 °C |
| skin (start, highest) / battery | 30, 40 / 36 °C | 32, 42 / 37 °C |
| the vendor's one step (little cores to 1708.8 MHz) | from 16.5 min | from 5.4 min (it started 2 °C warmer) |
| lowest available memory / swap used / game | 2831 / 138 / 1129 MB | 2838 / 162 / 1133 MB |

The boost lifts the ordinary and the fast scenes by 5-11 %; the slow moments (scene changes,
loading) stay as they are; it runs about 2 °C warmer. `mi_thermald` took one step off the little
cores both times and never touched the big cores or the GPU: the maintainer's point that this chip
does not get hot holds, and a cool-down between the runs made no difference worth waiting for.

**Did the boost reach the game?** The summary said 6 threads. Checked with ATRI started again
(`uclamp-show.py`: `sched_getattr` on every thread of every Wine process; `/proc/<tid>/sched`
does not show the clamp on this kernel): **all 51 of ATRI's threads were boosted**, and
`explorer.exe`'s 13. A thread or process started by a boosted thread inherits its clamp (the kernel
copies it on clone), and ATRI was started from a boosted `steam.exe`; Steam's process supervisor
(`reaper`) then adopts the game's processes, so they are no longer descendants of `wine`, but they
already carry the boost. Wine's services, started from the first `wine` before the boost, are not
boosted. The helper's summary now says "set on N threads (threads they start inherit it)".

**Decided: the boost is the default** (`UTDECK_BOOST=1024` in `defaults.env`); a game's
`utdeck-env` can turn it off. Not measured: battery drain (the phone was plugged in, the
maintainer's choice).

**Memory**: hidden Steam plus ATRI left at least 2.8 GB available, with swap at 140-160 MB, over
both runs. The heavier games (Goat Simulator and Ys IX at 2.1-2.2 GB) left about 1.1-1.2 GB in
stage 3.2b and 3.3; not run for 30 minutes here.

**3.5's exit is met**: frame rate, temperature and free memory over 30 minutes with and without
the boost.

## 5. Goat Simulator with the boost (asked by the maintainer)

The maintainer's question: Goat Simulator on Proton's base (stage 3.3) ran at 6.6 fps standing
still, with the GPU only 47 % busy at 473 MHz on average (its governor, `msm-adreno-tz`, clocked
it down) and the game's threads waiting on each other more than working (main thread 73 % of a
core, UE3's RenderingThread 42 %, DXVK's command thread 17 %). Does the boost change that? The
boost acts on the CPU side only; it helped thread hand-overs by 15-27 % in `syncbench`.

**With the boost** (21:49-21:52, the Proton tool, windowed 960x540, lowest settings, standing
still in the level; `measure.sh 30` and `threads.sh 15`: the busiest threads and how often each
was on a big core, sampled each second):

| | 15:34, no boost | 21:50, boost 1024 |
| --- | --- | --- |
| frame rate | 6.6 fps (154 ms) | **14.1 fps** (71 ms, worst 115) |
| GPU | 47 % busy at 473 MHz | 43-45 % busy at 610-660 MHz |
| main thread | 73 % of a core | 88 %, on a big core in 14 of 15 samples |
| RenderingThread | 42 % | 53 %, big core 15 of 15 |
| `dxvk-cs` | 17 % | 22 %, big core 15 of 15 |
| boosted threads | | all 64 (`uclamp-show.py`) |

More than twice the frame rate. Not yet a comparison: 15:34 was another session (other runs
before it). So the same evening without the boost (`UTDECK_BOOST=0` in the game's `utdeck-env`;
the log's defaults line shows the boost not applied, and `uclamp-show.py` 0 of 64 threads boosted),
the same kind of spot, 21:58-22:00:

| | boost off (21:59) | boost 1024 (21:50) |
| --- | --- | --- |
| frame rate | 5.9 fps (172 ms, worst 317) | **14.1 fps** (71 ms, worst 115) |
| GPU | 42-45 % busy at **371-382 MHz** | 43-45 % busy at **610-660 MHz** |
| main thread | 109 % of a core (samples a little over 1 s apart), big core 13 of 15 | 88 %, big core 14 of 15 |
| RenderingThread | 63 %, **on a big core 3 of 15** | 53 %, big core 15 of 15 |
| a second game thread | 50 %, big core 1 of 15 | 12 %, big core 13 of 15 |
| `dxvk-cs` | 27 %, big core 9 of 15 | 22 %, big core 15 of 15 |

**The boost makes Goat Simulator 2.4 times faster.** Without it, the scheduler (Energy-Aware
Scheduling, which puts light threads on the little cores to save power) keeps UE3's render thread
and a second game thread on the little cores, because they look light: they wait for each other
much of the time. The main thread then waits on a slow render thread, frames reach the GPU slowly,
and the GPU's governor lowers its clock into the 300s, which slows the chain once more. With the
boost every thread stays on the big cores, the hand-overs are quick, the GPU gets frames sooner and
clocks up by itself. The game is the same; only where its threads run changed.

**Goat Simulator on our base with the boost** (22:04-22:08; `UTDECK_RUNTIME=utdeck` in its
`utdeck-env` for this run, the default build with 0001-0013, the same settings): the first level
started loading at 78 s (105 s without the boost on 2026-11-20 afternoon), then **the same crash**:
UE3's "Pure virtual function being called" at 118 s (238 s before) and "Rendering thread
exception" at 122 s; the maintainer saw the same error window; the frames had stopped 38 s after
the first one, during the loading screens. Faster, but the same failure: it does not depend on
timing the way a plain race would, it is something our Wine 11.16 does differently from Proton's
during UE3's level load. **Goat Simulator stays on Proton's base** (its own setting removed again,
so Steam's tool choice applies), where it now runs at 14 fps.

What this means for later stages: the table's frame rates for CPU-bound or thread-bound games
(Ys: Memories of Celceta, ATRI, Goat Simulator) were measured without the boost and are low; the
GPU-bound ones (Ys IX, Stray) less so. The 3.3 comparison of the two Wine bases was also without
it. Worth measuring again with the boost on: the table's games, and Goat Simulator on our base
(where it fails at its first level, a failure that looked like two threads racing).


## Events

- 2026-11-20: 3.5 started; the phone's clocks, heat control and memory read; plan written.
- 2026-11-20: the logger and `utdeck-boost.py` made; first check with `syncbench`: thread
  hand-overs 15-27 % faster with the boost.
- 2026-11-20: the two 30-minute ATRI runs (57.7 and 61.7 fps, 2 °C apart, one vendor step on
  the little cores); the boost checked on all 51 of ATRI's threads; the boost made the default.
  **3.5 done.**
- 2026-11-20: Goat Simulator (the maintainer's request): 14.1 fps with the boost, 5.9 without, the
  same evening and spot; without it the render thread ran on the little cores and the GPU clocked
  down to 371-382 MHz.
- 2026-11-20: Goat Simulator on our base with the boost: the same crash at the first level,
  sooner; it stays on Proton's base.
