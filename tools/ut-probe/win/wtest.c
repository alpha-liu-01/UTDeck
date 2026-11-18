/*
 * UTDeck stage 2.1: a Windows console test program, built for every architecture Wine runs here
 * (ARM64, ARM64EC, x86-64, 32-bit x86) with llvm-mingw (build.sh).
 *
 *   wtest.exe            what it was built for, what Windows reports, then three small benchmarks
 *   wtest.exe --quick    the same with one round per benchmark
 *
 * The benchmarks are the same C code on every architecture, so their times compare native ARM64
 * code with x86 code emulated by FEX:
 *   int   an integer hash over 64 MiB of generated words (xorshift, multiply, rotate)
 *   float an n-body step over 2,048 bodies (single precision, sqrt and divide)
 *   copy  memcpy of a 64 MiB buffer, 8 times
 * It also reserves address space the way 64-bit games do (8 GiB reserved, 256 MiB touched) and
 * starts 8 threads.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#if defined(__arm64ec__)
#define BUILT "ARM64EC"
#elif defined(__aarch64__)
#define BUILT "ARM64"
#elif defined(__x86_64__)
#define BUILT "x86-64"
#elif defined(__i386__)
#define BUILT "x86 (32-bit)"
#else
#define BUILT "unknown"
#endif

static double now_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static const char *machine_name(USHORT m)
{
    switch (m) {
    case IMAGE_FILE_MACHINE_UNKNOWN: return "none";
    case IMAGE_FILE_MACHINE_I386: return "x86";
    case IMAGE_FILE_MACHINE_AMD64: return "x86-64";
    case IMAGE_FILE_MACHINE_ARM64: return "ARM64";
    case 0xa641: return "ARM64EC";
    default: return "other";
    }
}

static uint64_t bench_int(void)
{
    uint64_t x = 88172645463325252ULL, h = 0;
    for (uint32_t i = 0; i < 16u * 1024 * 1024; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        h = (h ^ x) * 0x9e3779b97f4a7c15ULL;
        h = (h << 29) | (h >> 35);
    }
    return h;
}

#define NB 2048
static float px[NB], py[NB], pz[NB], vx[NB], vy[NB], vz[NB];
static float bench_float(void)
{
    for (int i = 0; i < NB; i++) { px[i] = (float)(i % 37); py[i] = (float)(i % 53); pz[i] = (float)(i % 71); vx[i] = vy[i] = vz[i] = 0; }
    for (int i = 0; i < NB; i++) {
        float ax = 0, ay = 0, az = 0;
        for (int j = 0; j < NB; j++) {
            float dx = px[j] - px[i], dy = py[j] - py[i], dz = pz[j] - pz[i];
            float d2 = dx * dx + dy * dy + dz * dz + 0.01f;
            float inv = 1.0f / (d2 * sqrtf(d2));
            ax += dx * inv; ay += dy * inv; az += dz * inv;
        }
        vx[i] += ax * 0.001f; vy[i] += ay * 0.001f; vz[i] += az * 0.001f;
    }
    float s = 0;
    for (int i = 0; i < NB; i++) s += fabsf(vx[i]) + fabsf(vy[i]) + fabsf(vz[i]);
    return s;
}

static uint64_t bench_copy(unsigned char *a, unsigned char *b, size_t n)
{
    uint64_t s = 0;
    for (int r = 0; r < 8; r++) {
        unsigned char *dst = r & 1 ? a : b, *src = r & 1 ? b : a;
        memcpy(dst, src, n);
        dst[(r * 7919) % n] ^= (unsigned char)r; /* the next copy depends on this one */
        for (size_t o = r; o < n; o += 65536) s += dst[o];
    }
    return s;
}

