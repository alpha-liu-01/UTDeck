# Stage 5.3: Steam's memory during a game, the plan

Started 2026-11-24, after 5.2 (the maintainer). Goal (from [stage5-roadmap.md](stage5-roadmap.md)):
hidden Steam's memory given back while a game runs, without breaking Steam. Exit: Steam's memory
during a game measured before and after on both phones; cloud saves, quitting and the next start
unchanged.

## Terms used here

- *PSS* = a process's share of the memory it uses (shared pages divided among their users).
- *anon* memory = a program's own working memory (its heap, its data); only swap can take it away.
  *file* memory = files read into memory (the page cache): the kernel can drop it at any time and read
  the file again when needed. *shmem* = shared memory (`/dev/shm`), which behaves like anon.
- *zram* = swap that compresses memory pages and keeps them in RAM: a page swapped out takes about a
  third of its space, and comes back in microseconds instead of from storage. Costs CPU both ways.
- *cgroup* = the kernel's group of processes for one systemd unit, with its own memory counters and
  controls; hidden Steam has its own (`utdeck-steam.service`).
- *Steam's web helper* (`steamwebhelper`) = the embedded Chromium that draws Steam's interface; on
  our phones nobody sees it (Steam runs on an invisible X screen).

## What we know (2026-11-24, the Nothing Phone (1))

- **Steam idle**: about 1.07 GB PSS: the web helper about 890 MB in 9 processes (the largest 353 and
  292 MB), the client 177 MB. Its cgroup holds 2.16 GB in all: anon 759 MB, file 1.4 GB, shmem 151 MB.
  Stage 3.4: Steam's own options (`-cef-disable-gpu`, `-nofriendsui -nochatui`) saved nothing.
- **What it costs a heavy game**: Ys IX in its town (stage 4.3): 260 MB available, 1.9 GB in swap,
  14-33 fps with the CPU at 54-68 % (swapping); Ys IX itself about 5.3 GB (2.05 GB of it GPU memory,
  which no process list shows).
- **Swap**: 4 GB of zram (`/dev/block/zram0`), almost unused when idle; `swappiness` 100.
- **The kernel is 5.4** (`5.4.289-qgki`): no `memory.reclaim` (the cgroup control the roadmap named,
  from kernel 5.19). What it has instead:
  - **`/proc/<pid>/reclaim`** (`CONFIG_PROCESS_RECLAIM=y`, a Qualcomm/Android addition): writing
    `anon`, `file` or `all` pushes that process's pages out at once (anon into zram, file dropped);
    phablet may write it for its own processes.
  - **cgroup v2 with the memory controller** (`CONFIG_MEMCG=y`, `MEMCG_SWAP`): Steam's group's
    `memory.high` belongs to phablet; lowered, it makes the kernel reclaim the group down to it and
    hold it there (the 5.4 stand-in for `memory.reclaim`), restored to `max` afterwards.
  - **`cgroup.freeze`** (the v2 freezer, `CONFIG_CGROUP_FREEZER=y`), also phablet's: it would stop the
    whole group, the client too, which a running game talks to (Steam's API through `lsteamclient`):
    not for the whole group. Single processes can be paused with `SIGSTOP`.
- **What a running game needs from Steam**: the client (the game's Steam API: achievements, cloud
  saves at the end, the overlay we do not use); the web helper only for Steam's interface, which
  nobody sees, and for dialogs at a game's start (publishers' agreements, stage 4.3), which come
  before the game runs. Steam restarts a web helper it thinks hung (the option
  `-cef-disable-hang-timeouts` turns that off).

## Steps

1. **Where Steam's memory goes during a game**: per process (PSS, anon, file, shmem, swap) and the
   group's counters, idle and with Ys IX in its town (the case that hurts) and a lighter game (Stray),
   with the game's frame rate (overlay and sampler) and the phone's available memory.
