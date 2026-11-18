# Stage 2.2: Wine's Wayland driver on Lomiri

Started and finished 2026-11-18. Plan: [stage2-roadmap.md](stage2-roadmap.md). Goal: a Windows GDI window
(Wine's Notepad, `winecfg`) on Lomiri, in portrait and landscape, with input reaching it.

Terms used here:
- *`winewayland`* = Wine's Wayland display driver. Its Linux side (`winewayland.so`) talks to the
  compositor; its Windows side (`winewayland.drv`) is a thin stub.
- *`wp_viewporter`* = a Wayland extension that lets a client crop and scale a surface's picture.
  Lomiri (Mir 1.8) does not have it; Mir 2 does.
- *Subsurface* = a child surface placed on top of a window; Wine draws a game's Vulkan or OpenGL
  picture on one.

## Steps

| Step | What | Status |
| --- | --- | --- |
| 1 | Our build of `winewayland.so` from Hangover's exact Wine source, as a drop-in replacement | **Done** |
| 2 | Patch 0001: `wp_viewporter` optional | **Done**; the driver starts and draws on Lomiri (log) |
| 3 | Patch 0002: touch, the first finger as the mouse (Wine had no touch input at all) | **Done**; touch bound (log) |
| 4 | On screen: Notepad and `winecfg`, portrait and landscape, taps (manual run M1) | **Done**: drawn and usable by touch; landscape not seen in the log |

## Step 1: building only the Wayland driver

Only the Linux side of the driver needs our patches, so we build just `winewayland.so` and drop
it into the unpacked Hangover 11.16, keeping everything else Hangover's:

- Source: Hangover 11.16 pins `AndreRH/wine` at `3ea3a149` ("Wine version 11.16"; its `wine`
  submodule). Configured as Hangover's Ubuntu 24.04 package does (`--with-mingw=clang
  --enable-archs=arm64ec,aarch64,i386`, read from its `.packaging/ubuntu2404/wine/debian/rules`),
  plus `--without-freetype --without-x`, which only affect other parts of Wine.
- [`tools/wine-build/build-winewayland.sh`](../../tools/wine-build/build-winewayland.sh) (new):
  fetches that commit into `build/wine-src`, applies `tools/wine-build/patches/*.patch`, builds
  Wine's tools for the PC, configures for arm64 and builds only
  `dlls/winewayland.drv/winewayland.so`, stripped, with a `BUILD-INFO`. About a minute after the
  first run. Container: [`tools/wine-build/Dockerfile`](../../tools/wine-build/Dockerfile), the
  Mesa one plus `libxkbcommon-dev`, `libxkbregistry-dev`, `gettext`; llvm-mingw from
  `build/toolchains/` (stage 2.1).
- Checked against Hangover's own file before use: the same libraries (`ntdll.so`, `win32u.so`,
  `libwayland-client`, `libwayland-egl`, `libxkbcommon`, `libxkbregistry`, `libc`), the same
  imported symbols, OpenGL support on (`HAVE_LIBWAYLAND_EGL`), and within 176 bytes of its size
  (with patch 0001 in).
- On the phone, Hangover's original stays next to ours (`winewayland.so.hangover`,
  `winewayland.so.utdeck`); `winewayland.so` is a copy of the one in use.

Problems met on the way: Wine's configure wanted FreeType (only for its fonts:
`--without-freetype`); then it found no Wayland at all, because for a cross build it looks for
`aarch64-linux-gnu-pkg-config` and found none (`PKG_CONFIG=pkg-config`, already pointed at the
arm64 libraries).

## Step 2: patch 0001, `wp_viewporter` optional

[`0001-winewayland-make-wp_viewporter-optional.patch`](../../tools/wine-build/patches/0001-winewayland-make-wp_viewporter-optional.patch).
What Wine 11.16 uses the viewport for, and what the patch does without it:

| Use | Without `wp_viewporter` |
| --- | --- |
| Start-up check (`wayland.c`) | a warning, "windows will not be scaled", instead of stopping |
| Crop a window's buffer to the window: Wine rounds window buffers up to multiples of 128 pixels (`get_surface_rect` in `win32u`) so it does not reallocate at every resize | the window's part is copied into a buffer of its exact size, attached, and freed when Lomiri releases it |
| Scale a window to its configured size (`wayland_surface_reconfigure_size`) | skipped: windows are not scaled (Lomiri reports output scale 1, so nothing needs scaling here) |
| Scale the game's subsurface to the client area | skipped: the subsurface shows at its buffer size; Wine sizes a swapchain to the client area, so they match unless the game renders at another size (2.3 will check) |
| Scale the cursor | only the buffer scale, which Wine already set as a fallback |

The copy costs CPU only for ordinary windows (dialogs, launchers, menus); a game's picture goes
through Vulkan on its own subsurface and is not copied. A new buffer per update is simple but
allocates each time; a small pool is an optimisation for later if dialogs feel slow.

Result (screen off, `WAYLAND_DEBUG`): Wine's Notepad starts; its window gets an `xdg_toplevel`
("Untitled - Notepad"), Lomiri configures it, and Wine attaches 1026x769 buffers (its own buffer is
1152x896) which Lomiri releases. Lomiri describes the screen as 1080x2400 at 120 Hz, scale 1.
Other optional protocols Lomiri lacks are reported and skipped by Wine itself: pointer
constraints and relative pointer (mouse-look), text input (on-screen keyboard), fractional scale,
window icons, clipboard manager.

## Step 3: patch 0002, the first finger as the mouse

Wine's Wayland driver binds the pointer and the keyboard, never touch (`wl_seat_get_touch` is not
called anywhere in Wine 11.16), so on a phone no tap would reach a Windows program.
[`0002-winewayland-first-finger-as-mouse.patch`](../../tools/wine-build/patches/0002-winewayland-first-finger-as-mouse.patch):
when the seat has touch, the first finger moves the mouse to where it touches and holds the left
button until it lifts (Windows' own fallback for programs that do not handle touch); other fingers
are ignored. It shares the pointer's focus and button serial, so moving windows and opening menus
work from a tap as from a click. Real multi-touch and on-screen controls are stage 5.

Result (screen off): every Wine process now asks Lomiri for touch (`wl_seat.get_touch`).

## Step 4: on screen

**Manual run M1 (screen on and unlocked):**
```
cd /home/phablet/utdeck-probe/wine && ./wine-m1.sh
```
([`wine-m1.sh`](../../tools/ut-probe/wine-m1.sh)) It opens Wine's Notepad, then `winecfg`, one
after the other, each with every Wayland message logged. For each window, please note:
1. Does it appear? Where (fills the screen, or a smaller window at the top left with black
   around it)? Is it drawn correctly (menu bar, borders, text), or shifted, striped or garbled?

Both windows appeared, and they are both drawn correctly (though with a low DPI, which should be expected). 
They both are smaller windows on top of the lomiri desktop (with the rest of the desktop and wallpaper around them).

2. Taps: in Notepad, tap **File** (does the menu open?), then **Help > About Notepad** (does the
   dialog open?) and **OK**. In `winecfg`, tap through a few tabs.
   Yes, the dialog did open, (and the main window disappeared when the dialog opened. The main window is still accessible in recent apps though). Tabs in winecfg work normal.

3. Rotate the phone to landscape and back: is the window still drawn correctly?
Yes. 
4. Close it: Notepad with **File > Exit**, `winecfg` with **OK**.
They both work. 

A photo or screenshot helps if something looks wrong. The on-screen keyboard is not expected to
appear (Lomiri has no text-input protocol for Wine to use); typing is for later.

phablet@ubuntu-phablet:~/utdeck-probe$ cd /home/phablet/utdeck-probe/wine && ./wine-m1.sh
== notepad: look at it, tap it, then close it
   exit 0 (124 = closed after 3 minutes)
== winecfg: look at it, tap it, then close it
   exit 0 (124 = closed after 3 minutes)
done


**M1 result (run by the maintainer, 2026-11-18): successful.** The maintainer reports it works;
the logs (`m1-notepad.log`, `m1-winecfg.log`) show:

| | Notepad (about 50 s) | `winecfg` (about 3 s) |
| --- | --- | --- |
| Windows opened | "Untitled - Notepad", then **"About Notepad"** and **"Font"** (opened from the menus by taps) | "Wine configuration" |
| Taps (`wl_touch` down / up) | 37 / 37 | 1 / 1 |
| Window size | 1026x769, drawn as cropped 1026x769 buffers; dialogs 417x307 and 400x336 | 416x478 |
| Lomiri's suggested size (`configure`) | 1080x2305 (the screen less the status bar) | the same |
| Closed by | a tap on Wine's own title-bar close button (1006, 15) | the same (396, 23) |
| Errors | none beyond the missing optional protocols | none |

What this means:
- **Drawing works without `wp_viewporter`**, for the main window, its menus and dialogs.
- **Touch works**: taps open menus and dialogs and press buttons; a tap on the close button closes
  the window. Lomiri also sends a pointer `enter` when a finger lands (no pointer buttons), which
  only moves the mouse there; no double clicks.
- Windows keep their own size: Lomiri suggests 1080x2305, Wine keeps a normal window at the size
  the program chose (the protocol allows that), so a dialog does not fill the screen. Wine draws
  its own title bar and borders, as Lomiri does not offer server-side decorations. Fine for
  launchers and dialogs; games go fullscreen, which 2.3 checks.
- Landscape: no `configure` with a landscape size appears in either log, and the output's mode
  stayed 1080x2400. Either the phone was not rotated, or Lomiri did not rotate these windows
  (rotation lock, or Lomiri keeps them in portrait). Left for 2.3, where a fullscreen Vulkan
  window must follow rotation anyway.

**Stage 2.2 is done**: a Windows GDI program runs on Lomiri, drawn correctly and usable by touch,
with our two patches to Wine's Wayland driver.

## Events

- 2026-11-18: Hangover 11.16's Wine source located (`AndreRH/wine` `3ea3a149`) and its build
  options read from its packaging. `winewayland` read: the viewport crops window buffers, scales
  windows, the game subsurface and the cursor. Patch 0001 written; build script and container
  written; first builds stopped at FreeType and at the missing cross `pkg-config`; fixed. Our
  `winewayland.so` checked against Hangover's and installed: the driver starts and draws on
  Lomiri (screen off). Found that Wine has no touch input; patch 0002 written and installed.
  Manual run M1 written.
- 2026-11-18: M1 run by the maintainer: Notepad (with its About and Font dialogs) and `winecfg`
  drawn and used by touch (38 taps), closed from their title bars; no errors. Landscape not seen in
  the logs. Stage 2.2 done.
