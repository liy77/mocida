// backdrop_composition.cpp — real acrylic via Windows.UI.Composition.
//
// The DWM system backdrop (Mica/Acrylic via DwmSetWindowAttribute) is culled by
// DWM to a window's VISIBLE region, so it never renders behind a window that is
// fully occluded — useless for a backdrop companion parked under the main window.
// A Windows.UI.Composition *host-backdrop brush* instead renders the blurred
// desktop into our OWN DirectComposition visual tree, which composes regardless of
// occlusion. The main window's transparent holes then reveal it. This is the
// mechanism WinUI / Windows Terminal use for acrylic.
//
// Exposed as C for backdrop_companion.c. Windows only.

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
    wuc::SpriteVisual               rootV{ nullptr };
    wuc::SpriteVisual               tintV{ nullptr };
};

extern "C" void* UIBackdropComposition_Create(HWND hwnd, unsigned int tintRGB,
                                              float tintOpacity) {
    if (!hwnd) return nullptr;
    try {
        // COM/WinRT apartment for this thread. SDL may already have initialised
        // COM in a different mode — that's fine, swallow the change-mode error.
        try {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
        } catch (winrt::hresult_error const&) { /* already initialised */ }

        auto bd = new UICompBackdrop();

        // The Compositor needs a DispatcherQueue on the calling thread; SDL's main
        // message loop services it. CreateDispatcherQueueController lives in
        // CoreMessaging.dll — GetProcAddress it so we need no extra import lib.
        DispatcherQueueOptions dqOpts{ sizeof(DispatcherQueueOptions),
                                       DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
        typedef HRESULT(WINAPI* PFN_CDQC)(
            DispatcherQueueOptions, ABI::Windows::System::IDispatcherQueueController**);
        HMODULE coreMsg = LoadLibraryW(L"CoreMessaging.dll");
        PFN_CDQC createDqc = coreMsg
            ? (PFN_CDQC)GetProcAddress(coreMsg, "CreateDispatcherQueueController") : nullptr;
        if (!createDqc) { delete bd; return nullptr; }
        winrt::check_hresult(createDqc(dqOpts,
            reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(
                winrt::put_abi(bd->dqc))));

        bd->compositor = wuc::Compositor();

        // Bind a composition target to the HWND (interop). put_abi avoids a manual
        // Release on the incomplete ABI interface.
        auto interop = bd->compositor.as<
            ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        winrt::check_hresult(interop->CreateDesktopWindowTarget(hwnd, false,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(
                winrt::put_abi(bd->target))));

        // Root sprite fills the window with the blurred host backdrop (the desktop
        // behind the window). The ROOT visual has no parent visual, so
        // RelativeSizeAdjustment is meaningless on it — its Size must be set
        // explicitly (and kept in sync with the window via _Resize).
        bd->rootV = bd->compositor.CreateSpriteVisual();
        bd->rootV.Size({ 16.0f, 16.0f });
        bd->rootV.Brush(bd->compositor.CreateHostBackdropBrush());

        // Tint layer on top → the acrylic colour. Relative to the (now-sized) root.
        float a = tintOpacity; if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
        wux::Color col{};
        col.A = (uint8_t)(a * 255.0f);
        col.R = (uint8_t)((tintRGB >> 16) & 0xFF);
        col.G = (uint8_t)((tintRGB >> 8) & 0xFF);
        col.B = (uint8_t)(tintRGB & 0xFF);
        bd->tintV = bd->compositor.CreateSpriteVisual();
        bd->tintV.RelativeSizeAdjustment({ 1.0f, 1.0f });
        bd->tintV.Brush(bd->compositor.CreateColorBrush(col));
        bd->rootV.Children().InsertAtTop(bd->tintV);

        bd->target.Root(bd->rootV);
        fprintf(stderr, "[backdrop/comp] host-backdrop acrylic attached to hwnd=%p\n", (void*)hwnd);
        fflush(stderr);
        return bd;
    } catch (winrt::hresult_error const& e) {
        fprintf(stderr, "[backdrop/comp] FAILED hr=0x%08X: %ls\n",
                (unsigned)e.code(), e.message().c_str());
        fflush(stderr);
        return nullptr;
    } catch (...) {
        fprintf(stderr, "[backdrop/comp] FAILED (unknown exception)\n");
        fflush(stderr);
        return nullptr;
    }
}

extern "C" void UIBackdropComposition_Resize(void* handle, int w, int h) {
    if (!handle) return;
    auto bd = static_cast<UICompBackdrop*>(handle);
    try {
        if (bd->rootV) {
            bd->rootV.Size({ (float)(w > 0 ? w : 1), (float)(h > 0 ? h : 1) });
        }
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
