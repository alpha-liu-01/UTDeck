# Stage 1.2: Turnip's compute failure on the Adreno 619

Started and closed 2026-11-18. Plan: [stage1-roadmap.md](stage1-roadmap.md). Goal: find what is behind stage
0's two Turnip failures in Geekbench's Vulkan test, and end with a fix, a workaround, or an upstream
report.

What stage 0 found ([stage0-probe.md](stage0-probe.md), section 5), with both DD-Turnip (Mesa
27.0.0-devel, Linux build) and Turnip R11 (Mesa 25.3.0-devel, Android build through libhybris):
1. Workload 303 (Face Detection) "failed validation": the GPU's result was wrong, or read wrong.
2. In Gaussian Blur, Geekbench's tuning step (`convolve_horizontal_buffer`, local size 32x1x1 first)
   hit KGSL's 2 s limit: "gpu timeout", "gpu fault", the context dropped, `VK_ERROR_DEVICE_LOST`.

The vendor driver passed both, under the same limit.

## Ways to iterate

- **Geekbench runs only as a whole** (about 10 minutes): `--workload` and `--workload-list` exist
  but "are only available in Geekbench 6 Pro". So each experiment costs a full run; they run detached
  with a small runner on the phone (`gb-vk.sh <label> [VAR=value ...]`, which writes
  `gb-<label>.log` and `gb-<label>.status`).
