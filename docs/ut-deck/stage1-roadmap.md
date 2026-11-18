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
| 1.4 Decision and packaging | Route A against route B on the same tests (DXVK's feature check, frame pacing including 1.1's FIFO at 68.9 fps and 1.3's zink at about half the vendor GLES frame rate (the presentation copy and CPU wait), Turnip's cost per submission, Particle Physics at 59 % of the vendor driver, Stereo Matching's factor of 7.6 to verify, MAILBOX, long run); the driver chosen per app | A written decision; the driver folder layout UTDeck will ship |

Order: 1.1 first, because everything later (1.3, and stage 2's Wine) presents through it. 1.2 can
run in parallel once 1.1 has a build of our own to change.

## Status

- **1.1 (2026-11-18): done.** Notes:
  [stage1.1-turnip-wlegl.md](stage1.1-turnip-wlegl.md). Mesa 26.2.5 with the Droidian patch,
  built by `tools/mesa-build/build-turnip.sh`, presents on Lomiri through `android_wlegl` gralloc
  buffers with no CPU copies: 0.93 ms of CPU per frame instead of 24.9 ms, and MAILBOX works.
  On screen (M1): whole in portrait and landscape, smooth. Carried to 1.4: FIFO runs at 68.9 fps
  on the 120 Hz panel, so Lomiri's timing, not the panel, paces it.
- **1.2 (2026-11-18): done.** Notes: [stage1.2-turnip-compute.md](stage1.2-turnip-compute.md).
  The frozen UI: one shared KGSL queue; fixed by patch 0001 (Turnip passes Vulkan's queue priority
  to KGSL; a LOW game no longer blocks the compositor). The GPU timeout: several command buffers in
  one KGSL submission; fixed by patch 0002 (one submission per command buffer). The wrong results:
  Geekbench's tuner uses 2048x1x1 work-groups, over `maxComputeWorkGroupSize[0]`, which Turnip's
  2048-invocation limit lets it try; not a Turnip bug (`tu_restrict_subgroup_size_64=true` makes
  every workload validate). Both patches are in the default build.
- **1.3 (2026-11-18): done.** Notes: [stage1.3-zink.md](stage1.3-zink.md).
  zink on our Turnip gives desktop OpenGL 4.6 core and GLES 3.2, correct headless, and a window on
  Lomiri through `android_wlegl` (no CPU copies). Needed our patch 0003 (EGL kept dropping the
  zink request on a GPU without a DRM render node). On screen (M1): picture right; zink 42 fps
  against the vendor GLES driver's 82 fps on a full-screen test, about 1 ms more CPU per frame
  (carried to 1.4).
- 1.4: not started.

Rules for this stage, from the project rules:
- Nothing system-wide on the phone: the driver lives in a test folder and is chosen with
  `VK_DRIVER_FILES`, as in stage 0.
- The GPU's limits stay as the vendor set them (KGSL's 2 s timeout, clocks).
- Imported patches keep their author and stay separate from ours, so they can be replaced by an
  upstream version later.
