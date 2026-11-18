# Stage 0: probe and baseline

Started 2026-11-17 and closed 2026-11-18 on the Redmi Note 12 5G (stone). Goal (from
[feasibility-and-roadmap.md](feasibility-and-roadmap.md), stage 0): know the ground before
building anything.

How it was done: over adb as `phablet`. No root, nothing installed system-wide, nothing flashed.
Test files live only in `/home/phablet/utdeck-probe/` on the phone. The probes are in
[`tools/ut-probe/`](../../tools/ut-probe/).

| Item | Value |
| --- | --- |
| Phone | Redmi Note 12 5G, `stone`, SM4375, Adreno 619v2 |
| System | Ubuntu Touch 24.04-2.x, `daily` channel, Ubuntu 24.04.5 |
| Kernel | `5.4.283~positron #20`, built 2026-11-12 (stone-ut) |
| Shell | Lomiri 0.5.0, Mir 1.8.3, Qt 6.10.2 |
| libhybris | `0.1.0+git20260624+7079712` |

## Results at a glance

| # | Question | Answer | What it changes |
| --- | --- | --- | --- |
| 1a | 4 KiB pages? | **Yes** | Wine's ARM64EC and FEX need them |
| 1b | Address space | **39 bits** (512 GB per process) | Wine before 11.15 crashes at start on such kernels unless built with `-static-pie` (Wine bug 59898). FEX on 39 bits: check in stage 2 |
| 1c | Fast Windows synchronization | No NTSYNC, **no fsync** (`futex_waitv` missing), esync possible (`eventfd`) | The runtime must be Proton-based (esync); upstream Wine falls back to the slow `wineserver` path |
| 1d | `userfaultfd` | **Missing** | Proton's fast write-watch path (used by DroidDeck) is unavailable; Wine falls back to its slower method |
| 1e | User namespaces | **Yes**, unprivileged (`unshare -Ur` works as phablet) | Steam's game container (pressure-vessel) is possible later |
| 1f | Virtual input devices (`/dev/uinput`) | **No access for apps**: `root:3011`, mode 0660, phablet not in group 3011 | Touch controls cannot become a virtual gamepad; touch has to reach Wine directly (risk 4 confirmed) |
| 1g | Touchscreen and gamepads (`/dev/input`) | Readable (phablet is in `android_input`) | An unconfined click can read them, as Dolphin does |
| 1h | GPU device | `/dev/kgsl-3d0`, mode 0666 | Turnip can open it from any app |
| 1i | CPU | 6× Cortex-A55 (1.80 GHz) + 2× Cortex-A78 (2.02 GHz), LSE atomics and RCPC, no LSE2 or RCPC2 | FEX has what it needs for x86 memory ordering, but not the newer instructions that make it cheaper |
| 2 | Does Lomiri hand out gralloc buffers over `android_wlegl`? | **Yes**: linear dma-bufs with exactly the layout the Droidian Turnip patch checks | **Route B is open**: native Turnip can present on Lomiri without CPU copies (stage 1) |
| 3 | Memory and CPU at idle | 1.5 GB used, 5.9 GB available of 7.3 GB; CPU about 94 % idle | Room for Wine and a game. Telephony keeps running |
| 4a | Does the Linux (glibc) Turnip load and create a device? | **Yes**: "Turnip Adreno (TM) 619", Vulkan 1.3.368, no libhybris in the process | The route B driver works on this Halium kernel |
| 4b | Does it present on Lomiri? | Not with Mesa's normal Wayland path (`VK_ERROR_SURFACE_LOST_KHR`). **Yes** with software presentation: 32.7 fps at about 22 ms of CPU per frame (screen on), FIFO only, and torn when the row stride is padded (portrait) | The `android_wlegl` patch is needed (stage 1). Software presentation is a slow but working fallback |
| 4c | DXVK requirements | DXVK **2.7.1: all 41 required features present**. DXVK 3.1.1: missing 16-bit and 8-bit storage buffer access | Pin DXVK 2.7.x on Adreno 6xx, as ARMDeck and DroidDeck do |
| 5a | Baseline against Android | **CPU: same as Android** (Geekbench 6.7.1: 866 single-core, 1973 multi-core; Android references 841-852 and 1875-2048). GPU, vendor driver through libhybris: 1004 Vulkan (references 988 and about 1,160, neither a clean match) | The OS costs nothing on the CPU; gains over DroidDeck must come from removing its layers, not from Ubuntu Touch itself |
| 5b | Turnip under a compute load | **Fails, on both Turnips** (Mesa 27.0 Linux build and the maintainer's 25.3.0 R11 Android build): a wrong result, then a GPU timeout and `VK_ERROR_DEVICE_LOST`. The vendor driver passes the same test | Turnip's compute on the A619 is a stage 1 item, before Wine work leans on it |

## 1. Kernel and system

Read from `/proc/config.gz`, `/dev`, `sysctl`, and by calling the system calls directly
(`syscall()` through Python's ctypes: `ENOSYS` means the kernel does not have the call).

| Feature | State | Notes |
| --- | --- | --- |
| Page size | 4096 | `getconf PAGESIZE` |
| `CONFIG_ARM64_VA_BITS` | 39 | See "39-bit address space" below |
| `CONFIG_USER_NS` | y | `user.max_user_namespaces = 28886`; `unshare -Ur` as phablet prints uid 0 |
| `CONFIG_SECCOMP_FILTER` | y | Needed by bubblewrap (pressure-vessel) |
| `futex_waitv` (449) | ENOSYS | No fsync |
| NTSYNC | not in the kernel, no `/dev/ntsync` | The driver exists only since Linux 6.14 |
| `CONFIG_EVENTFD` | y | esync is possible. File descriptor limit: 1024 soft, 1048576 hard (esync raises the soft one itself) |
| `CONFIG_USERFAULTFD` | not set | `userfaultfd` (282) ENOSYS |
| `CONFIG_MEMFD_CREATE` | y | Dolphin's click needed it (stage 5 there) |
| `io_uring` | y | |
| `clone3`, `pidfd_open`, `process_madvise` | present | `close_range`, `openat2`, `epoll_pwait2`, `memfd_secret`: ENOSYS (glibc falls back where needed) |
| `CONFIG_BINFMT_MISC` | y, not mounted | Not needed: Wine is native ARM64 and the emulators are Wine DLLs |
| `CONFIG_INPUT_UINPUT` | y | `/dev/uinput` is `root:3011 0660`. Group 3011 has no name on the Linux side; it is Android's `uhid` group id. `/dev/uhid` is the same |
| `CONFIG_INPUT_EVDEV` | y | `/dev/input/event*` `root:android_input 0660` |
| `CONFIG_INPUT_JOYDEV` | not set | SDL and Wine use evdev, so no loss |
| `CONFIG_HIDRAW` | y | No `/dev/hidraw*` now (no pad connected). Check permissions with a pad: SDL's HIDAPI drivers use them |
| HID drivers | `sony`, `playstation`, `nintendo`, `steam`, `microsoft` built in | |
| `CONFIG_QCOM_KGSL` | y | `/dev/kgsl-3d0` `system:system 0666`. `/dev/dri/card0` and `renderD128` belong to the display controller only (stone-ut item 10) |
| `CONFIG_UCLAMP_TASK` | y | Dolphin's CPU boost works the same way here |
| Preemption, timer | `CONFIG_PREEMPT`, HZ 250 | |
| Transparent huge pages | not set | A small loss for FEX and big games; nothing to do on a vendor kernel |
| Memory | zram swap 2 GB, swappiness 60, `vm.overcommit_memory = 1`, `vm.mmap_min_addr = 32768` | Wine wants `mmap_min_addr` at 65536 or lower: fine |

CPU: `cpu0`-`cpu5` are Cortex-A55 (`0xd05`, up to 1804.8 MHz), `cpu6`-`cpu7` Cortex-A78 (`0xd41`, up
to 2016 MHz), all with `schedutil`. CPU features: `fp asimd evtstrm aes pmull sha1 sha2 crc32
atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp`. GPU: `Adreno619v2`, 266-700 MHz,
`msm-adreno-tz`.

### Parked cores: programs see 6 CPUs, not 8

Found during the Geekbench run (section 5): its thread library printed "The number of workers is
currently limited to 5. The request for 7 workers is ignored."

- `nproc` says **6** while `getconf _NPROCESSORS_ONLN` says 8 and `/sys/devices/system/cpu/online`
  is `0-7`.
- Qualcomm's `core_ctl` (the vendor kernel's core parking) is on for the little cluster:
  `min_cpus 4`, `max_cpus 6`, `busy_up_thres 60`, `busy_down_thres 40`. At idle it keeps 4 little
  cores active and **isolates** the other 2. It is off for the two big cores (both always active).
- An isolated core is left out of `sched_getaffinity()`, which is what `nproc`, glibc's
  `sysconf`-based helpers, `std::thread::hardware_concurrency()` and most thread pools read. So a
  program started at idle sizes its worker threads for 6 CPUs and keeps that number, even after
  `core_ctl` wakes the other cores under load.
- For UTDeck this matters beyond benchmarks: Wine, DXVK's worker threads, FEX and game engines'
  job systems size themselves at start the same way. Options for stage 3: tell Wine the topology
  (`WINE_CPU_TOPOLOGY`), keep all cores active during a game (`min_cpus` 6, the vendor's own
  maximum; root, so a system service on our ports, never a click), or start the game while the CPU
  is busy so `core_ctl` has woken every core. Measure which one matters.
- The Android power HAL runs in the Halium container (`android.hardware.power-service.lineage-libperfmgr`),
  but nothing on Ubuntu Touch sends it Android's hints (LAUNCH, INTERACTION, GAME). Its
  `/vendor/etc/powerhint.json` is a generic one (GPU 840 MHz, big cores 2054.4 MHz, above this
  phone's 700 MHz and 2016 MHz) and none of its hints touch `core_ctl`, so Android on this vendor
  parks cores the same way. What Ubuntu Touch lacks is the GAME hint's raised minimum clocks;
  Dolphin's `uclamp_min` boost does that job per app.

### The 39-bit address space

The kernel gives each process 512 GB of address space instead of the usual 256 TB.

- **Wine:** bug [59898](https://bugs.winehq.org/show_bug.cgi?id=59898) ("Wine on aarch64 fails on
  linux with CONFIG_ARM64_VA_BITS=39", 2026-06): Wine's preloader, when linked at a fixed address
  (`-Ttext=0x7d7d00000000`, a 47-bit address), dies before any code runs. That happens when the
  toolchain lacks `-static-pie`. Fixed by commit `b969dd675a0d`, released in **Wine 11.15**. So
  UTDeck needs Wine 11.15 or later, or a build made with `-static-pie` (Ubuntu 24.04's GCC has
  it). Hangover 11.16 is past the fix.
- **x86-64 games:** they expect up to 128 TB. A few engines reserve huge ranges up front; those
  could fail here. To watch in the compatibility table.
- **FEX:** DroidDeck runs FEX on Android phones, many of which have 39-bit kernels, so it most
  likely works. Not confirmed in FEX's documentation; stage 2 checks it.

## 2. Lomiri's `android_wlegl` buffers

Lomiri offers no `zwp_linux_dmabuf_v1`, so a normal Linux Vulkan driver cannot hand it frames
without CPU copies. The Droidian Mesa patch (`panxiao81/debian-mesa` commit `adb4b85`, "present
through android_wlegl gralloc buffers") asks the compositor for a buffer with
`android_wlegl.get_server_buffer_handle` (protocol version 2). It takes the first file descriptor
as the image memory (a dma-buf), checks that its size covers `stride × 4 × height`, and blits the
frame into it on the GPU. It uses HAL formats 1 (RGBA_8888), 2 (RGBX_8888) or 5 (BGRA_8888), and
usage `0x300` (GPU texture + GPU render).

[`wlegl-probe.py`](../../tools/ut-probe/wlegl-probe.py) makes the same request over Lomiri's
socket, in plain Python (the phone has no compiler and no Wayland tools):

| Request | Reply |
| --- | --- |
| 1080x2400 RGBA_8888 | wl_buffer, format 1, **stride 1088 px**, 2 fds (both `/dmabuf:`), 22 ints. fd[0] is 10,444,800 bytes = 1088 × 4 × 2400 exactly |
| 1080x2400 BGRA_8888 | format 5, stride 1088, same sizes |
| 1280x720 RGBA_8888 | stride 1280, fd[0] 3,686,400 bytes = 1280 × 4 × 720 |
| 2400x1080 RGBX_8888 | format 2, stride 2432, fd[0] 10,506,240 bytes = 2432 × 4 × 1080 |

- The second fd is always 32,768 bytes: gralloc's metadata.
- The size is exactly stride × 4 × height, with the stride rounded up to 64 pixels: a plain
  **linear** image, not Qualcomm's compressed (UBWC) layout. This is what the patch assumes.
- Both fds can be mapped, and `DMA_BUF_IOCTL_SYNC` works on fd[0].
- No protocol error in any request.

Not checked yet: that Lomiri actually shows such a buffer (manual test M1), and how the GPU writes
into it (stage 1).

## 3. Memory and CPU at idle

Measured 3 h after boot, screen off, no app open:

- 7,434 MiB total, **1,504 MiB used, 5,930 MiB available**, swap unused. 589 tasks, load 0.06-0.11,
  CPU about 94 % idle.
- Largest processes: `lomiri` 314 MB, `maliit-server` (keyboard) 116 MB, the display composer HAL
  106 MB, `media-hub` 59 MB, `lomiri-system-compositor` 53 MB. Telephony, location, address book
  and calendar services all run: the phone stays a phone.
- The Halium container (`lxc-start`, the Android vendor services) runs. Waydroid does not.
- Storage: 84 GB free on `/` (the home folder is on the same partition).
- Temperature sensors for later runs: `cpu-0-*-usr`, `cpu-1-*-usr`, `cpuss-*-usr`, `gpuss-0-usr`,
  `gpuss-1-usr` in `/sys/class/thermal/` (about 30-31 °C at idle).

## 4. Turnip for Linux (glibc) on this kernel

Driver: [Droid-Deck/Drivers](https://github.com/Droid-Deck/Drivers) `DD-Turnip-v0.7.0` (2026-11-13),
"Mesa 27.0.0-devel (beaf3192c2)". The zip's SHA-256 matched the published one
(`502ea8aa...a99a`), and the Linux library matched its `meta.json` (`1e52620e...37bf`). It links only
libraries the phone already has (`ldd`: nothing missing). It contains no `android_wlegl` code.

The phone has no standard Vulkan loader (libhybris replaces it) and no `vulkaninfo`, so Ubuntu
24.04's arm64 packages `libvulkan1` and `vulkan-tools` (1.3.275) were unpacked on the PC and pushed
to the test folder. The driver is selected with `VK_DRIVER_FILES` and an ICD file, and
`LD_LIBRARY_PATH` puts that loader first. Nothing system-wide changes.

### It loads and creates a device

- `vulkaninfo`: one device, **"Turnip Adreno (TM) 619"**, API **1.3.368**, driver 26.99.99,
  conformance 1.4.6.1, 174 device extensions. The vendor driver on the same phone: API 1.1.128,
  63 extensions.
- Dolphin's `wayland-probe.py vulkan-window` (pointed at this loader): `vkCreateInstance` 0,
  `vkCreateDevice` 0. "libhybris Vulkan platforms loaded: []": no Android code in the process.
- Full dumps: `turnip-vulkaninfo.txt` and `.json` in the phone's test folder (not committed).

### Presentation on Lomiri

| Path | Result |
| --- | --- |
| Mesa's Wayland WSI, default | `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` and every swapchain: **`VK_ERROR_SURFACE_LOST_KHR`** (-1000000000). The failure the Droidian patch describes for compositors without linux-dmabuf |
| `MESA_VK_WSI_DEBUG=sw` (frames copied by the CPU into shared memory) | Works: formats B8G8R8A8 UNORM/SRGB, present modes MAILBOX and FIFO, `currentExtent` 0xFFFFFFFF (correct, unlike libhybris' 1x1), swapchain created, image acquired |

`vkcube-wayland --c 600` (600 frames), timed with
[`run-timed.py`](../../tools/ut-probe/run-timed.py), **screen off**:

| Driver and path | Result |
| --- | --- |
| Turnip, software presentation, FIFO | 600 frames in 19.97 s = 30.0 fps; 14.94 s of CPU = **24.9 ms of CPU per frame** (75 % of one core); 71 MB |
| Turnip, software presentation, MAILBOX | Stalled: 0.27 s of CPU in 120 s, killed by the timeout |
| Vendor V@530 through libhybris, FIFO | Did not finish 600 frames in 120 s (about 5 fps), killed by the timeout |

- `vkcube` ignores `--width`/`--height` here: Lomiri sizes the window. A "1280x720" run cost the same
  25 ms per frame.
- With the screen off, Lomiri paces or holds buffers differently, so the frame rates and the two
  stalls do not mean much yet. The CPU cost per frame does: 25 ms means about 1.5 cores just for
  copying at 60 fps. Repeated with the screen on in M2 and M3.

### DXVK's requirements

[`dxvk-check.py`](../../tools/ut-probe/dxvk-check.py) reads DXVK's own feature table
(`src/dxvk/dxvk_device_info.cpp`, entries marked required) and looks each one up in the
`vulkaninfo -j` dump:

| DXVK | Vulkan 1.3 | 256 B push constants | Required features |
| --- | --- | --- | --- |
| 2.7.1 | ok | ok | **41 of 41** |
| 3.1.1 | ok | ok | 51 of 53: missing `storageBuffer16BitAccess` and `storageBuffer8BitAccess` |

This matches what ARMDeck found on the Adreno 650 (`docs/compat-perf-audit.md`) and DroidDeck's
DXVK 2.7.1 pin for Adreno 6xx. VKD3D-Proton (Direct3D 12) is checked in stage 3.

## 5. Baseline against Android

The plan (two identical phones, one on Android) is not possible now. The alternative: run
benchmarks on Ubuntu Touch that have many published Android results for the same phone, and compare
with the median of those.

| Benchmark | Runs here as | Compares | Android reference |
| --- | --- | --- | --- |
| Geekbench 6.7.1 CPU | native Linux ARM64 | the CPU side of Ubuntu Touch against Android (scheduler, governor, background load) | Geekbench Browser results for the Redmi Note 12 5G (nanoreview lists 852 single-core, 2048 multi-core) |
| Geekbench 6 GPU, Vulkan (Linux ARM since 6.6) | native, twice: Turnip, and the vendor driver through libhybris | Turnip against Qualcomm's driver, on the same GPU | Geekbench Browser Vulkan results for the same phone (Android's vendor driver) |
| 3DMark Wild Life, Wild Life Extreme | Android app, so only inside Waydroid | the Waydroid route (Android in a container on Ubuntu Touch) against Android | UL's page: 1039 and 308 (its page does not name the variant) |

Caveats: online results come from many firmware versions, temperatures and power settings, so they
give a range, not a number. 3DMark in Waydroid measures Android's graphics stack in a container, not
UTDeck's. Geekbench's free version uploads every result to its public website.

**Chosen by the maintainer (2026-11-17): Geekbench, uploads allowed.** 3DMark in Waydroid is not
run.

Geekbench 6.7.1 Linux ARM preview (`Geekbench-6.7.1-LinuxARMPreview.tar.gz`, SHA-256
`6e59acb8...e0e3`), pushed to the test folder. The preview always uploads, and prints only the
result link, not the scores; the Geekbench Browser refuses automated access (Cloudflare), so the
scores are read from the links by hand. It probes OpenCL at start, which loads libhybris, so it needs
the same TLS settings as other hybris programs (`GLIBC_TUNABLES=...optional_static_tls=4096`,
`LD_PRELOAD=libtls-padding.so`). Runs: screen off, on USB power, battery full.

| Run | Result | Temperature (cpuss-0 / gpuss-0) |
| --- | --- | --- |
| CPU (22:42-22:51) | uploaded: [19487821](https://browser.geekbench.com/v6/cpu/19487821). Multi-core ran **6 threads**, not 8 (parked cores, section 1) | 30.5 → 36.6 °C |
| GPU Vulkan, Turnip (22:53-23:03) | **failed, not uploaded**: workload 303 (Face Detection) "failed validation" (wrong results), then KGSL "gpu timeout" in Gaussian Blur, the context was dropped and every later call returned `VK_ERROR_DEVICE_LOST` | 33.6 → 37.0 / 33.2 → 36.3 °C |
| GPU Vulkan, vendor V@530 (first try, 23:03) | ended at "Stereo Matching" when the screen turned on and systemd restarted `adbd` at 23:19:58, which ended the adb session and Geekbench with it. Not a GPU problem | |
| GPU Vulkan, vendor V@530 (second try, 23:20-23:37, detached with `setsid nohup`) | **passed all 8 workloads**, no validation error, no KGSL timeout; uploaded: [6977517](https://browser.geekbench.com/v6/compute/6977517) | 35.1 → 37.8 / 34.3 → 37.0 °C |
| GPU Vulkan, Turnip v25.3.0 R11, Android build through libhybris (23:41-23:52) | **failed the same way as DD-Turnip**: workload 303 "failed validation", then KGSL "gpu timeout" in Gaussian Blur and `VK_ERROR_DEVICE_LOST`; not uploaded | 35.5 → 40.1 / 34.7 → 39.3 °C |

The R11 run is the driver the maintainer used for Dolphin (`third_party/Turnip_v25.3.0_R11.zip`,
SHA-256 `0452bdd5...3b40`; KIMCHI's build, "Mesa 25.3.0-devel (git-07f3b62d59)", Vulkan 1.3.328 on
this GPU). It is an Android (bionic) library, so it ran by route A: renamed to `vulkan.adreno.so`,
loaded by Android's Vulkan loader through libhybris with dolphin-ut's stand-in `libvndksupport.so`
(`HYBRIS_LD_LIBRARY_PATH`, `DOLPHIN_UT_VK_DRIVER_DIR`), and with glibc's `pthread_barrier_*`
supplied by [`hybris-barrier-hook.c`](../../tools/ut-probe/hybris-barrier-hook.c) (the hook from
dolphin-ut's `VulkanDriver.cpp`, as an `LD_PRELOAD` library, since Geekbench cannot install it
itself). Environment: `r11/env.sh` in the test folder.

**Scores** (read from the result pages by the maintainer, 2026-11-18):

| Test | Ubuntu Touch (stone) | Android references | Difference |
| --- | --- | --- | --- |
| CPU single-core | **866** | 852 (nanoreview, Snapdragon 4 Gen 1); 841 (Redmi Note 12 5G, Geekbench 6.0, Notebookcheck); 845 (another Snapdragon 4 Gen 1 phone, Geekbench 6.7.1, Notebookcheck) | +1.6 % to +3.0 % |
| CPU multi-core | **1973** | 2048; 1875; 2020 (same sources) | -3.7 % to +5.2 % |
| GPU Vulkan, vendor driver | **1004** | about 1,160 for the Adreno 619 (Notebookcheck, Geekbench 6.7.1, phones not named, so possibly other chips with the same GPU); 988 "GPU compute" (nanoreview, API not named) | +1.6 % against 988; -13.4 % against 1,160 |

The CPU on Ubuntu Touch performs **like Android on this chip**: single-core slightly above every
reference, multi-core inside their spread. Multi-core ran with 6 worker threads instead of 8 (the
parked cores, section 1), so it may have some room left. In short: Ubuntu Touch and the Halium
container take nothing measurable from the CPU. One run each, so differences of a few percent are
noise.

The GPU result (vendor driver through libhybris, 1004) falls between the two references. Neither
is a clean match: nanoreview's 988 is for this chip but does not name the API, and Notebookcheck's
1,160 is for the Adreno 619 without naming the phones, so it may include chips that clock the same
GPU higher. No sign of a loss through libhybris, but this one is weaker evidence than the CPU's. A
Turnip score could not be taken (the run fails, above).

**The Turnip failure, in detail.** Kernel log at the failure:
`kgsl kgsl-3d0: geekbench_aarch[54245]: gpu timeout ctx 7 ts 2`, then `gpu fault ctx 7 ctx_type
ANY ts 2 status 00EE1015`, a GPU snapshot, and `gpu failed ctx 7 ts 2`. KGSL recovered the GPU by
itself; only that program's context was lost. KGSL kills any command buffer that runs longer than
its dispatcher timeout: `ft_long_ib_detect=1`, `dispatch/drawobj_timeout=2000` (ms). Geekbench's
optimizer tries very small work-group sizes first, so one blur dispatch may have run past 2 s.

What the three GPU runs show:
- **The vendor driver passes** the same workloads under the same 2 s limit, so neither Geekbench nor
  the limit is at fault.
- **Both Turnips fail at the same two points**: DD-Turnip (Mesa 27.0.0-devel, Linux glibc, no
  libhybris) and R11 (Mesa 25.3.0-devel, Android build through libhybris). So it is not the Linux
  build, not the route (A or B), and not a recent Mesa regression: it is Turnip itself on the Adreno
  619 with this KGSL, at least since Mesa 25.3.
- The Dolphin games ran fine on R11. They are graphics work; Geekbench's failures are compute
  (a wrong result in Face Detection, and a blur dispatch that runs into KGSL's timeout). Games use
  compute shaders too (DXVK, and many engines' post-processing), so this matters for UTDeck.

For stage 1: find which shader and dispatch size trigger both (a small Vulkan compute test of our
own, not Geekbench), try `TU_DEBUG` options and Mesa main, and report to Mesa with the details
(Adreno 619v2, KGSL on kernel 5.4). Until then, Turnip is fine for graphics on the A619 but is not
trusted for heavy compute.

The 2-second limit is a GPU safety mechanism of the vendor kernel and stays as it is.

## Manual tests (screen on and unlocked)

Commands run in an `adb shell` on the phone. Run by the maintainer on 2026-11-18.

| Test | Result |
| --- | --- |
| M1 Lomiri gralloc buffer on screen | **Pass**: the eight color bars, as expected |
| M2 Turnip, software presentation, FIFO | Runs: 600 frames in 18.38 s = **32.7 fps**, 13.06 s of CPU = **21.8 ms of CPU per frame** (71 % of one core), 71 MB. "Smooth", but **in portrait the cube looks torn apart**; normal in landscape |
| M2 Turnip, software presentation, MAILBOX | **Stalls**: the cube appears but does not move (the same stall as with the screen off) |
| M3 vendor driver through libhybris, first try | Invalid: the output said "Turnip Adreno (TM) 619", because M2's settings were still exported in the same shell (a mistake in the instructions, corrected below) |
| M3 vendor driver through libhybris, corrected | **No picture**: "Adreno (TM) 619" selected, then nothing on screen except an "Unknown" app in the launcher; killed by the timeout after 120 s with 0.13 s of CPU. Cause below: a 1x1 swapchain |
| M4 Turnip, software presentation, landscape | **Normal picture.** Window 2400x1017, stride 9600 = 2400 x 4: no padding |

**The torn picture in portrait** is most likely a stride bug in Lomiri's shared-memory path. In the
software path Mesa pads each row of the image to 64 pixels: `WAYLAND_DEBUG=client` shows
`wl_shm_pool.create_buffer(..., 1080, 2305, 4352, ...)`, so a 1080-pixel row takes 4352 bytes (1088
pixels), and the stride is passed correctly. If Mir 1.8 reads such a buffer as if rows were exactly
1080 × 4 bytes, each row slips 8 pixels: a sheared, torn picture. M1 supports this: the gralloc
buffer had the same 1088-pixel stride and showed straight bars, because that path gives Mir the
stride through gralloc. M4 confirmed the pattern: in landscape Lomiri sizes the window 2400x1017, and
Mesa's row of 2400 x 4 = 9600 bytes needs no padding (Mesa rounds rows to 64 bytes; 4320 is not a
multiple of 64, 9600 is), so stride equals width x 4 and the picture is right. The tear appears
exactly when the stride differs from width x 4. Either way it weakens the software path as a fallback and
strengthens the case for the `android_wlegl` WSI patch (stage 1).

**MAILBOX stalls** with software presentation, screen on or off. FIFO works. Not needed for now
(games can use FIFO); noted for stage 1.

**Why M3 showed nothing.** `WAYLAND_DEBUG=client` on the same run (screen off, 15 s, log
`m3-hybris.log` in the test folder): libhybris' Wayland presentation asks Lomiri for its buffers
with `android_wlegl.get_server_buffer_handle(..., 1, 1, 1, 768)`, a **1x1** buffer (usage
`0x300`, as in the Droidian patch; later `0x10000300`). `vkcube` takes its swapchain size from the
surface's `currentExtent`, and libhybris reports 1x1 there (found in dolphin-ut's stage 0; Dolphin
uses the window's size instead). So the window was one pixel: Lomiri listed an app with nothing to
show. Only two frames were committed; then `vkcube` waited on a frame callback that never came, and
Lomiri never sent the real window size. This is the known libhybris quirk, not a failure of the
vendor driver, which ran the Geekbench Vulkan test and Dolphin fine. It also shows that libhybris'
own presentation uses the same `get_server_buffer_handle` request as the Droidian patch.

The commands used, for reference:

- M3 (corrected): in a new `adb shell`, `unset LD_LIBRARY_PATH VK_DRIVER_FILES MESA_VK_WSI_DEBUG`,
  then the libhybris settings (`GLIBC_TUNABLES`, `LD_PRELOAD=libtls-padding.so`) and
  `python3 run-timed.py --frames 600 -- timeout 120 vk/bin/vkcube-wayland --c 600`. The first line
  must name "Adreno (TM) 619" without "Turnip".
- M4: phone in landscape, then `WAYLAND_DEBUG=client ... MESA_VK_WSI_DEBUG=sw timeout 30
  vk/bin/vkcube-wayland --c 300 2>&1 | grep -E "configure\(|create_buffer" > m4-landscape.log`.

## What stage 0 changes in the plan

- **Stage 1 can go straight to route B.** Native Turnip works on this kernel, and Lomiri hands out
  the exact buffers the Droidian patch needs. Next: port that patch onto Mesa main (or onto
  DD-Turnip's tree) and replace the software copies with a GPU blit.
- **The software presentation path is only a partial fallback** for stage 2 if stage 1 takes
  longer: about 22 ms of CPU per frame (32.7 fps, screen on), FIFO only (MAILBOX stalls), and a torn
  picture whenever the row stride is padded (portrait at 1080 wide; M4 confirmed landscape at 2400
  is fine). Games run in landscape, so it is usable there.
- **Wine 11.15 or later** (or `-static-pie`), because of the 39-bit address space.
- **A Proton-based runtime**: without fsync, NTSYNC and `userfaultfd`, esync and Proton's other
  fallbacks matter. Optional, for our own ports only: an NTSYNC module backported to the Halium
  kernel.
- **Touch input reaches Wine directly** (a `winewayland` touch patch or a Wine-side helper), because
  apps cannot create virtual input devices. On our own ports a udev rule could open `/dev/uinput`,
  but other phones would not have it.
- Steam's container needs user namespaces: available.

## Problems met

- **vkcube ignores the requested size** on Lomiri: the shell sizes the window. Use the window's
  real size in measurements, not the one asked for.
- **Screen off changes presentation**: with the screen off, FIFO settled at exactly 30 fps and
  MAILBOX stalled. Frame-rate measurements need the screen on.
- **The phone has no `bc`** (and no GNU `time`): timing goes through `run-timed.py`.
- **Long runs die with the adb session**: turning the screen on restarted `adbd` (systemd stopped
  and started it 4 s later), which killed the running Geekbench. Long runs go detached:
  `setsid nohup script > status 2>&1 < /dev/null &`, and the PC polls the status file.
- **The Geekbench Browser blocks automated access** (Cloudflare, HTTP 403), and the Linux preview
  prints no scores: the scores are read from the result links by hand.
- **Turnip lost the GPU in Geekbench's Vulkan test** (open, stage 1): see section 5.

## Events

- 2026-11-17: source check (56 items, nothing changed). Kernel, devices and system calls read
  (section 1). `wlegl-probe.py` written and run: Lomiri hands out linear dma-buf gralloc buffers
  (section 2). Idle memory and CPU (section 3). DD-Turnip v0.7.0 Linux build loaded with Ubuntu's
  loader: device created, normal presentation fails, software presentation works, DXVK 2.7.1's
  requirements all met (section 4). Wine bug 59898 found and checked (39-bit address space).
  Baseline plan written (section 5); manual tests M1-M3 written.
- 2026-11-17, later: the maintainer chose Geekbench with uploads. CPU run uploaded; it revealed the
  parked cores (section 1). GPU Vulkan: DD-Turnip failed (wrong result, then GPU timeout), the vendor
  driver passed (after a first try was cut off by an `adbd` restart). The maintainer pointed to the
  Turnip used for Dolphin (`third_party/Turnip_v25.3.0_R11.zip`); run through libhybris with a new
  barrier-hook preload, it failed at the same two points.
- 2026-11-18: the maintainer read the scores (CPU 866 / 1973: like Android; vendor Vulkan 1004) and
  confirmed that the `adbd` restart came from waking the screen by hand. Manual tests: M1 passed; M2
  ran at 32.7 fps with a torn picture in portrait only, and MAILBOX stalled; M3 was invalid (M2's
  settings still exported), instructions corrected. `WAYLAND_DEBUG` showed the padded stride (4352
  bytes for 1080 pixels) behind the likely cause of the tear. M3 rerun and M4 (landscape log) asked
  for. Then: M3 showed no picture (a 1x1 swapchain from libhybris' `currentExtent`, confirmed with
  `WAYLAND_DEBUG`), M4 a normal picture with an unpadded stride (2400x1017, 9600). Stage 0 closed.

## Sources

- Wine bug 59898: https://bugs.winehq.org/show_bug.cgi?id=59898
- Droidian Mesa patch: https://github.com/panxiao81/debian-mesa/commit/adb4b85e82
- libhybris' protocol: https://github.com/libhybris/libhybris/blob/master/hybris/platforms/common/wayland-android.xml
- DD-Turnip v0.7.0: https://github.com/Droid-Deck/Drivers/releases/tag/DD-Turnip-v0.7.0
- Ubuntu packages: `libvulkan1_1.3.275.0-1build1_arm64.deb`, `vulkan-tools_1.3.275.0+dfsg1-1_arm64.deb`
  from http://ports.ubuntu.com/ubuntu-ports/pool/
- DXVK feature tables: https://github.com/doitsujin/dxvk (`v2.7.1`, `v3.1.1`,
  `src/dxvk/dxvk_device_info.cpp`; minimum API in `src/dxvk/dxvk_instance.h`)
- Geekbench 6.6 (Vulkan on Linux ARM): https://www.geekbench.com/blog/2026/02/geekbench-66/
- Android reference figures: https://nanoreview.net/en/phone-compare/xiaomi-redmi-note-12-4g-vs-xiaomi-redmi-note-12,
  https://benchmarks.ul.com/hardware/phone/Xiaomi+Redmi+Note+12+review