2. **The ways, each by hand on a running game**, measured for: memory given back, how much comes back
   and how fast, Steam's CPU, the game's frame times and available memory, and whether Steam still
   works afterwards (signed in, the library, a game's cloud save at its end):
   - a. `/proc/<pid>/reclaim` on Steam's processes once, when the game is up;
   - b. Steam's `memory.high` lowered (to a few hundred MB) while the game runs, `max` again after;
   - c. (a) with the web helper paused (`SIGSTOP`) while the game runs, so its pages stay out, and
     woken (`SIGCONT`) when the game ends; with `-cef-disable-hang-timeouts` if Steam restarts it.
3. **The choice**, built into the game's start and end (`game-run.sh`, for Steam games and for own
   games while Steam runs in the background), with a setting to turn it off.
4. **Tests**: cloud saves at a game's end, quitting, the next start, Steam's state afterwards, on both
   phones; Ys IX's town before and after.

## Rules for this stage

- Steam's rules from stage 3 apply: no account data in logs or notes; Steam's files read with long
  numbers masked; `config.vdf`, `loginusers.vdf` and `localconfig.vdf` never copied off the phone.
- Only Steam's own processes and group are touched, never the game's or the system's.
- Everything is undone at the game's end (memory.high back to max, paused processes woken), and a
  crash of the game must not leave Steam paused.

## Questions for the maintainer

1. **Test games**: Ys IX's town (the memory-bound case of stage 4.3) and Stray? Ys IX needs its save
   in the town.
2. **For which games**: every Steam game automatically, with a switch in Settings to turn it off? And
   own games too, while Steam runs in the background (Settings, "Run Steam in the background")?
3. **Pausing the web helper** during a game: Steam's interface would not answer until the game ends;
   UTDeck shows nothing of it during a game, and Steam's downloads go on in the client. Acceptable to
   try?

## Answers from the maintainer (2026-11-24)

1. **Test game: Ys IX**, from a save in its town.
2. **Both**: every Steam game automatically, with a switch in Settings to turn it off; and own games
   too while Steam runs in the background.
3. **Pausing the web helper** during a game: OK to try.

## Steps 1 and 2: Ys IX's town (2026-11-24)

Measured with a read-only script on the phone (`steam-mem.sh`, every 10 s: the phone's available
memory and swap, Steam's group, the web helper's and the client's PSS and swap, the game's PSS, swap
and GPU memory through KGSL) and Ys IX's own frame log. Ys IX from the maintainer's save, standing
still in its town (the Nothing Phone (1), `high.perf`).

**Found on the way**:
- **The game runs inside Steam's own cgroup** (`utdeck-steam.service`): Steam starts its games as its
  children. The group's counters rose from 2.06 to 3.37 GB with Ys IX. So **way (b) is out**:
  lowering the group's `memory.high` would squeeze the game as well.
- Under the town's pressure **the kernel had already pushed about 440 MB of Steam into zram by itself**
  (the web helper 325 MB, the client 113 MB): Steam held about 494 MB in RAM, Ys IX 3.04 GB plus
  2.0 GB of GPU memory, none of it swapped.
- Ys IX names its main thread `MainThread`, which Linux shows as the process's name: a game is found
  by its command line, not its name. (And a script that looks for a pattern must not find itself:
  `[y]s9\.exe`; the same mistake once ended an `adb` command, `pkill -f` matching its own line.)

**Way (a), `/proc/<pid>/reclaim` once** (`all`, on Steam's client and its 9 web helper processes, not
on Wine or the game; 23:48:46):

| | Baseline (3 min) | Right after | After 3 minutes |
| --- | --- | --- | --- |
| Available memory | about 310 MB | 672 MB | about 625 MB |
| Steam in RAM (web helper + client) | about 494 MB | 28 MB | about 75 MB |
| Steam in zram | about 445 MB | 913 MB | about 890 MB |
| Ys IX | 24.92 fps, longest frames median 43 ms | | 23.51 fps, 44 ms |

- About 360 MB given to the game at once; Steam took back only about 50 MB in 3 minutes, without being
  paused, so way (c) may not be needed (a longer run decides).
- The frame rate did not change here (Ys IX was not short of memory standing still: none of its own
  pages swapped); the gain is headroom before the game itself has to swap.
- The push itself took about a second (the pages compressed into zram).

