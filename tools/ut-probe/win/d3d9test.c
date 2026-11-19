/*
 * UTDeck stage 2.4: a Direct3D 9 program (most D3D9 games are 32-bit), built for x86 and x86-64 with
 * llvm-mingw (build.sh). Runs through Wine with DXVK's d3d9 (override d3d9=n) or wined3d (d3d9=b).
 *
 *   d3d9test.exe [--frames N] [--fullscreen] [--size WxH] [--rects N]
 *
 * Each frame clears the back buffer to a color ramp and clears --rects small rectangles (default 64)
 * plus a moving white bar with IDirect3DDevice9::Clear (no shaders), then presents. Prints the
 * adapter and the time between frames.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int opt_frames = 600, opt_fullscreen, opt_w = 960, opt_h = 540, opt_rects = 64;
static volatile int closed;

static double now_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLOSE || (msg == WM_KEYDOWN && wp == VK_ESCAPE)) { closed = 1; return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) opt_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) opt_fullscreen = 1;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &opt_w, &opt_h);
        else if (!strcmp(argv[i], "--rects") && i + 1 < argc) opt_rects = atoi(argv[++i]);
    }
    printf("d3d9test: %d-bit\n", (int)sizeof(void *) * 8);
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = { .lpfnWndProc = wndproc, .hInstance = hi, .lpszClassName = L"d3d9test",
                     .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW) };
    RegisterClassW(&wc);
    int w = opt_w, h = opt_h;
    HWND hwnd;
    if (opt_fullscreen) {
        w = GetSystemMetrics(SM_CXSCREEN); h = GetSystemMetrics(SM_CYSCREEN);
        hwnd = CreateWindowExW(0, L"d3d9test", L"UTDeck d3d9test", WS_POPUP | WS_VISIBLE, 0, 0, w, h, NULL, NULL, hi, NULL);
    } else {
        RECT r = { 0, 0, w, h };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindowExW(0, L"d3d9test", L"UTDeck d3d9test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
                               r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
    }
    printf("window: %s %dx%d\n", opt_fullscreen ? "fullscreen (borderless)" : "windowed", w, h);
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { printf("FAILED: Direct3DCreate9\n"); return 3; }
    D3DADAPTER_IDENTIFIER9 id;
    if (SUCCEEDED(IDirect3D9_GetAdapterIdentifier(d3d, 0, 0, &id)))
        printf("adapter: %s, vendor %04lx device %04lx\n", id.Description, (unsigned long)id.VendorId, (unsigned long)id.DeviceId);
    D3DPRESENT_PARAMETERS pp = { .BackBufferWidth = w, .BackBufferHeight = h, .BackBufferFormat = D3DFMT_X8R8G8B8,
        .BackBufferCount = 1, .SwapEffect = D3DSWAPEFFECT_DISCARD, .hDeviceWindow = hwnd, .Windowed = TRUE,
        .PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE };
    IDirect3DDevice9 *dev;
    double t0 = now_ms();
    HRESULT hr = IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) { printf("FAILED: CreateDevice 0x%08lx\n", (unsigned long)hr); return 3; }
    printf("device: created in %.0f ms, back buffer %dx%d, no vsync\n", now_ms() - t0, w, h);
    D3DRECT *rects = calloc(opt_rects + 1, sizeof(*rects));
    double *ft = calloc(opt_frames + 1, sizeof(double));
    int presented = 0;
    for (int f = 0; f < opt_frames && !closed; f++) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        int t = f % 240;
        IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(25 + t / 2, 50, 128 - t / 2), 1.0f, 0);
        for (int k = 0; k < opt_rects; k++) {
            int x = (k * 97 + f * 3) % (w - 32), y = (k * 151) % (h - 32);
            rects[k] = (D3DRECT){ x, y, x + 32, y + 32 };
        }
        IDirect3DDevice9_Clear(dev, opt_rects, rects, D3DCLEAR_TARGET, D3DCOLOR_XRGB(240, 200, 40), 1.0f, 0);
        int bx = (f * 8) % (w - w / 16);
        D3DRECT bar = { bx, 0, bx + w / 16, h };
        IDirect3DDevice9_Clear(dev, 1, &bar, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 0);
        hr = IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
        if (FAILED(hr)) { printf("FAILED: Present 0x%08lx at frame %d\n", (unsigned long)hr, f); break; }
        ft[presented++] = now_ms();
        if (presented == 1 || presented % 300 == 0) printf("  frame %d presented\n", presented);
    }
    int nf = 0;
    double sum = 0, mx = 0, *d = calloc(presented + 1, sizeof(double));
    for (int i = 31; i < presented; i++) { double x = ft[i] - ft[i - 1]; d[nf++] = x; sum += x; if (x > mx) mx = x; }
    for (int i = 1; i < nf; i++) for (int j = i; j > 0 && d[j - 1] > d[j]; j--) { double x = d[j]; d[j] = d[j - 1]; d[j - 1] = x; }
    printf("%d frames presented\n", presented);
    if (nf) printf("frame time over %d frames: mean %.2f ms (%.1f fps), median %.2f, p95 %.2f, max %.2f ms\n",
                   nf, sum / nf, 1000.0 * nf / sum, d[nf / 2], d[nf * 95 / 100], mx);
    IDirect3DDevice9_Release(dev);
    IDirect3D9_Release(d3d);
    DestroyWindow(hwnd);
    printf("done\n");
    return 0;
}
