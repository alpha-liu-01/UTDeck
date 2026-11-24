# Stage 4.9: uninstalling UTDeck, the plan

Written 2026-11-23, when 4.8 closed. The maintainer's choices (2026-11-23): uninstalling UTDeck becomes
4.9, before the game round and stage 4's exit run (now 4.10), since that exit run starts from a fresh
install and a phone that has had UTDeck cannot get back to one today; **the Steam games and the saves are
kept by default** (deleting them is ticked by the user); **the Nothing Phone (1) is the test device** for
the uninstall and for the exit run.

Goal: removing the click leaves almost everything UTDeck put on the phone (on stone: 26 GB of scripts and
runtimes, 57 GB of Steam with its games, the app drawer entries of the games). UTDeck gets an "Uninstall
UTDeck" in its settings that stops what runs and removes everything it put in the home folder, except
what the user keeps; afterwards the phone is as before UTDeck, plus the kept games and saves, which a new
install finds again.

Exit: on the Nothing Phone (1), from the app: the uninstall with the defaults (games and saves kept);
then a scan of the home folder finds nothing of UTDeck but what was kept; the app itself removed and
installed again; the setup run; the kept Steam games are in the library again without a download, a save
loads in one of them, and Steam is signed in again by QR code.

## Terms used here

- *Plan mode* (a dry run) = the uninstall lists what it would remove or keep, with sizes, and removes
  nothing. The app shows this list before the user confirms.
- *Transient unit* = a background job UTDeck starts with `systemd-run --user` (hidden Steam, its screen,
  a game, the keep-awake helper); it lives until stopped or until the phone restarts.
- *compatdata* = Steam's folder of each game's Windows folder (Wine prefix) and UTDeck's settings for it
  (`steamapps/compatdata/<appid>`); the saves of most games are in it.
- *userdata* = Steam's per-account folder: game settings Steam keeps, and the saves of games that use
  Steam Cloud through Steam's own interface (`userdata/<account>/<appid>/remote`).

## What UTDeck puts on a phone

From the scripts and from stone (2026-11-23). Setup's rule since 4.2: nothing outside the home folder
(checked again in step 1).

