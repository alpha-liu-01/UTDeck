# Stage 4.8: the app with a controller, the plan

Written 2026-11-23, when 4.7 closed. The maintainer's choice (2026-11-23), among three candidates for
the next stage: a controller-driven interface now; keyboard and mouse as test items of the game round
(now 4.9); the touch overlay in stage 5, where it was planned.

Goal: UTDeck is used from start to finish with a controller, as a Steam Deck is: picking a game,
starting it, its settings, Steam's own tasks, the "Steam is asking" page, back to the library when the
game ends, without touching the screen. Touch keeps working as now; a physical keyboard navigates the
same way as the controller.

Exit: on stone, with the BETOP T6 (Xbox mode) and the DualSense, the screen untouched: start a game from
the library and play it; open a game's settings, change one and save; check a game's files; uninstall
and install a game (Steam's Install and Uninstall windows answered on the "Steam is asking" page);
open the Controllers page and come back; the library returns after the game. Then stage 4's exit run
(from a fresh install: add a game, start it, play with a gamepad, quit back to the library).

## Terms used here

- *Focus* = the one control (a game card, a button, a list row) that a key press acts on. Lomiri's
  controls draw a frame around it when it was reached with keys (`keyNavigationFocus`).
- *Key events* = what a keyboard sends an app (arrow keys, Enter, Escape). The controller's buttons
  are turned into these, so the app's pages need no controller code of their own.
- *Positional buttons* = buttons named by where they sit, not by their label: SDL calls the bottom
  face button "A" on every controller (Xbox's A, PlayStation's cross, a Nintendo-style pad's B).

## What exists

- **Controllers through SDL in the app** (4.4, `app/src/Controllers.*`): the phone's own SDL 2
  (`libSDL2-2.0.so.0`, loaded at run time, the library Wine's `winebus` reads controllers through), with
  UTDeck's layouts from `padlayout.py` where SDL's own does not fit (stone's `hid-nintendo`). Opened
  only while the Controllers page is open (`start()`/`stop()`), polled every 33 ms. Positional names,
  as games get them.
- **Lomiri Components with key navigation**, on stone's Qt 6 build: `keyNavigationFocus`,
  `activeFocusOnTab` and a focus frame in `UCStyledItemBase` and `UCListItem` (checked in
  `libLomiriComponents.so`, 2026-11-23). Qt Quick's `GridView` and `ListView` move their current item
  with the arrow keys.
- **Knowing when a game is in front**: `library.gameInFront` (Wine's focus file, 4.1) and
  `Qt.application.state`. While a game is in front its window carries UTDeck's app id, so the app
  stays awake, and both the game and the app could read the controller.
- **The pages** (13): Library (a grid of cards; the header's Add, Downloads, Settings, Refresh; a long
  press opens a game's page), Add a game, Your Steam library (a search field and a list), Downloads,
  a game's Settings (selectors, switches, text fields, buttons), Edit (the user's own game), Settings,
  Controllers, Setup (first start, QR code), Starting, "Steam is asking" (a picture of Steam's window,
  taps passed through), and the dialogs inside pages.

## How it would work

- **One controller bridge for the whole app**: `Controllers` (or a small sibling sharing its SDL)
  keeps SDL open while the app is in front and **no game is**, and turns button presses into key
  events for the app's focused window: D-pad and left stick to arrow keys (repeating while held),
  A to Enter, B to Escape (back), and so on (the map below). Not while a game is in front: the game
  has the controller to itself; and not while the screen is off.
- **Focus on every page**: each page sets where focus starts (the library: the first card, or the
  card of the game last played), and which control the arrows reach next where Qt's own order is
  wrong (`KeyNavigation`). Escape goes back one page (the header's back action), as B does on a
  console.
- **What a long press does now gets a button**: on a game card, X opens its page (the long press);
  Y opens "Add a game"; Start opens Settings; the shoulder buttons move between the header's actions or
  the page's sections.
- **The "Steam is asking" page**: Steam's dialogs are pictures of a hidden window. First try: pass
  Tab, arrow keys and Enter to Steam's window (XTest, as taps are passed now): Steam's own dialogs may
  move their focus with Tab. If that does not reach every button: a cursor on the picture, moved by
  the left stick, A taps where it is.
- **Text fields** (the search in "Your Steam library", launch options, the advanced settings): Lomiri's
  on-screen keyboard needs touch. In 4.8 they are reached and left with the controller but typed with
  touch or a keyboard; the search gets jumping by first letter with the shoulder buttons instead. An
  on-screen keyboard for the controller is stage 5's (with the touch overlay).
- **Feedback**: Lomiri's focus frame, made clearly visible on the cards (a light border, the card a
  little larger), and a hint line of the buttons at the bottom of the library (A Play, X Settings, Y
  Add, Start Settings), shown once a controller has been used.

The map (by position, except confirm and back on Nintendo-style pads, which follow their labels: the
maintainer's answers below):

| Button | What it does |
| --- | --- |
| D-pad, left stick | move the focus (held: repeats) |
| A (bottom; on a Nintendo-style pad, A on the right) | press: play, open, switch, confirm |
| B (right; on a Nintendo-style pad, B at the bottom) | back one page, close a menu, cancel a dialog |
| X (left) | a game card: its settings page (the long press) |
| Y (top) | the library: "Add a game"; a list: its search |
| LB / RB | the previous / next section or header action; in long lists, a page up / down |
| Start (Menu) | the library: Settings |
| Back (View) | the library: Downloads |
| right stick | scroll the page |

## Steps

1. **Find out, on stone**: SDL kept open in the app while a game runs elsewhere (does the game still
   get every button? SDL reads the kernel's event devices without taking them, but checked); the app
   not reading while a game is in front or the screen is off; Lomiri's focus frame on its own controls
   with key events (how it looks, where Qt's order goes wrong); whether Steam's dialogs take Tab and
   Enter through XTest.
2. **The controller bridge**: SDL in the app while it is in front and no game is, buttons to key
   events with repeat, the controller connected or removed while the app runs, the Controllers page
   using the same SDL.
3. **The library**: focus on the cards, A to play, X to the game's page, the header actions, the hint
   line, focus back on the last game after it ends.
4. **The other pages**, one by one: Settings, a game's Settings (selectors, switches, save), Add a game,
   Your Steam library (with first-letter jumps), Downloads, Edit, Controllers, Starting (B: back to the
   library as its button does), Setup.
5. **The "Steam is asking" page**: keys through to Steam's window, or the cursor.
6. **The exit run** with the T6 and the DualSense, then stage 4's exit run.

Test device: stone (the T6 is not reliable on the Nothing Phone (1)).

## Questions for the maintainer

- **Which button confirms**: the bottom one everywhere (Xbox and PlayStation style, as Steam and the
  Steam Deck do, and as SDL names them), or the right one on a Nintendo-style pad (the T6 in Switch
  mode)? Proposed: the bottom one everywhere, one rule for every controller.
- **The Home (Guide) button during a game**: nothing in 4.8 (an in-game menu is stage 5's), or should
  it already bring UTDeck's library forward while the game keeps running?

## Answers from the maintainer (2026-11-23)

- **The confirm button**: the bottom one (A on an Xbox pad, cross on a PlayStation pad), **except on
  Nintendo-style controllers** (the T6 in Switch mode), where the button labelled A, on the right,
  confirms and the one labelled B, at the bottom, goes back: as each controller's own labels say.
- **The Home button during a game**: nothing for now (stage 5's in-game menu).

## Not in 4.8

- Controller text input (an on-screen keyboard), the in-game menu, the touch overlay: stage 5.
- Keyboard and mouse in games: test items of the game round (4.9). (The app itself is navigated by a
  physical keyboard too, as a side effect of the key events.)

## Step 1: found out, on stone (2026-11-23)

- **Steam's dialogs and keys** (`xwin.py` got `--key` and `--point`: key presses through XTest; with no
  window manager on the hidden screen the keyboard follows the pointer). On Steam's Install window (Plants
  vs. Zombies: Game of the Year, 49 MB, not installed): Tab moves Steam's focus (after two presses "Create
  an application shortcut" was drawn darker), but after two more nothing on the picture showed where the
  focus was. A button pressed through a picture of the window has to be seen first, so **A taps where a
  cursor points** (moved by the left stick), not Tab and Enter. **Escape closes Steam's window as Cancel
  does** (the game stayed uninstalled): **B sends Escape** to Steam's dialog.
- No controller was connected; the questions that need one (the app's SDL next to a running game, the
  focus frames with key events) are answered with the bridge in place (step 2).
- Seen on the way: the app holds every input device of the phone open (power key, touchscreen,
  headset jack, fingerprint), with the screen on or off; not SDL (no SDL thread runs, and the count does
  not change when navigation is off) but `libLomiriSystemInfo-Qt6` (Lomiri's device information, which
  Lomiri's components load to know whether a keyboard or mouse is attached; it links `libevdev`). Present
  before 4.8; it only reads.

## Step 2-3: the bridge and the library (UTDeck 0.3.45)

- `Controllers` keeps SDL open while its page is open or navigation is on (`navigation`, set by Main.qml
  while the window is active and the app is active: the window hides while a game is in front, so SDL
  is closed during games). Each poll (33 ms) turns new button presses into key events for the focused
  window (`navigate()`): Return, Escape, Menu (X), Search (Y), Page Up/Down (LB/RB), F2 (Start), F3
  (Back); directions from the D-pad or the left stick past half way, repeating after 400 ms every 120 ms;
  the right stick as wheel events in the middle of the window. On a Nintendo-style pad (vendor 057e)
  confirm and back, X and Y follow the labels. `navigationPaused` (the Controllers page): only back held
  for a second (Escape).
- Main.qml: Escape goes back one page (unless the page took it). The library: the grid has the focus;
  the current card gets a frame and grows a little once a key or button has been used; Return plays,
  Menu opens the game's page, Search "Add a game", F2 the settings, F3 the downloads. A keyboard does the
  same with its keys.

## The maintainer's first test, and fixes (UTDeck 0.3.46)

1. **The selected card reached into the page's header** (it was enlarged by 5 %): the frame stays, the
   enlargement goes, and the grid has half a grid unit of room above and below for the frame.
2. **Pages opened on top of each other** (X then Y opened both): the library kept the focus under the
   page pushed over it. Now each page that comes to the front gets the focus (`Main.qml`'s `focusPage`:
   the page's own `focusFirst()`, or its first control in Qt's tab order), and the library's keys act
   only while it is the page shown.
3. **Nothing inside a page could be reached**: Main.qml now moves the focus with the arrows to the
   previous or next control of the page (Qt's tab order, which Lomiri's controls take part in, with
   Lomiri's focus frame: `forceActiveFocus(Qt.TabFocusReason)`) when the focused control does not use
   the arrow itself, scrolls it into view inside the page's Flickable, and presses it with Return
   (`trigger()` or `clicked()`). On the "Steam is asking" page, B (Escape) now cancels Steam's dialog:
   `SteamLaunch::cancel()` presses Escape in Steam's window (with the pointer on it: no window manager,
   the keyboard follows the pointer), as Cancel does.
4. **else Heart.Break()'s first start** (not a controller matter): Steam reported the game started at
   15:28:42; `utdeck-compat` then prepared its Wine folder until 15:31:32 (prefix made, brought up to the
   runtime, DXVK, Wine Mono 31 s, Steam's registry keys). Making the prefix (`wineboot -i`) ran with
   Wine's display drivers on, so Wine's "setting up" window showed over the wallpaper (only the update
   step had them off since 4.3); and the starting page, waiting 45 s for the game after Steam's
   "Completed", said the launch had failed. Now `wineboot -i` runs without a window too, and
   `utdeck-compat` writes what it prepares into `$XDG_RUNTIME_DIR/utdeck/preparing` ("APPID what", at
   each step that does work: the Windows folder, the update with DXVK, Wine Mono, the Visual C++
   repair), removed before the game starts; `Library::preparing()` reads it, and the starting page shows
   "Preparing Wine for this game: ..." with a hint that first starts take a few minutes, and does not
   count it as waiting.

## The maintainer's second test, and fixes (UTDeck 0.3.47)

Fixed in 0.3.46: the card in the header, X then Y. Two more:

1. **The screen went off while the app was used with the controller.** The phone counts touches as use,
   not the controller (read through SDL, around Lomiri). `Controllers` now emits `used()` (a button, or a
   stick past a quarter, at most once a second, also on the Controllers page), and the app holds the
   screen on (`KeepAwake`, key "controller") until 15 s after the last use. repowerd then counts its own
   screen timeout from the start (checked on stone: with a 60 s timeout, a 75 s hold ended and the screen
   went off 61 s later), so the screen goes off as long after the last button as after the last touch.
   Not a hold for as long as the app is in front (the maintainer's first idea): left on the library, the
   phone would never sleep.
2. **Switches did not move with A** (selectors did): Lomiri's buttons, switches and list rows press
   themselves when Return is released, and Main.qml also pressed them when it was pressed: a switch
   toggled twice. Main.qml now presses only controls that do not handle Return themselves.

## Steps 4-5: the other pages and the "Steam is asking" page (UTDeck 0.3.48)

What every page now does with the controller (Main.qml's rules for all of them, then each page's own):

- **The arrows' stops** (`Main.qml`'s `stopsAt`): visible, enabled controls below the page's header (the
  header's own icons are left to B and the buttons); not a list row that holds its own control (a switch
  row: the switch is the stop, so one A toggles it); not a read-only text such as a game's log (its cursor
  would take the arrows line by line: the right stick scrolls it); never back into the control the focus
  is in (a text field's inner input), and never round from the end of the page to its top.
- **Text fields**: Lomiri opens the on-screen keyboard whenever a field gets the focus (its
  `InputHandler`, read on stone). The arrows passing through a field close it again; A on a field opens
  it (typing is by touch or a keyboard until stage 5); B closes an open keyboard before it leaves the
  page.
- **LB / RB**: the focus about a screen up or down (a long settings page).
- **The library**: a hint line of the buttons at the bottom once a controller has been used, with the
  labels of the controller used last (Xbox, PlayStation: Cross, Square, Triangle, Options, Create;
  Nintendo: A, X, Y, +, -). The selected card stays selected when the library is read again (each time
  the app comes to the front, so also after a game): the model is reset by a refresh, which moved the
  grid to the first card; now `Library::indexOf` finds the card again.
- **Your Steam library**: the focus starts on the list, not the search field (which would open the
  keyboard); the list moves with Lomiri's own list keys; A opens a row and puts the focus on its Install
  button; B closes an open row; **LB / RB jump to the first game of the previous or next first letter**.
- **Add a game, Edit**: the covers are reachable (framed while focused; A picks one); on Add a game, B
  goes back a step as the header's Back does, and leaves the page from the first step.
- **Starting**: a failed start puts the focus on what can be done ("Try again", "End the game" or
  "Settings"); B goes back to the library, as "Back to the library".
- **Setup**: "Can't scan the code?" is reachable (underlined while focused). Steam's own sign-in window
  (the fallback) needs typing: touch or a keyboard.
- **Controllers**: unchanged (the buttons are being tried there; B held for a second goes back).
- **Downloads**: nothing to press; B goes back, the right stick scrolls.

**Step 5, the "Steam is asking" page**: a pointer on the picture of Steam's dialog (a ring with a dot),
shown once a controller or a key has been used. The left stick moves it smoothly (`Controllers`'
`cursorMode`: while the page is in front, the left stick sends `cursorMoved` each poll instead of arrow
keys, faster the further it is pushed), the D-pad and a keyboard's arrows in steps; A clicks where it
points (the same tap as a finger's, which also moves the pointer there); the right stick scrolls the
dialog where the pointer is (a wheel notch per 120 of wheel movement); B cancels Steam's dialog, as before.

## Done (2026-11-23)

The maintainer's test of 0.3.48 on stone: everything works (the library with its hints, the pages with
the arrows, text fields, LB/RB, Your Steam library, the "Steam is asking" page with the pointer). 4.8 is
closed. Left for stage 4's exit run: the whole app from a fresh install, with a controller.
