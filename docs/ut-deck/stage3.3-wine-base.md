# Stage 3.3: two Wine bases, ours and Proton's

Started 2026-11-20. Plan: [stage3-roadmap.md](stage3-roadmap.md) ("Now: 3.3"). The first question:
stay on Hangover's Wine (Wine 11.16 with Hangover's ARM64 work, our eleven patches and Proton's
`lsteamclient`), or move to Proton 11's Wine (Wine 11.0 with Valve's game fixes, which Valve runs on
ARM64 for the Steam Frame).

**Plan change (2026-11-20, after step 4's graphics tests):** both bases are kept, as two compatibility
tools in Steam, the way Steam offers several Proton versions. Our 11.16 base stays the default (fully
supported); Proton's becomes a second tool, chosen per game, downloaded on request. Step 4's game runs
now find which games need the second base; step 5 builds the two tools. Exit: both tools in Steam, a
per-game list with numbers, both runtimes from one build script.

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

## Step 3: our patches on Proton's Wine

Dry run, on a clean checkout, in order: only 0002 (first finger as the mouse) and 0005 (the pNext
chain) applied as they are; 0001, 0003, 0004, 0007, 0008 had one or two failed places; 0006 (the
scaler) failed in eight of nine places in `vulkan.c`; 0010 (fill mode) in seven places; 0011 in
three. Proton's Wine is Wine 11.0 plus Valve's patches, five months older than our 11.16, and the
Wayland driver and `win32u`'s Vulkan code changed in between (11.0 keeps a configure state as
`width`/`height` where 11.16 has a `rect`; it passes DPI as a number, 11.16 as a ratio; its present
function hands the window's work to `ensure_window_surface_contents()`).

The ports are kept as a branch, `utdeck-port`, in `build/wine-proton-src` (one commit per patch on
top of Proton's commit), and exported with `git format-patch` into
[`tools/wine-build/patches-proton/`](../../tools/wine-build/patches-proton/), numbered as ours:

| Patch | On Proton's Wine |
| --- | --- |
| 0001 viewporter optional | Ported: the window and the client surface viewports made optional by hand (the alpha-modifier block that 11.16 has after them is not in 11.0), and the client surface's size guarded |
| 0002 first finger as the mouse | As it is |
| 0003 follow the rotation | Ported: the same check, on 11.0's `window_surf_width`/`height` |
| 0004 fullscreen again when shrunk | Ported: the check reads `current.width`/`height`, and is called at the end of `ensure_window_surface_contents()`, where 11.0 does what 11.16's present function does |
| 0005 pNext chain | As it is |
| 0006 scaler | **Not ported: Proton's Wine has its own**, Valve's "fullscreen hack" (`fs_hack` in `win32u/vulkan.c`): the game draws into extra images, which a compute shader scales onto the real swapchain (`WINE_FULLSCREEN_INTEGER_SCALING` for whole-number scaling, `WINE_DISABLE_FULLSCREEN_HACK=1` to turn it off). It switches on when the window's DPI differs from the monitor's real DPI, the way Wine reports an emulated smaller display mode, the same condition as ours. Comparing the two scalers is part of step 4 |
| 0007 frame statistics | Ported: the hook after the present call, which Proton holds under a global lock (11.16 does not) |
| 0008 screen-shaped modes | Ported: added after the default sizes; Proton's list also has low-resolution modes (640x480, 960x540, not for two games) and a resolution limit |
| 0009 Steam bridge | Proton's own code; `lsteamclient` and `steam.exe` built against Proton's tree (the `steam` part) |
| 0010 fill mode | Not now: it extends our scaler. For the comparison, games run in fullscreen at a smaller mode, which Valve's scaler covers |
| 0011 opaque bars | Not needed: Mesa patch 0005 fixed the see-through pictures |

Build: the first try stopped at a compile error in 0002: `SEND_HWMSG_RAWINPUT` is not in Wine 11.0.
In 11.16 raw input is opt-in and every mouse event passes that flag; in 11.0 the default (0) already
sends raw input and window messages, and 11.0's own mouse code passes 0. The ported 0002 passes 0
(noted in its description). Second build: Wine with the seven patches and the Steam bridge
(`lsteamclient.so`, `steam.exe` from Proton 11.0-2e, built against Proton's tree), 1.5 minutes.

`env.sh` got a third runtime choice, `UTDECK_RUNTIME=proton` (`wine/runtime-proton`, a link to the
installed Proton build), and `rt-gfx.sh` runs with it. On the phone, `rt-gfx.sh proton` (MAILBOX
forced, as in games; a first manual run without it met stage 2.3's FIFO deadlock on Lomiri, which is
not Proton's):

| Test | Proton's Wine (01:12) | Ours, run right after (01:15) |
| --- | --- | --- |
| Vulkan x86-64, fullscreen | 401 fps, 1080x2400 | **hung** after Lomiri resized the window to 2400x1017 (exit 124) |
| Vulkan ARM64, window | 619 fps | 471 fps |
| Direct3D 11, window / fullscreen | 529 / 208 fps | 279 / 105 fps |
| Direct3D 11, display mode 1280x720 / 1920x1080 | 236 / 245 fps (Valve's scaler) | 147 / 118 fps (our scaler) |
| Direct3D 9, 32-bit / 64-bit | 209 / 211 fps | 97 / 100 fps |
| Direct3D 12, window / fullscreen | 608 / 197 fps | 387 / 128 fps |
| Sound | 3 s tone in 3,003 ms | 3,035 ms |

Every test passes on Proton's Wine: windows, the display-mode changes through Valve's scaler,
Direct3D 9, 11 and 12, sound. The frame rates are **not yet a comparison**: Proton's ran first on a
cooler phone; the Vulkan test's windows got different sizes in the two runs (portrait 1080x2400 for
Proton's, a landscape resize for ours), so Lomiri's orientation differed; and on our base a
fullscreen portrait window can go through our scaler or fill code (0006, 0010) when the monitor is
reported landscape, an extra full-screen copy per frame that Proton's base does not make here. Our
numbers are also below our own of 2026-11-18 (D3D11 fullscreen 154 fps then). Step 4 measures this
properly: interleaved runs, the phone's orientation fixed, and the copy path checked. Also for step 4:
our runtime hung after that resize (the x86-64 Vulkan test).

**Step 3 done:** our patches carried over where they make sense (seven ported or applied, the scaler
replaced by Valve's, fill mode and the opaque bars not needed for the comparison), Proton's Wine with
the Steam bridge builds from one command and passes the graphics tests.

## Step 4: a fair comparison, and where the gap comes from

The phone was not turned during any of these runs (portrait, as the maintainer left it).

### Method

- `tools/ut-probe/ab-gfx.sh ROUNDS A B`: the graphics tests (`rt-gfx.sh`) on two runtimes,
  alternated (A, B, A, B...), so warming up and other drift fall on both alike. Before each run it
  notes the hottest thermal zone and the free memory.
- `tools/ut-probe/gpu-per-frame.sh RUNTIME PREFIX`: d3d11test fullscreen, the GPU's load and clock
  sampled for 8 seconds, then **GPU time per frame** = load / frame rate. Two runtimes drawing the same
  picture should cost the same GPU time per frame; more means someone adds GPU work. It also reports
  Lomiri's CPU time over the same 8 seconds (Lomiri, through Mir, is the compositor: the program that
  puts every window's picture on the screen, using the GPU too).
- `WAYLAND_DEBUG=client`: logs every Wayland request a program sends; counted per frame for both
  runtimes.

### Results

Three alternated rounds, 2026-11-20, both runtimes on the same Mesa and FEX, hottest zone 37 °C at
every start:

| test | Proton 11.0 base | our 11.16 base |
| --- | --- | --- |
| D3D11 (DXVK) fullscreen 1080x2400 | 206-216 fps | 105-108 fps |
| D3D11 mode 1280x720 | 240-291 fps | 144-145 fps |
| D3D9 x86 fullscreen | 202-217 fps | 97 fps |
| D3D12 (VKD3D-Proton) fullscreen | 197-198 fps | 128-129 fps |
| Vulkan ARM64 window 960x540 | 801-824 fps | 504-506 fps |
| Vulkan x86-64 fullscreen | 401-409 fps at 1080x2400 | 235-242 fps, window resized to 2400x1017 |

GPU time per frame (d3d11test fullscreen, GPU at 700 MHz in both):

| runtime | fps | GPU load | GPU per frame | Lomiri CPU |
| --- | --- | --- | --- | --- |
| Proton 11.0 | 199-207 | 61 % | 3.0-3.1 ms | 75 % of one core |
| 11.16, all patches | about 105 | 76 % | about 7 ms | - |
| 11.16, without 0010 and 0011 | 112 | - | 6.8 ms | - |
| 11.16, only 0001-0004 and 0009 | 112 | 76 % | 6.7-6.8 ms | 146 % of one core |

So the extra cost was **not in our patches** (a build with only the five patches needed to run at
all was as slow) and not in the game's drawing: the same picture cost twice the GPU time, and Lomiri
twice the CPU time. The compositor was doing more work for each of our frames.

Wayland requests over 600 frames:

| request | Proton 11.0 | 11.16 |
| --- | --- | --- |
| `wl_surface.commit` | 689 | 1879 |
| `wl_subsurface.set_position` | 3 | 1204 |
| `wl_subsurface.place_above` | 3 | 1204 |
| `wl_surface.attach`, `frame`, `damage_buffer` | about 600 each | about 600 each |

### First suspect: three commits per frame (real, but not the cause)

A game's picture lives in a *subsurface* (a child Wayland surface inside the window's surface).
In Hangover's Wine 11.16, win32u updates that client surface twice per present (before, and again
inside `client_surface_present`), and each update in the Wayland driver
(`wayland_client_surface_attach`) sends the subsurface's position and stacking again and commits the
parent surface, even when nothing changed. Mir 1.8 does not implement `place_above`: it writes
`wl_subsurface.place_above not implemented` to its log for every one, the stream of messages seen
before the Lomiri crash on 2026-11-20 (see Events).

Patch 0012 (`0012-winewayland-send-the-client-subsurface-placement-only-when-it-changes.patch`)
remembers the placement last sent and its parent surface, and sends and commits only a change (a
moved or resized window still goes through). Measured: 669 commits over 600 frames (Proton 689), no
more repeated `set_position`/`place_above`, but **the same speed**: 105 fps, 6.9 ms of GPU per frame,
Lomiri 144-146 % of one core. Kept, because it removes useless work and Lomiri's log spam, but the
gap is elsewhere.

### Why our window is landscape

With the per-frame traffic equal, the request arguments show the difference: our runtime gives
Lomiri **2400x1017 buffers** (landscape, the screen minus the 63-pixel top bar), Proton's
**1080x2400** (portrait). DXVK draws 1080x2400 in both; on ours, win32u then scales each frame into a
landscape buffer, and Lomiri turns and scales it back to the portrait screen. Two extra full-screen
passes per frame: twice the GPU time and twice Lomiri's work.

The trace (`WINEDEBUG=+waylanddrv,+win`) shows how the window gets there. Wine's monitor is correct
(1080x2400); the window goes fullscreen at 1080x2400; then **Lomiri sends a configure** (the
compositor's request for a new window size) **of 2400x1017**, and our patch 0003 ("follow rotation")
obeys it. Proton's runtime, on the same phone and the same test, gets only 1080x2400 configures.
Not the cause: the wineboot dialog that a prefix update shows first (three runs in a row without it,
the same landscape buffers).

Second wrong suspect: the size hints. Wine 11.16 tells the compositor that a window that is not
resizable has a fixed size (`xdg_toplevel.set_min_size` = `set_max_size`), and Proton's Wine sends no
size hints at all. A test build without them for fullscreen windows changed nothing (2400x1017
buffers, 105 fps); dropped.

The exact request sequence of the game window up to Lomiri's landscape configure shows it: the
window asks for fullscreen, Lomiri grants it (1080x2400 with the fullscreen state, serial 882), and
then **the window sends `set_fullscreen` a second time**. Lomiri answers that one with 2400x1017.
(Later shown to be correct: Lomiri itself was in landscape, see below.)
Proton's window sends `set_fullscreen` once. The second request comes from Wine's own state code
(`wayland_surface_update_state_toplevel`): it sends `set_fullscreen` when the window wants fullscreen
and the *applied* configure (current) is not fullscreen. A configure moves pending → requested →
processing → current; one that has been received or handled but not yet applied does not count, so
Wine asks again for what the compositor just granted. Both Wines have this code: Proton's wins the
timing race here, ours does not.

Third wrong suspect, as the trigger at least: patch 0013
(`0013-winewayland-compare-window-state-with-the-newest-configure.patch`) compares the wanted state
with the newest configure the compositor sent (requested, else processing, else current). The second
`set_fullscreen` is gone, but Lomiri still sends 2400x1017 and the speed is the same (105-106 fps,
6.8-7.0 ms). The patch fixes a real race and stays under test; the landscape request comes from
something else.

### Is Proton's picture on the screen at all?

The two sequences side by side, from the game window's creation to the first frame: on Proton's
Wine, the game window's **xdg_toplevel is destroyed before frame 1** and never created again (over the
whole run: one `get_toplevel`, one `xdg_toplevel.destroy`). Its frames still go to the client surface
(1080x2400 buffers, attach and commit every frame), but a surface that has lost its window role is
not shown: Lomiri can drop those frames almost for free. That alone would explain 3.0 ms instead of
6.9 ms and half of Lomiri's CPU time, so **Proton's fullscreen numbers may not be a comparison at
all**. Ours does show its picture, through a landscape detour.

The screenshot meant to settle it came out black for both runtimes: the backlight was at 0. The screen
was off for this night's automated runs, both runtimes alike.

Rerun with the screen on (2026-11-20, 09:29), each runtime on its own prefix (so no "configuration
is being updated" dialog), screenshots 8 and 14 s into each run (`build/screenshots/step4-run*.png`,
not in git):

| run | reported | on the screen |
| --- | --- | --- |
| A: Proton 11.0, d3d11test fullscreen | 210 fps, 3.0 ms GPU per frame, Lomiri 72 % | **only the wallpaper**: the test picture is never shown |
| B: ours (11.16 + 0001-0013) | 75 fps, 11.1 ms GPU per frame, Lomiri 123 % | the test picture over the whole screen |

The maintainer, watching: in run A the test picture showed for a second or two, then only the
wallpaper; in run B it stayed. The phone was lying flat (rotation lock off), so Lomiri kept the
landscape it last had.

So **Proton's fullscreen numbers measured nothing**: its window loses its role and Lomiri throws the
frames away. The "2x" from the alternated rounds is void for every fullscreen test. Two more
corrections from these screenshots:

- **Lomiri was in landscape**, though the phone was not turned (it was lying flat): its top bar runs
  along the long edge of the screen. Its 2400x1017 configure is therefore correct (the landscape screen minus the bar), and
  our window following it is our patch 0003 working as meant, not a Lomiri bug. The earlier
  "upright phone" reading was wrong.
- **Screen on costs more**: ours, 105 fps with the screen off, 75 fps with it on (11.1 ms of GPU per
  frame). Every number from this night's automated runs was taken with the screen off; real play is
  with the screen on, so measurements that matter must be taken with it on.

### Why Proton's window disappears

Traced (`WINEDEBUG=+waylanddrv,+win`): when the game window goes fullscreen, Proton's win32u takes its
window surface away (`apply_window_pos win ... surface ... -> (nil)`), and the Wayland driver, given a
window without a surface, destroys the window's Wayland surface and its role. The rule is Valve's,
not upstream Wine's (11.16 has nothing like it): `get_window_surface` gives no window surface to a
window whose client area shows a Vulkan or OpenGL picture (`window_clip_client_surfaces`). On X11,
which Valve uses, the X window stays and a copy is saved; Wine's Wayland driver cannot live with it.
The only way out in Proton's code is a per-game switch (`force_present_to_surface`, Steam app 803600
only).

Fix: Proton patch 0009 (`patches-proton/0009-win32u-keep-the-window-surface-of-windows-with-a-vulkan-picture.patch`)
keeps the surface, as upstream Wine does; `UTDECK_WAYLAND_CLIP_CLIENTS=1` restores Proton's rule.

Checked (09:46, screen on, Lomiri in landscape, phone lying flat): with 0009 the window keeps its
role until the program exits (one `xdg_toplevel.destroy`, after frame 300). The picture is shown,
but not right: in the screenshot it covers only part of the screen, its colours look mixed with the
wallpaper (see-through?), and it stays portrait (1080x2400 buffers) while Lomiri is landscape, so
Proton's window did not follow the rotation as ours does. Numbers with the picture on the screen:
126 fps, 6.4 ms of GPU per frame, Lomiri 153 % of one core (ours, same conditions: 75 fps, 11.1 ms).
Not a fair comparison yet: the two still do not show the same picture.

The test scripts set their own `WINEDLLOVERRIDES`, which replaced env.sh's `mscoree,mshtml=`: on the
prefix update after the new Proton build, Wine offered to download and install Mono (cancelled by the
maintainer). `gpu-per-frame.sh` now keeps Mono and Gecko off; `rt-gfx.sh` already did.

The maintainer, watching: the picture stayed the whole run, in the left 45 % of the screen (a
1080-wide portrait window on a 2400-wide landscape screen), and was not see-through (the odd colours
were only in the screenshot).

Why it stays portrait: Lomiri sends Proton's window the landscape size (2400x1017) but **without the
fullscreen state** (`array[4]`, activated only; ours gets `array[8]`, fullscreen and activated), and
our rotation patch follows only fullscreen windows. Before granting fullscreen to ours, Lomiri saw
fixed size hints (min = max = 1080x2400) that Proton's Wine does not send. Experiment (a Proton patch
sending the hints as 11.16 does, since removed): Lomiri then grants fullscreen once and takes it back
at once (`array[8]`, then `array[4]` twice), before the window even answers. The hints change Lomiri's
behaviour but do not explain it; that is Lomiri's own window logic. Lomiri's log has nothing on it,
but it shows that Proton's window, now kept, also repeats `place_above` every frame (what our 0012
removes).

Decision: compare the two bases with Lomiri in portrait (phone held upright), where both windows are
1080x2400 on a 1080x2400 screen and nothing has to turn. Proton's handling of Lomiri's rotation is a
port task for later, needed only if Proton's base is chosen.

### The first fair comparison

2026-11-20, 09:58-10:01: phone upright, Lomiri in portrait, screen on, hottest zone 37-38 °C, each
runtime on its own prefix (warmed up first), alternated, d3d11test fullscreen (DXVK), 2500 frames,
`gpu-per-frame.sh`, screenshots 9 s into each run:

| run | runtime | fps | GPU per frame | Lomiri CPU | on the screen |
| --- | --- | --- | --- | --- | --- |
| 1 | ours (11.16, 0001-0013) | 119.3 | 6.9 ms | 161 % | whole screen, over the top bar |
| 2 | Proton 11.0 (0001-0009) | 130.2 | 6.2 ms | 156 % | whole screen below the top bar |
| 3 | ours | 120.2 | 6.7 ms | 160 % | whole screen, over the top bar |
| 4 | Proton | 129.7 | 6.2 ms | 157 % | whole screen below the top bar |

**Proton's base is about 8 % faster here, not 2x.** The two do not yet show exactly the same thing:
Lomiri does not give Proton's window the fullscreen state (the top bar stays, the window is a little
smaller), and our base carries patches Proton's does not (0006 scaler, 0010 fill, 0011 opaque, 0012,
0013). The night's "2x" came from Proton's frames never reaching the screen.

