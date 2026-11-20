# Stage 3.2b: the game table, from the Steam library

Started 2026-11-19. Plan: [plan-change-steam-early.md](plan-change-steam-early.md) and
[stage3-roadmap.md](stage3-roadmap.md); it follows stage 3.2, which closed with no table entries
([stage3.2-games.md](stage3.2-games.md)), and 3.S, which made the maintainer's Steam library usable
([stage3S-steam.md](stage3S-steam.md)). Goal: real games on our runtime with frame rate, CPU, GPU,
memory and temperature in a table, so the Wine base decision (3.3) and every default (3.4, 3.5) is
measured on games. Exit: five or more games in the table, covering Direct3D 9, 11 and 12, 32- and
64-bit, and several engines.

Terms used here:
- *depot* = one block of a Steam game's files (for example the Windows build, or the Linux build);
  Steam downloads the depots that match the platform it installs the game for.
- *UE3, UE4* = Unreal Engine 3 and 4, Epic's game engines (many games of 2006-2014 and 2014-2022).

## How every game is run

1. The maintainer downloads it with the Steam phone app's remote install; hidden Steam installs it.
2. Before the first start: the folder is checked for DRM bypasses (a Steam emulator, a replaced
   `steam_api.dll`, crack DLLs, release-group markers in the executables), and the executables are
   read (32- or 64-bit, graphics API, engine).
3. Started with `steam-service.sh launch APPID`, phone upright, controller on before the start
   (known issues K1 and K3 of 3.S). `game-run.sh` (through `utdeck-compat`) writes the log, the
   frame statistics (one line a second) and the sensors (every 2 s).
4. The maintainer plays a few minutes and reports what works; the numbers come from the logs.

## Candidates (the maintainer's, 2026-11-19)

| Order | Game | Why |
| --- | --- | --- |
| 1 | Goat Simulator (2014) | UE3, Direct3D 9: DXVK's D3D9 path with a real 3D game; light. **Parked** (below): it has a Linux build, a separate test case |
| 2 | Ys IX: Monstrum Nox | 64-bit: the first game through FEX's ARM64EC path; same studio as Ys: Memories of Celceta |
| 3 | A visual novel (White Graduation or similar) | Japanese text and fonts; often DirectShow movies (3.S known issue K10) |
| 4 | Plants vs. Zombies: Replanted | A recent engine for a 2D game; engine and API read from its folder |
| 5 | Stray | UE4, Direct3D 12 or 11, heavy: VKD3D-Proton; perhaps on the stronger Nothing Phone (UT 24.04-1.x, the runtime to be checked there first) |

Ys: Memories of Celceta (3.S, M13-M16: Direct3D 11, 32-bit, 1200x540 borderless with fill mode)
is the first entry.

## Goat Simulator (app 265930)

### Steam installed the Linux build

The first download (2.4 GB) was the game's native Linux port: `Binaries/linux-amd64` and
`Binaries/linux-x86` with `GoatGame` and PhysX as `.so` libraries, no Windows file. UTDeck's
compatibility tool is Steam's default only for games with no Linux build (CompatToolMapping app
"0"); for a game that has one, Steam installs the Linux build and would run it its own way, under
FEX in Steam's Linux runtime, which does not work on Ubuntu Touch as it is (3.S, step 3). A Linux
x86 game would also run entirely under FEX, its OpenGL calls included, where our runtime emulates
only the game's code and draws natively (DXVK on Turnip).

