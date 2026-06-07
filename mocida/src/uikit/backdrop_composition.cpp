// backdrop_composition.cpp — composite the SDL composition swap chain (and, when
// possible, a real acrylic backdrop) on the main window.
//
// Two paths:
//   (A) Windows.UI.Composition with a HOST-BACKDROP brush behind the SDL content
//       → real blurred-desktop acrylic. Requires bridging SDL's swap chain into a
//       WUC surface (CreateCompositionSurfaceForSwapChain), which currently fails
//       with DXGI_ERROR_UNSUPPORTED on this stack (device-share). Kept; tried first.
//   (B) Fallback: DirectComposition hosting the swap chain via SetContent (the
//       battle-tested path). No host-backdrop, so the transparent holes show the
//       desktop sharp — but the window shows content instead of black.
//
// Windows only.

#ifdef _WIN32

#include <windows.h>
#include <dispatcherqueue.h>
#include <dcomp.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <windows.ui.composition.interop.h>

namespace wux  = winrt::Windows::UI;
namespace wuc  = winrt::Windows::UI::Composition;
namespace wucd = winrt::Windows::UI::Composition::Desktop;
namespace wsys = winrt::Windows::System;

struct UICompBackdrop {
    // WUC path (A).
    wsys::DispatcherQueueController dqc{ nullptr };
    wuc::Compositor                 compositor{ nullptr };
    wucd::DesktopWindowTarget       target{ nullptr };
    wuc::ContainerVisual            root{ nullptr };
    wuc::SpriteVisual               backdrop{ nullptr };
    wuc::SpriteVisual               tint{ nullptr };
    wuc::SpriteVisual               content{ nullptr };
    // DComp fallback path (B) — raw COM.
    IDCompositionDevice* dcompDevice = nullptr;
    IDCompositionTarget* dcompTarget = nullptr;
    IDCompositionVisual* dcompVisual = nullptr;
    bool usedWuc = false;
};

extern "C" void UIBackdropComposition_Destroy(void* handle); // fwd decl

static wux::Color make_color(unsigned int tintRGB, float tintOpacity) {
    float a = tintOpacity; if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    wux::Color col{};
    col.A = (uint8_t)(a * 255.0f);
    col.R = (uint8_t)((tintRGB >> 16) & 0xFF);
    col.G = (uint8_t)((tintRGB >> 8) & 0xFF);
    col.B = (uint8_t)(tintRGB & 0xFF);
    return col;
}

