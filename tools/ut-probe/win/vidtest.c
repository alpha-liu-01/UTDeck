/* UTDeck: play a video file through DirectShow, as older games do (Wine passes it to GStreamer).
 *
 *   vidtest FILE [SECONDS] [vmr9]   plays FILE in a window, up to SECONDS (default 30), then prints
 *                                   how it ended: "complete" (played to the end), "timeout" or an
 *                                   error. vmr9: shows the picture with the Video Mixing Renderer 9
 *                                   (Direct3D 9, as many games do) instead of the default renderer
 *                                   (DirectDraw)
 *
 * Checks that Wine's media path works in a game's kind of process (here 32-bit x86, like many
 * games): the file is read, a decoder is found for each stream, and the picture is shown.
 */
#define COBJMACROS
#include <windows.h>
#include <dshow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    IGraphBuilder *graph = NULL;
    IMediaControl *control = NULL;
    IMediaEvent *event = NULL;
    WCHAR path[MAX_PATH];
    LONG code = 0;
    DWORD start;
    HRESULT hr;
    int secs = argc > 2 ? atoi(argv[2]) : 30;

    if (argc < 2)
    {
        fprintf(stderr, "usage: vidtest FILE [SECONDS]\n");
        return 2;
    }
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);
    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (void **)&graph);
    printf("vidtest: filter graph: %#lx\n", hr);
    if (FAILED(hr)) return 1;
    if (argc > 3 && !strcmp(argv[3], "vmr9"))
    {
        IBaseFilter *vmr9;
        /* a renderer already in the graph is used by RenderFile before any other */
        hr = CoCreateInstance(&CLSID_VideoMixingRenderer9, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&vmr9);
        if (SUCCEEDED(hr)) hr = IGraphBuilder_AddFilter(graph, vmr9, L"VMR9");
        printf("vidtest: VMR-9: %#lx\n", hr);
        if (FAILED(hr)) return 1;
    }
    start = GetTickCount();
    hr = IGraphBuilder_RenderFile(graph, path, NULL);
    printf("vidtest: RenderFile: %#lx (%lu ms)\n", hr, GetTickCount() - start);
    fflush(stdout);
    if (FAILED(hr)) return 1;
    IGraphBuilder_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    IGraphBuilder_QueryInterface(graph, &IID_IMediaEvent, (void **)&event);
    start = GetTickCount();
    hr = IMediaControl_Run(control);
    printf("vidtest: Run: %#lx\n", hr);
    fflush(stdout);
    hr = IMediaEvent_WaitForCompletion(event, secs * 1000, &code);
    printf("vidtest: %s after %lu ms (hr %#lx, event %#lx)\n",
           hr == S_OK && code == EC_COMPLETE ? "complete" : hr == E_ABORT ? "timeout" : "error",
           GetTickCount() - start, hr, code);
    IMediaControl_Stop(control);
    IMediaEvent_Release(event);
    IMediaControl_Release(control);
    IGraphBuilder_Release(graph);
    CoUninitialize();
    return 0;
}
