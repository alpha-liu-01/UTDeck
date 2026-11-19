/*
 * UTDeck stage 3.1: a Direct3D 12 smoke test, built for x86-64 with llvm-mingw (build.sh). Runs
 * through Wine with VKD3D-Proton (d3d12 and d3d12core overridden to native) on our Turnip.
 *
 *   d3d12test.exe [--frames N] [--fullscreen] [--size WxH]
 *
 * Creates a D3D12 device, prints what it offers (feature level, shader model, binding tiers), then
 * a DXGI flip-model swapchain with three buffers, a root signature with four root constants and one
 * pipeline (HLSL compiled at run time with D3DCompile, so VKD3D-Proton translates DXBC), and draws
 * one full-screen triangle per frame: a color ramp with a white bar that moves across. Waits on a
 * fence per frame and prints the distribution of the time between frames.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBUF 3
static int opt_frames = 600, opt_fullscreen, opt_w = 960, opt_h = 540;
static volatile int closed;

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
    case WM_KEYDOWN: if (wp == VK_ESCAPE) closed = 1; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static const char shader_src[] =
    "cbuffer C : register(b0) { float4 p; };\n"   /* p.x = time 0..1, p.y = bar x 0..1 */
    "struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) {\n"
    "  V o; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o; }\n"
    "float4 ps(V i) : SV_Target {\n"
    "  float3 c = float3(0.1 + 0.4 * p.x, 0.2 + 0.3 * i.uv.y, 0.5 - 0.4 * p.x);\n"
    "  if (abs(i.uv.x - p.y) < 0.02) c = float3(1, 1, 1);\n"
    "  return float4(c, 1); }\n";

