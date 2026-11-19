# Plan change: Steam in stage 3

Written 2026-11-19, during [stage 3.2](stage3.2-games.md). Decided by the maintainer: part of
stage 7 (Steam) moves forward into stage 3, so that the compatibility table can be filled with
games the maintainer owns.

Terms used here:
- *Steam client* = Valve's program that logs in, keeps the library, downloads games and starts
  them. Games that use Steam's DRM or its online functions (Steamworks) need it running.
- *compatibility tool* = what Steam starts a Windows game with on Linux (Proton, or a custom one
  placed in `compatibilitytools.d`).
- *`lsteamclient`* = Proton's bridge: a Windows DLL that a Windows game's `steam_api.dll` talks
  to, which passes the calls on to the Linux Steam client. Without it a Windows game in Wine finds
  no Steam.
- *Steam Linux Runtime (SLR) / pressure-vessel* = the container Steam starts games in (stage 0:
  user namespaces work on stone, so it is possible).
- *Steam emulator* = a replacement `steam_api.dll` that pretends Steam is there; a DRM bypass.

## Why

- **The table needs real games, and the maintainer's games are on Steam.** The maintainer does
  not usually buy DRM-free games, and DRM-free games with recent graphics APIs (Direct3D 11 and 12)
  are hard to find. The copies tried in 3.2 all carried DRM bypasses and were not tested
  ([stage3.2-games.md](stage3.2-games.md)).
- The Steam library already exists from ARMDeck (Tiny Rails, Slime Rancher, INSIDE, Firewatch, Hue,
  The Forest, Subnautica, Content Warning, Isaac, Undertale, Geometry Dash, Muffin Knight, Terraria,
  A Story About My Uncle, Poppy Playtime, Tomb Raider and others; see `docs/compat-perf-audit.md`).
  Unity, Unreal 3 and 4, XNA, GameMaker, Direct3D 9 and 11, 32 and 64-bit: the spread the table
  needs.
- Steam is also the store most UTDeck users will have, so it was going to be needed anyway (stage
  7). Doing the part that lets games start now means stages 3.3-3.6 are measured on the games
  people will play.

## What changes in the plan

| | Before | After |
| --- | --- | --- |
| Stage 3.2 | The game list (games without Steam) and the first table | **3.2 is closed with no table entries**; its tools (frame statistics, screen-shaped modes, the game launcher, the d3dx9 installer) stay |
| New **stage 3.S** | (none) | **Steam, far enough to test games**: the client running on Ubuntu Touch, logged in, Windows games downloaded and started through UTDeck's runtime with Steam's API working |
| New stage 3.2b | (none) | The game list and the first table, **from the Steam library** (same exit: five or more games) |
| 3.3-3.6 | Measured on 3.2's games | Measured on 3.2b's Steam games |
| Stage 7 | GOG, itch.io, Epic, Steam | Steam's remaining parts stay there: launching from UTDeck's own library (stage 4), Big Picture or a touch-friendly mode, cloud saves, updates in the background, the other stores |

Stage 3's exit stays as it was: the runtime builds from a script (done, 3.1), and the table has ten
or more games.

## The ways to run Steam, and what is known about each

### A. Valve's ARM64 Linux client, with UTDeck's runtime as its compatibility tool (recommended)

The Steam client runs natively (ARM64, Linux); Windows games run in **our** runtime, registered
as a custom compatibility tool, with Proton's `lsteamclient` added so games find Steam.

- Known from ARMDeck (`docs/gaming-stack.md`, `docs/verification-log.md` 5.2): the ARM64 client
  exists (an unannounced automated build, channels `steamdeck_publicbeta` and `steamdeck_stable`),
  downloads directly from Valve, logs in, and starts Windows games through Proton 11 ARM64 with FEX.
  There it ran in a Fedora container (glibc 2.43) under gamescope. Drakulix's notes on
  postmarketOS: it needs SteamRT4 arm64 as its own runtime and some hand-made plumbing.
