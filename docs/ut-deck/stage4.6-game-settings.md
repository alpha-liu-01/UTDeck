# Stage 4.6: per-game settings, the plan

Written 2026-11-23, when 4.5 closed; started and done the same day (see "Done" at the end).

Goal ([stage4-roadmap.md](stage4-roadmap.md)): a settings page per game, Steam's and the user's own,
for how UTDeck runs it. Everything on it exists today as a line in the game's `utdeck-env` file,
written by hand over adb; the page makes it reachable without a PC.

Exit: each setting changes what the game gets, checked in its logs, on a Steam game and one of the
user's own; the last run's log readable on the phone.

## Terms used here

- *`utdeck-env`* = a game's own settings file, one `NAME=value` per line, read at each start:
  `steamapps/compatdata/<appid>/utdeck-env` for a Steam game (by `utdeck-compat`), `wine/own/<id>/utdeck-env`
  for the user's own (by `own-run.sh`). A value there wins over `defaults.env`.
- *Wine base* = which Wine runs the game: the default runtime (Hangover's Wine 11.16) or Proton 11's
  (stage 3.3). Each has its own prefix, so a game's saves inside the prefix do not move across.
- *Box64 / FEX* = the two translators for 32-bit x86 code (`HODLL`); 64-bit code always runs on FEX.

## What exists

| Setting | Line in `utdeck-env` | Read by | Default |
| --- | --- | --- | --- |
| Wine base | `UTDECK_RUNTIME=proton` | `utdeck-compat`, `own-run.sh` (each picks `pfx` or `pfx-proton`) | the default runtime |
| Launch options | `GAME_ARGS=...` (stage 3.9) | `utdeck-compat` (not for install scripts); for the user's games `own-run.sh` from 4.6 on | none |
| Resolution | `UTDECK_RESOLUTION=WxH` (patch 0008's screen-shaped modes) | `utdeck-compat` only (`setmode.exe`) | native |
| 32-bit translator | `HODLL=libwow64fex.dll` | Wine | Box64 |
| CPU boost | `UTDECK_BOOST=0` | `game-run.sh` | on (1024) |
| Pause when locked or behind another app | `UTDECK_PAUSE=0` | `game-run.sh` | on |
| Fast x87 maths | `FEX_X87REDUCEDPRECISION=0` | FEX | on (4.5) |
| Fill the screen with a smaller window | `UTDECK_FILL=0` | Wine (patch 0010) | on |
| Wine log level, DLL choices, more variables | `WINEDEBUG_GAME`, `GAME_OVERRIDES`, `GAME_ENV` | `game-run.sh` | errors only |

Logs: each run writes `wine/games/<label>-<time>.log` (Wine's errors and the game's output), `.frames`
and `.sensors`; Steam's runs also `compatdata/<appid>/utdeck-compat.log`. The app already reads them for
4.3's errors in words.

Proton's base is downloaded on request (`get-runtime.sh proton`, a published release, checksummed).

## How it would work

- **Reaching it**: a long press on a Steam game's card opens its settings; for the user's own games,
  their page (4.5) gets a "Settings" entry to the same page. Changes apply at the game's next start; the
  page says so while the game runs.
- **The page**, each item with one line of what it does:
  - *Wine base*: Default or Proton 11. Choosing Proton when it is not installed offers its download
    (size and progress shown). Said plainly: "a separate Windows folder: saved games stay with the
    other base".
  - *Launch options*: a text field (`GAME_ARGS`).
  - *Resolution*: Native, or one of the screen-shaped modes (1200x540, 1600x720, 1920x864, 2000x900),
    "for a game that only follows the desktop's size".
  - *32-bit code*: Box64 or FEX, "only for 32-bit games; try FEX if a 32-bit game does not start".
  - Switches: CPU boost, pause when not in front, fast x87 maths, fill the screen.
  - *Advanced*: the remaining lines as text (`GAME_OVERRIDES=...`, `WINEDEBUG_GAME=...`, anything
    hand-written), kept as they are.
  - *Reset to the defaults*: an empty `utdeck-env`.
  - *The last run's log*: the run's summary (how long, exit code, the problem in words, as 4.3), and
    the log's last few hundred lines, scrollable; Steam's `utdeck-compat.log` too for a Steam game.
- **Writing `utdeck-env`**: only the page's own lines are rewritten; comments and any other line stay
  where they were, so hand-written settings survive. Written to a new file and renamed over the old.
- **Resolution for the user's own games**: today only `utdeck-compat` applies `UTDECK_RESOLUTION`
  (`setmode.exe` when the setting changed). The same step moves into `own-run.sh` (or `game-run.sh`), so
  the setting works for both kinds.

## Moved to 4.7

**Forcing the Windows build** of a Steam game that also has a Linux build (stage 3.7's rule): it
helps only before Steam downloads the game (once the Linux build is installed, it means a new download
of all of it), and writing Steam's tool mapping needs Steam stopped (`set-default.py --app`). So it
belongs to installing a game from the app, 4.7.

## Steps

1. **`GameSettings`** (C++): read a game's `utdeck-env` into the known settings and the rest; write it
   back keeping the rest; Proton's base installed or not, and its download; the last run's log.
2. **The page**, and the long press on Steam's cards, and "Settings" on the user's games' page.
3. **Resolution for the user's games** (`setmode.exe` in their start).
4. **Checked in logs**, one setting at a time: a Steam game (Ys: Memories of Celceta, 32-bit: the
   translator and the resolution; Ys IX: the Wine base) and wiliwili (launch options, boost, pause),
   each change seen in the next run's log (`per-game: ...` lines, `game arguments added`, the runtime
   and prefix, the boost and pause lines).

Test device: stone.

## Answers from the maintainer (2026-11-23)

- Forcing the Windows build: moved to 4.7.
- The "Advanced" text field: on the phone.

## Steps 1-3: built (2026-11-23, UTDeck 0.3.26)

- **`GameSettings`** (`app/src/GameSettings.*`, `gameSettings` in QML): `load()` reads the game's
  `utdeck-env` into the page's settings, each switch's default taken from `defaults.env` (so the page
  stays right if a default changes); `save()` writes a setting only when it differs from its default,
  replaces its line where it was, keeps comments and blank lines, and puts the Advanced lines (any
  other `NAME=value`, the page's own names left out) at the end; `reset()`; Proton's base installed or
  not and its download (`get-runtime.sh proton`); `lastRun()`: the newest run of the game itself (not
  an install script), its summary (start, exit code, the problem in words as 4.3's errors) and its last
  300 lines, Steam's `utdeck-compat.log` too for a Steam game; terminal colours stripped and numbers of 9
  digits or more masked (Steam's logs can hold the account's).
- **The page** (`app/qml/GameSettingsPage.qml`): Wine base (with the Proton download when it is
  missing), launch options, resolution (native and patch 0008's four screen-shaped modes), 32-bit code
  (Box64 or FEX), the four switches, Advanced, Save, Reset to the defaults, and "Show the last run".
  Reached by a long press on a Steam game's card, and by "Settings" on the user's own game's page.
  Lomiri's option selectors get their index once the page is complete (their model resets it, as the
  Dolphin port found); the switch rows are made again after loading, since a tapped switch drops its
  binding.
- **Resolution for the user's games**: `own-run.sh` runs `setmode.exe` (from UTDeck's compatibility
  tool) when `UTDECK_RESOLUTION` changed, as `utdeck-compat` does. Both now keep what was set inside
  the prefix (`<prefix>/utdeck-resolution`, before: in the game's data folder, shared by both bases),
  so a game moved to the other base gets its mode there too.
- On stone: Proton's base is installed; Ys IX and Celceta already have Proton prefixes (stage 3.3), so
  switching their base costs no space (stone: 2.1 GB free).

## First test round (2026-11-23): two bugs, one misunderstanding (UTDeck 0.3.27)

- **Celceta** (32-bit FEX, 1600x720): both settings reached it. Its start log has
  `per-game: UTDECK_RESOLUTION=1600x720`, `per-game: HODLL=libwow64fex.dll` and
  `setmode: 1600x720 @120 Hz: set and stored (0)`, and its game log no Box64 banner (FEX ran its
  32-bit code). The picture did not change because Celceta takes its resolution from its own
  launcher's settings, not the desktop's: the setting is only for games that follow the desktop. The
  page now says so ("Most games set their own resolution ... this changes nothing for them").
- **Ys IX on Proton's base**: "Asking Steam to start the game" for minutes, UTDeck could not be swiped
  away and ended by itself a few minutes later, then Ys IX came up as a window of its own (no app id,
  no landscape lock). The timeline (journal, Steam's launch lines, the logs):
  - 01:32:46 Celceta ended; with Steam's switch off, UTDeck stopped Steam at once. The maintainer had
    just tapped Ys IX (its launch 01:32:47), so Steam was shut down under it and started again
    (01:33:06).
  - 01:34:37-01:37:13: Steam ran Ys IX's install step (`iscriptevaluator`) in the Proton prefix, which
    had not had it yet (2.5 minutes, part of the first start on the other base).
  - Meanwhile the swipe: the install step's Wine processes carried UTDeck's app id and
    `DESKTOP_FILE_HINT`, so Lomiri counted them as the app's and waited for them to close, which
    windowless processes never do; it killed the app at 01:35:02. Ys IX started at 01:37:16 with
    UTDeck gone, so `game-run.sh` gave it no app id.
- **Fixes**: Steam's stop after a game (switch off) waits 45 s and is called off when a game starts
  (`Setup::gameEnded` and the new `gameStarting`, called on a tap, on "Try again", on the restart after
  an agreement, and when a game runs); `game-run.sh` gives no app id and no `DESKTOP_FILE_HINT` to
  Steam's install scripts and own runs (`UTDECK_SETUP_RUN`); the page says the first start on the other
  base takes a few minutes.

## Second test round (2026-11-23)

- **Ys IX on Proton's base** started fine from UTDeck (as part of the app, `app: utdeck.alphaliu01_utdeck`):
  its start log has `per-game: UTDECK_RUNTIME=proton`, `UTDECK_BOOST=0`, `UTDECK_PAUSE=0` and
  `runtime: proton, prefix pfx-proton`; no boost or pause lines in its game log.
- **wiliwili** (launch options `--utdeck-test`, resolution 1200x540): the resolution reached the prefix
  (`own-run.log`: `resolution 1200x540: setmode: 1200x540 @120 Hz: set and stored (0)`), but wiliwili
  drew at 2400x1081 as before: it sizes itself from its window, which Lomiri sizes to the screen, not
  from the desktop's mode; like Celceta, not a program this setting is for.
- **The launch options were not passed** to the user's games: only `utdeck-compat` added `GAME_ARGS`
  (the plan's table had it wrong, `game-run.sh` never read it). `own-run.sh` now adds them the same way
  and logs "game arguments added".
- Checked again: wiliwili's start had `wiliwili.exe --utdeck-test` (and "game arguments added" in
  `own-run.log`); the page's "Show the last run" showed it. All three games reset to the defaults from
  the page: their `utdeck-env` files empty (the prefixes with a smaller mode go back to native at their
  next start, as the mode they have no longer matches). UTDeck 0.3.28 carries the fixed `own-run.sh`.

## Done (2026-11-23)

The exit, on stone (the maintainer), each change seen in the next run's logs:

| Setting | Game | Seen |
| --- | --- | --- |
| 32-bit code: FEX | Celceta (Steam) | `per-game: HODLL=libwow64fex.dll`; no Box64 banner in the game's log |
| Resolution 1600x720, 1200x540 | Celceta, wiliwili | `setmode: ... set and stored` (the programs size themselves otherwise, as the page now says) |
| Wine base: Proton 11 | Ys IX | `runtime: proton, prefix pfx-proton`; the game ran |
| CPU boost and pause off | Ys IX | `per-game: UTDECK_BOOST=0`, `UTDECK_PAUSE=0`; no boost or pause lines |
| Launch options | wiliwili (own) | `wiliwili.exe --utdeck-test` (after the fix) |
| Reset to the defaults | all three | empty `utdeck-env` |
| The last run's log | wiliwili | shown on the page |

Fixed on the way: Steam stopped under the next game's launch (now 45 s later, called off by a start);
Steam's windowless install steps held UTDeck's card (no longer joined to the app); launch options
for the user's games.

Not seen in a test: the Proton download (stone has it), the fast x87 and fill switches (on by default;
written and read like the others), the Advanced field with a real line.
