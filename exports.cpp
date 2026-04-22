#include "Exports.h"
#include "PresentHook.h"
#include "SharedMemoryChannel.h"
#include <atomic>

static std::atomic<bool> g_initialized { false };

extern "C"
{
    __declspec(dllexport) bool RMV_Initialize(int width, int height)
    {
        if (g_initialized.load()) return true;
        if (!SharedChannel::Initialize(width, height)) return false;
        if (!PresentHook::InstallHook())
        {
            SharedChannel::Shutdown();
            return false;
        }
        g_initialized.store(true);
        return true;
    }

    __declspec(dllexport) void RMV_StartCapture()
    {
        PresentHook::SetCaptureActive(true);
    }

    __declspec(dllexport) void RMV_StopCapture()
    {
        PresentHook::SetCaptureActive(false);
    }

    __declspec(dllexport) void RMV_Shutdown()
    {
        if (!g_initialized.load()) return;
        PresentHook::SetCaptureActive(false);
        PresentHook::RemoveHook();
        SharedChannel::Shutdown();
        g_initialized.store(false);
    }
}
