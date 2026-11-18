# Stage 1.1: a Turnip of our own that presents on Lomiri

Started and closed 2026-11-18. Plan: [stage1-roadmap.md](stage1-roadmap.md). Goal: a reproducible build of Mesa
Turnip (glibc, KGSL) with the `android_wlegl` WSI patch, from this repository, and `vkcube` on
Lomiri's screen without CPU copies.

*WSI* (window system integration) is the part of a Vulkan driver that hands finished frames to the
compositor. Mesa's Wayland WSI normally shares frames through linux-dmabuf, which Lomiri does not
offer; without it, it either fails (`VK_ERROR_SURFACE_LOST_KHR`) or copies every frame through the
CPU into shared memory (`MESA_VK_WSI_DEBUG=sw`). Both seen in stage 0.

## Result at a glance

| Check | Result |
| --- | --- |
| Build from this repository | **Yes**: [`tools/mesa-build/build-turnip.sh`](../../tools/mesa-build/build-turnip.sh) in a container of its own ([`Dockerfile`](../../tools/mesa-build/Dockerfile)) |
| Driver | Mesa **26.2.5** + the Droidian patch (`panxiao81/debian-mesa` `adb4b85e82`), "Turnip Adreno (TM) 619", Vulkan **1.3.354** |
| Links | Only `libz`, `libzstd`, `libwayland-client`, `libstdc++`, `libm`, `libgcc_s`, `libc` (all on the phone); 15.6 MB |
| Wayland surface without debug variables | **Works** (stage 0's driver: `VK_ERROR_SURFACE_LOST_KHR`) |
| Presentation path | **`android_wlegl` gralloc buffers**, no `wl_shm` buffer at all (`WAYLAND_DEBUG`) |
| CPU per frame (`vkcube`, screen off) | **0.97 ms** in FIFO, **0.68 ms** in MAILBOX; stage 0's software path: 24.9 ms, and MAILBOX stalled |
| DXVK's requirements | 2.7.1: all 41; 3.1.1: same 2 missing as before (8- and 16-bit storage) |
| Reproducible | Same source and options from a clean clone; not bit-for-bit (1,177 bytes of generated code differ); the clean build is the tested one |
| On screen (portrait, landscape, smoothness) | **Pass** (M1): whole in portrait and in landscape, smooth, MAILBOX moves. FIFO at 68.9 fps on the 120 Hz panel, MAILBOX at 127.1 fps |

## The build

| File | What it is |
| --- | --- |
| [`tools/mesa-build/Dockerfile`](../../tools/mesa-build/Dockerfile) | amd64 Ubuntu 24.04 with arm64 multiarch from the phone's archives (noble + UBports 24.04-2.x, the same setup as dolphin-ut's `ut/docker`). Image `utdeck-mesa-cross` |
| [`tools/mesa-build/cross-aarch64.ini`](../../tools/mesa-build/cross-aarch64.ini) | Meson cross file |
| [`tools/mesa-build/build-turnip.sh`](../../tools/mesa-build/build-turnip.sh) | Clones the pinned source into `build/mesa-src` (or builds `SRC=...`), configures, builds only `libvulkan_freedreno.so`, strips it, writes `build/turnip/` with an ICD file for the phone and `BUILD-INFO` |
| `tools/mesa-build/ubports.gpg` | UBports' archive key (from dolphin-ut) |

**Reproducibility.** A second build from a clean clone (`tools/mesa-build/build-turnip.sh` with no
`SRC`, so it cloned `build/mesa-src` itself) worked, but the binary is not bit-for-bit the same:
`150e4cae...0c287` against the first build's `69c1cb56...e4f8`. 1,177 bytes differ out of 15.6 MB,
every text string is identical (version, commit, driver info), and only the build IDs and some
generated code differ, most likely the order in which Mesa's Python generators emit tables. Not
needed now; if it is wanted later, try a fixed `PYTHONHASHSEED`. **The tested driver is the
clean-clone build** (`150e4cae`): it was pushed to the phone and the checks below repeated with it.

Source: `panxiao81/debian-mesa`, branch `droidian`, commit `adb4b85e82`. It is a Debian packaging
tree of Mesa 26.2.5 whose last commit is the patch "vulkan/wsi/wayland: present through
android_wlegl gralloc buffers". Building that exact tree means building the code the patch's author
tested (on an Adreno 650 under phoc), before porting it to Mesa main.

