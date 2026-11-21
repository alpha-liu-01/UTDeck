# Stage 4.2: first-time setup from the app, with a QR code sign-in

Started 2026-11-21. Plan: [stage4-roadmap.md](stage4-roadmap.md). Goal: on a phone with nothing
but UTDeck's click, the app sets up everything a game needs, with progress shown, and the user
signs in to Steam by scanning a QR code, with no password, Steam Guard code or token passing
through UTDeck. Tested on the **Nothing Phone (1)** (Snapdragon 778G+, Adreno 642L, Ubuntu Touch
24.04-1.x), which has the 1.x click installed (4.1) and no Steam.

Why first (the maintainer, 2026-11-21): every later substage needs a phone where Steam works, and
on a fresh phone that took a PC, adb and six scripts. Why a QR code: UTDeck never handles a
password, and Steam's own interface (a web view with no GPU acceleration here, and little memory
to spare) stays away from the user.

## What setting up Steam takes today (stone, by hand over adb)

| Step | Today | In the app |
|---|---|---|
| UTDeck's scripts and compatibility tools | `steam-push.sh` from the PC | carried in the click, copied on first start |
| The Wine runtime | `runtime-push.sh` from the PC | `get-runtime.sh` (downloads a published release and checks it) |
| The hidden screen (Xvfb: a virtual X11 screen in memory) | Ubuntu's `xvfb` package downloaded and unpacked by hand (stage 3.S) | the same, run by the app |
| Steam Runtime 3 and the client | `steam-install.sh 1`, then `2` (the client's first self-update, on Lomiri's Xwayland) | the same, with progress; whether `2` can run on the hidden screen is a question for step 1 |
| Sign-in | Steam's own window from a test drawer entry (stage 3.S, M11) | the QR code in the app; Steam's own window as the fallback |
| The background service | `steam-service.sh start` | started by the app |

## The QR code sign-in

Steam's "Sign in to Steam" window shows a QR code next to the name and password fields. Scanned
with the Steam phone app (and approved there), it signs the client in; the client keeps its own
token, as after M11. Plan:
1. Hidden Steam, not signed in, opens that window on its hidden screen.
2. UTDeck captures the hidden window and reads the QR code, and shows it natively in the app,
   again whenever Steam renews it.
3. The user scans it with the Steam phone app and approves; Steam signs itself in; UTDeck sees
   hidden Steam signed in and goes on.

What UTDeck sees: only the QR code's content, a one-time sign-in link (scanning it signs in the
scanner's account; it holds nothing of the user's). Kept in memory only: never logged, never
written to a file. Captures of the hidden screen stop once Steam is signed in (later windows can
show the account name). **The fallback**: see "The fallback, decided again" below (B, not A).

## Plan

1. **Probe on the Nothing Phone** (over adb, with the existing scripts, which the app will run
   too): Xvfb, Steam Runtime 3 and the client installed; hidden Steam started, not signed in. Does
   the sign-in window open on the hidden screen, with a QR code? Can it be read from a capture (and
   how: decoding it, or showing the captured code as it is)? How often does it change? Memory and
   CPU while it shows. Can the client's first update (`steam-install.sh 2`) run on the hidden screen
   too? Then the maintainer scans it: does the client sign in? What our scripts assume about stone
   (paths, sensors, the GPU) shows up here too.
2. **The pieces in the click**: the scripts and compatibility tools, the capture tool; one setup
   script with steps and progress the app can read, run as a systemd user unit (so it is not paused
   with the app).
3. **The setup page** in the app: each step, its progress, its errors, retry.
4. **The sign-in page**: the QR code, renewed; signed in detected; the fallback (B: the hidden
   sign-in window inside the app, taps and key presses passed on).
5. **From scratch on the Nothing Phone**: the click alone, no PC; once with Steam skipped, once with
   Steam set up and signed in by QR code; the fallback seen working once; the Steam switch off and
   on.

