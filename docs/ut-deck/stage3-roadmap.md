# Stage 3: a runtime we build and own, the plan

Written 2026-11-18, after [stage 2](stage2-roadmap.md). Goal (from
[feasibility-and-roadmap.md](feasibility-and-roadmap.md)): a reproducible runtime, tuned for Halium
phones. Exit of the whole stage: the runtime builds from a script, and the compatibility table has
ten or more games.

Terms used here:
- *runtime* = everything between the game and our GPU driver: Wine, the x86 translators (FEX,
  Box64), DXVK, VKD3D-Proton, Wine Mono.
- *ARM64EC* = the Windows ABI that lets ARM64 code and x86-64 code live in one process: Wine's own
  DLLs and DXVK run as native ARM64 code, only the game's code goes through FEX.
- *ARM64X* = one DLL file with both a plain ARM64 and an ARM64EC half, so it loads in both kinds of
  process.
- *WoW64* = how a 64-bit Wine runs 32-bit Windows programs; on ARM the 32-bit x86 code goes through
  `libwow64fex.dll` (FEX) or `wowbox64.dll` (Box64).
- *wineserver* = Wine's background process that owns shared objects (events, mutexes, windows); a
  game that synchronizes its threads through it pays a round trip each time.
- *esync, fsync, NTSYNC* = three ways to do those waits without the round trip: esync with
  `eventfd`, fsync with the `futex_waitv` system call (Linux 5.16), NTSYNC with `/dev/ntsync`
  (Linux 6.14).

## What we know before starting

What stage 2 left: the phone runs **Hangover 11.16** as Hangover publishes it (prebuilt Ubuntu 24.04
packages, unpacked into a folder), with two files swapped for our builds (`winewayland.so` and
`win32u.so`, patches 0001-0006), DXVK 2.7.1 from Hangover's tarball, and Wine Mono 11.3.0. It is
2.4 GB, mostly unstripped DLLs and build files (import libraries, headers) a runtime does not need.

Checked for this plan (2026-11-18):

1. **Hangover's recipe can be rebuilt by us.** Each piece is a pinned source plus a short build
   recipe in Hangover's `.packaging/` and `.github/workflows/deb.yml`:
   - Wine: AndreRH/wine `3ea3a149` (we already build from it), configured
     `--enable-archs=arm64ec,aarch64,i386` with llvm-mingw;
   - FEX: AndreRH/FEX `fa556167`, CMake with FEX's `toolchain_mingw.cmake`, targets `wow64fex`
     (aarch64) and `arm64ecfex` (arm64ec);
   - Box64: AndreRH/box64 `2f130fab`, CMake `-DARM_DYNAREC=ON -DWOW64=ON`, target `wowbox64`;
   - DXVK: AndreRH/dxvk `2c50a567` = upstream v2.7.1 plus four commits (an ARM64EC cross file and
     packaging), built with DXVK's `package-release.sh`.
2. **Proton 11 builds for ARM64 too** (`configure.sh --target-arch=arm64`, Steam Runtime 4's arm64
   SDK image, FEX, DXVK and VKD3D-Proton as submodules). But for this phone:
   - its Wine (ValveSoftware/wine at Proton 11.0-2e, `e60892cc`) is **Wine 11.0** plus Valve's
     patches, older than the 11.15 fix for our 39-bit address space (built with `-static-pie` it
     would start anyway, stage 0);
   - **it has no esync any more**: only fsync, which needs `futex_waitv` (Linux 5.16), and NTSYNC.
     Stone's kernel 5.4 has neither, so on this phone Proton waits through `wineserver` exactly as
     upstream Wine does. The roadmap's main reason for Proton ("esync") does not hold;
   - what it would still bring: hundreds of per-game fixes, its video playback path (media
     converter), Steam integration (`lsteamclient`), and Valve's own testing of ARM64EC on the
     Steam Frame;
   - its build expects Steam Runtime 4 (Debian 13, glibc 2.41); Ubuntu Touch has glibc 2.39, so its
     binaries cannot run outside Valve's container as built. Its Wine source can be built in our
     own container instead.
3. **VKD3D-Proton 3.1** (2026-10-28) builds ARM64X DLLs (`--build-arm64x`) and lists "Steam Frame
   is now a supported platform, ... fixes catering to QCOM hardware". Whether Direct3D 12 works at
   all on the Adreno 619 is not known yet.