Meson options, following Droid-Deck/Drivers' `build_linux.sh` but narrower:
`-Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Dgallium-drivers= -Dplatforms=wayland
-Dopengl=false -Dgbm=disabled -Dglx=disabled -Degl=disabled -Dllvm=disabled -Dvulkan-layers=
-Dtools= -Dvideo-codecs= -Dxmlconfig=disabled --buildtype release -Db_ndebug=true
--wrap-mode=nofallback`. Wayland only (Lomiri has no GPU path for X11 clients) and KGSL only (the
phone has no `msm` GPU node). Build time: about 1 minute for the 715 build steps.

What the container needed, found by failing builds:

| Failure | Cause | Fix |
| --- | --- | --- |
| `meson` missing | not in Dolphin's image; Mesa 26.2 needs meson 1.4 or later, noble has 1.3.2 | meson 1.6.1 from pip, in a venv |
| `Program 'glslangValidator' not found` | Mesa 26.2 compiles its ray-tracing BVH shaders with it, also for Turnip | `glslang-tools` (15.1.0) |
| "Compiler for language c for the build machine not found" | a few Mesa helpers run on the build machine | `build-essential` |
| `Unhandled python OSError` at `libarchive` | meson unpacks a fallback copy into the source tree, which is mounted read-only; libarchive only serves freedreno's debug tools | `--wrap-mode=nofallback` (`nodownload` was not enough) |
| wayland-protocols too old | Mesa 26.2 asks for 1.41; noble has 1.34 | UBports' repository has 1.45 (same as the phone's archive setup) |

## On the phone

The driver lives in `/home/phablet/utdeck-probe/turnip-wlegl/` and is chosen with
`VK_DRIVER_FILES=.../turnip_icd.json`, with Ubuntu's Khronos loader from stage 0 first in
`LD_LIBRARY_PATH`. Nothing system-wide.

### The surface

`vulkaninfo` with no debug variables: formats R8G8B8A8 and B8G8R8A8 (UNORM and SRGB), present modes
MAILBOX and FIFO, `currentExtent` 0xFFFFFFFF (correct), `minImageCount` 4. Stage 0's DD-Turnip
failed here with `VK_ERROR_SURFACE_LOST_KHR`.

### What goes over the wire

`WAYLAND_DEBUG=client vkcube-wayland --c 120`, screen off:

- 3 rounds of `android_wlegl.get_server_buffer_handle`: 500x500 twice (vkcube's first size), then
  **1080x2305**, the window Lomiri configured. Format 2 (RGBX_8888), usage 768 (`0x300`, GPU render
  + texture). 12 buffers in all (4 per swapchain).
- Lomiri's replies: server-created `wl_buffer`s (ids from `0xff000000`), stride 1088 for the
  1080-wide buffers: the same padded layout that tore in portrait through shared memory in stage 0,
  but here the stride travels with the gralloc buffer, as in stage 0's M1.
- **No `wl_shm_pool.create_buffer` at all**: no CPU-copied frames.
- 121 `wl_surface.commit`, 117 `wl_buffer.release`: frames flow and come back. Exit 0.

### What it costs

`run-timed.py` on `vkcube-wayland --c 600`, screen off:

| Driver and path | FIFO | MAILBOX |
| --- | --- | --- |
| DD-Turnip, `MESA_VK_WSI_DEBUG=sw` (stage 0) | 30.0 fps, 24.9 ms CPU per frame, 75 % of a core, 71 MB | stalled |
| **Our Turnip, `android_wlegl`** | 600 frames in 8.74 s = 68.7 fps, **0.97 ms CPU per frame**, 7 % of a core, 13 MB | 600 frames in 3.89 s = **154.3 fps**, **0.68 ms**, 11 % of a core |
| Our Turnip, clean-clone build (`150e4cae`) | 70.7 fps, **0.93 ms** | 158.5 fps, **0.66 ms** |

With the screen off, Lomiri paces frames differently (stage 0), so the frame rates are not the real
ones; the CPU cost per frame is. It fell by a factor of about 26. The patch still waits on the CPU
for the GPU's copy into the gralloc buffer before each present (KGSL attaches no fences to
dma-bufs), and that wait is inside the 0.97 ms.

No KGSL messages in the kernel log during these runs.

### DXVK

`dxvk-check.py` on this build's `vulkaninfo -j`: DXVK 2.7.1, all 41 required entries; DXVK 3.1.1,
missing `storageBuffer16BitAccess` and `storageBuffer8BitAccess`, as with stage 0's builds. These
come from the hardware, not the build.

## Manual test (screen on and unlocked)

