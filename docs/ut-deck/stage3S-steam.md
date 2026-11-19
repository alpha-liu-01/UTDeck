# Stage 3.S: Steam, far enough to test games

Started 2026-11-19. Plan: [plan-change-steam-early.md](plan-change-steam-early.md) (route A: Valve's
ARM64 Linux client, with UTDeck's runtime as its compatibility tool). Goal: the maintainer's Steam
games downloaded and started through UTDeck's runtime, with Steam's API working.

Terms used here:
- *Steam client* = Valve's program (`steamrtarm64/steam`); its interface is a Chromium-based
  program, the *web helper* (`steamwebhelper`, built on CEF, the Chromium Embedded Framework).
- *Steam Runtime 3c* (`steamrt3c`) = the libraries the ARM64 client is built against (Debian 11
  base); on the Steam Frame the client's parts run inside it.
- *SDL3* = the library the web helper opens its window with and reads controllers through.
- *kopper* = how zink (OpenGL on Vulkan) shows its pictures: through Vulkan's window system
  support. On this phone it is the only way OpenGL can reach the screen.
- *composer* = the web helper's part that puts Chromium's pictures into a window (Steam Frame
  code: `browsercomposer_system`, `_gl`, `_gamescope`).

## Steps

| Step | What | Status |
| --- | --- | --- |
| 1 | The ARM64 Linux client on Ubuntu Touch: downloaded, checked, updated, started | **Done** (M11) |
| 2 | Log in (the maintainer, on the screen) | **Done** (M11) |
| 2b | Steam's own interface usable: touch in the right half, typing, speed | **Parked**: not needed to test games (sign-in once, downloads from the Steam phone app); known issue K2 |
| 2c | Hidden Steam: a background service, no window, signed in by itself | **Done**: 1.33 GB, 13 % of one core at idle; `steam-service.sh launch` keeps its window away |
| 3 | UTDeck's runtime as a compatibility tool; a Windows game downloaded | **Done**: downloaded by remote install (M12); our tool is Steam's default for Windows games |
| 4 | A game without Steam DRM started through our runtime | No such game in the library; covered by step 5 |
| 5 | Proton's `lsteamclient` built against our Wine; a Steamworks game | **Done** (M13): Ys passes its Steam check; launcher and game run; audio, touch, controller work |
| 5b | A game in a smaller window fills the screen (found with Ys) | **Done** (M14): fill mode, Wine patch 0010; started sideways, known issue K1 |
| 6 | Start and quit cleanly, twice; hidden Steam back to idle | **Next** |
| 7 | Games' videos play (Ys: WMV) | To do |

Exit of 3.S: steps 6 and 7 done, the notes and the roadmap up to date; then 3.2b.

## Known issues

Recorded, not blocking the tests of stage 3:

| # | Issue | Workaround now | Where it gets fixed |
| --- | --- | --- | --- |
| K1 | A game started by hidden Steam with the phone **sideways** gets a portrait fullscreen size from Lomiri, and Lomiri then stops rotating its window (M15). Started upright, every rotation works (M14) | Start games with the phone upright, then turn it | Stage 4: games started through UTDeck's library, a Lomiri app with a desktop file that declares landscape (as in stage 2.3, M4) |
| K2 | Steam's own interface is drawn in software (2-5 fps), touch in its right half lands on the middle, no on-screen keyboard for X11 (M11) | Not needed: sign-in once; buy and download from the Steam phone app; games started by `steam-service.sh launch` | Stage 4 or 7: UTDeck's own library and dialogs; a viewer or Steam's Gaming Mode later |
| K3 | A controller connected while a game runs is not seen by it (M13) | Connect it before starting the game | Stage 5 (controllers) |
| K4 | Steam runs the game's install script again at every start (about 3 s; all steps skipped) | None needed | Later in stage 3, if Proton does not do the same |
| K5 | Each game's prefix is about 1 GB (Wine copies its DLLs in), plus one for Steam's driver query (app 0) | 60 GB free | Later in stage 3: hard links to the runtime's files |
| K6 | A game's exclusive fullscreen at the panel's resolution (Ys "Fullscreen") is too heavy and cannot be scaled down | Borderless or windowed at a smaller size: fill mode shows it on the whole screen | By design (the game chooses the resolution) |
| K7 | Steam's Direct3D 12 driver query fails (`E_INVALIDARG`) | None needed: the A619 has feature level 11_1 (stage 3.1); Steam only reports it | Not a problem |
| K8 | Steam's overlay and pipeline cache (fossilize) are off for games | None needed for tests | Overlay: stage 7; fossilize: measured in 3.3-3.6 |
| K9 | 32-bit games run under Box64; Steam's installers under FEX (Box64 hangs in the VC++ installer) | Done in `utdeck-compat` | Which emulator for games: measured in 3.3-3.6 |

## Next steps

1. **Step 6, start and quit (M16, the maintainer, a few minutes).** Ys started twice with
   `steam-service.sh launch 587110` (phone upright, K1), quit from the game's menu each time.
   Checked from the PC: no Wine, `steam.exe` or `reaper` process left; Steam's log shows the game
   ended; hidden Steam back to its idle CPU and memory (13 %, 1.33 GB before), with its cost while
   a game runs measured too.
2. **Step 7, games' videos.** The phone's GStreamer cannot decode WMV or read ASF (Ys's
   `op.wmv`). Plan: a GStreamer FFmpeg plugin (`gst-libav`) with a small FFmpeg built only with the
   decoders and readers games use (WMV/WMA in ASF, H.264 and AAC in MP4, VP8/VP9 in WebM, Theora),
   from pinned sources in the cross container, shipped in the runtime and found through
   `GST_PLUGIN_PATH`; checked with Ys's opening movie. (Proton ships its own GStreamer and FFmpeg
   for the same reason.)
