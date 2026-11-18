/*
 * UTDeck stage 2.3: a Windows Vulkan program, as a game uses Vulkan (Win32 window, VK_KHR_win32_surface,
 * swapchain), built for x86-64 and ARM64 with llvm-mingw (build.sh). Runs through Wine's winevulkan
 * on the phone's Turnip.
 *
 *   vktest.exe [--frames N] [--fullscreen] [--size WxH] [--cycles K] [--mode fifo|mailbox|immediate]
 *              [--images N]
 *
 * Each frame clears the picture to a slowly changing color and draws a white vertical bar that moves
 * 8 pixels per frame (render-pass clears only, no shaders): a stale frame, tearing or a frozen picture is easy to
 * see. The swapchain is recreated when the window changes size (resize, rotation). --cycles opens
 * and closes the window K times (a stale subsurface after closing was a problem on Lomiri for the
 * Dolphin port). Prints the surface's capabilities, the swapchain chosen, and the distribution of
 * the time between frames.
 */
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ < 0) { printf("FAILED: %s = %d (line %d)\n", #x, r_, __LINE__); exit(3); } } while (0)

static PFN_vkGetInstanceProcAddr gipa;
#define VKF(name) static PFN_##name name
VKF(vkCreateInstance);
VKF(vkEnumeratePhysicalDevices); VKF(vkGetPhysicalDeviceProperties); VKF(vkGetPhysicalDeviceQueueFamilyProperties);
VKF(vkCreateDevice); VKF(vkGetDeviceProcAddr); VKF(vkDestroyInstance);
VKF(vkCreateWin32SurfaceKHR); VKF(vkDestroySurfaceKHR); VKF(vkGetPhysicalDeviceSurfaceSupportKHR);
VKF(vkGetPhysicalDeviceSurfaceCapabilitiesKHR); VKF(vkGetPhysicalDeviceSurfaceFormatsKHR);
VKF(vkGetPhysicalDeviceSurfacePresentModesKHR);
VKF(vkGetDeviceQueue); VKF(vkCreateSwapchainKHR); VKF(vkDestroySwapchainKHR); VKF(vkGetSwapchainImagesKHR);
VKF(vkAcquireNextImageKHR); VKF(vkQueuePresentKHR); VKF(vkQueueSubmit); VKF(vkCreateCommandPool);
VKF(vkAllocateCommandBuffers); VKF(vkBeginCommandBuffer); VKF(vkEndCommandBuffer);
VKF(vkCreateSemaphore); VKF(vkCreateFence); VKF(vkWaitForFences); VKF(vkResetFences);
VKF(vkResetCommandBuffer); VKF(vkDeviceWaitIdle); VKF(vkDestroySemaphore); VKF(vkDestroyFence);
VKF(vkDestroyCommandPool); VKF(vkDestroyDevice); VKF(vkCreateImageView); VKF(vkDestroyImageView);
VKF(vkCreateRenderPass); VKF(vkDestroyRenderPass); VKF(vkCreateFramebuffer); VKF(vkDestroyFramebuffer);
VKF(vkCmdBeginRenderPass); VKF(vkCmdEndRenderPass); VKF(vkCmdClearAttachments);

static VkInstance inst;
static VkPhysicalDevice pdev;
static VkDevice dev;
static VkQueue queue;
static uint32_t qfam;

static int opt_frames = 600, opt_fullscreen, opt_w = 960, opt_h = 540, opt_cycles = 1, opt_images = 0;
static VkPresentModeKHR opt_mode = VK_PRESENT_MODE_FIFO_KHR;
static volatile int window_closed, window_resized;

static double now_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static const char *mode_name(VkPresentModeKHR m)
{
    switch (m) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
    default: return "other";
    }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE: window_closed = 1; return 0;
    case WM_SIZE: window_resized = 1; return 0;
    case WM_KEYDOWN: if (wp == VK_ESCAPE) window_closed = 1; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void load_vulkan(void)
{
    HMODULE m = LoadLibraryA("vulkan-1.dll");
    if (!m) { printf("FAILED: vulkan-1.dll not found\n"); exit(3); }
    gipa = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(m, "vkGetInstanceProcAddr");
#define G(name) name = (PFN_##name)gipa(NULL, #name)
    G(vkCreateInstance);
#undef G
    const char *exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    VkApplicationInfo ai = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "UTDeck vktest",
                             .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ai,
                                 .enabledExtensionCount = 2, .ppEnabledExtensionNames = exts };
    CHECK(vkCreateInstance(&ici, NULL, &inst));
#define I(name) name = (PFN_##name)gipa(inst, #name)
    I(vkEnumeratePhysicalDevices); I(vkGetPhysicalDeviceProperties); I(vkGetPhysicalDeviceQueueFamilyProperties);
    I(vkCreateDevice); I(vkGetDeviceProcAddr); I(vkDestroyInstance); I(vkCreateWin32SurfaceKHR);
    I(vkDestroySurfaceKHR); I(vkGetPhysicalDeviceSurfaceSupportKHR); I(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    I(vkGetPhysicalDeviceSurfaceFormatsKHR); I(vkGetPhysicalDeviceSurfacePresentModesKHR);
#undef I
    uint32_t n = 1;
    VkResult r = vkEnumeratePhysicalDevices(inst, &n, &pdev);
    if (r < 0 || !n) { printf("FAILED: no Vulkan device\n"); exit(3); }
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(pdev, &p);
    printf("device: %s, Vulkan %u.%u.%u\n", p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion),
           VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion));
    VkQueueFamilyProperties qf[8];
    uint32_t nq = 8;
    vkGetPhysicalDeviceQueueFamilyProperties(pdev, &nq, qf);
    for (qfam = 0; qfam < nq && !(qf[qfam].queueFlags & VK_QUEUE_GRAPHICS_BIT); qfam++) ;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qfam,
                                    .queueCount = 1, .pQueuePriorities = &prio };
    const char *dexts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                               .pQueueCreateInfos = &qci, .enabledExtensionCount = 1, .ppEnabledExtensionNames = dexts };
    CHECK(vkCreateDevice(pdev, &dci, NULL, &dev));
