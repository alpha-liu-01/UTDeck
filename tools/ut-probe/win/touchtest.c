/* UTDeck stage 4.10 (for stage 5.5): touch test. Shows what a Windows game receives from a touch
 * screen, and writes every event to standard output (the run's log, wine/games/own-ID-<time>.log):
 *   - pointer messages (WM_POINTERDOWN/UPDATE/UP, Windows 8 and later): each contact with its id,
 *     its type (touch, pen, mouse) and its position; what most current games and engines read;
 *   - WM_TOUCH (Windows 7's touch messages, after RegisterTouchWindow; the Mode button switches):
 *     every contact of a frame at once;
 *   - WM_GESTURE (pan, zoom, rotate, two-finger tap, press and tap), sent when pointer messages
 *     are left to Windows;
 *   - mouse messages, marked "from touch" when Windows made them out of a touch (their extra info
 *     carries the 0xFF515700 signature), else as a real mouse;
 *   - taps and swipes (direction, length, time), for each contact and for the mouse;
 *   - five targets (the four corners and the middle) to check where a touch lands.
 * Contacts are drawn as numbered circles with their trails; the most contacts at once is kept.
 * A full-screen borderless window. Quit with its Quit button or Alt+F4.
 * Pointer, touch and gesture functions are looked up at run time: the program starts even where
 * they are missing, and says so. Built by build.sh (x86-64, as games are). */
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <windowsx.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LOG_LINES 16
#define MAX_CONTACTS 10
#define TRAIL 64

struct contact
{
    BOOL active;
    UINT id;             /* the pointer or touch id; mouse: ~0u */
    const char *source;  /* "pointer", "WM_TOUCH", "mouse" */
    POINT pos, start;
    DWORD start_time;
    POINT trail[TRAIL];
    int trail_len;
};

static char log_text[LOG_LINES][160];
static int log_next;
static struct contact contacts[MAX_CONTACTS + 1];   /* the last one is the mouse */
static int most_at_once, pointer_downs, touch_downs, mouse_downs, mouse_from_touch, gestures;
static BOOL touch_mode;   /* RegisterTouchWindow: WM_TOUCH instead of pointer messages */
static RECT quit_rect, mode_rect, targets[5];
static int target_hits[5];
static int font_h, tap_px;
static HFONT font;
static BOOL dirty;

static BOOL (WINAPI *pRegisterTouchWindow)( HWND, ULONG );
static BOOL (WINAPI *pUnregisterTouchWindow)( HWND );
static BOOL (WINAPI *pGetTouchInputInfo)( HTOUCHINPUT, UINT, TOUCHINPUT *, int );
static BOOL (WINAPI *pCloseTouchInputHandle)( HTOUCHINPUT );
static BOOL (WINAPI *pGetPointerType)( UINT32, POINTER_INPUT_TYPE * );
static BOOL (WINAPI *pGetGestureInfo)( HGESTUREINFO, GESTUREINFO * );
static BOOL (WINAPI *pCloseGestureInfoHandle)( HGESTUREINFO );

static const COLORREF colors[] = { RGB( 240, 90, 90 ), RGB( 90, 200, 90 ), RGB( 80, 150, 240 ), RGB( 240, 200, 60 ),
                                   RGB( 200, 100, 220 ), RGB( 60, 210, 210 ), RGB( 240, 140, 60 ), RGB( 180, 180, 180 ),
                                   RGB( 140, 220, 120 ), RGB( 240, 120, 180 ) };

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
    if (h && h != INVALID_HANDLE_VALUE) WriteFile( h, "touchtest: ", 11, &written, NULL ), WriteFile( h, buf, len, &written, NULL );
    dirty = TRUE;
}

static int active_count(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_CONTACTS; i++) n += contacts[i].active;
    return n;
}

static void check_targets( POINT pt, const char *source )
{
    int i;
    for (i = 0; i < 5; i++)
    {
        if (!PtInRect( &targets[i], pt )) continue;
        target_hits[i]++;
        out( "target %d hit by %s at %ld, %ld", i + 1, source, pt.x, pt.y );
    }
}

