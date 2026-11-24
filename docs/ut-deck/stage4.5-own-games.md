# Stage 4.5: your own games, the plan

Written 2026-11-22, when 4.4 closed; started the same day, done 2026-11-23 (see "Done" at the end).

Goal ([stage4-roadmap.md](stage4-roadmap.md)): games without Steam. Added from the phone, each with
its own Wine prefix, started through `game-run.sh` as Steam's are, listed in the library next to
Steam's; edited and removed from the app. Steam is optional (4.2), so this is also what a phone
without Steam plays.

Exit: two games added and played from the app with Steam off or not set up; one removed.

## Terms used here

- *prefix* = a game's own Windows folder tree (its `C:` drive and registry), made by Wine; a game's
  saved games are often inside it.
- *MTP* = how a PC sees the phone's folders over USB (Documents, Downloads, ...).
- *installer* = a game's Windows setup program (a GOG offline installer, `setup_<game>.exe`), as
  opposed to a game that only needs its folder copied.

## What exists

- **Starting a game**: `game-run.sh LABEL GAME_DIR EXE [ARGS]` with `WINEPREFIX`: it makes the prefix
  if needed (with DXVK and VKD3D-Proton, `prefix-dlls.sh`), imports `controllers.reg`, sets the
  controller layouts (`padlayout.py`), writes the game file the app follows (focus, running game,
  errors), logs and measurements. Steam's games reach it through `utdeck-compat`; stages 2 and 3
  started 3DMark 11 and others with it from desktop files.
- **The library** (`Library`): Steam's installed games from `steamapps/appmanifest_*.acf`, covers
  from Steam's cache; `play()` starts `steam-service.sh play APPID`. Only numeric Steam ids.
- **The running game, its errors and its end** (4.3): from the game file and the game's log, by
  label (`steam-<appid>`).
- **`gamecheck.py`** (stage 3.2b): before a game's first start, lists its executables (machine,
  bitness, graphics API) and finds DRM-bypass markers (Steam emulators, crack groups). We do not run a
  game with one in our own tests; the app does not check (see "The app does not check for DRM
  bypasses" below).
- **`~/Documents/UTDeck/Games`** exists on stone (stage 3.2): the folder a PC copies games into over
  MTP.

## How it would work

- **Adding a game**, from the library ("Add a game"), in two ways:
  1. **A game folder** copied over MTP into `Documents/UTDeck/Games/<game>`: the app lists those
     folders, the user picks one, then the `.exe` to start, chosen from a list the app sorts (the
     largest executables of the right kind first; uninstallers, crash reporters and redistributables
     last).
  2. **An installer** (in `Downloads` or `Documents`): the app makes the game's prefix and runs the
     installer in it, as a game (its window over the app, touch as the mouse); when it ends, the app
     lists the executables it installed under the prefix's `C:` and the user picks the game's.
- **Before saving**: `gamecheck.py` on the game's files. A DRM-bypass marker stops the adding, with
  the marker named. The machine and graphics API go into the game's details. (Changed 2026-11-23:
  the app lists the programs only, see below.)
- **Name and cover**: the name from the folder (editable); the cover optional, an image from the
  game's folder or `Pictures`, otherwise a plain card with the title.
- **Where things live**: `~/utdeck-probe/wine/own/<id>/` per game: `game.conf` (name, folder, exe,
  cover; plain `NAME=value` lines a shell script can read) and the prefix `pfx/`. The game's own files
  stay where the user put them.
- **Starting**: `own-run.sh <id>` reads `game.conf` and starts `game-run.sh own-<id> DIR EXE` with the
  game's prefix, under a systemd unit as Steam's games are. The starting page shows the steps without
  Steam's ("Preparing Wine, the first start takes about a minute", "Starting the game"), and the same
  error checks (4.3) by the label `own-<id>`. Per-game settings (launch options, the Wine base, boost)
  are 4.6; the file `own/<id>/utdeck-env` is where they will go, as `compatdata/<appid>/utdeck-env`
  for Steam's.