- **A diagnostic build of our own** (stage 1.1's `tools/mesa-build/`): `build-turnip.sh` now takes
  `NAME` (output `build/turnip-<name>`, phone folder `turnip-<name>`) and `PATCHES` (applied with
  `git apply` on a clean checkout of the pinned commit). Diagnostic patches live in
  `tools/mesa-build/patches/diag/`, each behind an environment variable and off by default.
- Turnip's and its shader compiler's own switches, in this tree (`tu_util.cc`, `ir3_compiler.c`):
  `TU_DEBUG` (`flushall`, `syncdraw`, `nolrz`, `noubwc`, ...) and `IR3_SHADER_DEBUG` (`nofp16`,
  `nopreamble`, `spillall`, `fullsync`, `nodescprefetch`, `cs` to print compute shaders, ...).

## Hypothesis 1: the wrong result is a CPU cache problem

A wrong result in a compute test, from a driver that runs games correctly, often means the CPU read
stale data from memory the GPU wrote.

Memory types, from `vulkaninfo`:

| Driver | Types |
| --- | --- |
| DD-Turnip (Mesa 27.0) and our build (Mesa 26.2.5) | `0x7` device-local, host-visible, **coherent** (uncached); `0xb` device-local, host-visible, **cached, not coherent** |
| R11 (Mesa 25.3, Android) | `0x7`; `0xf` cached **and** coherent; `0xb` cached, not coherent; `0x11` lazily allocated |

All three offer `0xb`. With it, the CPU sees the GPU's results only after the program calls
`vkInvalidateMappedMemoryRanges`, which Turnip implements in userspace with
`util_flush_inval_range` (a CPU cache clean-and-invalidate over the range, `tu_knl.cc`
`tu_bo_sync_cache`). The memory comes from KGSL with `KGSL_CACHEMODE_WRITEBACK`. The same code runs
on the upstream `msm` kernel driver, which Mesa's CI tests; KGSL is not in Mesa's CI.

The test: a build that does not offer `0xb`. Diagnostic patch
[`0001-tu-diag-hide-cached-noncoherent-memory.patch`](../../tools/mesa-build/patches/diag/0001-tu-diag-hide-cached-noncoherent-memory.patch):
with `UTDECK_TU_NO_CACHED_NONCOHERENT=1`, `has_cached_non_coherent_memory` is false, so every
host-visible allocation is coherent. Built as `build/turnip-diag` (SHA-256 `65eae6cd...1514`) and
pushed to the phone's `turnip-diag` folder.

If workload 303 validates with the switch on, the stale-read hypothesis holds (and points at
Turnip's cache maintenance on KGSL); if it still fails, the result itself is wrong on the GPU.

**That test could not run**: with `0xb` hidden, Geekbench stops at once with "failed to find
suitable memory type". It needs a host-cached memory type, and the glibc builds offer no cached
coherent one (`0xf` appears only in R11). This also shows that Geekbench reads its results through
cached memory, so the hypothesis stands.

Second diagnostic,
[`0002-tu-kgsl-diag-cached-memory-as-uncached.patch`](../../tools/mesa-build/patches/diag/0002-tu-kgsl-diag-cached-memory-as-uncached.patch):
with `UTDECK_TU_CACHED_AS_UNCACHED=1` the cached types stay advertised, but the KGSL backend
allocates them write-combine (uncached) instead of write-back. The CPU then cannot read stale data,
whatever the cache maintenance does. Built as `build/turnip-diag` (SHA-256 `07ea8f45...d227`),
pushed.

## Runs

| Label | Driver | Switches | Result |
| --- | --- | --- | --- |
| `ours-base` (first try) | our build (Mesa 26.2.5 + `adb4b85`) | none | killed at 01:19 when the phone dropped off USB: `adbd` restarted and took the run with it (below) |
| `ours-base` | our build | none | **same two failures**: workload 303 "failed validation" (01:28:18), then a GPU fault in Gaussian Blur (ctx 7, ts 110, `status 00EE1015`) and `DEVICE_LOST`, this time in `convolve_vertical_buffer`; `convolve_horizontal_buffer`, which failed in stage 0, passed. Not uploaded |
| `diag-nocached` | diagnostic 0001 | `UTDECK_TU_NO_CACHED_NONCOHERENT=1` | stopped at once: "failed to find suitable memory type" |
| `diag-uncached` (first try) | diagnostic 0002 | as below | not started: `adbd` crashed right after the push |
| `diag-uncached` | diagnostic 0002 | `UTDECK_TU_CACHED_AS_UNCACHED=1`, plus `MESA_SPIRV_DUMP_PATH` and `IR3_SHADER_DEBUG=cs` | **workload 303 still "failed validation"** (09:28:16); Gaussian Blur fault at the same place (vertical pass, ts 110). 35 SPIR-V shaders dumped |
| `fd-control` | our build | `MESA_SHADER_CACHE_DISABLE=true`, `STOP_AFTER=Face Detection` | 303 failed; stopped after Face Detection, 3 minutes |

So **three Mesa versions** (25.3 R11, 26.2.5 ours, 27.0 DD) fail at the same two points. The
timeout moves between runs (horizontal pass in stage 0, vertical pass now; context timestamp 2
then 110), which fits a dispatch that runs close to KGSL's 2 s limit better than a real hang, which
would stop at the same place every time.

### Step 1 result: not a CPU cache problem

With every host-visible buffer allocated uncached (write-combine), workload 303 still fails. The
diagnostic did apply: the only allocations that skip that code are exportable (shareable) buffers,
which Geekbench's compute buffers are not. So the CPU reads what the GPU wrote; **the GPU computes a
wrong result.** Hypothesis 1 is ruled out.

### What Face Detection's shaders do

From `IR3_SHADER_DEBUG=cs` (the compiled code, printed while Geekbench runs) and the SPIR-V dump.
Geekbench's shaders are not reproduced here, only described.

- Face Detection compiled **115 variants of 5 shaders**: Geekbench's tuner recompiles each for many
  work-group sizes (32x1x1 up to 1024x2x1, through specialization constants).
- All five are small (135-361 lines of SPIR-V) and plain: only the basic `Shader` capability; no
  subgroup operations, atomics, barriers, images, shared memory or 16-bit types. They read and
  write 32-bit int and float storage buffers, with parameters in a push-constant block.
- **Every one uses signed integer division and modulo** (`OpSDiv`, `OpSMod`) for index arithmetic,
  several of them on two push-constant values, which is the same for every invocation. The Adreno
  has no integer divide instruction: ir3 expands these into a sequence of instructions. `OpSMod`
  takes the sign of the divisor (unlike C's `%`), a classic place for compiler bugs.
- ir3 moves work that is the same for all invocations into a **preamble** run once per dispatch; the
  compiled code starts with `@preamble_start_ir3` and computes 32-bit multiplies of push-constant
  values there from 16-bit pieces (`umul_16x16`, `imadsh_mix16`).

### Step 1, second part: a sweep of ir3's switches

Face Detection's verdict comes about 3 minutes into a run, long before Gaussian Blur (which freezes
the UI). The runner's `STOP_AFTER="Face Detection"` ends Geekbench as soon as the next workload
starts, so each test takes about 3 minutes and never reaches the blur. The shader cache is off in
these runs (`MESA_SHADER_CACHE_DISABLE=true`), so a cached binary cannot hide a switch. The control
run fails as expected. Sweep (`fd-sweep.sh`, one switch per run): `nopreamble`, `noearlypreamble`,
`nodescprefetch`, `nouboopt`, `expandrpt`, `fullsync`.

**Sweep result: every switch fails the same way** (runs `fd-<switch>`, 09:42-10:03):

| `IR3_SHADER_DEBUG` | Workload 303 |
| --- | --- |
| (none, control) | failed validation |
| `nopreamble` | failed validation |
| `noearlypreamble` | failed validation |
| `nodescprefetch` | failed validation |
| `nouboopt` | failed validation |
| `expandrpt` | failed validation |
| `fullsync` | failed validation |

No single one of these ir3 optimizations is behind the wrong result. (Not checked separately that
each switch changed the compiled code; with the shader cache off they should have.)

### Step 1, third part: our own integer test

[`tools/ut-probe/vkcompute/`](../../tools/ut-probe/vkcompute/) holds a small Vulkan compute harness
of our own (`vkc.h`) and a first test, `intops`: a shader written from scratch
([`intops.comp`](../../tools/ut-probe/vkcompute/intops.comp)) with the operations Face Detection's
index arithmetic uses: signed division and modulo with per-invocation, uniform (push-constant) and
mixed operands, chained uniform divisions used as a divisor, and 32-bit multiplies. It runs at the
local sizes Geekbench tries (32 to 1024) over 14 push-constant cases (positive, negative, large) and
65,536 random inputs, and checks every result against a CPU reference that follows SPIR-V's rules
(`OpSDiv` rounds toward zero, `OpSMod` takes the sign of the divisor, `OpIMul` wraps). Built by
`tools/ut-probe/vkcompute/build.sh` in the `utdeck-mesa-cross` container (which now also has
`libvulkan-dev:arm64`).

| Driver | Result |
| --- | --- |
| **Our Turnip** (Mesa 26.2.5) | **66,060,288 results, 0 wrong: PASS** |
| Vendor V@530 through libhybris | **6,720,204 wrong**: its `OpSMod` returns C's remainder (the sign of the dividend) whenever the signs differ, e.g. `47 % -90893` gives 47 instead of -90846. A conformance bug in Qualcomm's driver, not Turnip's; it also shows the test catches wrong results |

So integer division, modulo and multiply are not Turnip's problem, at least in these forms.

### Step 1, what the shaders are

From the dumps' debug names: Face Detection is a small neural network. Its five shaders are a
tensor padding (`input_batch_size`, `output_channels`, ...), an im2col (`kernel_width`,
`patches_width`, `stride_width`, ...: cuts the image into patches for a convolution), a softmax
(`max_value`, `sum`), a resize (`scale_width`, `src_x`, ...) and one more. The heavy part (a matrix
multiply) is probably a shader compiled earlier in the run and reused, so the wrong result may come
from a shader outside these five.

### Step 1 summary

- Not stale CPU reads (diagnostic 0002).
- Not one of six ir3 optimizations (switch sweep).
- Not signed division, modulo or multiply (our `intops` test passes on Turnip).
- The GPU computes a wrong value somewhere in Face Detection's network. Narrowing it further needs
  the shaders themselves run with the same inputs on both drivers and their outputs compared
  (differential testing), which the harness can grow into.

## The runner: Geekbench's shaders on both drivers (maintainer's choice, 2026-11-18)

[`spvrun.c`](../../tools/ut-probe/vkcompute/spvrun.c) runs any compute shader with given
specialization constants, push constants and storage buffers (inputs filled from a fixed seed, so
both drivers get identical data), times each dispatch, and dumps the outputs;
[`spvcmp.py`](../../tools/ut-probe/vkcompute/spvcmp.py) compares two dumps on the PC (floats with a
relative tolerance, packed RGBA8, or exact). Each dispatch is submitted alone and waited for. The
shaders, their interfaces and Geekbench's specialization constants came from the SPIR-V dump, the
compiler log and a local decompilation with SPIRV-Cross (kept on the PC, not in the repository).

### Which shaders Face Detection uses

The 35 dumped shaders, by the workload where each was first compiled: 21 in Background Blur (also a
neural network), 5 in Face Detection, 9 in Horizon Detection. None uses `RelaxedPrecision`, so ir3's
16-bit lowering is not involved. Face Detection's network uses its own 5 shaders (pad, im2col,
softmax, resize, and a fifth with `batch_size, y_offset, y_stride, width, height, channels`) and
most likely the two **tiled matrix multiplies** compiled in Background Blur: `69aa5e0b34` (bounds
checked) and `da26791459` (no bounds checks, only valid when the sizes are multiples of the 32x32
tile). Both were compiled once, with local size 32x4x1 and 4096-byte shared-memory tiles.

### Results

| Test | Result |
| --- | --- |
| GEMM `69aa`, 256x256x256 | **bit-identical** on both drivers, and correct against a CPU reference (largest error 5.35e-6) |
| GEMM `69aa`, 100x77x129, bias modes 0, 1, 2 | bit-identical |
| GEMM `da26`, 256x256x256 | bit-identical |
| GEMM `da26`, 100x77x129 | 38 of 7,700 elements differ: not a bug. This version reads past the ends of its buffers when the sizes are not tile multiples, and what lies there differs between drivers. An invalid test, discarded |
| pad, im2col, softmax, resize, fifth shader, local 64x1 | bit-identical |
| the same five, at all **27 local sizes** Geekbench's tuner tries (32x1 to 1024x2) | **all 135 outputs bit-identical** to the vendor driver's |
| Geekbench Face Detection with `TU_DEBUG=flushall` (flush caches after every dispatch) | still fails |
| ... with `TU_DEBUG=syncdraw` (wait for idle after every dispatch) | still fails |

**Every shader Face Detection uses is correct on Turnip on its own**, at every local size; and
forcing flushes or waits between dispatches does not fix the benchmark. What is left for the wrong
result: Geekbench's real sizes and parameters (not known: the runner used plausible ones), or a
reliance on out-of-bounds reads (the unchecked GEMM at non-multiple sizes, if Geekbench pads its
buffers or enables `robustBufferAccess`). The next test for it: a diagnostic Turnip build that logs
each dispatch's pipeline, push constants and group counts during Face Detection, then the same
dispatches replayed in the runner.

### Step 2: the blur's timing (hypothesis 2)

`blur-time.sh` (on the phone) times Geekbench's horizontal blur shader (`f1157a0883`) on both
drivers, best of 3, with every dispatch kept well under KGSL's 2 s limit:

| Image, blur diameter | Local 32x1: Turnip / vendor | Local 256x1: Turnip / vendor |
| --- | --- | --- |
| 1920x1080, 15 | 21.2 / 23.7 ms | 18.3 / 28.8 ms |
| 1920x1080, 63 | 74.0 / 88.2 ms | 33.4 / 42.5 ms |
| 1920x1080, 127 | 137.5 / 168.3 ms | 67.7 / 81.7 ms |
| 4000x3000, 63 | 448.7 / 515.7 ms | 189.3 / 223.4 ms |

Other local sizes at 1920x1080, diameter 15 (Turnip / vendor): 64x1 33.4 / 35.4, 1024x1 23.1 /
31.4, 32x8 20.9 / 28.6, 64x16 27.7 / 27.6 ms.

**Hypothesis 2 is wrong: Turnip runs the blur about 15 % faster than the vendor driver**, and both
scale linearly with the work. So Turnip does not hit the 2 s limit because its shader is slower.
KGSL's limit applies per submission (`dispatch/drawobj_timeout`): if Geekbench's tuner records
several blur dispatches into one command buffer to time them, the total passes 2 s. The vendor
driver must avoid that somehow (splitting command buffers into smaller submissions, or a context
or preemption setting that KGSL treats differently). That is step 3's question, which also covers
the frozen UI.

### A side finding: Turnip's cost per submission

In the runner's timings (one dispatch per submission, waited for), tiny dispatches take **5-8 ms
on Turnip** and **1.7-3.5 ms on the vendor driver** (softmax over 1,000 elements: 6.2 against
3.5 ms; pad: 8.1 against 1.8 ms). So Turnip seems to pay several milliseconds per submit-and-wait
on KGSL, which matters for frame pacing (stage 1.1 saw FIFO at 68.9 fps). For 1.4.

## The wrong results: Geekbench's real dispatches, recorded and replayed (2026-11-18)

### A Vulkan layer of our own

[`layer/dispatchlog.c`](../../tools/ut-probe/vkcompute/layer/dispatchlog.c) (`VK_LAYER_UTDECK_dispatchlog`,
built by `vkcompute/build.sh` with its JSON manifest) sits between the program and the driver and
records, without changing anything: every shader module (SPIR-V saved once, named by a 64-bit
FNV-1a hash), each compute pipeline's specialization constants, buffer sizes, descriptor writes
(direct, templated or pushed), bound sets and dynamic offsets, push constants, and one line per
`vkCmdDispatch` with all of it. Enabled through Ubuntu's Khronos loader with `VK_LAYER_PATH`,
`VK_INSTANCE_LAYERS=VK_LAYER_UTDECK_dispatchlog` and `UTDECK_DISPATCHLOG_DIR`. Its lookup functions
return NULL for anything the driver below does not offer, so it never pretends a feature exists.
Checked first on `spvrun` (push constants, buffers, specialization constants and the saved shader
all as given).

Geekbench under the layer on Turnip, stopped after Face Detection (which still failed): 13,294
dispatches, 1,045 pipelines, 35 shaders (all 35 matched by content to the earlier Mesa dump).
Face Detection's part (from the first pipeline built from one of its own shaders): **4,343
dispatches in 384 distinct configurations** at real sizes (the first pad dispatches use buffers of
about 39 MB). All bindings are storage buffers in set 0, numbered 0, 1, 2, ... with no offsets or
partial ranges, which `spvrun` handles; it gained `--push-hex` (the logged bytes) and `--hash` (a
checksum of every buffer after the run, so outputs are compared on the phone).

### Replay: all 384 configurations on both drivers

`fd-replay.sh` (generated on the PC from the log) runs each configuration on Turnip and on the
vendor driver with every buffer filled from the same seed, and records each buffer's checksum.
Result: **368 identical, 16 different**, all 16 in five tiny shaders: a plain copy
(`9d0897b954`, 8 configurations; `19ba55e632`, 5), a clamp to [0, 6] (`692f32b4cd`, 1), an addition
(`af11824a6d`, 1) and the softmax (1). A copy cannot differ by rounding, so these were wrong
outputs, not precision. **Every one of the 16 uses a local size of 2048x1x1.**

### The cause: a work-group wider than the GPU allows

| | `maxComputeWorkGroupSize` | `maxComputeWorkGroupInvocations` |
| --- | --- | --- |
| Turnip | 1024, 1024, 1024 | **2048** (with the A619's double thread size) |
| Vendor V@530 | 1024, 1024, 1024 | 1024 |

[`wgsize.comp`](../../tools/ut-probe/vkcompute/wgsize.comp), our own shader (each invocation writes
its own slot; local size from specialization constants), four work-groups per shape:

| Local size | Turnip | Vendor |
| --- | --- | --- |
| 256x1, 1024x1 | all slots written | all slots written |
| 512x2 | all | all |
| **1024x2, 512x4, 256x8** (2048 invocations, valid on Turnip) | **all** | all |
| **2048x1** (x over the 1024 limit: invalid on both) | **half** (4,096 of 8,192) | **half** (4,096 of 8,192) |

So the GPU runs 2048-invocation work-groups correctly when the shape is valid, and neither driver
can make one wider than 1024 in x, which is why both report `maxComputeWorkGroupSize[0]` = 1024.
**Geekbench's tuner breaks a Vulkan rule** (the work-group's x size must not exceed
`maxComputeWorkGroupSize[0]`): it seems to check only the total-invocation limit, so on Turnip
(2048) it tries and keeps 2048x1x1, and half of each such work-group's work is lost. On the vendor
driver (limit 1024) it never tries that size, which hides the bug. Turnip and the hardware behave
within the specification.

The test: Turnip's existing option `tu_restrict_subgroup_size_64=true` (honoured as an environment
variable in our build) turns off the double thread size, and `maxComputeWorkGroupInvocations` drops
to 1024, as on the vendor driver.

