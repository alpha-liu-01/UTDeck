# Stage 4.1: the app shell, and the game's window as part of it

Started 2026-11-21. Plan: [stage4-roadmap.md](stage4-roadmap.md). Goal: a Qt 6 QML app with
Lomiri Components, built in our own cross container and installed as a click on stone, that lists
the installed Steam games and starts one through hidden Steam; and first, the answer to how a game
started by hidden Steam is shown by Lomiri as part of the app.

## How Lomiri ties a window to an app (read from its source, 2026-11-21)

On the phone: Lomiri 0.5.0, qtmir 0.7.2 (Lomiri's link to Mir 1.8), lomiri-app-launch 0.1.12.
qtmir's source at tag `0.7.2` (`gitlab.com/ubports/development/core/qtmir`,
`src/modules/QtMir/Application/application_manager.cpp`) has two ways for a window to land on an
app:

1. **By the process, when it connects** (`authorizeSession(pid)`): the app is still "Starting" and
   the process belongs to it (started by lomiri-app-launch, in its unit), or the process carries
   `--desktop_file_hint=FILE` on its command line or `DESKTOP_FILE_HINT=FILE` in its environment.
   Lomiri logs nothing of this check for Wine's connections today (no "REJECTED" either), so it
   may not run for Wayland clients on Mir 1.8.
2. **By the window's own app id** (`findApplicationWithSurface` -> `findApplicationWithAppId`): the
   window's `xdg_toplevel` app id is compared with the id of every app Lomiri runs; an equal one
   owns the window. This does not care who started the process, so it fits games started by hidden
   Steam.

