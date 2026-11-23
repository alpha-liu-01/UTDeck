# Stage 4: the frontend, the plan

Written 2026-11-21, after stage 3 was wrapped up ([stage3-roadmap.md](stage3-roadmap.md)). Goal (from
[feasibility-and-roadmap.md](feasibility-and-roadmap.md)): someone picks up the phone and plays,
without a keyboard. Exit of the whole stage: from a fresh install, add a game, start it, play with a
gamepad, quit back to the library.

The maintainer's requirements (2026-11-21): **Qt 6 QML with the native Ubuntu Touch look** (Lomiri
Components, as the Dolphin port's frontend), on Ubuntu Touch 24.04-2.x (Qt 6.10 on the system) and
on 24.04-1.x, which has no Qt 6: there the click brings Qt 6 along, as the Dolphin port's 1.x click
does (`dolphin-ut/docs/ut-port/stage7-release.md`).

Terms used here:
- *click* = Ubuntu Touch's app package; *framework* = the system version a click is built for
  (`ubuntu-touch-24.04-2.x`, `ubuntu-touch-24.04-1.x`).
- *Lomiri Components* = Ubuntu Touch's own QML controls (the "Suru" look); the Qt 6 build is
  `qml6-module-lomiri-components`, on the phone.
- *app id* = the name Lomiri knows an app by: for a click `package_app` (for example
  `utdeck.alphaliu01_utdeck`), for a plain desktop file its file name.
- *xdg_toplevel app id* = the name a Wayland window gives itself; Lomiri uses it to find the app a
  window belongs to (4.1).

## What exists, from stage 3

- **Starting a game**: `steam-service.sh play APPID` (hidden Steam started if needed, its sign-in
  awaited, the game launched); one app drawer entry per installed game (`steam-library.py`, 3.8).
- **Per-game settings**: `compatdata/<appid>/utdeck-env` over `defaults.env` (3.4); the base per
  game through Steam's tool mapping (`set-default.py --app`).
- **The second runtime** downloaded on request (`get-runtime.sh`, GitHub releases).
- **From the Dolphin port** (`dolphin-ut/ut/`, `docs/ut-port/stage3*.md`, `stage5-packaging.md`,
  `stage7-release.md`): a cross container with the phone's Qt 6.10.2 and a 1.x Qt 6 bundle; the
  click scripts (launcher, manifest, unconfined AppArmor, install over the click service); Qt 6
  lessons on this phone (an OpenGL ES default surface format, no `QT_SCALE_FACTOR` with Lomiri
  Components, the xdg-shell plugin bundled, fullscreen asked after the window is mapped with no
  other window of the app showing, hfd-service for vibration, a page harness for screenshots).

## What stage 3 left for this stage

From [stage3-roadmap.md](stage3-roadmap.md), "Stage 3 wrapped up":
1. The game's window as part of the app: K1 (started sideways, a game comes up portrait),
   landscape-only games reshaped to portrait, Lomiri's top bar over a maximized window.
2. Hidden Steam's dialogs shown to the user (publishers' agreements, Steam's notices before a launch).
3. A starting screen (a first start can take minutes with nothing on screen).
4. Games with a Linux build: UTDeck's tool forced before the first download.
5. Per-game settings in the app.
6. Steam's own tasks without its interface: sign-in, install, uninstall, free space.
7. The next game round (the table to ten games).

## Substages

