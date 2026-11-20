# Stage 3.3: the Wine base, ours or Proton's

Started 2026-11-20. Plan: [stage3-roadmap.md](stage3-roadmap.md) ("Next: 3.3"). The question: stay
on Hangover's Wine (Wine 11.16 with Hangover's ARM64 work, our eleven patches and Proton's
`lsteamclient`), or move to Proton 11's Wine (Wine 11.0 with Valve's game fixes, which Valve runs on
ARM64 for the Steam Frame). Exit: a decision with numbers, the chosen base in `build-runtime.sh`.

Terms used here:
- *wineserver* = Wine's background process that owns shared objects (events, mutexes, windows,
  files); a Windows call on such an object becomes a request to it and a reply (a "round trip").
- *esync, fsync, NTSYNC* = three ways to wait on those objects without the round trip: esync with
  `eventfd` (works on this kernel), fsync with `futex_waitv` (Linux 5.16), NTSYNC with `/dev/ntsync`
  (Linux 6.14). Stone runs kernel 5.4; Proton 11 kept only fsync and NTSYNC.
- *context switch* = the kernel moving a CPU from one thread to another; a server that answers a
  request is woken once, so its switches per second approximate its requests per second.

## Step 1: what waits through wineserver cost

Before building anything: if the games spend only a few percent of a frame on server round trips,
an esync port is not worth carrying, whatever base is chosen.

