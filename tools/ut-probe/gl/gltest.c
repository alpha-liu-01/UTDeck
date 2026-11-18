/*
 * UTDeck stage 1.3: OpenGL through EGL on Ubuntu Touch, as a game would use it.
 *
 *   gltest headless           surfaceless EGL: context, strings, a triangle in an FBO checked
 *                             pixel by pixel, a compute shader (GL 4.3+) checked
 *   gltest wayland [FRAMES]   a fullscreen xdg window on the compositor, FRAMES frames (default 600)
 *   gltest ... --gles         OpenGL ES 3 instead of desktop OpenGL
 *   gltest ... --interval N   swap interval for the window (default 1; 0 = do not wait for vsync)
 *   gltest ... --flush        glFlush before each swap (starts the GPU work before EGL waits)
 *   gltest ... --limit FPS    sleep to a fixed frame rate before each swap (a game's frame limiter)
 *   The window mode prints the distribution of the time between frames at the end.
 *
 * Uses the system's glvnd libEGL.so.1 / libOpenGL.so.0 / libGLESv2.so.2; which EGL vendor answers
 * is chosen by the environment (__EGL_VENDOR_LIBRARY_FILENAMES). Exit 0 = all checks passed.
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif

static int gles;
static int swap_interval = 1;
static int flush_first;     /* --flush: glFlush before eglSwapBuffers */
static double limit_fps;     /* --limit FPS: app-side frame limiter */

/* GL entry points are fetched through EGL, so the same code drives desktop GL and GLES. */
#define GLF(ret, name, args) static ret (*p_##name) args
GLF(const GLubyte *, glGetString, (GLenum));
GLF(void, glGetIntegerv, (GLenum, GLint *));
GLF(void, glFlush, (void));
GLF(GLenum, glGetError, (void));
GLF(void, glGenFramebuffers, (GLsizei, GLuint *));
GLF(void, glBindFramebuffer, (GLenum, GLuint));
GLF(void, glGenTextures, (GLsizei, GLuint *));
GLF(void, glBindTexture, (GLenum, GLuint));
GLF(void, glTexStorage2D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei));
GLF(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint));
GLF(GLenum, glCheckFramebufferStatus, (GLenum));
GLF(GLuint, glCreateShader, (GLenum));
GLF(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *));
GLF(void, glCompileShader, (GLuint));
GLF(void, glGetShaderiv, (GLuint, GLenum, GLint *));
GLF(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *));
GLF(GLuint, glCreateProgram, (void));
GLF(void, glAttachShader, (GLuint, GLuint));
GLF(void, glLinkProgram, (GLuint));
GLF(void, glGetProgramiv, (GLuint, GLenum, GLint *));
GLF(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *));
GLF(void, glUseProgram, (GLuint));
GLF(void, glGenVertexArrays, (GLsizei, GLuint *));
GLF(void, glBindVertexArray, (GLuint));
GLF(void, glViewport, (GLint, GLint, GLsizei, GLsizei));
GLF(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
GLF(void, glClear, (GLbitfield));
GLF(void, glDrawArrays, (GLenum, GLint, GLsizei));
GLF(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
GLF(void, glFinish, (void));
GLF(GLint, glGetUniformLocation, (GLuint, const GLchar *));
GLF(void, glUniform1f, (GLint, GLfloat));
GLF(void, glGenBuffers, (GLsizei, GLuint *));
GLF(void, glBindBuffer, (GLenum, GLuint));
GLF(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum));
GLF(void, glBindBufferBase, (GLenum, GLuint, GLuint));
GLF(void, glDispatchCompute, (GLuint, GLuint, GLuint));
GLF(void, glMemoryBarrier, (GLbitfield));
GLF(void *, glMapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield));
GLF(GLboolean, glUnmapBuffer, (GLenum));

static int load_gl(void)
{
    int ok = 1;
#define L(name) if (!(*(void **)&p_##name = (void *)eglGetProcAddress(#name))) { fprintf(stderr, "missing %s\n", #name); ok = 0; }
    L(glGetString) L(glGetIntegerv) L(glGetError) L(glGenFramebuffers) L(glBindFramebuffer) L(glGenTextures)
    L(glBindTexture) L(glTexStorage2D) L(glFramebufferTexture2D) L(glCheckFramebufferStatus) L(glCreateShader)
    L(glShaderSource) L(glCompileShader) L(glGetShaderiv) L(glGetShaderInfoLog) L(glCreateProgram) L(glAttachShader)
    L(glLinkProgram) L(glGetProgramiv) L(glGetProgramInfoLog) L(glUseProgram) L(glGenVertexArrays) L(glBindVertexArray)
    L(glViewport) L(glClearColor) L(glClear) L(glDrawArrays) L(glReadPixels) L(glFinish) L(glGetUniformLocation)
    L(glUniform1f) L(glGenBuffers) L(glBindBuffer) L(glBufferData) L(glBindBufferBase) L(glDispatchCompute)
    L(glMemoryBarrier) L(glMapBufferRange) L(glUnmapBuffer) L(glFlush)
#undef L
    return ok;
}