| Substage | Goal | Exit |
| --- | --- | --- |
| **4.1 The app shell, and the game's window as part of it** | A Qt 6 QML app with Lomiri Components, built in our own cross container, installed as a click on stone: it lists the installed Steam games and starts one through hidden Steam. The question first: how a game started by hidden Steam is shown by Lomiri as part of the app (orientation, fullscreen, one icon, Lomiri's lifecycle) | The app opens with the native look and starts a game; the window question answered on the screen with a decision; the 24.04-1.x click builds |
| **4.2 First-time setup from the app** | On a phone with nothing but the click: the app installs UTDeck's pieces (its scripts and compatibility tools, carried in the click) and the Wine runtime (`get-runtime.sh`, a published release), with progress shown: the mandatory part. Then **Steam, optional**: "Set up Steam" or "Skip" (it can be added later from the settings): the hidden screen (Xvfb) and Valve's client, then the sign-in **by QR code** (the app reads the QR code from hidden Steam's own sign-in window and shows it natively, the user scans it with the Steam phone app, Steam signs itself in; no password, code or token passes through UTDeck), with fallback B (hidden Steam's sign-in window inside the app). A Steam switch in the settings: off stops hidden Steam (1.3-1.4 GB of memory) and keeps it from starting with the app; a Steam game's Play starts it on demand | On the Nothing Phone (1), from the click alone, no PC: the mandatory part installed; once with Steam skipped (no Steam running), once with Steam set up and signed in by QR code; the fallback seen working once; the Steam switch off and on |
| **4.3 The launch flow** | A starting screen with what is happening (Steam starting, signing in, install scripts, Wine Mono); hidden Steam's dialogs shown in the app, accepted only by the user; errors shown; back to the library when the game ends | A first start and a normal start of two games, each step visible; a publisher's agreement and Steam's Steam Input notice shown and answered in the app |
| **4.4 Controllers** | Every controller the maintainer has reaches games, or there is a written reason why not: each one followed from the kernel to the game (driver, device nodes, SDL in Wine's `winebus`, XInput and DirectInput) and fixed where it is lost; a native Controllers page that shows what games see, live. Plan: [stage4.4-controllers.md](stage4.4-controllers.md) | On stone: the BETOP T6 (Bluetooth and wired), an Xbox controller, the DualShock clone and the DualSense each work in a game or have their reason written down; the Switch Pro path ready, marked untested; the page shows each one |
| 4.5 Your own games | Games without Steam: added from a folder (name, the `.exe`, optional cover art), each with its own Wine prefix, started through `game-run.sh` like Steam's, listed in the library with Steam's; edited and removed from the app. (Another store may come later.) | Two games added and played from the app with Steam off or not set up; one removed |
| 4.6 Per-game settings | A page per game: the Wine base (with the download of the second one), launch options, boost, pause, logs; the Windows build forced for Steam games with a Linux build, before the download | Each setting changes what the game gets (checked in its logs); a game with a Linux build installs its Windows build from the app |
| 4.7 Steam without its interface | Installing and uninstalling games, free space, signing in again (the QR code, when Steam's token runs out) | A game installed and removed from the app with no PC and no Steam phone app |
| 4.8 The game round | Three or more new games through the app, bringing the table to ten; stage 3's leftovers for a game round | The stage 3 table at ten games, and stage 4's exit run by the maintainer |

Order and why: 4.1 first, because every later page lives in the app and the window question decides
how games are started. **4.2, the setup, moved first (2026-11-21, the maintainer):** every later
substage needs a phone where Steam works, and on a fresh phone that took a PC, adb and six scripts;
the Nothing Phone (1), with UTDeck's click and nothing else, is where it is tested, so nothing from
stone's hand setup can hide a gap. The sign-in is a QR code because UTDeck then never handles a
password, and to keep Steam's own interface (no GPU acceleration for its web view, little memory)
away from the user as far as possible. **Steam optional (2026-11-21, the maintainer):** the Wine
runtime runs games without Steam, so Steam is offered, not required, and can be switched off to save
memory; another store may come later, and the user can always bring their own games. Making Steam
optional is part of the setup (4.2, before its exit run); the user's own games are their own
substage (4.4). The launch flow (4.3) comes first: a game that seems not to start (3.9's first
start) is the most visible problem, and the Nothing Phone's fresh Steam shows every first-launch
dialog now. Prefix management and
a Turnip driver manager (the original stage 4 list) wait until a game needs them; controllers are
stage 5 (real controllers moved to 4.4 on 2026-11-22; touch controls stay in stage 5).

## Test devices (the maintainer, 2026-11-21)

- **stone** (Redmi Note 12 5G, Adreno 619, 24.04-2.x): the development phone, and **the test device
  for the controller stage (5) and the UI work**.
- **The Nothing Phone (1)** (Spacewar, Adreno 642L, upgraded to 24.04-2.x by the maintainer): the
  fresh phone for the setup and first launches, and about 5x stone's GPU speed. Not for controllers:
  the BETOP T6 is not reliable on it.
- Controllers that reach no game on either phone: a DualShock clone and a genuine DualSense (for
  stage 5 to look at).

## Rules for this stage

- The app is a **click**, unconfined (the Dolphin port's decision, for gamepads and Wine's process
  model), installed in the user's account; nothing system-wide.
- **No account data in the app's files or logs**: the Steam rules of stage 3 apply (no account
  name, password, code, token or number written anywhere; Steam's files that hold them never leave
  the phone).
- **The user accepts agreements**; UTDeck only shows them.
- Code, comments, strings and notes in English; the Lomiri look; touch and controller first.

## Status

- 4.1 started and done 2026-11-21. Notes: [stage4.1-app-shell.md](stage4.1-app-shell.md). The
  game's window belongs to the app (Wine 0016 and `DESKTOP_FILE_HINT`, set by `game-run.sh` while
  UTDeck runs), the library hides while a game plays, fullscreen held by Wine 0017 and 0018; the
  1.x click builds. Open: Proton's base gets 0016-0018 with its next release; a game on a 1.x
  phone (the app itself checked on the Nothing Phone (1)); the first second of fullscreen is 63 px short; Lomiri's lifecycle still does not reach
  the game (`utdeck-pause.py` stays). Next: 4.2.
