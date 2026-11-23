# Stage 4.7: Steam without its interface, the plan

Written 2026-11-23, when 4.6 closed; started the same day.

Goal ([stage4-roadmap.md](stage4-roadmap.md)): Steam's own tasks from the app, so that a phone needs no
PC and no Steam phone app: installing a game the user owns, uninstalling one, seeing the space it
needs and has, signing in again when Steam's login runs out; and a game that also has a Linux build
installed as its Windows build (moved here from 4.6).

Exit: a game installed and removed from the app with no PC and no Steam phone app; a game with a Linux
build installs its Windows build from the app.

## Terms used here

- *`steam://` links* = commands the Steam client takes from outside (`./steam steam://install/APPID`),
  as the app already does for starting games (`-applaunch`) and opening Steam's window (4.3).
- *`appinfo.vdf`* = Steam's local copy of each app's public data (name, type, the systems it runs on,
  its depots and their sizes), in a binary key-value format; no account data.
- *depot* = one downloadable part of a game (often one per system: Windows, Linux).
- *compatibility tool mapping* = which tool Steam runs a game with; set per game in `config.vdf`
  (`set-default.py --app`, stage 3.S), only while Steam is stopped.

## What exists

- **Hidden Steam** runs, signed in (4.2), on its screen (Xvfb `:50`); UTDeck starts games on it and
  shows its dialogs on the "Steam is asking" page, taps passed on (4.3, the publisher's agreement drawn
  in Steam's main window included).
- **Installed games** from `steamapps/appmanifest_<appid>.acf` (the library, 4.1); their state flags and,
  during a download, `BytesToDownload` and `BytesDownloaded` in the same file.
- **What the user owns**: Steam caches cover art for every game in the user's library
  (`appcache/librarycache/<appid>/`, 222 folders on stone against 13 installed apps), and its public data
  in `appcache/appinfo.vdf`.
- **Signing in** by QR code and fallback B (4.2); Settings already says "Steam is running but not signed
  in" with "Sign in".
- **Forcing the Windows build**: `set-default.py --app APPID utdeck` (stage 3.7's rule: before the first
  download, with Steam stopped).

## How it would work

- **"Install from Steam"**, a third way in "Add a game": the user's Steam games not installed, with their
  covers, names, and the download size where `appinfo.vdf` gives it; only games (not tools, DLC,
  soundtracks). A tap shows the game, its size against the free space on the phone, and "Install".
- **Installing**: for a game that also has a Linux build, UTDeck first maps it to its own tool (Steam
  stopped for a moment, `set-default.py --app`, Steam started again), so Steam installs the Windows build.
  Then `steam://install/APPID`; Steam's install dialog (location, size) comes on the "Steam is asking"
  page, answered by the user, unless step 1 finds a way without it.
- **The download in the library**: the game's card shows its progress (from its `.acf`: downloaded of
  total, in MB and %), and becomes playable when Steam marks it installed. The app can be closed meanwhile
  (Steam downloads on its own); with Steam's switch off, Steam stays on until the download ends.
