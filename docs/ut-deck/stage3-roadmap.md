# Stage 3: a runtime we build and own, the plan

Written 2026-11-18, after [stage 2](stage2-roadmap.md). Goal (from
[feasibility-and-roadmap.md](feasibility-and-roadmap.md)): a reproducible runtime, tuned for Halium
phones. Exit of the whole stage: the runtime builds from a script, and the compatibility table has
ten or more games.

**Wrapped up for now on 2026-11-21**: see [Stage 3 wrapped up](#stage-3-wrapped-up-2026-11-21)
below for the exit check, the game table and every item still open.

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
| **3.3 Two Wine bases** (done) | Proton 11's Wine built in our container next to Hangover's 11.16, our patches carried over, both measured on the stage 2 tests and the table's games; the cost of `wineserver` waits on this kernel measured (no esync port). Both bases are kept, as Steam keeps several Proton versions: one is the default, the other a second compatibility tool chosen per game | Two compatibility tools in Steam, each with its own prefixes; the default fully supported, the second best-effort and downloaded on request; a per-game list of which base a game needs, with numbers |
| **3.4 Defaults that remove losses** (items 0-7 done, 8-11 open) | Each default measured on the phone and kept in one place (the list below) | Each default kept or dropped with a measurement; one place where the defaults live |
| **3.5 CPU boost, heat and memory** (done) | `uclamp_min` for the game's threads as in the Dolphin port; long runs with CPU and GPU load together; memory under hidden Steam (1.3 GB) plus a game (up to 2.5 GB) on an 8 GB phone; the vendor's thermal service stays in charge, no limit is raised | Frame rate, temperature and free memory over 30 minutes with and without the boost |
| **3.6 Lifecycle** (done) | What Wine and the game do when the screen locks or another app comes to the front: games started by hidden Steam are not Lomiri apps, so Lomiri may not pause them (SIGSTOP) as it pauses apps; sound, timers, the gamepad, the GPU | Pause and resume work in two games, or the problems are written down with a plan |
| **3.7 Linux x86 games** (closed with a rule) | Games with a native Linux build (Goat Simulator's) through FEX in Steam's Linux runtime, against the same games' Windows builds through our runtime | Two games measured both ways; a rule for which build UTDeck installs |
| **3.8 Starting games from the phone** (done; asked by the maintainer, 2026-11-20; before stage 4) | Today a game starts only from the PC (`steam-service.sh launch APPID` over adb), because hidden Steam has no window. The maintainer starts, plays and quits an installed Steam game on the phone alone, without the Steam window: at least one entry per game in Lomiri's app list, or one small launcher, that sends `-applaunch` to hidden Steam (and starts it when it is not running). Stage 4's library builds on it | Two games started and quit from the phone with no PC attached |
| **3.9 Games drawing with Vulkan** (closed for now: back with a stronger device; asked by the maintainer, 2026-11-21; after 3.7) | Windows games that draw with Vulkan themselves, so no DXVK: Unity games switched with `-force-vulkan` (PvZ Replanted), UE4 games that ship a Vulkan renderer (`-vulkan`), games with Vulkan as their own option | Two games measured with Vulkan against their Direct3D path; a default per engine if Vulkan is faster |

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
from a prebuilt one. 3.2 (then 3.S and 3.2b) next, so that the base comparison (3.3) and every default
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

Kept for later (the maintainer's list, 2026-11-21): **Linux ARM64 games**, native code with no
emulator, once a game is found (Valve's ARM64 Steam Frame should bring builds; 3.7's Wayland finding
and Steam's ARM64 runtime apply); **Windows on ARM games**, very rare on Steam (our runtime already
runs ARM64 Windows programs).

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
- **3.9 (2026-11-21): closed for now**, until a more powerful device runs Ubuntu Touch. Notes:
  [stage3.9-vulkan-games.md](stage3.9-vulkan-games.md). Neither game reached a Vulkan run: PvZ
  Replanted's build has no Vulkan renderer; Aperture Desk Job's (Source 2) requires subgroup
  operations in drawing shaders, which the Adreno 619 and 650 lack in hardware (Adreno 6xx has them
  from its fourth generation, 7xx in full). **Direct3D 12 through VKD3D-Proton was a third faster
  than Direct3D 11 through DXVK in PvZ** (16.9 against 12.7 fps): a candidate Unity default, to be
  checked on a second Unity game. Desk Job's Direct3D 11 locks up at its menu (open). Fixed on the
  way: install scripts are never paused; `GAME_ARGS` for launch options.
- **3.7 (2026-11-21): closed with a rule.** Notes: [stage3.7-linux-games.md](stage3.7-linux-games.md).
  Steam's path for an x86 Linux game is its FEX tool over Steam Linux Runtime's container, wanting
  an x86 system with Mesa; FEX's Ubuntu 24.04 root filesystem was put in UTDeck's folder. Goat
  Simulator's Linux build draws with OpenGL through SDL2, which has no GPU route here (GLX means
  Xwayland, software only; FEX's EGL bridge is not in its list); and FEX did not start x86 programs
  with the folder root filesystem (file opens not redirected). **Rule: a game with both builds runs
  its Windows build through UTDeck.** Later: a Vulkan Linux build, once FEX's redirection works.
- **3.6 (2026-11-20): done.** Notes: [stage3.6-lifecycle.md](stage3.6-lifecycle.md). Games
  started by hidden Steam are not Lomiri apps, so Lomiri never froze them: behind another app and
  behind the lock screen they ran on (Ys IX at 3 cores and the whole GPU, music on); coming back
  always worked (picture, sound, controller). `utdeck-pause.py` (default `UTDECK_PAUSE=1`) freezes
  the game's Windows processes while locked and wakes them on unlock: checked in Ys IX and Celceta
  (nothing used meanwhile, the sound pauses by itself, everything comes back). Behind another app
  too: Wine patch 0015 (both bases) writes the window's focus to a file; the helper freezes and
  mutes the game and wakes it for 0.3 s every 1.5 s so that it can see it is back (Lomiri could not
  be used: it does not tie Wine's window to the app that started it). Checked in Celceta: a tenth
  of its CPU behind another app, nothing while locked. Proton's runtime with 0015 released as
  `runtime-proton11.0-20261121`. Open: a long freeze and Steam's view of it.
- **3.5 (2026-11-20): done.** Notes: [stage3.5-boost-heat-memory.md](stage3.5-boost-heat-memory.md).
  `utdeck-boost.py` sets `uclamp_min` on the game's threads (threads they start inherit it); now
  the default (`UTDECK_BOOST=1024`). ATRI, 30 minutes each way, plugged in: 61.7 fps with the boost
  against 57.7 without, slow moments unchanged, about 2 °C warmer, skin 42 °C at most;
  `mi_thermald` (Xiaomi's thermal service) took one step off the little cores both times. At least
  2.8 GB stayed available with hidden Steam and ATRI. **Goat Simulator: 14.1 fps with the boost
  against 5.9 without** (same evening, same spot): without it, the scheduler kept UE3's render
  thread on the little cores and the GPU clocked down to the 300s. The table's CPU-bound games and
  the 3.3 base comparison were measured without the boost and are to be measured again. Not
  measured: battery drain; 30 minutes of the heavier games.
- **3.4 (2026-11-20): items 0-7 done, 8-11 open.** Notes: [stage3.4-defaults.md](stage3.4-defaults.md).
  One place for the defaults (`defaults.env`, read by `game-run.sh`; a game's `utdeck-env` wins).
  The default runtime now has 0012/0013; log levels kept (no cost); `tu_dont_reserve_descriptor_set`
  only for VKD3D-Proton, built into Turnip (Mesa patch 0006: as an environment variable it would
  also reach zink, which uses the reserved set); FEX's Turnip cache option and Steam's options kept
  (no difference); `winemenubuilder` off; Wine Mono in every prefix on both bases; smaller prefixes
  parked (Wine writes through links). Open, for a game round: Box64 or FEX for 32-bit games, shader
  stutter, DirectShow's renderer (K10), Stray on Direct3D 12 with the built-in default. Found on the
  way: Lomiri can hang during automated runs (the numbers keep coming); runs now check it first.
- **3.8 (2026-11-20): done** (out of order, before 3.4, at the maintainer's request). Notes:
  [stage3.8-launch-from-phone.md](stage3.8-launch-from-phone.md). One app drawer entry per
  installed game (`steam-library.py`, the cover as its icon), which hands off to
  `steam-service.sh play`: hidden Steam started if needed, its sign-in awaited, the game launched.
  Two games started and quit with no PC attached. Open: no notification while Steam starts
  (Lomiri does not show ours; stage 4's library), K1.
- **3.3 (2026-11-20): done.** Notes: [stage3.3-wine-base.md](stage3.3-wine-base.md). No esync
  port (a server request costs about 17-24 µs, a few percent of a frame). Where both bases put the
  same picture on the screen, Proton's is 0-10 % faster; every larger gap measured was a picture
  Proton did not show. **Both bases kept, as two compatibility tools**: "UTDeck runtime" (Hangover's
  11.16, the default, fully supported) and "UTDeck runtime (Proton 11)" (best-effort, its own
  prefixes, downloaded on request with `get-runtime.sh` from the GitHub pre-release
  `runtime-proton11.0-20261120`). Per-game list: Goat Simulator needs Proton's base; Ys IX, Stray and
  ATRI stay on ours; DirectShow movies fail on both (K10). Proton's base brought level where games
  need it: Valve's display-mode scaler made to work on Wayland (it scaled to 1x1, and swapped red and
  blue), our 0012/0013 ported, its OpenGL video path off; checked on the screen with Ys IX
  fullscreen 1600x720, phone sideways. Left out: fill mode on Proton's base. Open: Lomiri drops the
  fullscreen state of Proton's window when upright; the Ys IX crash on ours; K10.

## Stage 3 wrapped up (2026-11-21)

The maintainer wrapped stage 3 up on 2026-11-21 to move on with the roadmap; what is still open is
listed below with the stage it now belongs to.

### The exit

| Exit | Result |
| --- | --- |
| The runtime builds from a script | **Met.** `build-runtime.sh` builds both bases (`WINE_FLAVOR`), pinned and byte-identical between builds; Mesa from `tools/mesa-build/` |
| Ten or more games in the table | **Not met: seven games, six of them run.** The three missing ones come with the next game round (kinds still missing: an XNA or .NET game, a GameMaker game, an OpenGL game, a second Unity game, a DirectShow movie game) |

### The table at the end of stage 3

All on stone (Adreno 619), through hidden Steam and UTDeck's compatibility tool, every folder
checked for DRM bypasses first. Frame rates from different days and settings: read the notes before
comparing.

| Game | Engine, binary | Graphics | Base | Result | Performance |
| --- | --- | --- | --- | --- | --- |
| Ys: Memories of Celceta | Falcom, 32-bit (Box64) | Direct3D 11 | ours (11.16) | plays; pause checked (3.6) | 30 fps (its cap), 16-25 fps in busy dungeons (CPU-bound, measured without the boost) |
| Ys IX: Monstrum Nox | Falcom, 64-bit (FEX) | Direct3D 11 | ours | plays; a crash on our base seen in 3.3 (open) | 15-16 fps at its test spot, 1600x720, Sampling 50 % (GPU-bound) |
| ATRI -My Dear Moments- | KiriKiri Z, 32-bit (Box64) | Direct3D 9 | ours | plays (its full-screen option) | 61.7 fps average over 30 minutes with the boost (57.7 without) |
| Plants vs. Zombies: Replanted | Unity 6, 64-bit (FEX) | Direct3D 11; 12 with `-force-d3d12` | ours | plays; landscape-only, portrait wrong (K1, stage 4) | 2400x1080: 12.7 fps (D3D11), **16.9 fps (D3D12)**; 22.8-23 fps at 1600x720 (D3D11, no boost) |
| Stray | Unreal 4.27, 64-bit (FEX) | Direct3D 11 and 12 | ours | runs, not playable here | 8.3-8.4 fps at 1280x720, Sampling 50 % (GPU-bound) |
| Goat Simulator | Unreal 3, 32-bit (Box64) | Direct3D 9 | **Proton 11** | plays on Proton's base; fails at its first level on ours (open) | 14.1 fps with the boost, 5.9 without |
| Aperture Desk Job | Source 2, 64-bit (FEX) | Direct3D 11 (its Vulkan renderer needs newer hardware) | ours | **locks up at its start menu** after 50 s (open) | GPU 89-99 % on the menu |

### Still open, and where each item goes

**A game round on stone** (stage 3 leftovers, done when games are added to reach ten):
- 3.4 items 8-11: Box64 or FEX for 32-bit games (K9); shader stutter in new areas (Celceta,
  150-700 ms; DXVK's state cache, Steam's fossilize, K8); DirectShow's window renderer (K10, the
  `renderer=no3d`/`vulkan` test); Stray on Direct3D 12 with Mesa patch 0006 instead of its own
  `tu_dont_reserve_descriptor_set` setting.
- Crashes and hangs: the Ys IX crash on our base (3.3); Goat Simulator's level load on our base
  (3.3, 3.5); Aperture Desk Job's Direct3D 11 lock-up (3.9); a finished Visual C++ uninstaller that
  did not exit (3.9).
- Measured again with the boost: the CPU-bound games and the 3.3 base comparison (3.5).
- `-force-d3d12` as a default for Unity games, on a second Unity game (3.9).
- Battery drain, and 30 minutes of the heavier games (3.5); a long freeze (an hour in a pocket) and
  Steam's view of it (3.6).
- Proton's base: fill mode (0010 not ported); Lomiri drops its window's fullscreen state upright (3.3).
- Smaller prefixes (K5, 3.4 item 5: Wine writes through links, two Wine changes needed first);
  Steam runs the install script at every start (K4, about 3 s).
- Lomiri: it crashed once with a stream of `wl_subsurface` requests it does not implement (3.3) and
  hung once during an automated run (3.4). Watched; a program must not crash the compositor.

**Stage 4, the frontend** (each found in stage 3, each needs UTDeck to be a real app):
- Games started as a Lomiri app with a landscape desktop file: K1 (started sideways, a game comes
  up portrait), landscape-only games reshaped to portrait (PvZ), Lomiri's top bar on a maximized
  window (ATRI).
- Hidden Steam's dialogs shown to the user: publishers' agreements (EA's), Steam's notices before a
  launch (the Steam Input page, 3.9). UTDeck never accepts an agreement.
- A "starting" screen: a first start can take minutes with nothing on screen (Wine Mono, install
  scripts), and Lomiri shows none of our notifications (3.8, 3.9).
- Games with a Linux build: UTDeck's tool forced right after the game is added, before the first
  download (3.7's rule; `set-default.py --app` by hand now).
- Per-game settings written to `utdeck-env`: the base (`utdeck` or `utdeck-proton`), `GAME_ARGS`,
  boost and pause, `WINEDEBUG_GAME`.
- Steam's own interface (K2): sign-in, install and uninstall without the PC or the Steam phone app.

**Stage 5, input**: a controller connected while a game runs (K3); Steam Input without `uinput`
(the real controller is passed through for now); no on-screen keyboard in Windows programs (Lomiri
offers no text-input protocol); touch beyond "the first finger is the mouse".

**Stage 7**: Steam's overlay (K8); the other stores.

**Stage 8, more devices**: the Nothing Phone (1) (runtime check, the GPU-bound games); 3.9's Vulkan
comparison on a device with Adreno 6xx fourth generation or 7xx.

**Kept for later**: Linux ARM64 games; Windows on ARM games; Linux x86 builds that draw with Vulkan,
once FEX redirects file opens into a folder root filesystem (3.7; FEX's Ubuntu 24.04 root
filesystem and Steam Linux Runtime 2.0 stay on the phone for it).

## Done: 3.3, two Wine bases (plan 2026-11-19, changed 2026-11-20)

The first question was: stay on Hangover's Wine (Wine 11.16 with Hangover's ARM64 work, plus our
patches and Proton's `lsteamclient`), or move to Proton 11's Wine (Wine 11.0 with Valve's game fixes,
which Valve runs on ARM64 for the Steam Frame).

**Changed after step 4 (2026-11-20): we do not choose, we keep both.** Neither base is broken and
the gap is small (Proton's 0-10 % faster where both show the same picture), while each brings
something the other lacks: Hangover's 11.16 follows upstream Wine closely (Wayland driver and ARM64EC
work arrive there first), Proton's 11.0 carries Valve's per-game fixes and launcher workarounds. Steam
already lets a user pick a compatibility tool per game, as it offers several Proton versions; for
"almost every game compatible", a second engine to try is worth more than 10 %. The cost is a second
patch series and a second set of prefixes, kept low by supporting one base fully and the other on a
best-effort basis.

Steps, one at a time:

1. **Done: what waits through `wineserver` cost on this kernel.** A request costs about 17 µs
   (ARM64) to 24 µs (x86-64 through FEX); in Ys IX and ATRI the waits are a few percent of a frame.
   No esync port.
2. **Done: Proton 11's Wine built in our container** (`WINE_FLAVOR=proton` in `build-runtime.sh`,
   own sources, build folders, patches and output).
3. **Done: our patches carried over** to `patches-proton/` (seven of them; the scaler replaced by
   Valve's `fs_hack`, fill mode and the opaque bars left out).
4. **Done: the same tests on both** (2026-11-20). The stage 2 graphics tests (Proton's 0-10 %
   faster where both show the same picture) and the table's games: Ys IX, Stray and ATRI run as
   well on ours, which also fills the screen; **Goat Simulator needs Proton's base** (ours fails at
   its first level); DirectShow movies fail on both (K10 stays open). List and parked items in the
   3.3 notes.
5. **Two compatibility tools** (was: the decision):
   1. **The default stays our 11.16 base**: it is the complete one (it follows Lomiri's rotation,
      gets real fullscreen, scales lower resolutions to the screen). Fully supported, tested on every
      game.
   2. **Proton's base brought level** with the default where a game needs it: follow Lomiri's
      rotation (Lomiri does not give its window the fullscreen state), a working scaler for display
      mode changes (our 0006 ported, or `fs_hack` made to switch on), our 0012 and 0013 ported.
   3. **A second tool in Steam**, e.g. "UTDeck (Proton 11)", next to `utdeck-compat`, chosen per game in
      the game's Properties, or written by `set-default.py --app`.
   4. **Separate prefixes per tool** (`pfx` and `pfx-proton` per game): a prefix updated by one Wine
      is not used by the other. Saves inside the prefix do not move across; Steam Cloud covers most
      games, the rest are written down.
   5. **The second runtime downloaded on request**, not shipped by default (about 210 MB packed).
   6. **The per-game list** from step 4 in the 3.3 notes and the table: which base each game runs on
      and why.

Exit: both tools in Steam on the phone, a game started on each, the per-game list with numbers, both
runtimes from one build script.

Risks: two patch series to port at each Wine update (this night's patches needed rework for 11.0's
older driver); Lomiri's window logic, which treats the two Wines' windows differently and is not yet
understood; disk space on the phone (two runtimes, two prefixes for the games that use both).

What step 4 taught for every later measurement (details in the 3.3 notes and lessons-learned): a
frame rate counts only once the picture is seen on the screen (screen on, a screenshot or the
maintainer watching, the buffer sizes handed to Lomiri), and Lomiri's orientation is noted with
every run.

Rules for this stage, from the project rules:
- Nothing system-wide on the phone: the runtime lives in a folder, chosen with environment
  variables, next to Hangover until it replaces it.
- Sources are pinned by commit and written into the runtime's `BUILD-INFO`; our patches stay
  separate files.
- Thermal and power limits stay the vendor's; long runs are watched (3.5).
- No DRM bypass: every game's folder is checked before its first start (`gamecheck.py`); Steam games
  run through hidden Steam and UTDeck's compatibility tool (3.S).
