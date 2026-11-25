# Stage 4.10: the game round, the plan

Parked 2026-11-24 until after stage 5.3 (the maintainer: Ys IX had run into memory trouble), taken
up again 2026-11-25. Goal (from [stage4-roadmap.md](stage4-roadmap.md)): three or more new games
through the app, bringing the game table to ten; stage 3's leftovers for a game round; a keyboard and
a mouse tried in each game. Exit: the table at ten games, and stage 4's exit run by the maintainer
(from a fresh install: add a game, start it, play with a gamepad, quit back to the library). New since
it was parked: the performance overlay (5.1) writes each game's numbers down as it is tried.

## The games

The table had seven, all measured on stone in stage 3 ([stage3-roadmap.md](stage3-roadmap.md)): Ys:
Memories of Celceta, Ys IX, ATRI, Plants vs. Zombies: Replanted, Stray, Goat Simulator, Aperture Desk
Job. New, on the Nothing Phone (1), each folder checked for DRM bypasses first (`gamecheck.py`, stage 3's
rule):

| Game | What it is | DRM-bypass check | First try (the maintainer, 2026-11-24/25) |
| --- | --- | --- | --- |
| Stardew Valley | MonoGame (OpenGL), .NET, 64-bit | (Steam's own copy; tried in 5.1/5.2) | plays (5.1: about 51-55 fps, through zink) |
| Celeste | .NET Framework, 32-bit (`Celeste.exe` imports only `mscoree.dll`), FMOD | none; `steam_api.dll` signed by Valve | **freezes a few seconds into the game on our Wine base; stable 60 fps on Proton 11's** |
| Need for Speed Most Wanted (2012) | `NFS13.exe`, 32-bit; EA's `Core/ActivationUI.exe` (Qt 4, WebKit) and the EA app's installer (`EAappInstaller.exe`, 263 MB) | none; the overlay's `steam_api.dll` signed by Valve | **does not start**: the EA app and its sign-in stand before the game, and could not be seen or used |

That makes ten. On the way: Steam installed Proton Experimental, its FEX and Steam Linux Runtime 4.0
by itself (about 2.2 GB, unused by UTDeck): a leftover for 5.4.

## What each needs first

- **Celeste**: why it freezes on our base and not on Proton's. Both bases use the same runtime
  otherwise, so the difference is in Wine (Hangover's 11.16 against Proton's 11.0, with Valve's game
  fixes) or in .NET's path (Wine Mono, its version, the 32-bit translator): the two runs' logs side by
  side, then the frozen process (its threads, what it waits on).
- **Need for Speed Most Wanted**: the EA app is a launcher of its own (EA's successor to Origin), a
  Chromium-based program with a sign-in to an EA account; Steam's install script runs its installer,
  and the game will not start without it running and signed in. Known as one of the hardest
  launchers under Wine (Proton carries fixes for it). First: what was installed and what runs (the EA
  app's processes, their windows, their logs), on our base and on Proton's. **Only the maintainer signs
  in; no account data goes into any log or note** (the same rules as Steam's).

## Celeste on our base (2026-11-25)

Its runs on the Nothing Phone (1): on our base the frame log ends after 1 and 4 seconds (60 fps until
then; the maintainer ended both, `exit 143`); on Proton's, 291 seconds, `exit 0`. Our base's log ends in
a stream of `err:virtual:allocate_virtual_memory out of memory for allocation` and Wine Mono's
`Assertion at .../wine-mono-11.3.0/mono/mono/utils/mono-mmap-windows.c:116, condition 'aligned' not met`:
**Celeste ran out of address space**, not of RAM. A 32-bit Windows program gets 2 GB of address space
unless its executable is flagged "large address aware" (4 GB); `Celeste.exe` is not. Proton's Wine
counts every 32-bit program as large address aware unless `WINE_LARGE_ADDRESS_AWARE=0`
(`need_override_large_address_aware`, on by default, in its `dlls/ntdll/unix/virtual.c`; Proton turns
it off only for a few games); Hangover's follows the program's flag (`user_space_wow_limit = limit_2g`).

The fix: Wine patch **0021** (`ntdll: 32-bit programs large address aware by default, as on Proton`):
the same switch with the same default in our Wine, keeping upstream's reserved area at the top of the
4 GB range. A game's settings can turn it off (`WINE_LARGE_ADDRESS_AWARE=0`).

Tested on the Nothing Phone (1) with only `ntdll.so` swapped into the runtime (the one file 0021
changes; the old one kept as `ntdll.so.before-0021`):
- **0021, Box64** (our default for 32-bit code): the address-space errors are gone (0 against 281), but
  Celeste still stopped after 2 seconds, now with Mono's thread-state check failing: `Cannot
  transition thread 00000158 from RUNNING with DONE_BLOCKING`. (Right before it, every run, on both
  bases, logs `steamclient:callback_len_utow Unix len 12 not found for callback 1112`: our Steam bridge
  cannot convert one of Steam's messages; harmless, Proton's base shows it too.)
- **The difference between the bases** was also the 32-bit translator: Hangover's Wine uses Box64 by
  default (`wowbox64.dll`, `dlls/wow64/syscall.c`), Proton's Wine on ARM64 uses FEX
  (`libwow64fex.dll`). Both bases bring the same Wine Mono (11.3.0).
- **0021 and FEX** (Celeste's page: "32-bit code" FEX, base Default): it plays, 95 seconds at 58.6 fps
  on average, no errors.
- **FEX without 0021** (the maintainer's earlier try): it stopped too, later, on the title screen. So
  Celeste needs both: the address space (0021) and FEX (Box64 trips Mono's thread-state check).

Open: whether FEX should be the default for some or all 32-bit games. It is stage 3.4's leftover K9
(Box64 or FEX for 32-bit games), now with a game that only runs on FEX; Box64 was chosen for speed on
32-bit games that ran on both (stage 3.4).

**The maintainer's choice (2026-11-25): FEX automatically for 32-bit .NET games** (option b; FEX for
all 32-bit games, as on Proton, waits for stage 3.4's K9 comparison on the 32-bit games, none of them on
the Nothing Phone (1) now). `game-run.sh` asks `dotnet32.py` (new: a cut-down copy of `gamecheck.py`'s PE
reader) whether the game's program, the first `.exe` of its command (for a Steam game, the path Steam
passes after `steam.exe`), is 32-bit x86 and imports `mscoree.dll`; if so, and no `HODLL` is set, it
uses FEX (`HODLL=libwow64fex.dll`) and logs "32-bit .NET program (...): FEX for its 32-bit code". Checked
on the installed games: Celeste yes; Stardew Valley (64-bit .NET), Need for Speed, Ys IX, PvZ no. The
game's page's "32-bit code" now has three choices: Automatic (no line), Box64 (`HODLL=wowbox64.dll`,
which overrides the automatic rule) and FEX. App 0.3.56. Patch 0021 goes into a runtime release later,
bundled with what Need for Speed needs (the maintainer).

Checked with 0.3.56 (the maintainer's OK) on the Nothing Phone (1), 0021's `ntdll.so` still swapped in
by hand: Celeste's "32-bit code" back to Automatic (its settings name no translator); the log "32-bit
.NET program (Celeste.exe): FEX for its 32-bit code"; it played (50 s, 56.9 fps on average, `exit 0`).

## Need for Speed Most Wanted (2026-11-25)

**On our base** (00:40): Steam starts the game through `steam2ea://launchgame/1262560`, which hands it
to the EA app (installed by Steam's install script in `C:\Program Files\Electronic Arts\EA Desktop`).
Its window stayed blank; Wine logged `D3D11: Failed to write shared resource info for a texture` and
`err:vulkan:vkGetSwapchainImagesKHR Exception 0xc0000005 in Unix call`: a crash inside Wine's Vulkan
path while the EA app's Chromium draws through Direct3D 11 (DXVK). Not looked into yet; our Wine
patches 0005 and 0006 touch that path. (Wine's own logs only; the EA app's files are not read.)

**On Proton's base** (the maintainer, 01:37): the EA app and its sign-in worked, the game started:
- At 2400x1080 the window was black with the game's sound playing; the picture only changed after a
  trip to Lomiri's app switcher. At 1280x720 it showed normally, without graphical errors.
- Windowed: Proton's patch set has no 0010/0011 (fill), as Ys IX on Proton's base.
- About 18 fps at 1280x720 and its lowest settings, held by one CPU thread: the fastest core (`c7`)
  96-98 % at its 2515 MHz, the GPU about 60 % at 450 MHz. `NFS13.exe` is 32-bit, and Proton's Wine
  runs 32-bit code with FEX by default.

**(D) Box64 against FEX** (the maintainer, 01:52, Proton's base, "32-bit code" Box64, the same spot):
14-18 fps with the overlay on, against about 18 on FEX; the fastest core again 96-97 % at 2515 MHz, the
GPU 66-74 %. And the same frame rate at 800x600, 1280x720 and 2000x900: the game is held entirely by one
CPU thread, its x86 code translated; the GPU does not count. Neither translator helps as it is.
FEX without its x86 memory-order emulation (`FEX_TSOENABLED=0`, stage 3.4's leftover 11; it can break a
game whose threads rely on that order): 14-18 fps again. The cost is not TSO; the game's 32-bit code is
more than one of this phone's cores can run translated at 30 fps. **For the table: runs on Proton's
base, 14-18 fps, CPU-bound (one thread)**; the TSO line taken out again (no gain, and a risk).

**What it needs, one at a time**: (D) Box64 against FEX for its 32-bit code (stage 3.4 chose Box64 for
speed); (C) the black picture at 2400x1080 on Proton's base; (B) fill on Proton's base (0010 and 0011
brought over); (E) the EA app on our base; and for every game (A) the scaling choice in the app: patch
0006 already has `UTDECK_SCALE` = `fit` (the default), `stretch`, `center` (1:1) and `off`, but only a
test script ever set it (the maintainer's question, 2026-11-25).

## (A) The scaling choice in the app (2026-11-25)

Wine patch 0006 (our base) scales a game's picture that is smaller than the screen, by `UTDECK_SCALE`:
`fit` (also when unset: as large as possible, the shape kept), `stretch`, `center` (1:1); anything
else turns the scaler off. Proton's base has its own patch 0006 (Valve's fullscreen hack made to work on
Wayland) and no `UTDECK_SCALE`. Only a test script had ever set it. Now: "Picture scaling" on a game's
page (As in Settings, Fit, Stretch, Centered (1:1)) and in Settings for all games (`user.env`, written
only when not Fit); `game-run.sh` needs no change, as every `UTDECK_*` setting reaches Wine from a
game's settings or `user.env`. App 0.3.57.

Checked with 0.3.57 (the maintainer's OK) on the Nothing Phone (1), Celeste windowed at 1280x720 in its
own video options, its page's "Picture scaling" set to each choice in turn with a restart between
(12:31-12:37): **Fit, Stretch and Centered (1:1) each showed as described** (the maintainer): Fit as
large as the screen allows with bars left and right, Stretch over the whole screen, Centered pixel for
pixel in the middle. Afterwards the game's page back on "As in Settings" (its `utdeck-env` without
`UTDECK_SCALE`) and Settings on Fit (`user.env` without the line). A game's own settings are not
written into its run log (only `user.env`'s are, "user settings:"); the choice is seen on the screen.

## Then

- Each new game with the overlay (Full) for its numbers, a keyboard and a mouse tried in each.
- Stage 3's leftovers for a game round (its list in [stage3-roadmap.md](stage3-roadmap.md)).
- Stage 4's exit run by the maintainer.

## Status

- 2026-11-25: planned (this file); the two new games' folders checked (no DRM bypasses).
- 2026-11-25: Celeste plays (patch 0021 by hand, FEX automatically for 32-bit .NET games, app
  0.3.56); Need for Speed runs on Proton's base at 14-18 fps, CPU-bound; (A) the scaling choice in
  the app (0.3.57), checked with Celeste. Next: (B) and (C), fill and the black picture on Proton's
  base; then (E).
