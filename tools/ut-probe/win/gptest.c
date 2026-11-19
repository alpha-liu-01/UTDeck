/*
 * UTDeck stage 2.4: a Windows gamepad test, as games read controllers: XInput (xinput1_4, what most
 * PC games use) and winmm's joystick API (older games). Built for x86-64 with llvm-mingw (build.sh).
 *
 *   gptest.exe [SECONDS]      (default 30)
 *
 * Lists the controllers each API sees, then prints every change of buttons, sticks and triggers
 * for SECONDS seconds. Through Wine the controllers come from winebus (SDL2, hidraw or evdev).
 */
#include <windows.h>
#include <xinput.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *xbuttons(WORD b, char *out)
{
    static const struct { WORD bit; const char *name; } names[] = {
        { XINPUT_GAMEPAD_A, "A" }, { XINPUT_GAMEPAD_B, "B" }, { XINPUT_GAMEPAD_X, "X" }, { XINPUT_GAMEPAD_Y, "Y" },
        { XINPUT_GAMEPAD_LEFT_SHOULDER, "LB" }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB" },
        { XINPUT_GAMEPAD_BACK, "Back" }, { XINPUT_GAMEPAD_START, "Start" },
        { XINPUT_GAMEPAD_LEFT_THUMB, "LS" }, { XINPUT_GAMEPAD_RIGHT_THUMB, "RS" },
        { XINPUT_GAMEPAD_DPAD_UP, "Up" }, { XINPUT_GAMEPAD_DPAD_DOWN, "Down" },
        { XINPUT_GAMEPAD_DPAD_LEFT, "Left" }, { XINPUT_GAMEPAD_DPAD_RIGHT, "Right" },
    };
    out[0] = 0;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (b & names[i].bit) { strcat(out, names[i].name); strcat(out, " "); }
    if (!out[0]) strcpy(out, "-");
    return out;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int seconds = argc > 1 ? atoi(argv[1]) : 30;
    XINPUT_STATE last[XUSER_MAX_COUNT];
    BOOL connected[XUSER_MAX_COUNT] = { 0 };
    memset(last, 0, sizeof(last));

    printf("XInput controllers:");
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
        XINPUT_CAPABILITIES caps;
        if (XInputGetCapabilities(i, 0, &caps) == ERROR_SUCCESS) {
            printf(" %lu (type %u, subtype %u)", i, caps.Type, caps.SubType);
            connected[i] = TRUE;
        }
    }
    printf("\n");
    UINT njoy = joyGetNumDevs();
    printf("winmm joysticks: %u slots;", njoy);
    for (UINT j = 0; j < njoy && j < 16; j++) {
        JOYCAPSW jc;
        if (joyGetDevCapsW(j, &jc, sizeof(jc)) == JOYERR_NOERROR && jc.wNumButtons)
            printf(" %u: \"%ls\", %u buttons, %u axes;", j, jc.szPname, jc.wNumButtons, jc.wNumAxes);
    }
    printf("\n");

    printf("reading for %d seconds: press buttons, move the sticks and triggers\n", seconds);
    DWORD end = GetTickCount() + seconds * 1000;
    int events = 0;
    while (GetTickCount() < end) {
        for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
            XINPUT_STATE st;
            DWORD r = XInputGetState(i, &st);
            if (r != ERROR_SUCCESS) {
                if (connected[i]) { printf("XInput %lu disconnected\n", i); connected[i] = FALSE; }
                continue;
            }
            if (!connected[i]) { printf("XInput %lu connected\n", i); connected[i] = TRUE; }
            if (st.dwPacketNumber == last[i].dwPacketNumber) continue;
            XINPUT_GAMEPAD *g = &st.Gamepad, *o = &last[i].Gamepad;
            /* report button changes always, axes only on large moves (sticks are noisy) */
            if (g->wButtons != o->wButtons || abs(g->sThumbLX - o->sThumbLX) > 8000 ||
                abs(g->sThumbLY - o->sThumbLY) > 8000 || abs(g->sThumbRX - o->sThumbRX) > 8000 ||
                abs(g->sThumbRY - o->sThumbRY) > 8000 || abs(g->bLeftTrigger - o->bLeftTrigger) > 40 ||
                abs(g->bRightTrigger - o->bRightTrigger) > 40) {
                char b[128];
                printf("XInput %lu: buttons %s| L %6d,%6d R %6d,%6d | LT %3u RT %3u\n", i, xbuttons(g->wButtons, b),
                       g->sThumbLX, g->sThumbLY, g->sThumbRX, g->sThumbRY, g->bLeftTrigger, g->bRightTrigger);
                last[i] = st;
                events++;
            }
        }
        Sleep(10);
    }
    printf("%d input events\n", events);
    /* a short rumble on controller 0, to check force feedback */
    XINPUT_VIBRATION v = { 30000, 30000 };
    if (XInputSetState(0, &v) == ERROR_SUCCESS) {
        printf("rumble on controller 0 for 1 s\n");
        Sleep(1000);
        v.wLeftMotorSpeed = v.wRightMotorSpeed = 0;
        XInputSetState(0, &v);
    }
    printf("done\n");
    return 0;
}
