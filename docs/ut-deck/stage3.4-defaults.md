# Stage 3.4: defaults that remove losses

Goal (roadmap): each default a game gets measured on the phone, kept or dropped with a number, and
kept in one place. Exit: every default on the list kept or dropped with a measurement; one place
where the defaults live.

Hardware safety: nothing on this list touches clocks, voltages, charging or thermal limits; the
vendor's thermal service stays in charge (3.5 covers heat). Turnip options change how the driver
uses the GPU, not its limits.

## Where the defaults live today (2026-11-20)

| place | what it sets |
| --- | --- |
| `wine-env.sh` (on the phone `wine/env.sh`) | the runtime (`UTDECK_RUNTIME`), `WINEDEBUG=-all` for tools, Mono and Gecko off, our driver (Turnip, zink for EGL), GStreamer's plugin folder and list, `PULSE_PROP` (volume keys) |
| `game-run.sh` | Wayland, `MESA_VK_WSI_PRESENT_MODE=mailbox`, `UTDECK_WAYLAND_LANDSCAPE=1`, `UTDECK_FILL=1`, DXVK and VKD3D-Proton as native DLLs, `mshtml` off (Mono's `mscoree` on), the game's Wine log level `err+all,fixme-all`, frame statistics and sensors |
| `utdeck-compat` | the Wine base and prefix, per-game `utdeck-env`, Steam's Vulkan layers off (overlay, fossilize), Steam Frame leftovers removed, Steam Input without uinput, FEX for Steam's install scripts, the Visual C++ runtime repair, XInput put back |
| each prefix's `fex-emu/Config.json` | FEX's options (written by FEX, not by us) |
| not set at all | DXVK's and VKD3D-Proton's log level (DXVK logs `info` into the game's log), Turnip options except per game |

Prefixes: 1.0-1.6 GB each, 15 GB for the 13 on the phone (known issue K5).

## The list, and how each is measured

Without the maintainer (test programs, timers, sizes), first:

| # | default | measurement |
| --- | --- | --- |
| 0 | The default runtime with patches 0012 and 0013 (built, not yet installed as the default) | the stage 2 graphics tests against the installed one, alternated, screen on |
| 1 | Log output: the game's Wine log level, DXVK's and VKD3D-Proton's log level | frame rate and frame-time spikes of the Direct3D test programs, each way |
| 2 | `tu_dont_reserve_descriptor_set=true` for every game (Direct3D 12 needs it on Adreno 6xx; set for Stray only now) | Direct3D 9, 11 and 12 tests with and without |
| 3 | `tu_override_uncached_as_cache_coherent=true`, which FEX sets for Turnip in every process it runs in | the same tests with FEX's value and with Turnip's own |
| 4 | Wine's start-up time | from the start of a test program to its first frame, cold and warm |
| 5 | Smaller prefixes (K5): what fills 1 GB, and links to the runtime's files instead of copies | size and start-up time |
| 6 | Wine Mono in every new prefix, as Proton does (Goat Simulator needed it: 33 s, +332 MB) | counted against the games that need it |
| 7 | Hidden Steam's own memory (1.3 GB) and CPU (9-13 % of one core) | its options, each measured with `steam-service.sh status` |

With the maintainer (games, the screen):

| # | default | measurement |
| --- | --- | --- |
| 8 | Box64 or FEX for 32-bit games, and Box64's speed options | ATRI and Ys: Memories of Celceta (both CPU-bound), long alternated runs |
| 9 | Shader stalls of 150-700 ms in new areas (Celceta): Mesa's shader cache (32 MB now), Steam's pipeline cache (fossilize, off, K8) | the same walk twice, cold and warm, worst frame times |
| 10 | DirectShow's window renderer (K10): wined3d's `renderer=no3d` or `vulkan` | `vidtest` with Ys's two movies, the maintainer watching and listening |
| 11 | FEX's memory-order options (TSO) per game, `DOTNET_EnableWriteXorExecute=0` | where a game shows a difference; noted when no game on the table can show one |

## One place for the defaults

Planned: `defaults.env` (on the phone `wine/defaults.env`), one `NAME=value` per line with the
measurement that decided it in a comment, read by `game-run.sh` for every game; a game's own
`utdeck-env` still wins. The scripts keep only what is not a choice (paths, Steam's fixes).

## 0. The default runtime with 0012 and 0013

Built from the current patch files (`PARTS=wine`), installed as `runtime-11.16-20261120`; the one the
games were tested on kept as `runtime-11.16-20261120-games`. `ab-gfx.sh 2`, alternated, phone
upright, screen on (17:57-18:09):

| test | tested build (round 1 / 2) | new build (round 1 / 2) |
| --- | --- | --- |
| vktest x86-64 fullscreen | 230.4 fps / hung | 222.5 fps / hung |
| vktest ARM64 window | 525.7 / 534.6 | 540.2 / 540.9 |
| d3d11test window | 339.7 / 323.7 | 341.4 / 353.3 |
| d3d11test fullscreen | 123.0 / 122.3 | 121.8 / 122.5 |
| d3d11test mode 1280x720 | **128.4 / 134.6** | **105.5 / 108.1** |
| d3d11test mode 1920x1080 | 91.9 / 91.2 | 91.8 / 90.7 |
| d3d9test x86 / x86-64 | 110.6, 112.3 / 110.2, 114.4 | 111.1, 114.8 / 112.9, 115.7 |
| d3d12test window / fullscreen | 357.0, 127.7 / 341.3, 127.1 | 358.9, 123.4 / 361.7, 129.4 |

The hang of the x86-64 Vulkan test in round 2 is old (stage 3.3, step 3: after Lomiri resizes its
window) and hits both. Everything is equal but the 1280x720 mode. Traced (`sp1280.sh`, buffers
handed to Lomiri and the window's requests): the tested build takes **one of two paths from run to
run**: once 2400x1080 buffers (a landscape picture for the upright screen) with few commits, once
1080x2400 buffers at 96.1 fps; the new build gave 1080x2400 every time, 101.5 fps. So the tested
build's 128-134 fps were runs drawing a picture that does not fit the screen, step 4's lesson again.
The case is artificial (a landscape monitor reported while the phone is upright, so that the test
gets landscape modes); the real one, phone sideways, is measured next.

Phone sideways, screen on (`modes-ab.sh`: both display mode tests on both builds, two rounds
alternated, then one traced run each for the buffers):

| mode | tested build | new build | buffers to Lomiri (both) |
| --- | --- | --- | --- |
| 1280x720 | 98.6 / 102.3 fps | 103.3 / 99.4 fps | 2400x1080, fits the screen |
| 1920x1080 | 85.2 / 86.1 fps | 85.2 / 85.7 fps | **1080x2400**: a portrait picture on the landscape screen |

Equal. The 1920x1080 portrait picture is on both builds, so not new: known issue K1 again (a window
going fullscreen while the phone is sideways gets a portrait size from Lomiri), now seen in a test
program too. **Kept: the build with 0012 and 0013 is the phone's default** (`wine/runtime`); the
tested one stays as `runtime-11.16-20261120-games` for going back.

## 2. `tu_dont_reserve_descriptor_set`: no cost, but not for every program

`ab-env.sh 2` without and with the option (18:50-19:00, upright, Lomiri checked): equal in every
test (d3d11test fullscreen 122.3 / 123.7 without, 123.5 / 121.7 with; d3d12test fullscreen 129.6 /
128.4 without, 128.9 / 132.8 with; the rest within the same spread). The 1280x720 mode switched
paths even with the same setting (127.3, then 109.3 fps without the option), which confirms it
measures Lomiri's choice.

**But not as a default for every game.** In Turnip (`tu_device.cc`, `tu_shader.cc`,
`tu_pipeline.cc`) the reserved set holds dynamic buffer descriptors (`UNIFORM_BUFFER_DYNAMIC`,
`STORAGE_BUFFER_DYNAMIC`); with the option there is none, yet Turnip still reports dynamic buffers
as supported, and a program using them hits a failed `assert`, compiled out in our release build,
so it would read from the wrong place. DXVK and VKD3D-Proton never use them; **zink does** (Mesa's
OpenGL on Vulkan, for every OpenGL program's default uniform block), and Turnip reads the option
once per process, so as an environment variable it would also reach zink inside a game: OpenGL
games and Wine's own wined3d (DirectDraw, old Direct3D). Turnip already has built-in defaults per
engine (`00-turnip-defaults.conf`, `<engine engine_name_match="vkd3d">`, matched on the engine name
a program gives Vulkan). **Mesa patch 0006** adds the option there: VKD3D-Proton gets it, nothing
else does.

Checked on the phone with the new build in a test folder and the variable also set: Mesa printed
"ATTENTION: option value of option tu_dont_reserve_descriptor_set ignored" (its notice that a
built-in value matched the program) for d3d12test only, not for d3d11test. Installed as the live
driver (`zink/lib/libvulkan_freedreno.so`; the previous one kept as `.pre0006`). Stray's own
`utdeck-env` line stays until Stray with `-dx12` has run on the built-in default.

## 3. Background: who sets `tu_override_uncached_as_cache_coherent`

FEX's Windows DLLs (`Source/Windows/Common/EnvironmentVariablesHandling.cpp`) set it to `true`
through Wine's `__wine_set_unix_env` in every process they are loaded into, unless it is already
set. Their reason: on ARM, GPU memory that the CPU sees uncached makes emulated x86 code slow (it
uses many ordered memory operations), and Turnip can hand out cached, coherent memory instead where
the hardware allows; the only cost named is the GPU checking the CPU's caches. So it applies to
emulated x86 programs only, and is measured by setting it to `false` for the x86 tests.

## 1. Log output: first run, not usable (18:21-18:30)

`ab-env.sh 2`, all logs off against today's game logs (Wine `err+all,fixme-all`, DXVK `info`,
VKD3D-Proton `warn`). Two faults:
- With DXVK's log on, `rt-gfx.sh` lost every Direct3D result: it keeps the first 25 lines of each
  test's output, and DXVK's own lines filled them. Fixed: log lines no longer count.
- The numbers jumped without a cause in the settings (d3d11test fullscreen 77.9 fps in round 1,
  204.1 in round 2, 122 an hour before), and the picture sizes changed during the run (vktest
  2400x1080, then 1080x2400): the phone's orientation or Lomiri's state changed (asking Lomiri
  whether it was locked timed out). Repeated with the phone's state checked first.

**The cause: Lomiri hung.** The maintainer (the phone never moved): the left 45 % of the screen
showed a test's picture, frozen, the right 55 % the wallpaper, after the run had ended and no Wine
process was left. Lomiri used no CPU in 3 s; its main threads and all three render threads
(`QSGRenderThread`) waited on locks (`futex_wait_queue_me`); it still logged new connections until
18:30:08, so it stopped drawing but kept accepting. From the run's numbers it froze about
18:22-18:23, during the display mode tests of round 1 with the phone sideways (from the Direct3D 9
test on, the rates are impossible: 195 fps where it was 110). Every number after that was a
picture nobody drew. Its thread states and log are kept in `build/lomiri-hang-20261120/` (not in
git). The second Lomiri failure under our tests, after this morning's crash with ATRI (stage
3.3); a test program must not be able to stop the shell. Restarting Lomiri's service
(`systemctl --user restart lomiri-full-greeter.service`, by the maintainer) did not bring it back;
the phone needed a hard restart. From here, `ab-env.sh` asks Lomiri before each run whether its lock
screen is up (D-Bus, 5 s limit; the question timed out during the hang) and stops the run if
Lomiri does not answer or the phone is locked. Sideways display mode tests stay out of automated
runs until the hang is understood.

## 1. Log output: measured (18:37-18:50, phone upright, Lomiri checked before each round)

`ab-env.sh 2`, all logs off against today's game logs (Wine `err+all,fixme-all`, DXVK `info`,
VKD3D-Proton `warn`):

| test | logs off (round 1 / 2) | today's logs (round 1 / 2) |
| --- | --- | --- |
| vktest x86-64 fullscreen | 232.5 / 226.9 fps | 231.2 / 231.0 |
| vktest ARM64 window | 560.3 / 552.5 | 568.2 / 596.9 |
| d3d11test window | 343.5 / 330.6 | 331.6 / 357.5 |
| d3d11test fullscreen | 120.8 / 121.5 | 121.8 / 122.3 |
| d3d11test mode 1920x1080 | 92.6 / 91.9 | 92.4 / 94.4 |
| d3d9test x86 / x86-64 | 112.9, 114.8 / 112.4, 117.1 | 111.5, 118.2 / 112.6, 117.5 |

Direct3D 12 (its results were pushed out of `rt-gfx.sh`'s 25 lines by VKD3D-Proton's log, whose
lines start with a timestamp; filter fixed), three alternated runs each: window 373.4 / 371.8 /
374.4 fps off, 375.8 / 382.1 / 370.2 on; fullscreen 130.5 / 130.2 / 129.3 off, 130.2 / 131.1 /
129.7 on. Worst frames vary from run to run either way.

**No cost.** The one difference, d3d11test at display mode 1280x720 (133.6 / 129.6 off, 108.9 /
107.1 on), is step 0's two picture paths again: which one Lomiri takes depends on timing, here the
slower start with logging, not on the build. That corrects step 0's reading (the build was not
the cause either), and leaves that test, upright with a landscape monitor reported, out of every
comparison: it measures Lomiri's choice of path. Log size: a normal game session's log is small
(Goat Simulator's whole session 448 kB); the large ones (3.5 MB) were runs with Wayland tracing on.
**Kept as they are**: Wine at errors, DXVK at `info`, VKD3D-Proton at its default; they are what is
read first when a game fails.

### Measured (19:04-19:14, upright, Lomiri checked)

`ab-env.sh 2`, FEX's choice (unset, so `true` in emulated x86 processes) against
`tu_override_uncached_as_cache_coherent=false`:

| test | FEX's choice (round 1 / 2) | off (round 1 / 2) |
| --- | --- | --- |
| vktest x86-64 fullscreen | 228.9 / 224.5 fps | 227.2 / 233.9 |
| d3d9test x86 fullscreen | 114.2 / 113.0 | 113.9 / 113.6 |
| d3d9test x86-64 fullscreen | 117.9 / 116.1 | 117.2 / 115.7 |
| the ARM64 tests (not affected) | equal | equal |

No difference. The limit: these programs hardly write GPU memory from x86 code, which is where
FEX expects the gain (a game filling buffers through Direct3D's `Map`; DXVK itself is ARM64EC, native
code). **Kept as FEX sets it**, since its cost was not measurable either; a CPU-bound x86-64 game
filling buffers every frame would show it, none of the table's is (Ys IX and Stray are GPU-bound).

## 4. Wine's start-up time

Measured on the phone (19:16-19:25, `prefix-utdeck-gfx`, up to date):

| step | time |
| --- | --- |
| `game-run.sh`'s GStreamer plugin-list check | 0.22 s |
| `wine cmd /c exit` (an empty program), fresh Wine server | 2.8 / 3.2 s |
| the same with the Wine server kept running (`wineserver -p`) | 3.2 s: no gain |
| `d3d11test` from start to its first frame | 4.2 / 4.0 s |

Where Wine's ~3 s go (`+timestamp,+process,+loaddll`): `wineboot` (0 s), `winemenubuilder -a -r`
(0.08), `services` (0.17), the first `winedevice` (0.60), `plugplay` (0.96), `svchost` (1.16), a
second `winedevice` (1.48) loading the device drivers one after another (controllers: `winebus`,
`winehid`; USB `wineusb`, Bluetooth `winebth`), `wineboot` loading the shell libraries, then the
program at 2.46 s. Spread over Wine's whole service chain: nothing large to remove. Turning off
USB and Bluetooth passthrough would save about 0.1-0.2 s at some risk to controllers: not done.

**`winemenubuilder` off** (Proton does the same): it writes Linux menu entries and file types for
Windows programs into the home folder, and had: `wine-extension-*.desktop` (chm, crt, hlp, msp,
reg, vbs, a 3DMark 11 result type), a `wine/Programs` menu folder with a 3DMark 11 entry, MIME
files in `~/.local/share/mime/packages`, a menu in `~/.config/menus/applications-merged`. Turning
it off saved no time (5.6-6.1 s with it, 6.0-6.4 s without, three alternated runs each, Wine
server shutdown included): it runs alongside. It goes for what it leaves behind. Now in
`defaults.env` (`UTDECK_DLL_DEFAULTS`, which `game-run.sh` adds to every game's DLL overrides,
together with `mshtml` off) and in `env.sh`'s own default, so new prefixes do not run it either.
Checked with `game-run.sh` and a test program: the override list is right, and a value set by the
caller (`UTDECK_FILL=0`) was kept, not replaced by the default.

## 5. Smaller prefixes: measured, then parked as its own task

What fills a prefix (Ys: Memories of Celceta's, 1.5 GB): `system32` 787 MB, `syswow64` 301 MB,
`mono` 241 MB, the rest about 140 MB. In `system32`, **614 of 734 programs and libraries (561 MB)
are byte-for-byte copies of the runtime's** (`lib/wine/aarch64-windows`, `arm64ec-windows`): Wine
copies its builtin DLLs into each prefix. Links to the runtime instead would save about 560 MB per
prefix, about 7 GB for the 13 on the phone. Proton does this (its script links the prefix's
builtins to its own files).

Why not now:
- **Wine's prefix update writes through links.** Our Wine 11.16 (`setupapi/fakedll.c`,
  `create_dest_file`) opens each existing builtin file for writing, truncates it and writes the new
  version: through a link, into the runtime's file. Proton's Wine checks first
  (`fake_dll_matches`: "If it's a symlink, assume it was put there by the proton script and is
  therefore correct", and an identical file is left alone too); ours has no such check. A port is
  about 145 changed lines (part of it the age difference between 11.0 and 11.16), and Proton's
  original commit is not in our shallow clone.
- **Installers write into `system32` too** (our Visual C++ repair runs Microsoft's installer, the
  games' DirectX setups). One that writes a temporary file and renames it replaces the link, but one
  that writes straight to the file name would overwrite the runtime's file, for every prefix at
  once. Proton repairs its links at each start; whether its Wine also guards writes was not
  settled by reading.
- The gain is disk space only, no speed.

**Parked as its own task** (after 3.4): the update guard ported, the runtime made read-only so that
any write through a link fails visibly, the links made by `utdeck-compat` after `wineboot`, then
the table's games and their installers run on linked prefixes. Space is not short now (14 GB free
after the old test runtimes were removed).

## 6. Wine Mono in every prefix: already so on our base, now on both

Prefixes on the phone with `drive_c/windows/mono` (241 MB each): on our base 6 of 7 (all but PvZ
Replanted's, made before 2026-11-19's `mscoree` fix); on Proton's base only Goat Simulator's (put
there by hand). Since `game-run.sh` stopped turning `mscoree` off (2026-11-19), Wine's own prefix
update installs the runtime's Mono at a game's first start on our base: Mono in every prefix was
already the default there, without anyone deciding it. Proton's Wine does not, so a .NET game on
the second tool fails as Goat Simulator did until Mono is put in by hand.

**Kept, and made explicit on both bases**: `utdeck-compat` installs the runtime's Mono
(`share/wine/mono/wine-mono-*.msi`, found from either runtime) into any prefix that lacks it, and
logs it. Cost: 33-39 s and 241 MB, once per prefix (the 241 MB would be shared through item 5's
links later); later starts check whether a folder exists. Of the table's games one needs it (Goat
Simulator); the cost of missing it is a game that does not start.

## 7. Hidden Steam's own memory and CPU

Three ways (`steam-opts.sh`: start, sign-in, one minute to settle, then `steam-service.sh status`:
memory as PSS, the process's share of RAM, and one minute of CPU time), 19:20-19:28:

| client options | signed in after | memory (PSS) | CPU, idle |
| --- | --- | --- | --- |
| none (today's) | 12 s | 986 MB in 13 processes | 12 % of one core |
| `-cef-disable-gpu -cef-disable-gpu-compositing` (the web helper's GPU drawing off) | 16 s | 993 MB in 10 | 12 % |
| `-nofriendsui -nochatui` (friends and chat interfaces off) | 6 s | 1049 MB in 10 | 12 % |

No gain from either: **kept as it is** (no extra options; `steam-service.sh start` takes
`UTDECK_STEAM_EXTRA` for later tries). The idle cost (about 1 GB and 12 % of one core) belongs to
3.5's memory and heat work.

On the way: when Steam was not running, `steam-service.sh stop` still sent `steam -shutdown`, which
starts a new client; without the hidden screen it showed "Unable to open a connection to X"
(`zenity`) in a window Lomiri could not draw: the maintainer saw an "unknown app" with an endless
spinner. The boxes were closed; `stop` now sends the shutdown only to a running client.

## Where 3.4 stands (2026-11-20, evening)

| # | default | result |
| --- | --- | --- |
| 0 | the default runtime with 0012 and 0013 | installed (equal sideways; the upright 1280x720 test measures Lomiri's choice of picture path) |
| 1 | log levels | kept (no cost) |
| 2 | `tu_dont_reserve_descriptor_set` | for VKD3D-Proton only, built into Turnip (Mesa patch 0006) |
| 3 | `tu_override_uncached_as_cache_coherent` | kept as FEX sets it (no difference in the tests) |
| 4 | start-up | `winemenubuilder` off; Wine's ~3 s spread over its services |
| 5 | smaller prefixes | parked as its own task (two Wine changes needed first) |
| 6 | Wine Mono | in every prefix, both bases |
| 7 | hidden Steam's options | kept (no gain) |
| 8-11 | Box64 or FEX for 32-bit games; shader stutter; DirectShow's renderer (K10); Stray with Direct3D 12 on the built-in default | **open**, left for a game round with the maintainer; the maintainer moved on to 3.5, which does not depend on them |

`defaults.env` holds the decided defaults; `winemenubuilder`'s leftovers (11 files: file types,
a `wine/Programs` menu with a second 3DMark 11 entry, MIME files) were removed with the
maintainer's OK.

## Events

- 2026-11-20: 3.4 started (after 3.8); inventory of where the defaults live; plan written.
- 2026-11-20: items 0-7 measured and decided (Mesa patch 0006, `defaults.env`, Mono in every
  prefix, `winemenubuilder` off); a Lomiri hang during a run (hard restart; Lomiri is now checked
  before each round); items 8-11 left open; the maintainer moved on to 3.5.
