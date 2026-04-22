#include "Exports.h"
#include <Windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH)
    {
        // Emergency cleanup if C# forgot to call RMV_Shutdown().
        // Suppress the return value — nothing we can do at this stage.
        RMV_Shutdown();
    }
    return TRUE;
}
