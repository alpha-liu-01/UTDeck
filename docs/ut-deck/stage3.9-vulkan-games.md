# Stage 3.9: games drawing with Vulkan

Asked by the maintainer on 2026-11-21 (after 3.7): Windows games that draw with Vulkan themselves
skip DXVK and VKD3D-Proton; our runtime already hands their Vulkan calls to Turnip (stage 2's
`vktest`), but no real game has been tried that way. Exit (roadmap): two games measured with Vulkan
against their Direct3D path; a default per engine if Vulkan is faster.

## The first game: PvZ Replanted

**Unity 6** (6000.0.52, its `Player.log`), IL2CPP, 64-bit through FEX (ARM64EC); by default
**Direct3D 11** through DXVK ("Direct3D 11.0 [level 11.1], Renderer: Turnip Adreno (TM) 619"). It
ships Microsoft's Direct3D 12 runtime (`D3D12/D3D12Core.dll`), so its build includes Unity's
Direct3D 12 renderer too; whether Vulkan is included cannot be read reliably from its data files
(no "vulkan" string in `globalgamemanagers`), and Unity reports it at start-up (or falls back).
Measured before (stage 3.2b, no boost): 22.8-23 fps at 1600x720 Full Screen, GPU-bound.

So three renderers of one game: **Direct3D 11** (DXVK), **Direct3D 12** (`-force-d3d12`,
VKD3D-Proton; also a check of Mesa patch 0006, the built-in Turnip option for VKD3D-Proton),
**Vulkan** (`-force-vulkan`, straight to Turnip).

## Made

- `GAME_ARGS` in a game's `utdeck-env` (`utdeck-compat`): more arguments for the game itself, as
  Steam's launch options would add, without touching Steam's account files; not for install
  scripts; logged in `utdeck-compat.log`.

## Vulkan in PvZ: not in its build (2026-11-21, 00:35-00:39)

`GAME_ARGS=-force-vulkan`. The maintainer: "PvZ failed to launch". It had not failed yet: it was
slow. Its prefix predated the Mono default (3.4, item 6), so `utdeck-compat` first installed Wine
Mono (94 s), then Steam's install script ran the DirectX setup; the game itself started about 2.5
minutes after the tap, with nothing on screen meanwhile (no notification shows, 3.8). Then Unity's
`Player.log`:

    Forcing GfxDevice: Vulkan
    Forced GfxDevice 'Vulkan' was not built from editor, shaders will not be available
    InitializeEngineGraphics failed

**PvZ Replanted was built without Unity's Vulkan renderer**: `-force-vulkan` cannot add it, and the
game stops at graphics start-up (ended with `wineserver -k`). Nothing wrong on our side. Unity games
have Vulkan only where their makers included it. Next with PvZ: `-force-d3d12` (included), which
measures VKD3D-Proton against DXVK in the same game and checks Mesa patch 0006 for real.

A long first start with nothing on screen looks like a failure: a "starting" screen in stage 4's
library matters more than it seemed.

## Direct3D 12 in PvZ (2026-11-21, 00:42)

`GAME_ARGS=-force-d3d12`; the start was quick this time. Unity: "Forcing GfxDevice: Direct3D 12",
"Direct3D 12 [level 11.1], Renderer: Turnip Adreno (TM) 619": **VKD3D-Proton works in a real
game**, with Mesa patch 0006's built-in Turnip option (no `tu_dont_reserve_descriptor_set` set for
PvZ). A busy spot in a level, boost on, `measure.sh 30` and `threads.sh 10`:

| renderer | size | fps | frame time (worst) | GPU | memory |
| --- | --- | --- | --- | --- | --- |
| Direct3D 12 (VKD3D-Proton), boost on | 2400x1080 | 16.9 | 59.7 ms (560) | 95-97 % at 685-700 MHz | game 1659 MB, 730 MB left on the phone |

GPU-bound (97 %); the main thread at 75 % of a core and Unity's graphics thread at 26 %, on the
big cores. The game drew at the screen's 2400x1080 (3.2b measured 1600x720 Full Screen: 22.8-23 fps,
and 11-14 fps at 2400x1080, both Direct3D 11, no boost, another day): Direct3D 11 measured next,
the same session and spot:

