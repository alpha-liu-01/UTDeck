# UTDeck probes

Small tools for probing an Ubuntu Touch phone over adb. Plain Python 3 with no extra modules,
because Ubuntu Touch has no compiler and few tools. Push them to a test folder on the phone
(`/home/phablet/utdeck-probe/`) and run them as `phablet`.

| Tool | Runs on | What it does |
| --- | --- | --- |
| `wlegl-probe.py` | phone | Asks the compositor for gralloc buffers over libhybris' `android_wlegl` (what a native Turnip needs to present on Lomiri) and checks them; `show` puts a test pattern on screen through one |
| `run-timed.py` | phone | Runs a command and prints wall time, CPU time, CPU load and frames per second |
| `hybris-barrier-hook.c` | phone (build on the PC with an arm64 GCC, e.g. in `dolphin-ut-cross`) | `LD_PRELOAD` library that gives an Android Turnip loaded through libhybris glibc's `pthread_barrier_*` (dolphin-ut's hook, for programs that cannot install it) |
| `vkcompute/` | phone (build on the PC with `vkcompute/build.sh`) | Small Vulkan compute tests of our own: `vkc.h` (harness), `intops` (signed division, modulo and multiply against a CPU reference), `spvrun` (runs any compute shader with given specialization constants, push constants and buffers from a fixed seed; times and dumps it) and `spvcmp.py` (PC: compares two `spvrun` dumps), `wgsize.comp` (does every invocation of a work-group run, for any shape), and `layer/` (`VK_LAYER_UTDECK_dispatchlog`: records every compute dispatch with its shader, specialization constants, push constants and buffers, for replay in `spvrun`). Driver chosen like any Vulkan program (`VK_DRIVER_FILES` for Turnip, nothing for the vendor driver through libhybris) |
| `vkcompute/kgsl-trace.c` | phone (built by `vkcompute/build.sh`) | Runs a program under `ptrace` and decodes its KGSL ioctls: context creation (priority, preemption style, type), set-property, every command submission (command buffers, sizes); counts the rest. Works for Turnip and the vendor driver alike |
| `gb-vk.sh` | phone | Runs Geekbench 6's Vulkan test unattended (start it with `systemd-run --user`): `ICD=` picks a Turnip build or `vendor`, `STOP_AFTER=` ends after a workload, `TRACE=1` runs it under `kgsl-trace` |
| `preempt-test.sh` | phone | Measures whether a long Turnip job makes other GPU work wait: a compositor-like probe through the vendor driver every 50 ms, timed |
| `gl/gltest.c` | phone (build on the PC with `gl/build.sh`) | OpenGL through EGL as a game uses it: `headless` (context, strings, a triangle checked pixel by pixel, a compute shader checked) and `wayland` (a fullscreen window, a fixed number of frames); `--gles` for OpenGL ES. The EGL vendor is chosen by the environment |
| `dxvk-check.py` | PC | Compares a `vulkaninfo -j` dump with the features DXVK requires, read from DXVK's source |

Stage 0 also used `wayland-probe.py` from the Dolphin port (`dolphin-ut/ut/phone-debug/`), which
creates a Vulkan device and swapchain on a Wayland window. See `docs/ut-deck/stage0-probe.md`.
