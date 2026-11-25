/* UTDeck stage 4.10: keyboard and mouse test. Shows what a Windows game receives from a keyboard and
 * a mouse, and writes every event to standard output (the run's log, wine/games/own-ID-<time>.log):
 *   - keys: WM_KEYDOWN/UP and WM_SYSKEYDOWN/UP (name, virtual key, scan code, extended, repeat),
 *     WM_CHAR, and the keys held down;
 *   - mouse: position, buttons (left, right, middle, X1, X2), double clicks, both wheels;
 *   - raw input (WM_INPUT, what most 3D games read): the mouse's relative motion and buttons, and
 *     the keyboard's keys;
 *   - mouse-look (F2 or its button): the cursor hidden, held in the window (ClipCursor) and put back
 *     in the middle after each move (SetCursorPos), as shooters do; the motion turns a view marker.
 * A full-screen borderless window. Quit with its Quit button or Alt+F4.
 * Built by build.sh (x86-64, as games are). */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <windowsx.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOG_LINES 18
#define TRAIL 96

static char log_text[LOG_LINES][160];
static int log_next;
static BOOL held[256];
static int mx = -1, my = -1, buttons, clicks[5], dblclicks, wheel_v, wheel_h;
static POINT trail[TRAIL];
static int trail_len;
static long raw_dx, raw_dy, raw_moves, raw_keys, raw_buttons;
static BOOL look, raw_ok;
static long look_x, look_y, warps;
static RECT quit_rect, look_rect;
static int font_h;
static HFONT font;
static BOOL dirty;
static long moves_since, raw_since;
static DWORD last_report;

static void out( const char *fmt, ... )
{
    char buf[200];
    va_list args;
    DWORD written;
    HANDLE h = GetStdHandle( STD_OUTPUT_HANDLE );
    int len;

    va_start( args, fmt );
    len = vsnprintf( buf, sizeof(buf) - 1, fmt, args );
    va_end( args );
    if (len < 0) return;
    if (len > (int)sizeof(buf) - 2) len = sizeof(buf) - 2;
    lstrcpynA( log_text[log_next], buf, sizeof(log_text[0]) );
    log_next = (log_next + 1) % LOG_LINES;
    buf[len++] = '\n';
    if (h && h != INVALID_HANDLE_VALUE) WriteFile( h, "kbmtest: ", 9, &written, NULL ), WriteFile( h, buf, len, &written, NULL );
    dirty = TRUE;
}

static const char *key_name( LPARAM lparam, UINT vk, char *buf, int size )
{
    if (GetKeyNameTextA( (LONG)lparam, buf, size ) <= 0) snprintf( buf, size, "VK 0x%02x", vk );
    return buf;
}

static void set_look( HWND hwnd, BOOL on )
{
    RECT rc;
    POINT c;

    look = on;
    if (on)
    {
        GetClientRect( hwnd, &rc );
        c.x = rc.right / 2;
        c.y = rc.bottom / 2;
        ClientToScreen( hwnd, &c );
        GetWindowRect( hwnd, &rc );
        SetCapture( hwnd );
        ClipCursor( &rc );
        while (ShowCursor( FALSE ) >= 0) ;
        SetCursorPos( c.x, c.y );
    }
    else
    {
        ClipCursor( NULL );
        ReleaseCapture();
        while (ShowCursor( TRUE ) < 0) ;
    }
    out( "mouse-look %s", on ? "on: cursor hidden, clipped, put back in the middle after each move" : "off" );
}

static void draw_text( HDC dc, int x, int y, COLORREF color, const char *fmt, ... )
{
    char buf[200];
    va_list args;
    int len;

    va_start( args, fmt );
    len = vsnprintf( buf, sizeof(buf), fmt, args );
    va_end( args );
    if (len < 0) return;
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
    SetTextColor( dc, color );
    TextOutA( dc, x, y, buf, len );
}