- **Uninstalling**: on a Steam game's long-press page (its settings, 4.6), "Uninstall", confirmed on the
  page with what goes (the game's files; its Windows folder with saves that are not in Steam Cloud), then
  `steam://uninstall/APPID` and Steam's confirmation answered on the "Steam is asking" page if it asks.
- **Free space**: shown before an install, and on the Steam game's page (its size on disk).
- **Signing in again**: when hidden Steam runs but is signed out (its login token ran out), the library
  says so with "Sign in" (the 4.2 path: QR code or fallback B), instead of games failing to start.

## Steps

1. **Find out, on stone**: what `steam://install/APPID` and `steam://uninstall/APPID` do on hidden Steam
   (a dialog of its own, a box in the main window, or nothing), and whether Steam's console offers a way
   without a dialog; read `appinfo.vdf` (its format, the fields: name, type, `oslist`, depot sizes) with a
   small reader; check that `librarycache` lists the user's games; how a download shows in the `.acf`.
2. **The Steam game list** in "Add a game" and the game's install page (size, free space).
3. **Installing** with the Windows-build mapping and the dialog, the progress on the card.
4. **Uninstalling** from the game's page.
5. **Signing in again** from the library when Steam is signed out.
6. **The exit run**: a small game installed and removed from the app; a small game with a Linux build
   installed as its Windows build.

Test device: stone. **Space**: stone has about 2.1 GB free, so the test games must be small, or the
uninstall is tried first on a game the maintainer no longer needs, which frees space.

## Not in 4.7

- Steam's store (buying), friends, achievements: Steam's interface stays hidden for those.
- Moving games between storage locations; several library folders (a memory card).
- Bringing 4.4-4.7 to the Nothing Phone: after 4.7, with the runtime release that carries Wine patch
  0020 (the maintainer's OK).

## Answers from the maintainer (2026-11-23)

- **Uninstall** Aperture Desk Job (4.4 GB) and Goat Simulator (2.4 GB) on stone: the uninstall test,
  and space for the installs.
- **Install** else Heart.Break() (1.35 GB; it also has a Linux build: the Windows-build test) and
  **reinstall** Goat Simulator.
- **"Install from Steam" lists every game in the library**, Linux-only ones too (marked as not
  playable yet): some Linux games will be supported later, and UTDeck does not ship right after 4.7.

## Step 1: found out, on stone (2026-11-23)

- **`tools/ut-probe/appinfo.py`** (new) reads `appcache/appinfo.vdf` (format version 29: key names in a
  table at the end of the file, its offset in the header). With `--library`, the apps in Steam's
  library cache: on stone 222, of which 57 games, 5 demos, 82 DLC, 70 tools, 7 applications, 1
  soundtrack. Per app: type, systems (`oslist`), the Windows build's size (its depots for Windows, public
  branch), name. For example else Heart.Break() `400110`, Windows, macOS and Linux, 1382 MB; its depots
  `400111` macOS, `400112` Windows, `400113` Linux.
- **Uninstalling** (`./steam -silent steam://uninstall/APPID` on `:50`): Steam opens its own window, 648x224
  ("Uninstall": "Would you like to uninstall <game> from this device? ...", Uninstall and Cancel), at
  a place that varies (+326+298, then +346+318); no account data in it. After "Uninstall": the
  game's `.acf` stays at "Uninstalling" while Steam runs the game's uninstall script through UTDeck's
  tool (`utdeck-compat run ... iscriptevaluator.exe ... evaluatorscript_<appid>.vdf`), which first
  brought Aperture Desk Job's old prefix up to the current runtime (about 1.5 minutes), then "finished
  uninstall (No Error)": the `.acf` and the game's files gone. **Steam keeps the game's
  `compatdata/<appid>`** (Aperture Desk Job's 1.5 GB, Goat Simulator's 3.0 GB with both bases): the
  Windows folder, with any saves not in Steam Cloud. Aperture Desk Job (4.4 GB) and Goat Simulator
  (2.4 GB) uninstalled, as the maintainer asked: stone from 1.6 GB free to 8.6 GB.
- **Forcing the Windows build**: Steam stopped, `set-default.py --app 400110 utdeck` (the game's own
  entry, priority 250), Steam started and signed in again (about 10 s).
- **Installing** (`steam://install/APPID`): Steam opens its "Install" window, 540x480 (the game with
  its size, two shortcut boxes, "Install to: Local Drive (/)" with the free space, Install and Cancel),
  and brings its main window up behind it (which shows the account name: only the dialog's own window
  is shown in the app). "Create an application shortcut" is ticked by default: it would add a second
  app drawer entry next to UTDeck's own (stage 3.8), so UTDeck unticks it. After Install: else
  Heart.Break() downloaded (335 MB, 1.45 GB unpacked) and installed in about 65 s, **its Windows build**
  (depot `400112`, `ElseHeartbreak.exe`). The main window stays open afterwards: closed again.
- **Progress**: the `.acf` gets its byte counts only at the end (`BytesDownloaded`, `BytesStaged` were 0
  until then); Steam's content log has the start (`update started : download 0/335508864 ... stage
  0/1449745509`) and the end (`state changed : Fully Installed`). So the progress comes from the size of
  `steamapps/downloading/<appid>` against the total to stage.
- A note for every later read of `.acf` files: they hold the account's number (`LastOwner`); printed
  only with numbers masked.

## Steps 2-5: built (2026-11-23, UTDeck 0.3.29)

- **`tools/ut-probe/steam-game.sh install|uninstall APPID`** (in the click, `steam/`): makes sure hidden
  Steam runs and is signed in (`steam-service.sh ready`, new: `play` without the game), then hands Steam
  `steam://install/APPID` or `steam://uninstall/APPID`. For an install of a game with a Linux build
  (`appinfo.py`: its systems), first its mapping to UTDeck's tool, unless it has one (Steam stopped,
  `set-default.py --app`, Steam started). `appinfo.py` goes into the click too.
- **`SteamGames`** (`app/src/SteamGames.*`, `steamGames` in QML): the library's games and demos (from
  `appinfo.py --library`: name, systems, the Windows build's size, Steam's cover, installed or not, a
  Windows build or not), free space, a game's size on disk; `install()` and `uninstall()` run the script,
  then show Steam's window on the "Steam is asking" page; the downloads (Steam's
  `steamapps/downloading/<appid>` against the game's total to stage, every 2 s); the task's end (the
  download running or the game installed; the `.acf` gone; or Steam's window closed without an answer).
  After an install, a `.desktop` file Steam made for the game (`steam://rungameid/<appid>`, its
  "application shortcut") is removed, never UTDeck's own (`utdeck-game-<appid>.desktop`). After an
  uninstall, the game's `compatdata/<appid>` is deleted only if the user ticked it (only that folder,
  checked by its real path). `.acf` fields are read one by one and never printed.
- **The "Steam is asking" page on request** (`SteamLaunch::showSteamDialog`): Steam's dialog window only,
  never its main window (which shows the account name); the page closes about 1.5 s after the window
  has gone, or after 90 s if none came. Its text now covers installing and uninstalling too.
- **Pages**: "Add a game" gets "From your Steam library" (when Steam is set up): the library's games with
  covers and a search field; every game listed, one without a Windows build marked "Linux only: not
  playable yet" (no Install); a tap shows the Windows build's size against the free space, the note on a
  Linux build, and Install. The library page shows, above the games: "Steam is signed out ... Sign in"
  (the 4.2 sign-in) when Steam runs signed out, Steam's task, and each download with a progress bar. A
  Steam game's settings page (4.6) gets "Uninstall": its size, the box for its Windows folder, a
  confirmation on the page.
- **Steam's switch off**: Steam stays on while a download runs (the stop after a game waits while
  `steamapps/downloading` has a game in it), and after an install or uninstall the same delayed stop
  follows.

## Tests and the Downloads page (2026-11-23, UTDeck 0.3.30)