**Result (12:24-12:40): every workload validates.** Full Geekbench run on our default build
(0001 + 0002) with `tu_restrict_subgroup_size_64=true`: all eight workloads, Face Detection and
Feature Matching included, with no validation error; exit 0; uploaded
([6978142](https://browser.geekbench.com/v6/compute/6978142); score to read by hand). **The wrong
results were Geekbench's out-of-range work-group size, not a Turnip bug.**

What this means:
- For UTDeck's games: Direct3D caps a compute work-group at 1024 threads and 1024 in x, so DXVK and
  VKD3D-Proton never ask for 2048x1. Native Vulkan games choose fixed sizes. No change needed for
  games; nothing in Turnip to fix.
- For Geekbench on Turnip: `tu_restrict_subgroup_size_64=true` is a working workaround (it also
  limits subgroups to 64 lanes, a possible small cost elsewhere). Mesa normally ships such
  per-application workarounds in its driconf file, which our build leaves out (`-Dxmlconfig=disabled`).
- Upstream: a report to Primate Labs (the tuner should check `maxComputeWorkGroupSize[0]`, not only
  the invocation limit). Optional, for the maintainer to send.

## Step 3: how the vendor driver submits work, and the frozen UI

### A tracer of our own

`strace` cannot decode KGSL's ioctl structures, so
[`kgsl-trace.c`](../../tools/ut-probe/vkcompute/kgsl-trace.c) runs a program under `ptrace`, follows
all its threads and children, and decodes context creation (flags, priority, preemption style,
type), `SETPROPERTY`, and every `IOCTL_KGSL_GPU_COMMAND` (flags, command buffers and their sizes,
timestamp); every other KGSL ioctl is counted by number (names generated from Mesa's copy of
`msm_kgsl.h`). It watches system calls, so it works the same for Turnip and for the vendor driver
through libhybris. First version killed its child at `execve` (the kernel's SIGTRAP after exec was
passed on): fixed with `PTRACE_O_TRACEEXEC`.

### One blur dispatch per submission, both drivers (`spvrun`, 1920x1080, diameter 63, 3 dispatches)

| | Turnip (ours, 26.2.5) | Vendor V@530 |
| --- | --- | --- |
| Context flags (as the kernel left them) | `0x00008052`: NO_GMEM_ALLOC, PREAMBLE, PER_CONTEXT_TS, priority 8, preemption style 0, **type 0 (any)** | `0x005080d2`: the same plus USER_GENERATED_TS, priority 8, preemption style 0, **type 5 (Vulkan)** |
| Submission | `GPU_COMMAND` flags `0x4` (SUBMIT_IB_LIST), 1 IB of 756 bytes, timestamps assigned by the kernel | flags `0x8` (CTX_SWITCH), 1 IB of 256 bytes, its own timestamps |
| Waiting | `DEVICE_WAITTIMESTAMP_CTXTID` (a blocking wait in the kernel) | `TIMESTAMP_EVENT` + sync sources (a fence it can poll) |

### What KGSL does with those (stone's kernel source, `stone-ut/third_party/android_kernel_xiaomi_sm6375`, `drivers/gpu/msm`)

- **The 2 s timeout** (`adreno_dispatch.c`): the timer restarts whenever any submission on the
  ringbuffer retires; a timeout fires only when one submission runs 2 s without one retiring. The
  context type plays no part; only `KGSL_CONTEXT_NO_FAULT_TOLERANCE` turns the check off. So the
  question is how long each single submission runs.
- **Priority** (`adreno_drawctxt.c`, `adreno.h`): a context without a priority gets
  `KGSL_CONTEXT_PRIORITY_MED` = 8, which is why both drivers show 8 (Turnip asks for none). Priority
  / 4 picks one of four ringbuffers: 0-3 ring 0 (highest), 4-7 ring 1, 8-11 ring 2, 12-15 ring 3.
  Preemption (`preemption=1` on this phone) switches only **between** ringbuffers. Everything left at
  the default, very likely Lomiri too, shares ring 2, so a long submission there makes the
  compositor wait: the frozen UI.
- **Turnip ignores Vulkan queue priority on KGSL**: `kgsl_submitqueue_new` sets no priority bits,
  and the KGSL backend reports one priority level (`submitqueue_priority_count = 1`), although
  Turnip advertises `VK_EXT_global_priority`.

### Geekbench under the tracer, vendor driver (stopped after Gaussian Blur, 10:41-10:54)

26,238 `GPU_COMMAND` submissions, **every one a single 256-byte IB** (most likely a jump into the
real command stream), 3,842 blocking waits, 10,250 buffer allocations. Gaussian Blur completed. The
same trace for Turnip, to compare how it packages the same work, is next; it did not start (below).

### The preemption test (2026-11-18, `preempt-test.sh`)

A long Turnip job (Geekbench's horizontal blur, 4000x3000, diameter 127, local 32x1, four
dispatches) while a probe submits a tiny dispatch through the vendor driver every 50 ms, standing in
for the compositor. Files are now sent with `adb shell 'cat > file' < file` and a checksum, not
`adb push` (all three `adbd` crashes came with `adb push`).

| Turnip context priority | Long job, per dispatch | Probe: best / worst | Probe dispatches over 100 ms |
| --- | --- | --- | --- |
| default (8, ring 2, the same as the probe) | 807-848 ms | 0.28 / **755.5 ms** | 3 of 70 (one per long dispatch: the probe waits until it ends) |
| **12** (ring 3, lower), `UTDECK_TU_KGSL_PRIORITY=12` | 815-848 ms | 0.31 / **2.3 ms** | **0** of 70 |

`kgsl-trace` confirmed the context: `flags 0x0000c052 (... priority=12 ...)`.

**This is the frozen UI, measured, and its fix.** At the same priority, other GPU work, the
compositor included, waits until Turnip's submission ends (up to 0.76 s here, up to 2 s before KGSL
gives up). With Turnip one ring lower, KGSL preempts it at once: the probe's worst wait fell from
755 ms to 2.3 ms, and the long job lost about 1 %. For UTDeck this means: **games run below the
compositor's priority**. The proper fix is in Turnip: pass Vulkan's queue priority
(`VK_EXT_global_priority`) to KGSL, as the `msm` backend does, so UTDeck can ask for a low priority
the standard way. A candidate for upstream Mesa.

### Geekbench under the tracer, Turnip (stopped by the fault in Gaussian Blur, 10:57-11:08)

| | Kernel submissions (`GPU_COMMAND`) | Command buffers (IBs) sent | IBs per submission |
| --- | --- | --- | --- |
| Vendor driver | 26,238 | 26,238 | always **1** |
| Turnip | 4,778 | 30,229 | **several**: 5 in the last ones (760 bytes each) |

**This explains the timeout.** Geekbench hands several command buffers to each `vkQueueSubmit`.
The vendor driver turns each command buffer into its own kernel submission; each one retires and
restarts KGSL's 2 s timer. Turnip packs them all into **one** kernel submission. Just before the
fault, Turnip's submissions came every 0.81-0.88 s, 5 command buffers each (about 0.16 s per
command buffer, the tuner timing a fast work-group size); the fault followed the next one, most
likely the first and slowest size of the vertical pass, where 5 passes in one submission run past
2 s. Turnip is not slower: it batches the work into submissions KGSL cannot let run.

