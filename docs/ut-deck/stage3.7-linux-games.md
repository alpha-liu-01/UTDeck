# Stage 3.7: Linux x86 games

Goal (roadmap): games with a native Linux build (Goat Simulator's) through FEX in Steam's Linux
runtime, against the same games' Windows builds through our runtime. Exit: two games measured both
ways; a rule for which build UTDeck installs.

Started 2026-11-21 (after 3.6; 3.4's items 8-11 stay open).

## What is known before starting

- **Steam's default for a game with a Linux build is that build** (3.2b: Goat Simulator came
  down as its Linux build). UTDeck's tool covers it only when forced per game
  (`set-default.py --app APPID utdeck`), as done for Goat Simulator.
- **Steam's own path for an x86 Linux game on this ARM64 client**: its FEX tool (app 3127680,
  `steamapps/common/FEX-Emu`: FEX `FEX-2607-76-g37265b1`, native ARM64 programs in `usr/bin`) inside
  Steam Linux Runtime's container (pressure-vessel; SLR 4 is installed). The tool's script
  (`fex-compat-tool`) wants an x86 system with Mesa at `/usr/share/guestos/fex-mesa`
  (`graphics_provider.json`), as SteamOS ships on the Steam Frame; `STEAM_COMPAT_GRAPHICS_PROVIDER`
  can point it elsewhere. On the phone: no `/usr/share/guestos`, and `FEX-Emu/rootfs` is empty. A
  system folder is off limits (nothing system-wide), our own folder is possible.
- **The container**: user namespaces work for the user (`unshare -U -r` succeeds) and `bwrap` is
  installed, so pressure-vessel may start; 3.S's failure was about Steam's web helper, not games.
- **Graphics**: Steam's FEX package has **thunks** (FEX's bridges that hand an x86 program's calls
  to a native ARM64 library): OpenGL (`libGL`), EGL, Vulkan, `libwayland-client`, `libdrm`, ALSA
  sound. An x86 Mesa could not use our GPU path (Turnip through Halium's `android_wlegl`), but the
  thunks hand the calls to the phone's own libraries:
  - a game drawing with OpenGL on **X11** (GLX) gets Xwayland, which has no GPU on Ubuntu Touch
    (software rendering only);
  - a game drawing on **Wayland** with EGL, or with Vulkan, reaches our Mesa (zink, Turnip).
  So a Linux build can use the GPU only if it can draw on Wayland: mostly a question of the SDL
  version it bundles (SDL2 has a Wayland driver from 2.0.2, usable from about 2.0.22).
- ARMDeck (the mainline OnePlus 8) met the same: x86 Linux games closed at once without the
  `fex-mesa` system (`docs/compat-perf-audit.md`, C2).

## Plan

1. **The games**: two of the maintainer's games with a Linux build (Goat Simulator, and one more).
   For each, read the Linux build's libraries before running anything: SDL's version, OpenGL or
   Vulkan, X11 or Wayland; and check it for DRM bypasses (`gamecheck.py`).
2. **The x86 system**: FEX's own root filesystem (an x86-64 Ubuntu, from FEX's servers), unpacked
   into UTDeck's folder (not a system path), handed to Steam's tool through
   `STEAM_COMPAT_GRAPHICS_PROVIDER`, with the thunks turned on.
3. **Steam's path**: the Linux build started by Steam through its FEX tool and SLR's container;
   what starts, what draws, and where.
4. **Both ways**: the same spot with the Windows build through our runtime (`measure.sh`), the
   Linux build through FEX; frame rate, CPU, GPU, memory, start-up.
5. **The rule** for which build UTDeck installs, and how (`set-default.py --app` per game, or a
   rule in stage 4's library).

Costs to keep in mind: a game's Linux and Windows builds share one install folder, so switching
means a download each way (Goat Simulator: 2.4 GB) unless one is kept aside; FEX's root filesystem
is about 1-2 GB.

## 1-2. The x86 system and the switch (2026-11-21, 00:00-00:10)

- **FEX's root filesystem**: FEX's list (`rootfs.fex-emu.gg/RootFS_links.json`) offers Ubuntu 24.04,
  the phone's own version (SquashFS, 2026-08-11, 526 MB). Downloaded on the PC, checked against the
  list's hash (XXH3-64 `3517e0e5ea25a473`: matches; `xxhash` from a throwaway Python environment, the
  PC had no tool), unpacked with `unsquashfs` as a normal user (device files and owners skipped),
  1.9 GB: x86-64 and i386 libraries, SDL2 2.30, the Vulkan loader, x86 Mesa (its drivers are built
  for normal Linux GPU access, not Android's KGSL, so they cannot drive our GPU; the thunks route does
  not need them), and its own `graphics_provider.json` (what Steam's tool looks for). On the phone in
  `utdeck-probe/fex-rootfs/Ubuntu_24_04` (one archive, one push).
- **Goat Simulator's Windows build kept aside** (`utdeck-probe/aside/GoatSimulator-windows`, 2423 MB,
  and its install record), to switch back without a download.
- **`set-default.py --app APPID --remove`** (new): the game goes back to Steam's own choice. Done for
  Goat Simulator with Steam stopped; at its next start Steam marked the game "Update Required" and
  began reconfiguring it, and wanted app 1391110 first (Steam Linux Runtime 2.0, "soldier", the
  container for Linux builds of this age), so it held the game's update meanwhile.

## The other kinds of games (the maintainer's question, 2026-11-21)