static GLuint shader(GLenum type, const char *body)
{
    char src[4096];
    snprintf(src, sizeof(src), "%s\n%s", gles ? "#version 310 es\nprecision highp float;" : "#version 430 core", body);
    const char *s = src;
    GLuint sh = p_glCreateShader(type);
    p_glShaderSource(sh, 1, &s, NULL);
    p_glCompileShader(sh);
    GLint ok;
    p_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; p_glGetShaderInfoLog(sh, sizeof(log), NULL, log); fprintf(stderr, "shader: %s\n", log); exit(3); }
    return sh;
}

static GLuint program(GLuint a, GLuint b)
{
    GLuint p = p_glCreateProgram();
    p_glAttachShader(p, a);
    if (b) p_glAttachShader(p, b);
    p_glLinkProgram(p);
    GLint ok;
    p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; p_glGetProgramInfoLog(p, sizeof(log), NULL, log); fprintf(stderr, "link: %s\n", log); exit(3); }
    return p;
}

/* A full-screen triangle, colored by position: red = x, green = y, blue = the uniform. */
static const char *VS =
    "out vec2 uv;\n"
    "void main() { vec2 p = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);\n"
    "  uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }";
static const char *FS =
    "in vec2 uv; uniform float t; out vec4 color;\n"
    "void main() { color = vec4(uv.x, uv.y, t, 1.0); }";
static const char *CS =
    "layout(local_size_x = 64) in;\n"
    "layout(std430, binding = 0) buffer B { uint v[]; };\n"
    "void main() { uint i = gl_GlobalInvocationID.x; v[i] = i * i + 7u; }";

static void print_strings(EGLDisplay dpy)
{
    printf("EGL vendor: %s\nEGL version: %s\nEGL client APIs: %s\n", eglQueryString(dpy, EGL_VENDOR),
           eglQueryString(dpy, EGL_VERSION), eglQueryString(dpy, EGL_CLIENT_APIS));
    printf("GL vendor: %s\nGL renderer: %s\nGL version: %s\nGLSL version: %s\n", p_glGetString(GL_VENDOR),
           p_glGetString(GL_RENDERER), p_glGetString(GL_VERSION), p_glGetString(GL_SHADING_LANGUAGE_VERSION));
}