#define D(name) name = (PFN_##name)vkGetDeviceProcAddr(dev, #name)
    D(vkGetDeviceQueue); D(vkCreateSwapchainKHR); D(vkDestroySwapchainKHR); D(vkGetSwapchainImagesKHR);
    D(vkAcquireNextImageKHR); D(vkQueuePresentKHR); D(vkQueueSubmit); D(vkCreateCommandPool);
    D(vkAllocateCommandBuffers); D(vkBeginCommandBuffer); D(vkEndCommandBuffer);
    D(vkCreateSemaphore); D(vkCreateFence); D(vkWaitForFences); D(vkResetFences);
    D(vkResetCommandBuffer); D(vkDeviceWaitIdle); D(vkDestroySemaphore); D(vkDestroyFence);
    D(vkDestroyCommandPool); D(vkDestroyDevice); D(vkCreateImageView); D(vkDestroyImageView);
    D(vkCreateRenderPass); D(vkDestroyRenderPass); D(vkCreateFramebuffer); D(vkDestroyFramebuffer);
    D(vkCmdBeginRenderPass); D(vkCmdEndRenderPass); D(vkCmdClearAttachments);
#undef D
    vkGetDeviceQueue(dev, qfam, 0, &queue);
}

#define MAXIMG 8
struct chain {
    VkSwapchainKHR sc;
    VkImage img[MAXIMG];
    VkImageView view[MAXIMG];
    VkFramebuffer fb[MAXIMG];
    VkRenderPass rp;
    VkFormat format;
    uint32_t n;
    VkExtent2D extent;
};

