/*
 * UTDeck stage 3.1: a Windows sound test, built for x86-64 with llvm-mingw (build.sh). Through Wine
 * the sound goes winmm -> mmdevapi -> winepulse.drv -> PulseAudio.
 *
 *   sndtest.exe [SECONDS]      (default 2)
 *
 * Lists the wave output devices, then plays a quiet 440 Hz tone for SECONDS seconds on the default
 * device. The tone is at 5 % of full scale (about -26 dBFS), far below normal media, and the
 * system volume is not touched. Prints how long the device took to play it, which shows whether
 * the audio clock runs at the right speed.
 */
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int seconds = argc > 1 ? atoi(argv[1]) : 2;
    if (seconds < 1 || seconds > 10) seconds = 2;

    UINT n = waveOutGetNumDevs();
    printf("wave output devices: %u\n", n);
    for (UINT i = 0; i < n; i++) {
        WAVEOUTCAPSW c;
        if (waveOutGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR)
            printf("  %u: \"%ls\", %u channels\n", i, c.szPname, c.wChannels);
    }

    WAVEFORMATEX f = { WAVE_FORMAT_PCM, 2, 48000, 48000 * 4, 4, 16, 0 };
    HWAVEOUT wo;
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    MMRESULT r = waveOutOpen(&wo, WAVE_MAPPER, &f, (DWORD_PTR)done, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) { printf("waveOutOpen failed: %u\n", r); return 2; }
    WaitForSingleObject(done, 1000);   /* the WOM_OPEN signal */

    DWORD frames = 48000 * seconds;
    short *pcm = malloc(frames * 4);
    for (DWORD i = 0; i < frames; i++) {
        double env = i < 2400 ? i / 2400.0 : (frames - i < 2400 ? (frames - i) / 2400.0 : 1.0);   /* 50 ms fades, no clicks */
        short s = (short)(0.05 * 32767 * env * sin(2 * M_PI * 440.0 * i / 48000.0));
        pcm[2 * i] = pcm[2 * i + 1] = s;
    }
    WAVEHDR h = { (LPSTR)pcm, frames * 4 };
    waveOutPrepareHeader(wo, &h, sizeof(h));
    LARGE_INTEGER fq, t0, t1;
    QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&t0);
    r = waveOutWrite(wo, &h, sizeof(h));
    if (r != MMSYSERR_NOERROR) { printf("waveOutWrite failed: %u\n", r); return 2; }
    printf("playing %d s of a quiet 440 Hz tone\n", seconds);
    while (!(h.dwFlags & WHDR_DONE)) WaitForSingleObject(done, 100);
    QueryPerformanceCounter(&t1);
    double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart;
    printf("played in %.0f ms (expected %d ms)\n", ms, seconds * 1000);
    waveOutUnprepareHeader(wo, &h, sizeof(h));
    waveOutClose(wo);
    printf("done\n");
    return 0;
}