static volatile LONG started;
static DWORD WINAPI thread_fn(void *p) { (void)p; InterlockedIncrement(&started); return 0; }

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* through Wine into a pipe, stdout is fully buffered */
    int rounds = (argc > 1 && !strcmp(argv[1], "--quick")) ? 1 : 3;
    printf("wtest: built for %s (pointer size %d bytes)\n", BUILT, (int)sizeof(void *));

    OSVERSIONINFOW v = { sizeof(v) };
    typedef LONG (WINAPI *rgv_t)(OSVERSIONINFOW *);
    rgv_t rgv = (rgv_t)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    if (rgv) rgv(&v);
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    printf("Windows %lu.%lu build %lu; %lu processors; page size %lu; user addresses %p - %p\n",
           v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, si.dwNumberOfProcessors, si.dwPageSize,
           si.lpMinimumApplicationAddress, si.lpMaximumApplicationAddress);
    typedef BOOL (WINAPI *iw2_t)(HANDLE, USHORT *, USHORT *);
    iw2_t iw2 = (iw2_t)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process2");
    USHORT pm = 0, nm = 0;
    if (iw2 && iw2(GetCurrentProcess(), &pm, &nm))
        printf("IsWow64Process2: process %s, native %s\n", machine_name(pm), machine_name(nm));
    char ident[256] = "";
    DWORD isz = sizeof(ident);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "ProcessorNameString", RRF_RT_REG_SZ, NULL, ident, &isz) == ERROR_SUCCESS)
        printf("CPU name: %s\n", ident);

#if (defined(__x86_64__) && !defined(__arm64ec__)) || defined(__i386__)
    /* What the x86 emulator offers (some newer games refuse to start without AVX or AVX2). */
    __builtin_cpu_init();
    printf("x86 features: sse4.2 %d, popcnt %d, avx %d, avx2 %d, fma %d, bmi2 %d, f16c %d, avx512f %d\n",
           __builtin_cpu_supports("sse4.2") != 0, __builtin_cpu_supports("popcnt") != 0,
           __builtin_cpu_supports("avx") != 0, __builtin_cpu_supports("avx2") != 0,
           __builtin_cpu_supports("fma") != 0, __builtin_cpu_supports("bmi2") != 0,
           __builtin_cpu_supports("f16c") != 0, __builtin_cpu_supports("avx512f") != 0);
#endif
    /* Address space, as 64-bit games use it: reserve 8 GiB (2 GiB on 32-bit), touch 256 MiB. */
    SIZE_T reserve = sizeof(void *) == 8 ? (SIZE_T)8 << 30 : (SIZE_T)1 << 30;
    char *r = VirtualAlloc(NULL, reserve, MEM_RESERVE, PAGE_NOACCESS);
    int touched = 0;
    if (r && VirtualAlloc(r, 256u << 20, MEM_COMMIT, PAGE_READWRITE)) {
        for (size_t o = 0; o < (256u << 20); o += 4096) r[o] = 1;
        touched = 1;
    }
    printf("reserve %lu MiB: %s at %p, 256 MiB committed and touched: %s\n", (unsigned long)(reserve >> 20),
           r ? "ok" : "FAILED", r, touched ? "ok" : "FAILED");
    if (r) VirtualFree(r, 0, MEM_RELEASE);

    HANDLE th[8];
    for (int i = 0; i < 8; i++) th[i] = CreateThread(NULL, 0, thread_fn, NULL, 0, NULL);
    WaitForMultipleObjects(8, th, TRUE, 5000);
    printf("threads: %ld of 8 ran\n", started);

    size_t n = 64u << 20;
    unsigned char *a = malloc(n), *b = malloc(n);
    uint32_t seed = GetTickCount() | 1;
    for (size_t o = 0; o < n; o++) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; a[o] = (unsigned char)seed; }
    memset(b, 0, n);
    for (int k = 0; k < rounds; k++) {
        double t0 = now_ms(); volatile uint64_t h = bench_int();
        double t1 = now_ms(); volatile float f = bench_float();
        double t2 = now_ms(); volatile uint64_t c = bench_copy(a, b, n);
        double t3 = now_ms();
        (void)h; (void)f; (void)c;
        printf("round %d: int %.1f ms, float %.1f ms, copy %.1f ms (%.2f GB/s copied)\n", k + 1, t1 - t0, t2 - t1, t3 - t2,
               8.0 * n / ((t3 - t2) / 1000.0) / 1e9);
    }
    printf("check: int %016llx float %.4f\n", (unsigned long long)bench_int(), bench_float());
    return 0;
}
