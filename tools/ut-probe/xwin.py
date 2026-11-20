#!/usr/bin/env python3
"""UTDeck: look at an X11 screen without X tools (the phone has libX11 but no xwininfo or xwd).

    xwin.py [DISPLAY]                 list top-level windows: id, mapped or not, size, position, title
    xwin.py [DISPLAY] --shot OUT.ppm  also save the whole screen as a PPM picture
    xwin.py [DISPLAY] --close         ask every mapped top-level window to close, as its close
                                      button would (WM_DELETE_WINDOW); Steam then hides it
    xwin.py [DISPLAY] --fit           move and resize every mapped top-level window to the screen
                                      (Steam opens its window at the phone's size, larger than :50)
    xwin.py [DISPLAY] --click X Y     a left mouse click at X,Y, through X11's test extension (XTest)
Options combine; the screenshot is taken last.

Used for hidden Steam's Xvfb screen (:50), where Steam's windows are drawn but nobody sees them.
Screenshots can show account data (Steam's interface): they are copied only into build/, which git
ignores.
"""
import ctypes
import ctypes.util
import sys

args = [a for a in sys.argv[1:]]
shot = None
close = "--close" in args
if close:
    args.remove("--close")
fit = "--fit" in args
if fit:
    args.remove("--fit")
click = None
if "--click" in args:
    i = args.index("--click")
    click = (int(args[i + 1]), int(args[i + 2]))
    del args[i:i + 3]
if "--shot" in args:
    i = args.index("--shot")
    shot = args[i + 1]
    del args[i:i + 2]
display_name = (args[0] if args else ":50").encode()

X = ctypes.CDLL("libX11.so.6")
X.XOpenDisplay.restype = ctypes.c_void_p
X.XOpenDisplay.argtypes = [ctypes.c_char_p]
X.XDefaultRootWindow.restype = ctypes.c_ulong
X.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
X.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                         ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                         ctypes.POINTER(ctypes.c_uint)]
X.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p)]
X.XGetImage.restype = ctypes.c_void_p
X.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint,
                        ctypes.c_uint, ctypes.c_ulong, ctypes.c_int]
X.XGetPixel.restype = ctypes.c_ulong
X.XGetPixel.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]


class Attrs(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int), ("width", ctypes.c_int), ("height", ctypes.c_int),
                ("border_width", ctypes.c_int), ("depth", ctypes.c_int), ("visual", ctypes.c_void_p),
                ("root", ctypes.c_ulong), ("class_", ctypes.c_int), ("bit_gravity", ctypes.c_int),
                ("win_gravity", ctypes.c_int), ("backing_store", ctypes.c_int),
                ("backing_planes", ctypes.c_ulong), ("backing_pixel", ctypes.c_ulong),
                ("save_under", ctypes.c_int), ("colormap", ctypes.c_ulong),
                ("map_installed", ctypes.c_int), ("map_state", ctypes.c_int),
                ("all_event_masks", ctypes.c_long), ("your_event_mask", ctypes.c_long),
                ("do_not_propagate_mask", ctypes.c_long), ("override_redirect", ctypes.c_int),
                ("screen", ctypes.c_void_p)]


X.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(Attrs)]
X.XInternAtom.restype = ctypes.c_ulong
X.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]


class ClientMessage(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int),
                ("display", ctypes.c_void_p), ("window", ctypes.c_ulong), ("message_type", ctypes.c_ulong),
                ("format", ctypes.c_int), ("l", ctypes.c_long * 5)]


class XEvent(ctypes.Union):
    _fields_ = [("xclient", ClientMessage), ("pad", ctypes.c_long * 24)]


X.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.POINTER(XEvent)]
X.XFlush.argtypes = [ctypes.c_void_p]


def close_window(d, w):
    ev = XEvent()
    ev.xclient.type = 33  # ClientMessage
    ev.xclient.window = w
    ev.xclient.message_type = X.XInternAtom(d, b"WM_PROTOCOLS", 0)
    ev.xclient.format = 32
    ev.xclient.l[0] = X.XInternAtom(d, b"WM_DELETE_WINDOW", 0)
    ev.xclient.l[1] = 0  # CurrentTime
    X.XSendEvent(d, w, 0, 0, ctypes.byref(ev))

d = X.XOpenDisplay(display_name)
if not d:
    sys.exit(f"cannot open display {display_name.decode()}")
root = X.XDefaultRootWindow(d)
r, p, children, n = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
X.XQueryTree(d, root, ctypes.byref(r), ctypes.byref(p), ctypes.byref(children), ctypes.byref(n))
state = {0: "unmapped", 1: "unviewable", 2: "MAPPED"}
for i in range(n.value):
    w = children[i]
    a = Attrs()
    X.XGetWindowAttributes(d, w, ctypes.byref(a))
    name = ctypes.c_char_p()
    X.XFetchName(d, w, ctypes.byref(name))
    title = name.value.decode(errors="replace") if name.value else ""
    print(f"{w:#010x} {state.get(a.map_state, a.map_state):10} {a.width}x{a.height}+{a.x}+{a.y} {title!r}")
    if close and a.map_state == 2:
        close_window(d, w)
        print(f"  close requested for {w:#010x}")
if close:
    X.XFlush(d)

if fit:
    import time
    a = Attrs()
    X.XGetWindowAttributes(d, root, ctypes.byref(a))
    X.XMoveResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint]
    for i in range(n.value):
        w = children[i]
        b = Attrs()
        X.XGetWindowAttributes(d, w, ctypes.byref(b))
        if b.map_state == 2:
            X.XMoveResizeWindow(d, w, 0, 0, a.width, a.height)
            print(f"  {w:#010x} resized to {a.width}x{a.height}")
    X.XFlush(d)
    time.sleep(3)  # let the program draw at its new size

if click:
    import time
    Xtst = ctypes.CDLL("libXtst.so.6")
    Xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
    Xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    Xtst.XTestFakeMotionEvent(d, -1, click[0], click[1], 0)
    X.XFlush(d)
    time.sleep(0.2)
    Xtst.XTestFakeButtonEvent(d, 1, 1, 0)
    X.XFlush(d)
    time.sleep(0.1)
    Xtst.XTestFakeButtonEvent(d, 1, 0, 0)
    X.XFlush(d)
    print(f"  clicked at {click[0]},{click[1]}")
    time.sleep(2)

if shot:
    a = Attrs()
    X.XGetWindowAttributes(d, root, ctypes.byref(a))
    img = X.XGetImage(d, root, 0, 0, a.width, a.height, 0xFFFFFFFF, 2)  # ZPixmap
    with open(shot, "wb") as f:
        f.write(f"P6 {a.width} {a.height} 255\n".encode())
        row = bytearray(a.width * 3)
        for y in range(a.height):
            for x in range(a.width):
                v = X.XGetPixel(img, x, y)
                row[3 * x:3 * x + 3] = bytes(((v >> 16) & 255, (v >> 8) & 255, v & 255))
            f.write(row)
    print(f"screen saved: {shot} ({a.width}x{a.height})")