3. **Close 3.S:** notes, the stage 3 roadmap, lessons; a commit when the maintainer asks.
4. **Stage 3.2b, the game table from the Steam library:** five or more of the maintainer's games
   covering Direct3D 9, 11 and 12, 32- and 64-bit, several engines (Unity, Unreal, XNA,
   GameMaker); each one's size checked before the remote download, its folder checked, then run
   with the frame and sensor logs (`game-run.sh` through `utdeck-compat`), phone started upright.
   Ys is the first entry.

## Step 1: the client

### Installing ([`tools/ut-probe/steam-install.sh`](../../tools/ut-probe/steam-install.sh))

ARMDeck's method (`userspace/steam/stageA_steam.sh`, from pocknix-os), on Ubuntu Touch itself
instead of a container, into `~/.local/share/Steam` (nothing system-wide):

- Phase 1 (40 s): the Steam Runtime 3c for arm64 (`steamrt3c` 3c.0.20261103, from
  repo.steampowered.com, SHA-256 checked) and the client from the `steamdeck_stable` arm64
  channel (version 1792618285; its archive's name ends with its SHA-1, checked). 549 MB.
- **The client archive was made on Windows**: three symbolic links in it are named
  `steamrtarm64\libs\libcurl.so` and so on, with backslashes; `unzip` creates them as odd files in
  the top folder. The script moves them into `steamrtarm64/libs/`.
- Phase 2 (3 minutes): the client updates itself (668 MB) on Lomiri's Xwayland (display `:1`, which
  Lomiri starts for X11 programs) and exits. 3.3 GB in all.
- Missing on Ubuntu Touch after the update: only GTK 2 (`libgtk-x11-2.0.so.0`, for `steamui.so` and
  `vgui2_s.so`). The runtime has it under its full version name only; the script links it (and
  `libgdk-x11-2.0`) into Steam's own library folder by name prefix, never the C library.

### Starting ([`tools/ut-probe/steam-run.sh`](../../tools/ut-probe/steam-run.sh)): what it took so far

The ARM64 client is built for the Steam Frame, where it runs inside the Steam Runtime on gamescope
(a Wayland compositor). On Lomiri each of these had to be found and fixed:

1. **Ubuntu Touch sets `SDL_VIDEODRIVER=wayland,x11` for every app.** The SDL3 Valve ships next to
   the client has no Wayland driver at all (only `x11`, `offscreen`, `dummy`, `evdev`, `openvr`, found
   by loading it from Python) and stops at the first name. Then it turned out the web helper asks
   for Wayland itself in any case: "SDL_Init failed: wayland not available", and a restart every
   10 s.
2. **The runtime's SDL3 (3.4.18) has Wayland and connects to Lomiri, but is too old for the
   client** (`steamui.so`: undefined symbol `SDL_TryLockJoysticks`). So we build SDL3 ourselves at
   the exact commit Valve ships (`SDL-release-3.4.0-1576-gab5541ec5`, from `strings`), with X11 and
   Wayland: [`tools/steam-build/build-sdl3.sh`](../../tools/steam-build/build-sdl3.sh). Its exported
   functions are the same 1,319 as Valve's. All Steam binaries find their libraries through
   `RUNPATH`, so a folder first in `LD_LIBRARY_PATH` (`Steam/utdeck-lib`) is enough to swap it.
3. **With Wayland, SDL3 loads EGL, and Ubuntu Touch's system EGL is libhybris** (Android's
   driver): "cannot allocate memory in static TLS block", as Wine in stage 2.1. Steam now uses our
   Mesa's EGL and our Turnip, as `wine/env.sh` does.
4. **Not `MESA_LOADER_DRIVER_OVERRIDE=zink`, though**: the client's own small X11 window uses the
   system's GLX, which that variable breaks ("failed to load driver: zink", then a fatal assert in
   `vgui2`). Instead, **Mesa patch 0003 now chooses zink by default** when no DRM device works (our
   EGL is only used on such phones), unless the variable names another driver.
5. **Valve's `steamwebhelper.sh` sets `LIBGL_KOPPER_DISABLE=true`** ("to get store working properly"),
   which leaves zink no way to show anything on this phone: EGL could not start, and the composer
   failed with "Invalid window". Shown with a small SDL3 test (an OpenGL window on Wayland: works
   without the variable, "Could not initialize EGL" with it). **New Mesa patch 0004**: where zink was
   chosen because no DRM device works, the variable is ignored. With it, an SDL3 OpenGL window drawn
   by zink on Turnip appears on Lomiri even with `LIBGL_KOPPER_DISABLE=true` (screenshot).