### All graphics tests, portrait, screen on

2026-11-20, 10:03-10:11: `ab-gfx.sh 2 utdeck:runtime-p13 proton:runtime-proton-p09` (the scripts
now take a test build's folder, keep the screen on through repowerd's `keepDisplayOn` and log the
backlight: 289 at every start), phone upright, hottest zone 37-39 °C. Frames per second, range of the
two rounds:

| test | ours (11.16) | Proton 11.0 + 0009 | Proton ahead |
| --- | --- | --- | --- |
| Vulkan x86-64 fullscreen | 210-215 | 230-233 | +8 % |
| Vulkan ARM64 window 960x540 | 512-535 | 539-578 | +6 % |
| D3D11 window 960x540 | 331-341 | 359-363 | +7 % |
| D3D11 fullscreen 1080x2400 | 120-121 | 130-135 | +10 % |
| D3D11 mode 1280x720 | 109-131 | 168-171 | +30-55 % |
| D3D11 mode 1920x1080 | 93-94 | 159 | +70 % |
| D3D9 x86 / x86-64, mode 1920x1080 | 109-116 | 121-125 | +8-10 % |
| D3D12 window 952x506 | 334-341 | 356-362 | +6 % |
| D3D12 fullscreen 1080x2400 | 117-121 | 127-132 | +6-9 % |

