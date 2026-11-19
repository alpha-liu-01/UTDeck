# Stage 2.4: Direct3D and the first game

Started and finished 2026-11-18. Plan: [stage2-roadmap.md](stage2-roadmap.md). Goal (the exit of stage 2): one
Direct3D 11 game playable on screen, with frame rate, CPU, GPU and RAM recorded. The maintainer
has no Windows game to hand, but a copy of **3DMark 11** (v1.0.3, 2012, with a key, in
`third_party/3DMark11`, not in git): a Direct3D 11 benchmark with game-like scenes, so it stands in
for the first game and gives scores. The key stays out of every log and note.

Terms used here:
- *DXVK* = Direct3D 8/9/10/11 on Vulkan, as DLLs a Windows program loads instead of Windows' own.
- *wined3d* = Wine's own Direct3D, here on OpenGL, which on this phone means zink on our Turnip.
- *DLL override* = `WINEDLLOVERRIDES`: `n` (native) loads the DLL found in the prefix (DXVK's), `b`
  (built-in) loads Wine's own.

## Steps

| Step | What | Status |
| --- | --- | --- |
| 1 | DXVK 2.7.1 in the prefix, chosen per program | **Done** |
| 2 | A Direct3D 11 sample of our own: DXVK against wined3d, GPU load | **Done** |
| 3 | 3DMark 11: install, run, scores, frame rate, CPU, GPU, RAM, temperature | **Done**: P566 (Graphics 529, Physics 1168, Combined 457) |
| 3b | A smaller picture scaled to the screen (fit by default) | **Done**: patch 0006; fit, stretch and center right (M8) |
| 4 | Sound (`winepulse`) and a gamepad (`winebus`) | **Done**: sound correct; a Bluetooth gamepad through SDL2 as an XInput controller, rumble included |

## Step 1: DXVK

Hangover's release tarball carries **DXVK 2.7.1** (`dxvk-v2.7.1.tar.gz`, the version DroidDeck and
ARMDeck pin for Adreno 6xx, stage 1.1's feature check: 41 of 41) built four ways: `x64`, `x32`,
`aarch64` and **`arm64ec`**. The ARM64EC build matters: an x86-64 game loads DXVK as native ARM64
code, so only the game itself goes through FEX, not the Direct3D layer. Installed as winetricks
does: DXVK's `d3d11`, `dxgi`, `d3d10core`, `d3d9` and `d3d8` replace Wine's copies in the prefix,
the ARM64EC builds in `system32` (64-bit programs) and the `x32` builds in `syswow64` (32-bit
programs). Chosen per run with `WINEDLLOVERRIDES`: `d3d11,dxgi,d3d10core,d3d9,d3d8=n` for DXVK,
`=b` for wined3d.

## Step 2: a Direct3D 11 sample of our own

[`tools/ut-probe/win/d3d11test.c`](../../tools/ut-probe/win/d3d11test.c) (new, x86-64): a window
(borderless fullscreen), a DXGI flip-model swapchain, HLSL shaders compiled at run time
(`D3DCompile`), one full-screen triangle per frame whose pixel shader draws a color ramp and a
moving bar, and loops `--load N` times over `sin`/`cos` per pixel to make GPU work. It handles
resizes (`ResizeBuffers`) and prints the adapter, the swapchain and the time between frames.

Both paths work on the first try (screen off, 1080x2400, MAILBOX, no vsync):

| | DXVK 2.7.1 | wined3d (OpenGL on zink) |
| --- | --- | --- |
| Adapter Windows sees | "Turnip Adreno (TM) 619", vendor 5143 | "NVIDIA GeForce GTX 470" (wined3d's fallback for a GPU it does not know) |
| Feature level | 11.0 | 11.0 |
| Device creation / shader compile | 124 / 40 ms | 444 / 28 ms |
| `--load 0` (presenting only) | 153.5 fps | 162.4 fps |
| `--load 32` | 41.1 fps | 39.6 fps |
| `--load 128` (GPU-bound) | 13.6 fps | 13.4 fps |

- With real work per pixel the two paths are equally fast: the same HLSL reaches the GPU as
  DXVK's SPIR-V, or as wined3d's GLSL through zink, and Turnip's compiler makes the same of it. With
  almost nothing per frame, wined3d is a little faster; DXVK's per-frame CPU work is larger.
- DXVK's log is clean apart from "readMonitorEdidFromKey: Failed to get EDID reg key size" (no
  monitor description in the registry; harmless, it falls back to defaults).
- The GPU clock: single samples showed 266 MHz while the GPU was 97 % busy, which would have meant
  the clock governor failed to ramp up. Sampled every second through a run, it is **700 MHz (the
  maximum) for the whole load**, 266 MHz only before and after. No throttling (32-33 °C).

## Step 3: 3DMark 11

### Installing

- **The outer installer cannot install silently.** `3DMark_11_v103_installer.exe` is InstallShield
  (2010 era); started with `/s /v/qn` it used 80 % of a core for over five minutes and never wrote
  a file beyond its first 3 MB. The reason: it is an **InstallScript** setup (`setup.inx`), not an
  MSI one, and InstallScript setups install silently only with a recorded answer file
  (`setup.iss`); without one `/s` waits. Stopped.
- **Its contents, unpacked on the PC**: 7-Zip shows one 293 MB payload, InstallShield's own file
  stream (a file count, then name, path, version and size as UTF-16 text before each file's bytes).
  [`tools/ut-probe/win/isstream.py`](../../tools/ut-probe/win/isstream.py) (new; reads only)
  unpacks it into the original `Disk1` folder: the real `setup.exe` (32-bit), `ISSetup.dll`,
  `setup.inx`, and the cabinets `data1.cab` and `data2.cab` (290 MB). Sent to the phone, checksums
  checked.
- **Box64 cannot run this setup; FEX can.** Under Hangover's default 32-bit emulator (Box64, `wowbox64.dll`)
  `setup.exe` stopped after loading `ISSetup.dll`, which is packed with PECompact2 (it unpacks its
  own code while running), and never opened a window. Under FEX (`HODLL=libwow64fex.dll`) the
  wizard opened ("3DMark 11 - InstallShield Wizard", "Extracting Files"), and the maintainer tapped
  through it on screen. Per-game setting confirmed: some 32-bit programs need FEX.
- **First wizard run cut short by our test's 120 s limit**: it had copied the data
  (`data0001.dat`, 160 MB) and was running the Visual C++ 2010 x64 runtime installer
  (`vcredist_x64.exe /passive`) when the limit ended it; no program files, no uninstall entry.
  Cleaned (the files, the `Futuremark Corporation` registry key) and started again with no short
  limit, as a user unit, recording the answers (`setup.exe /r /f1C:\3dm11-setup.iss`), so later
  installs can be silent.

- **Second wizard run (tapped through by the maintainer, 19:12-19:31): the program is installed**
  (378 MB, `Program Files\Futuremark\3DMark11\bin\x64`), with the Visual C++ 2010 x64 runtime;
  the answers were recorded (`C:\3dm11-setup.iss`: welcome, licence, setup type, start copying).
  The wizard's last part did not end cleanly: it spent 19:16-19:26 in Microsoft's .NET Framework 4
  installer (bundled as a prerequisite; it cannot work under Wine), then two InstallShield helpers
  (`ISBEW64.exe`) were still running, setup exited with 1, and no uninstall entry was written. Not
  needed to run the benchmark.

### What 3DMark 11 is made of

[`tools/ut-probe/win/pecheck.py`](../../tools/ut-probe/win/pecheck.py) (new; reads only) on
`bin\x64`:

| File | Kind |
| --- | --- |
| `3DMark11.exe` (the GUI), `3DMark11Cmd.exe` (the command-line runner) | 32-bit **.NET** (they run as 64-bit under Wine Mono: "AnyCPU") |
| `3DMark11Workload1.exe`, `3DMark11Workload2.exe` (the tests), `3DMark11Demo.exe` | 64-bit **native** x86-64, Direct3D 11 |
| `EvaDotNet.DirectX.dll`, `EvaDotNet.dll` | .NET helpers |

The workloads take their settings as XML from the runner, so the runner is the way in. It needs
**.NET**: **Wine Mono 11.3.0** (the version Wine 11.16's `appwiz.cpl` names; both packages'
SHA-256 match the ones in Wine's source) installed with `msiexec /i wine-mono-11.3.0-arm64.msi /qn`
in 23 s; it carries ARM64, x86 and x86-64 runtimes. Our `env.sh` switches Mono off
(`mscoree=`); for .NET programs `WINEDLLOVERRIDES` must not contain `mscoree`.

`3DMark11Cmd.exe` then starts under Wine Mono, through FEX, and even opens a Direct3D 11 device
through DXVK on our Turnip (3DMark checking the system), but stops with an unhandled
`COMException`: class `{f9c82199-b002-41b4-8b00-0f1eca89425f}` is not registered. It belongs to
**Futuremark SystemInfo**, whose installer (`Futuremark_SystemInfo_installer.exe`, version 4.6)
was among the 3DMark setup's prerequisites and never ran. Silently (`/s /v/qn`) it returns
**ResultCode -5** ("required data not found in the answer file"): another InstallScript setup that
needs a recorded answer file. Started once more on screen with recording
(`/r /f1C:\sysinfo-setup.iss`), for the maintainer to tap through.

### SystemInfo, registration, the GUI

- The maintainer installed SystemInfo through its wizard (answers recorded in
  `C:\sysinfo-setup.iss`). The runner still could not create the class. Two reasons, both fixed
  by hand:
  - SystemInfo's COM class is an **out-of-process service** (`LocalService`, `FMSISvc.exe`, 32-bit)
    registered only in the **32-bit registry view** (`Wow6432Node\CLSID`). 64-bit Windows lets a
    64-bit program start such a 32-bit server; Wine's COM looks only in the 64-bit view. Copied the
    class key into the 64-bit view (`reg copy ... /s`) and added the service name to its AppID
    (`LocalService=FMSISvc`).
  - The **service itself was never registered** (the installer's `register_fmsi.bat`, with
    `FMSISvc.exe /Service`, did not leave a service, and `sc create` through two shells failed on
    quoting). Written directly into `HKLM\System\CurrentControlSet\Services\FMSISvc` (own
    process, manual start, LocalSystem, the image path), which Wine's service manager reads.
- The runner then starts and prints its usage (`--definition`, `--loop`, `--out`, `--register`,
  ...), but **"Professional edition is required"** to run anything: the maintainer's key is an
  **Advanced Edition** key (checked without showing it). Registering through the runner works
  ("Registration successful"); the key went in through a temporary file on the phone, deleted at
  once, and was never printed.
- So the benchmark runs from the **GUI** (`3DMark11.exe`, .NET): with the screen off it opens
  "3DMark 11 Advanced Edition" (870x549, drawn with Direct3D 11 through DXVK on subsurfaces) and an
  "Update Available" dialog. Made a Lomiri app as in 2.3:
  [`utdeck-3dmark11.desktop`](../../tools/ut-probe/utdeck-3dmark11.desktop) (landscape) and
  [`3dmark11-app.sh`](../../tools/ut-probe/3dmark11-app.sh): DXVK, MAILBOX, landscape monitor,
  `DISPLAY` unset, Wine ended on exit, and the GPU's load and clock, the temperatures and free memory
  sampled every 2 s.

**Manual run M6 (screen on and unlocked, about 10-15 minutes, charger recommended):** from the app
drawer, open **"3DMark 11 (UTDeck test)"**. Close the "Update Available" dialog, keep the
**Performance** preset, tap **Run 3DMark**. It runs the graphics tests 1-4, the physics test and
the combined test, each in a fullscreen window, then shows the scores. Please note:
1. Does each test show its picture, filling the screen (or how)? Any crash or freeze?
2. The scores at the end: the 3DMark score (P....), Graphics, Physics, Combined, and the fps of each
   test if shown. A photo of the result screen helps.
Logs: `m6-<time>.log` and `m6-<time>-sensors.txt` in the `wine` folder.

**M6, first try (maintainer, 2026-11-18): "3DMark encountered an error".** After *Run 3DMark* a
small "Please wait" window stayed with its progress bar stuck (the word "wait" not even drawn),
then 3DMark reported an error. The log (`m6-195602.log`): the first test set the display mode to
1280x720@60 (classic fullscreen with a mode change, as the Performance preset does), then
`err:vulkan:vkCreateSwapchainKHR Exception 0xc0000005 in Unix call`: a crash on the Linux side.

### The crash: Wine drops the program's swapchain structures (patch 0005)

- Reproduced in `d3d11test` with a new `--mode WxH` option (`ChangeDisplaySettingsEx` first, then
  a fullscreen window and swapchain of that size): a 1280x720 swapchain after a 1280x720 mode change
  crashes the same way. A mode change to the screen's own size, or no mode change, does not.
- Wine offers Windows programs 96 display modes: the real one plus emulated ones (with
  `UTDECK_WAYLAND_LANDSCAPE=1`: 640x480 ... 1920x1080, 2400x1080, at 60 and 120 Hz). Under an
  emulated mode the swapchain is smaller than the real window on the panel.
- Where it crashes: Wine's exception trace gives the address; the process's memory map (sampled
  while it ran) puts it in our Turnip, `libvulkan_freedreno.so`, file offset `0x22b4ac`; with the
  unstripped zink build (same code bytes at that offset) `addr2line` names
  **`wsi_configure_image`** (Mesa's `src/vulkan/wsi/wsi_common.c`), at
  `assume(format_list_in && format_list_in->viewFormatCount > 0)`: a NULL format list, read at
  offset 16 (Wine's trace: fault address 0x10).
- Why it is NULL: in `win32u_vkCreateSwapchainKHR` (`dlls/win32u/vulkan.c`), when the swapchain is
  not the window's size Wine adds `VkSwapchainPresentScalingCreateInfoEXT` (stretch) with
  `create_info_host.pNext = &scaling;`, which **replaces** the program's whole `pNext` chain instead
  of adding to it. DXVK creates its swapchains with `VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR` and a
  `VkImageFormatListCreateInfo`; the list is lost, Mesa reads it anyway. **A Wine bug, still in
  upstream master (11.22)**: every D3D game that changes the display mode would crash this way on
  a driver with `swapchain_maintenance1`.
- [`0005-win32u-keep-pnext-chain-when-adding-present-scaling.patch`](../../tools/wine-build/patches/0005-win32u-keep-pnext-chain-when-adding-present-scaling.patch):
  one line, `scaling.pNext = create_info_host.pNext;` before chaining it in. A report for upstream
  Wine. `build-winewayland.sh` now also builds `win32u.so`.
- Found on the way: our configure had `--without-freetype` (added in 2.2 for winewayland, where it
  did not matter). For `win32u.so` it would have removed system-font rendering: Hangover's imports
  `open`/`mmap`/`fstat` and Wine's file-name helpers for its font code, ours did not. The container
  now has FreeType and fontconfig, and the script stops if FreeType is not found. Our `win32u.so`
  then has exactly Hangover's exports and imports (66 KB smaller, compiler flags).
- Result: `d3d11test --mode 1280x720` 280 fps, `--mode 1920x1080` 119 fps, no crash; Notepad still
  starts. Installed next to Hangover's (`win32u.so.hangover`, `win32u.so.utdeck`).

To watch on screen: Lomiri has no `wp_viewporter`, so nothing stretches a 1280x720 picture to the
panel (Mesa ignores Wine's stretch request, and patch 0001 had to drop Wine's own scaling): 3DMark's
tests may show at 1280x720 in part of the screen. The scores are not affected (the Performance
preset renders at 1280x720 anyway). If so, the fix is for later in this stage: let DXVK do the
scaling (it already stretches a game's back buffer to its swapchain), for example by not offering
emulated modes, so games stay at the panel's mode.

**Manual run M6, second try:** as before, "3DMark 11 (UTDeck test)" from the drawer, Performance,
Run 3DMark.

**M6, second try (maintainer, 2026-11-18, 20:18-20:43): all tests ran.** The maintainer: every
test renders correctly; the graphics tests run at about 3 fps, the physics test at about 4; the
first two demo scenes play their sound, correctly (Wine's `winepulse` to Ubuntu Touch's
PulseAudio, with no setup). Then the GUI stayed on its "please wait" screen instead of the scores.

**Why it stuck:** after the tests, 3DMark runs SystemInfo's hardware scan (`fmsiscan.exe`) to attach
a system description to the result. The scan needs SystemInfo's CPU-Z kernel driver, which Wine
cannot load (`ZwLoadDriver ... cpuz135: c0000142` in the log), and without it `fmsiscan.exe` spun at
a full core (41 minutes of CPU time) and never finished; the GUI waited for it at 80 % of a core.
Ending `fmsiscan.exe` let the GUI go on: it submitted the result to Futuremark's online service (as
3DMark 11 does by itself) and tried to open the result page in a browser, which Wine has none of
(`winebrowser: could not find a suitable app`). The result is online as 3DMark 11 result
**17035132** (the link's private key is left out here). For UTDeck: SystemInfo's scan must be
skipped for 3DMark (or SystemInfo not installed, with its COM class answered some other way), and a
browser link from a game should open Ubuntu Touch's browser.

**The results, read from the GUI's memory before the result page appeared**
([`memgrep.py`](../../tools/ut-probe/memgrep.py), new: searches a running process's memory for a
text, UTF-8 and UTF-16, reading only `/proc/<pid>/mem`): the tests hand the GUI one
`<workload_result>` each, kept in order in .NET strings; the demo's comes first with 0 fps.

| Test (Performance preset, 1280x720) | fps |
| --- | --- |
| Graphics Test 1 | 2.50 |
| Graphics Test 2 | 2.61 |
| Graphics Test 3 | 2.91 |
| Graphics Test 4 | 1.63 |
| Physics Test | 3.71 |
| Combined Test | 2.13 |

Scores, by the formulas and constants of 3DMark 11's technical guide (page 15-16: Graphics = 230 x
the harmonic mean of the four graphics tests; Physics = 315 x fps; Combined = 215 x fps; 3DMark
score = weighted harmonic mean, weights 0.75, 0.15, 0.10 for Performance):

| | Score |
| --- | --- |
| **3DMark 11 Performance** | **P566** |
| Graphics | 529 |
| Physics | 1168 |
| Combined | 457 |

The maintainer's reference for this class of hardware: about an average Core 2 Duo with Intel HD
4000 graphics, or a 32 nm AMD A8 APU. The online result page compares it with stored results.

Sensors over the run (every 2 s, [`3dmark11-app.sh`](../../tools/ut-probe/3dmark11-app.sh)):
- **GPU**: 82-87 % busy at **700 MHz** (its top clock) through the graphics tests: GPU-bound, the
  clock governor doing its job.
- **Temperatures**: CPU at most **62 °C**, GPU at most **62 °C**, in a 25-minute run; no throttling
  seen (the clock stayed at 700 MHz under load). The vendor's thermal control stays in charge.
- **Memory**: free memory at least **2.6 GB of 7.4 GB** (about 4.8 GB in use at the peak, with
  Lomiri and the rest of the phone).
- After the tests, the stuck result screen kept the GPU 26 % busy at 390 MHz.

What it means:
- **The stage 2 chain works end to end on a real Direct3D 11 program**: a 32-bit InstallShield
  installer (FEX), a .NET GUI (Wine Mono), native x86-64 D3D11 tests (FEX for the game code, DXVK as
  native ARM64EC code, our Turnip), fullscreen with display mode changes (patch 0005), sound.
- **The speed is the Adreno 619's**: 3DMark 11 is a 2010 desktop benchmark, and its graphics tests
  are GPU-bound at the GPU's top clock. Graphics 529 is about an Intel HD 4000-class integrated GPU,
  as the maintainer expected. Physics (1168) runs x86-64 physics code through FEX on the phone's
  cores.

### Scaling a smaller picture: fit by default (patch 0006)

The maintainer (M6): the tests showed exactly 1280x720, against the panel's native top edge (a side
in landscape), wallpaper around. 3DMark's "display scaling mode" was "centered" (the default; the
other choice is "stretched"). On Windows 3DMark switches the display to 1280x720 either way and the
graphics driver or the panel then centers or stretches it. What we saw is neither: Wine always asks
the host to stretch (never to center), Mesa's Wayland WSI ignores the request (it reports
`supportedPresentScaling = 0`: without `wp_viewporter` it cannot), and patch 0001 had to drop
Wine's own scaling, so nothing placed or scaled the picture.

Where to fix it:
- Mesa alone cannot: a Wayland surface takes its buffers' size, so Mesa never learns the window's
  size.
- DXVK 2.7.1 only stretches (its D3D11 present copies the whole back buffer onto the whole
  swapchain image, `cBlitter->present(..., VkRect2D(), ..., VkRect2D())`) and would only cover DXVK
  games.
- `win32u` knows both sizes and sits under every Windows Vulkan, DXVK, VKD3D and zink game.

The maintainer's choice (2026-11-18): **fit by default** (as large as possible, shape kept, black
bars), with stretch and centered available.
[`0006-win32u-scale-smaller-swapchains-when-the-host-cannot.patch`](../../tools/wine-build/patches/0006-win32u-scale-smaller-swapchains-when-the-host-cannot.patch):
only when the swapchain is not the window's real size **and** the host surface reports that it
cannot scale (`VkSurfacePresentScalingCapabilities`), Wine creates the host swapchain at the
window's real size, hands the game images of its own size (`vkGetSwapchainImagesKHR` is replaced in
the device's function table, which Wine's thunks call), and on each present clears the host image
to black and copies the game's image into it with `vkCmdBlitImage` (linear filter), placed by
`UTDECK_SCALE` = `fit` (default), `stretch`, `center` (1:1) or `off`; the present then waits for that
copy. Where the host can scale (a compositor with `wp_viewporter`, Mir 2), nothing changes.