- **In the library**: one grid with Steam's games and the user's, sorted by name; the user's games
  shown even when Steam is off or not set up.
- **Editing and removing** (a long press on the card): edit the name, exe and cover. Remove: from the
  library, keeping the game's files; with a separate, explicit choice to also delete its prefix
  ("its Wine data, with any saved games inside"). The game's own folder in Documents is never deleted
  by the app (the user deletes it over MTP or with the file manager); an installed game lives inside
  its prefix, so deleting the prefix deletes it, which the dialog says.

## Steps

1. **The two test games**: the maintainer names them and how they come (an installer, a folder); one
   of each would test both ways in.
2. **Starting by hand**: `own-run.sh` and `game.conf`, the first game started from adb with Steam off:
   prefix made, DXVK in, the controller settings, the game file, the log.
3. **The library**: the user's games next to Steam's (a model with both kinds), `play()` for both, the
   starting page and error checks for `own-<id>`.
4. **Adding**: the folder way, then the installer way, with `gamecheck.py`, the exe list, name and
   cover.
5. **Editing and removing**, with the explicit prefix choice.
6. **The exit run** on the phone, Steam off: both games added and played from the app, one removed.

Test device: stone (the UI work stays there, 4.4); the Nothing Phone for the exit if the maintainer
wants it on a phone without Steam's history.

## Not in 4.5

- Per-game settings (4.6), other stores (later), a prefix backup.
- Games whose installer needs the internet or an account (a launcher such as EA's or Ubisoft's):
  those are stores, not "your own games".

## Decisions (the maintainer, 2026-11-22)

- **The two test games**, both the maintainer's and DRM-free, from `third_party/`:
  - **wiliwili** (`wiliwili-Windows-x86_64.zip`), a video streaming app with controller support: the
    folder way. One `wiliwili.exe`, x86-64, 55 MB; it draws with Direct3D 11 (DXVK, the path
    Steam's games use); `gamecheck.py`: no DRM-bypass markers. Its videos are decoded on the CPU
    (through FEX), a video decoding test too. It needs the internet. It is also the one removed.
  - **3DMark 11** (`3DMark_11_v103_installer.exe`): the installer way, as in stage 2; it needs
    Wine Mono (.NET). Run as the free Basic Edition: its key is not used and stays out of every
    log and note.
- Not taken: the Dolphin ARM64 AppImage. It is a Linux program, so it would test none of 4.5's
  path (no Wine, no prefix, no `gamecheck.py`), and AppImages need FUSE; Dolphin-UT already runs as
  a click. Native Linux apps in the library could be a later feature.
- **`Documents/UTDeck/Games`** for copied games: yes.
- **Removing** keeps the game's files; its prefix is deleted only on an explicit choice: yes.

## Step 2: starting by hand (2026-11-22)

- **`tools/ut-probe/own-run.sh ID [--install SETUP_EXE]`**: reads `wine/own/ID/game.conf` (as text,
  never run as a script), the game's `utdeck-env` (as `utdeck-compat` reads Steam's), picks the Wine
  base and its prefix (`pfx/` or `pfx-proton/`), on the first start makes the prefix with no window
  (DXVK and VKD3D-Proton in) and installs Wine Mono, then hands over to
  `game-run.sh own-ID DIR EXE`; what it did goes to `wine/own/ID/own-run.log`. `--install` runs an
  installer in the same prefix.
