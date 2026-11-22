# Stage 4.3: the launch flow, and hidden Steam's dialogs

Started 2026-11-21. Plan: [stage4-roadmap.md](stage4-roadmap.md). Goal: from the tap on a game to
its window, the app shows what is happening (Steam starting and signing in, Steam's own preparation,
install scripts, the game starting), shows hidden Steam's dialogs so that the user answers them
(publishers' agreements, Steam's notices), shows errors, and goes back to the library when the
game ends.

Why now (the maintainer, 2026-11-21): hidden Steam's dialogs come on a game's first launch, and
every game on the Nothing Phone (1)'s fresh Steam still has its first launch ahead, while stone's
are past theirs.

## What is known already

- **Steam's own record of a launch** (`~/.local/share/Steam/logs/console_log.txt`, stage 3.9): one
  `GameAction [AppID N, ActionID M] : LaunchApp changed task to TASK` line per step, and
  `LaunchApp waiting for user response to TASK` when Steam waits; most waits are answered by Steam
  itself at once (`ProcessingShaderCache`, `CreatingProcess`: "continues with user response"),
  some wait for a person: `ShowInterstitials` held Aperture Desk Job's start until the "Steam Input
  Games" notice on the hidden screen was clicked (3.9, "Failed to launch" from the drawer). Then
  `Game process added` and `removed` (with the app id). Those lines carry no account data; other
  lines of the same file can, so only these are read, and numbers of 9+ digits are masked in
  anything shown to people.
- **Install scripts** (`iscriptevaluator.exe`, the Steamworks redistributables) run through UTDeck's
  tool as `game-run.sh` runs (label `steam-<appid>`), before the game; 3.9 measured minutes.
- **The hidden screen** (Xvfb `:50`): dialogs are windows there; `xwin.py` lists them (size,
  position); fallback B (4.2) already captures that screen and passes taps and key presses to it.
  Windows after the sign-in can show the account name: a capture is shown to the user only, kept
  nowhere, and only while a dialog waits.
- **The game's own window** belongs to the app (4.1), and the library comes back when the game ends.

## Plan

1. **Observe** (the Nothing Phone): after 4.2's `tool` step, the first launch of Ys IX through
   UTDeck's tool, with `GameAction` lines and the hidden screen's windows followed (sizes; a capture
   only when a window shows): which tasks, which waits, which dialogs, how long each part takes.
   Then more first launches there (the games the maintainer installs).
2. **A launch model in the app**: follows the chosen game's `GameAction` lines (only those), UTDeck's
   game file and the focus file: a state for each part (Steam starting, signing in, preparing,
   waiting for the user, installing, starting, running, ended, failed).
3. **The starting page**: shown from the tap until the game's window is in front; the current part
   and how long it has taken; a dialog shown in the page (its window captured from the hidden
   screen, taps and typing passed to it, as fallback B), answered only by the user.