Result with the screen off (`d3d11test --mode 1280x720`): every mode presents all frames; the copy
costs little (fit 183 fps, scaling off 189 fps). Wine's trace shows that Wine itself already maps an
emulated mode onto the panel keeping its shape: the window's real size was 1920x1080 for 1280x720 on
the 2400x1080 landscape monitor, so the scaler fills that window. Where Lomiri then places a
1920x1080 window on a 2400x1080 screen is for the screen to show.

**Manual run M7 (screen on and unlocked):** from the app drawer, **"UTDeck scaling test"**: the
D3D11 sample with a 1280x720 mode change, three times for about 10 seconds each: fit, stretch,
center. For each: where is the picture (centered, at an edge), how large, black or wallpaper around
it, stretched or not? Log: `m7-<time>.log`.

Fit works exactly as expected, stretch doesn't work and it looks exactly like fit, centered kinda works with a black square at roughly the same position as the picture in fit mode, and the picture is 720p in the center of that black square. The top bar is present in centered mode and not in the other modes. 

**M7 result (maintainer, 2026-11-18):** fit works exactly as expected; **stretch looked exactly
like fit**; centered "kinda works": a black square where the fit picture was, with the 720p picture
centered in it; Lomiri's top bar was there in the centered run (the third window opened within
seconds, as in M2 part b) and not in the others.