- **Tested by the maintainer on 0.3.29**: Goat Simulator (it has a Linux build) installed from "From your
  Steam library": Steam restarted for the mapping, its Install window came on the "Steam is asking" page,
  and Steam downloaded **the Windows build** (`common/GoatSimulator` has `Binaries/Win32`); the game is in
  the library, and Steam's own app drawer shortcut for it was removed. else Heart.Break() uninstalled
  from its settings page a minute later, while Goat Simulator still downloaded (Steam's log: `finished
  uninstall (No Error)`): the two tasks do not get in each other's way.
- **Leftovers**: Steam's uninstall keeps the game's `compatdata/<appid>`, and also the files the game wrote
  into its install folder (Aperture Desk Job: 64 KB of settings and a log in `common/Aperture Desk Job`).
  Both deleted by hand for Aperture Desk Job (maintainer's OK). The settings page's box now covers both:
  with it ticked, the install folder goes too (its name read from the `.acf` before the uninstall; only
  inside `common/`, and not when another installed game uses the same folder).
- **The maintainer's remark: the progress bars above the games did not look good.** Moved to a page of
  their own, **Downloads** (`app/qml/DownloadsPage.qml`), opened from the library's header (its icon
  changes while something runs): the task asked from the app, each download with its size done and total,
  its speed (averaged over the 2 s polls) and the time left, and what ended while the app ran (installed,
  uninstalled). Install and Uninstall now go on to that page. The library keeps only the "signed out"
  notice, which is about every Steam game.
- **Downloads while the phone is locked.** The maintainer saw the download stop with the phone locked:
  Ubuntu Touch puts the phone to sleep (suspend) when the screen is off, and Steam stops with it.
  Built in 0.3.31 as part of keep-awake, below.

## Keep-awake (2026-11-23, UTDeck 0.3.31)

UTDeck had nothing that kept the phone awake: with the screen's timeout (the maintainer's: 30
minutes), a game played with a controller (no touch) had its screen turned off and the phone locked in
the middle of play, and a download stopped once the phone slept. The maintainer's rule: keep-awake
while a game runs or something downloads, with a per-game switch to turn it off.

- **`tools/ut-probe/utdeck-awake.py`** (in `wine/`) asks the phone's power service, repowerd, on the
  system bus. `game PID LOG`: `keepDisplayOn` (the screen stays on) while the game's window is in front
  (Wine's focus file, as `utdeck-pause.py` reads it); another app in front, the request is dropped and
  the user's own timeout applies; back in front, it is taken again. `downloads`: `requestSysState(name,
  1)` (the phone does not sleep; the screen may go off, which saves the battery) while a folder in
  `steamapps/downloading` grew in the last 3 minutes; it ends 30 s after Steam ends.
- **Checked on stone before building**: repowerd lists both requests while held (`listSysRequests`) and
  drops them the moment the asking program ends, killed included. So a crashed helper cannot leave the
  phone awake. Both modes then tested on stone: a fake game (a `sleep` and a focus file switched
  between 1 and 0: the screen request came, went and came back, and was gone with the game) and a fake
  download folder (the request came, and went once the folder was gone).
- **Where it runs**: `game-run.sh` starts the game mode for every game (Steam's and the user's own),
  not for Steam's install scripts, and stops it when the game ends. `steam-service.sh start` starts the
  download mode as its own unit (`utdeck-awake`), `stop` stops it.
- **The switch**: `UTDECK_KEEP_AWAKE=1` in `defaults.env` (on for every game); "Keep the screen on" on
  each game's settings page turns it off for that game. The Downloads page says that the phone stays
  awake while downloads run.
- **To check on the phone**: a game played with the controller past the screen's timeout; the power
  button still turning the screen off during a game; a download going on with the phone locked.

### The first download test (2026-11-23)

The maintainer: the screen stayed on in Celceta and went off with the switch turned off, but "the phone
failed to keep awake during downloads". What the phone's logs show for that test (Barro 2020, 151 MB,
12:03:04-12:05:48):

- **The phone did not sleep**: the kernel's suspend counter (`/sys/power/suspend_stats/success`) was
  still 0 for the whole boot, and Steam's content log wrote its "Current download rate" line every 61 s
  without a gap. The download finished.
- **The keep-awake request works with the screen off**: holding it, with the screen turned off through
  repowerd (`setScreenPowerMode("off")`, as the timeout does), a 30 s download from Ubuntu's release
  server ran at 34 Mbps (23 Mbps with the screen on).
- **But the download folder stayed empty**: two looks at `steamapps/downloading/1168660` 15 s apart, the
  last one a second before the end, found no files; Steam then committed 89 files "from" that folder.
  So the folder's size, which the watcher (and the Downloads page's percentage) relied on, says nothing
  for such a download: the watcher would have let the phone sleep after 3 minutes of a longer one, and
  the Downloads page showed 0 %.

Changed (UTDeck 0.3.32): the watcher also counts Steam writing its content log in the last 3 minutes as
"downloading", and logs each decision to `~/utdeck-probe/steam/awake.log`; the Downloads page shows an
unknown (moving) bar instead of 0 % while the folder is empty.

**Settled (the maintainer, the same day)**: the "failure" was the screen turning off during the
download, which is the design (for downloads the phone stays awake, the screen may go off); Barro
finished with the screen off. Nothing was wrong.

## Extension of the plan (2026-11-23)

The exit above is met: Goat Simulator (it has a Linux build) installed from the app as its Windows build,
else Heart.Break() installed and uninstalled from the app. The maintainer asked what else 4.7 needs, and
named one thing: **Steam Cloud**, since Ys IX, Plants vs. Zombies: Replanted, Ys Memories of Celceta and
Stray all started on the phone from a fresh save.

### What was found (stone, 2026-11-23)

- **Steam Cloud never worked for a game on UTDeck.** Steam's `logs/cloud_log.txt`: "Unable to resolve path
  with root WinSavedGames (9)" (Ys IX 4743 times, Celceta 8117), "... WinAppDataLocal (3)" (Stray,
  Plants vs. Zombies), then "Skipping download of file ... failed to resolve path" for every save in the
  cloud. Steam knows the saves (its `userdata/<account>/<appid>/remotecache.vdf` lists them, e.g.
  Celceta's `auto0001/savedata.dat`), but has nowhere to put them, and nothing made on the phone goes up.
  The cloud copies (from the maintainer's PC) are still there: skipped, not deleted.
- **Why**: for a game run through a compatibility tool, Steam looks for the Windows folders ("Saved
  Games", "AppData\Local", "Documents") of the user **`steamuser`** in `compatdata/<appid>/pfx/drive_c/users/`,
  Proton's user name. UTDeck's Wine names the user after the Linux user: every UTDeck prefix has
  `users/phablet`, no `users/steamuser`. The Proton runtime (4.6) does make `steamuser`, but in
  `pfx-proton`, which Steam does not look in either.
- **Shader pre-caching**: Steam downloads, per game, shader collections other players' machines recorded
  (*Fossilize* databases) and compiles them on the phone (`fossilize_replay`, which exists for ARM and
  ran here: Aperture Desk Job on 2026-11-21, Stray on 2026-11-19). On stone: 858 MB, 772 MB of it for
  ATRI. Unknown whether our games ever use the result (the compiled shaders go into a driver cache that
  DXVK on Turnip may or may not read); if not, it is space, download and battery for nothing.
- **Updates**: every installed game is set to "always keep updated" (`AutoUpdateBehavior 0`) and to pause
  other downloads while it runs (`AllowOtherDownloadsWhileRunning 0`). With Steam's switch off, Steam
  runs only around a game, so an update can only happen when the user starts the game, which then waits
  on it with no word why. No game has an update pending now, so this is untested.

### Steps added

7. **Cloud saves, find out**: on one game whose phone save the maintainer does not need (see the
   questions), give Steam the folder it looks for: `users/steamuser` in `pfx` as a link to the prefix's
   real user folder (`users/phablet`), so Steam and the game read and write the same files. Check in
   `cloud_log.txt` that the paths resolve, that the cloud save comes down, that the game finds it, and
   that a new save goes up after the game ends. Also how Steam asks when the phone and the cloud both
   have saves (its "Cloud Sync Conflict" window): does it come on the hidden screen, and so on the "Steam
   is asking" page?
8. **Cloud saves, for every game**: `utdeck-compat` makes the link in every Steam prefix (new and
   existing) before the game starts; for the Proton runtime, the link points to `pfx-proton`'s
   `steamuser` (or both runtimes share one save folder, the maintainer's choice). Before the first sync of
   a game that has saves on the phone, those saves are copied aside (`compatdata/<appid>/utdeck-saves-
   <date>/`), so no choice in Steam's conflict window can lose them.
9. **Uploads before Steam stops**: Steam uploads a game's saves after the game ends; with Steam's switch
   off, UTDeck stops Steam 45 s after a game ends. The stop waits until Steam's cloud log shows the
   game's upload done (or a time limit), so the last session's save is never left only on the phone.
10. **Cloud state in the app**: the Steam game's settings page shows whether the game uses Steam Cloud
    and when it last synced, from `remotecache.vdf` and the cloud log; a failed sync (no network, a
    conflict left open) says so.
11. **Updates**: find out what the start of a game with a pending update looks like (Steam's log, the
    `.acf` flags, the starting page); then the starting page says "Updating ... before it starts" with the
    download's progress (the Downloads page's data), and the Downloads page shows an update like an
    install. If Steam pauses downloads while a game runs, the Downloads page says that too.
12. **Shader pre-caching, find out**: whether a game started after Steam's `fossilize_replay` ran gets
    its shaders from that work (the driver cache's files and hits, a game's first-minute stutter with and
    without); if it brings nothing, turn it off in Steam's settings and free the space (858 MB on stone);
    if it helps, keep it, and let it run only while charging if Steam allows that.
13. **Repair a game**: "Check the game's files" on the settings page (`steam://validate/APPID`, Steam
    downloads what is missing or broken), shown on the Downloads page.
