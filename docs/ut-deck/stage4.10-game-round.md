# Stage 4.10: the game round, the plan

Parked 2026-11-24 until after stage 5.3 (the maintainer: Ys IX had run into memory trouble), taken
up again 2026-11-25. Goal (from [stage4-roadmap.md](stage4-roadmap.md)): three or more new games
through the app, bringing the game table to ten; stage 3's leftovers for a game round; a keyboard and
a mouse tried in each game. Exit: the table at ten games, and stage 4's exit run by the maintainer
(from a fresh install: add a game, start it, play with a gamepad, quit back to the library). New since
it was parked: the performance overlay (5.1) writes each game's numbers down as it is tried.

## The games

The table had seven, all measured on stone in stage 3 ([stage3-roadmap.md](stage3-roadmap.md)): Ys:
Memories of Celceta, Ys IX, ATRI, Plants vs. Zombies: Replanted, Stray, Goat Simulator, Aperture Desk
Job. New, on the Nothing Phone (1), each folder checked for DRM bypasses first (`gamecheck.py`, stage 3's
rule):

| Game | What it is | DRM-bypass check | First try (the maintainer, 2026-11-24/25) |
| --- | --- | --- | --- |
| Stardew Valley | MonoGame (OpenGL), .NET, 64-bit | (Steam's own copy; tried in 5.1/5.2) | plays (5.1: about 51-55 fps, through zink) |
| Celeste | .NET Framework, 32-bit (`Celeste.exe` imports only `mscoree.dll`), FMOD | none; `steam_api.dll` signed by Valve | **freezes a few seconds into the game on our Wine base; stable 60 fps on Proton 11's** |
| Need for Speed Most Wanted (2012) | `NFS13.exe`, 32-bit; EA's `Core/ActivationUI.exe` (Qt 4, WebKit) and the EA app's installer (`EAappInstaller.exe`, 263 MB) | none; the overlay's `steam_api.dll` signed by Valve | **does not start**: the EA app and its sign-in stand before the game, and could not be seen or used. Later the same day: runs on both bases, 14-18 fps, CPU-bound (one thread); on ours with patches 0022-0024 and FEX for its 32-bit code |

That makes ten. On the way: Steam installed Proton Experimental, its FEX and Steam Linux Runtime 4.0
by itself (about 2.2 GB, unused by UTDeck): a leftover for 5.4.

## What each needs first

- **Celeste**: why it freezes on our base and not on Proton's. Both bases use the same runtime
  otherwise, so the difference is in Wine (Hangover's 11.16 against Proton's 11.0, with Valve's game
  fixes) or in .NET's path (Wine Mono, its version, the 32-bit translator): the two runs' logs side by
  side, then the frozen process (its threads, what it waits on).
- **Need for Speed Most Wanted**: the EA app is a launcher of its own (EA's successor to Origin), a
  Chromium-based program with a sign-in to an EA account; Steam's install script runs its installer,
  and the game will not start without it running and signed in. Known as one of the hardest
  launchers under Wine (Proton carries fixes for it). First: what was installed and what runs (the EA
  app's processes, their windows, their logs), on our base and on Proton's. **Only the maintainer signs
  in; no account data goes into any log or note** (the same rules as Steam's).

## Celeste on our base (2026-11-25)

Its runs on the Nothing Phone (1): on our base the frame log ends after 1 and 4 seconds (60 fps until
then; the maintainer ended both, `exit 143`); on Proton's, 291 seconds, `exit 0`. Our base's log ends in
a stream of `err:virtual:allocate_virtual_memory out of memory for allocation` and Wine Mono's
`Assertion at .../wine-mono-11.3.0/mono/mono/utils/mono-mmap-windows.c:116, condition 'aligned' not met`:
**Celeste ran out of address space**, not of RAM. A 32-bit Windows program gets 2 GB of address space
unless its executable is flagged "large address aware" (4 GB); `Celeste.exe` is not. Proton's Wine
counts every 32-bit program as large address aware unless `WINE_LARGE_ADDRESS_AWARE=0`
(`need_override_large_address_aware`, on by default, in its `dlls/ntdll/unix/virtual.c`; Proton turns
it off only for a few games); Hangover's follows the program's flag (`user_space_wow_limit = limit_2g`).

The fix: Wine patch **0021** (`ntdll: 32-bit programs large address aware by default, as on Proton`):
the same switch with the same default in our Wine, keeping upstream's reserved area at the top of the
4 GB range. A game's settings can turn it off (`WINE_LARGE_ADDRESS_AWARE=0`).

Tested on the Nothing Phone (1) with only `ntdll.so` swapped into the runtime (the one file 0021
changes; the old one kept as `ntdll.so.before-0021`):
- **0021, Box64** (our default for 32-bit code): the address-space errors are gone (0 against 281), but
  Celeste still stopped after 2 seconds, now with Mono's thread-state check failing: `Cannot
  transition thread 00000158 from RUNNING with DONE_BLOCKING`. (Right before it, every run, on both
  bases, logs `steamclient:callback_len_utow Unix len 12 not found for callback 1112`: our Steam bridge
  cannot convert one of Steam's messages; harmless, Proton's base shows it too.)
- **The difference between the bases** was also the 32-bit translator: Hangover's Wine uses Box64 by
  default (`wowbox64.dll`, `dlls/wow64/syscall.c`), Proton's Wine on ARM64 uses FEX
  (`libwow64fex.dll`). Both bases bring the same Wine Mono (11.3.0).
- **0021 and FEX** (Celeste's page: "32-bit code" FEX, base Default): it plays, 95 seconds at 58.6 fps
  on average, no errors.
- **FEX without 0021** (the maintainer's earlier try): it stopped too, later, on the title screen. So
  Celeste needs both: the address space (0021) and FEX (Box64 trips Mono's thread-state check).

Open: whether FEX should be the default for some or all 32-bit games. It is stage 3.4's leftover K9
(Box64 or FEX for 32-bit games), now with a game that only runs on FEX; Box64 was chosen for speed on
32-bit games that ran on both (stage 3.4).

**The maintainer's choice (2026-11-25): FEX automatically for 32-bit .NET games** (option b; FEX for
all 32-bit games, as on Proton, waits for stage 3.4's K9 comparison on the 32-bit games, none of them on
the Nothing Phone (1) now). `game-run.sh` asks `dotnet32.py` (new: a cut-down copy of `gamecheck.py`'s PE
reader) whether the game's program, the first `.exe` of its command (for a Steam game, the path Steam
passes after `steam.exe`), is 32-bit x86 and imports `mscoree.dll`; if so, and no `HODLL` is set, it
uses FEX (`HODLL=libwow64fex.dll`) and logs "32-bit .NET program (...): FEX for its 32-bit code". Checked
on the installed games: Celeste yes; Stardew Valley (64-bit .NET), Need for Speed, Ys IX, PvZ no. The
game's page's "32-bit code" now has three choices: Automatic (no line), Box64 (`HODLL=wowbox64.dll`,
which overrides the automatic rule) and FEX. App 0.3.56. Patch 0021 goes into a runtime release later,
bundled with what Need for Speed needs (the maintainer).

Checked with 0.3.56 (the maintainer's OK) on the Nothing Phone (1), 0021's `ntdll.so` still swapped in
by hand: Celeste's "32-bit code" back to Automatic (its settings name no translator); the log "32-bit
.NET program (Celeste.exe): FEX for its 32-bit code"; it played (50 s, 56.9 fps on average, `exit 0`).

## Need for Speed Most Wanted (2026-11-25)

**On our base** (00:40): Steam starts the game through `steam2ea://launchgame/1262560`, which hands it
to the EA app (installed by Steam's install script in `C:\Program Files\Electronic Arts\EA Desktop`).
Its window stayed blank; Wine logged `D3D11: Failed to write shared resource info for a texture` and
`err:vulkan:vkGetSwapchainImagesKHR Exception 0xc0000005 in Unix call`: a crash inside Wine's Vulkan
path while the EA app's Chromium draws through Direct3D 11 (DXVK). Not looked into yet; our Wine
patches 0005 and 0006 touch that path. (Wine's own logs only; the EA app's files are not read.)

**On Proton's base** (the maintainer, 01:37): the EA app and its sign-in worked, the game started:
- At 2400x1080 the window was black with the game's sound playing; the picture only changed after a
  trip to Lomiri's app switcher. At 1280x720 it showed normally, without graphical errors.
- Windowed: Proton's patch set has no 0010/0011 (fill), as Ys IX on Proton's base.
- About 18 fps at 1280x720 and its lowest settings, held by one CPU thread: the fastest core (`c7`)
  96-98 % at its 2515 MHz, the GPU about 60 % at 450 MHz. `NFS13.exe` is 32-bit, and Proton's Wine
  runs 32-bit code with FEX by default.

**(D) Box64 against FEX** (the maintainer, 01:52, Proton's base, "32-bit code" Box64, the same spot):
14-18 fps with the overlay on, against about 18 on FEX; the fastest core again 96-97 % at 2515 MHz, the
GPU 66-74 %. And the same frame rate at 800x600, 1280x720 and 2000x900: the game is held entirely by one
CPU thread, its x86 code translated; the GPU does not count. Neither translator helps as it is.
FEX without its x86 memory-order emulation (`FEX_TSOENABLED=0`, stage 3.4's leftover 11; it can break a
game whose threads rely on that order): 14-18 fps again. The cost is not TSO; the game's 32-bit code is
more than one of this phone's cores can run translated at 30 fps. **For the table: runs on Proton's
base, 14-18 fps, CPU-bound (one thread)**; the TSO line taken out again (no gain, and a risk).

**What it needs, one at a time**: (D) Box64 against FEX for its 32-bit code (stage 3.4 chose Box64 for
speed); (C) the black picture at 2400x1080 on Proton's base; (B) fill on Proton's base (0010 and 0011
brought over); (E) the EA app on our base; and for every game (A) the scaling choice in the app: patch
0006 already has `UTDECK_SCALE` = `fit` (the default), `stretch`, `center` (1:1) and `off`, but only a
test script ever set it (the maintainer's question, 2026-11-25).

## (A) The scaling choice in the app (2026-11-25)

Wine patch 0006 (our base) scales a game's picture that is smaller than the screen, by `UTDECK_SCALE`:
`fit` (also when unset: as large as possible, the shape kept), `stretch`, `center` (1:1); anything
else turns the scaler off. Proton's base has its own patch 0006 (Valve's fullscreen hack made to work on
Wayland) and no `UTDECK_SCALE`. Only a test script had ever set it. Now: "Picture scaling" on a game's
page (As in Settings, Fit, Stretch, Centered (1:1)) and in Settings for all games (`user.env`, written
only when not Fit); `game-run.sh` needs no change, as every `UTDECK_*` setting reaches Wine from a
game's settings or `user.env`. App 0.3.57.

Checked with 0.3.57 (the maintainer's OK) on the Nothing Phone (1), Celeste windowed at 1280x720 in its
own video options, its page's "Picture scaling" set to each choice in turn with a restart between
(12:31-12:37): **Fit, Stretch and Centered (1:1) each showed as described** (the maintainer): Fit as
large as the screen allows with bars left and right, Stretch over the whole screen, Centered pixel for
pixel in the middle. Afterwards the game's page back on "As in Settings" (its `utdeck-env` without
`UTDECK_SCALE`) and Settings on Fit (`user.env` without the line). A game's own settings are not
written into its run log (only `user.env`'s are, "user settings:"); the choice is seen on the screen.

## (B) Fill on Proton's base (2026-11-25)

Proton's base had neither fill (our 0010) nor opaque pictures (0011): a game in a window smaller than
the screen showed as that window, in a corner (Need for Speed, Ys IX). 0010 could not be carried over
as it is: on our base it extends our own scaler (0006), and Proton's base scales with Valve's
fullscreen hack instead (Valve's way of showing a game's smaller display mode over the whole screen:
the game draws into images of its own size, Wine scales each frame into the real window). Brought
over by hand, on top of Proton's patches 0001-0020:

- **`patches-proton/0022`** (fill): a top-level window smaller than its monitor, outside an emulated
  display mode, goes through Valve's scaling pass with a host swapchain of the screen's size (the
  real picture buffers the compositor shows); the picture is drawn into the rect `UTDECK_SCALE`
  chooses (fit, stretch, center), black around it. The Wayland half is 0010's: the window made
  fullscreen over a black buffer, the picture at the screen's origin (Wine 11.0's driver places it
  itself, 11.16 asks win32u), pointer and touch mapped back through the picture's rect, the window
  never resized by the compositor. Games that set a display mode keep Valve's own path.
- **`patches-proton/0023`** (opaque): the picture and the fill background marked opaque; the bars
  are opaque black already (Valve's pass samples outside the picture as opaque black).
- Numbered after 0020, the last patch they are written on (first saved as 0010/0011, which the build
  applied before 0012-0020 and failed: [lessons-learned.md](../lessons-learned.md)).

Built (`PARTS=wine WINE_FLAVOR=proton`, no compiler warnings in the changed files) and tested with
only `win32u.so` and `winewayland.so` swapped into the phone's `runtime-proton11.0-20261124` (the old
ones kept as `*.before-0022`; the maintainer's OK). **Need for Speed** (1280x720, Fit, 12:59): fills
the screen with black bars left and right, the picture shows from the start (the maintainer); no new
errors in Wine's log.

Then (the maintainer): **touch did nothing in the game** (likely the game: its PC menus are made for
a keyboard and a gamepad; the mapping is to be checked with a game played by mouse), and **at any
resolution not of the panel's 20:9 shape the picture sat on the left, the wallpaper on its right,
and Stretch did nothing**. Those modes are emulated: the game sets a display mode in fullscreen,
Proton only pretends to change it, and Valve's hack, not 0022, scales the picture, keeping the mode's
shape, into a host image of the fitted area only (1920x1080 on 2400x1080); Lomiri, unable to scale
a buffer, shows it at that size from the left edge, and `UTDECK_SCALE` is not asked. Our own 20:9
modes (0008) match the panel exactly, which is why it had never shown (Ys IX at 1600x720 in 3.3).
Not a corner case: most games offer only 16:9 and 4:3 modes.

- **`patches-proton/0024`** (`win32u: fill the screen under an emulated display mode too`): 0022's
  fill also takes a window under an emulated mode, fullscreen or smaller: a host swapchain of the
  whole panel (the real panel's rect, `get_monitor_physical_rect` as in our base's 0006; Proton's
  own mapping gives the fitted area), the picture in `UTDECK_SCALE`'s rect, black around it, the
  Wayland driver's fill as before.

Only `win32u.so` changed (swapped in, the maintainer's OK). **Need for Speed at 1280x720** in
fullscreen: black bars at the sides, no wallpaper; **Stretch** covers the whole screen (the
maintainer, 13:15). **Centered (1:1)**: the picture pixel for pixel in the middle (the maintainer).
Pointer and touch through the fill's rect: **moved to stage 5** (the maintainer: multitouch and
swipes are not responsive in games yet; a test program is needed first), as substage 5.5 in
[stage5-roadmap.md](stage5-roadmap.md). **(B) done** otherwise: patches 0022-0024, to be released with
the next runtime (with 0021, as agreed).

## (C) The black picture at 2400x1080 on Proton's base (2026-11-25)

**Not seen again** (the maintainer, after 0022-0024): Need for Speed at 2400x1080 on Proton's base
shows its picture from the start. The cause is not known: at 2400x1080 the window covers the whole
screen, which none of 0022-0024 change (fill takes only a smaller window, or an emulated mode; the
panel's own mode is not emulated), and 0023's opaque marks are ignored by Lomiri. What did change
between the runs: the game's settings (the maintainer had moved it to 1280x720 and back), and
Lomiri's state (the first run was right after the EA app's sign-in). Left open, to be watched: if a
game's picture stays black until a trip to the app switcher again, its log and the window's state are
taken right then.

## (E) Need for Speed on our base: the blank window (2026-11-25)

**Not the EA app.** The 00:40 run's log read again: the EA app did hand over to the game, and DXVK's
`Game: NFS13.exe` lines come just before the crash. The game's own Direct3D 11 swapchain (2400x1080,
5 images) failed: `err:vulkan:vkGetSwapchainImagesKHR Exception 0xc0000005 in Unix call`, so its
window stayed blank. Patch 0021 was not in yet (swapped in at 01:08), and 32-bit Direct3D games had
already worked on our base (Ys: Memories of Celceta, Goat Simulator).

**Where** (the maintainer's run at 16:21, our base, Box64, Fit, the overlay on, `WINEDEBUG_GAME` with
`trace+seh` for one run): this time c0000005 in `vkCreateSwapchainKHR`, and `handle_syscall_fault`
gave the fault address equal to the program counter: Wine called a function where no code was mapped.

**Why**: patch 0006 replaces `vkGetSwapchainImagesKHR` in each device's table and kept the host's
function from the first device in a global. The Vulkan loader unloads the driver (turnip) and its
layers (MangoHud) when a program destroys its last instance, and loads them again, at other
addresses, for the next one; neither is built to stay loaded. The game (and the EA app's overlay
helper `IGOProxy32.exe`) creates and destroys several Direct3D devices, so the kept pointer led
nowhere: in the scaler's call when the swapchain was scaled (`vkCreateSwapchainKHR`), in the plain
pass-through otherwise (`vkGetSwapchainImagesKHR`). Proton's base has no such hook, and ran the game.

**The fix**: patch 0022, the host function looked up from the device each time (only used around a
swapchain's creation). Runtime 11.16-20261125.2 (0001-0022) built; its `win32u.so` swapped into the
phone's runtime by hand (the maintainer's OK, the old one kept as `win32u.so.before-0022`, next to
0021's `ntdll.so`), to be checked with the game.

**Checked** (the maintainer, 16:28, the same settings): the game's picture shows on our base, no
Vulkan error; quit right after.

**The run that does not end**: at every start the EA app runs Microsoft's Visual C++ 2012 installer
(`vc_redist-11.0.61030.x86.exe /install /quiet`, a 32-bit installer with its elevated child
`vcredist_x86.exe`). On our base it never finishes, and while it runs the prefix's Wine stays up:
after the game is quit, only the wallpaper and the top bar are seen, with two UTDeck entries in
Lomiri's app switcher (seen at 16:21 and 16:33; the run ended from outside both times). One of its
threads uses a whole core in its own code, with no system calls (a busy wait for something that never
comes); the others sleep. It runs on Box64 (`wowbox64.dll`). In Proton's prefix (FEX) the same
runtime is installed (`Wow6432Node\Microsoft\VisualStudio\11.0\VC\Runtimes\x86`, `Installed`=1);
in our base's prefix it was never registered, so the EA app tries again at each start.

**With FEX for 32-bit code** (the maintainer, 16:37, only that changed): the game ran, the picture
showed, and the run was stuck again after quitting: the installer was still on Box64
(`wowbox64.dll` in its memory map), although every process's Linux environment had
`HODLL=libwow64fex.dll`. Wine reads `HODLL` from the Windows environment, and `services.exe` starts
each service with a fresh one made from the registry (`CreateEnvironmentBlock`), copying only
`WINEBOOTSTRAPMODE`. The EA Background Service is a service, so it and the installers it starts
always get Box64, whatever the game's setting. Stage 3.S had already found that Box64 spins in
Microsoft's "burn" installers and FEX runs them (Steam's install scripts run under FEX since then).
Fixed with patch 0023 (services keep `HODLL` and `HODLL64`), runtime 11.16-20261125.3 (0001-0023).
**Checked** (the maintainer, 16:49, FEX, `services.exe` swapped in by hand, the old one kept as
`services.exe.before-0023`): the installer finished, Visual C++ 2012 and 2013 x86 are now registered in
our base's prefix, and the run ended by itself after the game was quit (`exit 0`).

**Fit broken in the same run**: the wallpaper left and right of the picture and a stripe of it on top.
The game set 1280x720 (an emulated mode); at 16:49:55 Lomiri put another window in front for a
moment, DXVK left fullscreen (`Restoring display mode`, `Nested LeaveFullscreenMode`) and set the mode
again, and the game's swapchain was recreated at 1280x700: the game had come back as a window with a
title bar, 1280x720 outside, 1280x700 inside. Patches 0006 and 0010 compared the whole window with the
monitor: equal, so a fullscreen game (no fill, no black buffer), and the picture sat under a title bar
Wayland does not draw. On Proton's base the mode was set once and the window stayed fullscreen.
Fixed with patch 0024 (both checks use the client area), runtime 11.16-20261125.4 (0001-0024).

**The next start hung in "Preparing Wine for this game"** (16:56, `win32u.so` with 0022 and 0024
swapped in by hand): with its installers now running on FEX, the EA app had put Visual C++ 2015-2022
14.34 (its own copy) into the prefix at 16:49, so `utdeck-compat` found the Visual C++ files mixed and
ran Steam's 2022 installer. That one removes an older bundle first; the removal's own log ended
"Apply complete, result: 0x0" and "Exit code: 0x0" at 16:56:35, but its process never ended: one
thread left, waiting on a futex (a lock) forever, at 0 % CPU, under FEX. Ended by hand at 17:06 (the
maintainer's OK); Steam's installer then finished (`x86 /install: exit 0`, "Microsoft's complete set")
and the game started. Open: why a finished 32-bit process can hang at its exit under FEX; if it comes
again, the process's state is taken before it is ended.

**Checked** (the maintainer, 17:06 and 17:11, `win32u.so` with 0022 and 0024): 1280x720 with Fit, black
bars, no wallpaper, `exit 0`; a trip to the app switcher kept the bars. Neither run dropped out of
fullscreen (no 1280x700 picture in the logs; the second changed to 1440x900 in between), so 0024's
own case is reasoned from the 16:49 log, not yet seen working: watched (a picture a title bar's height
short of the mode, in a game's frames file, is that case).

**(E) closed** (the maintainer, 2026-11-25). Need for Speed keeps FEX for its 32-bit code (its
`utdeck-env`, `HODLL=libwow64fex.dll`; (D) measured the same speed on both). Left open:
- other EA games on our base: the EA app's Visual C++ installers spin under Box64 (Automatic) on a
  prefix's first start; FEX automatically for games started through the EA app, or Microsoft's
  installers always on FEX, is the maintainer's choice, not made yet;
- the finished installer that hung at its exit under FEX (above);
- patches 0022-0024 (`win32u.so`, `services.exe`) and 0021 (`ntdll.so`) are swapped into the phone's
  runtime by hand (the originals kept as `*.before-00NN`), until the next runtime release.

## Then

- Each new game with the overlay (Full) for its numbers, a keyboard and a mouse tried in each.
- Stage 3's leftovers for a game round (its list in [stage3-roadmap.md](stage3-roadmap.md)).
- Stage 4's exit run by the maintainer.

## Status

- 2026-11-25: planned (this file); the two new games' folders checked (no DRM bypasses).
- 2026-11-25: Celeste plays (patch 0021 by hand, FEX automatically for 32-bit .NET games, app
  0.3.56); Need for Speed runs on Proton's base at 14-18 fps, CPU-bound; (A) the scaling choice in
  the app (0.3.57), checked with Celeste. Next: (B) and (C), fill and the black picture on Proton's
  base; then (E).
- 2026-11-25: (B) fill on Proton's base (patches-proton 0022-0024: smaller windows and emulated
  display modes, Fit/Stretch/Centered), checked with Need for Speed; touch through it moved to stage
  5.5. (C) not seen again. Next: (E), the EA app on our base.
- 2026-11-25: (E) Need for Speed on our base: not the EA app but three Wine problems, fixed with
  patches 0022 (a driver function kept after the driver was unloaded: the blank window), 0023 (services
  lost the game's x86 translator: the EA app's installers spun under Box64) and 0024 (a window with a
  title bar the size of the screen taken for fullscreen); 0022 and 0023 checked, 0024's own case
  watched. (E) closed. Next: the rest of the round (below, "Then").
