# Stage 1: Vulkan 1.3 on Lomiri, the plan

Written 2026-11-18, after [stage 0](stage0-probe.md). Goal (from
[feasibility-and-roadmap.md](feasibility-and-roadmap.md)): a modern Vulkan driver drawing on Lomiri's
screen, chosen per app, with numbers to decide between route A (Android Turnip through libhybris)
and route B (native glibc Turnip).

Stage 0 settled part of the question already: native Turnip loads and creates a device on stone's
kernel, and Lomiri hands out the gralloc buffers the Droidian `android_wlegl` patch needs (and shows
them, M1). It also added two problems: both Turnips fail Geekbench's Vulkan compute test on the
Adreno 619, and Mesa's software presentation is torn in portrait. Stage 1 is too large for one go,
so it is split.

| Substage | Goal | Exit |
| --- | --- | --- |
| **1.1 A Turnip of our own that presents on Lomiri** | A reproducible build of Mesa Turnip (glibc, KGSL) with the `android_wlegl` WSI patch, from this repository; `vkcube` on screen without CPU copies | `vkcube` on Lomiri through `android_wlegl` buffers (checked with `WAYLAND_DEBUG`), portrait and landscape, with CPU per frame and fps measured against stage 0's software path |
| 1.2 Turnip's compute failure | Find the shader and dispatch size behind stage 0's wrong result and GPU timeout; try `TU_DEBUG` options and Mesa main | A small reproducer of our own; a fix, a workaround, or an upstream report |
| 1.3 OpenGL through zink | zink (OpenGL on Vulkan) on our Turnip, presenting on Lomiri | An OpenGL 4.x context and a window on screen, or the reason it cannot work |
| 1.4 Decision and packaging | Route A against route B on the same tests (DXVK's feature check, frame pacing including 1.1's FIFO at 68.9 fps, MAILBOX, long run); the driver chosen per app | A written decision; the driver folder layout UTDeck will ship |

Order: 1.1 first, because everything later (1.3, and stage 2's Wine) presents through it. 1.2 can
run in parallel once 1.1 has a build of our own to change.

## Status

- **1.1 (2026-11-18): done.** Notes:
  [stage1.1-turnip-wlegl.md](stage1.1-turnip-wlegl.md). Mesa 26.2.5 with the Droidian patch,
  built by `tools/mesa-build/build-turnip.sh`, presents on Lomiri through `android_wlegl` gralloc
  buffers with no CPU copies: 0.93 ms of CPU per frame instead of 24.9 ms, and MAILBOX works.
  On screen (M1): whole in portrait and landscape, smooth. Carried to 1.4: FIFO runs at 68.9 fps
  on the 120 Hz panel, so Lomiri's timing, not the panel, paces it.
- 1.2-1.4: not started.

Rules for this stage, from the project rules:
- Nothing system-wide on the phone: the driver lives in a test folder and is chosen with
  `VK_DRIVER_FILES`, as in stage 0.
- The GPU's limits stay as the vendor set them (KGSL's 2 s timeout, clocks).
- Imported patches keep their author and stay separate from ours, so they can be replaced by an
  upstream version later.