## 1. Probe on the Nothing Phone (2026-11-21)

- **Xvfb without apt**: the phone has no apt package lists (`/var/lib/apt/lists` is empty, and
  filling it would write to the system), so `apt-get download xvfb` fails there; stone had lists
  from earlier. Instead the package is fetched straight from Ubuntu's archive by its name and
  checked against the SHA256 of the archive's index, as `steam-install.sh` does for Steam:
  `xvfb_21.1.12-1ubuntu1.9_arm64.deb` (noble-updates, universe,
  `pool/universe/x/xorg-server/`, SHA256 `15827310...c5ad42c`). Unpacked into
  `~/utdeck-probe/xvfb/root`: every library it needs is on the phone (`ldd`: none missing), and so
  are `xkbcomp`, `xauth`, the keyboard data and `protocol.txt`. That is how the app's setup gets it.
- **UTDeck's Steam pieces**: `steam-push.sh` (with `ANDROID_SERIAL`) as on stone; SDL3's checksum
  matches the build.
- **Steam Runtime 3 and the client**: `steam-install.sh 1` (download and checks) in about a
  minute, 553 MB, both checksums OK.
- **The client's first update on the hidden screen**: Xvfb started as `steam-service.sh` does
  (unit `utdeck-xvfb`, `:50`, 1200x540), then `DISPLAY=:50 steam-install.sh 2`: "bootstrap OK" after
  129 s, 3.3 GB, without any window on the phone's screen. (The client exited with 254 there; the
  script's own check, the installed manifest and `steamui.so`, passed.) So the setup needs no
  visible window at all.
- **Hidden Steam, not signed in** (`steam-service.sh start`): about 10 s later a 700x440 window is
  mapped on the hidden screen, Steam's "Sign in to Steam", with the QR code ("Or sign in with QR",
  "Use the Steam Mobile App to sign in via QR Code") next to the name and password fields.
