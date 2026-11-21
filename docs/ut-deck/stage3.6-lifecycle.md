# Stage 3.6: lifecycle

Goal (roadmap): what Wine and the game do when the screen locks or another app comes to the front,
and when the player comes back: sound, timers, the gamepad, the GPU. Exit: pause and resume work in
two games, or the problems are written down with a plan.

Started 2026-11-20, after 3.5 (3.4's items 8-11 stay open).

## What is known before starting

- **Lomiri pauses apps that are not in front**: it stops every process of a Lomiri app with SIGSTOP
  (a signal that freezes a process until it gets SIGCONT) when another app comes to the front or
  the screen locks (3.S: Steam started as an app was frozen, state `T`; the Dolphin port met the
  same).
- **Our games are not Lomiri apps.** They are started by hidden Steam, a systemd user service
  (3.S), and since 3.8 the app drawer entry only hands off to that service and exits. So Lomiri has
  no app to freeze: the game most likely keeps running behind the other app or the lock screen,
  drawing frames nobody sees (with MAILBOX it does not wait for the screen), playing sound, using
  the CPU, the GPU and the battery. Not yet observed.
- Lomiri still sees the game's window (a Wayland surface): it can tell the window it lost focus
  (`wl_keyboard.leave`, the `activated` state gone from `xdg_toplevel.configure`). Many games pause
  or mute themselves when Windows tells them they lost focus (`WM_ACTIVATEAPP`); whether Wine's
  Wayland driver passes that on under Lomiri is to be seen.
- Known oddities: the screen off for automated runs measured faster (frames not shown); Lomiri hung
  once during automated tests (3.4) and crashed once with ATRI (3.3).

## Plan

1. **A watcher** (`lifecycle-watch.sh`, every second): the game's processes and their state
   (running, sleeping, stopped), their CPU use, the GPU's load, frames presented, the screen's
   backlight, whether the lock screen is up, the sound streams (PulseAudio's sink inputs, corked or
   playing).
2. **A test program first**: `vktest` fullscreen started like a Steam game (a background service,
   not a Lomiri app); the maintainer switches to another app for 30 s and back, then locks the
   screen for 30 s and unlocks it. What runs on, what comes back.
3. **Two games**, with sound and the controller: Ys: Memories of Celceta and Ys IX, the same two
   steps, plus what the maintainer hears and whether the controller still works afterwards.
4. **Then a plan**: what UTDeck should do (pause the game, as Lomiri does for apps; let the game
   pause itself on focus loss; mute; leave Steam awake for its DRM), measured where it can be.

## Made

[`lifecycle-watch.sh`](../../tools/ut-probe/lifecycle-watch.sh) `SECONDS [OUT]`: once a second the
backlight, the lock screen (Lomiri's `com.lomiri.LomiriGreeter.IsActive`, 1 s limit; "lomiri-silent"
if it does not answer), the game's main process (the largest process naming a `.exe`, Wine's
services and Steam's helper left out), its threads' states and CPU use, the GPU's load, the frames
presented in the last second (`game-run.sh`'s frames file), and its sound stream (PulseAudio:
playing or corked). The first version missed programs started without a Windows path (fixed).

## 2. The test program (2026-11-20, 22:13-22:19)

`vktest-x86_64.exe --fullscreen` through `game-run.sh`, started as a separate user service
(`systemd-run --user`, as hidden Steam starts games; not a Lomiri app), boost off, phone upright.
The maintainer: watched, went to another app for about 30 s, came back through the app switcher,
locked the screen for about 90 s, unlocked.

| time | state | frames | GPU |
| --- | --- | --- | --- |
| 22:13:44-22:17:03 | on screen | about 185 fps | 78-80 % |
| 22:17:06-22:17:34 | another app in front | 210-380 fps | 48-70 % |
| 22:17:37 | back | 177 fps | 82 % |
| 22:17:40-22:19:12 | locked, screen off (backlight 0) | 310-340 fps | 50-56 % |
| from 22:19:13 | unlocked | about 205 fps | 79 % |

The maintainer: the picture came back full screen and moving both times; the program showed in
the app switcher, **its animation still running inside the switcher's card**; nothing else odd.

**It never paused.** Behind another app and behind the lock screen it kept drawing, faster than on
screen (frames nobody composes cost less: MAILBOX does not wait for the screen), with the GPU half
busy all the time: battery spent on pictures nobody sees. Lomiri did not stop it (it is not a
Lomiri app), and nothing told it to wait. Coming back works.

## 3. Ys: Memories of Celceta (22:28-22:33)

Our base, the boost on (the default), started from its app drawer entry, the controller connected,
music playing; the same steps.

| time | state | frames | game CPU | GPU | sound |
| --- | --- | --- | --- | --- | --- |
| 22:28:31-22:29:03 | on screen | 30 fps (its cap) | 136-166 % | 39-60 % | playing |
| 22:29:08-22:29:41 | **another app in front** | **10-13 fps** | **24-57 %** | 0-55 % | playing |
| 22:29:46 | back | 30 fps | 155-164 % | 47-49 % | playing |
| 22:29:57-22:31:18 | **locked, screen off** | **30-31 fps** | **138-159 %** | 36-37 % | playing |
| from 22:31:28 | unlocked | 29-30 fps | 141-164 % | 34-56 % | playing |

The maintainer: the music kept playing everywhere; the picture and the controller were fine after
each return.

- **Another app in front: the game slowed itself** to about 10 fps and a third of its CPU, as
  Falcom's games do when they lose focus. So Lomiri's focus change (the window no longer
  activated) reaches the game through Wine's Wayland driver as Windows' "no longer in front". The
  music did not stop (the game does not mute itself).
- **Locked: full speed**, as if in front: 30 fps, 1.5 cores, the GPU a third busy, the music on, for
  the 81 s the screen was off. Locking does not take the focus away from the window, so the game
  gets no signal at all; left locked in a pocket, it costs about what it costs being played.

## 3. Ys IX: Monstrum Nox (22:36-22:41)

Our base, the boost on, the same steps.

| time | state | frames | game CPU | GPU | sound |
| --- | --- | --- | --- | --- | --- |
| 22:36:38-22:38:19 | on screen, and another app in front (about 30 s in this span) | 13-15 fps | 237-357 % | 92-98 % | playing |
| 22:38:26-22:39:23 | **locked, screen off** | **16 fps** | **310-329 %** | **96-98 %** | playing |
| from 22:39:37 | unlocked | 15-16, then 30-31 fps (a lighter spot or a menu) | 333-359 %, then about 75 % | 98, then 62-65 % | playing |

The maintainer: the music kept playing everywhere; picture and controller fine; the game does not
open its pause menu when left and come back to (as a mobile game would); the boost shows little to
no gain here, unlike Goat Simulator. Expected: Ys IX is GPU-bound (98 % busy), and the boost helps
where threads wait on each other on slow cores.

**Ys IX does not react at all**: not to another app in front (no slowdown, unlike Celceta), not to
the lock screen. It keeps about 3 cores and the whole GPU busy wherever the player is.

## 4. Can a game be frozen and woken? (22:43-22:45, Ys IX)

What Lomiri does to its own apps, done by hand: SIGSTOP to the game's process for 20 s while on
screen, then SIGCONT. (A first try froze the wrong process: Steam's launch helper, whose command
line names `ys9.exe`; the game ran on, and freezing the helper was harmless. The game's own
process is the one named `Z:\...\ys9.exe`, as `lifecycle-watch.sh` finds it.)

| | frames | game CPU | GPU | sound |
| --- | --- | --- | --- | --- |
| before | 14 fps | about 3 cores | 98 % | playing |
| frozen (22:44:50-22:45:10, state `T`) | **0** | **0 %** | **0 %** | **corked** (PulseAudio paused the stream by itself) |
| woken (from 22:45:13) | 14 fps | about 3 cores | 98 % | playing |

The maintainer: the picture froze, the music stopped, everything came back fine, the controller
too; Lomiri showed nothing (no "not responding"), and logged nothing about it.

## What this leads to

| finding | |
| --- | --- |
| resume after another app or the lock screen | works (picture, sound, controller), in the test program and both games |
| pause | **none**: Lomiri freezes only its own apps, and a game started by hidden Steam is not one |
| another app in front | Celceta slows itself to 10 fps (it gets Windows' "not in front" through Wine), Ys IX runs flat out; the music plays on in both |
| locked, screen off | full speed in all three (the window keeps its focus); a game in a pocket costs what a played one does |
| freezing the game ourselves | clean: nothing used while frozen, the sound pauses by itself, everything comes back |

**Plan**: UTDeck pauses a game itself, as Lomiri does for apps: SIGSTOP to the game's Windows
processes (not hidden Steam, which stays awake for its DRM; not Wine's server, which other
processes of the prefix need) when the screen locks or another app comes to the front, SIGCONT on
return. Needed:
1. **The lock screen**: Lomiri's `com.lomiri.LomiriGreeter.IsActive` (already read by the watcher).
2. **Another app in front**: the game window's focus. Wine's Wayland driver already knows it (that
   is how Celceta learns it); it can be read from Lomiri too, if Lomiri tells over D-Bus which app
   is in front. To find out.
3. **Which processes**: every process of the game's prefix that runs a `.exe` and is not one of
   Wine's services (a game that starts a second program keeps both frozen together).
4. **To check**: a long freeze (an hour, as a phone in a pocket) and Steam's view of a frozen game;
   games whose clock jumps on waking.

## 5. Pause while locked: made and checked

Lomiri's `com.lomiri.Shell.FocusInfo.isPidFocused(pid)` was the first candidate for "is the game in
front", but it answered false for Ys IX while it was on screen and unlocked: it tells whether a
process belongs to the app in front, and our game belongs to no Lomiri app. The lock screen is
known reliably (`com.lomiri.LomiriGreeter.IsActive`), and it is the larger waste (a game in a
pocket), so that part was built first.

[`utdeck-pause.py`](../../tools/ut-probe/utdeck-pause.py) `PID [LOG]`, started by `game-run.sh`
when `UTDECK_PAUSE=1` (now in `defaults.env`): once a second, when the lock screen is up or the
screen is off, SIGSTOP to every process of the prefix (`WINEPREFIX` in its environment) that runs
a Windows program, Wine's services and Steam's `steam.exe` helper left out; SIGCONT on unlock;
each change in the game's log; if the helper ends while the game is frozen, it wakes it first.
Hidden Steam, Wine's server and Wine's services keep running. It fills in the session bus address
if Steam's environment lacks it.

Two faults on the way, both found by trying it:
- The first manual freeze hit Steam's launch helper (a shell whose command line names the
  `.exe`), not the game: the game played on.
- The helper's first version took the program's name as the first word of the command line; Ys
  IX's folder is "Ys IX Monstrum Nox", so it saw `Z:\...\common\Ys`, found no game, froze nothing,
  and the music played on while locked (the maintainer). Now the name is everything up to the first
  `.exe`, and Linux programs naming one (shells, Steam's wrapper) are left out.

| | Ys IX (helper attached by hand, 22:51) | Ys: Memories of Celceta (from the app drawer, defaults, 22:55) |
| --- | --- | --- |
| frozen | 22:51:30, at the lock | 22:55:35, at the lock |
| while locked | game CPU 0 %, GPU 0 %, 0 frames, sound corked | GPU 0 % at its lowest clock (266 MHz); the phone's whole CPU about 22 % (hidden Steam and the system) |
| woken | 22:52:13, at unlock | 22:56:53, at unlock |
| the maintainer | music stopped when locked, everything came back fine | music stopped when locked, everything came back fine |

**3.6's exit is met**: pause and resume work in two games (the lock screen). Still open, with a
plan:

1. **Another app in front** pauses nothing (Celceta slows itself, Ys IX runs flat out, music on).
   The focus is known inside Wine's Wayland driver (the `activated` state of the window's
   `xdg_toplevel` configure, and keyboard enter/leave): a small Wine patch on both bases can write
   it to a file named by an environment variable (for example `UTDECK_FOCUS_FILE`), and
   `utdeck-pause.py` can freeze on "not in front" as on "locked". The app switcher's card then shows
   a still picture instead of a running one.
2. **A long freeze** (an hour in a pocket) and Steam's view of a frozen game: to check.
3. **Games whose clock jumps on waking** (physics, timers): none seen in the two games.

## 6. "Another app in front": three ways, B tried first

| | how | for | against |
| --- | --- | --- | --- |
| A | a Wine patch writes the window's focus to a file; the helper freezes the game and wakes it for a moment every few seconds so it can read Lomiri's "back in front" | self-contained | a frozen game cannot see the event, hence the peeks: some sound and battery unless muted, up to a few seconds' delay on return |
| B | make the game a Lomiri app, so that Lomiri pauses and wakes it as its own apps | Lomiri's own mechanism; might also fix K1 and `isPidFocused` | an experiment |
| C | Wine suspends the game's own threads on focus loss and resumes them on return | instant return | inside Wine; and the event reader is itself one of the process's Windows threads (`CreateThread` in `winewayland.drv/dllmain.c`): a thread suspended while holding a lock the reader needs would leave the game asleep for good |

The maintainer chose B first.

**B, tried.** A test app, "UTDeck lifecycle test" (`utdeck-lctest.desktop`, running `vktest`
fullscreen through `game-run.sh`, our own pause off), started by Lomiri: its unit is
`lomiri-app-launch--application-legacy--utdeck-lctest--.service`, the test program runs inside it
and has `APP_ID=utdeck-lctest` in its environment. Started first with `lomiri-app-launch` over adb
(the tool crashed after starting it: "Lost our connection with the registry"), then from the app
drawer by the maintainer. Both times: **never paused** (no thread ever stopped; 330-355 fps behind
another app, about 250 fps locked); the switcher's card kept moving; the picture came back fine.
Lomiri's log shows why: the sessions of Wine's Wayland connection have **no name**
(`TaskController::onSessionStarting - sessionName=`), then "Received a surface count changed event
from an app that's not in the Launcher model, creating icon": Lomiri does not tie Wine's window to
the app, so it has nothing to pause. Changing that would mean changing Lomiri (a system component,
off limits). **B does not work.**

**So A** (C's lock risk is real; A freezes the whole process, so nothing is left half-held): a Wine
patch on both bases writes `1` or `0` to `UTDECK_FOCUS_FILE` when the window gains or loses the
keyboard focus (`keyboard_handle_enter`/`_leave`, which is how Celceta learns it); `utdeck-pause.py`
freezes and mutes the game when it is not in front, wakes it for 0.3 s every 1.5 s so that it can
read Lomiri's "back in front", and wakes and unmutes it on return.

## 7. A, made and checked (2026-11-20, 23:33-23:38)

- **Wine patch 0015** on both bases (`patches/0015-...`, `patches-proton/0015-...`; Proton's
  0014 is its own video fix, so both series use 0015 for this): `keyboard_handle_enter`/`_leave`
  write `1`/`0` to `UTDECK_FOCUS_FILE`; nothing changes without the variable. Runtimes rebuilt
  (`11.16-20261121`, `proton11.0-20261121`, named by UTC date) and installed; the previous ones
  stay. The published GitHub pre-release (`proton11.0-20261120`) does not have it.
- `game-run.sh` names the file per game (`wine/games/<label>-<time>.focus`). Checked with the test
  program: Wine wrote `1` when its window opened.
- `utdeck-pause.py`: "not in front" (the file says `0` for a second) freezes the game and mutes
  its sound streams (`pactl set-sink-input-mute`, the streams found by the game's process ids);
  every 1.5 s it wakes the game for 0.3 s so that Wine can read Lomiri's "back in front", and
  wakes and unmutes it once the file says `1`. No file (an older Wine) counts as in front. Locked:
  frozen as before, no peeks.

Ys: Memories of Celceta from the app drawer, our base with the defaults:

| time | event (the game's log) | game | CPU | frames | sound |
| --- | --- | --- | --- | --- | --- |
| 23:35:19-30 | playing | running | 85-118 % | 44 fps | playing |
| 23:35:34 | **another app: frozen, 1 stream muted** | frozen (39 threads `T`) | 7-18 % (the peeks) | 1-14 fps (the peeks) | muted |
| 23:35:51 | **back in front: woken** | running | 56-152 % | 44-61 fps | playing |
| 23:36:19 | **locked: frozen** | frozen | 0 % | 0 | corked |
| 23:36:59 | **unlocked: woken** | running | 104-118 % | 43 fps | playing |

The maintainer: the music stopped in the other app and while locked, everything came back fine.
Behind another app the game now uses about a tenth of its CPU (the peeks, silent), behind the lock
screen nothing.

**3.6 done.** Proton's runtime with 0015 published (the maintainer's decision) as the
pre-release [`runtime-proton11.0-20261121`](https://github.com/alpha-liu-01/UTDeck/releases/tag/runtime-proton11.0-20261121),
tagged on commit 51c3be5 (the patches it is built from); `runtimes.list` points at it, and the
download was checked against its SHA-256. Still open: a long freeze (an hour in a pocket) and
Steam's view of it.

## Events

- 2026-11-20: 3.6 started; what is known written down; plan.
- 2026-11-20: `lifecycle-watch.sh` made; the test program: never paused behind another app or the
  lock screen (up to 380 fps, GPU half busy), came back fine both times.
- 2026-11-20: Celceta: slows itself to 10 fps behind another app (it gets the focus change), full
  speed behind the lock screen (no signal); music on throughout; picture and controller fine.
- 2026-11-20: Ys IX: full speed behind another app and the lock screen (3 cores, GPU 98 %), music
  on; picture and controller fine; no pause menu of its own.
- 2026-11-20: Ys IX frozen for 20 s and woken: clean (picture, music, controller back; Lomiri
  silent). Plan: UTDeck pauses the game itself on the lock screen and when another app is in front.
- 2026-11-20: `utdeck-pause.py` made and checked (Ys IX by hand, Celceta from the app drawer with
  the defaults): frozen at the lock, woken at unlock, music stopped, everything came back. **3.6's
  exit met**; "another app in front" planned (a Wine patch exposing the window's focus).
- 2026-11-20: B (the game as a Lomiri app) tried twice: Lomiri does not tie Wine's window to the app
  (sessions without a name) and never pauses it. A chosen: a Wine focus patch plus freeze, mute and
  short peeks.
- 2026-11-20: Wine patch 0015 (focus to `UTDECK_FOCUS_FILE`) on both bases, runtimes rebuilt; the
  pause helper pauses behind another app too (frozen, muted, short peeks): checked in Celceta.
  **3.6 done.**