DRM check of that build: clean (Valve's `libsteam_api.so`, a plain `steam_appid.txt`).

Fix: [`set-default.py`](../../tools/ut-probe/steam-compat/set-default.py) got `--app APPID [TOOL]`,
which writes the same entry for one game as its properties page does ("Force the use of a specific
Steam Play compatibility tool", priority 250). Done with Steam stopped; at its next start Steam
swapped the depots (added `265931` and `344450`, removed the Linux `265932` and `265935`), added
three depots of Steamworks Common Redistributables (app 228980: the Windows installers the game
declares) and queued the update by itself.

For later: every game with a Linux build needs this entry to run through UTDeck. Stage 4 (UTDeck's
library) can write it for each such game.

### The Windows build, checked before the first start

[`gamecheck.py`](../../tools/ut-probe/gamecheck.py) (new: the check before every first start as a
script; reads only, 45 s for 2.4 GB on the phone):
- **No DRM-bypass marker** in any file; `steam_api.dll` carries Valve's signature.
- `Binaries/Win32/GoatGame-Win32-Shipping.exe`: **32-bit x86**, UE3 with PhysX and APEX. No 64-bit
  build. It runs through WoW64 with Box64 (`utdeck-compat`, 3.S known issue K9).
- Graphics: imports `d3d9.dll`, so DXVK's 32-bit Direct3D 9 (it also imports `d3dx9_43`,
  `d3dx11_43` and `d3dcompiler_43`).
- Also: `xinput1_3`, `dinput8`, `dsound`, `x3daudio1_7`, `xapofx1_5` (DirectX June 2010), VC++ 2010
  (`msvcp100`, `msvcr100`), `mscoree` (.NET's loader, which `game-run.sh` turns off by default),
  and `easyhook32.dll` (a hooking library the game ships).

First start: Steam's install script ran in the new prefix, with Microsoft's DirectX June 2010 setup
(`DXSETUP.exe /silent`, from Steamworks Common Redistributables).

### First starts (manual run M17, 2026-11-19)

1. **The game would not load: `mscoree.dll` turned off.** `game-run.sh` turned off .NET's loader
   for every game (`WINEDLLOVERRIDES=mscoree,mshtml=`); Goat Simulator imports it, so Windows' loader
   stopped before the game ran (`c0000135`, which Steam reports as exit 53). Fixed in `game-run.sh`:
   only `mshtml` stays off.
2. **Then: "Wine Mono is not installed"**, exit 255: the game does start the .NET runtime. Wine
   Mono (the runtime ships `wine-mono-11.3.0-arm64.msi`) was installed into this prefix by hand:
   33 s, 1195 MB to 1527 MB. Whether every game prefix gets Mono at creation (as Proton does) is a
   defaults question for 3.4 (time and size against games that fail without it).
   (A first try installed it into the tools folder: `env.sh` sets its own `P`, which replaced the
   prefix path held in a variable of the same name; the six files Wine created there were removed.)
3. **The game runs, then hangs.** First frame after about 2.5 minutes (splash window first; Box64,
   first-time shader work). Intro logos at 45-50 fps, 1280x720, GPU 87 % at 700 MHz, CPU 52 %.
   Then a few frames, and nothing more (the maintainer: "loads about 3 frames, then stuck"). In
   the hung process: Wine threads waiting over 60 s for the process heap lock, owned by "thread
   0000" (no living owner); Valve's `steamclient.so` thread `IPC:CSteamEngine` at 100 % of one core
   (spinning in user space); an FAudio thread at 54 %. Game memory 2.5 GB (PSS), 0.9 GB left on the
   phone with hidden Steam. The game was ended with `wineserver -k`; Steam saw it end.
4. **Display, separately from the hang.** The game's 1280x720 window did not get fill mode:
   upright it was a landscape-shaped picture in the top 45 % of the screen, partly off the right
   edge; sideways it was "mostly right" but with Lomiri's top bar. Parts of the picture were
   see-through (the PhysX logo and text over the wallpaper; only the movie area was black): UE3
   writes alpha into its picture, which Windows ignores and the compositor apparently uses.

### Plan

One at a time, the hang first (it stops every other test):
1. **Emulator memory ordering.** Box64 by default does not keep x86's strict order of memory
   writes between threads (faster); multithreaded code with its own locks can then corrupt a lock
   exactly as seen. Run with `BOX64_DYNAREC_STRONGMEM=1`, then with FEX for the game
   (`HODLL=libwow64fex.dll`, which keeps the order by default), per game through `utdeck-env`.
2. **If both hang: the Steam API bridge.** This game uses a 2014 Steamworks SDK (Ys a newer one);
   trace `lsteamclient` (`WINEDEBUG=+steamclient`) to see the last Steam calls before the hang.
3. **Display:** why fill mode does not take this window (Wayland driver trace), Lomiri's top bar,
   and the see-through picture (the swapchain's alpha).
4. **Memory:** 2.5 GB for the game; measure again once it runs.

### The hang, step 1: the emulator (2026-11-19)

Each run: `steam-service.sh launch 265930`, the per-game setting in `compatdata/265930/utdeck-env`,
watched from the PC every 30 s (frames drawn, lock waits in the log, game memory).

| Run | Emulator for the game | Result |
| --- | --- | --- |
| M17 | Box64 (default) | Hung after the intro: heap lock "owned by 0000"; Steam's IPC thread at 100 % |
| A | Box64, `BOX64_DYNAREC_STRONGMEM=1` (x86's strict write order kept) | **Hung at the same point**: first frame at about 2 min, 2928 frames of intro (up to 4 min), then nothing. No heap-lock error this time, Steam's IPC thread quiet. The game's main thread waits on the Wine server (a Windows sync object); UE3's RenderingThread waits on a lock; DXVK idle; an FAudio (XAudio2) thread loops on `read()` at about 60 % of a core |
| B | FEX (`HODLL=libwow64fex.dll`) | **Crashed before the first frame**: `vkCreateSwapchainKHR: Exception 0xc0000005 in Unix call` (an access fault in Wine's own code while creating the swapchain, where patches 0005, 0006 and 0010 work), exit 3. A heap-lock wait "owned by 0000" was logged early and the game went on after it, so that message alone is not the hang. Needs its own look |
| C | Box64, Microsoft's XAudio2 2.7 (`GAME_OVERRIDES=xaudio2_7=n`, installed by the game's DirectX setup) | **Refused to start**: a dialog "Microsoft XAudio2: CPU does not meet minimum requirement; streaming SIMD extensions support required" (seen by the maintainer). This led to the Box64 bug below |
| D | Box64 with the fix below, Wine's XAudio2 (the default setup again) | **Hung at the same point.** The last frame was **1600x720** after two minutes at 1280x720: the game changed its picture size as it left the intro |
| E | As D, the game set to windowed (`Fullscreen=False` in its settings) | **Crashed before the first frame**, the same crash as B: `vkCreateSwapchainKHR: Exception 0xc0000005 in Unix call`. So that crash is not FEX's: it comes with a window smaller than the screen, where fill mode works |
| E2 | As E, with `+vulkan,+waylanddrv` tracing | **No crash** (fill mode engaged: `scaling 1280x720 into 2400x1080 (the monitor) at (240,0)-(2160,1080)`, the Wayland driver picked it up), but no frames either; the trace grew to 4.4 GB and was stopped and deleted. A crash that goes away when tracing slows things down suggests a race between the game's render thread (creating the swapchain) and the window thread (setting up the same Wayland surface for fill) |
| E3 | As E, with `+seh` tracing (fault address and registers) | Froze after a few frames (the maintainer); no crash to capture |

So the spinning Steam thread in M17 was a side effect, not the cause; strict ordering in Box64 does
not change it. Why C: the FAudio thread looping in the hung process, and the same pattern in other
games: Fallout 4 played its intro, then hung when the menu music started through Wine's XAudio2,
and Microsoft's XAudio2 avoided it (Wine bug 41271); Unreal games are known to be sensitive to
Wine's XAudio2 (Wine bug 54285).

What UE3's own log (`Documents/My Games/GoatSim/GoatGame/Logs/Launch.log`, written into the
user's real Documents folder through the prefix's link; it also holds the Steam account name and is
only read on the phone) shows for run D: 90 s from start to the menu level, then
`LoadMap: level_01` at 101 s behind a loading movie, still loading PhysX objects at 156 s. In the
hung process the main thread waits on the Wine server with almost no CPU, the render thread is
parked, Wine's XAudio2 thread (FAudio) runs at about 60 % of a core. Wine's debugger could not take
backtraces of this 32-bit process under Box64 (`bt all` hung; its time-out ended the game). The game
reads `Fullscreen=True`, `ResX=1280`, `ResY=720` from its settings
(`Config/Space/GoatSystemSettings.ini`): the size change at the start of the level load looks like
UE3 going into its fullscreen mode, a Direct3D 9 device reset with a display-mode change, a known
place for deadlocks. Hence E.

### Parked (2026-11-19)

The maintainer parked Goat Simulator: it has a native Linux build, which is a separate test case
(Linux x86 games through FEX), to be tried by the end of stage 3. Its Windows build was a good
first test anyway. What it left:
- Fixed: `game-run.sh` no longer turns off `mscoree` (games that import it could not load).
- Fixed: Box64 reports x86 features to Windows programs (below).
- New: `gamecheck.py`, `set-default.py --app`.
- **Open, ours, to check with the next games:** a crash creating the swapchain of a window smaller
  than the screen (E: fill mode), probably a race; the windowed picture not filling the screen in
  M17 (upright: a landscape band; sideways: the top bar); see-through parts of the picture (UE3's
  alpha).
- **Open, the game's or the runtime's, not followed further:** the freeze at the start of the
  first level load (fullscreen), with the game's main thread waiting on the Wine server and Wine's
  XAudio2 thread looping; not the emulator's memory ordering (A, B), not the CPU-feature answer (D).
  Wine's debugger cannot take backtraces of 32-bit Box64 processes (E2's attempt hung).
- Wine Mono: installed by hand in this prefix (33 s, +332 MB); the default is a 3.4 question.
- The settings file the game wrote was restored to its own values (`Fullscreen=True`).

## Ys IX: Monstrum Nox (app 1351630)

Windows only (no Linux build), 9.7 GB. `gamecheck.py` (3.5 minutes for 9.7 GB):
- **No DRM-bypass marker**; `steam_api64.dll` signed by Valve.
- `ys9.exe` (and `ys9_JP.exe`, the Japanese one): **64-bit x86-64**, the first 64-bit game: FEX in
  its ARM64EC mode (`libarm64ecfex.dll`).
- Graphics: Direct3D 11 (`d3d11`, `dxgi`, `d3dcompiler_47`: it may compile shaders while running),
  NVIDIA's HBAO+ ambient occlusion (`GFSDK_SSAO_D3D11.win64.dll`, which runs on other vendors too).
- Also: `xinput9_1_0`, `gdiplus`, Ogg Vorbis; `Galaxy64.dll` (the GOG Galaxy SDK, shipped in both
  stores' builds); `solstice.dll` (the publisher's store and overlay layer:
  `solsticeCompletePurchase`, `solsticeCloseCursorMode`), not audio.
- Movies: WebM (`movie/*.webm`). The game decodes them itself: `ys9.exe` contains libvpx 1.9.0 (VP9)
  and an Opus decoder, so Wine's media path (3.S step 7) is not used, and VP9 is decoded in emulated
  x86 code.
- No launcher; settings in the game.

### Manual run M18 (2026-11-19): it runs, GPU-bound

First start: Steam's install script, then the game. **Works on the first try:** stable, menus
responsive, no visual errors, **fullscreen in portrait and landscape** (the maintainer). The game's
smallest resolution is 1280x720 (it lists 1600x720, 2000x900 and 2400x1080 from patch 0008's
screen-shaped modes, but nothing below 720 lines: the game's own limit); its internal "Sampling" at
50 % renders at a quarter of the pixels. The maintainer set the graphics low (ambient shadowing,
dynamic shadows off, low shadow resolution).

| Setting | Frame rate | GPU | Other |
| --- | --- | --- | --- |
| First menus | 57-59 fps | | |
| 1280x720, Sampling 50 %, playing | high teens (16-18 fps) | 90-92 % at 700 MHz | CPU 64-67 % of the phone |
| 1600x720, Sampling 50 % (800x360 rendered), standing at a saved spot | 14.5-15.0 fps (67 ms) | 97 % at 700 MHz | game's main thread about 83 % of a core; draw thread, DXVK worker and job threads 11-15 % each |
| Same spot, Sampling 100 % (1600x720 rendered) | 8.3-8.5 fps (119 ms) | 97-98 % | |

700 MHz is the GPU's highest available clock (`gpu_available_frequencies`: 700, 650, 490, 390,
266 MHz), so it was fully used. Four times the pixels cost only 1.76 times the frame time: about
**50 ms of every frame does not depend on the render resolution** (geometry, shadows, effects at the
output size, per-draw work), about 17 ms is pixels at 50 %. Lower resolutions would gain little.
Memory: the game 2.3-2.5 GB, 0.7-0.9 GB left on the phone with hidden Steam. Temperatures: CPU at
most 56 °C, GPU 55 °C.

Turnip's rendering mode, same spot, Sampling 50 %, 1600x720 (per game: `TU_DEBUG` in `utdeck-env`):

| `TU_DEBUG` | Frame rate |
| --- | --- |
| (none, the default) | 14.9-15.0 fps |
| `sysmem` (straight into main memory) | 15.9 fps |
| `gmem` (always tiled) | 15.8-16.2 fps |

So the tiling step (sorting geometry into tiles, which can cost a lot with much geometry) is not
the bottleneck; the difference is within a few percent. For comparison, the Switch runs the game
at about 30 fps at a dynamic 540-720p with settings made for it. Whether our stack adds overhead on
top (DXVK, Turnip) is best answered on a phone with a stronger GPU (the maintainer's Nothing Phone).

### A crash about a minute after the start: GOG Galaxy's network thread (fixed: a newer FEX)

The first session (20 minutes, M18) did not crash. The gmem run and the following default run
both crashed about 63 s after the start, with the game's own "Ys 9 Crash" dialog and a minidump
(`AppData/Local/Temp/Ys9/*.dmp` in the prefix; read on the phone with
[`dmpinfo.py`](../../tools/ut-probe/dmpinfo.py), since a dump can hold account data):
- exception `c0000005` with parameter 8, an **execute fault**: the x86-64 thread tried to run code
  at `ntdll.so+0x39060` and `+0x39068`, inside **`__wine_syscall_dispatcher`**, Wine's native ARM64
  entry into its Unix side, which emulated x86-64 code never calls itself;
- the thread is GOG Galaxy's (`Galaxy64.dll`, the GOG SDK that the Steam build also loads): its
  network loop waits in `GetQueuedCompletionStatus` (decoded from the call site with
  [`callsite.py`](../../tools/ut-probe/callsite.py)), through kernelbase's ARM64EC entry thunks.

Reading: a thread waiting inside a system call was resumed with a wrong context (an x86-64 RIP at
the dispatcher), a problem of the boundary between emulated x86-64 code and native code in
ARM64EC (FEX or Wine's ARM64EC side). Not graphics, and not the Turnip mode. Searched: no report
for this signature; related ones are a Wine fix for ARM64 `__wine_syscall_dispatcher` scratch
registers after `NtContinue`, and FEX-2605's fix for a suspend crash with ARM64EC.

A third run in the default setup, with no measuring at all (the maintainer suspected the
measurements), crashed the same way after 4 minutes. So: crashes after about 1, 1 and 4 minutes,
none in the first 20-minute session.

**Fix: FEX-2611.1.** Our FEX was Hangover 11.16's (AndreRH/FEX `fa556167`, 2026-08-09). Upstream FEX
changed its ARM64EC system calls three times since: #5911 (a syscall regression), #5997 and
`aad2c969` ("Don't hardcode NT syscall numbers for ARM64EC": the old FEX used Windows' own syscall
numbers for some calls it makes itself, which do not match Wine's; the new one reads them from
`ntdll`'s `__arm64ec_syscall_ffs` table). `build-runtime.sh` now takes `FEX_URL` and `FEX_COMMIT` from
the environment for such A/B builds. With upstream FEX-2611.1 (`4f9c1f1e`, built in 43 s): **15.5
minutes of play, no crash, no new dump**; 15.7 fps at the saved spot (14.9-15.0 before), 22.3 fps on
average over the session. Its 32-bit side (`libwow64fex.dll`) passes the CPU-feature test. FEX-2611.1
is now the pinned FEX.

### Box64 told 32-bit programs "no SSE"

A 32-bit test program (`IsProcessorFeaturePresent` and the `cpuid` instruction), in this prefix:

| Emulator | `IsProcessorFeaturePresent` MMX / SSE / SSE2 / SSE3 | `cpuid` SSE / SSE2 |
| --- | --- | --- |
| Box64 (our pin) | **0 / 0 / 0 / 0** | 1 / 1 |
| FEX | 1 / 1 / 1 / 1 | 1 / 1 |
| Box64 with the fix | 1 / 1 / 1 / 1 | 1 / 1 |

In a 32-bit process, Wine's `RtlIsProcessorFeaturePresent` asks the emulator
(`NtWow64IsProcessorFeaturePresent`, `BTCpuIsProcessorFeaturePresent`); without that export every
answer is "no". FEX exports it; Box64 at our pin (Hangover 11.16's, v0.4.4) does not. Upstream
Box64 added it on 2026-08-24 (ptitSeb/box64 `ec1d5a6`, #4286: `BTCpuIsProcessorFeaturePresent`
and `BTCpuUpdateProcessorInformation`, answering from Box64's own settings: SSE up to SSE4.1
always, SSE4.2 and AVX only when Box64 emulates them). It applies cleanly to our pin and is carried
as [`patches-box64/0001`](../../tools/wine-build/patches-box64/) (`build-runtime.sh` applies it
after the checkout); Box64 rebuilt in 25 s, runtime reinstalled.

Every 32-bit game under Box64 was affected: any code that picks its SSE path from Windows' answer
(engines, audio and video libraries, Microsoft's XAudio2, which refuses to start) took the slow or
the failing path. Ys: Memories of Celceta ran with it; its numbers may change.

## Events

- 2026-11-19: 3.2b started; candidates listed. Goat Simulator came down as its Linux build;
  `set-default.py --app` forces the Windows build; Steam switched the depots.
- 2026-11-19, later: the Windows build installed and checked (`gamecheck.py`: clean, 32-bit,
  Direct3D 9). M17: `mscoree` turned off kept it from loading (fixed in `game-run.sh`); then it
  needed Wine Mono (installed); then it ran and hung after a few frames (heap lock, Steam IPC
  thread spinning). Display: no fill mode, top bar, see-through parts. Plan written.
- 2026-11-19, later: the hang: strict memory ordering in Box64 (A) and FEX (B, a crash at
  swapchain creation instead) did not help; Microsoft's XAudio2 (C) refused to start ("no SSE").
  Found: Box64's WoW64 module did not report x86 features to Wine; upstream's fix carried as a
  Box64 patch; test D (default setup with the fix) started.
- 2026-11-19, later: windowed mode crashed in swapchain creation (E), probably a race with fill
  mode; tracing hid it. The maintainer parked Goat Simulator (its Linux build is a separate test
  case for later in stage 3). Next candidate chosen.
- 2026-11-19, later: Ys IX (app 1351630) checked (clean, 64-bit, Direct3D 11) and run (M18): works
  first try, fullscreen both ways, GPU-bound (97 % at its top clock; about 50 ms per frame do not
  depend on the resolution; Turnip's rendering mode makes no difference). Two runs crashed about a
  minute after the start in GOG Galaxy's network thread (an x86-64 thread running Wine's syscall
  dispatcher): open. New tools: `measure.sh`, `watch-game.sh`, `dmpinfo.py`, `callsite.py`.
- 2026-11-19, later: the Galaxy crash came back without measuring (4 minutes). FEX-2611.1 (upstream)
  fixes it: 15.5 minutes, no crash. FEX pin moved from Hangover's to upstream FEX-2611.1.
