/* UTDeck stage 3.3: what a wait through wineserver costs on this phone.
 *
 *   syncbench.exe [N]      (default 100000)
 *
 * 1. SetEvent + WaitForSingleObject(signaled, 0) N times: two server requests per loop, no thread
 *    has to wake up (the cost of a request itself).
 * 2. Ping-pong between two threads through two auto-reset events, N/5 times: each pass wakes the
 *    other thread (the cost of handing work to another thread, as games do between their threads).
 * 3. Sleep(0) N times: a system call that does not go to wineserver, for comparison.
 * Built for ARM64 (native) and x86-64 (through FEX), see build.sh. Under Wine without esync, fsync
 * or NTSYNC every event call is a round trip to wineserver; on Windows they are kernel calls.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HANDLE ping, pong;
static int passes;

static DWORD WINAPI partner(void *arg)
{
    for (int i = 0; i < passes; i++)
    {
        WaitForSingleObject(ping, INFINITE);
        SetEvent(pong);
    }
    return 0;
}

static double now_us(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 100000;
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL), th;
    double t;

    t = now_us();
    for (int i = 0; i < n; i++)
    {
        SetEvent(ev);
        WaitForSingleObject(ev, 0);
    }
    t = now_us() - t;
    printf("set + check an event: %.1f us per pair (%.1f us per request), %d pairs\n", t / n, t / n / 2, n);

    passes = n / 5;
    ping = CreateEventW(NULL, FALSE, FALSE, NULL);
    pong = CreateEventW(NULL, FALSE, FALSE, NULL);
    th = CreateThread(NULL, 0, partner, NULL, 0, NULL);
    t = now_us();
    for (int i = 0; i < passes; i++)
    {
        SetEvent(ping);
        WaitForSingleObject(pong, INFINITE);
    }
    t = now_us() - t;
    WaitForSingleObject(th, INFINITE);
    printf("ping-pong between two threads: %.1f us per round (two wake-ups), %d rounds\n", t / passes, passes);

    t = now_us();
    for (int i = 0; i < n; i++) Sleep(0);
    t = now_us() - t;
    printf("Sleep(0), no server: %.2f us per call\n", t / n);
    return 0;
}
