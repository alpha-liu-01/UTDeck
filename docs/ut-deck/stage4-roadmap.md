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
| 4.2 The launch flow | A starting screen with what is happening (Steam starting, signing in, install scripts, Wine Mono); hidden Steam's dialogs shown in the app, accepted only by the user; errors shown; back to the library when the game ends | A first start and a normal start of two games, each step visible; a publisher's agreement and Steam's Steam Input notice shown and answered in the app |
| 4.3 Per-game settings | A page per game: the Wine base (with the download of the second one), launch options, boost, pause, logs; the Windows build forced for games with a Linux build, before the download | Each setting changes what the game gets (checked in its logs); a game with a Linux build installs its Windows build from the app |
| 4.4 Steam without its interface | Sign-in (the user types name, password and Steam Guard code into the app, which hands them to hidden Steam and keeps nothing), installing and uninstalling games, free space | A game installed and removed from the app with no PC and no Steam phone app |
| 4.5 The game round | Three or more new games through the app, bringing the table to ten; stage 3's leftovers for a game round | The stage 3 table at ten games, and stage 4's exit run by the maintainer |

Order and why: 4.1 first, because every later page lives in the app and the window question decides
how games are started. 4.2 before 4.3: a game that seems not to start (3.9's first start) is the
most visible problem. 4.4 can move earlier if signing in becomes the blocker. Prefix management and
a Turnip driver manager (the original stage 4 list) wait until a game needs them; controllers are
stage 5.

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
