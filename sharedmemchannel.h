#pragma once
#include <Windows.h>
#include <cstdint>

namespace SharedChannel
{
    // memory header layout (16 bytes before pixel data):
    // [0..3]  uint32_t width
    // [4..7]  uint32_t height
    // [8]     uint8_t  pixelFormat  (0=BGRA, 1=RGBA -> C# swaps)
    // [9..15] reserved
    // [16..]  raw pixel data (width * height * 4 bytes)
    static const int HEADER_SIZE = 16;

    static const wchar_t* MAP_NAME     = L"TarkovMVM_FrameData";
    static const wchar_t* EVENT_REQ    = L"TarkovMVM_CaptureRequest";
    static const wchar_t* EVENT_DONE   = L"TarkovMVM_CaptureDone";

    bool    Initialize(int width, int height);
    void    Shutdown();

    uint8_t* PixelDataPtr();
    HANDLE   CaptureRequestEvent();
    HANDLE   CaptureDoneEvent();
    void     WriteHeader(uint32_t w, uint32_t h, uint8_t fmt);
}
