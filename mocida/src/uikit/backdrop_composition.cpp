// backdrop_composition.cpp — real acrylic via Windows.UI.Composition.
//
// The main (foreground) window's content is rendered by SDL into a COMPOSITION
// swap chain (see the D3D11 renderer patch). We host that swap chain in a
// Windows.UI.Composition visual tree on the SAME HWND, with a HOST-BACKDROP
// brush visual BEHIND it. Because the window is the foreground window, the
// host-backdrop samples the real desktop and blurs it (acrylic); the app's
// transparent (alpha-0) holes then reveal that blur. This is how WinUI / Windows
// Terminal compose acrylic — and it sidesteps every wall the DWM system backdrop
// and a separate companion window hit (occlusion culling, foreground-only host
// backdrop, redirection-surface mismatch).
//
// Exposed as C. Windows only.

#ifdef _WIN32

#include <windows.h>
#include <dispatcherqueue.h>
#include <stdio.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <windows.ui.composition.interop.h>

namespace wf   = winrt::Windows::Foundation;
namespace wux  = winrt::Windows::UI;
namespace wuc  = winrt::Windows::UI::Composition;
namespace wucd = winrt::Windows::UI::Composition::Desktop;
namespace wsys = winrt::Windows::System;

struct UICompBackdrop {
    wsys::DispatcherQueueController dqc{ nullptr };
    wuc::Compositor                 compositor{ nullptr };
    wucd::DesktopWindowTarget       target{ nullptr };
    wuc::ContainerVisual            root{ nullptr };
    wuc::SpriteVisual               backdrop{ nullptr };
    wuc::SpriteVisual               tint{ nullptr };
    wuc::SpriteVisual               content{ nullptr };
};

// Create the DispatcherQueue + Compositor + DesktopWindowTarget for `hwnd`.
// Returns false on failure (caller deletes bd).
static bool init_compositor(UICompBackdrop* bd, HWND hwnd) {
    DispatcherQueueOptions dqOpts{ sizeof(DispatcherQueueOptions),
                                   DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
    typedef HRESULT(WINAPI* PFN_CDQC)(
        DispatcherQueueOptions, ABI::Windows::System::IDispatcherQueueController**);
    HMODULE coreMsg = LoadLibraryW(L"CoreMessaging.dll");
    PFN_CDQC createDqc = coreMsg
        ? (PFN_CDQC)GetProcAddress(coreMsg, "CreateDispatcherQueueController") : nullptr;
    if (!createDqc) return false;
    winrt::check_hresult(createDqc(dqOpts,
        reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(
            winrt::put_abi(bd->dqc))));

    bd->compositor = wuc::Compositor();

    auto interop = bd->compositor.as<
        ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
    winrt::check_hresult(interop->CreateDesktopWindowTarget(hwnd, false,
        reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(
            winrt::put_abi(bd->target))));
    return true;
}

static wux::Color make_color(unsigned int tintRGB, float tintOpacity) {
    float a = tintOpacity; if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    wux::Color col{};
    col.A = (uint8_t)(a * 255.0f);
    col.R = (uint8_t)((tintRGB >> 16) & 0xFF);
    col.G = (uint8_t)((tintRGB >> 8) & 0xFF);
    col.B = (uint8_t)(tintRGB & 0xFF);
    return col;
}

// Host the SDL composition swap chain on `hwnd` with a host-backdrop acrylic
// visual behind it. `swapchain` is the IDXGISwapChain1* SDL created
// (SDL_PROP_RENDERER_D3D11_SWAPCHAIN_POINTER).
extern "C" void* UIBackdropComposition_AttachToWindow(HWND hwnd, void* swapchain,
                                                      unsigned int tintRGB,
                                                      float tintOpacity) {
    if (!hwnd || !swapchain) return nullptr;
    try {
        try { winrt::init_apartment(winrt::apartment_type::single_threaded); }
        catch (winrt::hresult_error const&) {}

        auto bd = new UICompBackdrop();
        if (!init_compositor(bd, hwnd)) { delete bd; return nullptr; }
        fprintf(stderr, "[backdrop/comp] step1 compositor+target ok\n"); fflush(stderr);

        // Root container, sized to the window (kept in sync via _Resize).
        bd->root = bd->compositor.CreateContainerVisual();
        bd->root.Size({ 16.0f, 16.0f });

        // (1) Backdrop: the blurred desktop behind the (foreground) window.
        bd->backdrop = bd->compositor.CreateSpriteVisual();
        bd->backdrop.RelativeSizeAdjustment({ 1.0f, 1.0f });
        bd->backdrop.Brush(bd->compositor.CreateHostBackdropBrush());
        fprintf(stderr, "[backdrop/comp] step2 host-backdrop brush ok\n"); fflush(stderr);

        // (2) Tint over the backdrop → the acrylic colour.
        bd->tint = bd->compositor.CreateSpriteVisual();
        bd->tint.RelativeSizeAdjustment({ 1.0f, 1.0f });
        bd->tint.Brush(bd->compositor.CreateColorBrush(make_color(tintRGB, tintOpacity)));

        // (3) The SDL-rendered content on top (its alpha-0 holes reveal 1+2).
        auto compInterop = bd->compositor.as<
            ABI::Windows::UI::Composition::ICompositorInterop>();
        wuc::ICompositionSurface surface{ nullptr };
        winrt::check_hresult(compInterop->CreateCompositionSurfaceForSwapChain(
            static_cast<IUnknown*>(swapchain),
            reinterpret_cast<ABI::Windows::UI::Composition::ICompositionSurface**>(
                winrt::put_abi(surface))));
        fprintf(stderr, "[backdrop/comp] step3 swapchain surface ok\n"); fflush(stderr);
        bd->content = bd->compositor.CreateSpriteVisual();
        bd->content.RelativeSizeAdjustment({ 1.0f, 1.0f });
        bd->content.Brush(bd->compositor.CreateSurfaceBrush(surface));

        // Z-order: backdrop (bottom) → tint → SDL content (top).
        bd->root.Children().InsertAtBottom(bd->backdrop);
        bd->root.Children().InsertAtTop(bd->tint);
        bd->root.Children().InsertAtTop(bd->content);

        bd->target.Root(bd->root);
        fprintf(stderr, "[backdrop/comp] composition acrylic attached to window hwnd=%p\n", (void*)hwnd);
        fflush(stderr);
        return bd;
    } catch (winrt::hresult_error const& e) {
        fprintf(stderr, "[backdrop/comp] AttachToWindow FAILED hr=0x%08X: %ls\n",
                (unsigned)e.code(), e.message().c_str());
        fflush(stderr);
        return nullptr;
    } catch (...) {
        fprintf(stderr, "[backdrop/comp] AttachToWindow FAILED (unknown)\n");
        fflush(stderr);
        return nullptr;
    }
}

extern "C" void UIBackdropComposition_Resize(void* handle, int w, int h) {
    if (!handle) return;
    auto bd = static_cast<UICompBackdrop*>(handle);
    try {
        if (bd->root) bd->root.Size({ (float)(w > 0 ? w : 1), (float)(h > 0 ? h : 1) });
    } catch (...) {}
}

extern "C" void UIBackdropComposition_Destroy(void* handle) {
    if (!handle) return;
    auto bd = static_cast<UICompBackdrop*>(handle);
    try {
        if (bd->target) bd->target.Root(nullptr);
    } catch (...) {}
    delete bd; // projected smart pointers release their COM refs
}

#endif // _WIN32
