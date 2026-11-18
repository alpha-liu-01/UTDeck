# Stage 1.3: OpenGL through zink

Started and closed 2026-11-18. Plan: [stage1-roadmap.md](stage1-roadmap.md). Goal: zink (Mesa's OpenGL driver
on top of Vulkan) on our Turnip, presenting on Lomiri: an OpenGL 4.x context and a window on screen,
or the reason it cannot work.

Why it matters: the vendor driver offers only OpenGL ES (stone-ut item 10: no desktop OpenGL in
hardware). Desktop OpenGL is what Wine's own Direct3D (WineD3D) and OpenGL games need, and what
some emulators and ports use.

## Result at a glance

| Check | Result |
| --- | --- |
| Build | **Yes**: [`tools/mesa-build/build-zink.sh`](../../tools/mesa-build/build-zink.sh): Mesa 26.2.5 with zink, EGL (as a glvnd vendor library) and Turnip, from the same pinned tree and our patches |
| Context | **OpenGL 4.6 core** ("zink Vulkan 1.3 (Turnip Adreno (TM) 619)", GLSL 4.60) and **OpenGL ES 3.2** |
| Rendering, headless | A triangle in an FBO correct to the pixel; a compute shader's 65,536 values correct; no GL errors (desktop GL and GLES) |
| Window on Lomiri | **Yes**: desktop GL 4.6 and GLES 3.2 in a fullscreen 1080x2400 xdg window, presented through `android_wlegl` gralloc buffers (no `wl_shm` buffers, no CPU copies) |
| What it needed | **Our patch 0003**: Mesa's EGL dropped an explicit zink request when the GPU has no DRM render node (KGSL has none) |
| Cost (screen off) | 3.2 ms CPU per frame, about 45 fps; the vendor GLES driver in the same window: 1.9 ms, 79 fps |
| On screen | **Pass** (M1): picture right; zink 41.8 fps (desktop GL) and 42.5 fps (GLES) against the vendor GLES driver's 81.9 fps on a full-screen test |

## The build

[`build-zink.sh`](../../tools/mesa-build/build-zink.sh) uses the source tree `build-turnip.sh`
cloned (`build/mesa-src`, the same pinned commit), applies `tools/mesa-build/patches/*.patch`, and
installs Mesa into `build/zink/stage/`. Options: `-Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl
-Dgallium-drivers=zink -Dplatforms=wayland -Degl=enabled -Dglvnd=enabled -Dopengl=true -Dgles2=enabled
-Dgles1=disabled -Dglx=disabled -Dgbm=disabled -Dllvm=disabled -Dxmlconfig=disabled`, release.
Result: `libEGL_mesa.so.0` (388 KB, a glvnd EGL vendor), `libgallium-26.2.5.so` (20 MB, holds zink
and the GL state tracker), `libvulkan_freedreno.so`. Everything they link is on the phone.

Container additions (`tools/mesa-build/Dockerfile`): `libglvnd-dev:arm64` and
`libwayland-egl-backend-dev:arm64` (the first configure failed on `wayland-egl-backend`).

On the phone: `/home/phablet/utdeck-probe/zink/` with `lib/`, a glvnd vendor file
`egl_vendor.json`, `turnip_icd.json`, and `env.sh`:
`__EGL_VENDOR_LIBRARY_FILENAMES` (our EGL for this program only, through the system's
`libEGL.so.1`), `LD_LIBRARY_PATH`, `VK_DRIVER_FILES`, `MESA_LOADER_DRIVER_OVERRIDE=zink`. Lomiri
and other apps keep the system's EGL.

## The test program

[`tools/ut-probe/gl/gltest.c`](../../tools/ut-probe/gl/gltest.c) (built by `gl/build.sh`):
- `gltest headless [--gles]`: EGL's surfaceless platform; an OpenGL 4.6 core context (falling back
  to 4.5, 4.3, 3.3) or OpenGL ES 3.2; prints the strings; draws a full-screen triangle colored by
  position into a 256x128 FBO and checks every pixel; runs a compute shader writing i*i+7 into
  65,536 slots and checks them.