static void destroy_views(struct chain *c)
{
    for (uint32_t i = 0; i < c->n; i++) {
        if (c->fb[i]) vkDestroyFramebuffer(dev, c->fb[i], NULL);
        if (c->view[i]) vkDestroyImageView(dev, c->view[i], NULL);
        c->fb[i] = VK_NULL_HANDLE; c->view[i] = VK_NULL_HANDLE;
    }
    if (c->rp) vkDestroyRenderPass(dev, c->rp, NULL);
    c->rp = VK_NULL_HANDLE;
}

static void create_swapchain(VkSurfaceKHR surf, HWND hwnd, struct chain *c, int verbose)
{
    VkSurfaceCapabilitiesKHR caps;
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pdev, surf, &caps));
    RECT cr;
    GetClientRect(hwnd, &cr);
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFF) { ext.width = cr.right - cr.left; ext.height = cr.bottom - cr.top; }
    if (verbose) {
        VkPresentModeKHR modes[8];
        uint32_t nm = 8;
        vkGetPhysicalDeviceSurfacePresentModesKHR(pdev, surf, &nm, modes);
        printf("surface: currentExtent %ux%u (client area %ldx%ld), images %u-%u, present modes:",
               caps.currentExtent.width, caps.currentExtent.height, cr.right - cr.left, cr.bottom - cr.top,
               caps.minImageCount, caps.maxImageCount);
        for (uint32_t i = 0; i < nm; i++) printf(" %s", mode_name(modes[i]));
        printf("\n");
    }
    VkSurfaceFormatKHR fmts[16];
    uint32_t nf = 16;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surf, &nf, fmts);
    VkSurfaceFormatKHR fmt = fmts[0];
    for (uint32_t i = 0; i < nf; i++) if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) fmt = fmts[i];
    uint32_t want = opt_images ? (uint32_t)opt_images : caps.minImageCount + 1;
    if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;
    if (want < caps.minImageCount) want = caps.minImageCount;
    VkSwapchainKHR old = c->sc;
    VkSwapchainCreateInfoKHR sci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surf,
        .minImageCount = want, .imageFormat = fmt.format, .imageColorSpace = fmt.colorSpace, .imageExtent = ext,
        .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = opt_mode, .clipped = VK_TRUE,
        .oldSwapchain = old };
    CHECK(vkCreateSwapchainKHR(dev, &sci, NULL, &c->sc));
    if (old) vkDestroySwapchainKHR(dev, old, NULL);
    destroy_views(c);
    c->n = MAXIMG;
    CHECK(vkGetSwapchainImagesKHR(dev, c->sc, &c->n, c->img));
    c->extent = ext;
    c->format = fmt.format;
    VkAttachmentDescription ad = { .format = fmt.format, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
    VkAttachmentReference ar = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sd = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1,
                                .pColorAttachments = &ar };
    VkSubpassDependency dep = { .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
    VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1,
        .pAttachments = &ad, .subpassCount = 1, .pSubpasses = &sd, .dependencyCount = 1, .pDependencies = &dep };
    CHECK(vkCreateRenderPass(dev, &rpci, NULL, &c->rp));
    for (uint32_t i = 0; i < c->n; i++) {
        VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = c->img[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt.format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        CHECK(vkCreateImageView(dev, &vci, NULL, &c->view[i]));
        VkFramebufferCreateInfo fbci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = c->rp,
            .attachmentCount = 1, .pAttachments = &c->view[i], .width = ext.width, .height = ext.height, .layers = 1 };
        CHECK(vkCreateFramebuffer(dev, &fbci, NULL, &c->fb[i]));
    }
    printf("swapchain: %ux%u, %u images, format %d, %s\n", ext.width, ext.height, c->n, fmt.format, mode_name(opt_mode));
}