static struct contact *contact_down( UINT id, const char *source, POINT pt, BOOL mouse )
{
    struct contact *c = NULL;
    int i, n;

    if (mouse) c = &contacts[MAX_CONTACTS];
    else
    {
        for (i = 0; i < MAX_CONTACTS && !c; i++) if (contacts[i].active && contacts[i].id == id) c = &contacts[i];
        for (i = 0; i < MAX_CONTACTS && !c; i++) if (!contacts[i].active) c = &contacts[i];
        if (!c) { out( "%s down id %u: more than %d contacts", source, id, MAX_CONTACTS ); return NULL; }
    }
    c->active = TRUE;
    c->id = id;
    c->source = source;
    c->pos = c->start = pt;
    c->start_time = GetTickCount();
    c->trail_len = 0;
    if (!mouse && (n = active_count()) > most_at_once) most_at_once = n;
    check_targets( pt, source );
    return c;
}

static struct contact *find_contact( UINT id, BOOL mouse )
{
    int i;
    if (mouse) return contacts[MAX_CONTACTS].active ? &contacts[MAX_CONTACTS] : NULL;
    for (i = 0; i < MAX_CONTACTS; i++) if (contacts[i].active && contacts[i].id == id) return &contacts[i];
    return NULL;
}

static void contact_move( struct contact *c, POINT pt )
{
    if (!c) return;
    c->pos = pt;
    if (c->trail_len == TRAIL) { memmove( c->trail, c->trail + 1, sizeof(c->trail[0]) * (TRAIL - 1) ); c->trail_len--; }
    c->trail[c->trail_len++] = pt;
    dirty = TRUE;
}

static void contact_up( struct contact *c, POINT pt )
{
    int dx, dy;
    DWORD ms;
    const char *dir;

    if (!c) return;
    contact_move( c, pt );
    dx = pt.x - c->start.x;
    dy = pt.y - c->start.y;
    ms = GetTickCount() - c->start_time;
    if (abs( dx ) < tap_px && abs( dy ) < tap_px)
        out( "%s %s id %u: %s at %ld, %ld (%lu ms)", c->source, "up", c->id, ms < 500 ? "tap" : "long press",
             pt.x, pt.y, ms );
    else
    {
        if (abs( dx ) > abs( dy )) dir = dx > 0 ? "right" : "left";
        else dir = dy > 0 ? "down" : "up";
        out( "%s up id %u: swipe %s, %d, %d px in %lu ms", c->source, c->id, dir, dx, dy, ms );
    }
    c->active = FALSE;
}