- **wiliwili on stone** (its folder pushed to `Documents/UTDeck/Games/wiliwili`, a `game.conf` by
  hand, started as the app will, `systemd-run --user --unit=utdeck-play-own-wiliwili`): prefix made
  in 70 s, Wine Mono in 32 s, `controllers.reg` imported, the game file written (`own-wiliwili`).
  The screen locked meanwhile and the pause helper froze the game, as it does a game; after unlocking,
  wiliwili 1.6.0 started: GLFW 3.4 with Direct3D 11, DXVK 2.7.1 created its device on Turnip, the
  window was made ("createWindow done", after loading its internal font). Then **no frame**: a white
  window ("unknown app" in Lomiri: UTDeck was not running, so the window had no app id), no
  swapchain in DXVK's log, the GPU idle, the main thread busy all the time, and the game's memory
  rising from 1 GB to 3.3 GB in two minutes (stone's free memory down to 1.5 GB): stopped.
  Wine reports 96 DPI; wiliwili never made its settings folder. Nothing about it found online.

### Why wiliwili drew nothing: x87 maths in software (2026-11-22)

- **Finding the process took three tries** (a lesson in [lessons-learned.md](../lessons-learned.md)):
  on this Wine the game's command line starts with the loader's path and only its process name is
  `wiliwili.exe`; two guards that looked for it by command line missed it, and the game grew to 5.7 GB
  (ended by the kernel's out-of-memory killer, `anon-rss:5698172kB`: ordinary memory, not the GPU's)
  and then 4.6 GB. The third version guards on the phone's free memory (below 1.5 GB: the prefix's
  Wine stopped) and finds the game by its process name, and held: stopped at 1129 MB.
- **What the main thread does**: 100 % of a core and memory up by about 21 MB/s, but never in a system
  call, so `stackscan.py` could not read it. Frozen for a moment with `SIGSTOP` (a stopped thread shows
  its stack pointer and pc in `/proc/.../syscall`), it was inside FEX's `libarm64ecfex.dll` every time.
  The offsets, looked up in FEX's unstripped build (`build/runtime-obj/fex-arm64ec-w64-mingw32/Bin/`,
  the shipped DLL is the same build stripped, same code offsets; `llvm-symbolizer`):
  `cephes_f128_sinl`, `cephes_f128_polevll`, `softfloat_subMagsF128`, `f128_div`,
  `softfloat_roundToI32`, called from `FEXCore::CPU::OpHandlers<413>` and `<416>`. That is FEX's
  emulation of **x87**, the old 80-bit maths unit of x86 CPUs, done in software at full precision:
  wiliwili (a MinGW build, whose `long double` maths uses x87) computes sines with it, each one very
  slow on the phone.
- **The fix**: FEX's `X87ReducedPrecision` (`FEX_X87REDUCEDPRECISION=1`, read by our FEX build from
  the environment): x87 done with ordinary 64-bit doubles. In wiliwili's `utdeck-env` only. With it:
  memory levels at about 520 MB, the main thread at 10 to 20 % of a core, the first frame within a
  second of the window, **52 to 78 fps** (portrait 1080x2400 first, then landscape 2400x1080), its
  video player loaded (`libmpv-2.dll`, "fast decode"). FEX's own description warns that reduced
  precision "may result in rendering bugs"; for most programs 64-bit precision is plenty.
- Open: whether to make it a default (many 32-bit games use x87 for all their floating point, and
  FEX's software x87 is slow for every one of them) or keep it per game; why memory grew while the
  maths was slow (something in wiliwili queued work while its first frame never came).

### wiliwili working (2026-11-22)

- The main page shows and **touch works** (a tap is a click). **Touch scrolling does not**: UTDeck's
  touch makes the first finger the mouse, so a drag is a drag with the left button held, and
  wiliwili's interface library (borealis) scrolls on Windows only with the mouse wheel (it reads
  touch drags only on its Switch and Android builds). A touch gesture as the wheel (for example a
  two-finger drag) belongs to stage 5's touch work.
