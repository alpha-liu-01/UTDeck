# Stage 3.2: the game list and the first table

Started 2026-11-18, closed 2026-11-19 with no table entries. Plan: [stage3-roadmap.md](stage3-roadmap.md). Goal: real games on our runtime
(3.1), with frame rate, CPU, GPU, RAM and temperature written into a table, so that the Wine base
decision (3.3) and every default (3.4, 3.5) is measured on games. Exit: five or more games in the
table.

Terms used here:
- *mod pack* = community changes to a game, here as extra DLLs that the game loads.
- *ASI loader / ASI plugin* = a DLL placed next to the game under the name of one it loads anyway
  (`dinput8.dll`); it then loads every `.asi` file from `scripts/`, small DLLs that patch the game
  in memory (widescreen fixes, extra options).
- *DLL wrapper* = a DLL with the name of a system one (`d3d9.dll`) that sits in the game folder,
  changes a few calls and passes the rest on to the real one.
- *visual novel* = a story game made of text, still pictures, voice and music; little 3D work but
  many large images and video clips.

## The games

The maintainer's games that start without Steam (both in `third_party/`, not in git), copied to
the phone into `~/Documents/UTDeck/Games/` (MTP-visible, as the roadmap plans):

| Game | Year, engine | Binary | Graphics | Sound | Video | Extras |
| --- | --- | --- | --- | --- | --- | --- |
| **Need for Speed: Most Wanted** with the Redux 3.0 mod pack (12 GB) | 2005, EA Black Box's engine | `speed.exe`, 32-bit x86 (4 GB patched) | Direct3D 9 (plus `d3dx9_26`), through ThirteenAG's `d3d9-wrapper` in the game folder | DirectSound | EA's VP6 (32 files, decoded by the game) | Ultimate ASI Loader (`dinput8.dll`) with about 15 plugins: widescreen fix, extra options, NextGenGraphics, HD reflections, shader loader, race fixes; HD texture packs |

(Summer Pockets was listed here too; withdrawn, see below.) A 32-bit Direct3D 9 game runs through
WoW64 (Box64 or FEX) with DXVK's 32-bit build, the path that only `d3d9test` had covered so far.

What the phone offers them, checked before the first start:
- Locales `ja_JP.utf8` and `zh_CN.utf8`, CJK fonts (WenQuanYi Micro Hei, Takao, Droid Sans Fallback).
- GStreamer 1.24 with base, good and bad plugins, Theora and Vorbis, but **no WMV decoder**
  (`gst-libav`) and no ASF demuxer (`plugins-ugly`): Summer Pockets' two WMV openings will
  probably not play through Wine's `winegstreamer`. To see how the game copes.

## Tools made for this stage

- **Frame statistics in Wine** ([patch 0007](../../tools/wine-build/patches/0007-win32u-utdeck-frame-statistics.patch)):
  with `UTDECK_FRAMESTATS=<file>`, `win32u` appends one line per second for every process that
  presents with Vulkan (so DXVK, VKD3D-Proton and Vulkan games alike): frames, fps, mean and
  longest frame time, swapchain size. Nothing is done when the variable is unset. A starting point
  for stage 5's performance HUD.
- **Display modes with the screen's shape** ([patch 0008](../../tools/wine-build/patches/0008-win32u-add-smaller-modes-with-the-screen-aspect-ratio.patch)):
  Wine's emulated modes are 4:3, 16:9, 16:10 and a few wide ones; a phone is 20:9, so a game set
  below 2400x1080 got black bars or a stretched picture. The patch adds smaller modes with the
  screen's own ratio: here 1200x540, 1600x720, 1920x864 and 2000x900 (checked with `wtest`).
  Generic, a candidate for upstream.
- [`tools/ut-probe/game-run.sh`](../../tools/ut-probe/game-run.sh): starts a game in the games
  prefix (`wine/prefix-games`) with DXVK and VKD3D-Proton, the landscape monitor and MAILBOX, and
  records the frame statistics and, every 2 s, CPU load, big-core clock, GPU load and clock, CPU
  and GPU temperature, free memory and the game's memory. One Lomiri desktop file per game.
