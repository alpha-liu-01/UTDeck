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