### The fix for the frozen UI: patch 0001 (2026-11-18)

[`tools/mesa-build/patches/0001-tu-kgsl-honour-queue-global-priority.patch`](../../tools/mesa-build/patches/0001-tu-kgsl-honour-queue-global-priority.patch),
a real fix, not a diagnostic (applied by default by `build-turnip.sh` now):
- the KGSL backend reports 4 queue priorities (`submitqueue_priority_count = 4`), one per KGSL
  ringbuffer, so Turnip's existing mapping gives REALTIME 0, HIGH 1, MEDIUM 2, LOW 3, and apps are
  offered LOW, MEDIUM and HIGH (`VK_KHR_global_priority`);
- `kgsl_submitqueue_new` passes that on as KGSL priority 0, 4, 8 or 12, one level per ringbuffer.
  MEDIUM becomes 8, which is what the kernel picks for a context without a priority, so apps that
  ask for nothing behave exactly as before.

Checked with `kgsl-trace` and our harness asking the standard way (`VKC_GLOBAL_PRIORITY`, a
`VkDeviceQueueGlobalPriorityCreateInfoKHR`): none → 8, low → 12, medium → 8, high → 4. Preemption
test with this build (`turnip-split`, which has 0001):

| Game side | Probe: best / worst |
| --- | --- |
| no priority asked | 0.20 / 765.98 ms (unchanged) |
| **`VK_QUEUE_GLOBAL_PRIORITY_LOW`** | 0.21 / **3.15 ms** |

