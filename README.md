C++ DLL (TarkovMVM_ReshadeHook.dll) that hooks IDXGISwapChain::Present via vtable patching, captures the final       
composited backbuffer (with ReShade applied) into shared memory, and signals a C# bridge via named Win32 events. The C# side 
(ReshadeCaptureBridge) feeds those raw frames into the existing FFmpeg stdin export pipeline