static void fill( HDC dc, const RECT *rc, COLORREF color )
{
    HBRUSH brush = CreateSolidBrush( color );
    FillRect( dc, rc, brush );
    DeleteObject( brush );
}

static void paint( HWND hwnd, HDC target )
{
    static const char *button_names[5] = { "Left", "Right", "Middle", "X1", "X2" };
    const COLORREF fg = RGB( 230, 230, 230 ), dim = RGB( 140, 140, 140 ), accent = RGB( 255, 200, 60 );
    RECT rc, box;
    HDC dc;
    HBITMAP bmp;
    int w, h, x, y, i, n, col2;
    char names[600], name[64];

    GetClientRect( hwnd, &rc );
    w = rc.right;
    h = rc.bottom;
    dc = CreateCompatibleDC( target );
    bmp = CreateCompatibleBitmap( target, w, h );
    SelectObject( dc, bmp );
    SelectObject( dc, font );
    SetBkMode( dc, TRANSPARENT );
    fill( dc, &rc, RGB( 20, 22, 28 ) );

    /* title and buttons */
    draw_text( dc, font_h, font_h / 2, accent, "UTDeck keyboard and mouse test" );
    look_rect.left = w - font_h * 22; look_rect.right = w - font_h * 9;
    look_rect.top = font_h / 4; look_rect.bottom = font_h * 2;
    quit_rect.left = w - font_h * 8; quit_rect.right = w - font_h;
    quit_rect.top = font_h / 4; quit_rect.bottom = font_h * 2;
    fill( dc, &look_rect, look ? RGB( 60, 120, 60 ) : RGB( 60, 60, 70 ) );
    draw_text( dc, look_rect.left + font_h / 2, look_rect.top + font_h / 3, fg, "Mouse-look %s (F2)", look ? "on" : "off" );
    fill( dc, &quit_rect, RGB( 120, 50, 50 ) );
    draw_text( dc, quit_rect.left + font_h * 2, quit_rect.top + font_h / 3, fg, "Quit" );

    /* left column: keys */
    x = font_h;
    y = font_h * 3;
    names[0] = 0;
    for (i = 0, n = 0; i < 256; i++)
    {
        if (!held[i]) continue;
        UINT scan = MapVirtualKeyA( i, MAPVK_VK_TO_VSC );
        if (!scan || GetKeyNameTextA( scan << 16, name, sizeof(name) ) <= 0) snprintf( name, sizeof(name), "VK 0x%02x", i );
        if (strlen( names ) + strlen( name ) + 3 < sizeof(names)) { if (n++) strcat( names, ", " ); strcat( names, name ); }
    }
    draw_text( dc, x, y, dim, "Keys held:" );
    draw_text( dc, x + font_h * 7, y, fg, "%s", n ? names : "(none)" );
    y += font_h * 2;
    draw_text( dc, x, y, dim, "Last events (newest at the bottom):" );
    y += font_h * 3 / 2;
    for (i = 0; i < LOG_LINES; i++)
    {
        const char *line = log_text[(log_next + i) % LOG_LINES];
        if (*line) draw_text( dc, x, y, i == LOG_LINES - 1 ? accent : fg, "%s", line );
        y += font_h + font_h / 6;
    }

    /* right column: mouse */
    col2 = w * 11 / 20;
    y = font_h * 3;
    draw_text( dc, col2, y, fg, "Cursor: %d, %d    clicks L %d  R %d  M %d  X1 %d  X2 %d    double %d",
               mx, my, clicks[0], clicks[1], clicks[2], clicks[3], clicks[4], dblclicks );
    y += font_h * 3 / 2;
    for (i = 0; i < 5; i++)
    {
        box.left = col2 + i * font_h * 5; box.right = box.left + font_h * 9 / 2;
        box.top = y; box.bottom = y + font_h * 3 / 2;
        fill( dc, &box, (buttons & (1 << i)) ? RGB( 60, 140, 220 ) : RGB( 50, 50, 60 ) );
        draw_text( dc, box.left + font_h / 3, box.top + font_h / 4, fg, "%s", button_names[i] );
    }
    y += font_h * 2;
    draw_text( dc, col2, y, fg, "Wheel: vertical %d, horizontal %d (notches of 120)", wheel_v, wheel_h );
    y += font_h * 3 / 2;
    draw_text( dc, col2, y, raw_ok ? fg : RGB( 230, 90, 90 ), "Raw input: %s, moves %ld, total %ld, %ld, buttons %ld, keys %ld",
               raw_ok ? "on" : "NOT REGISTERED", raw_moves, raw_dx, raw_dy, raw_buttons, raw_keys );
    y += font_h * 3 / 2;
    draw_text( dc, col2, y, fg, "Mouse-look: view %ld, %ld, cursor put back %ld times", look_x, look_y, warps );
    y += font_h * 3 / 2;

    /* the trail area, with the view marker in mouse-look */
    box.left = col2; box.right = w - font_h; box.top = y; box.bottom = h - font_h;
    fill( dc, &box, RGB( 30, 33, 42 ) );
    if (look)
    {
        int bw = box.right - box.left, bh = box.bottom - box.top;
        int vx = box.left + (int)(((look_x % bw) + bw) % bw), vy = box.top + (int)(((look_y % bh) + bh) % bh);
        RECT m = { vx - font_h / 2, vy - font_h / 2, vx + font_h / 2, vy + font_h / 2 };
        fill( dc, &m, RGB( 90, 220, 90 ) );
    }
    else if (trail_len > 1)
    {
        HPEN pen = CreatePen( PS_SOLID, max( 2, font_h / 8 ), RGB( 60, 140, 220 ) );
        HGDIOBJ old = SelectObject( dc, pen );
        HRGN clip = CreateRectRgnIndirect( &box );
        SelectClipRgn( dc, clip );
        Polyline( dc, trail, trail_len );
        SelectClipRgn( dc, NULL );
        DeleteObject( clip );
        SelectObject( dc, old );
        DeleteObject( pen );
    }

    BitBlt( target, 0, 0, w, h, dc, 0, 0, SRCCOPY );
    DeleteDC( dc );
    DeleteObject( bmp );
    dirty = FALSE;
}