6. Where it stands: the web helper starts, loads the interface (the logs show the login window being
   created, 700x440), its renderer and GPU processes run, and its composer creates its output
   window, but **nothing appears on the screen, and the client restarts the web helper every 10 s**
   (`steamui_html.txt`: "Restart webhelper process", 6 s after "GetDesiredSteamUIWindows: ... 1
   windows"). Checked so far:
   - Lomiri shows windows from programs started over adb, X11 and Wayland alike (SDL3 test windows,
     screenshots), so the display side is not the problem.
   - Chromium's GPU process draws with Ubuntu Touch's own Mesa in software (llvmpipe) on X11; the
     composer's window was on Wayland (the session's `SDL_VIDEODRIVER=wayland,x11`). With
     `SDL_VIDEODRIVER=x11` (now the default in `steam-run.sh`) both are on X11; the restarts stay.
   - No thread is stuck in SDL, EGL, Mesa or Vulkan (`stackscan.py` on the browser and GPU
     processes); SDL3 creates windows and GL contexts from a second thread without trouble (test).
   - `-cef-disable-hang-timeouts`, `-cef-disable-gpu -cef-disable-gpu-compositing`: no change.
   - Without an X11 display the client itself stops ("Unable to open X11 display"): it needs X11.
   - The client starts the runtime's `steam-runtime-launcher-service` by name; with the runtime's
     `pressure-vessel/bin` in `PATH` it now runs (it "exited" before). No change to the restarts.
   Candidates left: the client-web helper link (the client logs "Created mapping
   SteamChrome_MasterStream... when set to fail if created" at every restart, so the web helper may
   not attach to the shared memory the client expects), the composer mode
   (`OverrideBrowserComposerMode`, the GL composer), and running the web helper inside the Steam
   Runtime through `steam-runtime-launcher-service`, as on the Steam Frame (it fails to start here).
