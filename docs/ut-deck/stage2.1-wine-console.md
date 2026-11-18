# Stage 2.1: Wine runs on the phone, no window yet

Started and finished 2026-11-18. Plan: [stage2-roadmap.md](stage2-roadmap.md). Goal: a Wine for
ARM64 with FEX, unpacked into a folder (nothing installed), creates a prefix and runs Windows
console programs of three kinds: native ARM64, x86-64 through ARM64EC and FEX, and 32-bit x86
through WoW64.

Terms used here:
- *ARM64EC* = a Windows ABI where ARM64 code and emulated x86-64 code live in one process: Wine's
  own DLLs run natively, only the game's x86-64 code goes through the emulator.
- *WoW64* = how 64-bit Windows runs 32-bit programs; here the 32-bit x86 code goes through an
  emulator DLL (`wowbox64.dll` or `libwow64fex.dll`).
- *Prefix* = the folder that is Wine's "C:" drive and registry (`WINEPREFIX`).
- *TSO* = x86's strict memory ordering between threads; ARM is looser, so the emulator adds
  barriers to imitate it, which costs speed in multi-threaded code.

## Result

**Done.** Hangover 11.16 (Wine 11.16) runs on stone without any change to the system: the prefix
is created in 44 s, and all four kinds of console program run, with identical results. x86-64 code
through FEX keeps 79 % of native speed in 7-Zip's single-thread benchmark.

## What was used