static void key_event( UINT msg, WPARAM wparam, LPARAM lparam )
{
    static const char *kinds[] = { "WM_KEYDOWN", "WM_KEYUP", "WM_SYSKEYDOWN", "WM_SYSKEYUP" };
    BOOL down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    UINT vk = wparam & 0xff;
    char name[64];

    held[vk] = down;
    out( "%-13s %-14s VK 0x%02x scan 0x%02x%s%s", kinds[msg - WM_KEYDOWN], key_name( lparam, vk, name, sizeof(name) ),
         vk, (UINT)(lparam >> 16) & 0xff, (lparam & (1 << 24)) ? " extended" : "",
         down && (lparam & (1 << 30)) ? " (repeat)" : "" );
}

static void button_event( HWND hwnd, int index, BOOL down, BOOL dbl, LPARAM lparam )
{
    static const char *names[5] = { "left", "right", "middle", "X1", "X2" };

    if (down) { buttons |= 1 << index; clicks[index]++; }
    else buttons &= ~(1 << index);
    if (dbl) dblclicks++;
    out( "mouse %s %s at %d, %d", names[index], dbl ? "double click" : down ? "down" : "up",
         GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) );
    if (look && down && index == 0 && PtInRect( &look_rect, (POINT){ GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) } ))
        set_look( hwnd, FALSE );
}