- **The QR code is readable from a capture** of the whole hidden screen (`xwin.py --shot`, decoded
  with ZXing-C++'s Python package on the PC): one QR code, a 38-character `https://s.team/...` link,
  found without knowing where the window is. Only its start and length were printed, and a hash
  for comparing captures.
- **Steam renews it every 15-30 s** (nine captures 15 s apart: seven different links), so the app
  re-reads it every couple of seconds.
- **Cost at the sign-in window**: 839 MB (PSS, 13 processes), 19 % of one core; 5 GB free.
- **The sign-in by QR code (the maintainer, 17:24)**: the current code, cropped from the capture,
  enlarged and rewritten every 3 s into `build/shots/` (git-ignored), was scanned with the Steam
  phone app and approved. Steam's connection log went to "Logged On" (only that state word is read)
  7 captures into the second try (the first 4-minute window passed before the maintainer got to
  it); the sign-in window closed and no other window opened. Captures stopped there (later windows
  can show the account name); the code's picture and the sign-in form's captures were deleted.
- **Signed in, a minute later**: 1407 MB in 11 processes, 43 % of one core (right after the
  sign-in; stone settled at 1.33 GB and 13 %); Steam's folder 3.7 GB.

**So the QR route works end to end**, on a 24.04-1.x phone, with no password, code or token
passing through UTDeck and no Steam window on the phone's screen. What the app needs for it:
a capture of the hidden screen (libX11's `XGetImage`, which the phone has), a QR reader and writer
(ZXing-C++, Apache-2.0, bundled in the click: it reads the code from the capture and draws it
natively), and the state word of Steam's connection log.

## The fallback, decided again (2026-11-21)

Fallback A (Steam's own sign-in window on the phone's screen, as M11) cannot be typed into with
Lomiri's on-screen keyboard (X11 programs do not receive it), and touches on its right half land on
the middle line (M11, stage 3.S step 2b, parked). So it works only with a hardware keyboard. **The
maintainer chose B as the fallback**: hidden Steam's real sign-in window shown inside UTDeck, taps
passed to it, and what is typed with Lomiri's keyboard passed on as key presses (X11's test
extension, XTest). Valve's window still does the sign-in; UTDeck passes keystrokes on and keeps
nothing (not in a variable longer than the key press, not in a log). Built after the QR page works.

## 2-4. Built (2026-11-21)

- **`tools/ut-probe/utdeck-setup.sh`** (`run`, `check`): the steps `files`, `xvfb`, `runtime`,
  `steam`, `service`, `signin`, each with a "done" test (so `run` goes on where it stopped), its
  state in `$XDG_RUNTIME_DIR/utdeck/setup/STEP` ("STATE DETAIL", written whole), its output in
  `~/utdeck-probe/setup-STEP.log`, and, while a download or unpacking runs, the size so far every
  3 s. Xvfb by name and SHA256 from Ubuntu's archive; the client's first update on the hidden screen
  (`steam-service.sh xvfb`, a new command that starts only Xvfb); the sign-in step waits up to 30
  minutes for "Logged On" (the state word only).
- **`tools/ut-probe/home-files.sh DEST`**: every UTDeck file a home folder needs, at its place
  (`steam-push.sh`'s and `runtime-push.sh`'s lists); `build-click.sh` puts the tree and the app's
  version into the click's `setup/`, and the `files` step copies it into the home folder.
- **`get-runtime.sh default`**: the default runtime too (into `wine/runtime-<version>`, `wine/runtime`
  pointing at it); a file already in `wine/dl` with its `.sha256` is used instead of a download, and
  both are removed once installed. The default runtime is not published yet (only Proton's is), so
  a phone with no PC cannot download it yet: **a GitHub release of it is needed** (the maintainer's
  call, asked before step 5).
- **The app** (0.2.0): `Setup` (`check` at start, `start` runs `utdeck-setup.sh run` as the systemd
  user unit `utdeck-setup`, the steps' files followed through `QFileSystemWatcher` and a 1 s timer);
  while the sign-in step runs, hidden Steam's screen (`:50`) captured every 2 s with libX11's
  `XGetImage` and read with ZXing-C++ off the UI thread, the code drawn again natively (one pixel
  per module, scaled up in whole steps) and offered as `image://qr/<revision>`; only the drawn code
  and a hash of its text are kept, nothing once signed in. `SetupPage.qml` (Lomiri Components): the
  steps with a spinner, a tick or a warning and their detail; "Set up" or "Try again"; while signing
  in, the QR code with how to scan it. The app opens on it when `check` finds a step not done, and
  goes to the library when every step is done.
- **The cross container** gets `libx11-dev`, `libxtst-dev` and `libzxing-dev` (arm64); the click
  bundles `libZXing.so.3` (2.2.1; it needs only the C and C++ runtimes). libX11 and XTest are on
  every 24.04 phone (both checked on the Nothing Phone, which also has ZXing).

### Manual run S1 (2026-11-21, the Nothing Phone): the setup page, with the probe's leftovers

`check` found `files` (the probe's files had no version stamp) and `runtime` (the probe installed no
Wine runtime) not done, everything else done; the runtime file was placed in `wine/dl` for the
test. The maintainer: the app opened on "Set up UTDeck", "Set up" turned every step green, and the
app switched to the library by itself. Checked over adb: `check` all done, the files stamped
`0.2.0.1x`, `wine/runtime` -> `runtime-11.16-20261121` (1.2 GB, with patches 0017 and 0018), and
`wine --version` answers `wine-11.16 (Hangover)`: **the default runtime runs on a second phone, on
24.04-1.x**. Not tested by S1: the downloads of Xvfb and Steam from the app, and the QR page (that
phone's Steam was already signed in).

## Steam optional (decided 2026-11-21, the maintainer)

The Wine runtime runs games without Steam (3DMark 11 and the test programs did in stage 3), another
store may come later, and the user can always bring their own games. So:
- **Mandatory**: UTDeck's files and the Wine runtime.
- **Steam, optional**: the hidden screen (Xvfb serves only hidden Steam), Steam, starting it, the
  sign-in. After the mandatory part the setup offers "Set up Steam" or "Skip"; skipped, Steam can be
  set up later from the settings. About 3.7 GB less on disk, and 1.3-1.4 GB less memory when it would
  be running.
- **A Steam switch** in the settings: off stops hidden Steam and keeps it from starting with the
  app; a Steam game's Play starts it on demand (`steam-service.sh play` does already), and it is
  stopped again when that game ends. On, the app starts it when it opens, so that downloads from the
  Steam phone app and game starts are immediate.
- **The user's own games** (a folder, the `.exe`, a cover, a prefix each): substage 4.3, right after.

Plan, before step 5: (2b) the setup split in two, with the choice; (2c) the settings page with the
Steam switch and "Set up Steam"; the fallback B. Step 5 then runs the setup both ways.

## 2b-2c. Steam optional, built (2026-11-21)

- **`utdeck-setup.sh run`** does the mandatory part (`files`, `runtime`), **`run steam`** adds Steam's
  (`xvfb`, `steam`, `service`, `signin`); `check` reports all six.
- **The app** (0.2.1): the choice ("use" or "skip") and Steam's switch kept with `QSettings`
  (`~/.config/utdeck.alphaliu01/utdeck.alphaliu01.conf`); Steam installed counts as "use". The setup
  page is needed while a run goes on, the mandatory part is not done, or Steam is neither set up nor
  skipped (or chosen and not installed). The page shows the mandatory steps and "Set up"; once they
  are done, a "Steam (optional)" card with "Set up Steam" and "Skip"; Steam's steps only once it is
  chosen. A step file's "done" counts only while a run goes on (otherwise the check decides: a
  "service done" from an earlier run is not proof Steam runs now).
- **Settings** (the library's header, a gear): Steam not set up: "Set up Steam". Set up: the switch
  "Run Steam in the background" (off stops hidden Steam now, `steam-service.sh stop`, and keeps it
  from starting with the app; on starts it), and Steam's state (running, signed in), with "Sign in"
  when it runs but is not signed in. With the switch off, a Steam game's Play still starts hidden
  Steam (`steam-service.sh play` did already), and the app stops it when the game ends. With it on,
  the app starts hidden Steam when it opens.
- 0.2.2: Steam's state is checked again 5, 15 and 30 s after a switch, since Steam signs itself in
  10-20 s after starting (until then the page would offer "Sign in" for nothing).

### Manual run S2 (2026-11-21, the Nothing Phone): Steam set up and signed in from the app

Before: hidden Steam stopped, then (the maintainer's OK) `~/.local/share/Steam` (4.0 GB, with the
probe's sign-in), `~/.steam`, `~/utdeck-probe/xvfb` and the files' version stamp deleted; `check`:
`files` and every Steam step todo, the runtime done. The maintainer: "Set up", then "Set up Steam";
every step green; **signed in by scanning the QR code drawn by UTDeck**; the app switched to the
library; Settings showed Steam running. Over adb: `check` all done, the choice `use` stored. **The
QR sign-in works end to end from the app, on 24.04-1.x**, Steam downloaded and updated with no
window, no adb step in between.

### Manual run S3 (2026-11-21, the Nothing Phone): Steam's switch

The maintainer: off stopped Steam, and it stayed off after the app was closed and opened again; on
started it again, **but "Sign in" stayed and Steam never seemed to sign in**. Steam's connection log
(state words only): started 18:04:13, "Logged On" at 18:05:19, **66 s**; the start before (when the
app opened, 18:01:07) took 59 s, about 50 s of it "Connecting" before Steam reached Valve at all.
So Steam did sign itself in, a minute later; the app's last check, 30 s after the start, came too
early, and nothing checked again. 0.2.3: after the app starts Steam it checks every 5 s until Steam
is signed in (at most 3 minutes, or until it is found not running 20 s after the start), and
Settings says "Steam is starting and signing in (about a minute)" meanwhile, with no "Sign in".

Repeated with 0.2.3 (the maintainer): off, then on; Steam signed itself in and no "Sign in" button
showed. **S3 passed.**

Installing 0.2.3 stopped the open 0.2.2 first ("stopping lomiri-app-launch--...0.2.2.1x--"): the
first time `install-click.sh`'s stop step met a running copy; the install went through.

## Fallback B, built (2026-11-21)

- **XScreen** (`app/src/XScreen.cpp`): a click and typing through X11's test extension (XTest, the
  `libXtst` every 24.04 phone has): each character pressed as its keysym (Latin-1 characters are
  their own, the rest 0x01000000 + the code point), with Shift when it is a key's second symbol; a
  character missing from Xvfb's keyboard map gets a key with no symbol for that one press and is
  taken off it again. Xlib's default error handler (which ends the program) replaced for these
  too.
- **Setup**: `setFallback` (only while the sign-in step runs): hidden Steam's screen captured twice a
  second and offered as `image://steamscreen/<revision>`; `tap` (fractions of the screen), `type`,
  `key` (only BackSpace, Return, Tab). The picture is dropped when the view closes or the sign-in
  step ends; what is typed is passed on and kept nowhere (no variable outlives the call, nothing is
  logged).
- **The page**: under the QR code, "Can't scan the code? Use Steam's own sign-in window"; the view
  fills the page: the screen, scaled to fit (a tap maps to the same place on Steam's screen), a text
  field (`ImhSensitiveData`, no suggestions, no auto capitals: Lomiri's keyboard learns nothing;
  each character is sent and the field emptied), Delete, Tab, Enter, "Back to the QR code".
- **0.2.5**: with no saved sign-in, Steam opens its sign-in window about 10 s after starting
  instead of signing in; the watch after a start would have said "starting" for 3 minutes. From 15 s
  on it now looks for that window's QR code on the hidden screen (found: "not signed in", with
  "Sign in", at once).

### Manual run S4 (2026-11-21, the Nothing Phone): fallback B

Before (the maintainer's OK): hidden Steam stopped and `~/.local/share/Steam/local.vdf` (1.2 KB,
Steam's saved sign-in for that phone) deleted, nothing else. The maintainer: Settings, "Sign in",
the QR code shown but not scanned, "Can't scan the code?": **signed in with account name and
password typed through UTDeck into hidden Steam's own window**, Steam Guard answered the same
way; the app went back to the library. **S4 passed: both sign-in paths work.**

## Events

- 2026-11-21: 4.2 planned (the substages reordered: setup first, sign-in by QR code).
- 2026-11-21: step 1, the probe on the Nothing Phone: Xvfb from Ubuntu's archive (no apt lists on
  1.x), Steam installed with no visible window, the QR code read from the hidden sign-in window, and
  the maintainer signed in by scanning it.
- 2026-11-21: steps 2-4 built (the setup script, the files in the click, the app's setup page and QR
  reading); S1 on the Nothing Phone: the setup page installed the missing files and runtime and
  handed over to the library; Wine runs there.
- 2026-11-21: Steam made optional (the maintainer): the setup's mandatory part is the files and the
  runtime; Steam is offered and can be switched off; the user's own games are 4.3.
- 2026-11-21: Steam optional built (the setup in two parts, the choice, Settings with Steam's
  switch); S2 on the Nothing Phone: Steam set up from the app and signed in by the app's QR code.
- 2026-11-21: S3: the switch works; Steam takes about a minute to sign itself in on the Nothing Phone,
  so the app now watches it after a start (0.2.3).
- 2026-11-21: S3 passed with 0.2.3.
- 2026-11-21: fallback B built (0.2.4-0.2.5); S4: signed in with name and password through hidden
  Steam's own window inside UTDeck.