| Where | What | By default |
| --- | --- | --- |
| `~/utdeck-probe` | UTDeck's scripts, the Wine runtimes (default and Proton's), Xvfb (the hidden screen), the FEX files, logs | removed |
| `~/utdeck-probe/wine/own/<id>` | the user's own games: each one's Windows folder, with its saves, and an installed game itself | kept (games and saves) |
| `~/Documents/UTDeck/Games` | the game folders the user copied | kept (games) |
| `~/.local/share/Steam`: the client, its runtime, `appcache`, `depotcache`, `logs`, `compatibilitytools.d/utdeck` | Valve's client and UTDeck's tool for it | removed |
| `~/.local/share/Steam/config`, `~/.steam` | Steam's sign-in (its token), Steam's links | **always removed** (the account's data) |
| `~/.local/share/Steam/steamapps`: `common`, `appmanifest_*.acf`, `shadercache` | the installed games, and what Steam knows of them | kept (games) |
| `~/.local/share/Steam/steamapps/compatdata` | each Steam game's Windows folder: saves, UTDeck's settings for the game | kept (saves) |
| `~/.local/share/Steam/userdata` | Steam's game settings and Steam Cloud saves of some games | kept (saves) |
| `~/.local/share/applications/utdeck-*.desktop` | the games' app drawer entries (and, on stone, development entries) | removed |
| Wine's menu files (`desktop-directories/wine-*`, `applications/wine/`) | left from before Wine's menu maker was turned off (stage 3) | removed |
| `~/.cache/utdeck`, `~/.cache/utdeck.alphaliu01`, `~/.config/utdeck.alphaliu01`, `~/.local/share/utdeck.alphaliu01` | the app's own cache and settings | removed |
| `/run/user/<uid>/utdeck` | the app's live state (in memory) | removed |
| transient units `utdeck-steam`, `utdeck-xvfb`, `utdeck-awake`, `utdeck-play-*`, `utdeck-install-*`, `utdeck-setup` | what runs in the background | stopped |
| `~/.cache/mesa_shader_cache` | the GPU driver's cache: possibly shared with other apps that use Mesa (step 1) | left alone unless only UTDeck uses it |

The user's ticks:
- **"Also delete the Steam games"**: `steamapps/common`, the manifests, `shadercache`, half-finished
  downloads, and the user's own games (their Windows folders and copied folders).
- **"Also delete saved games and game settings"**: `compatdata`, `userdata`, and the own games' Windows
  folders. With the games kept and the saves deleted, an own game installed into its Windows folder
  goes too (the game lives in it): said on the page before it is done.

## How it would work

- **`utdeck-uninstall.sh plan|run [--delete-games] [--delete-saves]`**, carried in the click like the
  setup's scripts. `plan` prints one line per item (what, where, size, removed or kept); `run` does it
  and writes each step's state for the app, as `utdeck-setup.sh` does. It runs as a transient unit
  (`utdeck-uninstall`), so Lomiri pausing or closing the app does not stop it halfway, and from a copy in
  `$XDG_RUNTIME_DIR`, since it removes its own folder.
- **The order**: refused while a game runs; Steam stopped through `steam-service.sh stop`, which waits
  for a cloud upload in progress (4.7 step 9: the last saves reach Steam Cloud); the other units
  stopped; then the removal, the kept folders left in place.
- **Kept means left where it was**, so a new install finds it: Steam lists the games of the
  `appmanifest_*.acf` files it finds; the setup's `tool` step sets UTDeck's tool as Steam's default
  again (the sign-in and the mappings went with `config`); each game's UTDeck settings
  (`compatdata/<appid>/utdeck-env`) are kept with the saves.
- **No account data**: the sign-in is always removed; the plan's list masks account numbers in paths
  (as every Steam listing here); nothing is read from Steam's files.
- **The app page**: Settings, "Uninstall UTDeck": what is removed and what is kept, with sizes, the two
  ticks, an Uninstall button (red) behind a confirmation, the progress, and at the end what was kept
  (where, how big) and how to remove the app itself. Usable with a controller (4.8).
- **The app itself**: removed by Lomiri or the OpenStore, or by the app at the end if Ubuntu Touch lets
  a click remove itself (step 1).

## Steps

1. **Find out, read-only**: the footprint on the Nothing Phone (sizes, every path above, anything
   missing from the table); a scan for files of the user outside the home folder; whose
   `mesa_shader_cache` is (if only ours: the own games' cache moved into their own folders, so a
   shared cache is never UTDeck's); how a click is removed on 24.04-2.x and whether its own folders
   (`~/.local/share/<click>`, `~/.config/<click>`, `~/.cache/<click>`) go with it.
2. **`utdeck-uninstall.sh`**: `plan` checked on stone and on the Nothing Phone; `run` tested only on a
   made-up home folder (a copy of the layout with small files, `HOME` pointed at it): **never on stone**,
   whose home holds the development data. Also checked there: the setup's Steam step installs the
   client into a Steam folder that still holds the kept `steamapps`, and Steam lists those games again.
3. **The app page**, with the plan's list and the progress; the click carries the script.
4. **The run on the Nothing Phone**, with the maintainer's OK at that moment (it deletes files):
   with the defaults; the home folder scanned afterwards; the app installed again, the setup run, the
   kept games and a save checked.

## Questions for the maintainer

- **The exit run's "fresh install"** (4.10): after this uninstall the Nothing Phone keeps its games, so
  the setup finds them installed. Is that the start you want for the exit run (it also tests the kept
  games), or a second uninstall with the games and saves deleted first (the games downloaded again)?

## Answers from the maintainer (2026-11-23)

- **The exit run starts with the kept games**: no second uninstall.

## Before 4.10's exit run (not part of 4.9)

The Nothing Phone has not had 4.4-4.8 yet, and a fresh install downloads the published runtime, which
does not have patch 0020 yet: the runtime release (a GitHub release, with the maintainer's OK) comes
before the exit run.

## Step 1: found out, on the Nothing Phone (1) (2026-11-23, read-only)

The phone: 24.04-2.x, UTDeck 0.3.15 (4.4-4.8 not rolled out yet), 73 GB free of 105 GB.

| Where | Size | Notes |
| --- | --- | --- |
| `~/utdeck-probe` | 3.6 GB | the default runtime twice (`runtime-11.16-20261121` and `-20261122`, 1.2 GB each: `get-runtime.sh` keeps the old one after an update), `wine/prefix-games` (1.4 GB, a Wine folder from stage 3's tests), Xvfb 2.3 MB, scripts, logs; no own games |
| `~/.local/share/Steam` | 22 GB | `steamapps` 18 GB (`common` 14 GB, six games; `compatdata` 4.8 GB; `shadercache` 20 MB; `workshop` 868 KB); the client and its runtimes about 4 GB; `config` 119 MB |
| `~/.steam` | 20 KB | links into the Steam folder, Steam's pid, pipe and token |
| `~/.cache/utdeck`, `~/.cache/utdeck.alphaliu01`, `~/.config/utdeck.alphaliu01` | 0.9 MB | the app's |
| `~/.local/share/applications` | | `utdeck-game-<appid>.desktop` (2), development entries `utdeck-gltest-portrait.desktop`, `utdeck-vktest.desktop`: all `utdeck-*.desktop`; the click's own entry (`utdeck.alphaliu01_utdeck_*.desktop`) belongs to the click. No Steam shortcuts, no Wine menu files, no Steam icons |
| `~/.cache/mesa_shader_cache` | 5.2 MB | **left alone**: written 2026-11-21/22 by games that ran before 4.7 gave Steam games their own cache folder; the user's own games still write here (`own-run.sh` does not set `MESA_SHADER_CACHE_DIR`), and the Dolphin port, also on this phone, may use it. Step 2 gives the own games their own cache folder, so this cache is never UTDeck's from then on |
| `/tmp` (in RAM) | 6 MB | Steam's (`steam*`, `.com.valvesoftware.Steam.*`, Chromium's `steam_chrome_shmem_*`), Xvfb's `.X50-lock` and `.X11-unix/X50`, and files pushed during stage 3-4 tests |
| `/dev/shm` (in RAM) | **328 MB** | 29 shared memory files of Steam (`u32011-Shm_*`, about 26 MB each, and `ValveIPCSharedObj-Steam`): **16 of them (272 MB) used by no running process** (checked against every process's mapped files and open files, Steam running) |
| elsewhere | none | `/` is read-only; nothing of the user outside `/home`, `/tmp`, `/run` and `/dev/shm`; `/var/tmp` empty |

- **Removing a click**: Lomiri's app drawer offers no uninstall; the OpenStore does. The click service
  (`com.lomiri.click`, system bus, run as root) has `Install(path)` (which `install-click.sh` uses) and
  `Remove(id)`: the app can remove itself at the end (how `Remove` is called, and that it works from the
  app, checked in step 3). Clicks are registered for every user (`users/@all`, owned by root), so
  `click unregister` as the user is not a way. The app's own folders (`~/.cache/<click>`,
  `~/.config/<click>`) are the user's: the uninstall removes them after the app has closed (the app
  could write its settings again while it closes).
- **So the uninstall also**: removes Steam's `/tmp` and `/dev/shm` leftovers and Xvfb's lock files once
  Steam and Xvfb have stopped (they live in RAM, so a restart also clears them, but the uninstall frees
  that memory at once), and every `utdeck-*.desktop`.

Found on the way, **not uninstall matters** (parked, CLAUDE.md's one thing at a time):
- **Steam leaves its shared memory behind**: 272 MB of RAM held by files nobody uses, on a phone up since
  2026-11-21; each Steam start (with Steam's switch off, one per game) probably adds more. A stage 5 item
  (memory), and likely a small fix in `steam-service.sh stop` (remove the files no process uses after
  Steam has exited). To check on stone too (disconnected at the time).
- **`get-runtime.sh` keeps the old runtime after an update** (1.2 GB here). A stage 5 item (other
  losses), or with the runtime release before 4.10.

## Step 2: the uninstall script (2026-11-23)

`tools/ut-probe/utdeck-uninstall.sh plan|run [--delete-games] [--delete-saves] [--wait-app PID]`, carried
in the click next to the setup (`setup/utdeck-uninstall.sh`, so it does not remove its own copy).

- **plan** prints one line per kind of item, tab-separated: `remove|keep`, KB, the kind (`utdeck`,
  `own`, `steamclient`, `signin`, `games`, `saves`, `copied`, `entries`, `appdata`, `memory`, worded by
  the app), the first paths. Steam's `userdata` is named as a whole, so no account number appears.
- **run**: steps `stop steam utdeck entries memory appdata`, each with its state in
  `$XDG_RUNTIME_DIR/utdeck-uninstall/STEP` as the setup does. `stop` refuses while a game runs, stops
  Steam through `steam-service.sh stop` (which waits for a Steam Cloud upload in progress), then every
  `utdeck-*` unit, then any Steam process or Xvfb left. `memory` (Steam's and Xvfb's files in `/tmp`
  and `/dev/shm`, and `$XDG_RUNTIME_DIR/utdeck`) only once nothing of Steam or Xvfb runs. `appdata`
  after the app has closed (`--wait-app`).
- **Safety**: with `HOME` anywhere but the user's real home, nothing is stopped and nothing outside
  `HOME` is touched (the tests); a home with `utdeck-probe/DEVELOPMENT` is refused (exit 3) for `run`.
  **stone gets that marker at its next connection, before an app with the uninstall page is installed
  there.**
- Own games: their GPU cache now goes into their own folder (`wine/own/<id>/shadercache`, `own-run.sh`),
  as Steam's games have had since 4.7.

Tested on the PC with a made-up home (UTDeck's layout with small files, plus files that are not
UTDeck's: a note in Documents, another app's entry and cache, the shared GPU cache), each of the four
choices:

| choice | left afterwards |
| --- | --- |
| defaults | the Steam games (`common`, manifest, `shadercache`, `libraryfolders.vdf`), `compatdata`, `userdata`, the own game's folder, the copied game folder |
| `--delete-games` | `compatdata`, `userdata`, the own game's folder with `game.conf` set aside as `game.conf.removed` |
| `--delete-saves` | the Steam games and the copied game folder |
| both | nothing of UTDeck |

In each, the files that are not UTDeck's stayed, and so did the click's own app drawer entry (the
click's, removed with it). The `DEVELOPMENT` marker stopped `run` with nothing removed; `--wait-app`
waited for the process given and removed the app's folders after it.

`plan` on the Nothing Phone (1), 0.7 s:

| | MB |
| --- | --- |
| removed: UTDeck's folder (`utdeck-probe`) | 3674 |
| removed: Steam's client and runtimes | 4038 |
| removed: Steam's sign-in | 118 |
| removed: app drawer entries (4), the app's cache and settings | 1 |
| removed: Steam's leftovers in RAM (`/tmp`, `/dev/shm`) | 327 |
| kept: the Steam games (six) | 13364 |
| kept: saves and game settings | 4885 |

With both ticks, everything is removed (26.8 GB). Not tried on the phone: `run` (step 4, with the
maintainer's OK), and Steam's setup over a kept `steamapps` (its install script fills only the folders
it lacks, `mkdir -p` and a check per folder; seen for real in step 4).

## Step 3: the app page (UTDeck 0.3.49)

- **Settings, "Uninstall UTDeck"** (`UninstallPage.qml`, `Uninstall.{h,cpp}`): what is removed and what
  is kept, each kind with its size, from the script's plan (read again when a tick changes); the ticks
  "Also delete the Steam games and the game folders you copied", "Also delete saved games and game
  settings" (off: the maintainer's default), "Also remove this app when it closes" (on); "Uninstall...",
  then "N GB is deleted, and this cannot be undone" with "Uninstall now". Not while a game runs; on a
  phone with the `DEVELOPMENT` marker the page says so and the button stays off.
- **While it runs**: the steps with their state, as the setup page shows its own; back (B, the header)
  is off, since the other pages run on what is being removed. A failed step (a game started meanwhile)
  shows why and allows going back and trying again.
- **At the end**: what was kept and where; "Close UTDeck" quits the app; the unit, waiting for the app's
  process to end (`--wait-app`), then removes the app's settings and cache and, with the tick, **the app
  itself**: `utdeck-uninstall.sh --remove-app` calls the click service's `Remove` with the package name
  (`utdeck.alphaliu01`). Read from the click service (`/usr/libexec/click/click-service`, its symbols and
  messages): it takes the caller's user from D-Bus, reads the package's version for that user and for
  all users, and removes it (`click_user_remove`, `click_db_maybe_remove`), as the OpenStore's removal.
  Done by the unit and not the app, so the app is not removed while it runs. Not tried yet (it removes
  the app): step 4.
- The script got `--remove-app` (step "app") and, on the development phone, a refusal the page can show
  (plan's first line `refuse 0 development ...`; `run` writes the "stop" step as failed). Both checked in
  the test home.

**On the Nothing Phone (1)** (the maintainer's OK, 2026-11-23): 0.3.49 installed over 0.3.15, which is
also that phone's rollout of 4.4-4.8. At the app's start the setup's files step copied the new scripts
(`.files-version` 0.3.49.2x) and ran `steam-tools.sh --restart` (19:01:46): Steam stopped, the old second
tool removed, each game's Windows user folder laid out as the shared one (1351630: the old user folder
merged in, its duplicate copies set aside in `utdeck-saves-20261123/`; 3654560: one phone save that the
cloud also has set aside in `utdeck-saves-20261123/cloud-wins/`), Steam started again with `-noshaders`
and the downloads keep-awake. Steam's default for Windows games (app 0) is `utdeck-proton`. No QML errors
at start. The runtime is still `runtime-11.16-20261122` (patch 0020 comes with the runtime release, before
4.10). A listing of the home folder (names only, account numbers masked) was kept on the PC, to compare
with after the uninstall.

## Step 4: the real uninstall on the Nothing Phone (1) (2026-11-23)

Run by the maintainer from the page, with the defaults (games and saves kept, "Also remove this app"
ticked), then "Close UTDeck". The unit's log (`journalctl --user -u utdeck-uninstall`), 21:17:34-21:18:04:

| Step | Took | What happened |
|---|---|---|
| stop | 2 s | Steam stopped (no upload was in progress), UTDeck's helper units stopped |
| steam, utdeck, entries, memory | 2 s | 92 paths removed, each one named in the log |
| appdata | 26 s | waited for the app to close (the time until "Close UTDeck" was pressed), then its settings and cache |
| app | under 1 s | the click service's `Remove`: `click list` no longer shows UTDeck, `/opt/click.ubuntu.com/utdeck.alphaliu01` is gone |

Compared with the listing taken before (same `find`, depth 4, names only):

- **Gone, as planned**: `~/utdeck-probe` (the scripts, the Wine runtimes, the old game prefix, the run
  logs), `~/.steam`, Steam's program in `~/.local/share/Steam` (all but the two kept folders), the four
  app drawer entries, the app's settings and cache, and Steam's leftovers in `/tmp` (lock files, socket
  folders, `steam_chrome_shmem_*`) and `/dev/shm` (29 files before, 2 after: the two left belong to the
  system's tracing library, lttng, not to Steam).
- **Kept, as planned**: `~/.local/share/Steam/steamapps` with the six games' manifests and `common`
  (14 GB), `compatdata` (4.8 GB: the saves and game settings, with no broken links, and the copies set
  aside at the 0.3.49 install, `utdeck-saves-20261123/`) and `userdata` (480 KB: Steam's per-game
  settings and the controller layouts).
- **New**: only a log of Lomiri's download manager.
- No Steam, Wine, Xvfb or UTDeck process left, and no `utdeck-*` unit.

Still to check (the second half of step 4): the click installed again, the setup run (Steam installed
over the kept `steamapps`, QR sign-in), the six games shown as installed without a download, and a save
loading.

### Step 4, second half: installing again (2026-11-23)

The click installed again over adb (`tools/app-build/install-click.sh`), the setup run by the maintainer
from scratch (Xvfb, the Wine runtime, Steam, the QR sign-in).

- **The kept games came back without a download**: the six manifests are fully installed
  (`StateFlags` 4), `downloading/` is empty, and no file under `steamapps/common` changed since the
  reinstall, apart from one controller layout Steam writes at sign-in. Steam was installed next to the
  kept folders and took them as they were.
- **Two setup problems found** (both in `utdeck-setup.sh`, both in `docs/lessons-learned.md`):
  1. Steam's download count started at 18,000 MB: it was the size of Steam's whole folder, kept games
     included. The kept folders are now left out of the count.
  2. The QR code had to be scanned twice: the tool step stopped Steam 2 s after "Logged On", before
     Steam had saved the sign-in (it does so at "Success"). The sign-in step now waits for "Success".
     Checked against the phone: the check says "done" for the saved sign-in it has now; the first,
     lost sign-in never reached "Success".
  Both fixes go into the next click; trying them needs another setup from scratch (the 4.10 exit run
  is one).
- **A save loads**: Plants vs. Zombies: Replanted, the maintainer's save from before the uninstall
  (2026-11-23). Ys IX's saves are on the phone too (its `compatdata` kept whole), but were not opened in
  the game: no controller reached it (the DualShock 4 clone below).

**Reported at the same time, parked until step 4 is closed** (one thing at a time):

- A DualShock 4 clone: its own entry ("Wireless Controller") reacts on the controllers page, Steam's
  virtual gamepad does not, and Ys IX did not react to it. **Solved (2026-11-23)**, not caused by the
  reinstall: Ys IX opts in to Steam Input, Steam (with `/dev/uinput` on this phone) gave it only a
  virtual pad, and hidden on Xvfb it never fed it. `utdeck-compat` now gives the game the real pads and
  hides Steam's virtual one; Ys IX responds (the maintainer). Details in `docs/lessons-learned.md`;
  telling Steam which game is in front (to keep Steam Input) is stage 6's research. Small open point: the
  ignore list hides the virtual pad from SDL's game controllers only, so Wine may still show it to a
  game as a plain DirectInput joystick (`padlayout.py`, which lists every joystick, still sees it).
- Own games: a cover picture cannot be chosen from the gallery or the disk (carried over). **Solved
  in UTDeck 0.3.50 (2026-11-23)**: the cover list also looks one folder down in `~/Pictures` (where
  Screenshots and the camera save), and "Choose a picture..." (`CoverChooser.qml`, shared by the Add
  game and Edit game pages; `CoverPickerPage.qml`) asks Gallery or File Manager through Content Hub. The
  picture arrives in the app's cache (`HubIncoming`) and is copied into the game's folder when the game
  is saved (`cover-<time>.<ext>`, the copy it replaces removed). Checked on the Nothing Phone (1) by the
  maintainer: a cover from Gallery and one from File Manager, shown on the card and still there after
  the app restarted; on the phone one copy in the game's folder, `HubIncoming` empty again.

## Done (2026-11-23)

Every item of the exit, on the Nothing Phone (1): the uninstall with the defaults from the app; nothing of
UTDeck left in the home folder but the kept games and saves; the app removed by itself and installed
again; the setup run; Steam signed in again by QR code; the six kept games installed without a download;
a save loads (Plants vs. Zombies: Replanted). 4.9 is closed. Found on the way and fixed in
`utdeck-setup.sh` (tried at the next setup from scratch, 4.10's exit run): the download count with the
kept games in it, the sign-in lost by a stop that came too soon. Parked for after 4.9: the DualShock 4
clone in games (solved since, in `utdeck-compat`), an own game's cover picture (solved since,
UTDeck 0.3.50).
