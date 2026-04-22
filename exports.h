#pragma once
#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) bool  RMV_Initialize(int width, int height);
__declspec(dllexport) void  RMV_StartCapture();
__declspec(dllexport) void  RMV_StopCapture();
__declspec(dllexport) void  RMV_Shutdown();

#ifdef __cplusplus
}
#endif