- Reordered 2026-11-21: first-time setup became 4.2 (with the QR sign-in); the launch flow is
  4.3, per-game settings 4.4, Steam without its interface 4.5, the game round 4.6.
- Reordered again 2026-11-21: Steam optional (in 4.2), the user's own games as 4.3; the launch flow
  is 4.4, per-game settings 4.5, Steam without its interface 4.6, the game round 4.7.
- 4.2 started and done 2026-11-21. Notes: [stage4.2-setup.md](stage4.2-setup.md). On the Nothing
  Phone (1), 24.04-1.x, from the click alone: UTDeck's files and the default runtime (published as
  a GitHub release, `runtime-11.16-20261121`), Steam optional (skip, or set up later from
  Settings), sign-in by the QR code read from hidden Steam's own window and drawn by the app, with
  fallback B (that window inside the app), Steam's switch. Open: the 2.x click with the setup on
  stone (built, 0.2.9.2x). Finished right after: Xvfb through Ubuntu's signed index, a free-space
  check before Steam, Proton's runtime `proton11.0-20261122` with 0016-0018. Next: 4.3.
- Reordered again 2026-11-21 (evening): **the launch flow becomes 4.3**, the user's own games 4.4.
  Hidden Steam's dialogs come on a game's first launch (install scripts, publishers' agreements,
  Steam's Steam Input notice); stone's games are past theirs, while every game on the Nothing Phone's
  fresh Steam still has its first launch ahead: the chance to see the real dialogs is now.
- 4.3 started 2026-11-21 and done 2026-11-22. Notes: [stage4.3-launch-flow.md](stage4.3-launch-flow.md).
  The starting page with Steam's steps in words; hidden Steam's dialogs on the "Steam is asking" page
  (a dialog window of its own, or the box an agreement is drawn in inside Steam's main window, with
  taps and scrolling passed on, a restart after an agreement is accepted, and a cancel); errors in
  words with "End the game"; the swipe that closes a game closes the app. On the way: the Nothing
  Phone moved to 24.04-2.x (1.x's Mir did not show Wine's subsurfaces), and the GPU driver became part
  of the runtime (`runtime-11.16-20261122`). Exit met on the Nothing Phone with Ys IX and PvZ
  Replanted. Open: the Visual C++ helper on a real install script; the box view with a real agreement.
- Reordered 2026-11-22 (the maintainer): **controllers become 4.4**, pulled forward from stage 5 (real
  controllers only; touch controls, the virtual controller and the on-screen keyboard stay there).
  Stage 4's exit needs a gamepad, and only the BETOP T6 over Bluetooth reaches games today. The
  user's own games are 4.5, per-game settings 4.6, Steam without its interface 4.7, the game round
  4.8. 4.4 is tested on stone (its Bluetooth is the more reliable), so it starts when stone is at hand.
