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
| `cputime.py` | phone | Wall and CPU time of a command with its output in a file (leaves out `wineserver`, which keeps pipes open) |
| `wine-m1.sh` | phone, screen on | Stage 2.2: Wine's Notepad and `winecfg` on Lomiri, with Wayland messages logged |
| `pacing.sh`, `pacing2.sh`, `pacing3.sh` | phone, screen on | Stage 1.4 frame pacing: `gltest` runs with `fps-panel.py` (panel refresh, compositor commits, GPU load); with `WAYLAND_DEBUG` time stamps (callback timing); the `--flush` and frame-limiter fixes |
| `dxvk-check.py` | PC | Compares a `vulkaninfo -j` dump with the features DXVK requires, read from DXVK's source |

Stage 0 also used `wayland-probe.py` from the Dolphin port (`dolphin-ut/ut/phone-debug/`), which
creates a Vulkan device and swapchain on a Wayland window. See `docs/ut-deck/stage0-probe.md`.
