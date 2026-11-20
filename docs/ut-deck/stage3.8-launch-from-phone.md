# Stage 3.8: starting games from the phone

Asked by the maintainer on 2026-11-20, before stage 4: start, play and quit an installed Steam
game on the phone alone, without the PC and without Steam's window. Until now every game was
started from the PC over adb (`steam-service.sh launch APPID`), because hidden Steam (stage 3.S:
the client running as a background service, with no window) has no interface on the phone.

Exit (roadmap): two games started and quit from the phone with no PC attached.

## What is known before starting

- A `.desktop` file in `~/.local/share/applications` shows up in Lomiri's app drawer, and
  Lomiri starts its `Exec` line as an app (stage 2.3, `utdeck-vktest`; stage 2.4, 3DMark 11).
- Lomiri gives such an app an X11 display (`DISPLAY`, Xwayland); Wine must not see it (stage 2.3).
  Games started through hidden Steam get Steam's environment instead, so this does not reach them.
- Lomiri links a window to an app through the process it started (stage 2.3, M4). A game started
  by hidden Steam is not such a process, so Lomiri has no app and no orientation policy for it:
  started sideways, it comes up portrait (3.S known issue K1; started upright, then turned, it
  works). An entry that only asks hidden Steam to start the game does not change that.
- Steam's install records (`steamapps/appmanifest_<appid>.acf`) hold each app's name, folder and
  state; Steam's image cache (`appcache/librarycache/<appid>/`) holds its pictures
  (`library_600x900.jpg`, the portrait cover, for every game here). The phone has no image tools
  (no PIL, no ImageMagick), so the cover is used as it is.
- Installed but not games: Steam Linux Runtime 1.0 and 4.0, Proton Experimental, Steamworks Common
  Redistributables, FEX (the client's own x86-64 emulator).

## Plan

1. **One entry per installed game** (`steam-library.py` on the phone): `utdeck-game-<appid>.desktop`
   with the game's name and cover; entries of games no longer installed are removed. Run when
   hidden Steam starts, and by hand.
2. **The entry hands off at once.** Its `Exec` starts a transient user service
   (`systemd-run --user`) running `steam-service.sh play APPID`, then exits: Lomiri may end an app
   that shows no window, and starting Steam can take a minute. The job starts hidden Steam if it is
   not running, waits until Steam has signed in, then starts the game as `launch` does.
3. **Feedback while waiting:** a phone notification ("Starting Steam...") if Lomiri shows one.
4. **The check:** the maintainer, with no PC attached, starts two games from the app drawer, plays
   and quits each.

## Made

- [`steam-library.py`](../../tools/ut-probe/steam-library.py): one `utdeck-game-<appid>.desktop`
  per fully installed game (Steam's state flag 4), named as in Steam, the portrait cover as its
  icon; Steam's tools skipped by app id and name; entries of removed games deleted. On the phone it
  found six: ATRI, Stray, Ys IX, Goat Simulator, PvZ Replanted, Ys: Memories of Celceta.
  `steam-service.sh start` runs it, so a game installed through Steam gets its entry at the next
  start of hidden Steam.
- `steam-service.sh play APPID`, the entry's job (a transient user service, `utdeck-play-<appid>`):
  if Steam is not signed in, a notification ("Starting Steam, the game follows in about a
  minute"), Steam started if it is not running, then a wait for a new "Logged On" in Steam's
  connection log (only the state word is read: the lines also hold the account's number), at most
  three minutes; then `launch`. The client is found by its process name (`steam`; its command line
  is `./steam -silent ...`, so a match on the path found only the web helper).
- `steam-push.sh` installs `steam-library.py` with the rest, in its one archive.
- Lomiri accepts notifications on the freedesktop interface (`gdbus ... Notify` answered with an
  id).

## First tries, PC attached (2026-11-20, 17:34-17:42)

Hidden Steam stopped beforehand. The maintainer, phone upright: the six games are in the app
drawer. **Ys: Memories of Celceta** from its entry: the job started Steam (17:34:31), waited for its
sign-in, launched the game (its prefix's start 17:34:49); played and quit, `exit 0`. **ATRI** and
**Ys IX** then started from their entries with Steam already running, and quit correctly.

- Each job ended "failed" (status 1) although its game ran: `launch` ends by looking for a Steam
  window to close on the hidden screen, and when there is none, its last search fails. Fixed:
  `launch` returns 0 after that loop.
- **The notification did not show**, neither the test nor the job's, although Lomiri's
  notification server ("Lomiri notification server" 1.2) accepted both (it returned an id). Its
  capabilities include urgency, icons and `x-lomiri-*` hints. Three more tests with the maintainer
  watching (17:45: A urgent, B with an icon, C with an icon and Lomiri's hints): none showed. At the
  first, Lomiri logged `Notifications.qml:53: TypeError: Cannot read property 'width' of null` and
  two more errors in `Notification.qml`: Lomiri receives the notification and fails while drawing
  it. Whether that is every notification on this phone or only those from a process that is not a
  Lomiri app is not known yet. **Parked**: the call stays (harmless; it shows once Lomiri draws
  it); the wait is silent for now, and stage 4's library, a real app with a window, can show its
  own "starting" screen.

## Events

- 2026-11-20: 3.8 started (3.3 closed); plan written.