**Steam afterwards**: Ys IX ended at about 23:53:39, five minutes after way (a), with `exit 5` and no
exception in Wine's log (the maintainer had seen Ys IX crash like this before; the crash is recorded
here as seen, not as ruled out). Steam then uploaded the game's cloud save ("Upload complete, result
OK", 23:53:47, eight seconds later) and stayed signed in ("SetLoginState: Success"): the client took
back the pages it needed by itself (web helper about 100 MB and client 50 MB in RAM afterwards).

**The choice (step 3)**: way (a) alone, once a game is up: about 360 MB for the game, Steam stays out
on its own, and cloud saves work. Way (b) is out (the game shares Steam's group); way (c), pausing the
web helper, is not needed while Steam takes back so little (to be checked over a longer session).

## Status

- 2026-11-24: planned (this file); the maintainer's answers (Ys IX; every Steam game and own games
  with Steam in the background, a switch to turn it off; pausing the web helper allowed).
- 2026-11-24/25: steps 1-4 on the Nothing Phone (1). Way (a) chosen (`/proc/<pid>/reclaim` once the
  game is up); (b) is out (the game shares Steam's group), (c) not needed (Steam stays out). Built into
  `game-run.sh`, on by default, a switch in Settings (app 0.3.55). Tested with Ys IX (twice, cloud
  sync at the exit both times) and an own game. Left: stone, with stage 5's exit run.

## Step 3: built into the game's start (2026-11-24)

- **`game-run.sh`** (for Steam games and own games alike): with `UTDECK_STEAM_RECLAIM=1` a helper
  started with the game waits until the game's window is in front (Wine's focus file says 1, at most
  10 minutes), then 20 s more (Steam's work at a start, the launch and the cloud download, is over),
  then pushes Steam's own processes out once: `echo all > /proc/<pid>/reclaim` for the web helper's
  processes and the client, never Wine or the game. The game's log says how much Steam held in RAM
  before and after ("steam memory: N of Steam's processes pushed out of RAM, X MB -> Y MB"), or that
  the kernel has no `/proc/<pid>/reclaim`. A game that ends first ends the helper; Steam not running
  (an own game with Steam off) leaves nothing to do; Steam's install scripts are left out.
- **On by default** (`defaults.env`, with the measurement); Settings (Steam's part, "Free Steam's
  memory during games") turns it off for all games (`user.env`: `UTDECK_STEAM_RECLAIM=0`, written only
  when it differs from `defaults.env`).
- App 0.3.55.

## Step 4: tests (2026-11-25)

App 0.3.55 (the maintainer's OK) on the Nothing Phone (1); the app put the new `game-run.sh` and
`defaults.env` in place (`UTDECK_STEAM_RECLAIM=1`), and Settings shows the switch, on.

**Ys IX, played in its town for a few minutes, quit through its menu** (23:59:45-00:04:37):
- The push, from `game-run.sh`, once the game was in front: "steam memory: 10 of Steam's processes
  pushed out of RAM, 446 MB -> 23 MB".
- Steam stayed out the whole session: 50-60 MB in RAM (web helper and client) for over 4 minutes of
  play, against about 500 MB before the push: no creeping back, so way (c) is not needed.
- Ys IX's town, walked through, needs more than the phone has even so: the game about 3.1 GB plus
  2.3 GB of GPU memory, the phone down to 220-250 MB available, and about 500 MB of Ys IX's own memory
  swapped out (without the push, about 900 MB would have had to go).
- Quit cleanly (`exit 0`); Steam's cloud sync at the exit ran and ended ("AutoCloud complete", one
  second after the game; nothing new to upload this time, while the first session uploaded its changed
  saves the same way); Steam still signed in.

**An own game while Steam runs in the background** (wiliwili, 30 seconds, Settings' "Run Steam in the
background" on): "steam memory: 10 of Steam's processes pushed out of RAM, 157 MB -> 23 MB" (Steam had
been resting since Ys IX), `exit 0`, Steam still running.

**The next start**: the second Ys IX session (step 4) came after the first one's push and crash, and
started, played and synced as usual.
