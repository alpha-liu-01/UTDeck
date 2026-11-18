# Stage 1.4: frame pacing, the open measurements, and the driver decision

Started 2026-11-18. Plan: [stage1-roadmap.md](stage1-roadmap.md). Goal: route A (Android Turnip
through libhybris) against route B (our glibc Turnip) on the same tests, the questions carried from
1.1-1.3, and the driver layout UTDeck will ship.

## Steps

Ordered so that each result can change the next:

| Step | Question | Status |
| --- | --- | --- |
| 1 | Turnip's cost per submission (1.2 measured 5-8 ms per submit-and-wait against 1.7-3.5 ms on the vendor driver): is it real, and does it explain the frame-pacing gap? | **Done: it was a measurement artifact** (below) |
| 2 | Frame pacing on screen: `vkcube` FIFO at 68.9 fps on the 120 Hz panel (1.1); zink at 42 fps against the vendor GLES driver's 82 fps (1.3); the vendor GLES driver itself at 82, not 120 | **Done: run games without vsync waiting, with a frame limiter** (60 fps steady); conclusion below |
| 3 | The two Geekbench anomalies: Particle Physics at 59 % of the vendor driver, Stereo Matching at 7.6 times | **Done: Particle Physics is a real compiler loss (ir3), Stereo Matching a real win** (below) |
| 4 | Route A against route B (DXVK's feature check on both, a long run), the written decision, and the driver folder layout | **Done: route B** (below) |

## Step 1: Turnip's cost per submission

[`spvrun --profile`](../../tools/ut-probe/vkcompute/spvrun.c) (new) splits each submit-and-wait:
the time in `vkQueueSubmit`, the time waiting for the fence, and the GPU's own time between two
timestamps written around the dispatch. How Turnip waits on KGSL (`tu_knl_kgsl.cc`):
`IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID`, which in stone's kernel (`adreno_drawctxt.c`) is a
`wait_event_interruptible_timeout` woken by the GPU's interrupt: no polling, no fixed delay.

A tiny dispatch (Geekbench's softmax, 1,000 elements), 15 times, our current build
(`turnip-utdeck`) against the vendor driver; the steady-state dispatches (11-14):

| | Back to back | 50 ms apart (like frames) |
| --- | --- | --- |
| Turnip | 0.21 ms (submit 0.02, wait 0.19; GPU 0.008) | 0.27-0.38 ms (submit 0.19-0.27, wait 0.08-0.11) |
| Vendor V@530 | 0.15-0.16 ms (submit 0.02, wait 0.14; GPU 0.007) | 0.48-0.54 ms (submit 0.43-0.48, wait 0.06) |

Only the first dispatch after the GPU has been idle is slow, on both drivers, and by a varying
amount (GPU power-up): in one three-dispatch run, 6.7 ms on stage 1.1's build, 0.8 ms on the current
build, 4.2 ms on the vendor driver; the worst of 15 in the runs above: 4.5 ms (Turnip), 1.6-2.4 ms
(vendor).

**So the "5-8 ms per submission" of stage 1.2 was a measurement artifact**: those were "best of 3"
from short runs that never got past this power-up phase. In steady state a submit-and-wait costs
Turnip about what it costs the vendor driver (0.2-0.4 ms against 0.15-0.5 ms). It does not explain
the frame-pacing gap; step 2 measures that directly. Corrected in
[stage1.2-turnip-compute.md](stage1.2-turnip-compute.md) and the stage 1 plan.

Problems met: `--profile` first crashed (SIGSEGV): the new query-pool field of the harness context
was never initialized; `vkc_init` now zeroes the context.

## Step 2: frame pacing on screen

What is known: on screen, `vkcube` on our Turnip ran FIFO at 68.9 fps and MAILBOX at 127.1 fps
(1.1); `gltest` ran at 41.8-42.5 fps through zink and 81.9 fps on the vendor GLES driver (1.3); the
panel runs at 120 Hz. With the screen off, Lomiri paces differently (stage 0), so this step needs
the screen on.

Tools:
- [`gltest`](../../tools/ut-probe/gl/gltest.c) now records the time between consecutive swaps and
  prints mean, median, 95th percentile, maximum, and how many frames fell into each band (under 6
  ms, around 120 Hz at 6-10.5 ms, 10.5-14, around 60 Hz at 14-20, 20-30, over 30 ms), skipping the
  first 30 frames; `--interval N` sets the swap interval (1 = wait for vsync, 0 = do not).
  Checked with the screen off: frames land mostly 20-30 ms apart there (mean 24.6 ms).
- stone-ut's `fps-panel.py` alongside: the panel's real refresh (vsync timestamps from the display
  driver), the frames the system compositor actually commits each second, and GPU load and clock.
- [`pacing.sh`](../../tools/ut-probe/pacing.sh): five runs of 900 frames, each with `fps-panel.py`:
  vendor GLES at swap interval 1 and 0, zink GLES at 1 and 0, zink desktop GL at 1. Output in
  `pacing-result.txt` in the test folder.

zink presents through Turnip's patched Wayland WSI, so its numbers stand for any Turnip Vulkan game
too; the vendor GLES driver presents through libhybris' own path and is the reference for what Lomiri
lets a client reach.

**Manual run M1 (screen on and unlocked, do not touch the phone for about 2 minutes):**
```
cd /home/phablet/utdeck-probe && ./pacing.sh
```
The results are read from `pacing-result.txt` over adb.

**M1 result (run by the maintainer, 2026-11-18).** The panel ran at 119.7 Hz (vsync median
8.35 ms) in every run.

| Run | App: frames per second | Time between frames: median (p95) | Frames in the bands (<6 / 6-10.5 / 10.5-14 / 14-20 / 20-30 / >30 ms) | Compositor commits/s (max) | GPU |
| --- | --- | --- | --- | --- | --- |
| vendor GLES, interval 1 | 83.6 | 11.94 ms (13.90) | 0 / 92 / 740 / 34 / 3 / 0 | 87.1 | 39 % at 650 MHz |
| vendor GLES, interval 0 | 274.5 | 3.52 ms (6.04) | 823 / 45 / 1 / 0 / 0 / 0 | **118.8** | |
| zink GLES, interval 1 | 44.3 | 22.89 ms (25.80) | 0 / 0 / 0 / 105 / 759 / 5 | 45.5 | 37 % at 390 MHz |
| zink GLES, interval 0 | 207.9 | 4.57 ms (7.65) | 662 / 200 / 6 / 1 / 0 / 0 | **118.7** | |
| zink desktop GL, interval 1 | 43.7 | 23.19 ms (26.56) | 0 / 0 / 1 / 99 / 764 / 5 | 45.8 | 38 % at 390-490 MHz |

(`run-timed.py`'s CPU per frame: vendor 1.67 / 1.33 ms, zink 2.81 / 1.52 / 2.87 ms. In the interval-0
runs `fps-panel.py`'s last lines read 0 because the run had already ended; its summary covers the
run.)

What it shows:
- **Neither path is limited by GPU or CPU.** The GPU stays under 40 % busy; unpaced, zink delivers
  208 fps and Lomiri commits up to 118.7 frames a second, the full panel rate, through our Turnip's
  `android_wlegl` path.
- **The loss is all in paced (FIFO) mode**: the wait before the next frame may start. The vendor path
  waits about 1.4 vsyncs (11.9 ms, so even it never reaches 120); our Turnip path about 2.7 vsyncs
  (22.9 ms, near 44 fps). Desktop GL and GLES through zink pace the same.
- Why waiting matters (Mesa's source, `src/vulkan/wsi/wsi_common_wayland.c`): Lomiri offers neither
  `wp_fifo_v1` nor `wp_commit_timing_v1`, so Mesa's Wayland WSI uses its "legacy FIFO": before
  presenting a frame it waits for the compositor's frame callback for the previous one
  (`legacy_fifo_ready`; Mesa's own comment: "the queue depth in WL WSI is just one frame due to frame
  callback in FIFO mode"). So in FIFO each frame costs the time from a commit to Lomiri's "done".
  Lomiri (a nested Mir server under `lomiri-system-compositor`) seems to answer late on this path.
- **An existing switch can avoid the wait**: `MESA_VK_WSI_PRESENT_MODE=mailbox` (also `fifo`,
  `relaxed`, `immediate`; `wsi_common.c`) forces the present mode for every swapchain of a program.
  With MAILBOX, frames go out as fast as they are made and Lomiri shows the newest at each vsync (no
  tearing), as the unpaced runs show; the game then needs a frame limiter (DXVK's own, or the
  overlay's) or it renders as fast as it can and wastes power.

Next, run M2: where the wait comes from, by time-stamping every Wayland message for the three paced
clients ([`pacing2.sh`](../../tools/ut-probe/pacing2.sh): the vendor GLES driver, zink GLES, and
`vkcube` on our Turnip, which is Vulkan without zink and ran FIFO at 68.9 fps in 1.1). From the logs:
the time from each commit to the frame callback's "done", and to each buffer release.

**Manual run M2 (screen on and unlocked, do not touch the phone for about 30 seconds):**
```
cd /home/phablet/utdeck-probe && ./pacing2.sh
```

**M2 result (run by the maintainer, 2026-11-18).** Read from the `WAYLAND_DEBUG` logs on the PC
(after skipping 30 warm-up frames; a vsync is 8.35 ms):

| Client (paced) | Time between commits (median) | Commit to Lomiri's "done" (median, p10-p90) | "done" to the next commit | Commit to a buffer release |
| --- | --- | --- | --- | --- |
| vendor GLES, fullscreen | 12.5 ms | 12.2 ms (10.7-14.2) | 0.29 ms | 10.8 ms |
| zink GLES on our Turnip, fullscreen | 24.2 ms | 14.2 ms (13.1-16.3) | **9.7 ms** (7.2-11.4) | irregular, 14-64 ms |
| `vkcube` on our Turnip, 500x500 window | 15.3 ms | 15.0 ms (13.8-16.7) | 0.27 ms | 13.7 ms |

Time between two "done" events, in vsyncs: vendor 135 times 1, 132 times 2; zink 40 times 2, 221
times 3; `vkcube` 17 times 1, 249 times 2.

There are two separate waits:

1. **Lomiri answers the frame callback late, for every client.** "done" arrives 12-15 ms after the
   commit, 1.5-1.8 vsyncs, and almost never within one vsync, the vendor driver included. A client
   that waits for "done" before the next commit (the vendor EGL, and Mesa's "legacy FIFO" above) is
   therefore capped around 65-80 fps on a 120 Hz panel, with frames alternating between 1 and 2
   vsyncs (uneven motion). This is Lomiri's behaviour (probably: it sends "done" only after its own
   composited frame has gone on to the system compositor, one nesting level more than a desktop),
   not a driver problem.
2. **Our Turnip's Vulkan WSI adds nothing**: `vkcube` commits 0.27 ms after "done", like the vendor
   driver. Its lower rate (65 against 80 fps) comes from its slower "done" (15 ms; a small window
   that Lomiri composites with the shell, not a fullscreen one).
3. **zink adds a second wait of its own, 9.7 ms**: after "done", the next commit only comes after
   the frame has been drawn. The cause is in Mesa's EGL on Wayland for zink
   (`src/egl/drivers/dri2/platform_wayland.c`, `dri2_wl_kopper_swap_buffers_with_damage`):
   `eglSwapBuffers` first waits for its own frame callback (`dri2_wl_surface_throttle`), and only
   then hands the frame to zink, which only then sends the frame's GPU work. So the GPU starts
   drawing frame N+1 only once Lomiri has answered for frame N, and the two waits add up: 14 + 10 =
   24 ms, 3 vsyncs. (The log shows one frame callback per frame, requested right after "done", which
   is EGL's; so the Vulkan swapchain under EGL does not run FIFO and the WSI's own wait is not
   involved.) The vendor EGL has its frame drawn before it waits.

What to do about it:
- **zink**: send the GPU work before the wait. Test first from the application side with a
  `glFlush` before `eglSwapBuffers` ([`gltest --flush`](../../tools/ut-probe/gl/gltest.c), new);
  if it works, the same flush belongs in Mesa's `dri2_wl_kopper_swap_buffers_with_damage` before
  the throttle, as our patch 0004, so every GL game gets it. Expected: about 70 fps paced (only
  Lomiri's wait left).
- **Every paced client, Vulkan games included**: Lomiri's late "done" stays. The way around it is
  not to wait for it: present in MAILBOX (`MESA_VK_WSI_PRESENT_MODE=mailbox` for Vulkan; swap
  interval 0 for zink, which then presents in MAILBOX on Wayland) and let a frame limiter set the
  pace (DXVK's `DXVK_FRAME_RATE`, or the overlay's). A Mesa change that lets one more frame be in
  flight (wait for the "done" of the frame before last) is the other option, kept for later.
  [`gltest --limit FPS`](../../tools/ut-probe/gl/gltest.c) (new) sleeps to a fixed rate before each
  swap, as such a limiter does.

**Manual run M3 (screen on and unlocked, do not touch the phone for about 1 minute):**
```
cd /home/phablet/utdeck-probe && ./pacing3.sh
```
[`pacing3.sh`](../../tools/ut-probe/pacing3.sh): zink paced with `--flush` (once with
`WAYLAND_DEBUG`, once with `fps-panel.py`), then zink at interval 0 with the limiter at 60, 90 and
120 fps. Results in `pacing3-result.txt` and `wl-zink-flush.log`.

**M3 result (run by the maintainer, 2026-11-18).** zink GLES on our Turnip, fullscreen, 600 frames
each; "committed" is what `fps-panel.py` counts the system compositor putting on the panel, each
second, once the run is going:

| Run | App: frames per second (median frame time) | Frames in the bands (<6 / 6-10.5 / 10.5-14 / 14-20 / 20-30 / >30 ms) | Committed per second | GPU |
| --- | --- | --- | --- | --- |
| paced (interval 1), before (M1) | 44.3 (22.9 ms) | 0 / 0 / 0 / 105 / 759 / 5 | 45 | 37 % at 390 MHz |
| paced, `--flush` | 53.5 (18.1 ms) | 0 / 1 / 11 / 391 / 162 / 4 | 51-55 | 35-41 % at 390-650 MHz |
| interval 0, limiter 60 | 60.0 (16.6 ms) | 0 / 5 / 79 / 427 / 58 / 0 | **59.6-59.9** | 38 % at 650 MHz |
| interval 0, limiter 90 | 90.0 (11.1 ms) | 4 / 182 / 352 / 31 / 0 / 0 | 86-89.5 | 50 % at 700 MHz |
| interval 0, limiter 120 | 120.0 (8.4 ms) | 78 / 421 / 66 / 4 / 0 / 0 | 101-107 | 54-61 % at 700 MHz |

The `--flush` run with `WAYLAND_DEBUG`: commit to "done" 14.1 ms (unchanged: Lomiri's part), "done"
to the next commit 6.7 ms (was 9.7).

What it shows:
- **`glFlush` before the swap helps only partly**: 44 to 53.5 fps. zink still needs 6.7 ms after
  "done" before it commits (not explained yet; kopper does more than the GPU work at swap time). A
  Mesa patch for this would bring a GL game that keeps vsync on from 44 to about 54 fps, still
  uneven; not the fix to aim for.
- **Not waiting for Lomiri, plus a frame limiter, works**: at 60 the panel gets 59.6-59.9 frames
  every second, steadily; at 90 it gets 86-89.5. At 120 the app makes 120 but only 101-107 reach the
  panel: the frames are not aligned to the vsync, so Lomiri (in MAILBOX it shows the newest frame at
  each vsync) drops some, so 120 is not a target on this path. The sleep-based limiter
  is not vsync-aligned either, so at 60 some frames still sit 1 or 3 vsyncs instead of 2 (79 frames
  in 10.5-14 ms, 58 in 20-30 ms); a limiter that times frames to the vsync would be steadier.
- The GPU and CPU have room left in every run (under 61 % GPU, 15-25 % of one CPU core).

**Step 2 conclusion.** On Lomiri, waiting for vsync the normal Wayland way costs a third to a half
of the frame rate, for every driver, because Lomiri answers each frame 1.5-1.8 vsyncs late; zink adds
its own wait on top. UTDeck should therefore run games **without vsync waiting, with a frame
limiter**, by default:
- Vulkan games (DXVK, vkd3d-proton, native): `MESA_VK_WSI_PRESENT_MODE=mailbox` and DXVK's
  `DXVK_FRAME_RATE=60` (or the game's own limiter). To be checked with a real DXVK game in step 4.
- OpenGL games through zink: `vblank_mode=0` (Mesa's standard driconf switch: in
  `src/egl/drivers/dri2/egl_dri2.c`, `dri2_setup_swap_interval` then clamps every swap interval to
  0, so EGL does not wait and zink presents in MAILBOX), and a limiter.
- Default target 60 fps (steady); 90 as an option for light games; not 120 on this path.
- MAILBOX does not tear: Lomiri composites and shows whole frames only.
Open, for later: a limiter that times frames to the panel's vsync (steadier at 60), and the 6.7 ms
zink still loses after "done" when vsync stays on.

## Step 3: the two Geekbench anomalies

Stage 1.2's scores (Turnip 6978084 against the vendor driver 6977517): Particle Physics 120.0 FPS
against 201.9 (0.59), Stereo Matching 4.80 Gpixels/s against 628.3 Mpixels/s (7.64).

### Tools

- The dispatch-logging layer ([`layer/dispatchlog.c`](../../tools/ut-probe/vkcompute/layer/dispatchlog.c))
  now also times things: `M` lines when a shader module is created (they mark where a workload
  starts), `Q` lines per `vkQueueSubmit` (how long the call took, how many command buffers),
  `W` lines per wait (`vkWaitForFences`, `vkQueueWaitIdle`, `vkDeviceWaitIdle`,
  `vkWaitSemaphores`), `G` lines per query read, fence polls counted, and `K` lines with what else
  a submitted command buffer holds (draws, indirect dispatches, timestamps, barriers, copies).
- [`dlog-timing.py`](../../tools/ut-probe/vkcompute/dlog-timing.py) (new) summarises such a log per
  workload: wall time, submissions, waits, and the busiest shaders with their configurations.
- `spvrun` gained `--batch N` (N command buffers, one dispatch and a barrier each, in **one**
  `vkQueueSubmit`, then `vkQueueWaitIdle`: Geekbench's own pattern) and a `rand@LO,HI` buffer fill.
- The layer works only on Turnip: Ubuntu Touch's `libvulkan.so.1` is libhybris' bridge straight to
  Android's Vulkan loader, not the Khronos loader, so no layer can sit in front of the vendor
  driver. The vendor side is measured by replaying the recorded dispatches with `spvrun`.

A full Geekbench run with the layer on our default build (`turnip-utdeck`), 14:33-14:50, uploaded
([6978256](https://browser.geekbench.com/v6/compute/6978256)); workloads 303 and 503 failed
validation as usual without `tu_restrict_subgroup_size_64` (stage 1.2). Geekbench submits work and
then waits for the queue to be idle (`vkQueueWaitIdle`) every time; it never polls fences or reads
GPU timestamps, so it times with the wall clock.

### Particle Physics: Turnip's compiled shader is slower

What Geekbench does: one shader (`9a1926a3d0`, the names are in the SPIR-V: 4,096 particles in a
50x50 world, one work-group per particle, its threads walk over all the other particles and check
collisions, then add up the forces in shared memory). One simulation step is one command buffer
with one dispatch and a barrier; the timed part submits **200 steps in one `vkQueueSubmit`** and
waits. On Turnip each 200-step batch took 1.65 s: 8.25 ms per step, which is exactly Geekbench's
"120.0 FPS" (200 / 1.65 s = 121).

Replayed with `spvrun --batch 200` ([`pp-batch.sh`](../../tools/ut-probe/vkcompute/pp-batch.sh); same shader, specialization constants and push constants as
logged, positions spread over the 50x50 world):

| | ms per step | GPU clock during the run |
| --- | --- | --- |
| Turnip, our build (0001 + 0002: one kernel submission per command buffer) | 8.26 | 700 MHz throughout |
| Turnip without our patches (the whole batch in one kernel submission) | 8.25 | |
| Vendor V@530 | 5.19 (= 193 FPS, close to its 201.9) | 700 MHz throughout |

So: the GPU is busy the whole time on both (the GPU span equals the wall time), both run at the top
clock (700 MHz), and patch 0002 costs nothing here. **Turnip's compiled shader simply runs this
code 1.6 times slower.** The same holds at every work-group size the tuner tries (ms per step,
Turnip / vendor): 32: 15.9 / 12.0; 64: 9.7 / 6.9; 128: 8.1 / 5.2; 256: 9.2 / 6.2; 512: 11.5 / 8.9;
1024: 17.4 / 17.3.

Why (Turnip's compiler output, `IR3_SHADER_DEBUG=cs,disasm`): in the inner loop each 2-float value
(another particle's position, its velocity) is fetched as **two separate 32-bit texture fetches**
(`isam`), four per iteration, followed at once by a wait for them (`(sy)`). On a6xx GPUs (the
A619 is one) Mesa deliberately splits read-only buffer loads this way to use the texture cache
(`ir3_nir.c`: "the tex cache benefit outweighs the benefit of vectorizing"); only a7xx GPUs can
fetch several values in one `isam`. Tested with a diagnostic switch,
[`diag/0005`](../../tools/mesa-build/patches/diag/0005-ir3-diag-no-isam-for-ssbo-loads.patch)
(`UTDECK_IR3_NO_ISAM_SSBO=1` keeps the loads paired on the other path, `ldib`; build
`turnip-isam`, [`pp-isam.sh`](../../tools/ut-probe/vkcompute/pp-isam.sh)): **3.7 times slower** (30.4 against 8.3 ms per step), identical results. So Mesa's
choice is right, and the loss is in how the loop is scheduled around those fetches. None of the
compiler's switches helps (ms per step at size 128): `thread64` 10.1, `tu_restrict_subgroup_size_64`
10.0, `nodescprefetch` 8.2, `noaliastex` 8.3, `nouboopt` 8.2, `nofp16` 8.3; only `nopreamble`
is a little faster (7.6).

For UTDeck: a real but narrow loss, in compute shaders that loop over buffer data with little
other work per fetch (particle systems, some post-processing); Turnip wins most other compute
workloads (stage 1.2). The fix belongs in Mesa's compiler (ir3); it is a report for upstream with
this shader as the test case, not something to patch ourselves now.

### Stereo Matching: Turnip really is that much faster

What Geekbench does: one main shader (`7cbee64526`: for each pixel of a 450x375 image pair, it
compares 11x11 windows of the left and right images at 8 disparities per dispatch and keeps the
best score), reading three R8 storage images and a score buffer, writing an image and a buffer.
The timed part submits 10 command buffers at a time; on Turnip each such batch took 262 ms.

First suspect: on Turnip, Geekbench's tuner picked work-groups of 32x64 = 2,048 threads, a size the
vendor driver (limit 1,024) cannot offer. 32x64 is a valid shape on Turnip, and our `wgsize` test
shows every one of its invocations runs (8,192 of 8,192 slots written). A second full Geekbench run
with the layer and `tu_restrict_subgroup_size_64=true` (limit 1,024; 15:00-15:15, every workload
validated, [6978269](https://browser.geekbench.com/v6/compute/6978269)): the tuner then picks 32x2
and a batch takes 276 ms, only 5 % slower. So the large work-groups are not the reason.

Replayed with `spvrun` ([`sm-time.sh`](../../tools/ut-probe/vkcompute/sm-time.sh); it gained storage images, `--img KIND:WxH`, R8): the main shader with
Geekbench's push constants (450, 375, stride 512, disparities 0-7, window 11), random image
contents, batches of 10 dispatches:

| Work-group | Turnip, ms per dispatch | Vendor V@530, ms per dispatch | Outputs |
| --- | --- | --- | --- |
| 32x2 | 35.2 | 285.6 | identical (both written image and buffer hash the same) |
| 32x32 | 34.8 | 299.6 | identical |
| 32x64 (Turnip only) | 34.8 | | identical |

**Turnip does all the work and gets the same answer, 8.1 times faster** (Geekbench: 7.6). The
vendor driver is simply slow on this shader. Likely why, from Turnip's side: Turnip reads the three
images through the texture cache (12 `isam` fetches in its compiled code; the shader never writes
them, so the compiler treats them as read-only even though the SPIR-V does not mark them so),
while a driver that takes the SPIR-V literally must use the uncached image path, which on this GPU
is several times slower (for buffers we measured 3.7 times, above). The vendor driver's code
cannot be inspected, so this stays a likely explanation, not a proven one.

For UTDeck: good news; Turnip wins clearly on image-processing compute of this kind.

### Step 3 conclusion

Both anomalies are explained, and neither is a fault in our patches or in KGSL:
- **Particle Physics (-41 %)** is a real Turnip loss in its shader compiler (ir3): the inner loop
  fetches each 2-float value as two texture fetches and waits on them at once. Same GPU clock,
  same submissions; patch 0002 costs nothing. A report for upstream Mesa with this shader as the
  test case; nothing to change in UTDeck now.
- **Stereo Matching (x7.6)** is a real Turnip win: identical output, 8.1 times faster than the
  vendor driver in our replay; not the 2,048-thread work-groups.

Side results: the dispatch layer now times submissions and waits; `spvrun` replays Geekbench's
submission pattern (`--batch`) and storage images (`--img`); diagnostic patch 0005 shows the
texture-cache path is right for buffers on this GPU.

## Step 4: route A against route B, and the decision

Route A: Android Turnip v25.3.0 R11 (Mesa 25.3.0-devel, the build the maintainer used for Dolphin)
loaded through the phone's own `libvulkan.so.1`: libhybris, then Android's Vulkan loader, with
dolphin-ut's stand-in `libvndksupport.so` and the `pthread_barrier` hook (`r11/env.sh`). Route B:
our build `turnip-utdeck` (Mesa 26.2.5, patches 0001-0003) through the Khronos loader.

### Features

| | Route A (R11) | Route B (ours) |
| --- | --- | --- |
| DXVK 2.7.1's required entries (`dxvk-check.py`) | 41 of 41 | 41 of 41 |
| Vulkan API | 1.3.328 | 1.3.354 |
| Device extensions | 143 | 168 |
| For DXVK's frame pacing and present control: `VK_KHR_present_id`, `VK_KHR_present_wait`, `VK_KHR_swapchain_maintenance1` | no (libhybris' Android WSI) | **yes** (Mesa's WSI) |
| Sparse binding (Direct3D 12 tiled resources in VKD3D-Proton) | advertised | not advertised |
| OpenGL through zink | no (an Android library) | **yes** (stage 1.3) |

Route A's sparse binding cannot really work here: Turnip implements it on KGSL with "virtual
buffers" (`KGSL_MEMFLAGS_VBO`, `IOCTL_KGSL_GPUMEM_BIND_RANGES`), which stone's kernel does not have
(neither appears in its `msm_kgsl.h` or driver). Our build probes the kernel
(`kgsl_is_virtual_bo_supported`) and correctly leaves it out; R11 advertises it anyway, which would
mislead VKD3D-Proton. To check in stage 3, when VKD3D-Proton is tried.

### Compute ([`ab-compute.sh`](../../tools/ut-probe/ab-compute.sh))

| | Route A (R11) | Route B (ours) |
| --- | --- | --- |
| `intops` (66,060,288 integer results against the CPU) | 0 wrong | 0 wrong |
| Particle Physics replay, ms per step (batch of 100) | 7.67 | 8.33 |
| Stereo Matching replay, ms per dispatch (batch of 10) | 35.1 | 35.4 |
| Outputs (hashes) | identical | identical |
| Kernel submissions for 300 command buffers (`kgsl-trace`) | **3** (100 command buffers each) | 302 (one each, patch 0002) |
| KGSL context priority | 8, fixed | 8 by default, LOW (12) on request (patch 0001) |

R11 is 8 % faster on the particle shader (an older compiler, or its build options; not our
patches: unpatched 26.2.5 runs it at 8.25 ms too), equal on Stereo. But it has both problems stage
1.2 fixed in route B: it packs many command buffers into one kernel submission, which KGSL kills
after 2 s (Geekbench's Gaussian Blur, stage 0), and it cannot run a game below the compositor's
priority, so a long GPU job freezes the UI.

### Presentation (screen off, `vkcube`, [`ab-present.sh`](../../tools/ut-probe/ab-present.sh))

- Route B: FIFO, 600 frames, 0.98 ms of CPU per frame (as in stage 1.1).
- **Route A: `vkcube` hangs.** `WAYLAND_DEBUG` shows two frames presented, then Lomiri's second
  `configure` (the window becoming active), after which `vkcube` recreates its swapchain and nothing
  more is presented; it sat there until the 90 s timeout. This is the swapchain re-creation hang
  dolphin-ut met in libhybris (Dolphin avoids it by rebuilding the whole surface). Wine's Vulkan
  code recreates swapchains on every resize and mode change, so route A would need that workaround
  inside Wine.

### Long run (10 minutes each, screen off, [`longrun.sh`](../../tools/ut-probe/longrun.sh))

Particle Physics batches (1,000 steps per run) in a loop, outputs hashed each run; route B also
ran `vkcube` (FIFO) for the whole 10 minutes:

| | Route A | Route B (+ `vkcube`) |
| --- | --- | --- |
| Compute runs | 76, all identical | 70, all identical |
| ms per step, over the run | 7.62-8.06 | 8.11-8.61 (sharing the GPU with `vkcube`) |
| `vkcube` | (cannot present) | ran the whole time |
| GPU / CPU temperature, peak | 49.8 / 43.6 °C | 56.7 / 47.5 °C |
| GPU clock | 700 MHz (two samples at 266) | 700 MHz |
| New KGSL kernel messages | 0 | 0 |

Both stable; nothing degraded over the 10 minutes. Thermal limits stay the vendor's (the vendor's
thermal service was in control throughout; no limit was touched).

### Packaging check: relative paths

Both manifests accept paths relative to their own file, tested on the phone with a copy of the
driver in another folder: the Khronos loader's ICD file (`"library_path": "./libvulkan_freedreno.so"`,
`vulkaninfo` found the device) and glvnd's EGL vendor file (`"./lib/libEGL_mesa.so.0"`, `gltest
headless` passed through zink). So a driver folder can be installed or moved anywhere as it is.

### The decision

**UTDeck uses route B: our own glibc Turnip build, with zink for OpenGL.** Route A is not shipped.

Why, in order of weight:
1. Route B has the fixes games need on KGSL, and route A cannot get them without us building
   Android Turnip ourselves: games below the compositor's priority (0001, no frozen UI) and one
   kernel submission per command buffer (0002, no 2 s GPU timeout).
2. Route A's presentation hangs on swapchain re-creation, which Wine does all the time.
3. Route B gives OpenGL (zink) for WineD3D and OpenGL games; route A gives none.
4. Route B has the WSI extensions DXVK uses for pacing (`present_wait`, `swapchain_maintenance1`).
5. Route B keeps libhybris, bionic and its TLS hooks out of Wine's process (risk 3 in the roadmap).

What route A does better, and what we do about it: 8 % on one compute shader (watch for an ir3
regression when updating Mesa; the Particle Physics shader is the test), and Android Turnip builds
are easy to find; our build script makes route B builds just as easy. Route A stays documented
(stage 0, this file) as a fallback should route B break on a future Ubuntu Touch release.

The vendor driver stays the system's (Lomiri and every other app), untouched.

### Defaults per game, from stage 1

| What | Default | Why |
| --- | --- | --- |
| Vulkan driver | our Turnip (route B) | above |
| OpenGL | zink on the same Turnip (`MESA_LOADER_DRIVER_OVERRIDE=zink`, our EGL vendor file) | stage 1.3 |
| GPU priority | the game's Vulkan queues ask for LOW (a small implicit layer of ours, stage 2) | stage 1.2: the UI stays responsive |
| Presentation | no vsync wait: `MESA_VK_WSI_PRESENT_MODE=mailbox` (Vulkan), `vblank_mode=0` (OpenGL) | step 2: Lomiri answers frame callbacks late |
| Frame limiter | 60 fps (`DXVK_FRAME_RATE=60`, or the game's own); 90 as an option | step 2: steady on the panel |
| `tu_restrict_subgroup_size_64` | off (only for Geekbench) | stage 1.2: games never ask for 2048-wide work-groups |

### The driver folder layout

One driver = one Mesa build (Turnip and zink from the same source and patches), in a folder that
works wherever it is placed:

```
drivers/
  turnip-26.2.5-utdeck1/            name: Mesa version + our build number
    BUILD-INFO                      source commit, patches, build date, SHA-256 (as build-turnip.sh writes)
    vulkan/
      libvulkan_freedreno.so
      icd.json                      "library_path": "./libvulkan_freedreno.so"
    gl/                             zink, as a glvnd EGL vendor
      libEGL_mesa.so.0
      libgallium-26.2.5.so
      egl_vendor.json               "library_path": "./libEGL_mesa.so.0"
runtime/
  lib/libvulkan.so.1                the Khronos loader (Ubuntu Touch's own libvulkan.so.1 is libhybris')
```

- Shipped drivers live inside UTDeck's package (read-only); drivers the user adds later go in
  UTDeck's data folder (`~/.local/share/<app id>/drivers/`) with the same layout, so a driver
  manager can list both.
- A game's launch sets, from its profile: `LD_LIBRARY_PATH=runtime/lib:<driver>/gl`,
  `VK_DRIVER_FILES=<driver>/vulkan/icd.json`, `__EGL_VENDOR_LIBRARY_FILENAMES=<driver>/gl/egl_vendor.json`,
  `MESA_LOADER_DRIVER_OVERRIDE=zink`, and the defaults above. Nothing on the system changes.
- `build-turnip.sh` and `build-zink.sh` become one script that produces this folder (stage 2).

## Events

- 2026-11-18: 1.4 split into four steps. Step 1: `spvrun --profile` written; Turnip's KGSL wait and
  the kernel's read; the per-submission cost measured in steady state on both drivers; the 1.2
  figure found to be an artifact of short runs; corrected.
- 2026-11-18: step 2 prepared: frame-time statistics and `--interval` in `gltest`, `pacing.sh` with
  stone-ut's `fps-panel.py`; manual run M1 written.
- 2026-11-18: M1 run by the maintainer: vendor paced 83.6 fps, zink paced 44.3 fps, both unpaced reach
  Lomiri's full 118.7-118.8 commits/s; cause traced to Mesa's legacy FIFO waiting for frame
  callbacks; `MESA_VK_WSI_PRESENT_MODE` found; `pacing2.sh` (callback timing) written.
- 2026-11-18: M2 run by the maintainer: Lomiri's "done" comes 12-15 ms after a commit for every
  client; zink's extra 9.7 ms traced to EGL's kopper swap waiting before the frame is sent to the GPU;
  `gltest --flush` / `--limit` and `pacing3.sh` written.
- 2026-11-18: M3 run by the maintainer: `--flush` 44 to 53.5 fps; interval 0 with a limiter: 60 and 90
  fps reach the panel steadily, 120 does not; step 2 closed with the default "no vsync wait plus a
  frame limiter".
- 2026-11-18: step 3: dispatch layer gained timing; Geekbench run with it (6978256); Particle
  Physics reproduced with `spvrun --batch` (8.26 against 5.19 ms per step, both at 700 MHz),
  traced to ir3's split texture fetches; diagnostic 0005 (`turnip-isam`) shows the other load path
  is 3.7 times slower. Stereo Matching: second run with the 1,024-thread cap (6978269) rules out the
  large work-groups; `spvrun --img` replay gives identical output, Turnip 8.1 times faster.
- 2026-11-18: step 4: route A (R11) against route B on DXVK's check (both 41/41), extensions,
  compute (`intops`, the two Geekbench replays, `kgsl-trace`), presentation (route A's `vkcube`
  hangs at swapchain re-creation), a 10-minute run each (both stable); relative manifest paths
  checked; decision: route B; driver layout written. Stage 1.4 done.