- **The controller**: not seen in the first working run, which had no `padlayout` line at its start:
  the T6 was connected after the start (K3, 4.4's open point). Connected first, it works:
  `padlayout: "Xbox One S Controller" ...: SDL's layout fits`, and wiliwili's GLFW found
  "XInput Gamepad (GLFW)" (GLFW loads XInput itself, which is why `gamecheck.py` listed no XInput
  import). About 50 fps in landscape.
- **A program that restarts itself**: after its language was changed, wiliwili restarted (a new
  process). The diagnostic's guard took the gap for "the game is gone" and stopped Wine, ending the
  new process. The same holds for UTDeck: `game-run.sh` follows the first process, so after such a
  restart the app would take the game for ended while its new process runs on. To handle in step 3
  (follow the prefix's Windows programs rather than one process).

## Step 3: the user's games in the library (2026-11-22, UTDeck 0.3.19)

- **`Library`**: besides Steam's games, every `wine/own/<id>/game.conf` with an `EXE` (the id lower
  case letters, digits, `.`, `_`, `-`), as `own:<id>`, titled by `NAME`, with `COVER` if that file
  exists (else the plain card with the title). One grid sorted by name, also when Steam is off or not
  set up. `play("own:<id>")` starts `own-run.sh <id>` in its own unit (`utdeck-play-own-<id>`), as
  Steam's games are started; the running game's label `own-<id>` maps back to `own:<id>`, so the
  library hides while it plays and the 4.3 error checks read its log. `isOwn()`, and `isPrepared()`
  (its prefix exists).
- **The starting page** for the user's games: no Steam steps or checks; "Preparing Wine for this
  game..." with "about two minutes" on the first start, else "Starting the game..."; "The game did
  not start" after four minutes without the game (pointing at `own-run.log`).
- **The library page**: "Starting <name>..." (not "through Steam"); with no games at all, the text
  names both kinds.
- **A program that restarts itself**: `game-run.sh` now follows the game until no Windows program of
  its prefix runs (by each process's `WINEPREFIX`; Wine's background programs, Steam's launcher,
  `iscriptevaluator` and Unity's crash reporter do not count), and logs "the first process ended ...,
  followed on: ...". So the game file stays, and the library stays hidden, through wiliwili's restart.
  Limits: the pause helper and the boost follow the first process only.

### Portrait in the landscape app: Wine patch 0020 (2026-11-22)

Started from the library, wiliwili came up portrait (1080x2400) in the left part of the landscape
screen, the wallpaper beside it, and rotating the phone changed nothing. Wine's Wayland trace
(`WINEDEBUG_GAME=...,+waylanddrv` in its `utdeck-env`):

- wiliwili's window covers the (landscape, 2400x1080) monitor, so Wine asks for fullscreen; Lomiri
  first configures it **fullscreen 1080x2400**, then nothing more. Patch 0003 (stage 2: a fullscreen
  window follows the phone's rotation, the only sign of which on Lomiri is the configured size) took
  that for a rotation: "fullscreen window follows the rotation to 1080x2400"; the window became an
  ordinary 1080x2400 one, and Lomiri sent no landscape size afterwards.
- Celceta's game window (stage 4.1's traces, `steam-587110-162552.log`) got the same first
  configure (`1080x2400,0x8`), but handled a newer one first and never followed; Lomiri then sent
  `2400x1017` and `2400x1080`, and patches 0017/0018 took it to the full screen. A resizable window
  (GLFW's) follows; a fixed-size game window does not.

**Patch 0020** (`tools/wine-build/patches/0020-winewayland-keep-UTDeck-windows-landscape.patch`):
a window of UTDeck's app (`UTDECK_APP_ID`, patch 0016) with the landscape monitor
(`UTDECK_WAYLAND_LANDSCAPE=1`) never follows a rotation into portrait (traced as "UTDeck's app is
landscape: not following ..."); programs started outside UTDeck keep following the phone. Built
with `build-winewayland.sh` (0001-0020) and put into stone's runtime by hand for the test
(`winewayland.so`, the original kept as `winewayland.so.20261122`); a runtime release follows once
it works (Proton's base too).

Also seen: wiliwili quit from its own menu left its process asleep (windows gone, threads waiting
on pipes), so the game counted as running; ended with `wineserver -k`, as the starting page's "End
the game" does.

Confirmed on stone (the maintainer, 2026-11-22): wiliwili fills the screen (trace: "UTDeck's app is
landscape: not following 1080x2400" three times; frames at 2400x1081, 56-59 fps), and swiping its
card away closed it (`.focus.closed` written, `exit 0`). The trace is off again; wiliwili's
`utdeck-env` keeps `FEX_X87REDUCEDPRECISION=1`.

### Reduced-precision x87 for every game (the maintainer, 2026-11-22)

`FEX_X87REDUCEDPRECISION=1` moved into `defaults.env` (every game, Steam's and the user's; a game's
`utdeck-env` can set it to 0), because games added from the app get no settings page before 4.6,
and wiliwili, the exit's game, does not start without it. It only reaches programs FEX runs: the
x86-64 ones (Ys IX, PvZ Replanted, wiliwili). 32-bit games run through Box64 on this runtime
(Celceta), which this does not touch. wiliwili's own `utdeck-env` removed, so its next start shows
the default at work; Ys IX to be checked for drawing errors.
Checked (the maintainer, 2026-11-22): wiliwili starts with the default alone, and Ys IX looks the
same as before.

## Steps 4 and 5: adding, editing, removing (2026-11-22, UTDeck 0.3.21)

- **`OwnGames`** (`app/src/OwnGames.*`, `ownGames` in QML): the folders in `Documents/UTDeck/Games`
  (each marked if already added); installers (`.exe` in `Downloads`, `Documents`,
  `Documents/UTDeck`); `check(folder)`: `gamecheck.py` in Python's isolated mode, in the background,
  its report read into the executables (the likely game first: not an uninstaller, setup, crash
  reporter, redistributable or helper by name, then the largest) and the DRM-bypass markers (removed
  from the app in 0.3.25, see below);
  `images()` for a cover (the game's folder two levels down, and `Pictures`); `add()`, `edit()`,
  `game()`; `remove(id, deletePrefix)`; `startInstall()`, `installedPrograms()`, `finishInstall()`,
  `pending()` for the installer way.
- **`game.conf`** written as plain `NAME=value` lines (a value never holds a line break), through a
  new file renamed over the old one. The id from the name (lower case letters, digits, `-`; another
  number added if taken).
- **Removing** renames `game.conf` to `game.conf.removed`: out of the library, the prefix with its
  saves kept; adding the same folder again reuses that id and so finds them. Deleting the prefix is
  a separate, explicit choice and only ever deletes the game's own folder under `wine/own` (checked
  by its real path). The game's own files in Documents are never deleted by the app.
- **The installer way**: the game is made with `NAME` and `INSTALLER` and no program, and the
  installer runs through `own-run.sh <id> --install` under the game's unit, as a game: its window
  over the app (the library hides, and comes back when it ends). Then the programs it installed under
  the prefix's `C:` are listed (not `windows`, `users`, `ProgramData`, nor Program Files' `Common
  Files`, `Internet Explorer`, `Windows ...`, `Microsoft...`), the chosen one's folder is checked with
  `gamecheck.py`, and the game keeps its id and prefix. An install left unfinished shows as "Finish
  adding <name>" at the top of "Add a game". (The installer itself is not checked before it runs: its
  files are packed inside it.)
- **Pages**: "Add a game" (the library's header) with both ways, the name, the program list, an
  optional cover, "Add to the library" (disabled while checking, without a program, or with a DRM
  marker); a long press on one of the user's games opens its page: name, program, cover, Save, and
  "Remove from the library..." confirmed on the page, with the prefix deletion as a box to tick and
  what it deletes said (for an installed game: the game itself too).
- The installer's default name keeps the name's own numbers and drops only version-like parts
  (`3DMark_11_v103_installer.exe` gives "3DMark 11"; a first version gave "3DMark").
- `gamecheck.py` goes into the click (`wine/gamecheck.py`). The 3DMark 11 installer copied to stone's
  `Downloads` for the test (3.6 GB free on stone after it).

### Test A passed; test B stuck on Box64 (2026-11-22, UTDeck 0.3.22)

- **Test A** (the maintainer): wiliwili removed (the prefix kept) and added again from its folder in
  the app; it started at once (its prefix found again). Nothing wrong.
- **Test B**, 3DMark 11's installer: prefix made (54 s), Wine Mono (27 s), the installer started,
  then "The installer is running" for minutes with no window. Its main thread ran at 100 % of a core
  for 6 minutes (76 MB of memory) under **Box64** (`wowbox64.dll`; the installer is 32-bit), its
  stack in Wine's signal handling. Stage 2.4 found the same: Box64 stalls on installers that unpack
  their own code while running (3DMark's InstallShield `ISSetup.dll`, PECompact2), FEX runs them,
  and Steam's install scripts run under FEX for that reason (`utdeck-compat`). Stopped with
  `wineserver -k`.
- **Fixes**: `own-run.sh --install` runs installers with `HODLL=libwow64fex.dll` (FEX for their
  32-bit code) and no pause (an unpacking installer has no window in front), unless the game's
  `utdeck-env` says otherwise; "Add a game" offers **"Run the installer again"** for an install that
  left no program, in the same prefix. And `game-run.sh`'s check for a restarted program missed the
  installer, because the kernel cuts process names to 15 characters (`3DMark_11_v103_`): it now looks
  for the `.exe` in the process name and the first two words of the command line (a 64-bit program's
  command line can start with Wine's loader instead); checked on stone with `gptest.exe` (found while
  it ran, nothing after).

### Test B again: installed, two installers, UTDeck closed (2026-11-23)

- Under FEX the installer worked: `3dmark-11` holds the whole installation
  (`C:\Program Files\Futuremark\3DMark11\bin\x64\3DMark11.exe`, 1.8 GB with the prefix).
- The maintainer had started the installer twice by accident: a second tap on "Run the installer"
  made a second game, `3dmark-11-2` (the first one's folder was taken), with its own prefix and
  installer. Swiping one installer away closed UTDeck: an installer's window, like a game's, belongs
  to UTDeck's card, and a swiped card is the app closing (stage 4.3). The other installer runs
  outside the app and finished. So UTDeck's closing was the designed behaviour; the duplicate was not.
- Fixes (0.3.23): `startInstall()` reuses an unfinished game with the same installer (its prefix and
  what was installed) instead of making a second one; the page ignores a second tap while the
  installer is starting.
- stone had **477 MB free** afterwards (the half-finished duplicate is 1.4 GB).

### 3DMark 11's first start: SystemInfo again (2026-11-23)

Added from the app ("Finish adding", `bin\x64\3DMark11.exe`), started from the library: "The game
stopped right after starting", exit 1 after 38 s. Its log: DXVK up, then 3DMark's .NET interface
stopped on `COMException`: class `{f9c82199-b002-41b4-8b00-0f1eca89425f}` could not be created (Wine's
`dllhost` tried, Box64 started for it). Stage 2.4 met the same: the class is **Futuremark SystemInfo**,
an out-of-process 32-bit service (`FMSISvc.exe`) that 3DMark's setup installs as a prerequisite (it did
here) and registers only in the 32-bit registry view (`Wow6432Node\CLSID`), which Wine's COM does not
look in; and SystemInfo's own service registration leaves no service. UTDeck's path worked (installed,
chosen, started); this is the program's known problem under Wine.

Fixed for this prefix as in stage 2.4, as a file kept in the repo:
`tools/ut-probe/game-fixes/3dmark11-systeminfo.reg` (the class in the 64-bit view, `LocalService=FMSISvc`
on its AppID, the service: own process, on demand, LocalSystem), imported with `wine regedit /S`; and
`HODLL=libwow64fex.dll` in the game's `utdeck-env`, so SystemInfo's 32-bit code runs under FEX (Box64
stalled on Futuremark's packed code). A general fix would be Wine's COM falling back to the 32-bit view
for a 32-bit `LocalService` or `LocalServer32`, as Windows does: for later. Stage 2.4's other limit
stays: SystemInfo's hardware scan after a test needs a kernel driver Wine does not have.

### 3DMark 11 runs; quitting it left SystemInfo behind (2026-11-23, UTDeck 0.3.24)

- With the fix, **3DMark 11's window opens, fits the screen, and its graphics tests run correctly**
  (the maintainer ran two, then ended the test window).
- Swiping its panel away ended `3DMark11.exe` (`exit 0`, `.focus.closed` written), but UTDeck still
  counted the game as running: `game-run.sh` "followed on" to `FMSISvc.exe`, SystemInfo's service,
  and SystemInfo's hardware scan `fmsiscan.exe` was running and stuck (stage 2.4's scan, which needs a
  kernel driver Wine does not have). Ended with `wineserver -k`.
- Their parent cannot say what they are (Wine starts Windows programs detached: their parent is
  systemd). So `game-run.sh` now (a) follows only programs started at most 15 s before the first
  process ended, or later, as a restart is (wiliwili's new process starts as the old one ends; a
  service or helper started while the game ran is not followed); checked on stone with `gptest.exe`
  inside and outside that window; and (b) after a swipe (the `.closed` file) follows nothing and ends
  the rest of the prefix (`wineserver -k`), as "End the game" does. The start time is field 22 of
  `/proc/<pid>/stat`, counted after the name in parentheses, and read only for programs whose name
  already matched.

## Done (2026-11-23)

The exit, on stone with Steam off (the maintainer; checked: no Steam process, both runs `exit 0`):

| | How it came in | Played | Removed |
| --- | --- | --- | --- |
| wiliwili | its folder, copied into `Documents/UTDeck/Games`, added in the app | yes, controller and touch (taps) | yes (test A), then added again: its prefix and settings found again |
| 3DMark 11 | its installer, run in the app (FEX), its program chosen | yes, two graphics tests (with its SystemInfo fix) | |

Built on the way: `own-run.sh`, the library with both kinds of games, the add and edit pages,
`OwnGames`; `game-run.sh` following a program that restarts itself and ending what is left after a
swipe; Wine patch 0020 (UTDeck's windows stay landscape); FEX's reduced-precision x87 for every game.

Open:
- **Patch 0020 is only in stone's runtime** (put in by hand). It goes into the next runtime release, on
  both Wine bases (Proton's needs its own copy of the patch), which the maintainer publishes. **Done
  2026-11-24**: runtimes `11.16-20261124` and `proton11.0-20261124` (commit bd0b753; Proton's copy is
  `patches-proton/0020`, since its patch 0003 differs at that spot), in `runtimes.list`.
- The Nothing Phone has none of 4.5 yet (UTDeck 0.3.24, the runtime with 0020).
- Touch scrolling in programs that scroll only with the mouse wheel (wiliwili): stage 5.
- A program left asleep after quitting from its own menu (wiliwili once): "End the game" ends it.
- Wine's COM not looking in the 32-bit registry view (3DMark's SystemInfo): a general fix would spare
  the per-game file.
- Covers: chosen from the game's folder or `Pictures`; none of the two test games brought one.

## The app does not check for DRM bypasses (the maintainer, 2026-11-23; UTDeck 0.3.25)

UTDeck is meant for people in countries where games with their copy protection removed are common,
and where the law on them differs from country to country; like Wine, Proton and emulators, the app
does not police what its users run. So "Add a game" no longer looks for DRM-bypass markers and never
refuses a game: `gamecheck.py --programs` (new) lists the Windows files and the programs' imports
only, which is also much faster (the full check reads every file of a game, gigabytes for a large
one). Removed from the app: the `drm` property, its message, and its hold on "Add to the library".

Our own tests keep the rule of stage 3: we do not run games with a DRM bypass ourselves, and
`gamecheck.py` without options still does the full check (the markers, Valve's signature on
`steam_api.dll`) before a game's first start in our tests.