The maintainer listed kinds not yet tested: Windows on ARM games, Linux ARM64 games, games drawing
with Vulkan (no DXVK). Proposed order: **Vulkan next** after 3.7 (Unity games can switch with
`-force-vulkan`, PvZ Replanted is one; our runtime already hands Vulkan to Turnip, `vktest`; if Unity's
Vulkan renderer beats its Direct3D 11 through DXVK, that is a default per engine); **Linux ARM64**
once a game is found (Valve's ARM64 Steam Frame should bring builds; the same Wayland question as
here, and Steam's ARM64 runtime); **Windows on ARM** last (very rare on Steam; our runtime already
runs ARM64 Windows programs). For 3.7's second game: none of the maintainer's games is known to have
a Linux build; Unturned (free, Linux x86-64, Unity) was suggested.

## 3. The Linux build, and Steam's path (2026-11-21, 00:12-00:35)

- Steam installed the Linux build (depot 265932: `Binaries/linux-amd64` and `linux-x86`, 2.3 GB)
  and Steam Linux Runtime 2.0 ("soldier", 671 MB). At 00:13:01 the game was started once through
  hidden Steam (`-applaunch 265930`, probably its app drawer entry) before the DRM check; its
  process ended after 2 s. **`gamecheck.py` afterwards: no DRM-bypass markers** (46 s; 3.2b had
  found this build clean too: Valve's `libsteam_api.so`, a plain `steam_appid.txt`).
- Steam's chain for it: `fex-compat-tool waitforexitandrun` -> `SteamLinuxRuntime_soldier/_v2-entry-point`
  -> `SteamLinuxRuntime/scout-on-soldier-entry-point-v2` -> the game: FEX emulates the whole x86
  container, not just the game.
- **How it draws** (its libraries, read without running it, `elfneeded.py`): `linux-amd64/GoatGame`
  (UE3, 2013) needs `libGL.so.1`, `libSDL2-2.0.so.0` (not bundled: the runtime's), `libopenal.so.1`,
  PhysX and APEX, Valve's `libsteam_api.so`. So OpenGL through SDL2.
- **Graphics routes**: FEX's thunk list (`ThunksDB.json`) has `GL` (only `libGL`, i.e. GLX), `Vulkan`,
  `WaylandClient`, `drm`, `asound`, `cuda`; there is an EGL bridge file (`libEGL-guest.so`) but the
  list does not use it. OpenGL through GLX means X11: Lomiri's Xwayland has no GPU on Ubuntu Touch,
  and a game started by hidden Steam inherits its invisible X screen (`DISPLAY=:50`, Xvfb). Through
  SDL's Wayland driver the game would load the x86 system's own EGL and Mesa: software rendering,
  emulated. **So this Linux build gets no GPU here, either way.** Only a Linux game drawing with
  Vulkan has a route to Turnip (the Vulkan bridge).
- **FEX does not start an x86 program with our root filesystem.** Started by hand with Steam's exact
  chain (`goat-linux.sh`: the graphics provider pointing at our system, the GL and Vulkan bridges and
  x86 memory order on, SDL on Wayland, no `DISPLAY`): "/bin/sh: error while loading shared
  libraries: libc.so.6: cannot open shared object file". The same with FEX alone
  (`FEX_ROOTFS=... FEX /bin/sh`), with `FEXServer` started by hand in the foreground, with and
  without a trailing slash, with and without `FEX_PORTABLE`. The x86 loader's own trace
  (`LD_DEBUG=libs`) shows it trying `/usr/lib/x86_64-linux-gnu/libc.so.6`, the right path, and
  failing: FEX found `/bin/sh` in our folder but does not redirect the program's file opens into it.
  The root filesystem is complete on the phone (all 9009 links, the loader, `ld.so.cache`). Possible
  causes: this FEX build expects its image mounted (SquashFS or EroFS, which needs root on this
  phone), or something of this kernel. Not followed further: it means debugging FEX itself, and the
  result would still be software rendering for this game.

## Closed (2026-11-21), with a rule instead of two measured games

The exit ("two games measured both ways") cannot be met: the Linux build does not start under FEX
here, and none of the maintainer's other games is known to have a Linux build. The maintainer
closed 3.7 with the rule the findings give:

> **A game with both builds: UTDeck installs and runs the Windows build** through our runtime
> (forced per game with `set-default.py --app APPID TOOL`), because a Linux x86 build has no GPU
> route on Ubuntu Touch (OpenGL means GLX on Xwayland, software only; FEX's EGL bridge is not in its
> list). **To look at later**: a Linux x86 build that draws with Vulkan (FEX's Vulkan bridge reaches
> Turnip), once FEX's file redirection works with a folder root filesystem; that FEX work partly
> serves Linux ARM64 games too (the roadmap's "kept for later").

Afterwards: Goat Simulator back on its Windows build (the copy moved back, no download, its install
record restored, "UTDeck runtime (Proton 11)" set again; Steam: fully installed, no update). Kept on
the phone for the later FEX work: FEX's Ubuntu 24.04 root filesystem (`utdeck-probe/fex-rootfs`,
1.9 GB) and Steam Linux Runtime 2.0 (671 MB). For stage 4: the library should force UTDeck's tool on
every Windows game that also has a Linux build, as `set-default.py --app` does by hand now.

## Events

- 2026-11-21: 3.7 started; Steam's FEX tool, its thunks and the container's needs read; plan.
- 2026-11-21: FEX's Ubuntu 24.04 root filesystem fetched, checked and put on the phone; Goat
  Simulator's Windows build kept aside; its tool entry removed, Steam swapping it to the Linux build
  (after Steam Linux Runtime 2.0).
- 2026-11-21: the Linux build installed and checked; it draws with OpenGL through SDL2 (no GPU route
  here); FEX does not start x86 programs with a folder root filesystem (file opens not redirected).
- 2026-11-21: **3.7 closed with the rule** (Windows build through UTDeck); Goat Simulator restored to
  its Windows build; the root filesystem kept.