static EGLContext make_context(EGLDisplay dpy, EGLConfig cfg)
{
    eglBindAPI(gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
    static const int tries[][2] = { { 4, 6 }, { 4, 5 }, { 4, 3 }, { 3, 3 } };
    for (unsigned i = 0; i < 4; i++) {
        EGLint attr[] = { EGL_CONTEXT_MAJOR_VERSION, gles ? 3 : tries[i][0],
                          EGL_CONTEXT_MINOR_VERSION, gles ? 2 : tries[i][1],
                          gles ? EGL_NONE : EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
        EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attr);
        if (ctx != EGL_NO_CONTEXT) {
            if (gles) printf("context: OpenGL ES 3.2 requested\n");
            else printf("context: OpenGL %d.%d core requested\n", tries[i][0], tries[i][1]);
            return ctx;
        }
        if (gles) break;
    }
    fprintf(stderr, "eglCreateContext failed: 0x%x\n", eglGetError());
    exit(2);
}

static int headless(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
    EGLint maj, min;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min)) { fprintf(stderr, "no surfaceless EGL display: 0x%x\n", eglGetError()); return 2; }
    EGLint cattr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, gles ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig cfg = NULL;
    EGLint n = 0;
    eglChooseConfig(dpy, cattr, &cfg, 1, &n);
    EGLContext ctx = make_context(dpy, n ? cfg : EGL_NO_CONFIG_KHR);
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { fprintf(stderr, "eglMakeCurrent failed: 0x%x\n", eglGetError()); return 2; }
    if (!load_gl()) return 2;
    print_strings(dpy);

    int fails = 0;
    /* Triangle into an FBO, read back. */
    const int W = 256, H = 128;
    GLuint tex, fbo, vao;
    p_glGenTextures(1, &tex); p_glBindTexture(GL_TEXTURE_2D, tex); p_glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    p_glGenFramebuffers(1, &fbo); p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "FBO incomplete\n"); return 3; }
    GLuint prog = program(shader(GL_VERTEX_SHADER, VS), shader(GL_FRAGMENT_SHADER, FS));
    p_glGenVertexArrays(1, &vao); p_glBindVertexArray(vao);
    p_glViewport(0, 0, W, H);
    p_glClearColor(0, 0, 0, 1); p_glClear(GL_COLOR_BUFFER_BIT);
    p_glUseProgram(prog); p_glUniform1f(p_glGetUniformLocation(prog, "t"), 0.5f);
    p_glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char *px = malloc(W * H * 4);
    p_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int r = (int)((x + 0.5f) / W * 255 + 0.5f), g = (int)((y + 0.5f) / H * 255 + 0.5f);
            unsigned char *p = px + (y * W + x) * 4;
            if (abs(p[0] - r) > 1 || abs(p[1] - g) > 1 || abs(p[2] - 128) > 1 || p[3] != 255) bad++;
        }
    printf("triangle in FBO %dx%d: %d pixels wrong: %s\n", W, H, bad, bad ? "FAIL" : "PASS");
    fails += bad != 0;

    /* Compute shader into an SSBO, read back. */
    const int N = 65536;
    GLuint buf;
    p_glGenBuffers(1, &buf); p_glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    p_glBufferData(GL_SHADER_STORAGE_BUFFER, N * 4, NULL, GL_DYNAMIC_READ);
    p_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buf);
    GLuint cprog = program(shader(GL_COMPUTE_SHADER, CS), 0);
    p_glUseProgram(cprog);
    p_glDispatchCompute(N / 64, 1, 1);
    p_glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    unsigned *v = p_glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, N * 4, GL_MAP_READ_BIT);
    bad = 0;
    for (int i = 0; v && i < N; i++) if (v[i] != (unsigned)i * (unsigned)i + 7u) bad++;
    if (v) p_glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    printf("compute shader, %d values: %d wrong: %s\n", N, v ? bad : N, (!v || bad) ? "FAIL" : "PASS");
    fails += !v || bad;
    printf("GL errors: 0x%x\n", p_glGetError());
    return fails ? 1 : 0;
}

/* ---- Wayland window ---- */
static struct wl_compositor *compositor;
static struct xdg_wm_base *wm;
static int cfg_w, cfg_h, configured;

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    if (!strcmp(iface, "wl_compositor")) compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "xdg_wm_base")) wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };
static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };
static void xs_configure(void *d, struct xdg_surface *s, uint32_t serial) { xdg_surface_ack_configure(s, serial); configured = 1; }
static const struct xdg_surface_listener xs_l = { xs_configure };
static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st) { if (w > 0 && h > 0) { cfg_w = w; cfg_h = h; } }
static void tl_close(void *d, struct xdg_toplevel *t) {}
static const struct xdg_toplevel_listener tl_l = { tl_configure, tl_close };

