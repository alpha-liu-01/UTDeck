# UTDeck probes

Small tools for probing an Ubuntu Touch phone over adb. Plain Python 3 with no extra modules,
because Ubuntu Touch has no compiler and few tools. Push them to a test folder on the phone
(`/home/phablet/utdeck-probe/`) and run them as `phablet`.

| Tool | Runs on | What it does |
| --- | --- | --- |
| `wlegl-probe.py` | phone | Asks the compositor for gralloc buffers over libhybris' `android_wlegl` (what a native Turnip needs to present on Lomiri) and checks them; `show` puts a test pattern on screen through one |
| `run-timed.py` | phone | Runs a command and prints wall time, CPU time, CPU load and frames per second |
| `hybris-barrier-hook.c` | phone (build on the PC with an arm64 GCC, e.g. in `dolphin-ut-cross`) | `LD_PRELOAD` library that gives an Android Turnip loaded through libhybris glibc's `pthread_barrier_*` (dolphin-ut's hook, for programs that cannot install it) |
| `dxvk-check.py` | PC | Compares a `vulkaninfo -j` dump with the features DXVK requires, read from DXVK's source |

Stage 0 also used `wayland-probe.py` from the Dolphin port (`dolphin-ut/ut/phone-debug/`), which
creates a Vulkan device and swapchain on a Wayland window. See `docs/ut-deck/stage0-probe.md`.