- **Hangover 11.16**, Ubuntu 24.04 arm64 packages
  ([release](https://github.com/AndreRH/hangover/releases/tag/hangover-11.16), tarball SHA-256
  `94611dad...9e72`): `hangover-wine` (Wine 11.16 with `winewayland` and `winevulkan`),
  `hangover-libarm64ecfex` (FEX for x86-64), `hangover-libwow64fex` (FEX for 32-bit x86),
  `hangover-wowbox64` (Box64 for 32-bit x86). Unpacked with `dpkg-deb -x` into
  `~/utdeck-probe/wine/hangover-11.16` (2.4 GB). Wine 11.16 is past the fix for this kernel's
  39-bit address space (Wine bug 59898, 11.15; stage 0).
- The phone: Ubuntu Touch 24.04.5 (noble), kernel 5.4, 4 KB pages, 39-bit address space, 7.4 GB of
  RAM. Every library Wine needs is already on the system except `libsane` (scanners only).
- **Test programs of our own**: [`tools/ut-probe/win/wtest.c`](../../tools/ut-probe/win/wtest.c),
  built by [`win/build.sh`](../../tools/ut-probe/win/build.sh) with llvm-mingw 20261104 (one Clang
  for Windows ARM64, ARM64EC, x86-64 and x86; added to [sources.md](../sources.md)). It prints
  what it was built for and what Windows reports, the x86 features the emulator offers, reserves
  8 GB of address space (1 GB on 32-bit) and touches 256 MB, starts 8 threads, then times
  three benchmarks: an integer hash, an n-body float step, and 64 MB memory copies.
- **7-Zip 26.05** (official builds: Windows x86, x64 and ARM64 `7za.exe`, and the Linux ARM64
  `7zz` as the native reference): its built-in benchmark (`7za b`) is a known, real-world
  compression workload with a rating in MIPS.
- Environment ([`wine/env.sh`](#the-environment) on the phone): Wine's `bin` folder on `PATH`, our
  driver (stage 1's route B) for Vulkan and EGL, `WINEDEBUG=-all`, Mono and Gecko off
  (`WINEDLLOVERRIDES=mscoree,mshtml=`).

## Problems met

1. **Wine loaded libhybris and failed.** The first `wineboot` stopped at once: "Failed to load
   hybris linker ... cannot allocate memory in static TLS block", then "could not load
   kernel32.dll". On Ubuntu Touch the system's `libvulkan.so.1` and `libEGL.so.1` lead into
   libhybris (the Android compatibility layer), and Wine opens them while starting. Solved by
   running Wine with our own driver environment, which UTDeck does anyway: the Khronos loader and
   our Turnip (`LD_LIBRARY_PATH`, `VK_DRIVER_FILES`), our Mesa as the EGL vendor
   (`__EGL_VENDOR_LIBRARY_FILENAMES`). Wine then never touches libhybris.
2. **Each prefix is 2.3 GB.** Wine copies its Windows DLLs into the prefix (no links), and
   Hangover's DLLs carry their debug information: `kernel32.dll` 3.5 MB, 1.0 MB stripped;
   `user32.dll` 10 MB, 3 MB stripped. Stripped, Wine would take about 0.8 GB instead of 2.4 GB, and
   each prefix about the same less. For stage 3 (our own runtime build): strip the DLLs, and share
   one prefix (or link its DLLs) between games.
3. **Start-up times looked like 7 s; they are not.** Our `run-timed.py` waits until its output
   pipe closes, and `wineserver` (Wine's background process, which stays a few seconds after the
   last program ends) keeps it open. Timed with output to a file instead (below).
4. **Our first benchmark was wrong**: the compiler removed the memory copy (it could predict the
   data) and the float check summed to 0 by symmetry. Fixed (data from the clock, a checksum over
   the copies, absolute values); only the fixed numbers are given here.
5. Expected, harmless: `wineboot` reports "Wayland compositor doesn't support wp_viewporter" (Wine
   found Lomiri on its own; this is substage 2.2's blocker, confirmed) and no window driver;
   `sysarm32\rundll32.exe` cannot start (no 32-bit ARM Wine, not needed).

## Measurements

### What Windows programs see

| Program | Built for | Windows reports | Emulator |
| --- | --- | --- | --- |
| `wtest-arm64.exe` | ARM64 | Windows 10 build 19045, native ARM64, user addresses up to `7FFFFFFEFFFF` | none |
| `wtest-arm64ec.exe` | ARM64EC | the same | FEX loaded (`libarm64ecfex.dll`), not used by this code |
| `wtest-x86_64.exe` | x86-64 | the same | FEX (`libarm64ecfex.dll`) |
| `wtest-i686.exe` | 32-bit x86 | WoW64 process "x86" on native ARM64 | Box64 (`wowbox64.dll`) by default; FEX with `HODLL=libwow64fex.dll` |

All of them: 8 GB reserved (1 GB on 32-bit) and 256 MB committed and touched, 8 of 8 threads ran,
and the same checksums as native code.

x86 features offered: through FEX to x86-64 programs, SSE4.2, POPCNT, **AVX, AVX2, FMA, BMI2,
F16C** (no AVX-512), so games that require AVX2 start. To 32-bit programs: Box64 offers the same;
FEX's 32-bit DLL offers SSE4.2 and POPCNT only.

### Speed: our benchmark (ms, lower is better; the second and third rounds)

| | int | float | copy (64 MB x 8) |
| --- | --- | --- | --- |
| ARM64 (native) | 36.1 | 16.2 | 76.5 (7.0 GB/s) |
| ARM64EC (native code, FEX loaded) | 35.6 | 16.3 | 77.8 |
| **x86-64 through FEX** | 58.9 (x1.6) | 54.4 (x3.4) | 77.2 (x1.0) |
| x86 through Box64 | 149 | 67.1 | 79.4 |
| x86 through FEX | 154 | 56.3 | 79.1 |

- The copy costs nothing extra under emulation: in ARM64EC, Windows' own C runtime (`memcpy`) is
  native ARM64 code; only the program's own code is translated.
- Scalar single-precision float with `sqrt` and division is where FEX costs most here (x3.4).
- The 32-bit "int" numbers are not comparable with the 64-bit ones (64-bit integer arithmetic on a
  32-bit target takes several instructions); compare Box64 with FEX only: equal on integers, FEX
  16 % faster on floats.

### Speed: 7-Zip's benchmark (total rating in MIPS, higher is better)

| | One thread | All cores |
| --- | --- | --- |
| Linux ARM64 (`7zz`, native reference) | 3,737 | 14,431 (580 % CPU) |
| Windows ARM64 (`7za.exe` through Wine) | 3,604 (96 %) | 12,696 (88 %, 490 % CPU) |
| **Windows x86-64 through FEX** | **2,946 (79 %)** | 8,118 (56 %, 466 % CPU) |
| x86-64, `FEX_TSOENABLED=0` | | **9,184 (+13 %)** |
| x86-64, `FEX_MULTIBLOCK=0` | | 5,865 (-28 %) |
| Windows x86 through Box64 | 2,311 (62 %) | 7,413 (51 %) |

- Wine itself costs little: native ARM64 Windows code runs at 96 % of the Linux build.
- FEX costs about a fifth in one thread, more on all cores. Part of that is TSO: switching it off
  gave +13 %, and this FEX build honours `FEX_*` environment variables (its DLL lists
  `FEX_MULTIBLOCK`, `FEX_HOSTFEATURES`, `FEX_SMCCHECKS`, ... and reads `AppConfig/` profiles). TSO
  off is only safe for programs that do not rely on x86 memory ordering, so it stays a per-game
  setting, as ARMDeck found (`docs/compat-perf-audit.md`).
- The phone's power management switches cores on and off (`nproc` read 6, later 4), so the
  all-core numbers vary from run to run; the single-thread numbers are the comparable ones.
- Temperatures stayed low (CPU 31-39 °C).

### Start-up time (output to a file; `wtest --quick`, which itself takes 0.3-0.5 s)

| | Cold (no `wineserver` running) | Warm |
| --- | --- | --- |
| ARM64 | 2.9 s | 0.68 s |
| ARM64EC | 3.0 s | 0.63-0.77 s |
| x86-64 (FEX) | 3.1 s | 0.90-1.09 s |
| x86 (Box64) | 3.8 s | 1.40 s (one outlier at 6.3 s) |

Memory: a running `wtest` process is 266 MB (ARM64), 272 MB (x86-64) and 287 MB (x86) resident,
most of it the 256 MB the program itself touches. The outlier is most likely a program starting
just as `wineserver` was shutting down (it exits a few seconds after the last program).

## The environment

`~/utdeck-probe/wine/env.sh` on the phone:

```sh
P=/home/phablet/utdeck-probe
W=$P/wine
export PATH=$W/hangover-11.16/usr/bin:$PATH
export WINEPREFIX=${WINEPREFIX:-$W/prefix}
export WINEDEBUG=${WINEDEBUG:--all}
export WINEDLLOVERRIDES=${WINEDLLOVERRIDES:-mscoree,mshtml=}
export LD_LIBRARY_PATH=$P/zink/lib:$P/vk/lib          # Khronos loader + our Mesa, never libhybris
export VK_DRIVER_FILES=$P/zink/turnip_icd.json
export __EGL_VENDOR_LIBRARY_FILENAMES=$P/zink/egl_vendor.json
export MESA_LOADER_DRIVER_OVERRIDE=zink
export XDG_RUNTIME_DIR=/run/user/32011
unset GLIBC_TUNABLES LD_PRELOAD
```

## What this means for the next substages

- Wine and FEX work on this kernel; any later failure is about display, Vulkan or the game.
- 2.2 needs our own Wine build (the `wp_viewporter` patch): Hangover's sources at 11.16, or Wine
  11.16 with Hangover's FEX DLLs, built in the cross container. The build also gives stripped DLLs.
- Per-game settings to carry: `HODLL` (Box64 or FEX for 32-bit games), `FEX_TSOENABLED`.

## Events

- 2026-11-18: source check (Mesa 26.3.1, DD-Turnip v0.8.0, DroidDeck subsurface fix, Mir 2
  packaging moved). Stage 2 split into four substages ([stage2-roadmap.md](stage2-roadmap.md)).
  Hangover 11.16 unpacked on the phone; first `wineboot` failed in libhybris, fixed with our
  driver environment; prefix created in 44 s. `wtest` written and built for four architectures;
  all ran; benchmark fixed and measured. 7-Zip 26.05 benchmark on four paths; FEX's TSO switch
  tested. Stage 2.1 done.