- What is new here: Ubuntu Touch's glibc is 2.39 and there is no distrobox/podman on the read-only
  system; Lomiri gives such programs Xwayland (X11 inside Wayland), so the client's interface would
  be drawn in software, like DroidDeck's desktop.
- Why our runtime and not Valve's Proton as it is: Valve's Proton would draw through X11
  (`winex11`), where our Turnip cannot present (route B presents only through Lomiri's Wayland
  buffers), or, with `PROTON_ENABLE_WAYLAND=1`, through a Wayland driver that requires
  `wp_viewporter`, which Lomiri does not have (stage 2.2's patch 0001 is exactly that fix). Valve's
  Proton is also built for Steam Runtime 4 (glibc 2.41, stage 3 roadmap). Our runtime already has
  all eight patches and is measured.
- What has to be built: Proton's `lsteamclient` (and its small `steam.exe` helper) against our Wine.
  Both are in Proton's source with an aarch64 Unix side (Proton 11's `Makefile.in` builds them for
  arm64), so this is a build job, not new code. This also feeds 3.3: if Proton's Wine becomes the
  base, `lsteamclient` comes with it.
- Whether the game runs inside Steam's container (SLR 4 arm64) or directly on Ubuntu Touch, as our
  runtime does now, is a choice a custom compatibility tool can make; directly is simpler and keeps
  our driver environment as it is.

### B. Valve's ARM64 client with Valve's Proton ARM64 as it is

The least work on paper, and it is what ARMDeck uses. On Ubuntu Touch it fails at the picture for the
reasons in A (X11, or Wayland without `wp_viewporter`), and Proton ships its own Wine we cannot
patch without building it. Kept only as a quick check, for example to see whether Steam's own
download and container machinery works here.

### C. The Windows Steam client inside our Wine

Steam for Windows installed into a prefix; games start from it in the same prefix, and Steam's API
works as on Windows with nothing to bridge.

- Simple integration: no `lsteamclient`, no compatibility tool, the runtime stays exactly as
  measured.
- Heavy: the client and its Chromium-based web views (`steamwebhelper`) are x86-64 code that would
  all run through FEX, with their own memory (likely 1 GB or more) and CPU cost, on a phone that
  has 7.4 GB and two big cores. Chromium under Wine usually needs its GPU use and sandbox turned off.
- Unknown on ARM64EC Wine: not measured by anyone we follow.

Fallback if A fails at the client or at `lsteamclient`.

## Steps of stage 3.S

| Step | What | Exit |
| --- | --- | --- |
| 1 | The ARM64 Linux client on Ubuntu Touch: download from Valve (checksums), unpack into a folder (nothing installed system-wide), start through Xwayland | The login window on the phone's screen, or the reason it cannot start (then C) |
| 2 | Log in (the maintainer, on the screen; Steam Guard) | The library is listed |
| 3 | UTDeck's runtime as a compatibility tool (`compatibilitytools.d`, a `toolmanifest.vdf` and a start script that sets our driver environment, the landscape monitor, MAILBOX and the frame statistics) | Steam offers it and downloads a Windows game with it (games kept in `~/Documents/UTDeck/Steam`, 60 GB free) |
| 4 | A game without Steam DRM started from Steam through our runtime | It runs, measured with `game-run.sh`'s sensors |
| 5 | Proton's `lsteamclient` and `steam.exe` helper built against our Wine (`build-runtime.sh` gets a part) | A game that uses Steamworks starts and sees Steam (achievements, the overlay may stay out) |
| 6 | Lifecycle basics: Steam and a game started from the app drawer, closed cleanly, no processes left behind | Starting and quitting twice in a row works |

Then 3.2b: the game list and the table from the Steam library.

## Decision (2026-11-19): hidden Steam first, gamescope later as an option

