/*
 * UTDeck stage 2.4: a Direct3D 11 program as a game uses it (DXGI flip-model swapchain, HLSL shaders
 * compiled at run time, a draw per frame), built for x86-64 with llvm-mingw (build.sh). Runs through
 * Wine with DXVK (d3d11/dxgi overridden to native) or with Wine's own Direct3D (wined3d).
 *
 *   d3d11test.exe [--frames N] [--fullscreen] [--size WxH] [--load N] [--vsync] [--exclusive WxH]
 *
 * --exclusive WxH: exclusive fullscreen with a display mode change to WxH (the swapchain is created
 * non-windowed at that size), as 3DMark 11 and many older games do.
 * --mode WxH: change the display mode first (ChangeDisplaySettings), then a borderless fullscreen
 * window and swapchain of that size.
 *
 * Each frame draws one full-screen triangle; the pixel shader paints a color ramp and a white bar
 * that moves across, and loops --load times over some arithmetic per pixel (0 = almost no GPU work),
 * so GPU throughput can be compared between DXVK and wined3d. Prints the adapter, the swapchain and
 * the distribution of the time between frames.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int opt_frames = 600, opt_fullscreen, opt_w = 960, opt_h = 540, opt_load = 0, opt_vsync, opt_ex_w, opt_ex_h, opt_mode_w, opt_mode_h;
static volatile int closed, resized;

static double now_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("FAILED: %s = 0x%08lx (line %d)\n", #x, (unsigned long)hr_, __LINE__); exit(3); } } while (0)

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE: closed = 1; return 0;
    case WM_SIZE: resized = 1; return 0;
    case WM_KEYDOWN: if (wp == VK_ESCAPE) closed = 1; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static const char shader_src[] =
    "cbuffer C : register(b0) { float4 p; };\n"   /* p.x = time 0..1, p.y = bar x 0..1, p.z = load, p.w = aspect */
    "struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) {\n"
    "  V o; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o; }\n"
    "float4 ps(V i) : SV_Target {\n"
    "  float3 c = float3(0.1 + 0.4 * p.x, 0.2 + 0.3 * i.uv.y, 0.5 - 0.4 * p.x);\n"
    "  float acc = 0; for (int k = 0; k < (int)p.z; k++) acc += sin(i.uv.x * 13.0 + k) * cos(i.uv.y * 7.0 - k);\n"
    "  c += acc * 1e-6;\n"
    "  if (abs(i.uv.x - p.y) < 0.03) c = float3(1, 1, 1);\n"
    "  return float4(c, 1); }\n";

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) opt_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) opt_fullscreen = 1;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &opt_w, &opt_h);
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) opt_load = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--vsync")) opt_vsync = 1;
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) { sscanf(argv[++i], "%dx%d", &opt_mode_w, &opt_mode_h); opt_fullscreen = 1; }
        else if (!strcmp(argv[i], "--exclusive") && i + 1 < argc) { sscanf(argv[++i], "%dx%d", &opt_ex_w, &opt_ex_h); opt_fullscreen = 1; }
    }
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = { .lpfnWndProc = wndproc, .hInstance = hi, .lpszClassName = L"d3d11test",
                     .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW) };
    RegisterClassW(&wc);
    if (opt_mode_w) {
        DEVMODEW dm = { .dmSize = sizeof(dm), .dmPelsWidth = opt_mode_w, .dmPelsHeight = opt_mode_h,
                        .dmFields = DM_PELSWIDTH | DM_PELSHEIGHT };
        LONG r = ChangeDisplaySettingsExW(NULL, &dm, NULL, CDS_FULLSCREEN, NULL);
        printf("display mode %dx%d: ChangeDisplaySettingsEx = %ld, now %dx%d\n", opt_mode_w, opt_mode_h, r,
               GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    }
    HWND hwnd;
    if (opt_fullscreen) {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        hwnd = CreateWindowExW(0, L"d3d11test", L"UTDeck d3d11test", WS_POPUP | WS_VISIBLE, 0, 0, sw, sh, NULL, NULL, hi, NULL);
        printf("window: fullscreen (borderless) %dx%d\n", sw, sh);
    } else {
        RECT r = { 0, 0, opt_w, opt_h };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindowExW(0, L"d3d11test", L"UTDeck d3d11test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
                               r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
        printf("window: client area %dx%d\n", opt_w, opt_h);
    }
    RECT cr;
    GetClientRect(hwnd, &cr);

    DXGI_SWAP_CHAIN_DESC sd = { 0 };
    sd.BufferDesc.Width = opt_ex_w ? (UINT)opt_ex_w : (UINT)(cr.right - cr.left);
    sd.BufferDesc.Height = opt_ex_h ? (UINT)opt_ex_h : (UINT)(cr.bottom - cr.top);
    if (opt_ex_w) { sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1; }
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = hwnd;
    sd.Windowed = opt_ex_w ? FALSE : TRUE;
    sd.SwapEffect = opt_ex_w ? DXGI_SWAP_EFFECT_DISCARD : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (opt_ex_w) printf("exclusive fullscreen: asking for a %dx%d display mode\n", opt_ex_w, opt_ex_h);
    IDXGISwapChain *sc;
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    D3D_FEATURE_LEVEL fl;
    double t0 = now_ms();
    CHECK(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &sc,
                                        &dev, &fl, &ctx));
    printf("device: feature level %x.%x, created in %.0f ms\n", fl >> 12, (fl >> 8) & 0xf, now_ms() - t0);
    IDXGIDevice *dxdev;
    IDXGIAdapter *ad;
    DXGI_ADAPTER_DESC add;
    if (SUCCEEDED(ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice, (void **)&dxdev)) &&
        SUCCEEDED(IDXGIDevice_GetAdapter(dxdev, &ad)) && SUCCEEDED(IDXGIAdapter_GetDesc(ad, &add)))
        printf("adapter: %ls, vendor %04x device %04x, %u MB video memory\n", add.Description, add.VendorId,
               add.DeviceId, (unsigned)(add.DedicatedVideoMemory >> 20));
    printf("swapchain: %ux%u, flip discard, %s\n", sd.BufferDesc.Width, sd.BufferDesc.Height,
           opt_vsync ? "vsync (interval 1)" : "no vsync (interval 0)");

    ID3DBlob *vsb, *psb, *err = NULL;
    t0 = now_ms();
    if (FAILED(D3DCompile(shader_src, sizeof(shader_src) - 1, "t", NULL, NULL, "vs", "vs_5_0", 0, 0, &vsb, &err)) ||
        FAILED(D3DCompile(shader_src, sizeof(shader_src) - 1, "t", NULL, NULL, "ps", "ps_5_0", 0, 0, &psb, &err))) {
        printf("FAILED: shader compile: %s\n", err ? (char *)ID3D10Blob_GetBufferPointer(err) : "?");
        return 3;
    }
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    CHECK(ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), NULL, &vs));
    CHECK(ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &ps));
    printf("shaders: compiled and created in %.0f ms\n", now_ms() - t0);
    D3D11_BUFFER_DESC bd = { .ByteWidth = 16, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                             .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE };
    ID3D11Buffer *cb;
    CHECK(ID3D11Device_CreateBuffer(dev, &bd, NULL, &cb));

    ID3D11RenderTargetView *rtv = NULL;
    UINT bw = sd.BufferDesc.Width, bh = sd.BufferDesc.Height;
    int recreated = 0;
    double *ft = calloc(opt_frames + 1, sizeof(double));
    int presented = 0;
    for (int f = 0; f < opt_frames && !closed; f++) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (closed) break;
        if (resized || !rtv) {
            resized = 0;
            GetClientRect(hwnd, &cr);
            UINT w = cr.right - cr.left, h = cr.bottom - cr.top;
            if (rtv && w && h && (w != bw || h != bh)) {
                ID3D11DeviceContext_OMSetRenderTargets(ctx, 0, NULL, NULL);
                ID3D11RenderTargetView_Release(rtv);
                rtv = NULL;
                CHECK(IDXGISwapChain_ResizeBuffers(sc, 0, w, h, DXGI_FORMAT_UNKNOWN, 0));
                bw = w; bh = h;
                recreated++;
                printf("  swapchain resized to %ux%u\n", w, h);
            }
            if (!rtv) {
                ID3D11Texture2D *bb;
                CHECK(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb));
                CHECK(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)bb, NULL, &rtv));
                ID3D11Texture2D_Release(bb);
            }
        }
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            float *p = m.pData;
            p[0] = (f % 240) / 240.0f; p[1] = (f % 300) / 300.0f; p[2] = (float)opt_load; p[3] = (float)bw / bh;
            ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cb, 0);
        }
        D3D11_VIEWPORT vp = { 0, 0, (float)bw, (float)bh, 0, 1 };
        ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
        ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
        ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cb);
        ID3D11DeviceContext_Draw(ctx, 3, 0);
        HRESULT hr = IDXGISwapChain_Present(sc, opt_vsync ? 1 : 0, 0);
        if (FAILED(hr)) { printf("FAILED: Present = 0x%08lx at frame %d\n", (unsigned long)hr, f); break; }
        ft[presented++] = now_ms();
        if (presented == 1 || presented % 300 == 0) printf("  frame %d presented\n", presented);
    }

    int nf = 0, band[6] = { 0 };
    double sum = 0, mx = 0, *d = calloc(presented + 1, sizeof(double));
    for (int i = 31; i < presented; i++) {
        double x = ft[i] - ft[i - 1];
        d[nf++] = x; sum += x; if (x > mx) mx = x;
        band[x < 6.0 ? 0 : x < 10.5 ? 1 : x < 14.0 ? 2 : x < 20.0 ? 3 : x < 30.0 ? 4 : 5]++;
    }
    for (int i = 1; i < nf; i++) for (int j = i; j > 0 && d[j - 1] > d[j]; j--) { double x = d[j]; d[j] = d[j - 1]; d[j - 1] = x; }
    printf("%d frames presented, swapchain resized %d times%s\n", presented, recreated, closed ? ", closed by the user" : "");
    if (nf)
        printf("frame time over %d frames: mean %.2f ms (%.1f fps), median %.2f, p95 %.2f, max %.2f ms\n"
               "  bands: <6 ms %d | 6-10.5 (120 Hz) %d | 10.5-14 %d | 14-20 (60 Hz) %d | 20-30 %d | >30 ms %d\n",
               nf, sum / nf, 1000.0 * nf / sum, d[nf / 2], d[nf * 95 / 100], mx, band[0], band[1], band[2], band[3], band[4], band[5]);
    IDXGISwapChain_Release(sc);
    ID3D11DeviceContext_Release(ctx);
    ID3D11Device_Release(dev);
    DestroyWindow(hwnd);
    printf("done\n");
    return 0;
}
