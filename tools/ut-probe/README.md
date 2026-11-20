# UTDeck probes

Small tools for probing an Ubuntu Touch phone over adb. Plain Python 3 with no extra modules,
because Ubuntu Touch has no compiler and few tools. Push them to a test folder on the phone
(`/home/phablet/utdeck-probe/`) and run them as `phablet`.

| Tool | Runs on | What it does |
| --- | --- | --- |
| `wlegl-probe.py` | phone | Asks the compositor for gralloc buffers over libhybris' `android_wlegl` (what a native Turnip needs to present on Lomiri) and checks them; `show` puts a test pattern on screen through one |
| `run-timed.py` | phone | Runs a command and prints wall time, CPU time, CPU load and frames per second |
| `hybris-barrier-hook.c` | phone (build on the PC with an arm64 GCC, e.g. in `dolphin-ut-cross`) | `LD_PRELOAD` library that gives an Android Turnip loaded through libhybris glibc's `pthread_barrier_*` (dolphin-ut's hook, for programs that cannot install it) |
| `vkcompute/` | phone (build on the PC with `vkcompute/build.sh`) | Small Vulkan compute tests of our own: `vkc.h` (harness), `intops` (signed division, modulo and multiply against a CPU reference), `spvrun` (runs any compute shader with given specialization constants, push constants, buffers and R8 storage images (`--img`) from a fixed seed; times and dumps it; `--profile` splits each dispatch's time, `--batch N` sends N command buffers in one submission as Geekbench does) and `spvcmp.py` (PC: compares two `spvrun` dumps), `wgsize.comp` (does every invocation of a work-group run, for any shape), and `layer/` (`VK_LAYER_UTDECK_dispatchlog`: records every compute dispatch with its shader, specialization constants, push constants and buffers, for replay in `spvrun`; also times submissions and waits) and `dlog-timing.py` (PC: summarises a layer log per workload). Driver chosen like any Vulkan program (`VK_DRIVER_FILES` for Turnip, nothing for the vendor driver through libhybris) |
| `vkcompute/kgsl-trace.c` | phone (built by `vkcompute/build.sh`) | Runs a program under `ptrace` and decodes its KGSL ioctls: context creation (priority, preemption style, type), set-property, every command submission (command buffers, sizes); counts the rest. Works for Turnip and the vendor driver alike |
| `gb-vk.sh` | phone | Runs Geekbench 6's Vulkan test unattended (start it with `systemd-run --user`): `ICD=` picks a Turnip build or `vendor`, `STOP_AFTER=` ends after a workload, `TRACE=1` runs it under `kgsl-trace` |
| `preempt-test.sh` | phone | Measures whether a long Turnip job makes other GPU work wait: a compositor-like probe through the vendor driver every 50 ms, timed |
| `gl/gltest.c` | phone (build on the PC with `gl/build.sh`) | OpenGL through EGL as a game uses it: `headless` (context, strings, a triangle checked pixel by pixel, a compute shader checked) and `wayland` (a fullscreen window, a fixed number of frames); `--gles` for OpenGL ES; `--interval N` (swap interval), `--flush` (glFlush before each swap), `--limit FPS` (app-side frame limiter); the window mode prints the time between frames. The EGL vendor is chosen by the environment |
| `vkcompute/pp-batch.sh`, `pp-isam.sh`, `sm-time.sh` | phone | Stage 1.4: Geekbench's Particle Physics and Stereo Matching shaders replayed on several drivers (needs the shaders recorded by the layer in `dlog-all/`) |
| `ab-compute.sh`, `ab-present.sh`, `longrun.sh` | phone | Stage 1.4: route A (Android Turnip through libhybris) against route B (our Turnip): compute, presentation, and a long stability run (`longrun.sh A|B [minutes]`) |
| `win/wtest.c` | phone, through Wine (build on the PC with `win/build.sh`, llvm-mingw) | Stage 2.1: a Windows console test built for ARM64, ARM64EC, x86-64 and x86: what Windows reports, the x86 features the emulator offers, address space and threads, and three small benchmarks (integer, float, memory copy) |
| `win/vktest.c` | phone, through Wine (build with `win/build.sh`) | Stage 2.3: a Windows Vulkan program as a game uses Vulkan: Win32 window (windowed or fullscreen), swapchain, a moving bar, window cycles, frame-time statistics |
| `wine-m2.sh` | phone, screen on | Stage 2.3: `vktest.exe` windowed, fullscreen three times, with rotation, and in FIFO |
| `wine-m3.sh` | phone, screen on | Stage 2.3: rotation with patch 0003, portrait and landscape monitor |
| `vktest-app.sh`, `utdeck-vktest.desktop` | phone (`~/.local/share/applications`) | Stage 2.3: `vktest.exe` started as a Lomiri app, landscape only, as UTDeck's games will be |
| `win/d3d11test.c`, `win/d3d9test.c` | phone, through Wine | Stage 2.4: Direct3D 11 (shaders, adjustable GPU load) and Direct3D 9 samples, for DXVK against wined3d |
| `win/isstream.py`, `win/pecheck.py` | PC or phone | Stage 2.4: unpack InstallShield's appended file stream; tell native Windows programs from .NET ones |
| `wine-install.sh` | phone (user unit) | Stage 2.4: run a Windows installer in the test prefix with its errors logged |
| `3dmark11-app.sh`, `utdeck-3dmark11.desktop` | phone (`~/.local/share/applications`) | Stage 2.4: 3DMark 11 as a Lomiri app, with GPU and temperature sampling |
| `memgrep.py` | phone | Stage 2.4: search a running process's memory for a text (UTF-8 and UTF-16); read 3DMark 11's results from its stuck GUI |
| `win/gptest.c`, `gptest.sh` | phone, through Wine | Stage 2.4: a gamepad through Wine (XInput and winmm), with Wine's controller messages logged |
| `scaletest-app.sh`, `utdeck-scaletest.desktop` | phone (`~/.local/share/applications`) | Stage 2.4: a 1280x720 picture with fit, stretch and center (patch 0006) |
| `runtime-push.sh` | PC | Stage 3.1: copy the runtime from `tools/wine-build/build-runtime.sh` to the phone (`wine/runtime-<version>`, `wine/runtime` points at it), with `env.sh` and `prefix-dlls.sh` |
| `wine-env.sh` | phone (installed as `wine/env.sh`) | The environment for Wine on the phone; `UTDECK_RUNTIME=utdeck` (default) or `hangover` |
| `prefix-dlls.sh` | phone | Stage 3.1: put the runtime's DXVK and VKD3D-Proton into a prefix |
| `rt-console.sh` | phone (user unit) | Stage 3.1: stage 2.1's console tests in a fresh prefix, for one runtime (prefix time and size, four architectures, start-up, 7-Zip) |
| `rt-gfx.sh` | phone (user unit), screen may be off | Stage 3.1: Vulkan, D3D9, D3D11 (with mode changes), D3D12, sound and the gamepad list, for one runtime |
| `win/d3d12test.c` | phone, through Wine | Stage 3.1: Direct3D 12 through VKD3D-Proton: device features, a pipeline, a draw per frame |
| `win/sndtest.c` | phone, through Wine | Stage 3.1: the wave output devices and a quiet 440 Hz tone through `winepulse` |
| `win/vidtest.c` | phone, through Wine (`game-run.sh`) | Stage 3.S: play a video file through DirectShow (32-bit x86), with the default renderer or VMR-9; prints how it ended |
| `xwin.py` | phone | Stage 3.S: hidden Steam's X11 screen (`:50`): list the windows, `--shot` a picture, `--close` Steam's window (its CPU cost), `--fit` the windows to the screen, `--click X Y` (XTest) on a dialog the maintainer decided on, such as a publisher's agreement |
| `gamecheck.py` | phone (`python3 -I`) | Stage 3.2b: check a game folder before its first start: machine and bitness of every Windows file, the .exe imports (graphics API), Valve's signature on `steam_api`, DRM-bypass markers |
| `measure.sh` | phone | Stage 3.2b: frame rate, frame time, GPU busy and clock of the newest game run, over N seconds (`sh measure.sh 20`) |
| `watch-game.sh` | phone | Stage 3.2b: after a launch, every 30 s the frames drawn, lock waits and memory; stops on exit or after 2 minutes without a frame |
| `dmpinfo.py` | phone (`python3 -I`) | Stage 3.2b: a Windows minidump's exception, module and stack return addresses (dumps stay on the phone: they can hold account data) |
| `callsite.py` | phone (`python3 -I`) | Stage 3.2b: which imported function an x64 `call [import]` before a return address calls |
| `cputime.py` | phone | Wall and CPU time of a command with its output in a file (leaves out `wineserver`, which keeps pipes open) |
| `wine-m1.sh` | phone, screen on | Stage 2.2: Wine's Notepad and `winecfg` on Lomiri, with Wayland messages logged |
| `pacing.sh`, `pacing2.sh`, `pacing3.sh` | phone, screen on | Stage 1.4 frame pacing: `gltest` runs with `fps-panel.py` (panel refresh, compositor commits, GPU load); with `WAYLAND_DEBUG` time stamps (callback timing); the `--flush` and frame-limiter fixes |
| `dxvk-check.py` | PC | Compares a `vulkaninfo -j` dump with the features DXVK requires, read from DXVK's source |

Stage 0 also used `wayland-probe.py` from the Dolphin port (`dolphin-ut/ut/phone-debug/`), which
creates a Vulkan device and swapchain on a Wayland window. See `docs/ut-deck/stage0-probe.md`.