Found in stage 3.S before this decision ([stage3S-steam.md](stage3S-steam.md)): the ARM64 client
runs and signs in on Lomiri, but its interface is drawn in software (Lomiri's Xwayland has no GPU
path: no `/dev/dri` device on Halium, no dmabuf sharing in Mir 1.8's Android backend), at 2-5 fps and
220 % CPU, with about 2 GB of memory while the library window is open. Valve's accelerated path is
the gamescope composer (Steam Frame, Steam Deck).

| | **gamescope** (DroidDeck's model) | **hidden Steam** (UTDeck's own frontend) |
| --- | --- | --- |
| What runs | gamescope as one Lomiri app; Steam's Gaming Mode (Big Picture) inside it, on the GPU; games composited by gamescope | the client started without a window (`-silent`) as a user service; UTDeck's library drives it (`steam://install/<appid>`, `steam -applaunch <appid>`); games present to Lomiri directly, as in stage 2 |
| Steam's interface | fast, made for touch and controllers, its own keyboard | slow (software) the few times it is shown |
| Built in | overlay, performance display, frame limiter, Steam Input settings, scaling, per-game settings | UTDeck builds its own (stages 4-5) |
| Other stores | non-Steam shortcuts inside Steam | one UTDeck library for all |
| Cost per game frame | one more composition pass over the whole screen on the A619, a little latency | none beyond stage 2's |
| Memory while a game runs | Gaming Mode's interface stays loaded, plus gamescope and its Xwayland | client and a web helper with no window: less (to measure) |
| Work | large: gamescope on Lomiri (not done anywhere), GPU buffer sharing on KGSL, touch patches | small for games: hidden start, a service Lomiri does not pause, `lsteamclient`; the library is stage 4 work anyway |
| Risk | leans more on Valve's unannounced ARM64 build and its Steam Frame assumptions | leans on `steam://` URLs and Steam's files, stable for years |

When only Steam games run, hidden Steam costs less memory and less CPU and GPU time: both keep the
client and web helper alive (Steam's DRM needs them), gamescope adds itself, keeps Gaming Mode's
interface loaded and composites every frame.

The user can avoid Steam's interface almost entirely: sign in once with the QR code (slow is fine),
buy on the Steam app on another phone, install by the Steam app's remote download or UTDeck's
`steam://install`, play from UTDeck's library (`-applaunch`, our runtime set as Steam's default for
Windows games). Steam still shows its own windows in rare cases UTDeck has to cover: games with
several launch options, some first-start agreements, cloud-save conflicts, sign-in again when the
login token expires.

**Decided by the maintainer: hidden Steam is UTDeck's path; gamescope may come later as an optional
"Steam Gaming Mode", measured against it on real games.**

Also found: Lomiri pauses (SIGSTOP) every process of an app that is not in front, Steam included.
Games need the client awake, so Steam runs as a user service, outside Lomiri's app handling.

## Rules for this part

- **Only games the maintainer owns, through Steam itself.** No Steam emulators, no cracked
  executables, no DRM bypass of any kind (stage 3 rule, and 3.2's lesson). Every game folder is
  checked for bypasses before its first start.
- **The account is the maintainer's alone.** The maintainer types the login and Steam Guard codes
  on the phone. No password, code, token or account name goes into a log, a note, a commit or a
  command line; logs that Steam writes stay on the phone and are read, not copied, where they
  might hold account data. Steam's own files (`config.vdf`, `loginusers.vdf`) are never copied off
  the phone.
- Steam is unpacked into a folder in the user's home, as the runtime is; nothing system-wide.
- Downloads are large: the phone has 60 GB free; each game's size is checked before it is
  downloaded.

## Risks

| Risk | Likelihood | What we do |
| --- | --- | --- |
| The ARM64 client will not start on glibc 2.39 or outside a container | Medium | Its own runtime (`steamrt3c` or SLR 4 arm64) first; then C |
| Its interface is too slow in software through Xwayland | Medium | Small mode, the downloads page only; later a UTDeck library page (stage 4) starts games without the client's interface |
| Valve changes the unannounced ARM64 build | Medium | Pin the version that works; `tools/check-sources.py` watches the channels |
| `lsteamclient` needs more of Proton's Wine than ours has | Medium | Port what it needs, or decide 3.3 for Proton's Wine |
| Memory: Steam (about 0.5-1 GB) plus a game on a 7.4 GB phone | Low | Measured; Steam's web views closed while a game runs |
