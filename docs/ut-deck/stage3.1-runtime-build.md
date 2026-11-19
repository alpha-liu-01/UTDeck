# Stage 3.1: the runtime from one script

Started and finished 2026-11-18. Plan: [stage3-roadmap.md](stage3-roadmap.md). Goal: one script builds the whole
Windows runtime from pinned sources, and it replaces Hangover's prebuilt packages on the phone with
nothing lost.

Terms used here:
- *runtime* = Wine, FEX, Box64, DXVK, VKD3D-Proton and Wine Mono: everything between the game and
  our GPU driver.
- *stripping* = removing the debug information compilers leave in a program file; the code is the
  same, the file is smaller. Kept unstripped copies are what `addr2line` reads to turn a crash
  address into a source line.
- *ARM64X* = one DLL with both a plain ARM64 and an ARM64EC half (see the stage 3 roadmap).

## Steps

| Step | What | Status |
| --- | --- | --- |
| 1 | `build-runtime.sh`: every part from pinned sources, in the cross container | **Done** |
| 2 | On the phone, next to Hangover: `runtime-push.sh`, `env.sh` with a runtime switch, `prefix-dlls.sh` | **Done** |
| 1b | Every Wine module Hangover has (sound, gamepads, video, TLS), checked by the script | **Done** (found missing in the first build) |
| 1c | Two clean builds identical byte for byte | **Done** |
| 3 | Stage 2's console tests on both runtimes (`rt-console.sh`) | **Done**: same results, same speed |
| 4 | Stage 2's graphics tests on both runtimes (`rt-gfx.sh`), plus sound and Direct3D 12 | **Done**: same results; Direct3D 12 runs |
| 5 | Our runtime as the default; Hangover only for comparison | **Done** (3DMark's prefix updated, backup kept) |
| 6 | Manual run M9: 3DMark 11 and the gamepad on our runtime | **Done**: both work; the tests fill the screen |

## Step 1: the build script

[`tools/wine-build/build-runtime.sh`](../../tools/wine-build/build-runtime.sh) builds, in the
cross container ([`tools/wine-build/Dockerfile`](../../tools/wine-build/Dockerfile), Ubuntu 24.04
with the phone's arm64 libraries) and with llvm-mingw 20261104 for the Windows side:

| Part | Source (pinned) | Recipe | Output |
| --- | --- | --- | --- |
| Wine 11.16 | AndreRH/wine `3ea3a149` (Hangover 11.16's) + our patches 0001-0006 | Hangover's Ubuntu 24.04 package: `--enable-archs=arm64ec,aarch64,i386`, `--with-mingw=clang` | `bin/`, `lib/wine/{aarch64-unix,aarch64-windows,i386-windows}`, `share/wine` |
| FEX | AndreRH/FEX `fa556167` (Hangover's) | FEX's `toolchain_mingw.cmake`, targets `wow64fex` and `arm64ecfex` | `libwow64fex.dll`, `libarm64ecfex.dll` |
| Box64 | AndreRH/box64 `2f130fab` (Hangover's) | `-DARM_DYNAREC=ON -DWOW64=ON`, target `wowbox64` | `wowbox64.dll` |
| DXVK 2.7.1 | AndreRH/dxvk `2c50a567` (v2.7.1 + an ARM64EC cross file) + two upstream build fixes | DXVK's `package-release.sh` | `lib/dxvk/{arm64ec,x32,aarch64}` |
| VKD3D-Proton 3.1 | HansKristian-Work/vkd3d-proton `4522cc1a` (v3.1; new, Hangover has none) | its `package-release.sh --build-arm64x` | `lib/vkd3d-proton/{arm64x,x86}` |
| Wine Mono 11.3.0 | dl.winehq.org, SHA-256 checked against Wine's source | copied | `share/wine/mono/` (wineboot installs it from there) |

`PARTS="dxvk vkd3d" build-runtime.sh` rebuilds only some parts; every run reassembles the folder,
writes `BUILD-INFO` (sources, commits, patches, toolchain, checksums) and packs
`build/runtime/utdeck-runtime-<version>.tar.xz`. **A clean build of everything takes 3-5 minutes**
on the 56-thread PC (166-305 s measured; Wine is most of it); after a patch, only what changed is
rebuilt.

What the runtime leaves out: headers, import libraries (`*.a`) and Wine's development tools
(`winegcc`, `widl`, `winebuild`, ...), which build programs and are never used to run one; DXVK's
plain x86-64 build (on ARM64EC Wine an x86-64 game should load the ARM64EC build, which runs
natively; the x86-64 build would run through FEX). Debug information is stripped; the unstripped
files stay in `build/runtime-obj/` for `addr2line`.

**Size:** 1.1 GB unpacked (Hangover's install: 2.4 GB), 210 MB packed. The parts: Wine's ARM64 and
ARM64EC DLLs about 720 MB (most of Wine's DLLs are ARM64X, both halves in one file), its 32-bit
DLLs about 220 MB, `share/wine` 126 MB (locale data 40 MB, Wine Mono 85 MB), DXVK 34 MB,
VKD3D-Proton 15 MB.
Since Wine copies its DLLs into every prefix, the stripped files also make each **prefix 974 MB
instead of 2.3 GB**.

### Problems met in the build

1. **FEX's version string is limited to 31 characters**: FEX writes it into the emulated CPU's
   name (`CPUID`), and a `static_assert` stopped the build at 40. Shortened to
   `FEX-fa556167-utdeck`.
2. **DXVK 2.7.1 does not compile with LLVM 23** (llvm-mingw 20261104): `config.cpp` used
   `std::transform` without `<algorithm>`, and one `unordered_map::emplace` with an empty
   `std::tuple()` trips the newer libc++. Both were fixed upstream after 2.7.1 ("Fix build failure
   with Clang 22", 1676dcaf; "Add missing includes", 1db34b56); we carry them as
   [`tools/wine-build/patches-dxvk/`](../../tools/wine-build/patches-dxvk/) (the second backported:
   2.7.1's `util_small_vector.h` has fewer includes). Same DXVK code, only the build fixed.
   Hangover still uses an older llvm-mingw (LLVM 19), which is why its DXVK builds unchanged.
3. **Wine's resources need locale data on the build machine**: "unable to load locale.nls" from
   `wrc`. Hangover's recipe runs `make -C nls` in the build-machine tools; ours now does too.
4. **Wine's bitmap fonts need FreeType on the build machine too**: `sfnt2fon` (which makes the
   `.fon` files) had been configured `--without-freetype` (fine for stage 2's two `.so` files, not
   for a full build). The container now has amd64 FreeType as well as arm64.
5. DXVK's ARM64EC cross file names plain `strip`, which cannot read ARM64 PE files; the build puts
   `llvm-strip` first in `PATH` under that name.
6. **The first full runtime had no sound.** Every console and graphics test passed on it, but a
   `winebth` (Bluetooth) error that Hangover did not show led to a comparison of the Unix-side
   modules: ours lacked `winepulse.so` (sound), `winealsa.so`, `winegstreamer.so` (video),
   `wineusb.so`, and `winebus.so` had no SDL2 or udev (gamepads), `secur32` no GnuTLS (TLS),
   `mountmgr` no D-Bus. The container had only the arm64 libraries the Wayland driver needed, and
   Wine's configure leaves out quietly what it cannot find. Fixed in the
   [Dockerfile](../../tools/wine-build/Dockerfile) with Hangover's build dependencies (PulseAudio,
   ALSA, SDL2, udev, libusb, D-Bus, GnuTLS, Kerberos, CUPS, pcap, v4l, PC/SC, GStreamer), minus X11
   and minus what the phone does not have (SANE, gphoto2, ODBC, OpenCL). The build now **fails**
   if configure reports one of the needed features missing or a key module was not built, and
   `BUILD-INFO` lists what configure left out. Ours now has every Unix module Hangover's has
   except `sane.so` (scanners) and `winex11.so` (X11, not used), plus `winscard.so` (smart cards)
   and `wpcap.so` (packet capture).
7. Installing those arm64 headers made apt replace the container's Python with the arm64 one
   (GLib's headers depend on "python3 | qemu-user | qemu-user-static"), which cannot run there;
   the amd64 `qemu-user-static` satisfies the dependency instead.
8. **Two clean builds differed in 1,586 files**, four bytes each: the linker stamps the build time
   into every PE file. With `SOURCE_DATE_EPOCH` set to the Wine commit's time, two clean builds
   are now **identical in all 1,812 files** (only `BUILD-INFO`, which records the build time, and
   so the tarball, differ).

Still left out by configure, on purpose or because the phone lacks it (from `BUILD-INFO`):
OpenCL, SANE (scanners), VA-API video acceleration (no VA driver for Adreno under Halium), gphoto2
(cameras), **FFmpeg** (Wine's newer media decoder, `winedmo`; the phone has `libavcodec60` but not
`libavformat`; GStreamer is there instead, as in Hangover), OSS, ISDN, Samba. FFmpeg is worth a
look in 3.4 if games' videos do not play through GStreamer.

## Step 2: on the phone

- [`tools/ut-probe/runtime-push.sh`](../../tools/ut-probe/runtime-push.sh) (PC side) unpacks the
  tarball into `wine/runtime-<version>/` and points `wine/runtime` at it (older versions stay);
  24 s over USB.
- [`tools/ut-probe/wine-env.sh`](../../tools/ut-probe/wine-env.sh) is now in the repository (it
  was only on the phone) and is installed as `wine/env.sh`. `UTDECK_RUNTIME=utdeck` or `hangover`
  picks the runtime.
- [`tools/ut-probe/prefix-dlls.sh`](../../tools/ut-probe/prefix-dlls.sh) copies the runtime's DXVK
  and VKD3D-Proton into a prefix (64-bit builds into `system32`, 32-bit into `syswow64`); programs
  use them when run with the native overrides.

## Step 3: console tests, both runtimes

[`tools/ut-probe/rt-console.sh`](../../tools/ut-probe/rt-console.sh): a fresh prefix, `wtest` in
all four architectures, start-up times and 7-Zip's single-thread benchmark. Run twice per runtime,
alternating (ours, Hangover, then the same again after the full rebuild):

| | Ours | Hangover 11.16 |
| --- | --- | --- |
| New prefix | **51.5-51.8 s, 974-978 MB** | 71.8-73.2 s, 2.3 GB |
| `wtest` results (all four architectures, Box64 and FEX for 32-bit) | identical checksums | identical checksums |
| `wtest` times (int / float / copy, ms; ARM64, x86-64, x86) | 35.9 / 16.3 / 80; 58.9 / 54.3 / 79; 149 / 67 / 81 | 35.6-35.9 / 16.3 / 79-80; 58.9 / 54.4 / 79; 150 / 67 / 78-83 |
| Start-up, cold / warm (ms): ARM64 | 2361-2454 / 590-751 | 2017-2555 / 640-704 |
| x86-64 | 2803-3472 / 1244-1509 | 3124-3517 / 1170-1291 |
| x86 | 3055-3163 / 1302-1395 | 3131-8110 / 1305-1367 |
| 7-Zip, one thread (MIPS): ARM64 | 3504, 3585 | 3606, 3628 |
| x86-64 (FEX) | 2847, 2921 | 2910, 2842 |

The same code, built the same way, runs at the same speed: the differences go both ways and are
the size of the phone's run-to-run spread (cores are switched on and off by its power
management). The prefix is created faster and is less than half the size because Wine copies
stripped DLLs into it. (Cold-start outliers of 8 s happen when a program starts while the
previous `wineserver` is still exiting.)

## Step 4: graphics, sound, Direct3D 12

[`tools/ut-probe/rt-gfx.sh`](../../tools/ut-probe/rt-gfx.sh), with the screen off (frames are still
presented). Frame rates (mean):

| Test | Ours | Hangover 11.16 |
| --- | --- | --- |
| Vulkan, x86-64, fullscreen 1080x2400 | 285-290 fps | 291-295 fps |
| Vulkan, ARM64, window 960x540 | 652-670 fps | 640-678 fps |
| D3D11 (DXVK), window | 353 fps | 367 fps |
| D3D11, fullscreen | 154 fps | 154 fps |
| D3D11, mode change to 1280x720 / 1920x1080, scaled to the screen (patches 0005, 0006) | 139 / 124 fps | 135 / 124 fps |
| D3D9 (DXVK), 32-bit / 64-bit, fullscreen | 139 / 146 fps | 141 / 144 fps |
| **D3D12 (VKD3D-Proton 3.1)**, window / fullscreen | **389 / 165 fps** | (no VKD3D-Proton) |
| Sound: 3 s tone through `winepulse` | PulseAudio stream "sndtest.exe", 48 kHz, played in 3,031 ms | the same, 3,028 ms |

- **Direct3D 12 works on the Adreno 619**, new in this stage
  ([`d3d12test.c`](../../tools/ut-probe/win/d3d12test.c): device, three-buffer flip swapchain, a
  root signature, a pipeline from DXBC shaders, a draw per frame). VKD3D-Proton reports feature
  level 11_1, shader model 5.1, resource binding tier 3, resource heap tier 2, no tiled resources.
  So D3D12 games that ask for feature level 12_0 or shader model 6 will refuse to start here; ones
  that accept 11_x may run. Stage 3.2 will tell.
- [`sndtest.c`](../../tools/ut-probe/win/sndtest.c) plays a quiet tone (5 % of full scale, the
  system volume untouched) and the script asks PulseAudio which streams play meanwhile. It would
  have caught step 1's missing sound at once; it is part of the standard run now.
- No gamepad was connected during these runs (both runtimes list no controllers); checked in M9.
- Mode changes need the landscape monitor (`UTDECK_WAYLAND_LANDSCAPE=1`, as 3DMark 11 is started):
  without it Wine offers only portrait modes and `ChangeDisplaySettingsEx` returns -2
  (`DISP_CHANGE_BADMODE`). The script now tests mode changes that way.
- A fresh prefix logs the same first-start errors on both runtimes (`RpcSs`, `Unsupported style`),
  Wine's usual noise.

## Step 5: our runtime as the default

`env.sh` now picks our runtime unless `UTDECK_RUNTIME=hangover`; Hangover stays on the phone for
comparisons. The 3DMark 11 prefix was copied to `prefix-hangover-backup` (3.5 GB, 12 s), then
updated to our Wine (`wineboot -u`, 96 s) and given our DXVK and VKD3D-Proton; it went from 3.5 to
2.4 GB. The Lomiri launchers (`3dmark11-app.sh`, `vktest-app.sh`, `gptest.sh`) use `env.sh`, so
they now run on our runtime.

## Manual run M9 (maintainer, 2026-11-18): 3DMark 11 and the gamepad on our runtime

**Gamepad** ([`gptest.sh`](../../tools/ut-probe/gptest.sh), log `gp-231645.log`): the Betop T6,
through `winebus`'s SDL2 backend as an Xbox controller (`045e:02e0`, `is_gamepad 1`), as XInput
controller 0 and the winmm "Wine joystick driver" (10 buttons, 5 axes). 54 events in 20 s: the
D-pad, A, B, X, Y, LB, RB, both stick clicks; both sticks over their full range (-32768 to 32767);
both triggers to 255. Rumble: `sdl_device_haptics_start ... duration_ms 1000`, and the maintainer
felt it. The same as M8 on Hangover, so `winebus` with SDL2 and udev (missing from the first build)
works.

**3DMark 11** (started from the app drawer, log `m6-231754.log`): our Wine, FEX, our DXVK 2.7.1
(the log shows "Build: aarch64 clang 23.1.5" for the 64-bit tests and "x86 clang 23.1.5" for the
32-bit GUI's parts) and Turnip. The maintainer confirmed the 3D picture and the sound, and that the
**tests now fill the screen** (patch 0006's fit, the default since stage 2.4; in M6 the 1280x720
picture sat in a corner). The maintainer ended 3DMark by hand after that, so there are no new scores;
the run was for function, and the stage 2 tests above already show the speed is unchanged. Sensors
over the 8 minutes: GPU at 700 MHz and 90 % busy or more in 102 of 243 samples, CPU and GPU at
most 60 °C (M6: 62 °C), at least 2,585 MB of memory free (M6: 2.6 GB).

## Result

**Stage 3.1 is done (2026-11-18).** The exit is met:
- the stage 2 tests pass on our runtime unchanged: console programs in all four architectures,
  Vulkan, Direct3D 9 and 11 (with mode changes and scaling), sound, the gamepad with rumble, and
  3DMark 11 from the app drawer;
- no Hangover file is in use any more: `env.sh` picks our runtime, and Hangover stays on the phone
  only for comparisons (`UTDECK_RUNTIME=hangover`);
- size and start-up time against Hangover's: 1.1 GB against 2.4 GB, prefixes 974 MB against 2.3 GB
  and created in 52 s against 72 s, programs start as fast;
- the Direct3D 12 smoke test: VKD3D-Proton 3.1 runs on the A619, feature level 11_1, shader model
  5.1.

Beyond the plan: the build is reproducible byte for byte, and it checks for itself that Wine has
every feature we need.

What 3.1 leaves for later: FFmpeg for Wine's newer media decoder (3.4, if game videos need it);
Hangover's prefix backup (`prefix-hangover-backup`, 3.5 GB) can go once 3.2 has run games on the
updated prefix.

## Events

- 2026-11-18: stage 3 roadmap written; 3.1 started. Hangover's recipes and Proton 11's ARM64 build
  read; `build-runtime.sh` written; five build problems fixed; the runtime (1.1 GB) installed on
  the phone next to Hangover; console tests started. Then: the first runtime had no sound (missing
  build dependencies, fixed, and the script now checks); builds made identical byte for byte;
  console, graphics, sound and Direct3D 12 tests equal to Hangover's; our runtime made the
  default; M9 written.
- 2026-11-18, later: M9 by the maintainer: the gamepad (all buttons, sticks, triggers, rumble) and
  3DMark 11 (3D, sound, now filling the screen) work on our runtime. Stage 3.1 done.
