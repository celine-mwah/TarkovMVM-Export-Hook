#include "PresentHook.h"
#include "SharedMemoryChannel.h"
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace PresentHook
{
    typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain*, UINT, UINT);

    static PFN_Present       g_OriginalPresent  = nullptr;
    static ID3D11Texture2D*  g_pStagingTexture  = nullptr;
    static D3D11_TEXTURE2D_DESC g_stagingDesc   = {};
    static std::atomic<bool> g_captureActive    { false };
    static std::atomic<bool> g_hookInstalled    { false };

    static void ReleaseStagingTexture()
    {
        if (g_pStagingTexture) { g_pStagingTexture->Release(); g_pStagingTexture = nullptr; }
        memset(&g_stagingDesc, 0, sizeof(g_stagingDesc));
    }

    static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
    {
        // call original first — reshade has already composited rn
        HRESULT hr = g_OriginalPresent(pSwapChain, SyncInterval, Flags);

        if (!g_captureActive.load(std::memory_order_relaxed))
            return hr;

        HANDLE hRequest = SharedChannel::CaptureRequestEvent();
        HANDLE hDone    = SharedChannel::CaptureDoneEvent();
        if (!hRequest || !hDone)
            return hr;

        // is a capture pending?
        if (WaitForSingleObject(hRequest, 0) != WAIT_OBJECT_0)
            return hr;

        // prevent double-capture if present is called again before C# reads.
        ResetEvent(hRequest);

        ID3D11Device*        pDevice  = nullptr;
        ID3D11DeviceContext* pContext = nullptr;
        ID3D11Texture2D*     pBB     = nullptr;
        ID3D11Texture2D*     pResolved = nullptr;

        if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBB))))
            goto signal_done;

        pBB->GetDevice(&pDevice);
        if (!pDevice) goto cleanup;
        pDevice->GetImmediateContext(&pContext);
        if (!pContext) goto cleanup;

        {
            D3D11_TEXTURE2D_DESC bbDesc;
            pBB->GetDesc(&bbDesc);

            ID3D11Texture2D* pSource = pBB;

            if (bbDesc.SampleDesc.Count > 1)
            {
                D3D11_TEXTURE2D_DESC resolveDesc = bbDesc;
                resolveDesc.SampleDesc.Count   = 1;
                resolveDesc.SampleDesc.Quality = 0;
                resolveDesc.BindFlags          = 0;
                resolveDesc.CPUAccessFlags     = 0;
                resolveDesc.Usage              = D3D11_USAGE_DEFAULT;
                resolveDesc.MipLevels          = 1;
                if (SUCCEEDED(pDevice->CreateTexture2D(&resolveDesc, nullptr, &pResolved)))
                {
                    pContext->ResolveSubresource(pResolved, 0, pBB, 0, bbDesc.Format);
                    pSource = pResolved;
                }
                bbDesc.SampleDesc.Count   = 1;
                bbDesc.SampleDesc.Quality = 0;
            }

            bool needsRecreate = (g_pStagingTexture == nullptr ||
                                  g_stagingDesc.Width  != bbDesc.Width  ||
                                  g_stagingDesc.Height != bbDesc.Height ||
                                  g_stagingDesc.Format != bbDesc.Format);
            if (needsRecreate)
            {
                ReleaseStagingTexture();
                D3D11_TEXTURE2D_DESC stDesc = bbDesc;
                stDesc.BindFlags      = 0;
                stDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                stDesc.Usage          = D3D11_USAGE_STAGING;
                stDesc.MiscFlags      = 0;
                stDesc.MipLevels      = 1;
                stDesc.ArraySize      = 1;
                if (FAILED(pDevice->CreateTexture2D(&stDesc, nullptr, &g_pStagingTexture)))
                    goto cleanup;
                g_stagingDesc = stDesc;
            }

            pContext->CopyResource(g_pStagingTexture, pSource);

            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(pContext->Map(g_pStagingTexture, 0, D3D11_MAP_READ, 0, &mapped)))
            {
                uint8_t* dst = SharedChannel::PixelDataPtr();
                const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
                uint32_t w = bbDesc.Width;
                uint32_t h = bbDesc.Height;

                // detect native pixel format: BGRA=0, RGBA=1 (C# must swap for ffmpeg)
                uint8_t fmt = (bbDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                               bbDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ? 0 : 1;

                SharedChannel::WriteHeader(w, h, fmt);

                size_t rowBytes = static_cast<size_t>(w) * 4;
                for (uint32_t row = 0; row < h; ++row)
                    memcpy(dst + row * rowBytes, src + row * mapped.RowPitch, rowBytes);

                pContext->Unmap(g_pStagingTexture, 0);
            }
        }

    cleanup:
        if (pResolved) pResolved->Release();
        if (pBB)       pBB->Release();
        if (pContext)  pContext->Release();
        if (pDevice)   pDevice->Release();

    signal_done:
        // signal done so C# doesn't time out waiting.
        SetEvent(hDone);
        return hr;
    }

    bool InstallHook()
    {
        if (g_hookInstalled.load()) return true;

        // create a temporary window + D3D11 device + swapchain to read the vtable.
        HWND hWnd = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 8, 8,
                                    HWND_MESSAGE, NULL, NULL, NULL);
        if (!hWnd) return false;

        DXGI_SWAP_CHAIN_DESC scd = {};
        scd.BufferCount       = 1;
        scd.BufferDesc.Width  = 8;
        scd.BufferDesc.Height = 8;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow      = hWnd;
        scd.SampleDesc.Count  = 1;
        scd.Windowed          = TRUE;
        scd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device*    pDevice    = nullptr;
        IDXGISwapChain*  pSwapChain = nullptr;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &scd, &pSwapChain, &pDevice, nullptr, nullptr
        );

        DestroyWindow(hWnd);

        if (FAILED(hr) || !pSwapChain) { if (pDevice) pDevice->Release(); return false; }

        void** vtable = *reinterpret_cast<void***>(pSwapChain);
        g_OriginalPresent = reinterpret_cast<PFN_Present>(vtable[8]);

        DWORD oldProtect;
        if (!VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            pSwapChain->Release(); pDevice->Release();
            return false;
        }
        vtable[8] = reinterpret_cast<void*>(&HookedPresent);
        VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &oldProtect);

        pSwapChain->Release();
        pDevice->Release();

        g_hookInstalled.store(true);
        return true;
    }

    void SetCaptureActive(bool active)
    {
        g_captureActive.store(active, std::memory_order_relaxed);
    }

    void RemoveHook()
    {
        if (!g_hookInstalled.load()) return;

        // create another dummy swapchain.
        HWND hWnd = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 8, 8,
                                    HWND_MESSAGE, NULL, NULL, NULL);
        if (hWnd)
        {
            DXGI_SWAP_CHAIN_DESC scd = {};
            scd.BufferCount       = 1;
            scd.BufferDesc.Width  = 8;
            scd.BufferDesc.Height = 8;
            scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.OutputWindow      = hWnd;
            scd.SampleDesc.Count  = 1;
            scd.Windowed          = TRUE;
            scd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

            ID3D11Device*   pDevice    = nullptr;
            IDXGISwapChain* pSwapChain = nullptr;

            if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(
                    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                    nullptr, 0, D3D11_SDK_VERSION,
                    &scd, &pSwapChain, &pDevice, nullptr, nullptr)) && pSwapChain)
            {
                void** vtable = *reinterpret_cast<void***>(pSwapChain);
                DWORD oldProtect;
                if (VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
                {
                    vtable[8] = reinterpret_cast<void*>(g_OriginalPresent);
                    VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &oldProtect);
                }
                pSwapChain->Release();
                if (pDevice) pDevice->Release();
            }
            DestroyWindow(hWnd);
        }

        ReleaseStagingTexture();
        g_OriginalPresent = nullptr;
        g_hookInstalled.store(false);
    }
}
