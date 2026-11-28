# Stage 6: a stronger phone for UTDeck, what the port needs

Written 2026-11-25 for the OnePlus Ace 5 port, which lives in its own repository (as stone's does).
This file is meant to be copied there. It adds to the stone port's notes what UTDeck needs from a
port, and answers the maintainer's three questions for this phone: a hybrid GPU stack, a
FreeSync-like display, and which Halium to build on.

Why a new phone: 4.10 (the game round) stopped on 2026-11-25 because the Nothing Phone (1) (Adreno
642L) is too slow for the games that matter (Ys IX, Stray, Need for Speed at 14-18 fps). The Ace 5
(Snapdragon 8 Gen 3, Adreno 750) is the first test phone; the Nothing Phone (3) (Snapdragon 8s Gen 4,
Adreno 825) follows once Turnip knows its GPU (section 5).

## Terms used here

- *Halium* = the layer that lets Ubuntu Touch run on an Android phone's kernel and vendor drivers.
  Its *GSI* (generic system image) is a minimal Android system that runs in a container and provides
  the Android libraries the vendor drivers expect; *libhybris* loads Android libraries (for example
  the GPU's EGL) into Linux programs.
- *vendor* = the phone maker's partitions with the hardware drivers (HALs) and their libraries; its
  Android version can differ from the system's.
- *HAL* = a hardware service of the vendor (display composer, audio, radio, camera). Most are
  separate processes reached through *binder* (Android's message system); a few are libraries loaded
  into each program (the graphics *mapper*).
- *GKI* = Google's generic kernel: one kernel per Android generation (`android14-6.1` here), with the
  phone's drivers as modules on `vendor_boot` and `vendor_dlkm`. *KMI* = the kernel's module
  interface; a kernel with the same KMI loads the stock modules unchanged.
- *KGSL* = the Android kernel's Adreno GPU driver (`/dev/kgsl-3d0`). Upstream Linux's is `msm`.
- *Turnip* = Mesa's Vulkan driver for Adreno; it can talk to KGSL. *zink* = Mesa's OpenGL on top of
  Vulkan. *freedreno* = Mesa's OpenGL driver for Adreno (and the name of the whole Mesa family).
- *HWC* / *composer* = the vendor's display HAL that puts pictures on the panel (`composer3`, AIDL,
  on this phone). Lomiri reaches it through Mir's Android platform and libhybris.
- *VRR* = variable refresh rate (FreeSync, G-Sync, the Steam Deck OLED's): the panel waits for the
  next picture instead of refreshing at fixed times, so a game at 47 fps shows 47 even frames.
  *Q-Sync* = Qualcomm's VRR for phone panels, in its display driver and composer.
- *Xwayland* / *glamor* / *GBM* / *DRI3*: the X11 server for Wayland desktops; glamor draws X11's
  windows with OpenGL; GBM is Mesa's buffer allocator, which needs a DRM device; DRI3 is how X11
  clients share GPU buffers with the server.
- *anti-rollback* = the bootloader refuses firmware older than what it has seen.

## 1. The phone, as read on 2026-11-25

Read with `adb shell` (no root, nothing written), Android running:

| | Value |
| --- | --- |
| Model | OnePlus Ace 5, `PKG110` (sold elsewhere as the OnePlus 13R; platform `pineapple`, SM8650) |
| Android, system and vendor | 16 (SDK 36), build `BP4A.251205.006`; `ro.board.first_api_level` 34 (the vendor's base froze at Android 14), `ro.board.api_level` 202504 (Android 16's vendor level) |
| Kernel | `6.1.175-g3127ab97bcbb`, built 2026-09-16 (`android14-6.1` GKI generation) |
| Software on it | LineageOS services are present (`lineagelivedisplay`, `vendor.lineage.livedisplay`): a LineageOS-based Android 16, not ColorOS |
| Bootloader | reports `ro.boot.flash.locked=1`, `verifiedbootstate=green`; LineageOS-based builds often spoof these, so `fastboot getvar unlocked` is the real answer |
| Updates | A/B slots (`_a` active), virtual A/B, dynamic partitions |
| Panel | 1264x2780, modes 60, 90 and 120 Hz (`dumpsys SurfaceFlinger`), each with `vrrConfig=N/A` |
| Display HALs | `composer3` AIDL version 2; allocator AIDL; mapper HIDL 4.0 (`android.hardware.graphics.mapper@4.0-impl-qti-display.so`) |
| Q-Sync | in use by the vendor (below, section 3) |
| DRM nodes | `card0` (`DSI-1`, two virtual outputs) and `renderD128`: the display controller's, not the GPU's (as on stone) |

## 2. Question 1: a freedreno and libhybris hybrid

**What exists.** UTDeck is already a hybrid on stone and the Nothing Phone (1). Lomiri and the phone
side stay on libhybris (the vendor's Adreno EGL and HALs), while games use Mesa over the same KGSL
driver:
- Vulkan: Turnip with its KGSL backend, presenting to Lomiri through libhybris' `android_wlegl`
  buffers (UTDeck's Mesa patch; stage 1.1);
- OpenGL: zink on Turnip, through Mesa's EGL (our patch 0003: EGL dropped zink without a DRM render
  node; stage 1.3). It works on Lomiri with no CPU copies, at about half the vendor driver's frame
  rate and about 1 ms more CPU per frame;
- X11: **no acceleration**. UTDeck's hidden Steam runs on Xvfb and draws its interface with llvmpipe
  (CPU) at 2-5 fps.

**What cannot be done.** The upstream `msm` GPU driver cannot sit next to KGSL. One GPU has one kernel
driver, and Lomiri's EGL, the vendor's camera and media HALs and Turnip-on-KGSL all need KGSL. Swapping
KGSL for `msm` (for a DRM render node, GBM and DRI3) would break Lomiri's drawing and parts of the
phone, and upstream's support for a GPU-only `msm` beside a separate display driver is much newer
than 6.1. Mesa's own OpenGL driver
(freedreno gallium) does not run on KGSL either: Mesa's build options say so ("the freedreno gallium
driver doesn't support KGSL. On those systems, zink with Turnip should be used instead"). So "hybrid"
means **Mesa user space over KGSL, beside libhybris**, which is the path UTDeck is on.

**What the port can add**, in order of value for UTDeck:
1. **Turnip on the Adreno 750 first** (a7xx; UTDeck's patches were only tested on 6xx): Vulkan 1.3
   and DXVK on the KGSL of this kernel, with `android_wlegl` buffers that are linear (UBWC-compressed
   buffers would not be read correctly; stone's stage 0 checked this for its Lomiri).
2. **X11 drawn with the GPU**: UBports packages an Xwayland with libhybris glamor
   (`hybris-support/xwayland-hybris`; Droidian's `glamor-hybris` is the same idea). That accelerates
   X11's own drawing (2D, windows). OpenGL *inside* X11 programs (Steam's Chromium) still needs
   buffers shared with the server (DRI3), which needs GBM, which needs a DRM GPU device: not there.
   Two ways to try, both open: Chromium drawing through ANGLE on Vulkan (Turnip), presented by
   Mesa's X11 WSI with a copy; or a GBM over Turnip's dma-buf export (new Mesa work).
3. **The kernel's device access for phablet**: `/dev/kgsl-3d0`, `/dev/dma_heap/*` (gralloc's
   buffers), `/dev/uinput` (Steam Input's virtual pad, open on the Nothing Phone (1), closed on
   stone), `/dev/input/*` and `hidraw` (controllers).

## 3. Question 2: a FreeSync-like display

**The panel can.** The vendor's "smooth motion" settings (`/vendor/etc/smomo_setting.xml`) turn
Q-Sync on in their game mode (`qsync="true"` at 120 Hz, for games at 52-68 and 100+ fps), and the
running display-config service (`vendor.qti.hardware.display.config.IDisplayConfig`, its client
libraries in `/vendor/lib64`, V2-V11) has `setQsyncMode` and `controlQsyncCallback`. Android's own
VRR interface is not offered (`vrrConfig=N/A`), so the way in is Qualcomm's.

Two levels, the second building on the first:
1. **The refresh rate follows the frame cap** (the Steam Deck's way, noted in stage 5.2): a cap of
   30 shows on 60 or 120 Hz, 40 on 120 Hz (3 refreshes a frame), 45 on 90 Hz, 60 on 60 Hz. Needs the
   compositor to switch among the composer's three modes on request. No Q-Sync needed; even frame
   times at the cap.
2. **Q-Sync during a game**: the panel refreshes when a new frame is committed, anywhere between its
   Q-Sync minimum and the mode's rate, so an uncapped game at 47 fps shows 47 even frames. Needs:
   - the panel's Q-Sync minimum, from the stock `dtbo` image (`qcom,mdss-dsi-qsync-min-refresh-rate`;
     not readable without root on the running phone);
   - a way to call `setQsyncMode` from the Ubuntu side (a small binder client through libhybris, or
     in the compositor), on when a game is in front, off after;
   - the compositor committing a frame as soon as the game's buffer arrives, not on a fixed vsync
     tick; best with the game's buffer going straight to a display plane (no extra copy by Lomiri).
     How Mir's Android platform schedules commits is the first thing to read;
   - below the minimum, frames shown twice (as FreeSync's low frame rate compensation), or the cap
     kept above the minimum.

**Why it is more than a stage-5 extra**: games on this class of phone often land at 40-55 fps, where
a fixed 60 Hz panel shows uneven frames; VRR fixes exactly that, without a cap.

**Hardware safety**: only the vendor's own modes and Q-Sync, switched through the vendor's own
service; panel timings, voltages and the device tree are not changed.

## 4. Question 3: Halium 15 and 16, and mismatches

**What exists** (2026-11-25):
- Halium's manifests (`github.com/Halium/android`) have branches `halium-13.0`, `halium-14.0` and
  **`halium-16.0`** (started 2026-07-15, last change 2026-08-28). **There is no `halium-15.0`.**
- Ubuntu Touch 24.04-2.x community ports on Halium 16 exist: the Google Pixel 6a (`bluejay`, alpha,
  most hardware working, calls and data included), on stock Android 16 build `BP4A.251205.006`, with
  UBports' generic GKI kernel branch `android14-6.1-halium` and the generic Halium 16 GSI built by
  UBports CI; Xiaomi Mi 9 (`cepheus`) and Mi 8 (`dipper`); UBports' own Android 16 community port for
  the Pixel 9 (`google-zumapro`).
- UBports' generic GKI kernel (`porting/community-ports/android12/generic/kernel-android-common`)
  has `android14-6.1-halium` (this phone's generation) and `android15-6.6-halium` (the Nothing Phone
  (3)'s), with the shared ramdisk and overlays in `generic/halium-gki`.

**For this phone**: kernel `android14-6.1`, vendor Android 16: the same pairing as the Pixel 6a
port. So the plan is **Halium 16 on the phone's own Android 16 vendor, with UBports' generic
`android14-6.1-halium` kernel and the stock vendor modules**. Nothing has to go back to an older
Android, so anti-rollback is not in the way; but the firmware the port is built against has to stay
on the phone (the Pixel 6a port pins its build for the same reason), and an update of the vendor
partitions can break the port.

**Can an older Halium run on a newer vendor?** Sometimes, and the Nothing Phone (1) port (Halium
11.0 on a newer vendor) shows it. The Halium system only has to understand the vendor interfaces
Ubuntu Touch really uses:
- binder HALs are versioned and mostly keep working across Android versions;
- the libraries loaded into each program (the mapper, through libhybris) and new interface
  generations are where it breaks. This vendor needs `composer3` (from Android 13: Halium 13 or
  newer), an AIDL allocator and mapper 4.0.
A matched Halium avoids checking each HAL by hand; with Halium 16 there is no reason to mismatch here.

**To check in the port before building**: that the KMI of UBports' `android14-6.1-halium` matches
this kernel's modules (the stock `vendor_boot` and `vendor_dlkm` load unchanged), and that the
vendor build the port pins is the one on the phone.

## 5. The Nothing Phone (3), later

- GPU: Adreno 825 (a8xx). Upstream Mesa does not recognise it yet: DroidDeck's issue #66 shows Turnip
  stopping with "device is unsupported" (chip id `0x44030000`) on its Mesa 26.2.2. The a8xx support
  is in unmerged Mesa work, used by experimental community builds (Banners-Turnip, faux123's).
- Kernel: `android15-6.6` GKI, which UBports' generic kernel also has (`android15-6.6-halium`).
- Ported once Turnip runs on the Adreno 825 from upstream Mesa (or a branch UTDeck can pin).

## 6. What UTDeck needs from any port (from stages 0-5)

Each item is a lesson from stone and the Nothing Phone (1):
- **adb (or ssh) over Wi-Fi.** Both phones have one USB port: a mouse, a keyboard or a controller on
  USB meant unplugging adb, and a reader started from adb died with its session (stage 4.10). A
  network shell from the first boot.
- **Lomiri's Wayland protocols** as UTDeck's Wine patches expect (no `wp_viewporter`, no relative
  pointer, no pointer locking, no text input on Mir 1): the same as stone, or better. Side buttons of
  a mouse are lost inside Lomiri on the Nothing Phone (1) (the kernel sees `BTN_SIDE`/`BTN_EXTRA`).
- **Touch through `wl_touch`** (Wine's first finger as the mouse, patches 0002 and 0025).
- **The vendor's thermal profiles** readable and selectable (`thermal-engine`; the Nothing Phone (1)'s
  `high.perf` gave Stray +16 % at the same temperature) and the GPU's KGSL files readable for the
  overlay (`/sys/class/kgsl/kgsl-3d0/`).
- **Memory**: zram swap, cgroup v2 with the memory controller; on 6.1 the kernel's
  `memory.reclaim` exists (stage 5.3 had to use `/proc/<pid>/reclaim` on 5.4).
- **The kernel's newer features that UTDeck can use** on 6.1: `futex_waitv` (Wine's fsync, missing
  on stone's 5.4); NTSYNC is newer (6.14).
- **CPU layout and sensor names** differ per phone: UTDeck's sampler and boost read them per phone
  (the sampler broke on the Nothing Phone (1) by assuming stone's `policy6`).
- **The firmware build pinned** and written down (vendor partitions, modem), since the port depends
  on it.

## 7. Order in the port, as far as UTDeck is concerned

1. The phone boots Ubuntu Touch with Lomiri, touch, Wi-Fi, a network shell, charging.
2. UTDeck's stage 0 probe (kernel features, device access, memory) and stage 1's checks: Turnip on the
   Adreno 750 over KGSL, presenting through `android_wlegl` with linear buffers.
3. UTDeck's runtime and app installed as on the other phones; a Direct3D 11 game measured with the
   overlay, as the first number for this phone.
4. The display: the composer's three modes switchable (level 1), then Q-Sync (level 2).
5. X11: Xwayland with libhybris glamor, then OpenGL inside X11 programs (section 2).

## Sources

- Halium's manifests: https://github.com/Halium/android (branches `halium-14.0`, `halium-16.0`)
- The Pixel 6a's Halium 16 port: https://github.com/logicalangel/ubuntu-touch-bluejay
- UBports' generic GKI kernel and ramdisk:
  https://gitlab.com/ubports/porting/community-ports/android12/generic/kernel-android-common,
  https://gitlab.com/ubports/porting/community-ports/android12/generic/halium-gki
- UBports' Pixel 9 Android 16 port:
  https://gitlab.com/ubports/porting/community-ports/android16/google-pixel-9/google-zumapro
- UBports' Xwayland with libhybris: https://gitlab.com/ubports/development/core/hybris-support/xwayland-hybris;
  Droidian's glamor: https://github.com/droidian/glamor-hybris
- Mesa's note on freedreno and KGSL: `meson.options`, option `freedreno-kmds` (Mesa 26.2.5)
- The Adreno 825 in Turnip: https://github.com/Droid-Deck/DroidDeck/issues/66,
  https://github.com/The412Banner/Banners-Turnip/releases