14. **Signing in again** (step 5, built but untested): tested once, by making Steam sign out.
15. **The exit run, extended**: a game's cloud save comes down to the phone and the game continues from
    it; a save made on the phone goes up and is seen on the PC; a game's update (when one comes) shows
    before the start.

### Questions for the maintainer

- **Whose saves count** for Ys IX, Ys Memories of Celceta, Stray and Plants vs. Zombies: Replanted: the
  PC's (in the cloud) or the fresh ones made on the phone? The phone's are copied aside either way.
- **Which game for step 7's first test**: one whose phone save does not matter.
- **Proton runtime saves**: one shared save folder for both runtimes (a game switched between them keeps
  its saves), or each runtime its own (as now)?

## Step 7: cloud saves, found out (2026-11-23, Celceta)

The maintainer's answers: **the cloud saves win** (the phone's are copied aside), **Celceta** for the first
test, **one save folder shared** by both runtimes.

- Celceta's phone saves (3 slots, 4 autosaves, `systemdata.dat`, `cate.b`) moved to
  `compatdata/587110/utdeck-saves-20261123/pfx/` (and the files a 12:30 launch made, to `.../pfx-1230/`):
  kept, not deleted, and the live folder empty, so nothing could conflict with the cloud.
- **A `users/steamuser` link alone did nothing**: with `pfx/drive_c/users/steamuser -> phablet` in place,
  the next launch still logged "Unable to resolve path with root WinSavedGames (9)" and the game showed no
  PC saves. Steam never called the tool's `getcompatpath`/`getnativepath` either.
