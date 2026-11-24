#include "XScreen.h"

#include <cstring>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

namespace
{
// Xlib's default error handler ends the program; a failed request here only means "no capture".
int IgnoreXError(Display*, XErrorEvent*)
{
  return 0;
}
}  // namespace

bool CaptureXScreen(const char* display_name, XScreenImage* image)
{
  static bool handler_set = false;
  if (!handler_set)
  {
    XSetErrorHandler(IgnoreXError);
    handler_set = true;
  }
  Display* display = XOpenDisplay(display_name);
  if (!display)
    return false;
  const int screen = DefaultScreen(display);
  const Window root = RootWindow(display, screen);
  const int width = DisplayWidth(display, screen);
  const int height = DisplayHeight(display, screen);
  XImage* x_image = XGetImage(display, root, 0, 0, width, height, AllPlanes, ZPixmap);
  bool ok = false;
  // Xvfb's 24-bit screen: 32 bits per pixel, blue in the lowest byte on this little-endian phone.
  if (x_image && x_image->bits_per_pixel == 32 && x_image->byte_order == LSBFirst)
  {
    image->width = width;
    image->height = height;
    image->stride = x_image->bytes_per_line;
    image->pixels.resize(static_cast<size_t>(image->stride) * height);
    std::memcpy(image->pixels.data(), x_image->data, image->pixels.size());
    ok = true;
  }
  if (x_image)
    XDestroyImage(x_image);
  XCloseDisplay(display);
  return ok;
}

namespace
{
Display* OpenForInput(const char* display_name)
{
  XSetErrorHandler(IgnoreXError);  // a failed request is no reason to end the app
  int event_base, error_base, major, minor;
  Display* display = XOpenDisplay(display_name);
  if (display && !XTestQueryExtension(display, &event_base, &error_base, &major, &minor))
  {
    XCloseDisplay(display);
    return nullptr;
  }
  return display;
}

void Tap(Display* display, KeyCode code, bool shift)
{
  const KeyCode shift_code = XKeysymToKeycode(display, XK_Shift_L);
  if (shift)
    XTestFakeKeyEvent(display, shift_code, True, CurrentTime);
  XTestFakeKeyEvent(display, code, True, CurrentTime);
  XTestFakeKeyEvent(display, code, False, CurrentTime);
  if (shift)
    XTestFakeKeyEvent(display, shift_code, False, CurrentTime);
  XSync(display, False);
}
}  // namespace

bool XClick(const char* display_name, int x, int y)
{
  Display* display = OpenForInput(display_name);
  if (!display)
    return false;
  XTestFakeMotionEvent(display, -1, x, y, CurrentTime);
  XTestFakeButtonEvent(display, 1, True, CurrentTime);
  XTestFakeButtonEvent(display, 1, False, CurrentTime);
  XSync(display, False);
  XCloseDisplay(display);
  return true;
}

bool XScroll(const char* display_name, int x, int y, int steps)
{
  Display* display = OpenForInput(display_name);
  if (!display)
    return false;
  XTestFakeMotionEvent(display, -1, x, y, CurrentTime);
  const unsigned int button = steps < 0 ? 4 : 5;   // X11's wheel: 4 up, 5 down
  for (int i = 0; i < (steps < 0 ? -steps : steps); ++i)
  {
    XTestFakeButtonEvent(display, button, True, CurrentTime);
    XTestFakeButtonEvent(display, button, False, CurrentTime);
  }
  XSync(display, False);
  XCloseDisplay(display);
  return true;
}

bool XPressKeyAt(const char* display_name, int x, int y, const char* keysym_name)
{
  Display* display = OpenForInput(display_name);
  if (!display)
    return false;
  XTestFakeMotionEvent(display, -1, x, y, CurrentTime);
  XSync(display, False);
  XCloseDisplay(display);
  return XPressKey(display_name, keysym_name);
}

bool XPressKey(const char* display_name, const char* keysym_name)
{
  Display* display = OpenForInput(display_name);
  if (!display)
    return false;
  const KeySym sym = XStringToKeysym(keysym_name);
  const KeyCode code = sym == NoSymbol ? 0 : XKeysymToKeycode(display, sym);
  if (code)
    Tap(display, code, false);
  XCloseDisplay(display);
  return code != 0;
}