Method ([`wineserver-cost.sh`](../../tools/ut-probe/wineserver-cost.sh), on the phone): the game at a
fixed spot (the maintainer stands still), 20 s untouched (frame rate, wineserver's CPU time and
context switches, the game's busiest threads), then 20 s with `strace -c -f` attached to
wineserver (its system calls counted, and the game's frame rate while the server is slowed down by
the tracing: the drop shows how much the game waits on the server).

### Results

| Game (spot) | Frame rate | wineserver | Requests (reads, writes) | Traced: frame rate | Notes |
| --- | --- | --- | --- | --- | --- |
| Ys IX (saved spot, 1600x720, Sampling 50 %) | 14.9 fps (67 ms) | 18 % of one core, 3,645 switches/s | about 3,000 per second (read 3,535/s, write 2,943/s, `epoll_pwait` 1,411/s): about 200 per frame | 12.9 fps (82 ms), -13 % | GPU-bound (97 % at 700 MHz at this spot in 3.2b); main thread 95 % of a core |
| ATRI (busy scene, 2400x1080) | 26.1 fps (42 ms) | 8 % of one core, 815 switches/s | about 750 per second (read 759/s, write 741/s), the same at 6 or 26 fps (time-driven: the sound mixer, timers): about 29 per frame | **6.0 fps (204 ms), -77 %** | CPU-bound: main thread 93 % of a core, GPU 43 % at 468 MHz |

Ys IX: at 10-20 µs a round trip on this phone, 200 requests cost about 2-4 ms of a 67 ms frame,
plus the server's own 18 % of a core. Tracing slowed every server call down many times over, and the
game lost only 13 %: it is not very sensitive to the server's speed. With the GPU at its limit,
esync would gain close to nothing here. CPU-bound games are where it could matter (ATRI: KiriKiri's
one thread at 100 % in busy scenes).

ATRI is sensitive to the server's speed: some requests sit on the main thread's critical path.
How much of that thread's time is spent waiting at all, from the kernel's scheduling statistics
(`/proc/<pid>/task/<tid>/schedstat`, 15 s, the same scene again, 31.6 fps, GPU 48 %): **running 90 %,
waiting for a CPU 0 %, blocked 8 %** (about 2.8 ms per frame, for everything: the server, other
threads, the GPU, sound), 305 voluntary sleeps per second. So removing every server round trip could
gain at most 8 %, and esync would only shorten the round trips, not waits on real events.

What one round trip costs on this phone ([`win/syncbench.c`](../../tools/ut-probe/win/syncbench.c),
default prefix, no game running, two runs each):

| | Native ARM64 | x86-64 through FEX |
| --- | --- | --- |
| One server request (set or check an event) | 16.6-17.1 µs | 23.9-24.9 µs |
| Ping-pong between two threads (two wake-ups) | 57-59 µs | 58-60 µs |
| `Sleep(0)`, a system call that skips the server | 2.1 µs | 2.2 µs |

On Windows such a request is a kernel call well under a microsecond; with esync it would be a few
microseconds (an `eventfd` read or write). So a round trip costs about 20 µs more here than with
esync. Against the games: Ys IX, about 200 requests per frame, about 4.8 ms spread over all its
threads (about 7 % of a frame), but the GPU is the limit; ATRI, about 24 per frame, about 0.6 ms
(about 1.7 %) on a CPU-bound main thread that is blocked only 8 % in all.

**Step 1's result: an esync port is not worth carrying.** At most about 2 % for the CPU-bound game,
nothing for the GPU-bound ones, against a large patch set to keep up with every Wine (Proton 11
dropped esync for the same reason). wineserver's own CPU time (8-18 % of one core) is real but
small. `wineserver-cost.sh` and `syncbench` stay, so a future game with heavy server traffic can be
checked in a minute; the question reopens only if one shows a much larger share.

A first look with our Direct3D 11 test program (not a game): wineserver at 27 % of one core and
about 3,100 switches per second, so test programs show the same order of traffic.

## Step 2: Proton 11's Wine built in our container

`build-runtime.sh` got a switch, `WINE_FLAVOR=proton`, that builds the same runtime on Proton's Wine:
ValveSoftware/wine at `e60892cc` (Proton 11.0-2e's `wine` submodule, Wine 11.0 with Valve's
patches), with its own source folder (`build/wine-proton-src`), its own build folders (suffix
`-proton` in `build/runtime-obj`), its own patches (`tools/wine-build/patches-proton/`, empty for
this step) and its own output (`build/runtime-proton`). FEX, Box64, DXVK, VKD3D-Proton, Wine Mono
and the media plugins are shared with the default build. With the suffix removed, the script is the
same as before, so the default build's folders are unchanged.

The 39-bit address space (stage 0, Wine bug 59898) does not stop Proton's Wine 11.0: the bug only
bites a toolchain without `-static-pie`, and Ubuntu 24.04's GCC in our container has it.

First build: stopped at once, `/wine/configure: not found`. Proton's Wine keeps no generated files in
git: no `configure`, no server protocol headers, no DLL spec files, no winevulkan thunks. Its tree
has `autogen.sh` (`tools/make_requests`, `tools/make_specfiles`, `dlls/winevulkan/make_vulkan -x
vk.xml -X video.xml` with the `vk.xml` in the tree, `autoreconf -ifv`), which Proton's own build runs
(`Makefile.in`, `.wine-post-source`). `build-runtime.sh` now runs it in the container, with the source
writable for that one step, when a source has no `configure` (log `build/runtime-obj/wine-autogen-proton.log`).
Proton configures Wine with `--enable-werror` (warnings become errors), its MinGW type and
`--disable-tests`, plus libraries its own build provides (vkd3d, FFmpeg, speech: vosk, piper); we keep
our options: configure leaves those libraries out, as it does for our Wine, and `--enable-werror` with a
newer compiler than Valve's would stop the build on harmless warnings.

Second build: Wine itself built in about 2.5 minutes ("Wine build complete"); configure left out the
same optional parts as for our Wine, plus Proton's own extras (vosk and piper for speech, libgcrypt).
But the install had no `bin/wine`: Proton 11.0's `makedep` does not build `tools/wine` (the `bin/wine`
launcher) when cross-compiling with separate tools, as we do (Hangover's 11.16 does; Valve builds
natively on ARM64, so never meets it); `wineboot`, `winecfg` and the other links pointed to a missing
`wine`. The assembly now makes `bin/wine` a link to the loader (`../lib/wine/aarch64-unix/wine`, which
finds its files from its real place, as Debian packages Wine), and leaves that link out of `strip`
(which would replace a link with a stripped copy). Result: `build/runtime-proton/utdeck-runtime`,
1.1 GB (194 MB packed), without the Steam bridge yet (`lsteamclient` against Proton's tree: step 3).

On the phone, installed next to ours (`wine/runtime-proton11.0-20261120`; the `wine/runtime` link the
games use is unchanged), in a fresh prefix:
- `wine --version`: `wine-11.0`; "using server-side synchronization" (no fsync or NTSYNC on 5.4).
- `wineboot -i`: 48 s, prefix 937 MB.
- Our console test (`wtest`) in all four architectures: native ARM64, ARM64EC, x86-64 through FEX
  (with the x86 features FEX emulates) and 32-bit x86 through Box64 and through FEX: all run (memory
  reservations, threads), as on our Wine.
- **No windows:** "Wayland compositor doesn't support wp_viewporter", so the Wayland driver gives up
  ("no driver could be loaded"). Lomiri has no viewporter; our patch 0001 makes it optional. Step 3.

Seen on the way: FEX sets the Turnip option `tu_override_uncached_as_cache_coherent=true` inside every
process it runs in (Turnip prints "default value ... overridden by environment"; an older audit,
`docs/compat-perf-audit.md`, noted it). The same on both bases; a default no one has measured on this
phone yet, for 3.4.

**Step 2 done:** Proton 11's Wine builds in our container from one switch and runs on this phone.

## Events

- 2026-11-20: 3.3 started; sources checked (DroidDeck: `WINESTEAMNOEXEC=0` for one game, the same
  switch as in our patch 0009; GPU clock logging, for 3.5; Nova-Deck on Mesa 26.3.1). Step 1's
  method and script written.
- 2026-11-20: the script checked on the Direct3D 11 test program; Ys IX measured (about 200
  requests per frame, -13 % when the server is traced, GPU-bound).
- 2026-11-20: ATRI measured in its busy scene. About two minutes after the measurement, with the
  maintainer standing still, **Lomiri crashed** (`lomiri-full-greeter.service: Main process exited,
  code=killed, status=11/SEGV`, 00:38:16) and systemd restarted it; Wine lost its Wayland connection
  ("Failed to read events from the compositor, terminating process") and ended the game. Before it,
  Lomiri logged a steady stream of `wl_subsurface.place_above not implemented` and
  `wl_subsurface.visibility not implemented` (requests from Wine's Wayland driver about the game's
  subsurface, which Mir 1.8 does not implement). Watched for a second occurrence; a program must not
  be able to crash the compositor.
- 2026-11-20: ATRI relaunched (no second Lomiri crash): its main thread blocked 8 %. `syncbench`
  written: a server request costs about 17 µs (ARM64) to 24 µs (x86-64 through FEX). Step 1 done:
  no esync port.
- 2026-11-20: step 2: `WINE_FLAVOR=proton` in `build-runtime.sh` (own source, build folders,
  patches, output); Proton's `autogen.sh` run in the container; `bin/wine` linked to the loader. Proton's
  Wine 11.0 builds (2.5 minutes) and runs on the phone in all four architectures; no windows without
  patch 0001 (viewporter). Next: step 3, our patches carried over.