Why: Wine maps an emulated mode onto the panel keeping its shape (`map_rect_virt_to_raw`: 1280x720
becomes a 1920x1080 window, centered on the 2400x1080 panel), and the scaler filled that window, so
stretching inside an already fitted window changes nothing, and "centered" centered within it.

The fix, in the same patch 0006: for a window covering its monitor (a fullscreen game), the host
swapchain is made the **monitor's physical size** and Wine places the client subsurface over the
whole monitor:
- `get_monitor_physical_rect` (new, `sysparams.c`): the monitor's physical rectangle
  (`monitor_get_rect(..., MDT_RAW_DPI)`, the physical mode); under an emulated mode
  `map_rect_virt_to_raw` gives only the fitted area (first try: "the monitor" came out 1920x1080);
- `vulkan_scaler_get_monitor_rect` (new, `vulkan.c`), used by `get_client_surface_rects`
  (`window.c`): the client surface's rectangle becomes the monitor, relative to the toplevel, so
  the subsurface sits at (-240, 0) of the 1920x1080 window, which Lomiri centers on the panel.
  No structure changes, so no driver's binary interface changes.

Started through Lomiri's launcher (screen off): fit now scales "1280x720 into 2400x1080 (the
monitor) at (240,0)-(2160,1080)", 1,500 frames, 105 fps at `--load 4`. The stretch and center parts
did not run that time (the test app was ended after its first window closed, with the screen off).