Also found:
- The web helper is pinned to CPUs 2-6 (`taskset 0x7c` in `steamwebhelper.sh`, made for the Steam
  Frame's cores); on this phone that is mostly the little cores. For later.
- **Killed Steam runs leave their shared memory in `/dev/shm`** (RAM): 1.1 GB after these tests.
  `steam-run.sh` now removes it when no Steam process is left.
- The web helper sends its crash reports to Valve by itself (Steam's normal behaviour).

### The cause: SDL's Ubuntu Touch rule (found 2026-11-19)

Reading Steam's web helper in a disassembler (its composer's `CreateOutputWindow`, located through
its log strings) and SDL's source gave the whole picture:

- **SDL3 forces its Wayland driver on Ubuntu Touch.** `SDL_VideoInit` has
  `if (SDL_IsUbuntuTouch()) driver_name = "wayland";` (SDL issue 12247), and `SDL_IsUbuntuTouch()`
  is true when `XDG_SESSION_DESKTOP=ubuntu-touch`, which Lomiri's session sets. `SDL_VIDEODRIVER`
  and hints are then ignored. That is why Valve's SDL3 (built without Wayland) said "wayland not
  available" whatever was set, and why `SDL_VIDEODRIVER=x11` changed nothing.
- **Steam's system composer needs an X11 window**: after `SDL_CreateWindowWithProperties` it reads
  `SDL.window.x11.window` and `SDL.window.x11.display` and calls `XChangeProperty`. Before that, a
  helper (`SDL_WasInit`, then `SDL_InitSubSystem`) failing is what printed "Failed to initialize
  SDL_INIT_VIDEO" (the "Invalid window" after it is only a stale SDL error).
- **Fix: `steam-run.sh` unsets `XDG_SESSION_DESKTOP` and sets `SDL_VIDEODRIVER=x11`.** SDL then
  behaves as on any Linux, the composer gets an X11 window (on Lomiri's Xwayland), the web helper
  is no longer restarted, and the log shows "SP DesktopLoginWindow ... Created window ... mode:
  System window", then the "Sign in to Steam" page loaded and visible. The screenshot was black only
  because the screen had switched itself off.
- With that, **Valve's own SDL3 works**: our SDL3 build is not needed (kept as
  `UTDECK_STEAM_OWN_SDL=1`), and Mesa patches 0003/0004 are not needed by Steam either. They stay:
  they make our EGL work with no variables and through `LIBGL_KOPPER_DISABLE`, which helps any
  program that ends up on our EGL. (SDL's own source asks that no AI-generated changes be sent to
  SDL; nothing of SDL's is changed here, and nothing will be sent upstream from this work.)
- Also kept: `-cef-disable-breakpad` in the launcher, so crash reports are not uploaded while we
  test (each one also left a `steamerrorreporter` process holding the web helper's sockets).

### Manual run M11: the login (screen on and unlocked)

Start **Steam (UTDeck test)** from the app drawer. Expected: a small "Sign in to Steam" window
(700x440). Sign in (password and Steam Guard, or the QR code with the Steam app); everything typed
stays on the phone. Then let it run for a minute and quit from Steam's menu (Steam > Exit), or tell
me and I end it from adb.

Questions:
1. Did the sign-in window appear, and could you type into it (on-screen keyboard or a keyboard)?
2. After signing in: what did you see (the library, a black window, nothing)?
3. Anything else: speed of the interface, touch, rotation.

Answers (maintainer, 2026-11-19): **Steam is logged in correctly.** The window takes the whole
screen. The interface runs at about 2-5 fps. Lomiri's on-screen keyboard does nothing in it. Touch
is right on the left half of the screen; on the right half every touch lands on the middle line:
on the 2400x1080 panel, a touch at (1800, 900) arrives at (1200, 900), one at (2000, 1000) at
(1200, 1000).

What the phone shows about it:
- Started from the drawer, Steam got **display `:2`**: Lomiri started a new Xwayland for it (the
  `:1` of earlier tests was gone). `steam-run.sh`'s `:1` default is only for shells; to fix.
- **The X11 screen is portrait, 1080x2400** (one monitor, 70x155 mm), as Lomiri reports the panel
  to every client (stage 2.3: Lomiri shows rotation only in the sizes it asks windows to take). Steam's
  window is landscape (its log: 2398x806, then 2384x931). Xwayland keeps pointer positions inside
  its screen, so touches beyond its width are cut off near the middle. Wine's Wayland driver had the
  same Lomiri quirk (patch 0003); Xwayland is not ours to patch.
- **The keyboard**: Lomiri's on-screen keyboard talks to Wayland programs through an input-method
  protocol; X11 programs under Xwayland do not get it.
- **The speed**: Chromium draws the interface in software (Ubuntu Touch's llvmpipe through GLX on
  Xwayland; Xwayland here has no GPU path, `-shm` only). The web helper used **220 % CPU** and
  about **700 MB** in each of its two big processes, and Valve's script pins it to CPUs 2-6 (four
  little cores and one big one).

Next (step 2b):
1. **Touch: a rootful Xwayland of our own** for Steam, a normal Wayland window with a landscape X11
   screen we choose (`Xwayland :N -geometry 2400x1080 -fullscreen`; Xwayland 23.2.6 supports it).
   Lomiri then sends touches into that window like any app's, and the X11 screen matches the window.
2. **Speed**: let the web helper use all cores (Valve's script calls `taskset 0x7c`; a `taskset` of
   ours earlier in `PATH` can drop that mask), compare `-cef-disable-gpu` (Chromium's own software
   drawing instead of llvmpipe), and measure with the frame counter idea from stage 3.2.
3. **Typing**: Steam's own on-screen keyboard (in its gamepad interface, `-gamepadui`, which is also
   the touch- and controller-friendly one for a handheld), or a hardware keyboard. Lomiri's keyboard
   cannot reach X11 programs.

### Hidden Steam (decided 2026-11-19, [plan-change-steam-early.md](plan-change-steam-early.md))

[`tools/ut-probe/steam-service.sh`](../../tools/ut-probe/steam-service.sh) `start|stop|status`:

- **Lomiri pauses apps that are not in front**: the drawer-started Steam was found with every
  process stopped (state `T`, SIGSTOP). Games need the client awake (Steam's DRM), so Steam runs as
  a systemd user unit (`utdeck-steam`), outside Lomiri's app handling.
- **Its X11 display is an Xvfb of our own** (`:50`, 1200x540; a virtual screen in memory), unpacked
  from Ubuntu's `xvfb` package (`apt-get download`, `dpkg-deb -x` into `~/utdeck-probe/xvfb`; every
  library it needs is already on the phone). Lomiri's Xwayland is started and ended by Lomiri with
  its X11 apps (display `:1`, then `:2`), so it cannot be relied on for a background service.
- Steam is started with `-silent` (no window). **Exit code 42 means "updated, start me again"**:
  the first start updated the client to version 1794535744 and stopped there; `steam-run.sh` now
  starts it again, as Valve's `steam.sh` does.
- **It signs in by itself** with the token saved at M11 (connection log: "Logged On").
- Measured (2026-11-19, 3 minutes after start, nothing open): **1.33 GB** (PSS, 10 processes;
  the web helper's renderer about 500 MB and its GPU process about 390 MB, both still loaded with no
  window) and **13 % of one core** over 60 s. With the library window open (M11): about 2 GB and
  220 %.

### M12: a remote download from the Steam phone app (2026-11-19)

With hidden Steam running, the maintainer installed **Ys: Memories of Celceta** (appid 587110,
bought on Steam; Direct3D 11, 32-bit) from the Steam app on another phone. The UTDeck phone showed
up as a device and the download started at once, with no Steam window: `appmanifest_587110.acf`
(2.43 GB to download, 5.06 GB on disk) and `logs/content_log.txt` show its progress, as UTDeck's
library page will read them. (The maintainer has no free games on Steam; any owned game serves, and
free-to-play ones often use anti-cheat that Wine cannot run anyway.)

Steam also fetched, by itself, what it would run a Windows game with: Proton Experimental (appid
1493710), Steam Linux Runtime 4 (4183110), its FEX package (3127680) and the Steamworks
redistributables (228980). UTDeck will not use Proton Experimental (its DXVK 3 fails on Adreno 6xx,
`docs/compat-perf-audit.md`; it cannot present on Lomiri either); with our runtime set as Steam's
default compatibility tool (step 3) these downloads should not be needed.

This game uses Steamworks (Steam's DRM and API), so it is also the test for step 5: it will need
the `lsteamclient` bridge to find the running client.

### New tools

- [`tools/ut-probe/screenshot.sh`](../../tools/ut-probe/screenshot.sh): a screenshot of the phone's
  screen from the PC, through Mir's `mirscreencast`. From an adb shell it needs the session's glibc
  tunable (`glibc.rtld.optional_static_tls=4096`), as every libhybris program does. Screenshots stay
  in `build/` (not in git): they can show personal data.
- [`tools/ut-probe/stackscan.py`](../../tools/ut-probe/stackscan.py): a rough backtrace of a stuck
  process without a debugger (each thread's stack read through `/proc/PID/mem`, return addresses
  listed as library+offset).
- [`tools/ut-probe/steam-push.sh`](../../tools/ut-probe/steam-push.sh): copies the scripts and our SDL3
  to the phone.

## Step 3: UTDeck's runtime as Steam's compatibility tool

### How the ARM64 client runs Windows games by itself

Read from the files Steam downloaded and from `linuxarm64/steamclient.so` (disassembled on the PC):

- **Proton Experimental here is the x86-64 build** (`experimental-11.0-20261112-x86_64`: `files/lib`
  has only `x86_64-*` and `i386-*` folders). Steam does not run an ARM64 Proton: it runs the whole
  x86-64 Proton, its Wine and the game through **FEX** (appid 3127680, `fex-compat-tool`, with its own
  x86-64 root filesystem), inside Steam Linux Runtime 4's container (*pressure-vessel*, the container
  Steam runs games in).
- The chain comes from the tools' manifests: Proton's `toolmanifest.vdf` says
  `require_tool_appid 4183110` (SLR 4), and the client adds FEX under a tool when its name starts
  with `SteamLinuxRuntime_` and does not end in `-arm64` (the code next to the message
  `DEBUG: adding FEX dependency to %s %u`). A tool that requires nothing gets neither the container
  nor FEX.
- So Steam's own path would emulate Wine itself, its Unix side included; UTDeck's runtime runs Wine
  as ARM64 code and emulates only the game's x86 code (ARM64EC with FEX's library for 64-bit games,
  Box64's for 32-bit ones). That, and presenting on Lomiri (route B), are why the tool is ours.

### The tool ([`tools/ut-probe/steam-compat/`](../../tools/ut-probe/steam-compat/))

Installed by `steam-push.sh` into `~/.local/share/Steam/compatibilitytools.d/utdeck`:

- `compatibilitytool.vdf`: the tool `utdeck`, shown as "UTDeck runtime", from `windows` to `linux`.
- `toolmanifest.vdf`: `commandline "/utdeck-compat %verb%"`, no `require_tool_appid` (so no container
  and no FEX layer), Steam's `reaper` kept (it lets Steam end the whole process tree on "Stop").
- `utdeck-compat`: the verbs Proton has. `run` and `waitforexitandrun` start the game through
  `game-run.sh`, so a game started by Steam gets exactly the environment and the logs of 3.2 (our
  driver, landscape, MAILBOX, frame statistics, sensors) in `wine/games/steam-<appid>-<time>.*`.
  `getcompatpath`/`getnativepath` (path conversion), `runinprefix` and `destroyprefix` as in Proton.
  Each game gets its own prefix, `steamapps/compatdata/<appid>/pfx`, made on its first start.
  Every call is logged to `compatdata/<appid>/utdeck-compat.log` with the verb, the command line
  and the variables Steam passed, only those whose names cannot hold account data (Steam passes
  `SteamUser`, the account name: names only for the rest).
- Steam's Vulkan layers for games (overlay, fossilize pipeline caching) are switched off by the
  script for now; fossilize gets its own measurement later.
- `set-default.py`: makes the tool Steam's default for all Windows games without the settings
  window: the `CompatToolMapping` entry for app `0` in `config/config.vdf`, which the settings page
  "Enable Steam Play for all other titles" writes. Run on the phone with Steam stopped (Steam
  rewrites the file while running); only that block is touched, the previous file stays on the
  phone as `config.vdf.utdeck-bak`, and only the mapping block is printed.

Not solved yet: a prefix is about 1 GB, because Wine copies its DLLs into it; Proton shares one
template prefix by links. Hard links to the runtime's files (same file system) would bring a game's
prefix down to its own files; for later in stage 3. Steam also gets a prefix for app 0 (its
Direct3D driver query, below), so two such prefixes exist after the first game.

### Results (2026-11-19, no screen needed)

After `set-default.py` and a Steam restart, `logs/compat_log.txt` shows the tool loaded and mapped:
`Registering tool utdeck`, `Mapping AppID 0 to tool "utdeck" with priority 75`, and its command
`.../utdeck/utdeck-compat run` with no FEX or container in front. Valve's own per-game choices
(`non-user mapping ... priority 100`, to Proton) would win over that default for the games they
name; Ys is not among them. `steam -applaunch 587110` sent to the hidden Steam then gave:

| Call from Steam | What it is | Result |
| --- | --- | --- |
| `run d3ddriverquery64.exe`, then `-d3d12` (app 0) | Steam asks Direct3D what the GPU is, once per start of Steam | Runs: DXVK and VKD3D-Proton find "Turnip Adreno (TM) 619"; the D3D12 query's `D3D12CreateDevice` returns `E_INVALIDARG` (to look at: stage 3.1's D3D12 test works) |
| `run iscriptevaluator.exe legacycompat\evaluatorscript_587110.vdf` | the game's install script: Steamworks redistributables (VC++ 2015 and 2017, DirectX June 2010, .NET 4.7) | First exit 1 (wrong folder), then hung in VC++ 2015 under Box64; after both fixes **exit 0 in about 3 min** (`XAudio2_7` registered, Microsoft's `vcruntime140.dll` in the prefix) |
| `waitforexitandrun Ysc_dx11.exe` | the game (Steam picks `Ysc_dx11.exe`, not the launcher) | Runs under Box64 (32-bit), loads Valve's `steam_api.dll`, DXVK's `d3d11.dll` and `dxgi.dll`, never tries `steamclient.dll`, **exits with 53 before a window**: its Steam check fails, as expected without `lsteamclient` (step 5) |