Two findings:

- **Without a display mode change, Proton's base is 6-10 % faster** in every test, drawn the same
  way (except Proton's window stops below the top bar).
- **With a display mode change (a game asking for 1280x720 or 1920x1080), the gap is 30-70 %.** A mode
  change is where each base scales the game's picture to the screen: ours with our patch 0006 (a
  scaler we wrote), Proton's with Valve's `fs_hack` (a compute shader). Most games on the phone run
  this way (a resolution below the native 1080x2400). But see the next section: Proton was not
  drawing the right picture there.

### The scaling paths

2026-11-20, about 10:20, same conditions. For each mode test, the size the game draws at (the
swapchain), the buffers handed to Lomiri (`android_wlegl.get_server_buffer_handle` in the Wayland log)
and whether a Wayland viewport is used (no: Lomiri offers no `wp_viewporter`, so the compositor never
scales). The mode tests need `UTDECK_WAYLAND_LANDSCAPE=1` (patch 0003 reports the portrait screen as a
landscape monitor, as `rt-gfx.sh` does); without it, a portrait screen has no 1280x720 mode and the
change fails (`ChangeDisplaySettingsEx = -2`).

| test (landscape monitor) | game draws at | ours: buffers to Lomiri | ours | Proton: buffers to Lomiri | Proton |
| --- | --- | --- | --- | --- | --- |
| D3D11 mode 1280x720 | 1280x720 | 1080x2400 | 102 fps | **1x1** | 244 fps |
| D3D11 mode 1920x1080 | 1920x1080 | 1080x2400 | 91 fps | **1920x1080, unscaled** | 148 fps |
| D3D9 x86 fullscreen | | 1080x2400 | 85.5 fps | 1080x2400 | 86.4 fps |