static void raw_input( LPARAM lparam )
{
    RAWINPUT raw;
    UINT size = sizeof(raw);

    if (GetRawInputData( (HRAWINPUT)lparam, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER) ) == (UINT)-1) return;
    if (raw.header.dwType == RIM_TYPEMOUSE)
    {
        USHORT f = raw.data.mouse.usButtonFlags;
        if (raw.data.mouse.lLastX || raw.data.mouse.lLastY)
        {
            raw_moves++;
            raw_since++;
            if (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) raw_dx = raw.data.mouse.lLastX, raw_dy = raw.data.mouse.lLastY;
            else raw_dx += raw.data.mouse.lLastX, raw_dy += raw.data.mouse.lLastY;
        }
        if (f & 0x3ff)
        {
            raw_buttons++;
            out( "raw mouse buttons 0x%03x%s", f & 0x3ff, (f & RI_MOUSE_WHEEL) ? " (wheel)" : "" );
        }
        if (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) dirty = TRUE;
    }
    else if (raw.header.dwType == RIM_TYPEKEYBOARD)
    {
        raw_keys++;
        out( "raw key VK 0x%02x make 0x%02x %s", raw.data.keyboard.VKey, raw.data.keyboard.MakeCode,
             (raw.data.keyboard.Flags & RI_KEY_BREAK) ? "up" : "down" );
    }
}

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_CREATE:
    {
        RAWINPUTDEVICE rid[2] = { { 1, 2, 0, hwnd }, { 1, 6, 0, hwnd } };   /* generic desktop: mouse, keyboard */
        RECT rc;

        GetClientRect( hwnd, &rc );
        font_h = max( 14, rc.bottom / 34 );
        font = CreateFontA( font_h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                            FIXED_PITCH | FF_MODERN, "Consolas" );
        raw_ok = RegisterRawInputDevices( rid, 2, sizeof(rid[0]) );
        out( "window %ldx%ld, raw input %s", rc.right, rc.bottom, raw_ok ? "registered" : "not registered" );
        SetTimer( hwnd, 1, 33, NULL );
        return 0;
    }
    case WM_TIMER:
    {
        DWORD now = GetTickCount();
        if (now - last_report >= 1000)
        {
            if (moves_since || raw_since)
                out( "motion: %ld cursor moves, %ld raw moves in the last second; cursor %d, %d; raw total %ld, %ld",
                     moves_since, raw_since, mx, my, raw_dx, raw_dy );
            moves_since = raw_since = 0;
            last_report = now;
        }
        if (dirty) InvalidateRect( hwnd, NULL, FALSE );
        return 0;
    }
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        key_event( msg, wparam, lparam );
        if (msg == WM_KEYDOWN && wparam == VK_F2 && !(lparam & (1 << 30))) set_look( hwnd, !look );
        if (msg == WM_SYSKEYDOWN && wparam == VK_F4) break;   /* Alt+F4 closes */
        return 0;
    case WM_CHAR: case WM_SYSCHAR:
        if (wparam >= 32 && wparam < 127) out( "%s '%c'", msg == WM_CHAR ? "WM_CHAR" : "WM_SYSCHAR", (char)wparam );
        else out( "%s 0x%02x", msg == WM_CHAR ? "WM_CHAR" : "WM_SYSCHAR", (UINT)wparam );
        return 0;
    case WM_KILLFOCUS:
        memset( held, 0, sizeof(held) );
        if (look) set_look( hwnd, FALSE );
        out( "focus lost" );
        return 0;
    case WM_SETFOCUS:
        out( "focus gained" );
        return 0;
    case WM_MOUSEMOVE:
        mx = GET_X_LPARAM( lparam );
        my = GET_Y_LPARAM( lparam );
        moves_since++;
        if (look)
        {
            RECT rc;
            POINT c;
            GetClientRect( hwnd, &rc );
            c.x = rc.right / 2;
            c.y = rc.bottom / 2;
            if (mx != c.x || my != c.y)
            {
                look_x += mx - c.x;
                look_y += my - c.y;
                ClientToScreen( hwnd, &c );
                SetCursorPos( c.x, c.y );
                warps++;
            }
        }
        else
        {
            if (trail_len == TRAIL) { memmove( trail, trail + 1, sizeof(trail[0]) * (TRAIL - 1) ); trail_len--; }
            trail[trail_len].x = mx;
            trail[trail_len].y = my;
            trail_len++;
        }
        dirty = TRUE;
        return 0;
    case WM_LBUTTONDOWN:
        if (PtInRect( &quit_rect, (POINT){ GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) } )) { DestroyWindow( hwnd ); return 0; }
        if (!look && PtInRect( &look_rect, (POINT){ GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) } ))
        {
            button_event( hwnd, 0, TRUE, FALSE, lparam );
            set_look( hwnd, TRUE );
            return 0;
        }
        button_event( hwnd, 0, TRUE, FALSE, lparam ); return 0;
    case WM_LBUTTONUP: button_event( hwnd, 0, FALSE, FALSE, lparam ); return 0;
    case WM_LBUTTONDBLCLK: button_event( hwnd, 0, TRUE, TRUE, lparam ); return 0;
    case WM_RBUTTONDOWN: button_event( hwnd, 1, TRUE, FALSE, lparam ); return 0;
    case WM_RBUTTONUP: button_event( hwnd, 1, FALSE, FALSE, lparam ); return 0;
    case WM_RBUTTONDBLCLK: button_event( hwnd, 1, TRUE, TRUE, lparam ); return 0;
    case WM_MBUTTONDOWN: button_event( hwnd, 2, TRUE, FALSE, lparam ); return 0;
    case WM_MBUTTONUP: button_event( hwnd, 2, FALSE, FALSE, lparam ); return 0;
    case WM_MBUTTONDBLCLK: button_event( hwnd, 2, TRUE, TRUE, lparam ); return 0;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        button_event( hwnd, GET_XBUTTON_WPARAM( wparam ) == XBUTTON1 ? 3 : 4, msg != WM_XBUTTONUP,
                      msg == WM_XBUTTONDBLCLK, lparam );
        return TRUE;
    case WM_MOUSEWHEEL:
        wheel_v += GET_WHEEL_DELTA_WPARAM( wparam );
        out( "wheel vertical %+d (total %d)", GET_WHEEL_DELTA_WPARAM( wparam ), wheel_v );
        return 0;
    case WM_MOUSEHWHEEL:
        wheel_h += GET_WHEEL_DELTA_WPARAM( wparam );
        out( "wheel horizontal %+d (total %d)", GET_WHEEL_DELTA_WPARAM( wparam ), wheel_h );
        return 0;
    case WM_INPUT:
        raw_input( lparam );
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint( hwnd, &ps );
        paint( hwnd, dc );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        if (look) set_look( hwnd, FALSE );
        out( "quit" );
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcA( hwnd, msg, wparam, lparam );
}

int WINAPI WinMain( HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show )
{
    WNDCLASSA wc = { CS_DBLCLKS, wndproc, 0, 0, inst, NULL, LoadCursorA( NULL, (LPCSTR)IDC_ARROW ), NULL, NULL, "kbmtest" };
    BOOL (WINAPI *dpi_aware)(void) = (void *)GetProcAddress( GetModuleHandleA( "user32.dll" ), "SetProcessDPIAware" );
    HWND hwnd;
    MSG msg;

    if (dpi_aware) dpi_aware();
    RegisterClassA( &wc );
    hwnd = CreateWindowExA( 0, "kbmtest", "UTDeck keyboard and mouse test", WS_POPUP | WS_VISIBLE, 0, 0,
                            GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ), NULL, NULL, inst, NULL );
    if (!hwnd) return 1;
    while (GetMessageA( &msg, NULL, 0, 0 ) > 0)
    {
        TranslateMessage( &msg );
        DispatchMessageA( &msg );
    }
    return 0;
}