What it took (each is in [lessons-learned.md](../lessons-learned.md)):

1. Keep Steam's working folder. Install scripts get paths relative to Steam's folder; changing into
   the program's folder lost the script (exit 1). `game-run.sh` is now called with Steam's folder.
2. `printf`, not `echo`, for arguments: the phone's `sh` turned `\e` in a Windows path into an
   escape character in our log.
3. `game-run.sh` returns the program's exit code (it returned `wineserver -k`'s).
4. Steam's install scripts run their 32-bit code under FEX (`HODLL=libwow64fex.dll`): under Box64
   0.4.4 the VC++ 2015 installer spun at 100 % of a core for 10 minutes; under FEX it finished in
   8 s. Games keep the runtime's default (Box64) until 3.3-3.6 measure both.
5. `VK_INSTANCE_LAYERS` (two Steam Frame layers this phone does not have) and `TU_DEBUG_FILE` (a
   file that changes Turnip's debug options while the game runs) are cleared.

Also seen: `SteamService.exe` (Steam's Windows service, used by the install script) logs "Failed to
find Steam executable": the prefix has none of Steam's registry entries. Proton's `steam.exe`
writes them; that is part of step 5.

## Step 5: Steam's API for Windows games (`lsteamclient`)

### How Proton does it (read from Proton 11.0-2e and its Wine)

1. `lsteamclient.dll` (Windows side) and `lsteamclient.so` (Linux side) carry every Steamworks
   interface from the game to the Linux client's `steamclient.so` (`~/.steam/sdkarm64` on ARM64).
2. Valve's Windows files from Steam's `legacycompat` folder (`steamclient.dll`, `steamclient64.dll`,
   `GameOverlayRenderer64.dll`, `Steam.dll`, `SteamService.exe` as `steam.exe`) are copied into
   `C:\Program Files (x86)\Steam`, and the registry says Steam runs there (`ActiveProcess`,
   `SteamClientDll`).
3. A game's `steam_api.dll` loads that `steamclient.dll`. Proton's **Wine** maps Valve's real file
   (some DRM checks it) and overwrites each of its exports with a jump into `lsteamclient`.
4. Every program starts through Proton's own `steam.exe`, which writes the per-game registry (app
   id, language, running state) through `lsteamclient` and then starts the game.

### What UTDeck has now

- `build-runtime.sh` part **steam**: `lsteamclient` and `steam.exe` from Proton `proton-11.0-2e`
  (sparse checkout: only `lsteamclient/` and `steam_helper/`), built as Proton builds them, with
  Wine's `makedep` inside Wine's build tree. Our Wine's `makedep` does not take modules from outside
  the tree (Proton's Wine has a change for that), so the part builds in a copy of the tree made of
  links (`build/runtime-obj/steam-src`, `steam-obj`) with both modules added, and only their five
  files are built: `lsteamclient.dll` as ARM64X (one file for x86-64 games through ARM64EC and for
  ARM64 programs) and i386, `lsteamclient.so` (aarch64, C++: linked with `-lstdc++`), `steam.exe`
  as ARM64X and i386. Problems on the way: `makedep` crashes without `include` among its folders,
  then wants every IDL dependency (fixed by keeping Wine's whole folder list and building only our
  targets); an empty `config.status` newer than `include/stamp-h` made `make` reconfigure (given
  the old time stamp).
- Wine patch **0009** (`tools/wine-build/patches/0009-ntdll-redirect-steamclient-to-lsteamclient.patch`):
  point 3 above, ported from Proton's Wine (`proton_11.0`): ntdll's loader recognises
  `steamclient(64)` and `gameoverlayrenderer(64)`, loads `lsteamclient` and asks the Unix side to
  write the jumps (x86 or x86-64 code, run by the emulator, which passes into lsteamclient's
  ARM64EC or i386 code). Two changes: a flag defined in `loader.c` instead of `winternl.h` (that
  header would rebuild all of Wine), and Proton's `WINESTEAMNOEXEC` mode left out (it needs a fault
  handler Proton has only on x86-64). Only ntdll is rebuilt.
- `utdeck-compat`: makes the prefix, copies Valve's files (point 2), imports
  [`steam-prefix.reg`](../../tools/ut-probe/steam-compat/steam-prefix.reg) (the keys from Proton's
  `wine.inf`, all pointing to `C:\Program Files (x86)\Steam`, plus Proton's ".NET and XNA installed"
  keys) and starts everything through `C:\windows\system32\steam.exe` (builtin). Per-game settings
  in `compatdata/<appid>/utdeck-env` (`NAME=value` lines), since Steam starts the tool with its own
  environment.
- The runtime on the phone was replaced; the previous one stays as
  `wine/runtime-11.16-20261119-before-steam`.

### Manual run M13: Ys: Memories of Celceta from Steam (2026-11-19)

Started with `steam -applaunch 587110` sent to the hidden Steam:

- Steam's install script ran again (exit 0) and the keys were imported; then
  `waitforexitandrun Ysc_dx11.exe`.
- `steam.exe` loaded `lsteamclient` and wrote Steam's registry; `Ysc_dx11.exe` loaded
  `steam_api.dll`, `lsteamclient.dll` and Valve's `steamclient.dll` (redirected), **passed its Steam
  check** (before: exit 53) and started the game's own launcher, `YscLauncher.exe` (which also
  loaded the bridge).