Ours scales and turns the game's picture into a full-screen buffer (patch 0006). Proton's own scaler
(`fs_hack`, which switches on only when the window's DPI differs from the screen's) never switches on
here: at 1280x720 Lomiri gets a 1x1 buffer (nothing to see), at 1920x1080 an unscaled picture that
cannot fit a 1080x2400 screen. Its "30-70 %" was work it did not do. Where both do the same work
(D3D9), they are equal.

### Step 4 conclusion

Where both bases put the same picture on the screen, **Proton 11's Wine is 0-10 % faster than
Hangover's 11.16** (6-10 % at the screen's own size, equal with scaling). Everything bigger measured
this night was a picture Proton did not show: no window (fixed by Proton patch 0009), a 1x1 or
unscaled buffer in mode changes (Proton has no working scaler for our landscape monitor), and the
screen off for the first runs. To match ours, Proton's base would still need: following Lomiri's
rotation (Lomiri does not give its window the fullscreen state), a scaler for mode changes (our 0006
port or a fix to `fs_hack`), and 0012/0013. Games at saved spots are still to be compared; the
decision is step 5.

### Games on both bases

Set-up (step 5.4 in its first form): `utdeck-compat` now gives each base its own prefix,
`pfx` for ours and `pfx-proton` when the game's `utdeck-env` says `UTDECK_RUNTIME=proton`. On the
phone, `wine/runtime-proton` points at the 0001-0009 build (`runtime-proton-p09`). A new Proton
prefix: Steam runs the game's install script into it first (about 30 s); Proton's Wine names the
prefix user `steamuser` (ours `phablet`), so saves and settings are copied into that folder by hand.