// Path (A): full WUC tree with host-backdrop acrylic. Throws on failure.
static void attach_wuc(UICompBackdrop* bd, HWND hwnd, void* swapchain,
                       unsigned int tintRGB, float tintOpacity) {
    DispatcherQueueOptions dqOpts{ sizeof(DispatcherQueueOptions),
                                   DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
    typedef HRESULT(WINAPI* PFN_CDQC)(
        DispatcherQueueOptions, ABI::Windows::System::IDispatcherQueueController**);
    HMODULE coreMsg = LoadLibraryW(L"CoreMessaging.dll");
    PFN_CDQC createDqc = coreMsg
        ? (PFN_CDQC)GetProcAddress(coreMsg, "CreateDispatcherQueueController") : nullptr;
    if (!createDqc) throw winrt::hresult_error(E_FAIL);
    winrt::check_hresult(createDqc(dqOpts,
        reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(
            winrt::put_abi(bd->dqc))));

    bd->compositor = wuc::Compositor();

    auto interop = bd->compositor.as<
        ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
    winrt::check_hresult(interop->CreateDesktopWindowTarget(hwnd, false,
        reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(
            winrt::put_abi(bd->target))));

    bd->root = bd->compositor.CreateContainerVisual();
    bd->root.Size({ 16.0f, 16.0f });

    bd->backdrop = bd->compositor.CreateSpriteVisual();
    bd->backdrop.RelativeSizeAdjustment({ 1.0f, 1.0f });
    bd->backdrop.Brush(bd->compositor.CreateHostBackdropBrush());

    bd->tint = bd->compositor.CreateSpriteVisual();
    bd->tint.RelativeSizeAdjustment({ 1.0f, 1.0f });
    bd->tint.Brush(bd->compositor.CreateColorBrush(make_color(tintRGB, tintOpacity)));

    auto compInterop = bd->compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
    wuc::ICompositionSurface surface{ nullptr };
    winrt::check_hresult(compInterop->CreateCompositionSurfaceForSwapChain(
        static_cast<IUnknown*>(swapchain),
        reinterpret_cast<ABI::Windows::UI::Composition::ICompositionSurface**>(
            winrt::put_abi(surface))));
    bd->content = bd->compositor.CreateSpriteVisual();
    bd->content.RelativeSizeAdjustment({ 1.0f, 1.0f });
    bd->content.Brush(bd->compositor.CreateSurfaceBrush(surface));

    bd->root.Children().InsertAtBottom(bd->backdrop);
    bd->root.Children().InsertAtTop(bd->tint);
    bd->root.Children().InsertAtTop(bd->content);
    bd->target.Root(bd->root);
}

// Path (B): DirectComposition hosting of the swap chain (no host-backdrop).
// Returns false on failure.
static bool attach_dcomp(UICompBackdrop* bd, HWND hwnd, void* swapchain) {
    IDXGISwapChain1* sc = (IDXGISwapChain1*)swapchain;
    ID3D11Device* d3d = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&d3d)) || !d3d) return false;
    IDXGIDevice* dxgi = nullptr;
    HRESULT hr = d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi);
    d3d->Release();
    if (FAILED(hr) || !dxgi) return false;

    hr = DCompositionCreateDevice(dxgi, __uuidof(IDCompositionDevice), (void**)&bd->dcompDevice);
    dxgi->Release();
    if (FAILED(hr) || !bd->dcompDevice) return false;

    hr = bd->dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &bd->dcompTarget);
    if (FAILED(hr) || !bd->dcompTarget) return false;
    hr = bd->dcompDevice->CreateVisual(&bd->dcompVisual);
    if (FAILED(hr) || !bd->dcompVisual) return false;
    bd->dcompVisual->SetContent((IUnknown*)sc);
    bd->dcompTarget->SetRoot(bd->dcompVisual);
    bd->dcompDevice->Commit();
    return true;
}

extern "C" void* UIBackdropComposition_AttachToWindow(HWND hwnd, void* swapchain,
                                                      unsigned int tintRGB,
                                                      float tintOpacity) {
    if (!hwnd || !swapchain) return nullptr;
    try { winrt::init_apartment(winrt::apartment_type::single_threaded); }
    catch (winrt::hresult_error const&) {}

    auto bd = new UICompBackdrop();

    // (A) Try the real acrylic (WUC host-backdrop).
    try {
        attach_wuc(bd, hwnd, swapchain, tintRGB, tintOpacity);
        bd->usedWuc = true;
        fprintf(stderr, "[backdrop/comp] WUC host-backdrop acrylic attached\n"); fflush(stderr);
        return bd;
    } catch (winrt::hresult_error const& e) {
        fprintf(stderr, "[backdrop/comp] WUC path failed hr=0x%08X (%ls) — falling back to DComp\n",
                (unsigned)e.code(), e.message().c_str());
        fflush(stderr);
    } catch (...) {
        fprintf(stderr, "[backdrop/comp] WUC path failed (unknown) — falling back to DComp\n");
        fflush(stderr);
    }

    // (B) Fallback: at least show content (transparent holes, no blur).
    if (attach_dcomp(bd, hwnd, swapchain)) {
        fprintf(stderr, "[backdrop/comp] DComp fallback attached (content only, no blur)\n");
        fflush(stderr);
        return bd;
    }

    fprintf(stderr, "[backdrop/comp] both paths failed\n"); fflush(stderr);
    UIBackdropComposition_Destroy(bd); // declared below; safe (releases what's set)
    return nullptr;
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
    try { if (bd->target) bd->target.Root(nullptr); } catch (...) {}
    if (bd->dcompVisual) { bd->dcompVisual->Release(); bd->dcompVisual = nullptr; }
    if (bd->dcompTarget) { bd->dcompTarget->Release(); bd->dcompTarget = nullptr; }
    if (bd->dcompDevice) { bd->dcompDevice->Release(); bd->dcompDevice = nullptr; }
    delete bd;
}

#endif // _WIN32