| renderer | size | fps | frame time (worst) | GPU | main thread | memory left |
| --- | --- | --- | --- | --- | --- | --- |
| Direct3D 11 (DXVK, Unity's default), 00:46 | 2400x1080 | 12.7 | 81.1 ms (355) | 96 % at 656-700 MHz | 38 % of a core | 938 MB (game 1520 MB) |
| Direct3D 12 (VKD3D-Proton), 00:42 | 2400x1080 | **16.9** | 59.7 ms (560) | 95-97 % at 685-700 MHz | 75 % | 730 MB (game 1659 MB) |

**Direct3D 12 is a third faster** (16.9 against 12.7 fps), the GPU equally full: Unity's
Direct3D 12 renderer through VKD3D-Proton costs the GPU less per frame here than its Direct3D 11
renderer through DXVK. Costs: about 140 MB more for the game (200 MB less left on the phone) and a
worse single worst frame (560 against 355 ms; one each in 30 s, too few to compare). A candidate
default for Unity games whose build includes Direct3D 12 (`-force-d3d12`): one game so far, to be
checked on a second Unity game before it becomes one.

## The second game: Aperture Desk Job

PvZ has no Vulkan renderer, so the Vulkan test needs a game that ships one. **Aperture Desk Job**
(app 1902490): free, a short Valve game made to show the Steam Deck, played with a controller,
**Source 2**. Source 2 has its own Vulkan renderer (the default on Linux; on Windows, launch option
`-vulkan`), so one game gives Direct3D 11 through DXVK against Vulkan straight to Turnip. Its
minimum GPU is a GTX 1060, far above the Adreno 619: low frame rates are expected, which does not
spoil a comparison within the same game. Sources: [Steam](https://store.steampowered.com/app/1902490),
[Valve wiki: Vulkan](https://developer.valvesoftware.com/wiki/Vulkan),
[Valve wiki: Aperture Desk Job](https://developer.valvesoftware.com/wiki/Aperture_Desk_Job).

**It has a Linux build too, and Steam started with that one** (2026-11-21, 11:41): `compat_log.txt`
"Mapping AppID 1902490 to tool native", depots 1902491 (shared data) and 1902493 (Linux program
files). As 3.7's rule says, the Windows build was forced with Steam stopped
(`set-default.py --app 1902490 utdeck`, our base). At the next start Steam kept the forced tool
("Skip mapping ... to tool native ...: mapping to tool utdeck with priority 250 already exists"),
swapped depot 1902493 for **1902492 (Windows program files)**, added Steam's Windows
redistributables (app 228980) as a dependency, and carried on with the shared data: the 1 GB
already downloaded was kept. 3.35 GB in all; 6.4 GB free on the phone.

Plan: `gamecheck.py` on its folder before the first start; then the same spot twice, Direct3D 11
(its Windows default) and `GAME_ARGS=-vulkan`, `measure.sh` and `threads.sh` each.

**Installed and checked** (2026-11-21): 4.65 GB on disk, all x86-64 (`game/bin/win64/deskjob.exe`,
which imports only `kernel32` and `user32` and loads the engine's DLLs itself). Both renderers are
in the build: `rendersystemdx11.dll` and `rendersystemvulkan.dll` (6.2 MB). `gamecheck.py`
(87 s): `steam_api64.dll` signed by Valve, **no DRM-bypass markers**. Its app drawer entry was made
with `steam-library.py` (the game finished installing after Steam's start, when entries are made).

### The first start (2026-11-21, 11:49-12:56): three things in the way

The maintainer: "The game failed to launch." Three separate problems:

1. **The pause helper froze Steam's install script** (11:49, the first launch). Steam first runs the
   game's install script (`iscriptevaluator.exe`: the Steamworks redistributables) in the new
   prefix. The phone was locked at 11:51, and the pause helper (3.6) froze it during the prefix's
   start-up: "boot event wait timed out", "Failed to open service manager", an unhandled exception,
   and exit 29 after 5 minutes, leaving a half-installed Visual C++ runtime. **Fixed**:
   `utdeck-compat` sets `UTDECK_PAUSE=0` for install scripts (a setup step is never paused).
2. **A Steam dialog on the hidden screen** (12:47, the second launch). The install script ran
   through (exit 0), then Steam stopped at "ShowInterstitials": a two-page "Steam Input Games"
   notice (the game uses Steam Input's action sets), drawn on hidden Steam's Xvfb screen where
   nobody sees it. Seen with `xwin.py :50 --shot` (picture in `build/`) and passed with
   `xwin.py --click` on "Next" and "OK": a notice, no agreement. Steam shows it once per game type;
   **for stage 4**: hidden Steam needs a way to show or answer such dialogs, or a launch just waits.
3. **The Visual C++ 2022 installer hung on a finished child** (12:51). Our tool's Visual C++ step
   (stage 3.4) found the prefix's runtime mixed (from problem 1) and ran Microsoft's 2022 installer,
   which first uninstalls the 2015-2019 copy. That uninstaller's log ended with "Exit code: 0x0"
   at 12:51:24, but its process stayed (one thread, asleep in a futex, no CPU), and the installer
   waited on it. Ended by hand (`kill`) at 12:55: the installer carried on, x64 and x86 installed,
   "Microsoft's complete set". Open: whether this happens on a clean prefix too (it came from the
   leftovers of problem 1); `timeout 600` would have ended it after 10 minutes.

The game then started at 12:56:19: `deskjob.exe`, "D3D11 ... DXVK", Turnip, swapchain 2400x1080.

### Direct3D 11: locked up at the start menu (12:56-13:04)

The maintainer: "The game locked up at the start menu. Neither controller nor touch works."
- The sensor log: loading until 12:58:08, then the menu drawn for about 50 s with the GPU at
  89-99 % (GPU-bound already on a menu), then **from 12:59:00 the GPU idle** for good. Game memory
  1.66-1.7 GB, 635 MB left on the phone, swap 855 MB of 2 GB: tight, not exhausted.
- **A deadlock, not a slow frame**: none of the game's 58 threads used CPU; the main thread and 28
  others waited in Wine's server (`pipe_read`), DXVK's threads (`dxvk-cs`, `-submit`, `-queue`,
  `-frame`, shader compilers) idle on futexes. No process stopped (not the pause helper; its log
  had no entry), the focus file said "in front". **No GPU fault**: the kernel log covered 12:54
  onwards with no `kgsl` error.
- The log's last lines: "Presenter: Got VK_SUBOPTIMAL_KHR, recreating swapchain" (DXVK rebuilding
  its picture buffers after a size change) and "MakeWindowAssociation"; earlier, while starting,
  one thread's large memory reservations failed from 2 GB down to 1 MB ("out of memory for
  allocation", the kernel overcommits freely, so a limit on where it may reserve, not RAM); the
  game carried on after those.
- Wine's debugger (`winedbg`, "bt all") gives no call stacks of the emulated x86-64 game, only of
  Wine's native processes, and its attempt to attach ended the game (exit 84, 13:03:57).

Cause not found. To tell DXVK from the rest: the same game with `-vulkan` (no DXVK).

### Vulkan: the Adreno 619 cannot (13:08-13:13)

`GAME_ARGS=-vulkan` (the maintainer: nothing was touched before the Direct3D 11 lock-up). The game
showed "Failed to initialize Vulkan. Please make sure your driver and GPU support Vulkan." A second
run with Wine's refused Vulkan extensions logged (`warn+vulkan`), the loader's errors and Source 2's
console log (`-condebug`, `game/steampal/console.log`). Wine refused only extensions no Windows game
needs (Linux file-descriptor sharing, Wayland surfaces); Source 2's own log names the cause:

    [RenderSystem] Using VK_EXT_memory_budget set texture memory budget to 4278 MB.
    [RenderSystem] Subgroup wave op support required because VulkanRequireSubgroupWaveOpSupport is
    set in gameinfo.gi, but subgroup wave ops are not supported.  Vulkan device creation failed.

*Subgroup (wave) operations* let the GPU threads that run one shader together exchange values
directly. The game's `game/steampal/gameinfo.gi` sets `"VulkanRequireSubgroupWaveOpSupport" "1"`
(and `"VulkanOnly" "1"`, `"DefaultRenderSystem" "-vulkan"` on Linux): it was made for the Steam
Deck. Turnip on this phone (`vulkaninfo`: Mesa 26.2.5, Vulkan 1.3) reports subgroup operations in
**compute shaders only** (`subgroupSupportedStages: COMPUTE`). Mesa's source says why
(`tu_device.cc`): drawing stages get them only with `has_getfiberid`, an instruction that tells a
thread its place in the group; `freedreno_devices.py` gives it to Adreno 6xx's fourth generation
(660, 690, the 7c+ family) and to Adreno 7xx. The 619 is first generation (with 615, 616, 618), so
**the hardware lacks it**; the OnePlus 8's Adreno 650 (ARMDeck) is third generation and lacks it
too. Claiming support in the driver would hand the game shaders the GPU cannot run. So Aperture
Desk Job's Vulkan renderer is out of reach on both phones; its `utdeck-env` was removed.

## Closed for now (2026-11-21), back with a stronger device

The exit ("two games measured with Vulkan against their Direct3D path") is not met, and cannot be
on this phone. The maintainer closed 3.9 until a more powerful device runs Ubuntu Touch; in their
view even the Nothing Phone (1) would not be capable enough. What 3.9 found:

- **Vulkan in a Windows game depends on the game's build and on the GPU generation, not on our
  runtime.** Unity games have Vulkan only if their makers built it in (PvZ Replanted: not).
  Source 2 games made for the Steam Deck require subgroup operations in drawing shaders, which Adreno
  6xx has only from its fourth generation (660, 690, the A635 family) and Adreno 7xx has in full.
  The Adreno 619 (stone) and 650 (OnePlus 8) lack it in hardware. The Nothing Phone (1)'s Adreno 642L
  is an A635 variant (the 778G shares its die with the QCM6490, Mesa's "FD643", fourth generation),
  so it should pass that check; its limit is speed.
- **Direct3D 12 through VKD3D-Proton beat Direct3D 11 through DXVK by a third** in PvZ Replanted
  (16.9 against 12.7 fps, same spot, 2400x1080), and Mesa patch 0006 works in a real game. A
  candidate default for Unity games whose build includes Direct3D 12 (`-force-d3d12`), to be checked
  on a second Unity game first.
- `GAME_ARGS` in a game's `utdeck-env`: launch options without touching Steam's account files.
- Found on the way and fixed: install scripts are never paused (`UTDECK_PAUSE=0`).

Open, for later:
- Aperture Desk Job's Direct3D 11 locks up at its start menu after about 50 s (all threads waiting,
  no GPU fault, cause not found). Next tries: Proton's base, a smaller resolution.
- A Visual C++ 2015-2019 uninstaller that finished but did not exit (12:51): does it happen on a clean
  prefix too?
- Steam's notices before a launch (the Steam Input page) wait on hidden Steam's invisible screen:
  stage 4's library has to show them.
- `-force-d3d12` as a Unity default: a second Unity game.
- The Vulkan comparison itself: on a device with an Adreno 6xx fourth generation or 7xx, with a game
  that ships Vulkan (Aperture Desk Job is installed on stone, 4.65 GB, and can be tested there
  again).

## Events

- 2026-11-21: 3.9 started; PvZ Replanted read (Unity 6, Direct3D 11 by default, Direct3D 12
  included); `GAME_ARGS` added.
- 2026-11-21: PvZ with `-force-vulkan`: not built with Vulkan ("InitializeEngineGraphics failed");
  switched to `-force-d3d12`.
- 2026-11-21: PvZ, same spot, 2400x1080: Direct3D 12 (VKD3D-Proton) 16.9 fps against Direct3D 11
  (DXVK) 12.7 fps.
- 2026-11-21: Aperture Desk Job (Source 2, `-vulkan`) chosen as the Vulkan game; Steam began with its
  Linux build, switched to the Windows build with `set-default.py --app 1902490 utdeck`, keeping the
  shared data already downloaded.
- 2026-11-21: Aperture Desk Job installed (4.65 GB), checked clean, app drawer entry made.
- 2026-11-21: its first start failed: install script frozen by the pause helper (fixed:
  `UTDECK_PAUSE=0` for install scripts), a Steam Input notice waiting on the hidden screen (clicked
  through), a hung Visual C++ uninstaller (ended by hand); the game started at 12:56 (Direct3D 11).
- 2026-11-21: Direct3D 11 locked up at the start menu after 50 s of drawing (all threads waiting, no
  GPU fault); cause not found.
- 2026-11-21: Aperture Desk Job with `-vulkan`: the game requires subgroup operations in drawing
  shaders, which the Adreno 619 (and the 650) lacks in hardware; not possible here.
- 2026-11-21: **3.9 closed for now** by the maintainer, until a more powerful device runs Ubuntu
  Touch.