**Ys IX** (2026-11-20, 12:13 and 12:18; phone in landscape, screen on, the same save and the same
`settings.xml`: 1600x720 windowed, Sampling 50 %; `measure.sh 30`):

| base | fps | frame time (worst) | GPU | CPU | game memory | on the screen |
| --- | --- | --- | --- | --- | --- | --- |
| Proton 11.0 + 0001-0009 | 15.4 | 65 ms (69) | 97 % at 700 MHz | 56 % | 2.1 GB | top left, 1:1 |
| ours (11.16, installed default) | 14.9 | 67 ms (76) | 96 % at 700 MHz | 59 % | 2.1 GB | the whole screen (patch 0010 scales it) |

Equal within 3 %, and ours also scales the picture to the screen: in a GPU-bound game, the base does
not matter. The controller works on both. On Proton's base, **fullscreen 1600x720 shows nothing**
(only the wallpaper; DXVK draws 1600x720, the display mode change has no working scaler there: step
5.2); the game was set back to windowed in `pfx-proton`'s `settings.xml`.

**Ys IX crashed again on our base** (open), right after the measurement: it drew normally to the end
(15.7 fps in its last second, 12:20:51, about 4.5 minutes after the start), then froze with the GPU
idle for 17 s and quit with exit code 5 (12:21:09), with no crash file in the prefix or the game
folder; the maintainer saw it as the same crash as on 2026-11-19 (the game's own error window).
That one went away with FEX-2611.1, which this runtime has, so the FEX update was not the whole
fix. Proton's base ran about 2.5 minutes without it, too short to say anything. To look at after
the game comparisons: a longer run on each base, and the game's error text.

**ATRI** (2026-11-20, 12:30-12:34; the busy scene, the game's fullscreen option; game data copied
into `pfx-proton`, `measure.sh 30`):

| base | run | fps | frame time (worst) | GPU | CPU | game memory | on the screen |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Proton 11.0 + 0001-0009 | 1 | 25.8 | 50.5 ms (548) | 23 % at 385 MHz | 47 % | 855 MB | a 1280x720 window, windowed or fullscreen |
| ours | 1 | 37.3 | 33.8 ms (404) | 39 % at 481 MHz | 46 % | 867 MB | the whole screen |
| ours | 2, right after | 20.9 | 54.4 ms (434) | 24 % at 382 MHz | 41 % | 861 MB | the whole screen |

**Works on both bases; the speed is not comparable at this scene.** The scene itself varies from
second to second on both bases (ours 8-80 fps per second, with long stretches at 10-16 and at
50-80; Proton's 15-44, with a long flat stretch at exactly 40 fps, a cap worth a look), so
30-second averages measure which part of the animation fell into them. ATRI does not need the
second base, which is the question of this step, so no longer runs were made; the CPU-bound
comparison comes back in 3.5 with long runs. On Proton's base it does not fill the screen (patch
0010, fill mode, is not in Proton's set: step 5.2).

**Stray** (2026-11-20; Direct3D 11, DXVK, on both bases: started by hidden Steam
(`steam-service.sh launch 1332010`), which passes no `-dx12`; 2026-11-19's D3D12 run had it on the
command line. Both APIs gave the same speed then, 8.4 and 8.3 fps). Set-up: no save in the prefix (the game starts from its opening), only its settings, copied
into `pfx-proton` (made beforehand as `utdeck-compat` would make it). Both bases run **windowed
1280x720** (`FullscreenMode=2`, Sampling 50 %, the game's 30 fps limit), not fullscreen: on Proton's
base a fullscreen mode change shows nothing (as with Ys IX). Our prefix's fullscreen settings are
kept beside it (`GameUserSettings.ini.fullscreen`); after the runs the maintainer chose to keep
windowed (it fills the screen on our base and is not slower). First start on
Proton's base: Steam's install script ran into the new prefix in 43 s; no agreement window (Steam
keeps the acceptance per account).

| base | fps | frame time (worst) | GPU | CPU | game memory | on the screen |
| --- | --- | --- | --- | --- | --- | --- |
| Proton 11.0 + 0001-0009 (15:05) | 9.1 | 110 ms (226) | 90 % at 700 MHz | 38 % | 1.5 GB (1.16 GB free) | top left, 1280x720 1:1 |
| ours (11.16, installed default) (15:10) | 9.2 | 109 ms (184) | 91 % at 700 MHz | 40 % | 1.4 GB (1.10 GB free) | the whole screen (patch 0010 scales it) |

Equal, as with Ys IX: GPU-bound, the base does not matter, and ours also fills the screen. Stray
does not need the second base. (Windowed 1280x720 with our scaler, 9.2 fps, is not slower than
2026-11-19's fullscreen 1280x720, 8.4 fps: the scaling costs nothing visible here.)

**Goat Simulator** (2026-11-20; UE3, 32-bit through Box64, Direct3D 9 through DXVK). The question
here is not speed: on our base it hung at the start of its first level on 2026-11-19 (Wine's server
waits, Wine's XAudio2 thread looping; see [stage3.2b-games.md](stage3.2b-games.md)), and Proton's
Wine carries Valve's fixes for games like it. Set-up of `pfx-proton`: made as `utdeck-compat`
makes it, then Wine Mono by hand as in our prefix (the game starts .NET; 39 s, 977 to 1308 MB).
Proton's prefix has a real `Documents` folder (ours links to the phone's home folder), so only the
game's `Config` folder was copied into it (its `Logs` hold the Steam account name), set to
**windowed** 1280x720 (`Fullscreen=False` in `[SystemSettings]`): fullscreen shows nothing on
Proton's base.

First run on Proton's base (15:14, `steam-service.sh launch 265930`): Steam's install script
(DirectX June 2010 setup under Box64) about 3 min, then the game process; first frame 1 min 45 s
later. Intro at 28-38 fps, GPU about 30 %. UE3's log: `LoadMap: LegalLevel` at 89 s,
**`LoadMap: level_01` at 103 s, the point where ours hung, and the game went on drawing**:
4-7 fps (frame time 145-255 ms), GPU 45-70 %, game CPU about 305 % (three cores), 2.2 GB resident,
still drawing more than 6 minutes after the first frame; the maintainer played it (windowed, no
hang, the controller works). The swapchain is 960x540: the maintainer had set that in the game's
menu during the run. (Also logged: `Can't start an online game that hasn't been created` at
213 s.)

Second run (15:28, after the maintainer set 960x540 and the lowest graphics in the menu; the game
saved them at once, so it was ended with `wineserver -k` and started again): first frame after
about 1.5 min (faster, the shaders were already compiled), intro 38-57 fps, `level_01` at 92 s, no
hang again. The settings file still has ambient occlusion, bloom, motion blur, lens flares and
distortion on (only dynamic shadows and depth of field off). Standing still in the level
(`measure.sh 30`, 15:34):

| base | fps | frame time (worst) | GPU | CPU | game memory | on the screen |
| --- | --- | --- | --- | --- | --- | --- |
| Proton 11.0 + 0001-0009 | 6.6 | 154 ms (279) | 47 % at 473 MHz | 38 % | 2.2 GB (1.2 GB free) | a 960x540 window |
| ours | hangs at `level_01` (2026-11-19) | | | | | |

Neither the GPU (half busy, so its clock dropped) nor one CPU core is the limit: over 10 s the
game's main thread used 73 % of a core, UE3's RenderingThread 42 %, a second game thread 36 %,
DXVK's command thread 17 %. The threads wait on each other (or on Wine's server: Proton's Wine
reports "using server-side synchronization", no fast sync on this kernel) more than they compute.
**First entry for the per-game list: Goat Simulator needs Proton's base**, pending one check:
our base with exactly these settings (windowed 960x540, lowest), so that the hang is shown to come
from the base and not from the settings.

The check (15:38, our base, the four settings files the game had changed copied over: `GoatEngine`,
`GoatInput`, `GoatSettings`, `GoatSystemSettings.ini`; ours kept in
`compatdata/265930/config-ours-20261120`): intro at 39 fps as on Proton's base, `level_01` at
105 s behind the loading movie, `Game class is 'GGGameInfo'` at 139 s; the last frame at about
156 s; then **UE3 stopped with an error window**, seen by the maintainer: "Pure virtual function
being called while application was running (GIsRunning == 1)". Its log: `appError called: Pure
virtual function being called` at 238 s, then `Rendering thread exception` at 245 s (raised in
`kernelbase.dll`, two frames in code UE3 could not name), a minidump in the game's `Logs` folder.
A "pure virtual function call" is C++ code calling a method of an object that is being
destroyed or was never finished: here UE3's render thread used an object the level load was
replacing. **So the base is the cause, not the settings:** with the same files, Proton's base
loads the level and plays, ours fails at the same point every time, as a hang (2026-11-19, five
runs) or as this crash. Two threads racing while the level loads fits both. Why Proton's Wine
gets through (its Direct3D 9 device reset, XAudio2, the heap or the thread timing) is not known
yet. The maintainer kept today's settings in our prefix too.

**Ys's movies through DirectShow** (2026-11-20, 15:47-15:51; `vidtest.exe FILE 30` with
DirectShow's default renderer, the one that fails on our base, known issue K10; phone upright,
screen on, sound on, the maintainer watching; Proton's runs in a new `pfx-proton` for Ys: Memories
of Celceta, app 587110):

| file | base | result |
| --- | --- | --- |
| `logo.wmv` (ASF, 11.5 s) | Proton | **Does not open**: `RenderFile` 0x80040218 (no filter chain can play it); GStreamer: `failed to create glupload`, then the ASF demuxer's output not linked |
| `op.wmv` (AVI, 100 s) | Proton | Played for the 30 s and stopped when asked (ours hangs on its second frame); no blit error. The maintainer: picture correct but single-digit frame rate, **no sound**. GPU 0 % (drawn in software: wined3d found no OpenGL pixel format), the program's memory grew by about 70 MB every 2 s (576 to 1085 MB in 17 s): decoded frames piling up faster than they are shown |

Why `logo.wmv` fails on Proton's base: its Windows Media reader (`wm_reader.c`), for **32-bit**
programs only, converts the video in GStreamer through OpenGL (`glupload`, `glcolorconvert`, ...,
`gldownload`), because 32-bit GStreamer on a PC converts I420 to RGBA slowly (Valve's comment), and
turns that off per game by Steam app id. Here the GStreamer side is always native 64-bit ARM, so
the reason does not apply, and GStreamer's OpenGL plugin is not on the phone. The AVI path
(`quartz_parser.c`) never asks for OpenGL, so `op.wmv` opens.

The same two files on our base (`pfx` of app 587110), right after:

| file | base | result |
| --- | --- | --- |
| `logo.wmv` | ours | Played to the end (11.5 s), 296 `Failed to blit` (as on 2026-11-19) |
| `op.wmv` | ours | **Hung** again: no end reported, the 30 s stop did not return either, ended by `timeout` at 45 s; a lock wait timed out on `strmbase_filter.stream_cs` (the renderer filter's lock) |

The maintainer, for our base: **a white window of the right size, no picture, no sound.**

The difference between the bases: Proton's wined3d (Wine's own Direct3D, which DirectDraw draws
through) finds no OpenGL pixel format here (`Failed to get a GL context`, 9 times) and falls back
to drawing in software, which is slow but correct. Ours logs no such error, so its DirectDraw
draws through OpenGL, and that is where the blits fail and the renderer hangs. A test for later:
ours with wined3d's software renderer (`WINE_D3D_CONFIG=renderer=no3d`) or its Vulkan one
(`renderer=vulkan`).

### Step 4 closed: which base each game needs (2026-11-20)

| game | ours (11.16) | Proton's (11.0) | base |
| --- | --- | --- | --- |
| Ys IX: Monstrum Nox | 14.9 fps, fills the screen | 15.4 fps, top left 1:1; fullscreen modes show nothing | ours (equal speed, ours fills the screen) |
| Stray | 9.2 fps, fills the screen | 9.1 fps, top left 1:1 | ours (equal) |
| ATRI -My Dear Moments- | works, fills the screen | works, a 1280x720 window | ours (speed not comparable at its busy scene) |
| Goat Simulator | fails at its first level every time (hang, or UE3's "pure virtual function" crash) | **plays**, 6.6 fps standing still | **Proton's** |
| DirectShow movies, default renderer (Ys's files) | ASF drops most frames, AVI hangs | ASF does not open, AVI correct but single-digit fps and no sound | neither (K10 stays open) |

Not compared: Ys: Memories of Celceta and PvZ Replanted (both run on ours; nothing pointed to the
second base). **Parked**, each with its notes above: the Ys IX crash after about 4.5 minutes on our
base; wined3d's renderer for our DirectShow path (`renderer=no3d` or `vulkan`); why our base fails
Goat Simulator's level load. For step 5.2: the OpenGL video path of Proton's Windows Media reader
(turned off for ARM64), the missing scaler and fill mode, rotation and the fullscreen state.

## Step 5: two compatibility tools

### 5.3 and 5.4: the second tool, its own prefixes

[`steam-compat/proton/`](../../tools/ut-probe/steam-compat/proton/) is a second Steam compatibility
tool, **"UTDeck runtime (Proton 11)"** (`compatibilitytools.d/utdeck-proton`, installed by
`steam-push.sh`). Its `utdeck-compat` only sets `UTDECK_TOOL_RUNTIME=proton` and runs the default
tool's script, so both tools share one script: the tool Steam started picks the Wine base, a
game's `utdeck-env` can still override it (`UTDECK_RUNTIME`, for tests), and each base keeps its
own prefix (`pfx`, `pfx-proton`; step 4's form). A game is set to it in its Properties
(Compatibility) or with `set-default.py --app APPID utdeck-proton` (Steam stopped).

### 5.5: Proton's runtime on request

[`get-runtime.sh`](../../tools/ut-probe/get-runtime.sh) `proton [FILE]` installs Proton's runtime on
the phone: downloaded from the address in [`runtimes.list`](../../tools/ut-probe/runtimes.list)
(a release asset of this repository), or from a file already there; checked against its SHA-256
(the file's own `.sha256`, else the list's); unpacked into `wine/runtime-proton-<version>/`, with
`wine/runtime-proton` pointing at it (older versions stay). It wants 2.2 GB free (1.1 GB unpacked,
plus room for games). `runtime-push.sh proton` pushes a build from the PC and installs it through
the same script. A game set to the Proton tool without the runtime installed stops at once with
"Proton 11's runtime is not installed (sh .../get-runtime.sh proton)" in its `utdeck-compat.log`.
No release exists yet, so the download address answers 404 until one is made.

### 5.2: Proton's base brought level

On the `utdeck-port` branch of `build/wine-proton-src`, exported into `patches-proton/` (numbered as
ours; 0014 is Proton's only):

| patch | what |
| --- | --- |
| 0006 make the fullscreen hack work on Wayland with RGBA games | Valve's scaler for display modes (`fs_hack`) scaled to the surface's minimum size: the window's on X11, **1x1 on Wayland** (Mesa reports no current size), so a game at 1280x720 showed nothing (Wine's log: "scaling from (1280,720) -> (1,1)"; Ys IX's fullscreen 1600x720 in step 4). Now the client area at the raw DPI. It also read every game image as BGRA and wrote BGRA: with DXVK drawing RGBA (our Mesa offers no BGRA swapchain), red and blue were swapped and Lomiri got `BGRA_8888` with the game's alpha (see-through). RGBA games now stay RGBA; Lomiri gets opaque `RGBX_8888`. Checked: 1280x720 scaled to 1920x1080 on the reported landscape monitor, 117.7 fps, buffers 1920x1080 in format 2 (RGBX). It replaces our 0006 on this base |
| 0012 client subsurface placement only when it changes | Ported (11.0 keeps x, y, width, height, not a rect) |
| 0013 compare the window state with the newest configure | Ported (11.0 has no minimized checks) |
| 0014 no OpenGL video conversion for 32-bit programs | Proton's Windows Media reader converts 32-bit programs' video through GStreamer's OpenGL plugin, a fix for slow 32-bit GStreamer on PCs; here GStreamer is native 64-bit ARM and has no OpenGL plugin, so a 32-bit program's ASF video did not open at all (step 4's `logo.wmv`) |

While porting 0012, its own description and comment still said the extra commits doubled the GPU
time per frame; step 4 had shown they did not (the speed was unchanged with the patch). Both texts
corrected, in our patch too.

**Fullscreen state and rotation (open).** Traced on both bases, `d3d11test --fullscreen`, phone
upright: Lomiri grants fullscreen at 1080x2305 (the screen less its top bar) and soon sends the
"activated" state alone. Ours asks again (patch 0004: unset, then set) and gets fullscreen back;
Proton's base asks with `set_fullscreen` alone (from its state update), and its 0004 never runs,
because it only checks a window the compositor calls fullscreen. Both hand Lomiri 1080x2400
buffers. Why Lomiri drops the state is not known (the roadmap's risk: Lomiri's window logic); what
it means on the screen, sideways, is checked with a game.

Fill mode (our 0010, a window smaller than the screen shown over all of it) is not ported: it
extends our own scaler, which this base does not use. Windowed games on Proton's base stay 1:1 in
a corner (Ys IX, Stray, ATRI in step 4); the games that need this base so far (Goat Simulator) run
in a window too.

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
- 2026-11-20: step 3: patches ported on a branch (`utdeck-port`) and exported to `patches-proton/`;
  0006 replaced by Proton's own scaler (`fs_hack`); 0002 needed flags 0 instead of
  `SEND_HWMSG_RAWINPUT`; built with the Steam bridge; `UTDECK_RUNTIME=proton` added. The graphics tests
  pass on Proton's Wine; a first, unfair comparison shows it much faster, and our runtime hung after a
  resize: both for step 4.
- 2026-11-20: step 4: three alternated rounds and a bisect showed Proton's base "2x faster";
  patches 0012 and 0013 written while chasing it (both fix real waste, neither changed the speed).
  With the screen on (it had been off all night), Proton's window turned out to vanish after a second:
  Proton patch 0009. A test script's own `WINEDLLOVERRIDES` let Wine offer to install Mono (cancelled);
  fixed. Lomiri was in landscape because the phone lay flat; the fair runs were made upright.
- 2026-11-20: step 4: with both pictures on the screen, Proton's base is 0-10 % faster; its 30-70 %
  in display mode tests was a 1x1 or unscaled buffer. **Plan change:** both bases are kept, as two
  compatibility tools (the maintainer's proposal); roadmap rewritten.
- 2026-11-20: step 4's games: Ys IX equal (Proton 15.4, ours 14.9 fps); ATRI works on both, not
  comparable at its scene; Stray equal (9.1 and 9.2 fps); **Goat Simulator plays on Proton's base
  and fails at its first level on ours** with the same settings; Ys's DirectShow movies fail on
  both bases, differently. Step 4 closed with the per-game list.
- 2026-11-20: step 5 started: the second tool "UTDeck runtime (Proton 11)", `get-runtime.sh` and
  `runtimes.list`, Proton patches 0006 (the fullscreen hack on Wayland), 0012, 0013 and 0014 (no
  OpenGL video path); Proton's runtime rebuilt and installed through `get-runtime.sh`. During the
  restart of hidden Steam to install the tools, the phone dropped off USB.