**Manual run M8 (maintainer, 2026-11-18):** the scaling test again, and the gamepad through
[`gptest.sh`](../../tools/ut-probe/gptest.sh).

**Scaling: all three modes are right.** Fit, stretch and center as intended, no wallpaper in any of
them, and no top bar in the centered run any more.

**The gamepad works, rumble included.** A Betop T6 Bluetooth controller (`input7: BETOP T6`, plus
`/dev/hidraw0`, group 1018, not readable by the user):
- Wine's `winebus` took it through **SDL2** (the phone's `libSDL2-2.0`), which maps it as an Xbox
  controller (`045e:02e0`, `is_gamepad 1`, "creating non-hidraw device ... usages 0001:0005").
- `gptest.exe`: **XInput controller 0** (type 1, subtype 1: a gamepad), and through winmm "Wine
  joystick driver", 10 buttons, 5 axes.
- 66 input events in 30 s: buttons A, B, X, Y, LB, RB, both stick clicks and the D-pad (Back and
  Start not pressed this time); both sticks over their full range (-32768 to 32767 on each axis);
  left trigger up to 229, right trigger to 255.
- Rumble: `XInputSetState` reached SDL (`sdl_device_haptics_start ... duration_ms 1000`) and the
  controller vibrated.
- Nothing to set up: the user is in the groups that read `/dev/input`, and SDL does the mapping.