- `gltest wayland [FRAMES] [--gles]`: a fullscreen xdg window with `wl_egl_window`, swap interval
  1, the same triangle with a pulsing blue channel, FRAMES frames.

GL functions are loaded through `eglGetProcAddress`, so one program drives both APIs and any EGL
vendor.

## What blocked it: EGL without a DRM render node

First run: `MESA-EGL: warning: egl: failed to create dri2 screen`, `EGL_NOT_INITIALIZED`. From
Mesa's source (`src/egl/main/eglapi.c`, `platform_surfaceless.c`, `platform_wayland.c`,
`src/gallium/drivers/zink/zink_screen.c`):

- EGL's normal paths look for a DRM device. The phone's only DRM nodes (`card0`, `renderD128`)
  belong to the display controller, not the GPU; the GPU is reachable only through KGSL.
- The path without DRM is EGL's "software" branch, which can load zink and presents through
  Vulkan WSI (kopper): exactly what we want. But when the normal paths fail, `eglInitialize` sets
  `Options.Zink = false` before `ForceSoftware = true`, so it loads `swrast` (not built), even when
  `MESA_LOADER_DRIVER_OVERRIDE=zink` asked for zink.
- Forcing the branch with `LIBGL_ALWAYS_SOFTWARE=1` does not help: zink reads that variable as
  "use a CPU Vulkan device" and stops with "ZINK: CPU device requested but none found!".
- Without that variable and without a DRM device, zink simply takes the first Vulkan device, our
  Turnip (`choose_pdev`).

