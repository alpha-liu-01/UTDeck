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
| **3.1 The runtime from one script** | One script in the cross container builds Wine (Hangover's source, our patches), FEX's two DLLs, Box64's `wowbox64.dll`, DXVK 2.7.1 and VKD3D-Proton 3.1 from pinned sources into one stripped folder, with Wine Mono; installed on the phone next to Hangover | The stage 2 tests pass on it unchanged (console programs in all four architectures, Vulkan, Direct3D 9 and 11, the gamepad); no Hangover file is left in use; size and start-up time against Hangover's; a Direct3D 12 smoke test answers "does VKD3D-Proton run on the A619" |
| 3.2 The game list and the first table | The maintainer's games that can run without Steam (stage 7 brings Steam), plus free and freeware games chosen to cover engines and APIs (Unity, Unreal, GameMaker, D3D9, D3D11, D3D12, OpenGL, 32-bit, .NET); how each is installed and started | A table with five or more games: starts or not, frame rate, CPU, GPU, RAM, temperature, problems |
| 3.3 The Wine base: ours or Proton's | Proton 11's Wine built in our container, our patches carried over, measured against 3.1's on the table's games and the stage 2 tests; the cost of `wineserver` waits on this kernel measured, and whether an esync port is worth carrying | A decision with numbers, written down; the chosen base in the build script |
| 3.4 Defaults that remove losses | The defaults learned in ARMDeck and DroidDeck, each measured on the phone: `DOTNET_EnableWriteXorExecute=0`, FEX TSO and its other options per game, DXVK's config and shader cache, `TU_DEBUG` options for Adreno 6xx, debug output off, a smaller prefix (2.3 GB now), Wine's start-up time | Each default kept or dropped with a measurement; one place where the defaults live |
| 3.5 CPU boost and heat | `uclamp_min` for the game's threads as in the Dolphin port; long runs with CPU and GPU load together; the vendor's thermal service stays in charge, no limit is raised | Frame rate and temperature over 30 minutes with and without the boost |
| 3.6 Lifecycle | What Wine and the game do when Lomiri stops the app (SIGSTOP on app switch and screen lock) and resumes it: sound, timers, the gamepad, the GPU | Pause and resume work in two games, or the problems are written down with a plan |

Order and why: 3.1 first, because every later number should come from the runtime we build, not
from a prebuilt one. 3.2 next, so that the base decision (3.3) and every default (3.4, 3.5) is
measured on real games instead of on our samples. The table grows through 3.3-3.6 until it has
ten or more games (the stage exit).

**For the maintainer, ahead of 3.2:** which Windows games you own outside Steam (GOG, itch.io,
retail discs, Humble), and which Steam games you know run without Steam open. The ARMDeck notes
list your Steam library (Tiny Rails, Slime Rancher, INSIDE, Firewatch, Hue, Undertale, Isaac,
Terraria and others); some of those run from their folder without Steam, and we can try them in
3.2 if you copy them over.

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

Rules for this stage, from the project rules:
- Nothing system-wide on the phone: the runtime lives in a folder, chosen with environment
  variables, next to Hangover until it replaces it.
- Sources are pinned by commit and written into the runtime's `BUILD-INFO`; our patches stay
  separate files.
- Thermal and power limits stay the vendor's; long runs are watched (3.5).
- No DRM bypass: games that need Steam wait for stage 7.