- [`tools/ut-probe/push-d3dx9.sh`](../../tools/ut-probe/push-d3dx9.sh): installs Microsoft's own
  d3dx9 DLLs into a prefix, as winetricks does (from Microsoft's DirectX End-User Runtime, June
  2010, checked against winetricks' SHA-256). Microsoft's licence lets games ship these, not
  UTDeck, so they are fetched per prefix, never put in the runtime.

## Need for Speed: Most Wanted (Redux 3.0): dropped

1. **First start: Wine's d3dx9 stopped the game** with an assertion,
   `iface->lpVtbl == &ID3DXEffectPool_Vtbl` (`dlls/d3dx9_36/effect.c`). The mod pack's shader
   loader and NextGenGraphics use `d3dx9_43`, the game `d3dx9_26`; Wine builds each d3dx9 version as
   a separate DLL that accepts only its own objects, Microsoft's accept each other's. With
   Microsoft's `d3dx9_26` and `d3dx9_43` (`push-d3dx9.sh`, overrides `d3dx9_26,d3dx9_43=n`) it got
   past that.
2. **Second start: it ran, slowly and wrongly drawn.** Measured: intro, then a minute of loading
   (CPU busy, GPU idle, no frames), menus at about 20 fps, then 10-12 fps with the GPU 97 % busy at
   700 MHz, 2400x1080, 59-60 °C, 700 MB resident. The maintainer saw the left half of the screen
   washed out, the intro movie (VP6) not played to the end.
3. With all mods but the widescreen fix moved aside (into `disabled-by-utdeck/` in the phone's
   copy), the maintainer saw black-and-white graphics and no 3D on the right half, and judged
   **this copy of the game broken**: the mod pack has replaced the game's own files (cars, tracks,
   textures), so it cannot be tested as the game itself. Dropped at the maintainer's request; the
   copy stays on the phone for now (12 GB). For reference, the maintainer ran the same game with
   only a resolution mod at 60 fps on Windows 11 ARM64 on a Snapdragon 845 (2160x1080), a
   stronger GPU (Adreno 630) with Qualcomm's own Direct3D driver.

Lessons kept from it: mods that mix d3dx9 versions need Microsoft's d3dx9 under Wine; the frame
statistics and the per-second sensors make a run readable without watching it.

## Summer Pockets REFLECTION BLUE: withdrawn

It started and ran (the maintainer found it working well apart from the WMV opening, not reached),
but a check made afterwards found a DRM crack in the folder: `kDays.dll` names itself "Visual Art's
Game Universal Cracker Kernel" (for the publisher's PlayDRM protection). Its results are withdrawn
and it is not in the table; the launcher was removed. What it showed about the runtime is kept,
without numbers: 32-bit Direct3D 9 through Box64 and DXVK works, and for this engine Box64 was
much faster than FEX, so the choice of 32-bit emulator stays a per-game setting.

## Games not tested: copies with a DRM bypass

The maintainer also offered a copy of Ys: Memories of Celceta (Direct3D 11, 32-bit) that starts
without Steam. It is a cracked release (a Steam emulator in place of `steam_api.dll`), so it was
not tested: UTDeck does not run games through DRM bypasses (stage 3 rules). If the maintainer owns
it on Steam, it is a candidate for stage 7, once Steam runs.

The maintainer then offered Need for Speed: Most Wanted Black Edition installed from the DVD. Its
`speed.exe` (6,029,312 bytes, version 1.3) carries the marker `*!ReLOADeD!*`: it is a no-DVD crack,
not the DVD's own executable, so it was not unpacked onto the phone. The DVD's own executable checks
the disc with SecuROM, which the phone (no optical drive) cannot satisfy.

**Check before the first start, from now on:** look in the game folder for a Steam emulator
(`steam_emu.ini`, a replaced `steam_api.dll`), crack DLLs, and group markers in the executables
(`strings`). Games with one are not tested.

## Result

**Closed with no games in the table (2026-11-19).** The maintainer's copies could not be used: two
carried DRM bypasses (one found only after it had been tested; its results withdrawn), one more had
one in its executable, and one had been broken by its mod pack. The maintainer does not usually
buy DRM-free games, and those with recent graphics APIs are rare, so the table moves to the
maintainer's Steam library: [plan-change-steam-early.md](plan-change-steam-early.md) (stages 3.S
and 3.2b). What 3.2 leaves: patches 0007 and 0008, `game-run.sh`, `push-d3dx9.sh`, the bypass
check before every first start, and two runtime facts (32-bit Direct3D 9 through Box64 and DXVK
works; mods that mix d3dx9 versions need Microsoft's d3dx9).

## Events

- 2026-11-19: plan change written: Steam moves into stage 3. 3.2 closed.
- 2026-11-19: Summer Pockets run by the maintainer (M10), then withdrawn: a DRM crack found in its
  folder. The modded NFS deleted from the phone at the maintainer's request. NFS Black Edition from a
  DVD install not tested: its executable is a no-DVD crack. A check for DRM bypasses added before
  every first start.
- 2026-11-18: 3.2 started. The maintainer's two games inspected (both 32-bit Direct3D 9; NFS with a
  large mod pack, Summer Pockets with WMV openings) and copied to the phone. Patches 0007 (frame
  statistics) and 0008 (modes with the screen's shape), `game-run.sh`, `push-d3dx9.sh` written.
  NFS Most Wanted: Wine's d3dx9 assertion fixed with Microsoft's d3dx9; then dropped, the copy
  being broken by its mod pack. A cracked copy of another game not tested.