- **Steam's rule, read from its client** (`linuxarm64/steamclient.so`, disassembled on the PC with
  Capstone): the reader of `compatibilitytool.vdf` keeps each tool's key name (the name inside
  `compat_tools`, e.g. `utdeck`) at offset 0x40 of its record (display name 0x50, install path 0x60,
  from_oslist 0x78, to_oslist 0x80); another function searches that name for `proton` ignoring case
  (`V_stristr`-like, through the C library's tolower table), and only if found sets the Windows folders
  to Proton's: `/pfx/drive_c/users/steamuser/My Documents`, `.../AppData/LocalLow`, `.../Application
  Data`, `.../Local Settings/Application Data`, `.../Saved Games`, `/pfx/drive_c/ProgramData`. So a tool
  named `utdeck` never gets cloud saves for Windows folders; `utdeck-proton` does. The display name and
  the folder do not count.
- **Checked**: Celceta mapped to `utdeck-proton` (Steam stopped, `set-default.py --app`, started), launched:
  no "Unable to resolve" line, 338 files (101 MB) downloaded into `pfx/drive_c/users/steamuser/Saved
  Games` (the link, so `users/phablet/...`), "Download complete, result OK". That launch ran on the Proton
  base in `pfx-proton` (the tool's own runtime), so it was ended; for the play test, Celceta's `utdeck-env`
  says `UTDECK_RUNTIME=utdeck` (the default runtime in `pfx`, where the saves are) while it stays mapped
  to `utdeck-proton`.
- **Also found**: in UTDeck's prefixes `Documents`, `Downloads`, `Music`, `Pictures` and `Videos` of the
  Windows user are links to the phone's own folders (`/home/phablet/Documents`, ...): a game saving to
  "Documents" writes into the phone's Documents. Proton makes them real folders in the prefix.
- **The play test (the maintainer, 12:40-12:55)**: Celceta (mapped to `utdeck-proton`, default runtime in
  `pfx` through its `utdeck-env`) showed the PC saves and loaded one. A first session wrote no save slot
  (only `systemdata.dat`, the game's settings, which Celceta's cloud rule `FALCOM/Ys Memories of
  Celceta/game/*.*` does not cover), so Steam's exit sync had nothing to upload. The second: the
  maintainer saved into slot 116 ("saved" on screen) and quit; `game/slot0116/` was written at 12:54:25,
  the game ended at 12:54:44, and Steam's exit sync found it and uploaded its three files at 12:54:46-47
  ("Upload complete, result OK", change number 1). **Step 7 is done: a cloud save comes down and a phone
  save goes up**, given a tool whose name contains `proton`.
- Steam's exit sync runs when the game's own process ends (12:54:45, right after `waitforexitandrun`
  ended), within 1-2 s: well inside the 45 s before UTDeck stops Steam. Step 9's wait is still needed for
  big saves or slow networks.

## Step 8: cloud saves for every game (2026-11-23, UTDeck 0.3.33)

The maintainer's OK: one tool named `utdeck-proton`.

- **One tool**: `compatibilitytool.vdf` names UTDeck's tool `utdeck-proton` (display name still "UTDeck
  runtime", folder still `compatibilitytools.d/utdeck`); the second tool (`steam-compat/proton/`,
  Proton 11's base) is gone from the repository: a game's Wine base is its `UTDECK_RUNTIME` (the app's
  settings page, 4.6). `set-default.py` defaults to `utdeck-proton` and gained `--apps TOOL` (the games
  mapped to a tool; allowed while Steam runs) and `--rename OLD NEW`.
- **`tools/ut-probe/steam-compat/utdeck-profile.py`** (new): one Windows user folder per game, in
  `pfx/drive_c/users/steamuser` (where Steam syncs); `pfx`'s own user folder (`users/phablet`) and
  `pfx-proton`'s `users/steamuser` are links to it, so both Wine bases share the saves (the maintainer's
  choice). Inside it Steam's Windows XP names as links (`My Documents`, `Application Data`, `Local
  Settings/Application Data`: Steam's client resolves the Documents and AppData roots through them), and
  Documents, Downloads, Music, Pictures, Videos, Desktop, Templates as real folders (Wine linked them to
  the phone's own; `wineboot -u` was checked to leave real folders alone, on a throwaway prefix). A
  folder that already had files is merged in; copies both had go to `utdeck-saves-<date>/`.
- **`utdeck-compat`**: lays the user folder out at every start; a prefix counts as made when Wine's
  `system.reg` exists (Steam itself now creates `pfx/drive_c/users/steamuser/...` when it brings a new
  game's cloud saves before its first start). `own-run.sh` makes the media folders real in the user's own
  games' prefixes (`utdeck-profile.py folders`; no Steam Cloud there).
- **`tools/ut-probe/steam-tools.sh`** (new, `steam/`): brings a phone over, each part only if not done:
  the old second tool's games get `UTDECK_RUNTIME=proton`, its folder goes, every `utdeck` mapping
  becomes `utdeck-proton`, and every game's user folder is laid out once (`utdeck-profile.py migrate`,
  marker `compatdata/<appid>/utdeck-profile`) with **the cloud winning**: a file the game's cloud list
  names, never synced to the phone (`localtime` 0) but present, is moved to
  `utdeck-saves-<date>/cloud-wins/`. Runs before every Steam start (`steam-service.sh start`) and once
  after an app update (the setup's files step, `--restart`: Steam stopped for it and started again; not
  while a game runs).
- **On stone** (the real update path: 0.3.33 installed, the app started, its quiet update): Steam stopped,
  Celceta kept its own runtime line, Goat Simulator got `UTDECK_RUNTIME=proton`, the old tool removed,
  `utdeck -> utdeck-proton` for the default and two games, seven games' user folders laid out (the
  Proton prefixes' copies to each game's `utdeck-saves-20261123/pfx-proton-steamuser/`), 29 phone files
  of Ys IX and 3 of Plants vs. Zombies moved to `cloud-wins/`, Steam started. Steam registered the tool
  ("Posting queued tool registration callback 0 utdeck-proton"), no mapping left on `utdeck`, and within
  a minute synced Stray, Ys IX (527 files) and Plants vs. Zombies: "Download complete, result OK", no
  conflict, no unresolved path.
- **By hand** (no rule can tell which game wrote what into the phone's shared Documents): Goat
  Simulator's `My Games/GoatSim` from the phone's Documents (settings, logs and two crash dumps of its
  runs on the default base; its progress is in Steam's own storage) to its
  `utdeck-saves-20261123/phone-documents/` (the prefix had its newer copy from the Proton base, which it
  uses now); 3DMark 11's folder into its own prefix's Documents. Steam's install-script log
  `runprocess_log.txt` stays in the phone's Documents. Celceta's test line in `utdeck-env` removed.
- **Tested by the maintainer**: Ys IX, Plants vs. Zombies: Replanted and Ys Memories of Celceta start
  with the PC's saves (Celceta's slot 116 too); Stray skipped (its saves synced, but it loads very slowly
  and runs badly: a performance matter for later, not 4.7). **Shared saves work**: Celceta switched to
  the Proton runtime on its settings page saw the same saves. Step 8 is done.

## Step 9: no stop during a cloud sync (2026-11-23, UTDeck 0.3.34)

- `steam-service.sh stop` (every stop goes through it: the app's after a game with Steam's switch off,
  the restarts of `steam-game.sh` and `steam-tools.sh`) first waits while Steam's `logs/cloud_log.txt`
  was written in the last 10 s (an exit sync and its uploads write it line by line), at most 5 minutes.
  The app's checks after a stop now also come at 60, 120 and 330 s.
- Measured on stone: with the log touched every 3 s for 21 s, the stop waited and took 31 s in all, then
  stopped Steam as before. In real play the exit sync and upload took 1-2 s after the game ended (step 7),
  well inside the 45 s the app waits anyway; the wait covers big saves and slow networks.

## Step 10: the cloud state on the game's page (2026-11-23, UTDeck 0.3.35)

- **What Steam's files tell** (stone): the game's cloud list `userdata/<account>/<appid>/remotecache.vdf`
  has one block per file with its `size` and `syncstate` (1: in sync; 2 was "never synced" before step
  8); Steam rewrites it at every sync ("YldWriteCacheDirectoryToFile"), so its time is the last sync. A game
  that never synced has none (Barro 2020, else Heart.Break()). The cloud log gives each sync's result
  ("Download/Upload complete, result OK"), conflicts ("Sync conflict for file") and folders it could not
  place ("Unable to resolve path"); it rotates at 1 MB (`cloud_log.previous.txt`).
- **How a game uses Steam Cloud**, from its public data: `appinfo.py` prints a sixth column, `cloud`,
  from the game's `ufs` block: "auto" (rules for files in its folders: Celceta, Ys IX, Stray, Plants vs.
  Zombies, Barro 2020), "api" (the game hands its saves to Steam: Goat Simulator, ATRI, else
  Heart.Break()), or empty.
- **`SteamGames::cloudState(appId)`**: files, size, files in sync, last sync, the last result, a problem
  of the last sync ("conflict", "path", "failed"; each sync starts afresh, and "Successfully synced" does
  not clear one, since Steam logs it also after a sync that placed nothing), and the dates of
  `utdeck-saves-<date>` folders. The account's folder is only looked into, never shown.
- **The settings page of a Steam game** has a "Steam Cloud" section above "Uninstall": "N files in Steam
  Cloud (size), last synced ...", or "does not use Steam Cloud", or "not synced yet"; a problem in red
  (a conflict: Steam asks at the next start; the save folder not found; a failed sync, perhaps no
  network); the dates of phone copies set aside and where they are.

## Keep-awake during a game's start (2026-11-23, UTDeck 0.3.36)

- **The maintainer**: the screen still went off while a game loaded (more than a minute). The game-mode
  keep-awake begins in `game-run.sh` (Ys IX's log: "keep-awake: 13:25:24 the screen stays on", the
  moment its run began), but before that the start runs without it: Steam's own start when it was off
  (about a minute), the "Steam is asking" windows, first-start install scripts, the game's launcher.
  UTDeck's starting page is in front all that time.
- **Fix**: the starting page holds the screen on (the game's "Keep the screen on" setting) until the
  game's window has been in front, the page goes, or it shows an error. `KeepAwake` (new,
  `app/src/KeepAwake.*`, `keepAwake` in QML: hold(key)/release(key)) runs `utdeck-awake.py hold`
  (new mode: `keepDisplayOn` until its input is closed) while any key is held; if the app ends, the
  helper's input closes and repowerd drops the request. From the game's window in front on,
  `game-run.sh`'s helper alone decides (it lets the timeout work while another app is in front).
- Checked on stone: `utdeck-awake.py hold` with its input open: one screen request (`listSysRequests`);
  input closed: none.
- **Then (the maintainer)**: the screen stayed on while loading, but after quitting the game the
  starting page was still there, and then said "The game ended before it started" (Ys IX, twice). A
  per-second recorder on stone showed UTDeck running the whole time (never paused), game-run.sh's state
  file there with focus "1" while the game ran, and the starting page's hold on until the game ended:
  the app never saw the game. Its file watches were on `~/.cache/utdeck` and `~/.cache/utdeck/setup`,
  not `/run/user/32011/utdeck`: the app had `XDG_RUNTIME_DIR=/home/phablet/.cache`, because it was
  started from an adb shell (`lomiri-app-launch` after each install, by me), which has no
  `XDG_RUNTIME_DIR`. Not the keep-awake change: every app started that way since 11:28 was blind to
  games.
- **Fix (0.3.37)**: `main.cpp` sets `XDG_RUNTIME_DIR` to `/run/user/<uid>` when it differs and that folder
  exists (for the app and what it starts), and Library and Setup use `/run/user/<uid>` directly.
  Checked: the app started the same way from adb now watches `/run/user/32011/utdeck` and its `setup`.

## Step 11: updates before a game starts (2026-11-23, UTDeck 0.3.38)

- **Found out**: Steam's launch stages (`steamui.so`'s list, and the 132 launches in stone's console
  log) include `DownloadingDepots` (the game brought up to date before the start), `VerifyingFiles`,
  `UpdatingAppInfo`, and the cloud questions `cloudconflict`, `syncfailed`, `pendingcloudsessions`
  (these wait for an answer, so they come on the "Steam is asking" page). `DownloadingDepots` was seen
  twice, both for Plants vs. Zombies on 2026-11-21: about 10 s, the content log showing "Update Queued"
  and back to "Fully Installed" with nothing downloaded (the game was up to date: a check). A real update
  downloads into `steamapps/downloading/<appid>`, which the Downloads page already follows. Every game is
  set to "always keep updated" and to hold other downloads back while it runs. Nothing to update now:
  each installed game's `buildid` (`.acf`) equals its public branch's (`appinfo.vdf`), checked on stone.
- **Built**: the starting page names the stage: "Steam is checking for an update..." during
  `DownloadingDepots` with no download, "Updating the game before it starts: N %" with a progress bar
  when the game downloads (the Downloads page's data), a hint that with Steam's switch off updates come
  at a game's start, and "Steam is checking the game's files..." for `VerifyingFiles`. A download whose
  game was installed already (its `.acf` has a size on disk) is shown on the Downloads page as "(update)",
  and while a game runs the page says Steam holds other downloads back until it ends.
- **To check when an update comes** (step 15): the starting page's progress during a real update.
- **Corrected after the maintainer's test** (Ys IX and Celceta showed no "checking for an update"):
  their launches went from `CheckShaderDepotManifest` straight to `ProcessingInstallScript`; there was no
  `DownloadingDepots` stage at all. Steam enters it only when it has an update check or an update queued
  for the game (Plants vs. Zombies on 2026-11-21: not played for 27 hours). So the text shows only then;
  "every start checks for 10 s" was a wrong conclusion from 2 launches out of 132.
- **Found on the way, fixed (0.3.39)**: Celceta spent 49 s in `ProcessingInstallScript` because
  `utdeck-compat` updated its whole prefix again ("prefix updated to the runtime, DXVK and VKD3D-Proton
  put in"). The marker of a prepared prefix was one per game (`compatdata/<appid>/utdeck-prefix-dlls`),
  but since 4.6 each Wine base has its own prefix: after the maintainer's switch to the Proton base and
  back (step 8's shared-saves test) the marker named the other base, and every switch would cost a full
  prefix update. Now each prefix has its own (`<prefix>/utdeck-prefix-dlls`); the old one still counts
  for the base it names. Also seen: Ys IX spends about 10 s at every start in Steam's install-script
  check (`iscriptevaluator.exe` through Wine); Proton does the same; a candidate for later speed work.

## Step 12: shader pre-caching (2026-11-23, UTDeck 0.3.40-0.3.41)

- **What the 858 MB were** (`steamapps/shadercache/<appid>/` on stone): `transcoded_video.foz` (game
  videos re-encoded by Valve into open formats, for Proton's media converter: 760 MB for ATRI alone),
  `fozpipelinesv6` (shader collections recorded on other players' machines, a few MB each), and
  `mesa_shader_cache_sf` (13-21 MB per game): **Turnip's own cache**, what our driver compiled while the
  games ran (Steam points Mesa there with `MESA_SHADER_CACHE_DIR`, and after each game reads it:
  "Turnip Adreno (TM) 619 / L7:...: 113 shaders").
- **Steam's pre-compiling brings our games nothing**: the phone has no system Vulkan driver (no
  `icd.d`; UTDeck's runtime brings its own Turnip), so Steam's driver check (`vulkandriverquery`) fails at
  every start (64 times) and its `fossilize_replay` compiles the collections with SwiftShader, Steam's
  CPU Vulkan driver (`vk_swiftshader_icd.json`, the only one it has; its "GPU" is "SteamSwarm"). Costs: CPU
  and battery (26 s for Aperture Desk Job's 750 pipelines), and first starts held on "Processing Vulkan
  shaders" until answered (7 times on 2026-11-19, 6 minutes to 1.5 hours). The re-encoded videos: only
  the Proton base's `winegstreamer` has the media converter, and it needs `MEDIACONV_*` variables that
  Proton's own script sets and `utdeck-compat` does not, so no game reads them now (a lead for the movie
  issue K10 on the Proton base, later).
- **Changed**: hidden Steam starts with `-noshaders` ("Disable the shader manager", from Steam's client):
  no shader downloads, no SwiftShader compiling, no "Processing Vulkan shaders" stage. `utdeck-compat`
  sets `MESA_SHADER_CACHE_DIR` to the game's shadercache folder itself (single-file cache), so Turnip
  keeps its per-game cache. `steam-tools.sh` removed the pre-caching files once (shader collections,
  re-encoded media, empty folders): the shadercache folder went from 862 MB to 75 MB (ATRI 772 -> 13
  MB), Turnip's caches kept. Steam's command line on stone shows `-noshaders`.
- **Found on the way, fixed (0.3.41)**: step 9's wait ("the cloud log quiet for 10 s") held a stop made
  right after Steam's start for 99 s (Steam then checks every game's cloud state and writes for a
  minute), and once a stop returned while a Steam process was still ending (`set-default.py` refused).
  The stop now waits only while the log's last upload line is one of an upload under way ("Starting sync
  (up", a batch, a file upload; not "Upload complete"/"Upload failed"), and up to 15 s more for Steam's
  processes to end. Measured: a stop right after a start took 5 s; the wait logic tested on sample logs.
- **Checked with Celceta (the maintainer)**: no shader question, no stutter; Turnip's cache
  (`shadercache/587110/mesa_shader_cache_sf/<build>/Turnip Adreno (TM) 619/foz_cache.foz`) written at
  14:38:08 during the run, through the folder `utdeck-compat` now sets; the launch no longer has the
  `CheckShaderDepotManifest` stage. Step 12 is done.

## Step 13: checking a game's files (2026-11-23, UTDeck 0.3.42-0.3.43)

- **Found out** (Barro 2020, 565 MB, 76 files): `steam://validate/APPID` asks nothing; Steam runs the check
  as a job of its download scheduler and brings its main window up (closed again, as after a launch).
  "Start validating appID ...", "Verifying Installed": every file hashed (565 MB in about a second on
  stone). With one file damaged by hand (4 bytes of a 58 KB data file, backed up first): "Validation: 1
  chunks corrupt ... full scan ... found 1/76 mismatching files", then a download of that one chunk, and
  the file's checksum was the original's again. With nothing wrong: no "Validation:" line, only
  "Verifying Installed" and "scheduler finished (result No Error)".
- **Built**: `steam-game.sh validate APPID` (then `steam-service.sh hide` for the main window);
  `SteamGames::verify()` follows the job in Steam's content log from the task's start ("scheduler
  finished" ends it, after any repair download) and puts the result into the Downloads page's finished
  list: "files checked, all fine" or "N missing or damaged fetched again"; a repair download shows as
  "(repair)". A Steam game's settings page has "The game's files" with "Check the game's files" (not while
  the game runs), which opens the Downloads page.
- Checked: the script sent the check and left no Steam window mapped on the hidden screen.
- **The maintainer's test, then fixed (0.3.44)**: the Downloads page stayed on "Steam is checking Barro
  2020's files" with a "repair" at 0 MB. Two causes. (1) Barro 2020 had been started at 14:45:47 (its
  runtime began only at 14:48:17), and the check was sent at 14:47:53: Steam's console log shows
  "VerifyApp changed task to Completed with "0"" with no check started (Steam does not check a game that
  is running or starting), and UTDeck waited for a "Start validating" that never came. (2) My own check at
  14:44, which found nothing, left an empty `steamapps/downloading/1168660`, which UTDeck took for a
  download (so the "repair"); the same would have held Steam's stop and the phone awake. Steam's
  verdict is in the console log: "Completed with "76"" (all 76 files fine), "Failed with "1"" (one
  damaged: the repair download follows; done at "scheduler finished"), "Completed with "0"" (not checked).
  Now `SteamGames` reads that verdict (not checked: "Try again once it has ended" in the finished list),
  and a `downloading/<appid>` folder counts as a download only while the game's `StateFlags` is not plain
  4 ("fully installed"): in the Downloads page, Setup's stop of Steam, and `utdeck-awake.py`.
- **Tested by the maintainer (0.3.44)**: with one file of Barro 2020 damaged, "Check the game's files"
  ended with "1 missing or damaged fetched again"; the second check said "all fine". The repaired file's
  checksum equals the original's (backup compared, then deleted). Step 13 is done.

## Step 14: signing in again (2026-11-23)

Tested with the maintainer's OK: Steam signed out on stone through its own menu on the hidden screen
(Steam > Sign Out... > Confirm; the confirmation is drawn inside the main window). Steam then showed its
sign-in window with the QR code, and its connection log stayed on "Connecting". The maintainer: the
library showed "Steam is signed out ..." with "Sign in"; signing in by QR code from the app worked; Celceta
started afterwards. The screenshots of these steps (the account and a live sign-in code) were deleted
right after. Step 14 is done.

## Done (2026-11-23)

The exit ([stage4-roadmap.md](stage4-roadmap.md)), on stone, with the maintainer:

| Exit criterion | Result |
| --- | --- |
| A game installed and removed from the app, no PC, no Steam phone app | Goat Simulator installed and else Heart.Break() uninstalled from the app (steps 2-5); both reinstalled and used since |
| A game with a Linux build installs its Windows build from the app | Goat Simulator (Windows and Linux builds): mapped to UTDeck's tool first, `common/GoatSimulator/Binaries/Win32` |
| A cloud save comes down and a phone save goes up | Celceta: the PC's saves on the phone, slot 116 saved on the phone uploaded ("Upload complete, result OK"); Ys IX, Plants vs. Zombies and Stray got their PC saves (step 8) |

Built on the way, each tested on the phone: the Downloads page (progress, speed, time left, what ended),
keep-awake (screen on in games and their start, the phone awake during downloads; per-game switch), one
Steam tool named `utdeck-proton` with one save folder per game shared by both Wine bases, media folders
inside the prefixes, no Steam stop during a cloud upload, the cloud state on the game's page, Steam's
shader pre-caching off (787 MB freed), checking a game's files, signing in again.

Fixed on the way: the app started from adb watched the wrong runtime folder; a prefix marker per game
where it is per prefix (a 49 s prefix update after switching bases); a stop that waited 99 s on an
unrelated cloud log; empty download folders taken for downloads.

**Open**, and where each goes:
- The starting page during a real update (step 11): checked when one of the installed games gets an
  update.
- Bringing 4.4-4.7 to the Nothing Phone, with the runtime release carrying Wine patch 0020 (the
  maintainer's OK); the tool's rename and the user folders move over by themselves there (steam-tools.sh).
- Speed: about 10 s of Steam's install-script check at every start (Ys IX), Stray's slow load and poor
  performance.
- Movies on the Proton base: its `winegstreamer` has Proton's media converter, which needs `MEDIACONV_*`
  and Steam's re-encoded videos (off now with `-noshaders`): a lead for K10.