4. **Errors**: a launch that ends without a game window, a game that ends within seconds (Ys IX on
   Steam's own Proton), Steam not signing in: said in words, with the log to look at.
5. **Two games' first and normal starts** from the app on the Nothing Phone (the exit), a
   publisher's agreement or Steam's notice answered in the app.

## 1. Observed: Ys IX's first launch on the Nothing Phone (2026-11-21, 19:50-20:07)

Followed with a monitor: Steam's `GameAction` lines for app 1351630 (numbers masked), the hidden
screen's mapped windows (sizes), UTDeck's game file and the running `.exe` names, every 2 s.

| Time | Steam's step | On the phone |
|---|---|---|
| 19:50:22 | `CheckShaderDepotManifest`, `ProcessingInstallScript` (3 steps) | a new prefix (`wineboot`), `iscriptevaluator.exe` |
| 19:50:49 | | a Steam toast, 283x70, sliding in at the hidden screen's bottom right |
| 19:50:53-19:51:05 | | Visual C++ 2015-2019 x86 (`vcredist_x86.exe`, its `Setup.exe`), then x64: both installed ("Exit code: 0x0" at 19:51:05 in the x64 one's log) |
| 19:51:05-19:59:27 | | **`VC_redist.x64.exe /q` never exits**: one thread asleep in a futex, 0.26 s of CPU in 8 minutes (3.9's hang, now on a brand-new prefix); ended by hand |
| 19:59:24-19:59:54 | | DirectX's setup (`DXSETUP.exe`, `infinst.exe`): 30 s |
| 19:59:54 | `SynchronizingCloud`, `SynchronizingStats`, `ShowInterstitials`, **waiting for user response** | a 700x480 dialog (and Steam's main window, 1280x800, behind it): "Controller using Steam Input" ("Your DualShock controller isn't supported by this game. Steam Input will translate..."), one OK button |

Found on the way:
- **The library hid for the install script** and Lomiri then paused UTDeck: the installers' Wine
  processes carry UTDeck's app id and focus file, their focus file said "1", so the library hid;
  their windows went, nothing of the app was on screen, and Lomiri paused it (state `T`): the
  maintainer saw only the wallpaper for ten minutes. **Fixed**: `utdeck-compat` sets
  `UTDECK_SETUP_RUN=1` for `iscriptevaluator.exe`, and `game-run.sh` then writes no game file.
- **The app's dialog view** (0.3.0-0.3.1, `SteamLaunch`, `DialogPage.qml`): the page "Steam is
  asking" came up 2 s after the app opened (the log's last lines said Steam waited), showing only
  the dialog's window. 0.3.0 took the window in front, which for a moment was Steam's toast; 0.3.1
  takes the frontmost window that is neither a toast (at least 300x200) nor Steam's main window
  (smaller than 1000x500).
- **The tap on OK did nothing**, because the dialog was gone: at 20:05:08, as the app opened, Steam
  received `-applaunch 1351630` again (`steam-service.sh play`, a tap on the game in the library),
  and `play` ends with a 10 s loop of `xwin.py --close`, which asked *every* mapped window to close:
  the dialog too, which dismisses it without an answer, and the launch waited on with nothing shown.
  **Fixed**: `xwin.py --close` only closes windows of at least 1000x500 (Steam's main window).
- **Launched again** (`steam-service.sh launch`, 20:06:37): the install script ran in 2 s, Steam
  counted the notice as shown ("continues with user response ShowInterstitials" at once), and
  `ys9.exe` started through UTDeck's tool, then stopped at once (exit 53): **"d3d11.dll ... not
  found"**. Ys IX's prefix had been made at 19:40 by Steam's own Proton (before the `tool` step
  existed; its `steamuser` folder is there next to our `phablet`), and `utdeck-compat` put DXVK and
  VKD3D-Proton only into prefixes it made itself. **Fixed** (0.3.2): it puts them in whenever the
  prefix has not got them from this very runtime (a marker, `utdeck-prefix-dlls`, holds the
  runtime's name), which also updates a prefix's DXVK after a runtime update.
- **Launched a third time** (the maintainer, from the app, 20:09:33, 0.3.2): install script 2 s, the
  notice skipped, `ys9.exe` started; DXVK loaded this time ("DXVK: 2.7.1", the prefix fix works),
  then **"Failed to create Vulkan instance"**, exit 0 after 7 s. The runtime's `env.sh` loads the
  GPU driver from `~/utdeck-probe/zink` (Turnip, zink and its EGL file: `VK_DRIVER_FILES`,
  `__EGL_VENDOR_LIBRARY_FILENAMES`) and `~/utdeck-probe/vk` (Ubuntu's Khronos Vulkan loader; the
  phone's own `libvulkan.so.1` is libhybris', which cannot load Turnip). **Both were put on stone by
  hand in stages 1.1-1.4 and are neither in the runtime nor in the setup**: the Nothing Phone has
  no Vulkan driver of ours at all. The second hand step of stone's that the setup missed (after
  4.2's `tool`).
- **The GPU driver by hand, for now** (the maintainer's choice: A now, then B, the driver in the
  runtime): stone's layout rebuilt from `build/zink` (Mesa 26.2.5, the pinned commit, patches
  0001-0005, built 2026-11-20): `~/utdeck-probe/zink/lib/` (`libvulkan_freedreno.so`,
  `libgallium-26.2.5.so`, `libEGL_mesa.so.0`), `turnip_icd.json` and `egl_vendor.json` with paths
  relative to themselves (stage 1.4 checked that both loaders accept that), `BUILD-INFO`; and
  `~/utdeck-probe/vk/lib/libvulkan.so.1` (Ubuntu 24.04's Khronos loader, `libvulkan1`
  1.3.275.0-1build1, through apt in the cross container). Checked on the Nothing Phone with stage
  1's programs and `env.sh`'s settings: `spvrun intops.spv` ran on "Adreno 7c+ Gen 3, driver: turnip
  Mesa 26.2.5" (Turnip's name for the 642L's family), and `gltest headless`: OpenGL 4.6 core on "zink
  Vulkan 1.3 (Adreno 7c+ Gen 3 (MESA_TURNIP))", triangle and compute shader PASS. **Our driver works on
  a second GPU generation.** (Turnip counts this family among those with the subgroup operations
  Source 2's Vulkan needs, which stone's 619 lacks, 3.9: Aperture Desk Job's Vulkan mode is worth a
  try here.)
- Still open: the GPU driver's delivery (B: into the runtime package); the Visual C++ installer hanging at its exit (Wine
  on FEX); the dialog view's tap is not yet seen working on a live dialog (the notice is counted as
  shown now).

## The black picture on the Nothing Phone (2026-11-21, 20:15-20:37)

With the driver in place, Ys IX ran (DXVK on "Adreno 7c+ Gen 3", music and controls working) but the
screen stayed black, at exactly 10.0 fps (100 ms a frame). Dug into (the maintainer: "this should
have been a stage 2 or 3 issue"):

1. **A diagnostic Mesa** (`tools/mesa-build/patches/diag/0007-diag-wlegl-gralloc-usage-and-handle.patch`,
   built into `build/zink-diag` with `OUT=`, a new option of `build-zink.sh`): `UTDECK_WLEGL_DEBUG=1`
   prints each `android_wlegl` server buffer's size, stride and the gralloc handle's ints;
   `UTDECK_WLEGL_USAGE` overrides the usage (default `0x300`, GPU render and texture). The handle
   (Qualcomm's, magic `msmg`): private flags `0x10100288` (display consumer, GPU texture, cached, ION,
   no CPU writer; **no UBWC**, `0x08000000`), linear, stride 2432 px for 2400, usage `0xb00` (the
   composer bit `0x800` added on the compositor's side). The compressed-buffer theory was wrong.
2. **`vktest` through Wine**: 245 fps fullscreen and 176 fps filled from 1280x720, no stall, but
   nothing seen. Three effects untangled with more runs (the maintainer watching):
   - **On 24.04-1.x, Lomiri refuses a Wayland client with no app**: "ApplicationManager REJECTED
     connection ... not launched by lomiri-app-launch, and no desktop_file_hint is specified" (never
     logged on stone's 2.x). So a window of Wine's own is not shown there; with UTDeck's
     `DESKTOP_FILE_HINT` it joins UTDeck.
   - **Inside a paused UTDeck** (state `T`) its windows get no frame callbacks: one frame, then Wine's
     100 ms timeout per frame: **Ys IX's exact 10 fps**. (Lomiri's lifecycle on 1.x; to look at.)
   - **As an app of its own** (`utdeck-vktest.desktop`'s hint and app id): frame callbacks fine
     (about 280 fps), but a white screen; also with `MESA_VK_WSI_DEBUG=sw` (shared memory): white,
     and frames stalled (0.5 fps).
3. **`gltest wayland`** (stage 1's program: zink on our Turnip, one top-level window, no Wine, no
   subsurface), as its own app: **colours while its buffers were portrait-shaped (1080x2400), white
   as soon as they were landscape-shaped (2400x1017)**, back to colours when portrait again (a
   portrait-only entry, the phone upright: 1080x2400, then 2400x1017, then 1080x2337). In landscape
   only the left 45-50 % was white, the rest black: 1080/2400 = 45 %, the panel's native width.

**Reading**: the panel is portrait; a landscape window's buffer must be turned by 90 degrees to be
shown. Mir's Android platform (`mir1/server-platform/graphics-android2.so.16`) hands buffers that
carry the composer bit to the display controller's overlays (HWComposer) when it can; turning a
landscape buffer there fails on this phone, while portrait ones need no turn. UTDeck's own Qt
windows (buffers without the composer bit) always go through Lomiri's OpenGL composition, and show.
Not proven yet. Mir's platform has the option `disable-overlays` (`MIR_SERVER_DISABLE_OVERLAYS`):
everything then goes through OpenGL composition. Lomiri runs as the user service
`lomiri-full-greeter.service`: a drop-in in the home folder would set it, and restarting Lomiri tests
it (the maintainer's OK).

### More tests (2026-11-21, 20:47-20:58)

- **Overlays off** (the maintainer's OK: a drop-in `~/.config/systemd/user/lomiri-full-greeter.service.d/utdeck-overlays.conf`
  with `MIR_SERVER_DISABLE_OVERLAYS=true`, Lomiri restarted): `gltest` in landscape not fullscreen
  (2400x1017) showed colours (51 fps; Lomiri logged "Mir buffer is gl:Texture (new)" instead of
  "TextureSource (old)"), but `vktest` fullscreen as an app of its own stayed white. **Overlays are
  not the cause**; the drop-in removed and Lomiri restarted (20:52:50).
- **A screenshot** (Volume Up and Down, which captures what Lomiri composes) while `vktest` was white:
  **all white**, 2400x1080. So Lomiri itself draws white: the white is likely its splash
  (`ApplicationWindow.qml`: shown until the surface is `surfaceInitialized`, which waits for qtmir's
  `isReady`).
- **Ys IX not fullscreen** (a test-only Wine patch, `tools/wine-build/patches-test/test-0001-no-fullscreen.patch`:
  `UTDECK_NO_FULLSCREEN=1` turns a fullscreen wish into a maximized one; a test runtime
  `11.16-20261122` = the series plus 0019, installed on the Nothing Phone only, the released
  20261121 kept next to it; `UTDECK_NO_FULLSCREEN=1` in Ys IX's `utdeck-env`): a landscape window
  below the top bar (2394x985), Wine's prefix-update window shown correctly first, then Ys IX's
  window **white, still at exactly 10 fps**, music and sounds on.

**Reading now**: `gltest` (drawing into its window's own surface) shows in that case and Wine does
not. Wine draws the Windows window into the window's surface (the white: its background) and the
game's Vulkan picture into a **subsurface** on top of it. On this phone Lomiri neither shows that
subsurface nor sends it frame callbacks (Wine waits 100 ms for each: the 10 fps). On stone the same
Lomiri version shows Wine's subsurfaces (every game there). Fullscreen landscape `gltest` (no
subsurface) also went white, which may be a second effect or the same.

### The subsurface, isolated (2026-11-21, 21:00-21:05)

`gltest` (`tools/ut-probe/gl/gltest.c`) got two options: `--windowed` (no fullscreen request) and
`--subsurface` (as Wine draws: the window's own surface a plain grey shared-memory buffer, the GL
picture in a subsurface on top, at 0,0, the window's size, desync). Run as an app of its own
(`utdeck-vktest.desktop`, landscape) by a helper on the phone (`~/utdeck-probe/gl-run.sh`), our
Turnip and zink, no Wine:

| Run | Window | Seen (the maintainer) | Frames |
|---|---|---|---|
| A | landscape, not fullscreen, **with** subsurface | **grey** | never finish: 300 frames not done after 3 minutes (stopped) |
| B | landscape, not fullscreen, no subsurface (1080x2400, then 2400x1017) | **colours** | 900 at 47.9 fps |

**So on the Nothing Phone (24.04-1.x) Lomiri neither shows a subsurface nor sends it frame
callbacks**: Mesa's presentation waits for them without a time limit (A hangs); Wine's waits time
out after 100 ms (Ys IX's 10 fps). Every Wine game draws into a subsurface. On stone (24.04-2.x)
the same Lomiri version shows Wine's subsurfaces. A second effect stays: a fullscreen landscape
window went white even without a subsurface (`gltest2`).

**The same runs on stone** (24.04-2.x, the same `gltest`, stone's own driver folders): **A, with the
subsurface: colours**, 600 frames at 38.6 fps (frame callbacks arrive); B: 600 at 43.1 fps.

| Part | Nothing Phone (24.04-1.x) | stone (24.04-2.x) |
|---|---|---|
| Lomiri | 0.5.0, built 2026-08-25 | 0.5.0, built 2026-09-10 |
| qtmir | 0.7.2, built 2026-08-25 | 0.7.2, built 2026-09-02 |
| Mir (`libmirclient9`, the server) | 1.8.3, built 2026-03-16 | 1.8.3, built 2026-09-22 |
| libhybris | git 2025-06-30 | git 2026-06-24 |
| Android vendor base | 11 | 14 |

Subsurfaces are handled in Mir's Wayland frontend, which does not depend on the device, and the
Nothing Phone's Mir is six months older: most likely a subsurface fix that reached 24.04-2.x and not
1.x. (The older libhybris or the Android 11 base could still be behind the separate
fullscreen-landscape white.)

### The Nothing Phone on 24.04-2.x (2026-11-21, 21:28-)

The maintainer upgraded the Nothing Phone (1) to **Ubuntu Touch 24.04-2.x** (channel
`24.04-2.x/arm64/android9plus/daily`; UBports' device page did not list it as supported). Its
compositor packages are now stone's exactly (Mir 1.8.3 built 2026-09-22, qtmir 0.7.2 2026-09-02,
Lomiri 0.5.0 2026-09-10, libhybris git 2026-06-24); the home folder came through untouched (the
driver folders, the setup's files, Steam and its sign-in, UTDeck 0.3.2.1x, which 2.x accepts).

- **Test A again (subsurface, landscape, not fullscreen): colours**, 600 frames at 50.4 fps. The
  subsurface bug is gone with the 2.x build: it was the 1.x one.
- Test C (plain `gltest`, asking for fullscreen): colours, but Lomiri gave it 2400x1017 below the top
  bar (`gltest` does not ask again as UTDeck's Wine does), so fullscreen was not tested by it.
- Put back to normal: Ys IX's test `utdeck-env` removed, `wine/runtime` pointed at the released
  `11.16-20261121` again (the test build `11.16-20261122` stays unused), the reference Turnip back in
  `zink/lib` (its SHA256 is `build/zink`'s), and **UTDeck's 2.x click** (0.3.2.2x) installed in place
  of the 1.x one.
- **Ys IX through UTDeck (the maintainer): fullscreen, with a picture, playable**: 1280x720, the
  game's 30 fps limit, its Sampling at 75 % (stone needs 50 % for the high teens), "a pretty stable
  30". A 4-minute measurement follows.

**Measured (2026-11-21), Ys IX on the Nothing Phone, 1280x720, Sampling 75 %:**
- *Capped at 30 fps, playing, 4 minutes*: 30.0 fps median (1 second of 120 below 28), the GPU at
  68 % median mostly at its lowest clock (315 MHz 67 samples, 450 29, 550 21, 608 3), CPU 30 % with
  the big cores at their top (2400/2515 MHz), CPU at most 64 °C, GPU 54 °C, 1.4 GB free; 10 seconds
  with a frame over 50 ms (worst 101 ms; likely DXVK compiling shaders on a GPU it has no cache
  for). The skin sensor stayed at exactly 27 °C: probably not updated on this phone.
- *The cap at 144 (the maintainer cannot turn it off), standing still, 2 minutes, three scenes*:
  a dungeon **60-65 fps (median about 62), the GPU 88-92 % at its top 608 MHz: GPU-bound**; then
  thermal throttling by the vendor's service (CPU 67-73 °C: big cores 2400 to 2054, 2515 to 2208
  MHz, the GPU's top to 550 MHz), 49-62 fps; then the town with its characters, **14-33 fps**, CPU
  54-68 % and free memory down to about 150 MB.
- *Memory then*: 7.0 of 7.3 GB used, 260 MB available, 1.9 GB in swap. **Ys IX about 5.3 GB**:
  2.8 GB in RAM plus 0.44 GB swapped out, and **2.05 GB of GPU memory** (KGSL's per-process counters:
  429 MB mapped, 1,620 MB GPU-only; KGSL in all 2,357 MB), which no process list shows; kernel about
  450 MB, CMA 384 MB. The town's assets push the phone into swap, which adds to its CPU load.
- *The same spot on stone* (the same save and settings, cap 144, 2 minutes, 23:02-23:04; a reload in
  the middle, when the game's GPU memory grew from 1.4 to 2.1 GB): steady **12.3 fps** (11.4-13.2),
  the GPU 98 % at its top 700 MHz, CPU 42-47 % (big cores 2016 MHz), the game's GPU memory 2,077 MB,
  635 MB free and 345 MB in swap.

  | Dungeon spot, 1280x720, Sampling 75 % | stone (Adreno 619, a6xx gen 1) | Nothing Phone (Adreno 642L, a6xx gen 4) |
  |---|---|---|
  | Frame rate | 12.3 fps (81 ms) | 62 fps (16 ms) |
  | GPU | 98 % at 700 MHz | 88-92 % at 608 MHz |
  | Game's GPU memory | 2,077 MB | 2,049 MB |

  **About 5x the frame rate, about 6x the work per GPU cycle**, where Android benchmarks show about
  2x. Not Turnip's tiling (3.2b: `TU_DEBUG=sysmem` gained 7 % on stone). Likely the hardware (as far
  as known, about 3x the shader cores and roughly double the memory bandwidth, which a PC game's
  shaders use and phone benchmarks less so) and Turnip's per-generation state (gen 4 is its
  best-tuned family; gen 1 lacks hardware features, so the driver takes slower ways). Splitting the
  two would take GPU performance counters on both phones: stage 7 (devices).
- *Boost* (3.5, `uclamp_min`) works here ("set on 6 threads") and is CPU-only by design; the GPU's
  clock is the vendor's GPU governor's (KGSL), which lowers it when frames wait for a cap.

So the 24.04-1.x findings (no subsurfaces, no frame callbacks, app-less clients refused) belong to
that build; a report for UBports about 1.x is only worth it if 1.x phones are to be supported.

## Swiping a game's card away left UTDeck stray (2026-11-21, stone)

The maintainer quit Ys IX by swiping its card away in the app switcher (the controller's connection
was unreliable). Afterwards UTDeck was gone from the switcher but still running, and could not be
closed. Lomiri's journal: at 23:05:25 "MirSurface[...,"utdeck.alphaliu01_utdeck"]::onCloseTimedOut()
object is still alive despite being not live and has no session. ... Will now try to ask Mir to
force close this surface. Expect strange behavior."; at 23:05:28 a UTDeck window again ("not in the
Launcher model, creating icon").

What happened: the game's card is UTDeck's (patch 0016 and the hint), so the swipe asked that window
to close (`xdg_toplevel.close`); Wine posted `SC_CLOSE` and Ys IX quit; its window object outlived
its process inside Lomiri, which force-closed it; and UTDeck, seeing the game end, showed its library
again (as built in 4.1), into an app Lomiri was closing: a stray window outside the switcher.
Quitting from the game's own menu does not do this (4.1, M2-M6). UTDeck's unit stopped by hand.

**Fixed** (0.3.3 and Wine patch **0019**, both bases): when the compositor asks a window to close,
Wine creates `<UTDECK_FOCUS_FILE>.closed` before posting `SC_CLOSE`; when the game ends, the app
(`Library.gameClosedFromOutside`) finds the marker and quits instead of showing its library. A quit
from the game's menu still brings the library back. Checked on stone (the maintainer): a swipe at
23:13:26, the marker at 23:13:27, UTDeck quit (no process, no unit, no game process; Lomiri still
logs its force-close warning for the game's window, harmless now); then a quit from Ys IX's menu:
no marker, the library back. (The test-only patch formerly numbered 0019 is
now `tools/wine-build/patches-test/test-0001-no-fullscreen.patch`.) Installed on stone: the runtime
`11.16-20261122` (the series 0001-0019; unpublished, the released 20261121 kept) and UTDeck 0.3.3.

## B: the GPU driver in the runtime (2026-11-21)

- **`build-runtime.sh` part `gpu`** (default runtime only; both Wine bases use the default runtime's
  driver): Mesa from `tools/mesa-build/build-zink.sh` (`OUT=build/runtime-obj/mesa`: Turnip and zink,
  the pinned commit, our Mesa patches **0001-0006**, so with 3.4's 0006 that the hand-made copy on
  the Nothing Phone, from `build/zink`, lacked) and Ubuntu 24.04's Khronos loader `libvulkan1`
  1.3.275.0-1build1 (from the release pocket of Ubuntu's archive, SHA256 `9459488a...a89d53e63`, the
  same in the signed index, a direct download and apt's copy). Into `lib/gpu/` with stage 1.4's
  layout: `vulkan/` (`libvulkan_freedreno.so`, `icd.json` with `./`), `gl/` (`libEGL_mesa.so.0`,
  `libgallium-26.2.5.so`, `egl_vendor.json` with `./`), `loader/libvulkan.so.1`, Mesa's `BUILD-INFO`;
  listed in the runtime's `BUILD-INFO` with their SHA256.
- **`wine-env.sh` and `steam-run.sh`** take the driver from `wine/runtime/lib/gpu`; a runtime without
  it (built before 4.3, such as the released `11.16-20261121`) falls back to `~/utdeck-probe/zink`
  and `~/utdeck-probe/vk`, which then can go.
- **Built** (`PARTS=gpu`, 2026-11-22 04:26 UTC: the runtime `11.16-20261122`, Wine 0001-0019 and the
  driver; unpublished): `lib/gpu/` as planned, Mesa's patches 0001-0006 (0006's option is in the
  driver), all four libraries in `BUILD-INFO` with their SHA256.
- **On the Nothing Phone** (24.04-2.x): the runtime by `runtime-push.sh`, UTDeck 0.3.4 (carrying the
  new `env.sh` and `steam-run.sh`), and the hand-made folders renamed `zink.handmade` and
  `vk.handmade`, so nothing else is in reach. With `env.sh` alone: `spvrun` on "Adreno 7c+ Gen 3,
  turnip Mesa 26.2.5", `gltest headless` on zink, both checks PASS. The maintainer: Steam switched off
  and on (it now runs with `VK_DRIVER_FILES` in `wine/runtime/lib/gpu`), Ys IX runs as before. No
  process maps the renamed folders. **B works**; the released runtime does not have it yet.

## Events

- 2026-11-21: 4.3 planned, before the user's own games (now 4.4): the Nothing Phone's fresh Steam
  has every first launch ahead.
- 2026-11-21: step 1 on the Nothing Phone: Ys IX's first launch followed end to end; the dialog
  page built (0.3.0-0.3.2); three launch-flow bugs fixed (the library hiding for install scripts,
  `xwin.py --close` closing dialogs, DXVK missing from a prefix not made by UTDeck).
- 2026-11-21: the black picture on the Nothing Phone narrowed down: our buffers show while
  portrait-shaped and not when landscape-shaped (likely the display controller's overlays turning
  them); 1.x Lomiri also refuses app-less clients and starves a paused app's windows of frame
  callbacks.
- 2026-11-21: overlays ruled out; the white is Lomiri's own (a screenshot); Ys IX not fullscreen still
  white at 10 fps: Wine's Vulkan subsurface is not shown and gets no frame callbacks on the Nothing
  Phone.
- 2026-11-21: the cause isolated with `gltest`: on the Nothing Phone a subsurface is neither shown nor
  given frame callbacks.
- 2026-11-21: the A/B runs on stone: its 2.x Lomiri shows the subsurface; the Nothing Phone's 1.x Mir
  build is six months older.
- 2026-11-21: the Nothing Phone upgraded to 24.04-2.x by the maintainer: subsurfaces work; Ys IX
  plays fullscreen at 30 fps (1280x720, Sampling 75 %).
- 2026-11-21: a fair comparison of Ys IX on both phones: the Nothing Phone about 5x stone's frame rate
  (62 against 12.3 fps, both GPUs at their top clocks).
- 2026-11-21: a swiped game card left UTDeck stray on stone; fixed with Wine patch 0019 and the app
  quitting after a game closed from outside (0.3.3).
- 2026-11-22: B done: the GPU driver (Mesa 0001-0006 and Ubuntu's Khronos loader) is part of the
  default runtime and used from there by games and Steam (checked on the Nothing Phone).