static int window(int frames)
{
    struct wl_display *wd = wl_display_connect(NULL);
    if (!wd) { fprintf(stderr, "no Wayland display\n"); return 2; }
    struct wl_registry *reg = wl_display_get_registry(wd);
    wl_registry_add_listener(reg, &reg_l, NULL);
    wl_display_roundtrip(wd);
    if (!compositor || !wm) { fprintf(stderr, "missing wl_compositor or xdg_wm_base\n"); return 2; }
    xdg_wm_base_add_listener(wm, &wm_l, NULL);
    struct wl_surface *surf = wl_compositor_create_surface(compositor);
    struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, surf);
    xdg_surface_add_listener(xs, &xs_l, NULL);
    struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_l, NULL);
    xdg_toplevel_set_title(tl, "UTDeck gltest");
    xdg_toplevel_set_fullscreen(tl, NULL);
    wl_surface_commit(surf);
    while (!configured && wl_display_dispatch(wd) >= 0) {}
    int w = cfg_w ? cfg_w : 1080, h = cfg_h ? cfg_h : 2400;
    struct wl_egl_window *ew = wl_egl_window_create(surf, w, h);

    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wd, NULL);
    EGLint maj, min;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min)) { fprintf(stderr, "no Wayland EGL display: 0x%x\n", eglGetError()); return 2; }
    EGLint cattr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, gles ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT,
                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || !n) { fprintf(stderr, "no window config: 0x%x\n", eglGetError()); return 2; }
    EGLContext ctx = make_context(dpy, cfg);
    EGLSurface es = eglCreatePlatformWindowSurface(dpy, cfg, ew, NULL);
    if (es == EGL_NO_SURFACE) { fprintf(stderr, "eglCreatePlatformWindowSurface failed: 0x%x\n", eglGetError()); return 2; }
    if (!eglMakeCurrent(dpy, es, es, ctx)) { fprintf(stderr, "eglMakeCurrent failed: 0x%x\n", eglGetError()); return 2; }
    if (!load_gl()) return 2;
    print_strings(dpy);
    printf("window: %dx%d\n", w, h);
    eglSwapInterval(dpy, swap_interval);
    printf("swap interval: %d%s", swap_interval, flush_first ? ", glFlush before each swap" : "");
    if (limit_fps > 0) printf(", limiter %.1f fps", limit_fps);
    printf("\n");
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    double *ft = calloc(frames + 1, sizeof(double));
    GLuint prog = program(shader(GL_VERTEX_SHADER, VS), shader(GL_FRAGMENT_SHADER, FS));
    GLuint vao;
    p_glGenVertexArrays(1, &vao); p_glBindVertexArray(vao);
    GLint tu = p_glGetUniformLocation(prog, "t");
    for (int f = 0; f < frames; f++) {
        wl_display_dispatch_pending(wd);
        if (cfg_w && (cfg_w != w || cfg_h != h)) { w = cfg_w; h = cfg_h; wl_egl_window_resize(ew, w, h, 0, 0); }
        p_glViewport(0, 0, w, h);
        p_glUseProgram(prog);
        p_glUniform1f(tu, (f % 120) / 119.0f);
        p_glDrawArrays(GL_TRIANGLES, 0, 3);
        if (flush_first) p_glFlush();
        if (limit_fps > 0) { /* sleep to an absolute deadline, like a game's frame limiter */
            long step = (long)(1e9 / limit_fps);
            next.tv_nsec += step;
            while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        }
        if (!eglSwapBuffers(dpy, es)) { fprintf(stderr, "eglSwapBuffers failed at frame %d: 0x%x\n", f, eglGetError()); return 1; }
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ft[f] = ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
    }
    /* Time between consecutive swaps, skipping the first 30 frames (start-up). */
    int nf = 0, band[6] = { 0 };
    double sum = 0, mx = 0, *d = calloc(frames, sizeof(double));
    for (int f = 31; f < frames; f++) {
        double x = ft[f] - ft[f - 1];
        d[nf++] = x; sum += x; if (x > mx) mx = x;
        band[x < 6.0 ? 0 : x < 10.5 ? 1 : x < 14.0 ? 2 : x < 20.0 ? 3 : x < 30.0 ? 4 : 5]++;
    }
    for (int i = 1; i < nf; i++) for (int j = i; j > 0 && d[j - 1] > d[j]; j--) { double t = d[j]; d[j] = d[j - 1]; d[j - 1] = t; }
    if (nf)
        printf("frame time over %d frames: mean %.2f ms (%.1f fps), median %.2f, p95 %.2f, max %.2f ms\n"
               "  bands: <6 ms %d | 6-10.5 (120 Hz) %d | 10.5-14 %d | 14-20 (60 Hz) %d | 20-30 %d | >30 ms %d\n",
               nf, sum / nf, 1000.0 * nf / sum, d[nf / 2], d[nf * 95 / 100], mx, band[0], band[1], band[2], band[3], band[4], band[5]);
    printf("%d frames presented, GL errors: 0x%x\n", frames, p_glGetError());
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, es);
    eglTerminate(dpy);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "headless";
    int frames = 600;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--gles")) gles = 1;
        else if (!strcmp(argv[i], "--interval") && i + 1 < argc) swap_interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--flush")) flush_first = 1;
        else if (!strcmp(argv[i], "--limit") && i + 1 < argc) limit_fps = atof(argv[++i]);
        else frames = atoi(argv[i]);
    }
    if (!strcmp(mode, "headless")) return headless();
    if (!strcmp(mode, "wayland")) return window(frames);
    fprintf(stderr, "usage: gltest headless|wayland [FRAMES] [--gles] [--interval N] [--flush] [--limit FPS]\n");
    return 2;
}