## Substages

Each substage needs the one before it.

| Substage | Goal | Exit |
| --- | --- | --- |
| **3.1 The runtime from one script** (done) | One script in the cross container builds Wine (Hangover's source, our patches), FEX's two DLLs, Box64's `wowbox64.dll`, DXVK 2.7.1 and VKD3D-Proton 3.1 from pinned sources into one stripped folder, with Wine Mono; installed on the phone next to Hangover | The stage 2 tests pass on it unchanged; no Hangover file is left in use; size and start-up time against Hangover's; a Direct3D 12 smoke test |
| 3.2 The game list and the first table (closed, no entries) | The maintainer's games that run without Steam | The copies at hand carried DRM bypasses; replaced by 3.S and 3.2b |
| **3.S Steam far enough to test games** (done) | Valve's ARM64 client hidden as a service; UTDeck's runtime as its compatibility tool; Steam's API for games; videos, sound volume, fill mode | A Steam game starts, plays and quits through our runtime |
| **3.2b The table from the Steam library** (exit met) | Five or more of the maintainer's Steam games, checked for DRM bypasses, measured | Five games: Direct3D 9, 11, 12; 32- and 64-bit; Box64 and FEX; five engines |
| 3.3 The Wine base: ours or Proton's | Proton 11's Wine built in our container, our patches carried over, measured against ours on the table's games and the stage 2 tests; the cost of `wineserver` waits on this kernel measured, and whether an esync port is worth carrying | A decision with numbers, written down; the chosen base in the build script |
| 3.4 Defaults that remove losses | Each default measured on the phone and kept in one place (the list below) | Each default kept or dropped with a measurement; one place where the defaults live |
| 3.5 CPU boost, heat and memory | `uclamp_min` for the game's threads as in the Dolphin port; long runs with CPU and GPU load together; memory under hidden Steam (1.3 GB) plus a game (up to 2.5 GB) on an 8 GB phone; the vendor's thermal service stays in charge, no limit is raised | Frame rate, temperature and free memory over 30 minutes with and without the boost |
| 3.6 Lifecycle | What Wine and the game do when the screen locks or another app comes to the front: games started by hidden Steam are not Lomiri apps, so Lomiri may not pause them (SIGSTOP) as it pauses apps; sound, timers, the gamepad, the GPU | Pause and resume work in two games, or the problems are written down with a plan |
| 3.7 Linux x86 games | Games with a native Linux build (Goat Simulator's) through FEX in Steam's Linux runtime, against the same games' Windows builds through our runtime | Two games measured both ways; a rule for which build UTDeck installs |

The defaults for 3.4, collected from ARMDeck, DroidDeck, 3.S and 3.2b:
- From before: `DOTNET_EnableWriteXorExecute=0`, FEX TSO and its other options per game, DXVK's
  config and shader cache, `TU_DEBUG` options for Adreno 6xx, debug output off, Wine's start-up time.
- `tu_dont_reserve_descriptor_set=true` for every game (Direct3D 12 needs it on Adreno 6xx; set per
  game for Stray now): does it cost anything in Direct3D 11 games?
- Wine Mono in every new game prefix, as Proton does (Goat Simulator needed it; 33 s, +332 MB).
- Box64 or FEX for 32-bit games, per engine (KiriKiri was CPU-bound at 100 % of one core), and
  Box64's speed options.
- `tu_override_uncached_as_cache_coherent=true`, which FEX sets for Turnip in every process it runs
  in (3.3): never measured on this phone.
- Shader stalls of 150-700 ms in new areas (Ys: Memories of Celceta): DXVK's state cache, and
  Steam's pipeline cache (fossilize, off now; 3.S known issue K8).
- Smaller prefixes (about 1 GB each, 3.S known issue K5): links to the runtime's files.
- Hidden Steam's own memory (1.3 GB) and CPU (9-13 % of one core).
- DirectShow's window renderer (3.S known issue K10): it hangs or drops frames; games that play
  movies in a plain window need it.

Order and why: 3.1 first, because every later number should come from the runtime we build, not
from a prebuilt one. 3.2 (then 3.S and 3.2b) next, so that the base decision (3.3) and every default
(3.4, 3.5) is measured on real games instead of on our samples. The table grows through 3.3-3.7
until it has ten or more games (the stage exit); five are in it now.

### What 3.S and 3.2b changed in this plan

- **Steam is here already.** Games come from the maintainer's Steam library through hidden Steam and
  UTDeck's compatibility tool; the rule "games that need Steam wait for stage 7" no longer applies.
  Stage 7 keeps the other stores and Steam's own interface.
- **The pins moved.** FEX is upstream FEX-2611.1 (Hangover's crashed 64-bit games); Box64 carries an
  upstream fix (`patches-box64/`); Wine carries eleven patches; Mesa five. 3.3 starts from these.
- **What 3.3 is measured on is known now:** five games with saved spots (Ys IX, Stray) and
  repeatable frame-rate measurements (`measure.sh`), plus Goat Simulator's open hang as a test
  case for the other base.
- **Memory** is a limit next to heat (0.7 GB left with Stray running): 3.5 measures both.
- **Lifecycle** changed shape: games are started by a background service, not by Lomiri (3.6).
- **Linux builds** of games are a separate path (3.7), asked for by the maintainer.

### Open items from 3.S and 3.2b that belong to later stages

| Item | Stage |
| --- | --- |
| A game started sideways stays portrait (K1); landscape-only games are reshaped to portrait when the phone turns (PvZ); a maximized game window keeps Lomiri's top bar (ATRI) | 4: UTDeck's library starts games as a Lomiri app, with a per-game orientation setting (landscape by default) and fullscreen windows |
| A publisher's agreement (EA's for PvZ) shown on hidden Steam's invisible screen | 4: the library shows it to the user; UTDeck never accepts one |
| Games with a Linux build need a per-game compatibility tool entry to get the Windows build (`set-default.py --app`) | 4: the library writes it (after 3.7's rule) |
| Steam's own interface (K2) | 4 or 7 |
| A controller connected while a game runs is not seen (K3); Steam Input without `uinput` | 5: the input service and virtual controller (`uinput`) |
| No on-screen keyboard in Windows programs (Lomiri does not offer Wine the Wayland input-method protocol) | 5: touch input and the in-game menu |
| The Nothing Phone (UT 24.04-1.x, a stronger GPU): runtime check and the GPU-bound games (Ys IX, Stray) | 8, or earlier in 3.5 if the runtime runs there unchanged |

**For the maintainer, ahead of 3.3:** five more games bring the table to ten. Useful kinds: an XNA
or .NET game (Terraria: Wine Mono), a GameMaker game (Undertale, Hue), an OpenGL game, an older
DirectX 9 game with movies in a plain window (DirectShow, K10), another visual novel, and a light
Unity game (Slime Rancher, INSIDE, Firewatch). Games with a Linux build need the Windows build
forced (`set-default.py --app`) or wait for 3.7.

## Status

**Plan change (2026-11-19):** Steam comes forward from stage 7, so the table can use the
maintainer's Steam library: new stage **3.S** (Steam far enough to test games), then **3.2b** (the
game list and table from Steam). See [plan-change-steam-early.md](plan-change-steam-early.md).

- **3.1 (2026-11-18): done.** Notes:
  [stage3.1-runtime-build.md](stage3.1-runtime-build.md). `build-runtime.sh` builds Wine (our six
  patches), FEX, Box64, DXVK 2.7.1, VKD3D-Proton 3.1 and Wine Mono from pinned sources in 3-5
  minutes, identical byte for byte between builds; 1.1 GB (Hangover: 2.4 GB), prefixes 974 MB
  (2.3 GB). Same results and speed as Hangover in every stage 2 test; sound checked; **Direct3D 12
  runs on the A619** (feature level 11_1, shader model 5.1). Now the default on the phone; M9: the
  gamepad and 3DMark 11 (3D, sound, filling the screen) work on it.
- **3.2 (2026-11-19): closed with no table entries.** Notes: [stage3.2-games.md](stage3.2-games.md).
  The games at hand carried DRM bypasses (one was found only after testing, its results withdrawn)
  or had been broken by a mod pack. Kept: Wine patches 0007 (frame statistics) and 0008 (display
  modes with the screen's shape), `game-run.sh`, `push-d3dx9.sh`, a bypass check before every
  first start. Next: 3.S, Steam.
- **3.S (2026-11-19): done** (parked parts and known issues listed in its notes). Notes: [stage3S-steam.md](stage3S-steam.md). Valve's
  ARM64 Steam client runs hidden as a service on Ubuntu Touch; UTDeck's runtime is Steam's default
  compatibility tool; Proton's `lsteamclient` and Wine patch 0009 give Windows games Steam's API
  (Ys: Memories of Celceta runs from Steam, with audio, touch and controller); Wine patch 0010
  shows a game window smaller than the screen filling it; games start and quit cleanly (step 6);
  games' videos play through the runtime's own GStreamer plugins (FFmpeg), and games' sound follows
  the volume keys (step 7). Known issues listed there (K1-K10). Next: 3.2b.
- **3.2b (2026-11-19): exit met.** Notes: [stage3.2b-games.md](stage3.2b-games.md). Five games from
  the maintainer's Steam library in the table: Ys: Memories of Celceta, Ys IX, ATRI, Plants vs.
  Zombies: Replanted, Stray (Goat Simulator parked); Direct3D 9, 11 and 12, 32- and 64-bit, Box64 and
  FEX, five engines. Fixed on the way: Box64 reporting x86 features (upstream patch), FEX moved to
  upstream FEX-2611.1 (an ARM64EC crash), Microsoft's XInput and Steam Input without uinput
  (`utdeck-compat`), see-through pictures (Mesa patch 0005), `mscoree` no longer turned off. The Visual C++ runtime mix-up after
  Steam's installers is repaired before every game start (`utdeck-compat`). Open: Goat Simulator's
  hang, portrait for landscape-only games, the on-screen keyboard.

## Next: 3.3, the Wine base (plan, 2026-11-19)

The question: stay on Hangover's Wine (Wine 11.16 with Hangover's ARM64 work, plus our eleven
patches and Proton's `lsteamclient`), or move to Proton 11's Wine (Wine 11.0 with Valve's game
fixes, which Valve runs on ARM64 for the Steam Frame). Both now meet the same games, so the answer
comes from them.

Steps, one at a time:

1. **What waits through `wineserver` cost on this kernel**, measured on our base first, because it
   decides whether an esync port is worth anything at all: for each table game at its test spot,
   `wineserver`'s CPU time and its requests per second (`strace -c` on `wineserver` for 20 s, read
   on the phone), against the game's frame time. Kernel 5.4 has `eventfd` (esync could work) but not
   `futex_waitv` (fsync) or `/dev/ntsync`; Proton 11 has dropped esync. If `wineserver` is a few
   percent of a frame, the esync question is closed.
2. **Proton 11's Wine built in our container**, as a second Wine variant in `build-runtime.sh`
   (`WINE_URL`/`WINE_COMMIT` taken from the environment, as for FEX): ValveSoftware/wine at
   proton-11.0-2e, `--enable-archs=arm64ec,aarch64,i386`. Proton's Wine is older than the 11.15 fix
   for this phone's 39-bit address space; stage 0 found that a `-static-pie` build starts anyway.
3. **Our patches carried over:** 0001-0008, 0010 and 0011 (0009 is Proton's own code); each
   conflict and what it needed written down. `lsteamclient` and `steam.exe` come with Proton's tree.
4. **The same tests on both:** the stage 2 tests (`rt-console.sh`, `rt-gfx.sh`), start-up time,
   prefix size; the five table games at their saved spots (frame rate, CPU, GPU, memory, with
   `measure.sh`); Goat Simulator's hang (a test case Proton's game fixes may cover); Ys's opening
   movie through DirectShow (`vidtest.exe`, K10).
5. **The decision, with the numbers,** in this file and in the 3.3 notes; the chosen base becomes the
   default in `build-runtime.sh`, the other stays buildable for comparison.

Risks: Proton's Wine may lack ARM64EC fixes that Hangover's fork carries (then: measured, written
down, and the fixes listed); our Wayland patches (0001-0004, 0010, 0011) may meet Proton's own
`winewayland` changes. The maintainer is needed for the game runs in step 4 (load the saved spot,
stand still, say "ready"), as in 3.2b.

Rules for this stage, from the project rules:
- Nothing system-wide on the phone: the runtime lives in a folder, chosen with environment
  variables, next to Hangover until it replaces it.
- Sources are pinned by commit and written into the runtime's `BUILD-INFO`; our patches stay
  separate files.
- Thermal and power limits stay the vendor's; long runs are watched (3.5).
- No DRM bypass: every game's folder is checked before its first start (`gamecheck.py`); Steam games
  run through hidden Steam and UTDeck's compatibility tool (3.S).