- The maintainer tapped "Launch" on the phone: **the game started.** Measured over the first
  minute: **32 fps**, frame time 31 ms (max 34-37 ms), drawn at 1602x902, GPU 95 % busy at 700 MHz
  (GPU-bound), CPU 55 °C, GPU 55 °C, 3.8 GB memory free, the game 785 MB.
- Found at the same time: Steam's web helper used **200-280 % CPU** (two `steamwebhelper`
  processes, 98 minutes of CPU time in 38 minutes since boot), against 13 % at idle measured before
  the phone was restarted. Hidden Steam must not cost that while a game runs; next to look at.

### M13, second start (controller connected first) and what the logs show (2026-11-19)

Reported by the maintainer: audio works; touch works; **the controller works** when it is connected
before the game starts (connected while the game ran, it was not seen); the picture stays a window
in the top left corner, not scaled, with Lomiri's top bar; at 800x600 with some settings lowered
the game runs smoothly. Measured: exactly 30.0 fps (frame time 33.4 ms), GPU 57-60 %: the game's
own 30 fps limit, not the GPU (at 1600x900 it was 32 fps with the GPU at 95 %).

From the logs:

- **Why the picture is not scaled:** the game runs **windowed**. Its swapchains are 2 pixels larger
  than the mode (802x602, 1602x722): a window with a thin border, which Lomiri shows as an ordinary
  window. Stage 2.4's scaler (patch 0006) works for windows that cover their monitor (fullscreen
  games, as in 3.2), so it does not apply. In fullscreen (the launcher's "Fullscreen" option) with
  one of patch 0008's screen-shaped modes (1200x540, 1600x720) the picture fills the panel. Games
  that only run windowed need a "fill the screen with the game's window" mode (what gamescope does
  on the Steam Deck): added to the plan, not done. The game keeps its settings in
  `Saved Games/FALCOM/Ys Memories of Celceta/systemdata.dat` (binary) in the prefix.
- **The game's movies do not play** (`movie/op.wmv`, `logo.wmv`, `ins01.wmv`): "GStreamer doesn't
  support WMV decoding". Ubuntu Touch's GStreamer has no WMV decoder (`gstreamer1.0-libav`) and no
  ASF reader (`plugins-ugly`); Proton ships its own GStreamer and FFmpeg for this. To do: a small
  GStreamer FFmpeg plugin with only what games' videos need, in the runtime.
- **Steam's web helper at 200-280 % CPU: found and fixed.** A small tool,
  [`tools/ut-probe/xwin.py`](../../tools/ut-probe/xwin.py) (window list and screenshot through the
  phone's `libX11`, since there is no `xwininfo` or `xwd`), showed one window mapped on the hidden
  screen: **Steam's main window, on the Store page**, whose featured carousel plays trailers. Every
  video frame went through the web helper's `Media` thread and was composited in software (five
  Mesa `llvmpipe` threads in the GPU process). Sending `-applaunch` to the running client brings its
  main window up, as starting Steam a second time does on a desktop. Closing the window as its close
  button would (`WM_DELETE_WINDOW`, `xwin.py --close`) hid it: **both web helper processes went to
  0 %**, the phone's CPU load from 66 % to 47 % with the game running, same 30 fps.
  `steam-service.sh` got `launch APPID` (`-silent -applaunch`, then closes any window that shows up)
  and `hide`. Whether `-silent` alone keeps the window away is checked at the next start.
- Steam runs the install script again at every start (about 3 s): all its steps are skipped (their
  keys are in the prefix). Open, minor.

## Games in a smaller window: fill mode (Wine patch 0010)

The case (named by the maintainer): a game with no render-scale option has to be played at a lower
window resolution (the GPU cannot draw 2400x1080), and should still fill the screen. Ys: Memories
of Celceta is one: its "Fullscreen" is an exclusive mode at the panel's resolution only, its
"Borderless" and "Windowed" are windows of the chosen size, which Lomiri showed in the top left
corner with its top bar.

### What was tried first, and why it was dropped

1. **The prefix's desktop at 1200x540** (`tools/ut-probe/win/setmode.c`, a small Windows tool that
   sets and stores a display mode; Wine's explorer applies the stored mode at each start). The
   game's windows then were the desktop's size and Wine scaled them (patch 0006), but the game's
   launcher, taller than 540 lines, had its buttons below the screen. Kept as a tool and as
   `UTDECK_RESOLUTION` in `utdeck-compat` (whole prefix), for games without a launcher.
2. **A display mode for one program** (a user32 patch with `UTDECK_RESOLUTION_FOR`, matching
   `Ysc_dx11.exe nlc`): it switched the mode, but the game's "Fullscreen" is an exclusive mode that
   sets 2400x1080 again, and the maintainer asked for a general solution rather than per-game
   matching. Removed.

### Fill mode

[`0010-win32u-winewayland-fill-the-screen-with-a-smaller-game-window.patch`](../../tools/wine-build/patches/0010-win32u-winewayland-fill-the-screen-with-a-smaller-game-window.patch),
on by default for games started through `game-run.sh` (`UTDECK_FILL=1`; `UTDECK_FILL=0` in a
game's `utdeck-env` turns it off). What gamescope does on the Steam Deck, at Wine's level:

- **Which window:** a top-level window smaller than its monitor that creates a swapchain (the
  game; launchers and dialogs draw without one and are left alone). The game keeps seeing its own
  window size; nothing about it changes from its side.
- **win32u:** patch 0006's scaler gets a host swapchain of the screen's size and fits the game's
  picture into it (`UTDECK_SCALE`: fit by default, stretch, center); the picture's client surface is
  placed at the screen's origin; three window properties carry the picture's rect, the screen's
  size, and (from the driver) the size the compositor shows the window at.
- **winewayland:** the window is made fullscreen (Lomiri hides its top bar); its own drawing is
  replaced by one black buffer of the screen's size, so that touches anywhere reach it (without
  `wp_viewporter` a Wayland buffer cannot be scaled, and input outside a surface's buffer is not
  delivered); pointer and touch positions are mapped from the picture's rect back to the window.
- **Orientation:** Lomiri gives a fullscreen window 1080x2400 when the phone is upright and
  2400x1080 when it is turned. The window is never resized by that (patch 0003's "follow the
  rotation" gave the game a 1080x2400 window: portrait picture, fill switched off, top bar back,
  first try); instead the driver records the size, win32u returns `VK_SUBOPTIMAL_KHR` once, DXVK
  recreates its swapchain (still 1200x540) and the picture is fitted into the new size: the whole
  screen when turned, a 1080x486 band when upright.

### Manual runs M14 and M15 (2026-11-19): Ys, borderless 1200x540

| Run | Started | Result |
| --- | --- | --- |
| M14 | phone upright, then turned sideways and back twice | **Works**: right shape, top bar hidden; at each turn Lomiri sent the new size (`2400x1080` / `1080x2400`), the swapchain was recreated, the picture filled the screen sideways and was a 1080x486 band upright; the game's swapchain stayed 1200x540 throughout. Touch right (upright) |
| M15 | phone **sideways** | **Wrong in landscape**: Lomiri's first fullscreen configure was **1080x2400 although the phone was sideways**, and after that Lomiri sent **no configure at all**, through two turns of the phone; the picture stayed a portrait band, shown in the left 45 % of the landscape screen and past its lower edge |

So fill follows every size Lomiri sends; the open problem is Lomiri's orientation for this window.
Most likely cause, to check: a game started by hidden Steam is not a Lomiri app (it comes from a
background service, with no desktop file and no app id Lomiri knows), so Lomiri has no orientation
policy for it; in stage 2.3 (M4) a game started from a desktop file declaring landscape came up
landscape and stayed so. Options: give the game's window an app id that matches a desktop file
with landscape orientation, or start Steam games through a Lomiri app (UTDeck's library, stage 4).

## Account and privacy

The maintainer signed in on the phone (M11); Steam keeps its login token there. Steam's logs on the
phone are read with account-related lines filtered out. Copied off the phone so far: Valve's own
SDL3 (to compare it with ours) and `linuxarm64/steamclient.so` (to read how compatibility tools are
chosen); both are Valve's program files, with no account data, kept in `build/` (not in git).

## Events

- 2026-11-19: 3.S started. The ARM64 client (steamdeck_stable 1792618285) and Steam Runtime 3c
  installed and updated; GTK 2 linked from the runtime; SDL3 with Wayland built at Valve's commit;
  Mesa patches 0003 (zink by default without DRM) and 0004 (kopper kept) made; the web helper now
  runs but its window does not appear yet; the client restarts it every 10 s. X11 and Wayland
  windows from adb shown by Lomiri (screenshots); several composer and GPU settings ruled out.
  Then the cause found with a disassembler and SDL's source: SDL forces Wayland on Ubuntu Touch;
  unsetting `XDG_SESSION_DESKTOP` fixes it, the login window is created, Valve's own SDL3 suffices.
  M11 written (the maintainer signs in).
- 2026-11-19, later: M11 by the maintainer: logged in; the window fills the screen; 2-5 fps; the
  on-screen keyboard does nothing; touch on the right half lands on the middle line. Causes found:
  the X11 screen is portrait (1080x2400) under a landscape window, X11 programs get no input method,
  the interface draws in software at 220 % CPU. Plan for step 2b written.
- 2026-11-19, later: the maintainer chose hidden Steam (plan-change-steam-early.md). Steam as a user
  service on an Xvfb of our own, `-silent`; restarts after its own update (exit 42); signed in by
  itself; 1.33 GB and 13 % of one core at idle.
- 2026-11-19, later: M12: Ys: Memories of Celceta downloaded by remote install from the maintainer's
  Steam phone app, with no Steam window; Steam also fetched Proton Experimental, SLR 4 and FEX.
- 2026-11-19, later: Ys fully installed; its folder checked: only Valve's `steam_api.dll`, no
  bypass. Found that Steam's own path is x86-64 Proton under FEX in SLR 4. UTDeck's compatibility
  tool written (`steam-compat/`), with `set-default.py`. While installing it, right after Steam was
  stopped, the phone disappeared from USB (no adb, not listed by `lsusb`).
- 2026-11-19, later: the link was dropped from the PC's side (adbd: `FUNCTIONFS_SUSPEND`; the PC:
  `error -71`, then another drop); the phone had not rebooted. The maintainer restarted it. Tool
  installed, set as default, registered by Steam. Ys started from Steam through it: install script
  fixed (working folder, Box64 hang, run under FEX) and done; the game stops at its Steam check
  (exit 53). Step 3 done; step 4 joins step 5.
- 2026-11-19, later: step 5: Proton 11.0-2e's `lsteamclient` and `steam.exe` built in our Wine tree
  (new `steam` part), Wine patch 0009 ported from Proton's Wine, runtime rebuilt and installed (old
  one kept), prefix prepared as Proton does. M13: Ys passes its Steam check, its launcher appears,
  the maintainer starts the game: 32 fps, GPU-bound. Steam's web helper found at 200-280 % CPU.
- 2026-11-19, later: second start with the controller connected: controller, touch and audio work;
  the game runs windowed (not scaled; fullscreen and screen-shaped modes suggested), its WMV movies
  do not play (no decoder in the phone's GStreamer). Web helper cause found (Steam's main window
  open on the hidden screen, Store trailers drawn in software) and fixed by closing it: 0 % CPU.
- 2026-11-19, later: `-silent -applaunch` keeps Steam's window away (`steam-service.sh launch`).
  Ys's window at a smaller size: a prefix-wide desktop mode (launcher too tall), then a per-program
  mode (the game's exclusive fullscreen resets it; dropped), then fill mode (patch 0010). Fill:
  first try followed Lomiri's portrait size into the game's window (portrait picture, top bar);
  fixed by keeping the window's size and fitting into the configured size. M14 (started upright):
  works in both orientations. M15 (started sideways): Lomiri configures portrait and stops
  rotating the window; open.
