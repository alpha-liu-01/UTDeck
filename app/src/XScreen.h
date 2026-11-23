#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A capture of an X11 screen, and taps and key presses sent to it (stage 4.2: hidden Steam's screen,
// Xvfb :50). Kept apart from Qt and ZXing: X11's headers define macros (None, Bool, Status, ...)
// that clash with their names.
struct XScreenImage
{
  std::vector<uint8_t> pixels;  // 4 bytes per pixel, B G R X
  int width = 0;
  int height = 0;
  int stride = 0;  // bytes per row
};

// The whole screen of DISPLAY (":50"); false when the display cannot be opened or read.
bool CaptureXScreen(const char* display, XScreenImage* image);

// Stage 4.2, fallback B (hidden Steam's sign-in window inside the app), through X11's test extension
// (XTest), as a real mouse and keyboard would:
// a left click at X,Y of the screen;
bool XClick(const char* display, int x, int y);
// STEPS turns of the mouse wheel at X,Y (negative: up), as one notch each;
bool XScroll(const char* display, int x, int y, int steps);
// the characters of TEXT typed (a character missing from the screen's keyboard map gets a spare key
// for the moment); nothing of the text is kept;
bool XTypeText(const char* display, const std::u32string& text);
// one named key pressed and released ("BackSpace", "Return", "Tab").
bool XPressKey(const char* display, const char* keysym_name);

// Stage 4.3, hidden Steam's dialogs: the screen's mapped top-level windows, bottom to top (the
// last one is in front).
struct XWindowRect
{
  unsigned long id = 0;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};
std::vector<XWindowRect> ListXWindows(const char* display);
// Every mapped top-level window at least WIDTH x HEIGHT asked to close, as its close button would
// (WM_DELETE_WINDOW): Steam then hides its main window. The number asked.
int CloseLargeXWindows(const char* display, int width, int height);
