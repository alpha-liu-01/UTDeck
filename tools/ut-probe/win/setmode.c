/* UTDeck: set the Windows desktop resolution of a Wine prefix, and keep it for the next starts.
 *
 *   setmode WIDTHxHEIGHT     e.g. setmode 1200x540
 *   setmode native           back to the monitor's own mode (the largest one)
 *   setmode                  print the current mode and the list of modes
 *
 * Many games have no real fullscreen: their "fullscreen" is a window the size of the desktop, their
 * "borderless" and "windowed" modes a window of the chosen size. Wine scales a picture to the screen
 * only under a display mode smaller than the monitor (patch 0006), so such games are shown in a
 * corner. With the desktop itself at a smaller mode (one of patch 0008's screen-shaped modes), the
 * game's desktop-sized window is that mode and Wine fills the panel with it.
 *
 * The mode is set with CDS_UPDATEREGISTRY, so it is stored in the prefix's registry: Wine's explorer
 * applies it each time the prefix starts (initialize_display_settings). Needs the same environment
 * as games (Wayland, UTDECK_WAYLAND_LANDSCAPE=1), or the mode list is the portrait one.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    DEVMODEW cur = {.dmSize = sizeof(cur)}, dm = {.dmSize = sizeof(dm)}, best = {.dmSize = sizeof(best)};
    DWORD i, w = 0, h = 0;
    LONG ret;
    BOOL found = FALSE;

    EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &cur);
    if (argc < 2)
    {
        printf("current: %lux%lu @%lu Hz\nmodes:", cur.dmPelsWidth, cur.dmPelsHeight, cur.dmDisplayFrequency);
        for (i = 0; EnumDisplaySettingsW(NULL, i, &dm); i++)
            if (dm.dmBitsPerPel == 32) printf(" %lux%lu", dm.dmPelsWidth, dm.dmPelsHeight);
        printf("\n");
        return 0;
    }
    if (strcmp(argv[1], "native") && sscanf(argv[1], "%lux%lu", &w, &h) != 2)
    {
        fprintf(stderr, "usage: setmode [WIDTHxHEIGHT|native]\n");
        return 2;
    }
    /* the mode from the list: the requested size (or the largest one), 32 bits, highest refresh.
     * As the first program of a new Wine session, the Wayland driver may not know the screen yet
     * (the list is empty or portrait for a moment): look again for up to 5 s. */
    for (int tries = 0; !found && tries < 50; tries++)
    {
        if (tries) Sleep(100);
        for (i = 0; EnumDisplaySettingsW(NULL, i, &dm); i++)
        {
            if (dm.dmBitsPerPel != 32) continue;
            if (w ? (dm.dmPelsWidth != w || dm.dmPelsHeight != h)
                  : (found && dm.dmPelsWidth * dm.dmPelsHeight < best.dmPelsWidth * best.dmPelsHeight)) continue;
            if (found && dm.dmPelsWidth == best.dmPelsWidth && dm.dmPelsHeight == best.dmPelsHeight &&
                dm.dmDisplayFrequency <= best.dmDisplayFrequency) continue;
            best = dm;
            found = TRUE;
        }
    }
    if (!found)
    {
        fprintf(stderr, "setmode: no %s mode (current %lux%lu); run without arguments for the list\n",
                argv[1], cur.dmPelsWidth, cur.dmPelsHeight);
        return 1;
    }
    best.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    ret = ChangeDisplaySettingsExW(NULL, &best, NULL, CDS_UPDATEREGISTRY, NULL);
    printf("setmode: %lux%lu @%lu Hz: %s (%ld)\n", best.dmPelsWidth, best.dmPelsHeight,
           best.dmDisplayFrequency, ret == DISP_CHANGE_SUCCESSFUL ? "set and stored" : "failed", ret);
    return ret == DISP_CHANGE_SUCCESSFUL ? 0 : 1;
}