**Patch 0003**,
[`0003-egl-keep-zink-request-without-drm-device.patch`](../../tools/mesa-build/patches/0003-egl-keep-zink-request-without-drm-device.patch):
in that fallback, keep `Options.Zink` when `MESA_LOADER_DRIVER_OVERRIDE=zink` asked for it. Six
lines; an upstream candidate (it serves any GPU without a DRM render node, Android's KGSL included).
`build-turnip.sh` and `build-zink.sh` apply it by default with 0001 and 0002 (it does not touch
Turnip).

## Runs (2026-11-18)

Headless, our zink:

| API | Context | Renderer | Triangle | Compute |
| --- | --- | --- | --- | --- |
| desktop OpenGL | 4.6 core, GLSL 4.60 | zink Vulkan 1.3 (Turnip Adreno (TM) 619 (MESA_TURNIP)), Mesa 26.2.5 | 0 of 32,768 pixels wrong | 0 of 65,536 wrong |
| OpenGL ES | 3.2 | same | 0 wrong | 0 wrong |

Window on Lomiri, `WAYLAND_DEBUG=client`, desktop GL, 120 frames, screen off: window 1080x2400,
120 frames presented, no GL errors; 8 `android_wlegl.get_server_buffer_handle` requests
(1080x2400, format 5 = BGRA_8888, usage `0x300`), **0 `wl_shm` buffers**, 121 commits. So zink's
kopper presents through Turnip's patched Wayland WSI with no CPU copies.

Cost, `run-timed.py`, 600 frames, screen off (the frame rates with the screen off are not the real
ones, stage 0; CPU per frame is):

| Path | fps | CPU per frame | Memory |
| --- | --- | --- | --- |
| zink, desktop GL 4.6 | 44.8 | 3.21 ms | 33 MB |
| zink, GLES 3.2 | 44.9 | 3.26 ms | 32 MB |
| vendor V@530 GLES 3.2 through libhybris (the system default) | 79.1 | 1.89 ms | 36 MB |

zink costs about 1.3-1.4 ms more CPU per frame than the vendor GLES driver here (it translates
GL to Vulkan, then presents through a blit into the gralloc buffer), and the screen-off frame rate
is lower; the on-screen test tells the real rate. For GLES apps the vendor driver stays the
lighter path; zink's place is desktop OpenGL, which the vendor driver does not have.

Side note: with the system's default EGL, the headless (surfaceless) test lands on the system Mesa's
**llvmpipe**, i.e. software rendering: glvnd falls through to Mesa because the hybris vendor has no
surfaceless platform. Only the window path reaches the Adreno GLES driver.

## Manual test (screen on and unlocked)

**M1. zink on screen.** In a new `adb shell`:
```
cd /home/phablet/utdeck-probe/zink && . ./env.sh && export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
python3 run-timed.py --frames 1200 -- timeout 120 ./gltest wayland 1200
python3 run-timed.py --frames 1200 -- timeout 120 ./gltest wayland 1200 --gles
```
Then the vendor driver for comparison, in a **new** `adb shell`:
```
cd /home/phablet/utdeck-probe/zink && export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0 GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so
python3 run-timed.py --frames 1200 -- timeout 120 ./gltest wayland 1200 --gles
```
Expected each time: a fullscreen gradient, red growing from left to right, green from bottom to
top, blue pulsing about every 2 seconds. The first output line names the renderer ("zink ...
Turnip" for the first two runs, "Adreno (TM) 619" for the last). Report the three result lines,
whether the picture looked right and smooth, and anything odd when turning the phone.


**Result (run by the maintainer, 2026-11-18): pass.** "I saw exactly what's expected from these
tests": the gradient, the pulsing blue, in all three runs.

| Run | Renderer | Frames | fps | CPU per frame |
| --- | --- | --- | --- | --- |
| zink, desktop GL | OpenGL 4.6 core, Mesa 26.2.5 | 1,200 in 28.71 s, no GL errors | **41.8** | 2.76 ms (12 % of a core) |
| zink, GLES | OpenGL ES 3.2, Mesa 26.2.5 | 1,200 in 28.25 s, no GL errors | **42.5** | 2.66 ms (11 %) |
| vendor, GLES (libhybris) | OpenGL ES 3.2 V@0530.0 | 1,200 in 14.65 s, no GL errors | **81.9** | 1.73 ms (14 %) |

- On screen, **zink reaches about half the vendor driver's frame rate** on this test (a single
  full-screen triangle at 1080x2400, swap interval 1), and uses about 1 ms more CPU per frame.
- Likely cause, to measure in 1.4, not assumed: the presentation path. The `android_wlegl` WSI renders
  each frame into a private image, copies the whole frame on the GPU into the compositor's gralloc
  buffer, and waits on the CPU for that copy before presenting (KGSL attaches no fences to dma-bufs).
  With almost no work per frame, that copy and wait dominate. Stage 1.1's `vkcube` (the same path)
  ran FIFO at 68.9 fps; the vendor driver's own presentation has no extra copy.
- Desktop GL and GLES cost the same through zink: the API makes no difference, the path does.

## Still open in 1.3

- ~~The manual test above~~: passed.
- zink at about half the vendor driver's frame rate on screen, and about 1 ms more CPU per frame:
  for 1.4, with the presentation path's copy and CPU wait, and Turnip's cost per submission.

## Problems met

- `wayland-egl-backend` missing in the container: added `libwayland-egl-backend-dev:arm64`.
- EGL dropped the zink request without a DRM device: patch 0003 (above).
- `LIBGL_ALWAYS_SOFTWARE=1` sends zink looking for a CPU Vulkan device: not a way around it.

## Events

- 2026-11-18: build script and container additions; first build; `gltest` written; headless test
  failed (no DRM render node); EGL and zink source read; patch 0003 written and built; desktop
  OpenGL 4.6 and GLES 3.2 pass headless; a desktop GL window presents on Lomiri through
  `android_wlegl`; cost measured against the vendor GLES driver. Manual test M1 written.
- 2026-11-18, later: M1 passed (maintainer); on screen zink runs at 41.8 / 42.5 fps against the
  vendor GLES driver's 81.9 fps. 1.3 closed; the gap moved to 1.4.

## Sources

- Mesa 26.2.5 source in `build/mesa-src` (pinned commit `adb4b85e82`): `src/egl/main/eglapi.c`
  (`eglInitialize`'s fallback), `src/egl/drivers/dri2/platform_surfaceless.c`,
  `src/egl/drivers/dri2/platform_wayland.c` (`dri2_initialize_wayland_swrast`, kopper),
  `src/gallium/drivers/zink/zink_screen.c` (`choose_pdev`).