## Result

**Stage 2.4 is done, and with it stage 2 (2026-11-18).** A real Direct3D 11 program, 3DMark 11,
installed by its own 32-bit installer and run from its .NET GUI, ran all its tests on screen,
correctly drawn, with sound, scaled to the screen, at the speed of the Adreno 619 (P566: Graphics 529,
Physics 1168, Combined 457; GPU-bound at 700 MHz, 62 °C at most, at least 2.6 GB of memory free).
A Bluetooth gamepad works as an XInput controller, with rumble.

Honest note on "playable": 3DMark 11's tests run at 2-4 fps here; it is a 2010 desktop benchmark at
1280x720, and its score puts this phone near an Intel HD 4000. A real game of the right weight
(older or lighter titles, or lower settings) is for stage 3's game list.

What stage 2.4 added to UTDeck's runtime:

| Piece | Why |
| --- | --- |
| DXVK 2.7.1, ARM64EC build for 64-bit games, x32 for 32-bit | Direct3D 8-11 on our Turnip, with DXVK running as native code |
| Wine Mono 11.3.0 in the prefix | .NET launchers and tools (3DMark's GUI and runner) |
| Wine patch 0005 (`win32u`) | games that change the display mode crashed in `vkCreateSwapchainKHR` (upstream bug) |
| Wine patch 0006 (`win32u`) | a smaller picture is scaled by Wine: fit (default), stretch, center (`UTDECK_SCALE`) |
| FEX for some 32-bit programs (`HODLL=libwow64fex.dll`) | Box64 stalls on self-unpacking code (3DMark's installer) |
| skip bundled .NET/DirectX installers, hardware scanners | they cannot work under Wine and hang |

Open, carried to stage 3: SystemInfo-style scanners must be skipped (3DMark's result page needed its
scan ended by hand); a browser link from a game should open Ubuntu Touch's browser; upstream reports
for patches 0005 (Wine) and the earlier Mesa ones; a game list of the right weight.

## Events

- 2026-11-18: DXVK 2.7.1 (Hangover's ARM64EC and x32 builds) installed in the prefix.
  `d3d11test.c` written; DXVK and wined3d both run it, equally fast when GPU-bound. 3DMark 11
  installer identified (InstallShield) and started silently (`/s /v/qn`) as a user unit.
- 2026-11-18: the silent install waits forever (InstallScript without an answer file); payload
  unpacked on the PC with `isstream.py`; Box64 stalls on PECompact2-packed `ISSetup.dll`, FEX runs
  the wizard; the maintainer tapped through it; first run cut by our 120 s limit; cleaned and the
  wizard started again with answer recording. `d3d9test.c` written (x86 and x86-64).
- 2026-11-18: 3DMark 11 installed by the maintainer's second wizard run (answers recorded; the
  bundled .NET 4 installer wasted 10 minutes and the end did not finish cleanly). `pecheck.py`
  written: the GUI and runner are .NET, the tests native x86-64. Wine Mono 11.3.0 installed. The
  runner stops on a COM class of Futuremark SystemInfo; its silent install fails (-5, needs an
  answer file); started on screen for the maintainer.
- 2026-11-18: SystemInfo installed by the maintainer; its COM service registered by hand (64-bit
  view, AppID, service key); the runner needs the Professional edition, the key is Advanced;
  registered through the runner (key never shown). The GUI opens; Lomiri app and sensor logging
  made; manual run M6 written.
- 2026-11-18: M6 first try: 3DMark's first test crashed in `vkCreateSwapchainKHR` after a 1280x720
  mode change. Reproduced (`d3d11test --mode`), traced to Mesa's `wsi_configure_image` reading a
  format list that Wine's `win32u` had dropped from the `pNext` chain (upstream bug); patch 0005;
  `win32u.so` built with FreeType after finding the font code missing without it. Fixed in the
  reproduction. M6 second try asked.
- 2026-11-18: M6 second try: all six tests ran and rendered correctly, sound correct. The GUI stuck
  on SystemInfo's scan (`fmsiscan.exe` spinning without its CPU-Z driver); results read from the
  GUI's memory (`memgrep.py`), scores computed from the technical guide: P566. Ending the scan let
  the GUI submit the result online (17035132). Sensors: GPU-bound at 700 MHz, 62 °C peak, 2.6 GB free.
- 2026-11-18: the 1280x720 picture was not scaled (maintainer, M6). Decided: fit by default, stretch
  and centered as options. Patch 0006 (Wine scales smaller swapchains when the host cannot) built;
  works with the screen off at little cost; Wine already fits an emulated mode into a 1920x1080
  window. Scaling test app and manual run M7 written.
- 2026-11-18: M7 by the maintainer: fit right; stretch like fit, centered within the fitted window.
  Patch 0006 extended: fullscreen windows are scaled over the monitor's physical area. Screen-off
  check through Lomiri's launcher: fit over the whole monitor. Gamepad test `gptest.c` and
  `gptest.sh` written. Manual run M8 written.
- 2026-11-18: M8 by the maintainer: fit, stretch and center all right; the Betop T6 gamepad works
  through SDL2 as an XInput controller, rumble included. Stage 2.4 done; stage 2 done.