**Why 3.6's experiment B failed**: Wine's Wayland driver names every window after its program,
in lower case (`wayland_surface_make_toplevel`: `xdg_toplevel_set_app_id(process_name)`, from
`waylanddrv_main.c`), on both bases. The test app was `utdeck-lctest`, the window `vktest.exe`; no
app had that id, so Lomiri made it an icon of its own ("Received a surface count changed event from
an app that's not in the Launcher model, creating icon...", logged again today for Aperture Desk
Job's `deskjob.exe`) and treated it as a stranger.

**So the plan**: a small Wine patch (both bases) that takes the app id from an environment variable
(`UTDECK_APP_ID`) when it is set, and UTDeck's app id (`utdeck.alphaliu01_utdeck`) given to every
game through `defaults.env`. Then the check on the screen: orientation from UTDeck's desktop file
(K1), fullscreen without the top bar, one launcher icon, and whether Lomiri's own lifecycle reaches
the game (it pauses an app's processes through lomiri-app-launch; games started by hidden Steam are
not in the app's unit, so `utdeck-pause.py` probably stays).

## Plan

1. **Build setup**: a cross container in `tools/app-build/`, taken from the Dolphin port's
   (`dolphin-ut/ut/docker/`: Ubuntu 24.04 with UBports 24.04-2.x pinned, Qt 6.10.2 for arm64, the
   xdg-shell plugin and the 1.x Qt 6 bundle), without what only Dolphin needed.
2. **The app** in `app/`: CMake, `main.cpp` with the Dolphin port's Qt 6 lessons, a QML module with
   Lomiri Components; a library page listing the installed Steam games (`steamapps/appmanifest_*.acf`,
   covers from Steam's `appcache/librarycache`); a tap starts the game with
   `steam-service.sh play APPID`.
3. **The click**: `utdeck.alphaliu01`, app `utdeck`, unconfined, framework 24.04-2.x; and the
   24.04-1.x click with Qt 6 bundled.
4. **The Wine patch** for the app id, both bases; `UTDECK_APP_ID` in `defaults.env`.
5. **On the screen**: started from the app, sideways and upright: orientation, fullscreen, icons,
   another app in front, the lock screen.

## 1-3. Built (2026-11-21)

- **`tools/app-build/`**: the cross container (`docker/Dockerfile`, image `utdeck-app-cross`),
  taken from the Dolphin port's without Dolphin's own libraries and libhybris' Vulkan: Ubuntu 24.04
  amd64 with arm64 multiarch, UBports 24.04-2.x pinned (Qt 6.10.2 as on the phone), `click`, Qt 6's
  xdg-shell plugin in `/opt/ut-bundle`, and the 1.x Qt 6 runtime in `/opt/ut-runtime-1x`
  (`runtime-24.04-1.x.list`). `build.sh` (CMake and Ninja into `build/app/`), `build-click.sh`
  (`CLICK_UT=24.04-2.x` or `24.04-1.x`, into `build/app-click/`), `install-click.sh` (over the
  click service, as clickable does).
- **`app/`**: `main.cpp` (the Dolphin port's Qt 6 lessons: `ubuntumirclient` dropped, an OpenGL ES
  default surface format, no `QT_SCALE_FACTOR`; SIGTERM sets a flag a timer acts on), `Library`
  (the installed games from Steam's `appmanifest_*.acf`, covers from `appcache/librarycache`, the
  same rules as `steam-library.py`; `play(appid)` runs the same `systemd-run ... steam-service.sh
  play APPID` as the app drawer entries), and QML with Lomiri Components: `Main.qml` (a `MainView`
  with a `PageStack`), `LibraryPage.qml` (a header with Refresh, a grid of covers, a status line,
  read again whenever the app comes to the front), `GameCard.qml` (a `LomiriShape` cover, the title
  below).
- **The click** (`app/click/`): package `utdeck.alphaliu01`, app `utdeck` (app id
  `utdeck.alphaliu01_utdeck`), unconfined, `X-Lomiri-Supported-Orientations=landscape` (a console:
  the library and the games are landscape, which also answers K1 if games belong to the app),
  `launcher.sh` after the Dolphin port's.
- **Wine patch 0016** on both bases (`winewayland: take the window app id from UTDECK_APP_ID`):
  when the variable is set, Wine's windows carry it as their `xdg_toplevel` app id instead of the
  program's name; nothing changes without it. The default runtime rebuilt with it (same day, so the
  same name `11.16-20261121`; the previous day's stays for going back). **Proton's base waits**: its
  `proton11.0-20261121` is a published release with a checksum, and a rebuild under the same name
  would not match it; it comes with the next release.

Installed on stone (2026-11-21, 13:40-13:45): the click `utdeck.alphaliu01` `0.1.0.2x` (130 KB) through
the click service; the default runtime with 0016. For the test, only Ys: Memories of Celceta
(587110) gets `UTDECK_APP_ID=utdeck.alphaliu01_utdeck` (its `utdeck-env`); every other game keeps
Wine's own window name, for comparison.

## 5. Manual run M1: the game's window as part of the app

1. Phone **sideways** (the K1 case), unlocked. Open **UTDeck** from the app drawer: the native look,
   landscape, the installed games with their covers.
2. Tap **Ys: Memories of Celceta**. The status line says it is starting; the game comes up through
   hidden Steam.
3. Look at: the game's orientation and whether Lomiri's top bar shows; the launcher (swipe from
   the left edge): one UTDeck icon, or an extra icon for the game; the app switcher (swipe from the
   right edge): one card or two.
4. Another app to the front and back; the lock screen and back.
5. Quit the game from its own menu: does UTDeck's library come back?

### M1 results (2026-11-21, the maintainer)

| Check | Result |
|---|---|
| Library | native look, landscape; covers right except two (below) |
| Celceta started from UTDeck, phone sideways | **landscape, no top bar**: the game's window belongs to UTDeck (route 2 works) |
| App switcher | **two cards**, both with UTDeck's name and icon (the library and the game), none with the game's own |
| Orientation | UTDeck and every game started from it stay landscape (the desktop file's setting) |
| First picture | Celceta first came up with the wrong shape (the K1 look); starting it with the phone upright did not help, the app switcher and back fixed it |
| Another app, lock screen | as in 3.6: paused away, back on return (`utdeck-pause.py`, as expected: the game is not in the app's unit) |
| Covers | Plants vs. Zombies: Replanted none, Stray the wrong picture |
| Library back after quitting | not reported (to check in M2) |

**Covers**: this Steam keeps some pictures in `librarycache/<appid>/<hash>/`, and the portrait
cover there is `library_capsule.jpg` (300x450 on stone, like `library_600x900.jpg`). PvZ's cover is
`<hash>/library_600x900.jpg`, so nothing was found; Stray has only `<hash>/library_capsule.jpg`, so
the top-level `logo.png` was shown. Now the app (`CoverFor`) and the drawer entries
(`steam-library.py icon_for`) search one level down, by kind (portrait, then the wide header
`header.jpg`/`library_header.jpg`, then the logo), the newest copy first. Drawer entries rewritten
on the phone: Stray and PvZ now point at their covers.

**Two cards and the first picture**: Lomiri lists windows, not apps (`TopLevelWindowModel`), so
the library and the game are two cards of the same app. And Lomiri makes a window fullscreen
properly only when no other window of the app shows (the Dolphin port's stage 3.2), which fits the
wrong first shape that a trip through the switcher fixed. Both point to the Dolphin port's answer:
hide the library while a game is in front.

**Hiding is not free on Lomiri** (qtmir 0.7.2, `application.cpp`):
- `Application::updateState`: when the app has one process (session) and it has lost all its
  windows (`lostAllSurfaces`), the app is closed after 1 s (`applyClosing`, `m_stopTimer`, 1000 ms).
- The game's window joined the app only in Lomiri's window list (`findApplicationWithSurface` by
  app id); the game's processes are not sessions of the app. So a hidden library would leave
  UTDeck "with no window" and get it closed.
- A process joins a running app as one of its sessions when it carries `DESKTOP_FILE_HINT` (route 1
  above: "attaching to existing entry"). The short app id is enough (`utdeck.alphaliu01_utdeck`:
  lomiri-app-launch finds the installed version). Then the app has several sessions and the 1 s
  rule does not apply while the game runs.
- When the game ends, its sessions go; the library has to be back before the last one does.

## 6. After M1 (2026-11-21): hiding the library while a game is in front

- **`game-run.sh`** keeps a file for the app: `$XDG_RUNTIME_DIR/utdeck/game`, "LABEL PID FOCUS_FILE"
  (`steam-587110`, the script's pid, Wine's focus file of patch 0015). Written when the game starts,
  removed right after the game's own process ends (`wait $G`), before the helpers' last lines and
  `wineserver -k` (up to about 2 s): the time the library has to come back before Wine's last
  processes, and with them the app's extra sessions, are gone.
- **The app** (`Library`: `runningGame`, `gameInFront`) watches that folder (inotify, through
  `QFileSystemWatcher`) and, while a game runs, reads the focus file four times a second. A file
  whose pid is not a running `game-run.sh` is ignored (a killed run). `Main.qml`: the window hides
  when the game's window has the focus (so the game's processes are connected, and joined to the
  app, before the library goes), and shows itself again, raised and activated, as soon as no game
  runs. Lomiri hands the app's next window the state its last window closed in (fullscreen, after
  a game), so the library then asks for a normal window, at most three times (the Dolphin port's
  stage 5 fix).
- **Celceta's `utdeck-env`** also gets `DESKTOP_FILE_HINT=utdeck.alphaliu01_utdeck`, for the test;
  every other game still has neither variable.
- Installed (2026-11-21, 15:42-15:45): the click `0.1.1.2x`, `game-run.sh`, `steam-library.py` (the
  drawer entries rewritten).

If the hint does not join the game's processes to the app, Lomiri closes UTDeck about 1 s after the
library hides: the game keeps running (it is not UTDeck's process), but the library does not come
back. That would show in M2 as no UTDeck in the launcher during the game, and nothing after it.

### Manual run M2

1. Close UTDeck if it is open (swipe its card away), phone sideways, open UTDeck: Stray and PvZ
   show their covers.
2. Tap Celceta. When the game is up: one card in the app switcher? Is the first picture right this
   time, without a trip through the switcher?
3. Another app and back; lock and back (as in M1).
4. Quit the game from its own menu: does the library come back by itself, with the top bar (not
   fullscreen)?

### M2 results (2026-11-21, the maintainer)

| Check | Result |
|---|---|
| Covers | Stray and PvZ right |
| App switcher while Celceta runs | **one card** |
| First picture | **right, landscape from the start**, no trip through the switcher needed |
| UTDeck in the launcher during the game | yes: the game's processes joined the app (`DESKTOP_FILE_HINT` works), Lomiri did not close it |
| Another app, lock screen | as expected |
| Quitting the game | **the library comes back by itself**, with the top bar |
| Top bar during the game | **shown**: the game is no longer fullscreen (in M1 it was) |

So hiding the library fixed the cards and the first shape, and the game's end hands back to the
library, but the game lost its fullscreen.

What Lomiri's source says (`qml/Shell.qml`, `qml/Stage/Stage.qml`):
- The top bar hides only when the **focused window's state is fullscreen**
  (`topLevelSurfaceList.focusedWindow.state == Mir.FullscreenState`).
- When a new window has drawn its first frame (`windowReady`), Lomiri gives it the state the app's
  last closed window had (`WindowStateStorage`, saved on a window's destruction), unless the new
  window is already fullscreen ("Initial window state is fullscreen, not using saved state") or the
  saved state is fullscreen ("Saved window state is fullscreen, using initial window state"). Neither
  line was logged for the game's window in M2, so the game was not fullscreen when it drew its first
  frame, and got the saved state.
- A touch app's window that is hidden leaves Lomiri's window list (`Mir::HiddenState` ->
  `removeAt`) and comes back when shown.

Not yet known: whether Lomiri then took fullscreen away from the game (Wine should ask again while
the Win32 window stays fullscreen-sized: `wayland_surface_update_state_toplevel`), or the game kept
it and Lomiri's focused window changed when the library left. A traced run decides it: Celceta's
`utdeck-env` gets `WINEDEBUG_GAME=err+all,fixme-all,trace+waylanddrv` (Wine's Wayland driver logs
every configure it receives with its size and state, and every state it asks for, into the game's
log; nothing of the account).

### Manual run M3 (traced)

1. Open UTDeck, tap Celceta, play about 20 s once the top bar shows; note whether the top bar was
   there from the first picture or appeared later.
2. Quit the game from its menu.

### M3 results (2026-11-21): Lomiri takes fullscreen away

The maintainer: the top bar never went away, in the library, in Celceta's launcher dialog, or in
the game. The trace (`steam-587110-160758.log`, Wine's Wayland driver) for the game's window:

| Step | Size, state |
|---|---|
| Wine asks for fullscreen before the window shows | (`window_state=0x8`) |
| Lomiri's configures | `0x0` normal, `1080x2400` normal, **`1080x2400` fullscreen**, `1080x2400` normal, `2400x1017` normal (twice) |
| Wine afterwards | the Win32 window stays 2400x1080 and fullscreen (`window=(0,0)-(2400,1080),0x8`), the compositor's state `0`; **no new request** |

So Lomiri grants fullscreen and takes it away a moment later, as `windowReady` in `Stage.qml`
does: once the window has drawn, the state the app's last window closed in (here the library's,
normal) replaces it. Nothing in the game's window changes afterwards, and patch 0004 asks again only
while the compositor still calls the window fullscreen, so nobody asks again. In M1 the game ended
fullscreen only because the trip through the switcher made Celceta create its window anew (Lomiri
then logged "Initial window state is fullscreen, not using saved state").

**Wine patch 0017** (both bases, on top of 0004): `wayland_surface_check_fullscreen_size` (on each
presented frame, 0.5 s after the last configure) also asks for fullscreen when the window wants it
and the compositor's state is not fullscreen at all, at most three times (the count resets once
the window covers the screen); not before the first configure, nor while one waits. The default
runtime is rebuilt with it (`11.16-20261121`, same day); Proton's base takes it with its next
release, like 0016.

Installed on stone (2026-11-21, 16:15): the default runtime `11.16-20261121` with 0001-0017
(`runtime-push.sh`; the new code checked in `winewayland.so`). Celceta's trace stays on for the
next run, which should show "fullscreen was taken away, asking again" and then a fullscreen
configure.

### Manual run M4

1. Open UTDeck, tap Celceta. Does the top bar go away once the game is up (it may show for about a
   second first)?
2. Another app and back, lock and back: still fullscreen afterwards?
3. Quit the game: the library back, with the top bar.

### M4 results (2026-11-21): fullscreen, but 63 px short

The maintainer: the top bar hides now, but the game had a strip of wallpaper along the bottom; a
trip through the app switcher made it right. The trace (`steam-587110-161427.log`):

| Moment | Lomiri sent | Wine's window geometry |
|---|---|---|
| before fullscreen (normal state) | 2400x1017 normal | **2400x1080** (Wine reports its whole Win32 window in the normal state) |
| Wine asks again (0017) | 2400x1017 fullscreen | 2400x1017 |
| 0004's two more tries (unset, set) | 2400x1017 normal, then fullscreen | 2400x1017 |
| about 20 s | nothing | 2400x1017 (the strip) |
| app switcher and back | 2400x1017, then **2400x1080**, fullscreen | 2400x1080 |

Lomiri in phone mode sizes each window to the free area (`Stage.qml`, state "staged":
`requestedHeight: availableDesktopArea.height`), which is the whole screen only while the top
bar is in fullscreen mode, and that needs the focused window to be fullscreen already (`Shell.qml`).
So a window turning fullscreen is first sized with the bar counted. Coming back from the switcher,
Lomiri did send 2400x1080, so it can resize a fullscreen window; at the start the follow-up resize
never came. Two explanations: qtmir skips a resize to the size it believes the window already has
(`MirSurface::resize`: `mirSizeIsDifferent`), and Wine had reported 2400x1080 while held normal; or
the free area was already full when Lomiri sent 1017, so nothing changed afterwards. Lomiri's own
resize log (qtmir's debug lines) would tell, but Qt reads its log settings only when Lomiri starts
(a restart closes every app). The maintainer chose to try the Wine fix for the first explanation
first.

**Wine patch 0018** (both bases): a window that wants fullscreen but is held in the normal state
at a known size reports its visible part clipped to that size as its geometry (Wine's existing
rule for maximized and fullscreen windows), instead of the whole fullscreen-sized Win32 window.
If the strip stays, the second explanation is the one, and Lomiri's log comes next.

Installed on stone (2026-11-21): the default runtime `11.16-20261121` with 0001-0018. Manual run
M5: as M4 (Celceta from UTDeck; the strip at the start? still right after another app and the
lock screen? the library back after quitting).

### M5 results (2026-11-21): fills the screen

The maintainer: the game fills the whole screen; the strip goes about a second into the game, and
about a second after coming back from another app. The trace (`steam-587110-162552.log`): the first
fullscreen still came at 2400x1017; 0004's next try (unset, set) now went through the normal state
with the window reporting 2400x1017 (0018), and Lomiri then sent **2400x1080 fullscreen**, which it
never did before 0018. The run ends at 2400x1080. So the first explanation of M4 was the right
one: qtmir skips a resize to the size it believes the window already has.

What the second costs: Lomiri's first fullscreen is still 63 px short, and 0004 needs one try
(0.5 s after the size settles) to fix it. Fine for now; a later look could ask in a way that skips
the first short size.

## 4. Done (2026-11-21): every game started while UTDeck runs belongs to it

`game-run.sh` sets `UTDECK_APP_ID` and `DESKTOP_FILE_HINT` to the running app's id (from its
`APP_ID`, shortened to `package_app`) when UTDeck runs, and logs it (`app: ...` in the game's log).
Without the app running nothing is set: `DESKTOP_FILE_HINT` would make Lomiri create a "UTDeck"
app out of the game's own processes, which is untested, so a game started from its drawer entry
stays a window of its own, as before. Not in `defaults.env`, because it depends on the moment.
Celceta's test `utdeck-env` (the two variables and the trace) is removed.

### M6 (2026-11-21): another game

The maintainer started Ys IX: Monstrum Nox from UTDeck: landscape, fullscreen, the library back
after quitting. Its log says `app: utdeck.alphaliu01_utdeck`: the id came from the running app,
with nothing set for the game itself.

## 3b. The 24.04-1.x click (2026-11-21)

`CLICK_UT=24.04-1.x tools/app-build/build-click.sh`: `utdeck.alphaliu01_0.1.1.1x_arm64.click`,
19.8 MB (the 2.x one is 139 KB), with Qt 6 and its QML modules (Lomiri Components for Qt 6
included) from `/opt/ut-runtime-1x`. No 24.04-1.x phone was connected, so it was checked on stone:
- **What it needs from the phone**: 43 libraries outside the click (from every bundled library,
  plugin and the program: `readelf` NEEDED, less what the click carries), none of them Qt 6; every
  one is also needed by the Dolphin port's 1.x click, whose list was checked against a real 1.x
  phone (the Nothing Phone (1), Dolphin port stage 7).
- **A run on stone** (the click unpacked into a test folder, not installed, started over adb for
  8 s through its `launcher.sh`): it stays up, logs only the two lines the installed app logs, and
  every Qt file it maps comes from the click (151), except one plugin from Qt's own default plugin
  folder (`libcomposeplatforminputcontextplugin.so`, which a 1.x phone does not have). Started from
  an adb shell, libhybris first failed with "cannot allocate memory in static TLS block": Lomiri's
  apps get `GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096` from
  `/etc/profile.d/hybris-static-tls.sh`, which an adb shell does not read (the Dolphin port's stage
  1 met the same). Sourced, it ran.

It cannot be installed on stone next to the 2.x click (same package, lower version).

**On a real 24.04-1.x phone (2026-11-21, the Nothing Phone (1), frameworks `ubuntu-touch-24.04-1.x`,
no Steam and no UTDeck runtime there):** `install-click.sh` picked the 1.x click by the phone's
framework and installed `0.1.1.1x` (its new "stop a running copy" step ran, with nothing to stop).
The maintainer opened it from the app drawer: landscape, the native look, the empty-library message;
refresh and closing from the app switcher work (the unit ended, no process left). Its log: the
`UCPageWrapper` line also seen on stone, and two libhybris lines of that phone's driver
(`eglSubDriverAndroid.so` not found, an empty `gralloc.default.so`) that did not stop it drawing.
Starting a game there is not tested (no Steam on that phone).

## Done (2026-11-21)

| Exit criterion ([stage4-roadmap.md](stage4-roadmap.md)) | Result |
|---|---|
| The app opens with the native look and starts a game | yes: Lomiri Components, landscape, the installed games with their covers, a tap starts the game through hidden Steam |
| The window question answered on the screen, with a decision | yes: the game's window carries the app's id (Wine 0016) and its processes join the app (`DESKTOP_FILE_HINT`), set by `game-run.sh` whenever UTDeck runs; the library hides while a game is in front; fullscreen held by Wine 0017 and 0018. Landscape, one card, UTDeck's icon, no top bar, the library back after the game (M1-M6, Celceta and Ys IX) |
| The 24.04-1.x click builds | yes, 0.1.1.1x with Qt 6 bundled, checked on stone and on screen on the Nothing Phone (1) (24.04-1.x): installs, opens, closes; no game started there |

Still open, and where each goes:
- **Proton's base** has 0016-0018 as patches but not in a build: its next release (a new date;
  `proton11.0-20261121` is published and stays as it is). Until then a game on the Proton runtime
  is a window of its own, as before 4.1.
- **A game on a 24.04-1.x phone**: the Nothing Phone has no Steam or UTDeck runtime; the app
  itself works there.
- **Lomiri's first fullscreen is 63 px short** for about a second (0004 fixes it on its next try).
- **Lomiri's lifecycle does not reach the game** (not in the app's unit): `utdeck-pause.py` stays.
- **The library's look while a game starts** (it stays as it is until the game's window is in
  front; Celceta's own launcher dialog is a window of the app too): 4.2's starting screen.
- **`install-click.sh` stopping a running copy** has run through a full install (on the Nothing
  Phone), but with no copy running; stopping an open one is not yet seen.

## Events

- 2026-11-21: 4.1 started; Lomiri's (qtmir 0.7.2) way of tying windows to apps read; the cause of
  3.6's failed experiment B found (Wine's window app id is the program's name).
- 2026-11-21: M1 done by the maintainer: the game's window belongs to UTDeck (landscape, no top
  bar, UTDeck's name and icon), but two cards and a wrong first shape; two covers wrong (Steam's
  hashed image folders). Covers fixed; the library now hides while a game is in front, with
  `DESKTOP_FILE_HINT` so that Lomiri does not close it; click 0.1.1 installed; M2 next.
- 2026-11-21: M2's first try: UTDeck did not open. The 0.1.0 process, paused by Lomiri in the
  background, had been left frozen by the upgrade, and Lomiri queued every new start behind it. Woken
  by hand (`kill -CONT`), it closed and Lomiri started 0.1.1 itself. `install-click.sh` now stops a
  running copy before installing.
- 2026-11-21: M2 done: one card, the right first picture, the library back after the game; the top
  bar now shows during the game. Lomiri's top bar and window state code read; a traced run (M3)
  set up for Celceta.
- 2026-11-21: M3 (traced): Lomiri grants the game's fullscreen and takes it away once the window
  has drawn (the app's saved state); Wine never asked again. Patch 0017 written for both bases;
  default runtime rebuilt with it.
- 2026-11-21: M4: 0017 works (fullscreen granted, the top bar hides), but the picture is 63 px
  short until a trip through the switcher. Patch 0018 written for both bases (the maintainer's
  choice, before a Lomiri restart for its log); default runtime rebuilt.
- 2026-11-21: M5: the game fills the screen (0018 confirmed: qtmir skipped the resize). Step 4:
  every game started while UTDeck runs carries its app id (`game-run.sh`); Celceta's test settings
  removed; 3.6's experiment B corrected.
- 2026-11-21: M6 (Ys IX from UTDeck) passed. The 24.04-1.x click built (0.1.1.1x, Qt 6 bundled)
  and checked on stone (libraries, an 8 s run on the bundled Qt); on screen on a 1.x phone pending.
- 2026-11-21: 4.1 done (exit criteria met); open items listed above.
- 2026-11-21: the 1.x click installed and checked on screen on the Nothing Phone (1): opens in
  landscape with the native look, closes cleanly.