**M1. Our Turnip on screen, portrait and landscape.** In a new `adb shell`:
```
cd /home/phablet/utdeck-probe && unset MESA_VK_WSI_DEBUG LD_PRELOAD GLIBC_TUNABLES
export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0 LD_LIBRARY_PATH=$PWD/vk/lib VK_DRIVER_FILES=$PWD/turnip-wlegl/turnip_icd.json
python3 run-timed.py --frames 1200 -- timeout 120 vk/bin/vkcube-wayland --c 1200
python3 run-timed.py --frames 1200 -- timeout 120 vk/bin/vkcube-wayland --c 1200 --present_mode 1
```
Each run is about 10-20 s. During each run, turn the phone from portrait to landscape and back (rotation lock
off). The first output line must say "Turnip Adreno (TM) 619". Report: the two result lines; whether
the cube is whole (not torn) in portrait and in landscape; whether it looked smooth; whether the
MAILBOX run moves.

**Result (run by the maintainer, 2026-11-18): pass.**

| Run | Output |
| --- | --- |
| FIFO | "Turnip Adreno (TM) 619"; exit 0, 1200 frames in 17.41 s = **68.9 fps**, 1.15 s of CPU = **0.96 ms per frame**, 7 % of one core, 13 MB |
| MAILBOX | exit 0, 1200 frames in 9.44 s = **127.1 fps**, 0.86 s of CPU = **0.72 ms per frame**, 9 % of one core |

In the maintainer's words: "Both runs move, both are smooth, smoother than last time. In both
portrait and landscape, the cube is whole and spinning."

- **No tear in portrait.** Same 1088-pixel stride as stage 0's torn software path, but here the
  stride travels with the gralloc buffer, so Lomiri reads it right. This confirms stage 0's reading
  of the tear.
- **MAILBOX works** with the screen on too (it stalled with the software path).
- The CPU cost with the screen on matches the screen-off runs (0.96 / 0.72 ms against 0.93 / 0.66).
- **FIFO does not follow the panel.** 68.9 fps on a 120 Hz panel is neither 120 nor 60. So in FIFO
  the pace comes from when Lomiri releases buffers and sends frame callbacks (it composes twice:
  Lomiri, then `lomiri-system-compositor`), or from the patch's CPU wait before each present, not
  from the display's refresh. Dolphin saw the same kind of thing (83-87 fps with OpenGL ES at swap
  interval 1, dolphin-ut stage 0). Not a problem for `vkcube`; for games it decides frame pacing,
  so it goes to 1.4 (measure with stone-ut's `fps-panel.py`, which counts the panel's real refreshes).

## Still open in 1.1

- ~~The manual test above~~: passed.
- FIFO pacing at 68.9 fps on the 120 Hz panel (moved to 1.4).
- The patch's CPU wait before each present: whether it limits frame rate in heavier scenes (stage 2
  and 1.4 will show).

## Problems met

- Four container gaps, found one failed build at a time (table under "The build").
- `| tail` hid the build script's exit status in one run; the script itself stops on the first
  error (`set -eu`). Check `BUILD-INFO`, not only the tail of the log.

## Events

- 2026-11-18: source check (DroidDeck UI and store commits, a gamesir-linux-tools commit: nothing
  for UTDeck). Stage 1 split into 1.1-1.4. DroidDeck's `linux_kgsl_wsi.py` read: it adds KGSL device
  numbers for linux-dmabuf feedback, so it does not help on Lomiri; the Droidian patch is the one we
  need. `tools/mesa-build/` written; four container fixes; Mesa 26.2.5 + `adb4b85` built (SHA-256
  `69c1cb56...e4f8`, then `150e4cae...0c287` from a clean clone). On the phone: the surface works without debug variables, `vkcube` presents
  through `android_wlegl` with no `wl_shm` buffers, CPU per frame 0.97 ms (FIFO) and 0.68 ms (MAILBOX,
  which no longer stalls). DXVK check unchanged. Manual test M1 written.
  Clean-clone rebuild: same source, not bit-for-bit identical (1,177 bytes), retested on the phone
  with the same results (`android_wlegl` buffers, 0.93 / 0.66 ms per frame).
- 2026-11-18, later: M1 passed (maintainer): whole picture in portrait and landscape, smooth, MAILBOX
  moves; FIFO 68.9 fps, MAILBOX 127.1 fps, under 1 ms of CPU per frame. 1.1 closed; FIFO pacing
  moved to 1.4.

## Sources

- The patch: https://github.com/panxiao81/debian-mesa/commit/adb4b85e82
- DroidDeck's Linux build script and KGSL WSI change: https://github.com/Droid-Deck/Drivers
  (`scripts/build_linux.sh`, `patches/scripts/linux_kgsl_wsi.py`)
- Mesa's build requirements: `meson.build` of the source tree (meson 1.4, wayland-protocols 1.41,
  glslangValidator)
