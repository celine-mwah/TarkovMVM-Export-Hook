#include "SharedMemoryChannel.h"
#include <cstring>

namespace SharedChannel
{
    static HANDLE   g_hMapFile        = NULL;
    static void*    g_pMappedView     = NULL;
    static HANDLE   g_hCaptureRequest = NULL;
    static HANDLE   g_hCaptureDone    = NULL;
    static int      g_width           = 0;
    static int      g_height          = 0;

    bool Initialize(int width, int height)
    {
        if (g_pMappedView != NULL) return true;

        DWORD totalSize = static_cast<DWORD>(HEADER_SIZE + (LONGLONG)width * height * 4);

        g_hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, totalSize, MAP_NAME);
        if (!g_hMapFile) return false;

        g_pMappedView = MapViewOfFile(g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!g_pMappedView) { CloseHandle(g_hMapFile); g_hMapFile = NULL; return false; }

        // manual-reset, initially non-signaled — C# sets this to request a capture
        g_hCaptureRequest = CreateEventW(NULL, TRUE, FALSE, EVENT_REQ);
        if (!g_hCaptureRequest) goto fail;

        // auto-reset, initially non-signaled — C++ sets this after memcpy completes
        g_hCaptureDone = CreateEventW(NULL, FALSE, FALSE, EVENT_DONE);
        if (!g_hCaptureDone) goto fail;

        g_width  = width;
        g_height = height;

        // Zero the header region
        memset(g_pMappedView, 0, HEADER_SIZE);
        return true;

    fail:
        Shutdown();
        return false;
    }

    void Shutdown()
    {
        if (g_hCaptureDone)    { CloseHandle(g_hCaptureDone);    g_hCaptureDone    = NULL; }
        if (g_hCaptureRequest) { CloseHandle(g_hCaptureRequest); g_hCaptureRequest = NULL; }
        if (g_pMappedView)     { UnmapViewOfFile(g_pMappedView); g_pMappedView     = NULL; }
        if (g_hMapFile)        { CloseHandle(g_hMapFile);        g_hMapFile        = NULL; }
        g_width  = 0;
        g_height = 0;
    }

    uint8_t* PixelDataPtr()
    {
        if (!g_pMappedView) return nullptr;
        return static_cast<uint8_t*>(g_pMappedView) + HEADER_SIZE;
    }

    HANDLE CaptureRequestEvent() { return g_hCaptureRequest; }
    HANDLE CaptureDoneEvent()    { return g_hCaptureDone;    }

    void WriteHeader(uint32_t w, uint32_t h, uint8_t fmt)
    {
        if (!g_pMappedView) return;
        uint8_t* base = static_cast<uint8_t*>(g_pMappedView);
        memcpy(base + 0, &w,   4);
        memcpy(base + 4, &h,   4);
        base[8] = fmt;
    }
}