For UTDeck: games' Vulkan queues ask for LOW (by DXVK's own setting if it has one, or by a small
implicit Vulkan layer of ours that adds the request at `vkCreateDevice`, for any app), and the
compositor stays responsive whatever the game does. Upstream note: HIGH is offered to any app here
(KGSL does not restrict priorities); upstream reviewers may want only LOW and MEDIUM exposed.

### Testing the timeout fix: diagnostic 0004

[`0004-tu-kgsl-diag-one-submission-per-command-buffer.patch`](../../tools/mesa-build/patches/diag/0004-tu-kgsl-diag-one-submission-per-command-buffer.patch):
with `UTDECK_TU_KGSL_SPLIT_SUBMIT=1`, each `submit_add_entries` call (one command buffer's stream,
or a small extra stream) becomes its own `GPU_COMMAND` on the same context: the wait on the first,
the profiling object and the timestamp on the last. Built with 0001 as `build/turnip-split`
(SHA-256 `4ff8241f...7123`). Files now go to the phone through `adb shell 'cat > file'` with a
checksum: no `adbd` crash since.

**Result: the timeout is gone.** Full Geekbench run with `UTDECK_TU_KGSL_SPLIT_SUBMIT=1`
(`turnip-split`, 11:12-11:29): **all eight workloads ran to the end**, exit 0, no KGSL message,
result uploaded ([6978037](https://browser.geekbench.com/v6/compute/6978037); score to read by
hand). It is the first complete Geekbench Vulkan run on Turnip on this phone. Two workloads
failed validation: **303 (Face Detection)**, as always, and **503 (Feature Matching)**, which no
Turnip run had reached before. The vendor driver validated every workload. So one kernel submission
per command buffer fixes the timeout, and does not fix the wrong results.

### The fix for the timeout: patch 0002 (2026-11-18, maintainer's decision)

Diagnostic 0004 made into a real patch,
[`tools/mesa-build/patches/0002-tu-kgsl-one-submission-per-command-buffer.patch`](../../tools/mesa-build/patches/0002-tu-kgsl-one-submission-per-command-buffer.patch):
always on (no environment variable), comments written for upstream, generated on top of 0001 so
both apply in order. `build-turnip.sh` applies both by default; `BUILD-INFO` now lists the patch
names on one line. Diagnostic 0004 stays in `patches/diag/` as the record of the test.

Built as `build/turnip-utdeck` (`NAME=utdeck`, default patches; SHA-256 `800ff34d...c031`), sent
to the phone's `turnip-utdeck` folder. Checks:

| Check | Result |
| --- | --- |
| Presentation (`vkcube`, `WAYLAND_DEBUG`, screen off) | `android_wlegl` buffers, no `wl_shm` buffers; 1.00 ms CPU per frame (FIFO), 0.68 ms (MAILBOX): as before |
| `intops` | 66,060,288 results, 0 wrong |
| Preemption test, game side LOW | probe worst 3.7 ms (0 of 70 over 100 ms) |
| **Full Geekbench Vulkan run, no switches, traced** (11:41-11:58) | **all eight workloads to the end**, exit 0, no KGSL message, uploaded ([6978084](https://browser.geekbench.com/v6/compute/6978084)). **43,409 kernel submissions for 43,409 command buffers: exactly one each**, as the vendor driver does. Workloads 303 and 503 still fail validation |

**Scores, Turnip (6978084) against the vendor driver (6977517)**, read from the result pages by the
maintainer:

| Workload | Vendor V@530 | Turnip (0001 + 0002) | Turnip / vendor, throughput |
| --- | --- | --- | --- |
| Background Blur | 523 (2.16 images/s) | 775 (3.21 images/s) | **1.49** |
| Face Detection | 335 (1.09 images/s) | **0**, failed validation (1.70 images/s) | 1.56 |
| Horizon Detection | 1884 (58.6 Mpixels/s) | 1757 (54.7 Mpixels/s) | 0.93 |
| Edge Detection | 2006 (74.4 Mpixels/s) | 2057 (76.3 Mpixels/s) | 1.03 |
| Gaussian Blur | 1312 (57.2 Mpixels/s) | 1473 (64.2 Mpixels/s) | **1.12** |
| Feature Matching | 391 (15.4 Mpixels/s) | **0**, failed validation (16.4 Mpixels/s) | 1.06 |
| Stereo Matching | 661 (628.3 Mpixels/s) | 5050 (4.80 Gpixels/s) | **7.64** |
| Particle Physics | 4587 (201.9 FPS) | 2727 (120.0 FPS) | **0.59** |
| **Vulkan score** | **1004** | **295** | |

- Turnip's total is low only because the two failed workloads score 0 and Geekbench takes a
  geometric mean. By throughput Turnip leads in six of eight; the geometric mean of the eight
  ratios is about 1.36 (about 1.07 without Stereo Matching). With both workloads validating, its
  score would be roughly 1,100-1,400: an estimate, not a measurement.
- **To check:** Stereo Matching's factor of 7.6 is too large to take at face value (Turnip skipping
  work, or a slow path in the vendor driver; Geekbench validated it). **Particle Physics** is
  Turnip's one clear loss (-41 %), and particle effects are common in games: for 1.4, with the
  per-submission cost found earlier.

So our default Turnip build now: presents on Lomiri without CPU copies (stage 1.1), lets a game ask
for a low GPU priority so the compositor is never blocked (0001), and never merges command
buffers into a submission KGSL would kill (0002). Left open in 1.2: the wrong results in 303 and
503.

### Prepared earlier

- Diagnostic patch
  [`0003-tu-kgsl-diag-context-priority.patch`](../../tools/mesa-build/patches/diag/0003-tu-kgsl-diag-context-priority.patch):
  `UTDECK_TU_KGSL_PRIORITY=<0-15>` sets the KGSL context priority. Built as `build/turnip-prio`
  (SHA-256 `e6dcf954...16b0`) and pushed.
- `spvrun --interval MS` (a pause between repeated dispatches; prints when each started, and the
  worst time). The **preemption test**: a long Turnip job (blur 4000x3000, diameter 127, about 0.9 s
  per dispatch, below the limit) while a second program submits a tiny dispatch through the vendor
  driver every 50 ms, standing in for Lomiri. If the tiny dispatches wait about 0.9 s, the
  compositor waits behind the game; if they stay at a few milliseconds with Turnip at priority 12
  (ring 3), a lower game priority keeps the UI responsive. The real fix would then be to map
  Vulkan's global priority to KGSL priorities in Turnip (an upstream candidate).

Hypothesis 2, for the timeout, as first written: the dispatches are just slower on Turnip than on the
vendor driver, at the work-group sizes Geekbench tries first. The SPIR-V dump
(`MESA_SPIRV_DUMP_PATH`, one file per shader, named by its BLAKE3 hash) and ir3's disassembly
(`IR3_SHADER_DEBUG=cs`: register count, spills, loops) of the blur shaders show whether the compiled
shader is unusually heavy. Geekbench's shaders belong to Primate Labs: they stay on the phone and the
PC, never in this repository or a public report.

## Problems met

- **The runner's status file landed in `/home/phablet/`**: a redirection on a `{ ... }` group is
  opened before the `cd` inside the group runs. Fixed with an absolute path.
- **The phone dropped off USB twice: `adbd` crashed (software, not the cable).** The phone's
  journal, at both times (01:19:14 and 01:39:50): a new adb session opens, then
  `adbd.service: Main process exited, code=killed, status=11/SEGV`; `mtp-server` loses its USB
  endpoint (`request read returned -1: Input/output error`) at the same moment; systemd restarts
  `adbd` within 0.5 s, but the new one logs `timed out while waiting for FUNCTIONFS_BIND` and
  `UsbFfs: connection terminated`, so the USB gadget never binds again until the cable is replugged.
  Both crashes came at the start of an adb session right after an `adb push`; one while Geekbench was
  loading the GPU, one at idle. These are the only two `adbd` SEGVs in the journal since 2026-11-17.
  Package: `adbd 1:14.0.0+r45-0ubports1` (UBports). No core dump (no `coredumpctl`, no
  `/var/crash`). This belongs to the stone-ut port, not to UTDeck. The maintainer also reports
  that during the hung Geekbench runs the phone's UI is extremely slow or frozen.
- **A third `adbd` crash** (2026-11-18, about 10:56 phone time): during a sequence of `adb push`
  commands (the 15 MB priority build, then two small files, then `spvrun`), before the next
  `adb shell`. All three crashes came during or right after `adb push`, at idle or under load.
- How runs work around it: one push per command, then a short pause; few adb calls while a run is
  going (poll once a minute); results read from files after the run.
- **`adbd`'s restart kills detached runs**: when the phone reconnects, systemd restarts `adbd` and
  stops every process in its cgroup; `setsid nohup` leaves the session but not the cgroup. Runs
  now start as user units (`systemd-run --user --unit=... --collect`), which live under
  `user@32011.service` (checked in `/proc/<pid>/cgroup`).

## Events

- 2026-11-18, later: diagnostic 0002 run: 303 still fails, so not stale reads; 35 shaders dumped.
  Face Detection's shaders analysed (signed division and modulo everywhere). `STOP_AFTER` added to
  the runner (3-minute tests); control and a six-switch ir3 sweep: all fail. Our `intops` test
  written and built: Turnip passes 66 million results; the vendor driver fails `OpSMod` with mixed
  signs. Step 1 closed without a culprit; next step for the maintainer to choose.
- 2026-11-18, the wrong results (maintainer's go): Vulkan layer `dispatchlog` written and checked;
  Geekbench's Face Detection recorded on Turnip (4,343 dispatches, 384 configurations); all 384
  replayed on both drivers: 16 differ, all at local size 2048x1x1; `wgsize.comp` shows 2048x1 loses
  half its work on both drivers while valid 2048-invocation shapes work on Turnip; with
  `tu_restrict_subgroup_size_64=true` a full Geekbench run validates every workload. Cause:
  Geekbench's tuner exceeds `maxComputeWorkGroupSize[0]`. Stage 1.2 closed.
- 2026-11-18, patch 0002 (maintainer's decision): 0004 made into an always-on patch; default build
  `turnip-utdeck` (0001 + 0002) built and checked; a full Geekbench run with no switches completes,
  one command buffer per kernel submission; 303 and 503 still fail.
- 2026-11-18, step 3 continued (phone replugged): files now sent by shell stream with checksums.
  Preemption test: a default-priority Turnip job makes a compositor-like job wait 755 ms; at
  priority 12, 2.3 ms. Geekbench traced on Turnip: several command buffers per kernel submission
  (4,778 submissions for 30,229 command buffers) against one each on the vendor driver: the cause
  of the timeout. Patch 0001 (queue priority to KGSL) written, built and checked (LOW: 3.15 ms
  worst wait; default unchanged). Diagnostic 0004 (one submission per command buffer): full
  Geekbench run completes for the first time; 303 and 503 (Feature Matching) still fail
  validation.
- 2026-11-18, step 3: `kgsl-trace` written (exec fix); single-dispatch traces of both drivers
  compared; KGSL's timeout and priority logic read in stone's kernel; Turnip found to ignore queue
  priority on KGSL; diagnostic 0003 (context priority) and `spvrun --interval` built; Geekbench
  traced on the vendor driver (26,238 single-IB submissions). Pushing the priority build crashed
  `adbd` a third time before the Turnip trace started.
- 2026-11-18, later still (maintainer chose the runner): no `RelaxedPrecision` in any shader;
  shaders mapped to workloads; `spvrun` and `spvcmp.py` written; GEMMs and all five Face
  Detection shaders bit-identical on both drivers (at all 27 local sizes for the five); the
  unchecked GEMM's difference traced to out-of-bounds reads in the test; `TU_DEBUG=flushall` and
  `syncdraw` do not fix Face Detection. Blur timing: Turnip about 15 % faster than the vendor driver,
  so hypothesis 2 is wrong and the timeout points at how work is submitted (step 3). Turnip's cost
  per submission noted.
- 2026-11-18: Geekbench's single-workload switches found to need Pro. Memory types compared (all
  three Turnips offer cached non-coherent memory). Diagnostic patch and build-script support for
  patches written; diagnostic build made and pushed. Baseline run on our build started; the phone
  then dropped off USB (the maintainer replugged it; it had kept running). Runs moved to user
  units. Baseline on our build: same two failures. Diagnostic 0001 unusable (Geekbench needs cached
  memory); diagnostic 0002 written, built and pushed; the phone dropped off USB again before the run
  started.