static void set_touch_mode( HWND hwnd, BOOL on )
{
    if (on)
    {
        if (!pRegisterTouchWindow) { out( "RegisterTouchWindow is missing: WM_TOUCH not available" ); return; }
        if (!pRegisterTouchWindow( hwnd, 0 )) { out( "RegisterTouchWindow failed, error %lu", GetLastError() ); return; }
    }
    else if (pUnregisterTouchWindow) pUnregisterTouchWindow( hwnd );
    touch_mode = on;
    out( "mode: %s", on ? "WM_TOUCH (RegisterTouchWindow)" : "pointer messages and gestures" );
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

static void layout( int w, int h )
{
    int t = h / 7;

    mode_rect.left = w - font_h * 26; mode_rect.right = w - font_h * 9;
    mode_rect.top = font_h / 4; mode_rect.bottom = font_h * 2;
    quit_rect.left = w - font_h * 8; quit_rect.right = w - font_h;
    quit_rect.top = font_h / 4; quit_rect.bottom = font_h * 2;
    SetRect( &targets[0], 0, h - t, t, h );            /* bottom left (the top corners hold the buttons) */
    SetRect( &targets[1], w - t, h - t, w, h );        /* bottom right */
    SetRect( &targets[2], 0, font_h * 9, t, font_h * 9 + t );           /* left edge, upper */
    SetRect( &targets[3], w - t, font_h * 9, w, font_h * 9 + t );       /* right edge, upper */
    SetRect( &targets[4], w / 2 - t / 2, h / 2 - t / 2, w / 2 + t / 2, h / 2 + t / 2 );   /* middle */
}

static void paint( HWND hwnd, HDC target )
{
    const COLORREF fg = RGB( 230, 230, 230 ), dim = RGB( 140, 140, 140 ), accent = RGB( 255, 200, 60 );
    RECT rc;
    HDC dc;
    HBITMAP bmp;
    int w, h, x, y, i;

    GetClientRect( hwnd, &rc );
    w = rc.right;
    h = rc.bottom;
    layout( w, h );
    dc = CreateCompatibleDC( target );
    bmp = CreateCompatibleBitmap( target, w, h );
    SelectObject( dc, bmp );
    SelectObject( dc, font );
    SetBkMode( dc, TRANSPARENT );
    fill( dc, &rc, RGB( 20, 22, 28 ) );

    for (i = 0; i < 5; i++)
    {
        fill( dc, &targets[i], target_hits[i] ? RGB( 50, 90, 60 ) : RGB( 45, 45, 58 ) );
        draw_text( dc, targets[i].left + font_h / 2, targets[i].top + font_h / 2, fg, "%d: %d", i + 1, target_hits[i] );
    }

    draw_text( dc, font_h, font_h / 2, accent, "UTDeck touch test" );
    fill( dc, &mode_rect, touch_mode ? RGB( 60, 120, 60 ) : RGB( 60, 60, 70 ) );
    draw_text( dc, mode_rect.left + font_h / 2, mode_rect.top + font_h / 3, fg, "Mode: %s", touch_mode ? "WM_TOUCH" : "pointer" );
    fill( dc, &quit_rect, RGB( 120, 50, 50 ) );
    draw_text( dc, quit_rect.left + font_h * 2, quit_rect.top + font_h / 3, fg, "Quit" );

    x = font_h;
    y = font_h * 5 / 2;
    draw_text( dc, x, y, fg, "Contacts now %d, most at once %d.  Downs: pointer %d, WM_TOUCH %d, mouse %d (from touch %d).  Gestures %d",
               active_count(), most_at_once, pointer_downs, touch_downs, mouse_downs, mouse_from_touch, gestures );
    y += font_h * 3 / 2;
    draw_text( dc, x, y, dim, "API: pointer %s, WM_TOUCH %s, gestures %s",
               pGetPointerType ? "yes" : "missing", pRegisterTouchWindow && pGetTouchInputInfo ? "yes" : "missing",
               pGetGestureInfo ? "yes" : "missing" );
    y += font_h * 3 / 2;
    for (i = 0; i < LOG_LINES; i++)
    {
        const char *line = log_text[(log_next + i) % LOG_LINES];
        if (*line) draw_text( dc, x + h / 7, y, i == LOG_LINES - 1 ? accent : fg, "%s", line );
        y += font_h + font_h / 6;
    }

    for (i = 0; i <= MAX_CONTACTS; i++)
    {
        struct contact *c = &contacts[i];
        COLORREF color = i == MAX_CONTACTS ? RGB( 230, 230, 230 ) : colors[i % ARRAYSIZE(colors)];
        int r = i == MAX_CONTACTS ? font_h / 2 : font_h * 2;
        HPEN pen;
        HGDIOBJ old;

        if (!c->active && !c->trail_len) continue;
        pen = CreatePen( PS_SOLID, max( 2, font_h / 6 ), color );
        old = SelectObject( dc, pen );
        if (c->trail_len > 1) Polyline( dc, c->trail, c->trail_len );
        if (c->active)
        {
            SelectObject( dc, GetStockObject( NULL_BRUSH ) );
            Ellipse( dc, c->pos.x - r, c->pos.y - r, c->pos.x + r, c->pos.y + r );
            if (i < MAX_CONTACTS) draw_text( dc, c->pos.x + r, c->pos.y - r, color, "%u", c->id );
        }
        SelectObject( dc, old );
        DeleteObject( pen );
    }

    BitBlt( target, 0, 0, w, h, dc, 0, 0, SRCCOPY );
    DeleteDC( dc );
    DeleteObject( bmp );
    dirty = FALSE;
}

static const char *pointer_type( UINT id )
{
    POINTER_INPUT_TYPE type;
    if (!pGetPointerType || !pGetPointerType( id, &type )) return "?";
    switch (type)
    {
    case PT_TOUCH: return "touch";
    case PT_PEN: return "pen";
    case PT_MOUSE: return "mouse";
    case 5: return "touchpad";   /* PT_TOUCHPAD */
    default: return "other";
    }
}

static void pointer_event( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    UINT id = GET_POINTERID_WPARAM( wparam );
    POINT pt = { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };
    const char *type = pointer_type( id );

    ScreenToClient( hwnd, &pt );
    if (!strcmp( type, "mouse" )) return;   /* the mouse is shown by its own messages */
    switch (msg)
    {
    case WM_POINTERDOWN:
        pointer_downs++;
        out( "WM_POINTERDOWN id %u (%s) at %ld, %ld", id, type, pt.x, pt.y );
        contact_down( id, "pointer", pt, FALSE );
        break;
    case WM_POINTERUPDATE:
        contact_move( find_contact( id, FALSE ), pt );
        break;
    case WM_POINTERUP:
        contact_up( find_contact( id, FALSE ), pt );
        break;
    }
}

static void touch_event( HWND hwnd, WPARAM wparam, LPARAM lparam )
{
    TOUCHINPUT inputs[MAX_CONTACTS];
    UINT n = min( LOWORD( wparam ), MAX_CONTACTS ), i;

    if (!pGetTouchInputInfo || !pGetTouchInputInfo( (HTOUCHINPUT)lparam, n, inputs, sizeof(TOUCHINPUT) )) return;
    for (i = 0; i < n; i++)
    {
        POINT pt = { TOUCH_COORD_TO_PIXEL( inputs[i].x ), TOUCH_COORD_TO_PIXEL( inputs[i].y ) };
        ScreenToClient( hwnd, &pt );
        if (inputs[i].dwFlags & TOUCHEVENTF_DOWN)
        {
            touch_downs++;
            out( "WM_TOUCH down id %lu at %ld, %ld (%u in this frame)", inputs[i].dwID, pt.x, pt.y, n );
            contact_down( inputs[i].dwID, "WM_TOUCH", pt, FALSE );
        }
        else if (inputs[i].dwFlags & TOUCHEVENTF_UP) contact_up( find_contact( inputs[i].dwID, FALSE ), pt );
        else contact_move( find_contact( inputs[i].dwID, FALSE ), pt );
    }
    if (pCloseTouchInputHandle) pCloseTouchInputHandle( (HTOUCHINPUT)lparam );
}

static void gesture_event( LPARAM lparam )
{
    static const char *names[] = { "?", "begin", "end", "zoom", "pan", "rotate", "two-finger tap", "press and tap" };
    GESTUREINFO gi = { sizeof(gi) };

    if (!pGetGestureInfo || !pGetGestureInfo( (HGESTUREINFO)lparam, &gi )) return;
    if (gi.dwID != GID_BEGIN && gi.dwID != GID_END)
    {
        gestures++;
        if (gi.dwFlags & GF_BEGIN)
            out( "WM_GESTURE %s at %d, %d", gi.dwID < ARRAYSIZE(names) ? names[gi.dwID] : "?", gi.ptsLocation.x, gi.ptsLocation.y );
    }
    if (pCloseGestureInfoHandle) pCloseGestureInfoHandle( (HGESTUREINFO)lparam );
}

static void mouse_event_( HWND hwnd, UINT msg, LPARAM lparam )
{
    POINT pt = { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };
    BOOL from_touch = (GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700;
    const char *src = from_touch ? "mouse (from touch)" : "mouse";

    switch (msg)
    {
    case WM_LBUTTONDOWN:
        if (PtInRect( &quit_rect, pt )) { DestroyWindow( hwnd ); return; }
        if (PtInRect( &mode_rect, pt )) { set_touch_mode( hwnd, !touch_mode ); return; }
        mouse_downs++;
        if (from_touch) mouse_from_touch++;
        out( "%s left down at %ld, %ld", src, pt.x, pt.y );
        contact_down( ~0u, from_touch ? "mouse-from-touch" : "mouse", pt, TRUE );
        SetCapture( hwnd );
        break;
    case WM_MOUSEMOVE:
        contact_move( find_contact( ~0u, TRUE ), pt );
        break;
    case WM_LBUTTONUP:
        contact_up( find_contact( ~0u, TRUE ), pt );
        ReleaseCapture();
        break;
    case WM_RBUTTONDOWN:
        out( "%s right down at %ld, %ld", src, pt.x, pt.y );
        break;
    }
}

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HMODULE user32 = GetModuleHandleA( "user32.dll" );
        RECT rc;

        pRegisterTouchWindow = (void *)GetProcAddress( user32, "RegisterTouchWindow" );
        pUnregisterTouchWindow = (void *)GetProcAddress( user32, "UnregisterTouchWindow" );
        pGetTouchInputInfo = (void *)GetProcAddress( user32, "GetTouchInputInfo" );
        pCloseTouchInputHandle = (void *)GetProcAddress( user32, "CloseTouchInputHandle" );
        pGetPointerType = (void *)GetProcAddress( user32, "GetPointerType" );
        pGetGestureInfo = (void *)GetProcAddress( user32, "GetGestureInfo" );
        pCloseGestureInfoHandle = (void *)GetProcAddress( user32, "CloseGestureInfoHandle" );
        GetClientRect( hwnd, &rc );
        font_h = max( 14, rc.bottom / 34 );
        tap_px = max( 10, rc.bottom / 40 );
        font = CreateFontA( font_h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                            FIXED_PITCH | FF_MODERN, "Consolas" );
        out( "window %ldx%ld; touch digitizer: %s, %d contacts at most", rc.right, rc.bottom,
             (GetSystemMetrics( SM_DIGITIZER ) & NID_READY) ? "ready" : "not reported",
             GetSystemMetrics( SM_MAXIMUMTOUCHES ) );
        out( "API: pointer %s, WM_TOUCH %s, gestures %s", pGetPointerType ? "yes" : "missing",
             pRegisterTouchWindow && pGetTouchInputInfo ? "yes" : "missing", pGetGestureInfo ? "yes" : "missing" );
        SetTimer( hwnd, 1, 33, NULL );
        return 0;
    }
    case WM_TIMER:
        if (dirty) InvalidateRect( hwnd, NULL, FALSE );
        return 0;
    case WM_POINTERDOWN: case WM_POINTERUPDATE: case WM_POINTERUP:
        pointer_event( hwnd, msg, wparam, lparam );
        break;   /* left to Windows too: it makes the gestures and the mouse messages from them */
    case WM_TOUCH:
        touch_event( hwnd, wparam, lparam );
        return 0;
    case WM_GESTURE:
        gesture_event( lparam );
        break;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MOUSEMOVE: case WM_RBUTTONDOWN:
        mouse_event_( hwnd, msg, lparam );
        return 0;
    case WM_KEYDOWN:
        out( "key VK 0x%02x", (UINT)wparam );
        return 0;
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
        out( "quit: most contacts at once %d; downs: pointer %d, WM_TOUCH %d, mouse %d (from touch %d); gestures %d",
             most_at_once, pointer_downs, touch_downs, mouse_downs, mouse_from_touch, gestures );
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcA( hwnd, msg, wparam, lparam );
}

int WINAPI WinMain( HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show )
{
    WNDCLASSA wc = { 0, wndproc, 0, 0, inst, NULL, LoadCursorA( NULL, (LPCSTR)IDC_ARROW ), NULL, NULL, "touchtest" };
    BOOL (WINAPI *dpi_aware)(void) = (void *)GetProcAddress( GetModuleHandleA( "user32.dll" ), "SetProcessDPIAware" );
    HWND hwnd;
    MSG msg;

    if (dpi_aware) dpi_aware();
    RegisterClassA( &wc );
    hwnd = CreateWindowExA( 0, "touchtest", "UTDeck touch test", WS_POPUP | WS_VISIBLE, 0, 0,
                            GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ), NULL, NULL, inst, NULL );
    if (!hwnd) return 1;
    while (GetMessageA( &msg, NULL, 0, 0 ) > 0)
    {
        TranslateMessage( &msg );
        DispatchMessageA( &msg );
    }
    return 0;
}
