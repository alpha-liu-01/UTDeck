# Stage 2: first light, the plan

Written 2026-11-18, after [stage 1](stage1-roadmap.md). Goal (from
[feasibility-and-roadmap.md](feasibility-and-roadmap.md)): Wine for ARM64, with FEX for x86 code,
draws a Direct3D game on Lomiri with the GPU. Exit of the whole stage: one D3D11 game playable on
screen, with frame rate, CPU, GPU and RAM recorded.

What stage 1 settled for this stage:
- The GPU driver is our Turnip (route B) with zink for OpenGL; one driver folder, chosen per app
  with environment variables ([stage1.4-decision.md](stage1.4-decision.md)).
- Games present without vsync waiting, with a frame limiter, and ask for a LOW GPU priority.
- The phone's kernel has a 39-bit address space (`CONFIG_ARM64_VA_BITS=39`): Wine 11.15 or later
  (Wine bug 59898, stage 0).

Stage 2 is too large for one go, so it is split. Each substage needs the one before it.

| Substage | Goal | Exit |
| --- | --- | --- |
| **2.1 Wine runs on the phone, no window yet** | A Wine for ARM64 with FEX (Hangover 11.16's Ubuntu 24.04 packages, unpacked into a folder, nothing installed) creates a prefix and runs Windows console programs: native ARM64, x86-64 through ARM64EC and FEX, and 32-bit x86 through WoW64 and FEX | The three kinds of console program print their output; the prefix is created; start-up time, memory and FEX's CPU cost measured on a small benchmark of our own |
| 2.2 Wine's Wayland driver on Lomiri | Our patch making `wp_viewporter` optional in `winewayland.drv`, built with Wine in the cross container; a Windows GDI window (Wine's `notepad`, `winecfg`) on Lomiri | A Wine window on screen in portrait and landscape, with touch or mouse input reaching it, or the reason it cannot work and the fallback chosen |
| 2.3 Vulkan inside Wine | `winevulkan` on our Turnip, presenting through Wine's Wayland driver (the game's picture on a subsurface); a Windows Vulkan sample, closed and reopened several times, windowed and fullscreen | A Windows Vulkan program on screen without stale frames or crashes, with CPU per frame and frame rate measured; the subsurface question answered (stage 2 risk 1b) |
| 2.4 Direct3D: the first game | DXVK 2.7.1 for Direct3D 9, 10 and 11; samples first, then one real game the maintainer owns; sound through `winepulse`, a gamepad through `winebus` | The stage exit above |

Order and why: 2.1 checks Wine and FEX on this kernel before any display work, so a failure there
is not confused with a Wayland one. 2.2 is the known blocker (no `wp_viewporter` on Lomiri). 2.3 is
the second known risk (subsurfaces on Lomiri). 2.4 adds only DXVK on top of what 2.3 proved.

Fallbacks, if 2.2 or 2.3 stall (from the roadmap): varmd's Vulkan-only Wayland driver built for
aarch64, or `winex11` through Xwayland with Mesa's software presentation, to keep 2.4 moving.

## Status

- **2.1 (2026-11-18): done.** Notes: [stage2.1-wine-console.md](stage2.1-wine-console.md).
  Hangover 11.16 (Wine 11.16, FEX, Box64), unpacked into a folder, creates a prefix in 44 s and runs
  ARM64, ARM64EC, x86-64 (FEX) and 32-bit x86 (Box64 or FEX) console programs with identical
  results. x86-64 through FEX keeps 79 % of native speed in 7-Zip's single-thread benchmark; FEX
  offers AVX2. Wine must run with our driver environment, or it loads libhybris and fails. Next:
  2.2, our own Wine build with the `wp_viewporter` patch.
- **2.2 (2026-11-18): done.** Notes: [stage2.2-winewayland.md](stage2.2-winewayland.md).
  Our `winewayland.so` (Hangover's exact source, two patches: `wp_viewporter` optional, the first
  finger as the mouse) draws Wine's Notepad and `winecfg` on Lomiri, usable by touch (M1). Windows
  keep their own size. Next: 2.3, Vulkan inside Wine.
- **2.3 (2026-11-18): done.** Notes: [stage2.3-vulkan-in-wine.md](stage2.3-vulkan-in-wine.md).
  A Windows Vulkan program (x86-64 through FEX) presents through `winevulkan` on our Turnip, on a
  subsurface with zero-copy buffers, 0.81 ms of CPU per frame; windowed and fullscreen, opened and
  closed repeatedly, no stale frames. FIFO deadlocks under Wine on Lomiri, so MAILBOX is forced.
  Two more `winewayland` patches: 0003 (fullscreen follows Lomiri's rotation; optional landscape
  monitor) and 0004 (ask again for fullscreen when Lomiri shrinks it, as the Dolphin port does).
  Started as a Lomiri app with a landscape desktop file, it comes up landscape and full screen and
  keeps it. Next: 2.4, DXVK and the first game.
- **2.4 (2026-11-18): done.** Notes: [stage2.4-direct3d.md](stage2.4-direct3d.md). DXVK 2.7.1
  (ARM64EC); 3DMark 11 installed and run from its .NET GUI (Wine Mono): every test drawn correctly,
  with sound, P566 (Graphics 529, about an Intel HD 4000); Wine patches 0005 (mode-change crash,
  upstream bug) and 0006 (Wine scales a smaller picture: fit by default, stretch, center); a
  Bluetooth gamepad as an XInput controller, rumble included.

**Stage 2 is done (2026-11-18).** Exit met: a Direct3D 11 program on screen through Wine, FEX, DXVK
and our Turnip, with frame rate, CPU, GPU, RAM and temperature recorded; sound and a gamepad work.

Rules for this stage, from the project rules:
- Nothing system-wide on the phone: Wine, FEX and the drivers live in a test folder, chosen with
  environment variables. No `apt install`, no change to the read-only root.
- Thermal and power limits stay the vendor's; long runs are watched.
- Imported code keeps its author and licence; our patches stay separate, so each can be dropped or
  replaced by an upstream version later (`wp_viewporter` support in Mir 2 makes the 2.2 patch
  unnecessary).