/* One window: open, present opt_frames frames, close. Returns 0 if every frame was presented. */
static int run_window(int cycle)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    static int registered;
    if (!registered) {
        WNDCLASSW wc = { .lpfnWndProc = wndproc, .hInstance = hi, .lpszClassName = L"vktest",
                         .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW) };
        RegisterClassW(&wc);
        registered = 1;
    }
    HWND hwnd;
    if (opt_fullscreen) {
        /* Borderless fullscreen, as most games do it: a popup covering the whole monitor. */
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        hwnd = CreateWindowExW(0, L"vktest", L"UTDeck vktest", WS_POPUP | WS_VISIBLE, 0, 0, sw, sh, NULL, NULL, hi, NULL);
        printf("window %d: fullscreen %dx%d\n", cycle, sw, sh);
    } else {
        RECT r = { 0, 0, opt_w, opt_h };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindowExW(0, L"vktest", L"UTDeck vktest", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
                               r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
        printf("window %d: windowed, client area %dx%d\n", cycle, opt_w, opt_h);
    }
    window_closed = window_resized = 0;

    VkWin32SurfaceCreateInfoKHR wci = { .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, .hinstance = hi, .hwnd = hwnd };
    VkSurfaceKHR surf;
    CHECK(vkCreateWin32SurfaceKHR(inst, &wci, NULL, &surf));
    VkBool32 ok = 0;
    vkGetPhysicalDeviceSurfaceSupportKHR(pdev, qfam, surf, &ok);
    if (!ok) { printf("FAILED: queue cannot present to this surface\n"); return 1; }

    struct chain c = { 0 };
    create_swapchain(surf, hwnd, &c, 1);

    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = qfam };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
    VkCommandBuffer cbs[2];
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 2 };
    CHECK(vkAllocateCommandBuffers(dev, &cai, cbs));
    VkSemaphore acq[2], done[MAXIMG];
    VkFence fences[2];
    VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    for (int i = 0; i < 2; i++) { CHECK(vkCreateSemaphore(dev, &sci, NULL, &acq[i])); CHECK(vkCreateFence(dev, &fci, NULL, &fences[i])); }
    for (int i = 0; i < MAXIMG; i++) CHECK(vkCreateSemaphore(dev, &sci, NULL, &done[i]));

    double *ft = calloc(opt_frames + 1, sizeof(double));
    int presented = 0, recreated = 0, fails = 0;
    for (int f = 0; f < opt_frames && !window_closed; f++) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (window_closed) break;
        if (window_resized) {
            window_resized = 0;
            vkDeviceWaitIdle(dev);
            create_swapchain(surf, hwnd, &c, 0);
            recreated++;
        }
        int k = f & 1;
        vkWaitForFences(dev, 1, &fences[k], VK_TRUE, UINT64_MAX);
        uint32_t idx;
        VkResult r = vkAcquireNextImageKHR(dev, c.sc, 2000000000ull, acq[k], VK_NULL_HANDLE, &idx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) { window_resized = 1; continue; }
        if (r < 0) { printf("FAILED: acquire %d at frame %d\n", r, f); fails++; break; }
        vkResetFences(dev, 1, &fences[k]);
        VkCommandBuffer cb = cbs[k];
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkBeginCommandBuffer(cb, &bi);
        float t = (f % 240) / 240.0f;
        VkClearValue bg = { .color = { .float32 = { 0.1f + 0.4f * t, 0.2f, 0.5f - 0.4f * t, 1.0f } } };
        VkRenderPassBeginInfo rbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = c.rp,
            .framebuffer = c.fb[idx], .renderArea = { { 0, 0 }, c.extent }, .clearValueCount = 1, .pClearValues = &bg };
        vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        uint32_t bw = c.extent.width / 16 ? c.extent.width / 16 : 1;
        VkClearAttachment ca = { VK_IMAGE_ASPECT_COLOR_BIT, 0, { .color = { .float32 = { 1, 1, 1, 1 } } } };
        VkClearRect bar = { { { (int32_t)((f * 8u) % (c.extent.width - bw + 1)), 0 }, { bw, c.extent.height } }, 0, 1 };
        vkCmdClearAttachments(cb, 1, &ca, 1, &bar);
        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
        VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &acq[k],
                            .pWaitDstStageMask = &ws, .commandBufferCount = 1, .pCommandBuffers = &cb,
                            .signalSemaphoreCount = 1, .pSignalSemaphores = &done[idx] };
        CHECK(vkQueueSubmit(queue, 1, &si, fences[k]));
        VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &done[idx], .swapchainCount = 1, .pSwapchains = &c.sc, .pImageIndices = &idx };
        r = vkQueuePresentKHR(queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) window_resized = 1;
        else if (r < 0) { printf("FAILED: present %d at frame %d\n", r, f); fails++; break; }
        ft[presented++] = now_ms();
        if (presented == 1 || presented % 120 == 0) printf("  frame %d presented\n", presented);
    }
    vkDeviceWaitIdle(dev);

    int nf = 0, band[6] = { 0 };
    double sum = 0, mx = 0, *d = calloc(presented + 1, sizeof(double));
    for (int i = 31; i < presented; i++) {
        double x = ft[i] - ft[i - 1];
        d[nf++] = x; sum += x; if (x > mx) mx = x;
        band[x < 6.0 ? 0 : x < 10.5 ? 1 : x < 14.0 ? 2 : x < 20.0 ? 3 : x < 30.0 ? 4 : 5]++;
    }
    for (int i = 1; i < nf; i++) for (int j = i; j > 0 && d[j - 1] > d[j]; j--) { double x = d[j]; d[j] = d[j - 1]; d[j - 1] = x; }
    printf("window %d: %d frames presented, swapchain recreated %d times%s\n", cycle, presented, recreated,
           window_closed ? ", closed by the user" : "");
    if (nf)
        printf("frame time over %d frames: mean %.2f ms (%.1f fps), median %.2f, p95 %.2f, max %.2f ms\n"
               "  bands: <6 ms %d | 6-10.5 (120 Hz) %d | 10.5-14 %d | 14-20 (60 Hz) %d | 20-30 %d | >30 ms %d\n",
               nf, sum / nf, 1000.0 * nf / sum, d[nf / 2], d[nf * 95 / 100], mx, band[0], band[1], band[2], band[3], band[4], band[5]);
    free(ft); free(d);

    for (int i = 0; i < 2; i++) { vkDestroySemaphore(dev, acq[i], NULL); vkDestroyFence(dev, fences[i], NULL); }
    for (int i = 0; i < MAXIMG; i++) vkDestroySemaphore(dev, done[i], NULL);
    vkDestroyCommandPool(dev, pool, NULL);
    destroy_views(&c);
    vkDestroySwapchainKHR(dev, c.sc, NULL);
    vkDestroySurfaceKHR(inst, surf, NULL);
    DestroyWindow(hwnd);
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    return fails;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* through Wine into a pipe, stdout is fully buffered */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) opt_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) opt_fullscreen = 1;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &opt_w, &opt_h);
        else if (!strcmp(argv[i], "--cycles") && i + 1 < argc) opt_cycles = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--images") && i + 1 < argc) opt_images = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            opt_mode = !strcmp(m, "mailbox") ? VK_PRESENT_MODE_MAILBOX_KHR :
                       !strcmp(m, "immediate") ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
        }
    }
#if defined(__x86_64__) && !defined(__arm64ec__)
    printf("vktest: x86-64\n");
#elif defined(__aarch64__) || defined(__arm64ec__)
    printf("vktest: ARM64\n");
#endif
    load_vulkan();
    int fails = 0;
    for (int c = 0; c < opt_cycles; c++) fails += run_window(c + 1);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    printf("%s\n", fails ? "FAILED" : "done");
    return fails ? 3 : 0;
}