bool XTypeText(const char* display_name, const std::u32string& text)
{
  Display* display = OpenForInput(display_name);
  if (!display)
    return false;
  int min_code = 0, max_code = 0, per_code = 0;
  XDisplayKeycodes(display, &min_code, &max_code);
  bool ok = true;
  for (const char32_t c : text)
  {
    // Latin-1 characters are their own keysyms; the rest are 0x01000000 + the code point.
    const KeySym sym = c < 0x100 ? static_cast<KeySym>(c) : static_cast<KeySym>(0x01000000 | c);
    KeyCode code = XKeysymToKeycode(display, sym);
    bool shift = false;
    KeyCode spare = 0;
    if (code)
    {
      // The second keysym of a key is its shifted one ("A" on the "a" key).
      KeySym* syms = XGetKeyboardMapping(display, code, 1, &per_code);
      shift = syms && per_code > 1 && syms[0] != sym && syms[1] == sym;
      if (syms)
        XFree(syms);
    }
    else
    {
      // Not on the keyboard map: a key with no symbol gets this one, for this character only.
      KeySym* map = XGetKeyboardMapping(display, min_code, max_code - min_code + 1, &per_code);
      for (int k = max_code; map && k >= min_code && !spare; --k)
      {
        bool empty = true;
        for (int i = 0; i < per_code; ++i)
          empty = empty && map[(k - min_code) * per_code + i] == NoSymbol;
        if (empty)
          spare = static_cast<KeyCode>(k);
      }
      if (map)
        XFree(map);
      if (!spare)
      {
        ok = false;
        continue;
      }
      KeySym one = sym;
      XChangeKeyboardMapping(display, spare, 1, &one, 1);
      XSync(display, False);
      code = spare;
    }
    Tap(display, code, shift);
    if (spare)
    {
      KeySym none = NoSymbol;
      XChangeKeyboardMapping(display, spare, 1, &none, 1);
      XSync(display, False);
    }
  }
  XCloseDisplay(display);
  return ok;
}

std::vector<XWindowRect> ListXWindows(const char* display_name)
{
  std::vector<XWindowRect> list;
  XSetErrorHandler(IgnoreXError);
  Display* display = XOpenDisplay(display_name);
  if (!display)
    return list;
  Window root_return, parent_return;
  Window* children = nullptr;
  unsigned int count = 0;
  if (XQueryTree(display, DefaultRootWindow(display), &root_return, &parent_return, &children, &count))
  {
    for (unsigned int i = 0; i < count; ++i)
    {
      XWindowAttributes attributes;
      if (!XGetWindowAttributes(display, children[i], &attributes) || attributes.map_state != IsViewable)
        continue;
      list.push_back({children[i], attributes.x, attributes.y, attributes.width, attributes.height});
    }
    if (children)
      XFree(children);
  }
  XCloseDisplay(display);
  return list;
}

int CloseLargeXWindows(const char* display_name, int width, int height)
{
  XSetErrorHandler(IgnoreXError);
  Display* display = XOpenDisplay(display_name);
  if (!display)
    return 0;
  const Atom protocols = XInternAtom(display, "WM_PROTOCOLS", False);
  const Atom delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
  int asked = 0;
  Window root_return, parent_return;
  Window* children = nullptr;
  unsigned int count = 0;
  if (XQueryTree(display, DefaultRootWindow(display), &root_return, &parent_return, &children, &count))
  {
    for (unsigned int i = 0; i < count; ++i)
    {
      XWindowAttributes attributes;
      if (!XGetWindowAttributes(display, children[i], &attributes) || attributes.map_state != IsViewable ||
          attributes.width < width || attributes.height < height)
        continue;
      XEvent event = {};
      event.xclient.type = ClientMessage;
      event.xclient.window = children[i];
      event.xclient.message_type = protocols;
      event.xclient.format = 32;
      event.xclient.data.l[0] = static_cast<long>(delete_window);
      event.xclient.data.l[1] = CurrentTime;
      XSendEvent(display, children[i], False, NoEventMask, &event);
      ++asked;
    }
    if (children)
      XFree(children);
  }
  XSync(display, False);
  XCloseDisplay(display);
  return asked;
}