static ID3DBlob *compile(const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = D3DCompile(shader_src, sizeof(shader_src) - 1, "d3d12test", NULL, NULL, entry, target, 0, 0, &code, &err);
    if (FAILED(hr)) { printf("shader %s: %s\n", entry, err ? (char *)ID3D10Blob_GetBufferPointer(err) : "?"); exit(3); }
    return code;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) opt_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) opt_fullscreen = 1;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &opt_w, &opt_h);
    }

    IDXGIFactory4 *factory;
    CHECK(CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&factory));
    IDXGIAdapter1 *adapter;
    CHECK(IDXGIFactory4_EnumAdapters1(factory, 0, &adapter));
    DXGI_ADAPTER_DESC1 ad;
    IDXGIAdapter1_GetDesc1(adapter, &ad);
    printf("adapter: %ls, vendor 0x%04x device 0x%04x, %lu MB video memory\n", ad.Description, ad.VendorId,
           ad.DeviceId, (unsigned long)(ad.DedicatedVideoMemory >> 20));

    ID3D12Device *dev;
    double t0 = now_ms();
    HRESULT hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
    if (FAILED(hr)) { printf("D3D12CreateDevice failed: 0x%08lx\n", (unsigned long)hr); return 2; }
    printf("device created in %.0f ms\n", now_ms() - t0);

    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
                                                D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2 };
    D3D12_FEATURE_DATA_FEATURE_LEVELS fl = { sizeof(levels) / sizeof(levels[0]), levels, 0 };
    D3D12_FEATURE_DATA_SHADER_MODEL sm = { D3D_SHADER_MODEL_6_7 };
    D3D12_FEATURE_DATA_D3D12_OPTIONS o = { 0 };
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl))))
        printf("max feature level: %d_%d\n", fl.MaxSupportedFeatureLevel >> 12, (fl.MaxSupportedFeatureLevel >> 8) & 0xf);
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))))
        printf("highest shader model: %d.%d\n", sm.HighestShaderModel >> 4, sm.HighestShaderModel & 0xf);
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o))))
        printf("resource binding tier %d, tiled resources tier %d, resource heap tier %d\n",
               o.ResourceBindingTier, o.TiledResourcesTier, o.ResourceHeapTier);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"d3d12test";
    RegisterClassW(&wc);
    int w = opt_w, h = opt_h;
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (opt_fullscreen) {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
        style = WS_POPUP;
    }
    HWND hwnd = CreateWindowExW(0, L"d3d12test", L"UTDeck D3D12 test", style | WS_VISIBLE, 0, 0, w, h, NULL, NULL, wc.hInstance, NULL);
    RECT cr;
    GetClientRect(hwnd, &cr);
    w = cr.right - cr.left;
    h = cr.bottom - cr.top;

    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
    ID3D12CommandQueue *queue;
    CHECK(ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&queue));

    DXGI_SWAP_CHAIN_DESC1 sd = { 0 };
    sd.Width = w;
    sd.Height = h;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = NBUF;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *sc1;
    IDXGISwapChain3 *sc;
    CHECK(IDXGIFactory4_CreateSwapChainForHwnd(factory, (IUnknown *)queue, hwnd, &sd, NULL, NULL, &sc1));
    CHECK(IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void **)&sc));
    printf("swapchain: %dx%d, %d buffers, flip discard\n", w, h, NBUF);

    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NBUF, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    ID3D12DescriptorHeap *rtv_heap;
    CHECK(ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap));
    UINT rtv_step = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv0;
    rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv0);
    ID3D12Resource *buf[NBUF];
    for (int i = 0; i < NBUF; i++) {
        CHECK(IDXGISwapChain3_GetBuffer(sc, i, &IID_ID3D12Resource, (void **)&buf[i]));
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = { rtv0.ptr + i * rtv_step };
        ID3D12Device_CreateRenderTargetView(dev, buf[i], NULL, hnd);
    }

    D3D12_ROOT_PARAMETER rp = { 0 };
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp.Constants.ShaderRegister = 0;
    rp.Constants.Num32BitValues = 4;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd = { 1, &rp, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    ID3DBlob *rs_blob, *rs_err = NULL;
    CHECK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err));
    ID3D12RootSignature *rs;
    CHECK(ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(rs_blob), ID3D10Blob_GetBufferSize(rs_blob),
                                           &IID_ID3D12RootSignature, (void **)&rs));

    ID3DBlob *vs = compile("vs", "vs_5_0"), *ps = compile("ps", "ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = { 0 };
    pd.pRootSignature = rs;
    pd.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs);
    pd.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    pd.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps);
    pd.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = 0xffffffff;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    ID3D12PipelineState *pso;
    t0 = now_ms();
    CHECK(ID3D12Device_CreateGraphicsPipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&pso));
    printf("pipeline created in %.0f ms\n", now_ms() - t0);

    ID3D12CommandAllocator *alloc[NBUF];
    for (int i = 0; i < NBUF; i++)
        CHECK(ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc[i]));
    ID3D12GraphicsCommandList *cl;
    CHECK(ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc[0], pso, &IID_ID3D12GraphicsCommandList, (void **)&cl));
    ID3D12GraphicsCommandList_Close(cl);
    ID3D12Fence *fence;
    CHECK(ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence));
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    UINT64 fence_val = 0, buf_fence[NBUF] = { 0 };

    double *dt = calloc(opt_frames, sizeof(double)), last = now_ms(), start = last;
    int n = 0;
    for (int f = 0; f < opt_frames && !closed; f++) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        UINT bi = IDXGISwapChain3_GetCurrentBackBufferIndex(sc);
        if (ID3D12Fence_GetCompletedValue(fence) < buf_fence[bi]) {
            ID3D12Fence_SetEventOnCompletion(fence, buf_fence[bi], ev);
            WaitForSingleObject(ev, INFINITE);
        }
        ID3D12CommandAllocator_Reset(alloc[bi]);
        ID3D12GraphicsCommandList_Reset(cl, alloc[bi], pso);
        D3D12_RESOURCE_BARRIER b = { 0 };
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = buf[bi];
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &b);
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = { rtv0.ptr + bi * rtv_step };
        static const float clear[4] = { 0, 0, 0, 1 };
        ID3D12GraphicsCommandList_ClearRenderTargetView(cl, hnd, clear, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(cl, 1, &hnd, FALSE, NULL);
        D3D12_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
        D3D12_RECT sr = { 0, 0, w, h };
        ID3D12GraphicsCommandList_RSSetViewports(cl, 1, &vp);
        ID3D12GraphicsCommandList_RSSetScissorRects(cl, 1, &sr);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(cl, rs);
        float p[4] = { (f % 240) / 240.0f, (f % 120) / 120.0f, 0, (float)w / h };
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(cl, 0, 4, p, 0);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(cl, 3, 1, 0, 0);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &b);
        CHECK(ID3D12GraphicsCommandList_Close(cl));
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList **)&cl);
        hr = IDXGISwapChain3_Present(sc, 0, 0);
        if (FAILED(hr)) { printf("Present failed: 0x%08lx at frame %d\n", (unsigned long)hr, f); break; }
        ID3D12CommandQueue_Signal(queue, fence, ++fence_val);
        buf_fence[bi] = fence_val;
        double t = now_ms();
        if (f > 0) dt[n++] = t - last;
        last = t;
    }
    ID3D12CommandQueue_Signal(queue, fence, ++fence_val);
    ID3D12Fence_SetEventOnCompletion(fence, fence_val, ev);
    WaitForSingleObject(ev, INFINITE);
    double total = now_ms() - start;
    if (n > 0) {
        qsort(dt, n, sizeof(double), cmp_double);
        printf("frames: %d in %.0f ms, %.1f fps; frame time median %.2f ms, p95 %.2f ms, max %.2f ms\n", n + 1, total,
               (n + 1) * 1000.0 / total, dt[n / 2], dt[n * 95 / 100], dt[n - 1]);
    }
    printf("done\n");
    return 0;
}
