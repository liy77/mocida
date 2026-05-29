// webview_dcomp.cpp
//
// C++ wrapper around DirectComposition so we can keep webview.c clean
// of the dcomp.h overload-in-C breakage. Only the methods actually
// used by the composition-mode webview are exposed.

#include <uikit/webview_dcomp.h>

#ifdef _WIN32

#include <windows.h>
#include <dcomp.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/implements.h>
#include <wrl/client.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <new>     // std::nothrow

/* debug.h is included AFTER the Windows / D2D headers so windows.h's
 * #define DrawText DrawTextW has already substituted the D2D method
 * declarations consistently with the call sites in this file. Otherwise
 * the method gets declared as DrawText (no W) but called as DrawTextW. */
#include <uikit/debug.h>

using namespace Microsoft::WRL;

struct OverlayCell {
    int x = 0, y = 0, w = 0, h = 0;
    float radius = 0;
    UIWVDCompColor fill { 255, 255, 255, 1.0f };
    std::wstring text;
    std::wstring fontFamily = L"Segoe UI";
    float fontSize = 14.0f;
    UIWVDCompColor textColor { 0, 0, 0, 1.0f };
    float pad = 14.0f;
    IDCompositionVisual* visual = nullptr;
    IDCompositionSurface* surface = nullptr;
    bool alive = false;
};

struct UIWebViewDComp {
    IDCompositionDevice*        device         = nullptr;
    IDCompositionDevice2*       device2        = nullptr; // for surface alloc
    IDCompositionTarget*        target         = nullptr;
    // Root container at window origin. Transparent itself - only its
    // children (webviewVisual + D2D overlays) draw. Lets SDL paint
    // (border ring around the webview) remain visible.
    IDCompositionVisual*        rootVisual     = nullptr;
    // Visual WebView2 renders into (returned by GetRootVisualAsIUnknown
    // and passed to put_RootVisualTarget). Inset inside rootVisual by
    // the configured border width.
    IDCompositionVisual*        webviewVisual  = nullptr;
    IDCompositionRectangleClip* clip           = nullptr;

    // Rounded-corner mask: a D2D-rendered surface that paints opaque
    // cornerColor over the 4 corner triangles of the webview visual
    // (the area outside the rounded body, inside the rectangular
    // bounding box). Sits as a sibling visual on top of the webview
    // visual so DWM composes it after webview content but before any
    // user-added D2D overlay.
    IDCompositionVisual*  cornerMaskVisual  = nullptr;
    IDCompositionSurface* cornerMaskSurface = nullptr;
    int   cornerMaskW       = 0;
    int   cornerMaskH       = 0;
    float cornerMaskRadius  = -1.0f;
    UIWVDCompColor cornerMaskColor { 0, 0, 0, 0 };
    bool  cornerMaskAttached = false;

    // Lazy D3D11 / D2D / DWrite (built on first overlay).
    ID3D11Device*         d3dDevice    = nullptr;
    IDXGIDevice*          dxgiDevice   = nullptr;
    ID2D1Factory1*        d2dFactory   = nullptr;
    ID2D1Device*          d2dDevice    = nullptr;
    ID2D1DeviceContext*   d2dContext   = nullptr;
    IDWriteFactory*       dwriteFactory= nullptr;

    HWND hwnd = nullptr;
    std::vector<OverlayCell> overlays;
};

// Forward decls.
static bool   EnsureD2DStack(UIWebViewDComp* dc);
static bool   EnsureDevice2 (UIWebViewDComp* dc);
static bool   RebuildOverlaySurface(UIWebViewDComp* dc, OverlayCell& cell);
static void   RedrawOverlay(UIWebViewDComp* dc, OverlayCell& cell);
static void   FreeOverlay  (OverlayCell& cell);

// ------------------------------------------------------------------
// WebView2 environment options factory. Uses the SDK's official
// CoreWebView2EnvironmentOptions implementation (WRL-based) - rolling
// our own from the bare ICoreWebView2EnvironmentOptions interface had
// the env creation reject every argument with E_INVALIDARG because
// the SDK validates fields we weren't filling in correctly.
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// Lazy D3D11 + D2D + DWrite init. Called the first time an overlay
// is requested - we don't want to pay these costs when no overlay
// has been added.
// ------------------------------------------------------------------
static bool EnsureD2DStack(UIWebViewDComp* dc) {
    if (!dc) return false;
    if (dc->d2dContext) return true;
    // D3D11 + DXGI were built up front by UIWebViewDComp_Create.
    if (!dc->d3dDevice || !dc->dxgiDevice) return false;

    HRESULT hr;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           __uuidof(ID2D1Factory1), nullptr,
                           (void**)&dc->d2dFactory);
    if (FAILED(hr) || !dc->d2dFactory) return false;

    hr = dc->d2dFactory->CreateDevice(dc->dxgiDevice, &dc->d2dDevice);
    if (FAILED(hr) || !dc->d2dDevice) return false;

    hr = dc->d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                            &dc->d2dContext);
    if (FAILED(hr) || !dc->d2dContext) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             (IUnknown**)&dc->dwriteFactory);
    if (FAILED(hr)) {
        UI_ERROR(UI_CAT_WEBVIEW, "DWriteCreateFactory failed hr=0x%08lX",
                (unsigned long)hr);
        // Non-fatal: overlays without text still work.
    }

    return true;
}

static bool EnsureDevice2(UIWebViewDComp* dc) {
    if (!dc || !dc->device) return false;
    if (dc->device2) return true;
    HRESULT hr = dc->device->QueryInterface(__uuidof(IDCompositionDevice2),
                                            (void**)&dc->device2);
    if (FAILED(hr) || !dc->device2) {
        UI_ERROR(UI_CAT_WEBVIEW, "IDCompositionDevice2 unavailable hr=0x%08lX",
                (unsigned long)hr);
        return false;
    }
    return true;
}

// Allocates the DComp surface + visual for an overlay cell.
static bool RebuildOverlaySurface(UIWebViewDComp* dc, OverlayCell& cell) {
    FreeOverlay(cell);
    if (cell.w <= 0 || cell.h <= 0) return false;

    HRESULT hr = dc->device2->CreateSurface((UINT)cell.w, (UINT)cell.h,
                                            DXGI_FORMAT_B8G8R8A8_UNORM,
                                            DXGI_ALPHA_MODE_PREMULTIPLIED,
                                            &cell.surface);
    if (FAILED(hr) || !cell.surface) {
        UI_ERROR(UI_CAT_WEBVIEW, "CreateSurface failed hr=0x%08lX",
                (unsigned long)hr);
        return false;
    }

    hr = dc->device->CreateVisual(&cell.visual);
    if (FAILED(hr) || !cell.visual) {
        cell.surface->Release();
        cell.surface = nullptr;
        return false;
    }

    cell.visual->SetOffsetX(static_cast<float>(cell.x));
    cell.visual->SetOffsetY(static_cast<float>(cell.y));
    cell.visual->SetContent(cell.surface);

    // Add ABOVE the existing children (so overlays draw on top of
    // the webview's root visual content).
    dc->rootVisual->AddVisual(cell.visual, FALSE, nullptr);
    cell.alive = true;
    return true;
}

static D2D1_COLOR_F toD2D(const UIWVDCompColor& c) {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a);
}

static void RedrawOverlay(UIWebViewDComp* dc, OverlayCell& cell) {
    if (!cell.surface || !dc->d2dContext) return;

    IDXGISurface* dxgiSurf = nullptr;
    POINT offset = { 0, 0 };
    HRESULT hr = cell.surface->BeginDraw(nullptr, __uuidof(IDXGISurface),
                                         (void**)&dxgiSurf, &offset);
    if (FAILED(hr) || !dxgiSurf) {
        UI_ERROR(UI_CAT_WEBVIEW, "surface BeginDraw failed hr=0x%08lX",
                (unsigned long)hr);
        return;
    }

    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));

    ID2D1Bitmap1* bitmap = nullptr;
    hr = dc->d2dContext->CreateBitmapFromDxgiSurface(dxgiSurf, &bmpProps, &bitmap);
    if (FAILED(hr) || !bitmap) {
        dxgiSurf->Release();
        cell.surface->EndDraw();
        return;
    }

    dc->d2dContext->SetTarget(bitmap);
    dc->d2dContext->BeginDraw();
    // The surface origin is given by `offset` (atlas allocator may
    // pack multiple surfaces into one texture).
    dc->d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(
        (FLOAT)offset.x, (FLOAT)offset.y));
    dc->d2dContext->Clear(D2D1::ColorF(0, 0, 0, 0));  // transparent

    // Fill rounded rect.
    ID2D1SolidColorBrush* fillBrush = nullptr;
    dc->d2dContext->CreateSolidColorBrush(toD2D(cell.fill), &fillBrush);
    if (fillBrush) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(0.0f, 0.0f, (FLOAT)cell.w, (FLOAT)cell.h),
            cell.radius, cell.radius);
        dc->d2dContext->FillRoundedRectangle(&rr, fillBrush);
        fillBrush->Release();
    }

    // Text via DirectWrite if available + text set.
    if (!cell.text.empty() && dc->dwriteFactory) {
        IDWriteTextFormat* fmt = nullptr;
        dc->dwriteFactory->CreateTextFormat(
            cell.fontFamily.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, cell.fontSize, L"en-us", &fmt);
        if (fmt) {
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            ID2D1SolidColorBrush* textBrush = nullptr;
            dc->d2dContext->CreateSolidColorBrush(toD2D(cell.textColor), &textBrush);
            if (textBrush) {
                D2D1_RECT_F layout = D2D1::RectF(
                    cell.pad, cell.pad,
                    (FLOAT)cell.w - cell.pad,
                    (FLOAT)cell.h - cell.pad);
                dc->d2dContext->DrawText(
                    cell.text.c_str(), (UINT32)cell.text.size(),
                    fmt, &layout, textBrush);
                textBrush->Release();
            }
            fmt->Release();
        }
    }

    dc->d2dContext->EndDraw();
    dc->d2dContext->SetTarget(nullptr);
    bitmap->Release();
    dxgiSurf->Release();
    cell.surface->EndDraw();

    if (dc->device2) dc->device2->Commit();
    else if (dc->device) dc->device->Commit();
}

static void FreeOverlay(OverlayCell& cell) {
    if (cell.visual)  { cell.visual->Release();  cell.visual  = nullptr; }
    if (cell.surface) { cell.surface->Release(); cell.surface = nullptr; }
    cell.alive = false;
}

extern "C" {

void* UIWebViewOptions_Create(const char* additionalArgsUtf8) {
    auto options = Make<CoreWebView2EnvironmentOptions>();
    if (!options) return nullptr;

    if (additionalArgsUtf8 && *additionalArgsUtf8) {
        int n = MultiByteToWideChar(CP_UTF8, 0, additionalArgsUtf8, -1, nullptr, 0);
        if (n > 0) {
            std::wstring args(n - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, additionalArgsUtf8, -1, &args[0], n);
            options->put_AdditionalBrowserArguments(args.c_str());
        }
    }

    ICoreWebView2EnvironmentOptions* raw = nullptr;
    options.CopyTo(&raw);
    return raw;
}

void UIWebViewOptions_Release(void* options) {
    if (options) {
        static_cast<IUnknown*>(options)->Release();
    }
}

UIWebViewDComp* UIWebViewDComp_Create(HWND hwnd) {
    if (!hwnd) return nullptr;

    UIWebViewDComp* dc = new (std::nothrow) UIWebViewDComp{};
    if (!dc) return nullptr;
    dc->hwnd = hwnd;

    // Build D3D11 device up front so DCompositionCreateDevice2 can
    // bind to it - we need the Device2 interface for CreateSurface
    // (the legacy DCompositionCreateDevice only returns Device, which
    // doesn't have CreateSurface).
    UINT d3dFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   d3dFlags, nullptr, 0, D3D11_SDK_VERSION,
                                   &dc->d3dDevice, &fl, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               d3dFlags, nullptr, 0, D3D11_SDK_VERSION,
                               &dc->d3dDevice, &fl, nullptr);
    }
    if (FAILED(hr) || !dc->d3dDevice) {
        UI_ERROR(UI_CAT_WEBVIEW, "D3D11CreateDevice failed hr=0x%08lX",
                 (unsigned long)hr);
        delete dc;
        return nullptr;
    }
    hr = dc->d3dDevice->QueryInterface(__uuidof(IDXGIDevice),
                                       (void**)&dc->dxgiDevice);
    if (FAILED(hr) || !dc->dxgiDevice) {
        UI_ERROR(UI_CAT_WEBVIEW, "QI IDXGIDevice failed hr=0x%08lX",
                 (unsigned long)hr);
        UIWebViewDComp_Destroy(dc);
        return nullptr;
    }

    // Try IDCompositionDesktopDevice first (inherits Device2 and adds
    // CreateTargetForHwnd on the same interface). Fall back to plain
    // Device2 if that fails. The renderingDevice param accepts either
    // a DXGI device or a D3D device; we pass the D3D11 device.
    IDCompositionDesktopDevice* desktopDev = nullptr;
    hr = DCompositionCreateDevice2(dc->d3dDevice,
                                   __uuidof(IDCompositionDesktopDevice),
                                   reinterpret_cast<void**>(&desktopDev));
    if (SUCCEEDED(hr) && desktopDev) {
        desktopDev->QueryInterface(__uuidof(IDCompositionDevice2),
                                   reinterpret_cast<void**>(&dc->device2));
        desktopDev->QueryInterface(__uuidof(IDCompositionDevice),
                                   reinterpret_cast<void**>(&dc->device));
        desktopDev->Release();
    } else {
        UI_ERROR(UI_CAT_WEBVIEW,
                 "DCompositionCreateDevice2(DesktopDevice) failed hr=0x%08lX, "
                 "trying Device2 directly", (unsigned long)hr);
        hr = DCompositionCreateDevice2(dc->d3dDevice,
                                       __uuidof(IDCompositionDevice2),
                                       reinterpret_cast<void**>(&dc->device2));
        if (SUCCEEDED(hr) && dc->device2) {
            dc->device2->QueryInterface(__uuidof(IDCompositionDevice),
                                        reinterpret_cast<void**>(&dc->device));
        }
    }
    if (!dc->device2 || !dc->device) {
        UI_ERROR(UI_CAT_WEBVIEW, "DCompositionCreateDevice2 failed hr=0x%08lX",
                 (unsigned long)hr);
        UIWebViewDComp_Destroy(dc);
        return nullptr;
    }

    hr = dc->device->CreateTargetForHwnd(hwnd, TRUE, &dc->target);
    if (FAILED(hr) || !dc->target) {
        UI_ERROR(UI_CAT_WEBVIEW, "CreateTargetForHwnd failed hr=0x%08lX",
                (unsigned long)hr);
        UIWebViewDComp_Destroy(dc);
        return nullptr;
    }

    hr = dc->device->CreateVisual(&dc->rootVisual);
    if (FAILED(hr) || !dc->rootVisual) {
        UI_ERROR(UI_CAT_WEBVIEW, "CreateVisual(root) failed hr=0x%08lX",
                (unsigned long)hr);
        UIWebViewDComp_Destroy(dc);
        return nullptr;
    }

    hr = dc->device->CreateVisual(&dc->webviewVisual);
    if (FAILED(hr) || !dc->webviewVisual) {
        UI_ERROR(UI_CAT_WEBVIEW, "CreateVisual(webview) failed hr=0x%08lX",
                (unsigned long)hr);
        UIWebViewDComp_Destroy(dc);
        return nullptr;
    }

    hr = dc->rootVisual->AddVisual(dc->webviewVisual, FALSE, nullptr);
    if (FAILED(hr)) {
        UIWebViewDComp_Destroy(dc);
        return nullptr;
    }

    hr = dc->target->SetRoot(dc->rootVisual);
    if (FAILED(hr)) {
        UI_ERROR(UI_CAT_WEBVIEW, "SetRoot failed hr=0x%08lX",
                (unsigned long)hr);
        UIWebViewDComp_Destroy(dc);
        return nullptr;
    }

    return dc;
}

void* UIWebViewDComp_GetRootVisualAsIUnknown(UIWebViewDComp* dc) {
    // WebView2 renders into the webviewVisual (child of root). The
    // root stays transparent so siblings (D2D overlays) and the SDL
    // border paint around the webview can be seen.
    return dc ? static_cast<IUnknown*>(dc->webviewVisual) : nullptr;
}

// Builds (or updates) the corner mask surface that paints `color` over
// the 4 corner triangles of a (w x h, radius) rounded shape. Strategy:
// 1. Clear to transparent
// 2. Set primitive blend to COPY (overwrites the destination alpha)
// 3. Fill the entire surface with `color` -- now fully opaque
// 4. FillRoundedRectangle with a transparent brush (alpha 0) in COPY
//    mode, which PUNCHES a transparent rounded hole in the middle
// 5. Restore SOURCE_OVER for any subsequent draws
//
// The result: corners are opaque `color`, rounded interior is fully
// transparent so the webview content underneath shows through.
static void BuildOrUpdateCornerMask(UIWebViewDComp* dc, int w, int h,
                                    float radius, UIWVDCompColor color) {
    if (!dc || !dc->device || !dc->device2) return;

    // If the new spec doesn't want a mask, detach + drop the resources.
    if (radius <= 0.0f || w <= 0 || h <= 0) {
        if (dc->cornerMaskAttached && dc->rootVisual && dc->cornerMaskVisual) {
            dc->rootVisual->RemoveVisual(dc->cornerMaskVisual);
            dc->cornerMaskAttached = false;
        }
        if (dc->cornerMaskVisual)  { dc->cornerMaskVisual->Release();  dc->cornerMaskVisual  = nullptr; }
        if (dc->cornerMaskSurface) { dc->cornerMaskSurface->Release(); dc->cornerMaskSurface = nullptr; }
        dc->cornerMaskW = 0; dc->cornerMaskH = 0;
        dc->cornerMaskRadius = -1.0f;
        return;
    }

    if (!EnsureD2DStack(dc)) return;

    const bool sizeChanged = (w != dc->cornerMaskW || h != dc->cornerMaskH);
    const bool colorChanged = (color.r != dc->cornerMaskColor.r ||
                               color.g != dc->cornerMaskColor.g ||
                               color.b != dc->cornerMaskColor.b ||
                               color.a != dc->cornerMaskColor.a);
    const bool radiusChanged = (radius != dc->cornerMaskRadius);
    const bool needsRedraw = sizeChanged || colorChanged || radiusChanged;

    if (sizeChanged || !dc->cornerMaskSurface) {
        if (dc->cornerMaskAttached && dc->cornerMaskVisual) {
            dc->rootVisual->RemoveVisual(dc->cornerMaskVisual);
            dc->cornerMaskAttached = false;
        }
        if (dc->cornerMaskVisual)  { dc->cornerMaskVisual->Release();  dc->cornerMaskVisual  = nullptr; }
        if (dc->cornerMaskSurface) { dc->cornerMaskSurface->Release(); dc->cornerMaskSurface = nullptr; }

        HRESULT hr = dc->device2->CreateSurface((UINT)w, (UINT)h,
                                                DXGI_FORMAT_B8G8R8A8_UNORM,
                                                DXGI_ALPHA_MODE_PREMULTIPLIED,
                                                &dc->cornerMaskSurface);
        if (FAILED(hr) || !dc->cornerMaskSurface) return;

        hr = dc->device->CreateVisual(&dc->cornerMaskVisual);
        if (FAILED(hr) || !dc->cornerMaskVisual) return;
        dc->cornerMaskVisual->SetContent(dc->cornerMaskSurface);
        dc->cornerMaskW = w; dc->cornerMaskH = h;
    }

    if (needsRedraw) {
        IDXGISurface* dxgi = nullptr;
        POINT off = { 0, 0 };
        HRESULT hr = dc->cornerMaskSurface->BeginDraw(nullptr, __uuidof(IDXGISurface),
                                                      (void**)&dxgi, &off);
        if (FAILED(hr) || !dxgi) return;

        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED));
        ID2D1Bitmap1* bmp = nullptr;
        dc->d2dContext->CreateBitmapFromDxgiSurface(dxgi, &bp, &bmp);
        if (bmp) {
            dc->d2dContext->SetTarget(bmp);
            dc->d2dContext->BeginDraw();
            dc->d2dContext->SetTransform(D2D1::Matrix3x2F::Translation((FLOAT)off.x, (FLOAT)off.y));
            dc->d2dContext->Clear(D2D1::ColorF(0, 0, 0, 0));

            // Phase 1: fill everything in `color`.
            dc->d2dContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
            ID2D1SolidColorBrush* fill = nullptr;
            dc->d2dContext->CreateSolidColorBrush(toD2D(color), &fill);
            if (fill) {
                dc->d2dContext->FillRectangle(D2D1::RectF(0, 0, (FLOAT)w, (FLOAT)h), fill);
                fill->Release();
            }
            // Phase 2: punch a transparent rounded rect in the middle.
            ID2D1SolidColorBrush* clear = nullptr;
            dc->d2dContext->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &clear);
            if (clear) {
                D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
                    D2D1::RectF(0, 0, (FLOAT)w, (FLOAT)h), radius, radius);
                dc->d2dContext->FillRoundedRectangle(&rr, clear);
                clear->Release();
            }
            dc->d2dContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
            dc->d2dContext->EndDraw();
            dc->d2dContext->SetTarget(nullptr);
            bmp->Release();
        }
        dxgi->Release();
        dc->cornerMaskSurface->EndDraw();

        dc->cornerMaskRadius = radius;
        dc->cornerMaskColor  = color;
    }
}

void UIWebViewDComp_SetBounds(UIWebViewDComp* dc, int x, int y, int w, int h,
                              int borderW, float radius, UIWVDCompColor cornerColor) {
    if (!dc || !dc->rootVisual || !dc->webviewVisual || !dc->device) return;

    // rootVisual sits at window origin so D2D overlay children can be
    // positioned in window-pixel coordinates directly. webviewVisual
    // is inset by borderW so the SDL paint of the border ring at
    // (x, y, w, h) remains visible around the webview content.
    dc->rootVisual->SetOffsetX(0.0f);
    dc->rootVisual->SetOffsetY(0.0f);

    const int innerW = w - 2 * borderW;
    const int innerH = h - 2 * borderW;
    const int innerX = x + borderW;
    const int innerY = y + borderW;
    dc->webviewVisual->SetOffsetX(static_cast<float>(innerX));
    dc->webviewVisual->SetOffsetY(static_cast<float>(innerY));

    if (!dc->clip) {
        HRESULT hr = dc->device->CreateRectangleClip(&dc->clip);
        if (FAILED(hr) || !dc->clip) {
            UI_ERROR(UI_CAT_WEBVIEW, "CreateRectangleClip failed hr=0x%08lX",
                    (unsigned long)hr);
            dc->clip = nullptr;
        }
    }
    if (dc->clip) {
        dc->clip->SetLeft(0.0f);
        dc->clip->SetTop(0.0f);
        dc->clip->SetRight(static_cast<float>(innerW > 0 ? innerW : 1));
        dc->clip->SetBottom(static_cast<float>(innerH > 0 ? innerH : 1));
        dc->webviewVisual->SetClip(dc->clip);
    }

    // Build or refresh the corner mask. The mask radius accounts for
    // the border: SDL paints a rounded border ring with the widget's
    // radius, and the webview visual sits inside the border, so the
    // visual's "inner" rounded radius is `radius - borderW`.
    const float innerRadius = (radius > (float)borderW) ? (radius - (float)borderW) : 0.0f;
    BuildOrUpdateCornerMask(dc, innerW, innerH, innerRadius, cornerColor);

    if (dc->cornerMaskVisual) {
        dc->cornerMaskVisual->SetOffsetX(static_cast<float>(innerX));
        dc->cornerMaskVisual->SetOffsetY(static_cast<float>(innerY));
        if (!dc->cornerMaskAttached && dc->rootVisual && innerRadius > 0.0f) {
            // Insert RIGHT AFTER webviewVisual so the mask renders on
            // top of the webview but BELOW any user D2D overlays.
            dc->rootVisual->AddVisual(dc->cornerMaskVisual, TRUE, dc->webviewVisual);
            dc->cornerMaskAttached = true;
        }
    }

    dc->device->Commit();
}

void UIWebViewDComp_Commit(UIWebViewDComp* dc) {
    if (dc && dc->device) dc->device->Commit();
}

void UIWebViewDComp_Destroy(UIWebViewDComp* dc) {
    if (!dc) return;
    for (auto& cell : dc->overlays) FreeOverlay(cell);
    dc->overlays.clear();

    if (dc->dwriteFactory) { dc->dwriteFactory->Release(); dc->dwriteFactory = nullptr; }
    if (dc->d2dContext)    { dc->d2dContext->Release();    dc->d2dContext    = nullptr; }
    if (dc->d2dDevice)     { dc->d2dDevice->Release();     dc->d2dDevice     = nullptr; }
    if (dc->d2dFactory)    { dc->d2dFactory->Release();    dc->d2dFactory    = nullptr; }
    if (dc->dxgiDevice)    { dc->dxgiDevice->Release();    dc->dxgiDevice    = nullptr; }
    if (dc->d3dDevice)     { dc->d3dDevice->Release();     dc->d3dDevice     = nullptr; }

    if (dc->cornerMaskVisual)  { dc->cornerMaskVisual->Release();  dc->cornerMaskVisual  = nullptr; }
    if (dc->cornerMaskSurface) { dc->cornerMaskSurface->Release(); dc->cornerMaskSurface = nullptr; }
    if (dc->clip)              { dc->clip->Release();              dc->clip              = nullptr; }
    if (dc->webviewVisual)     { dc->webviewVisual->Release();     dc->webviewVisual     = nullptr; }
    if (dc->rootVisual)        { dc->rootVisual->Release();        dc->rootVisual        = nullptr; }
    if (dc->target)        { dc->target->Release();        dc->target        = nullptr; }
    if (dc->device2)    { dc->device2->Release();    dc->device2    = nullptr; }
    if (dc->device)     { dc->device->Release();     dc->device     = nullptr; }
    delete dc;
}

// ------------------------------------------------------------------
// Public overlay API
// ------------------------------------------------------------------

static std::wstring Utf8ToWide(const char* s) {
    std::wstring out;
    if (!s || !*s) return out;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n > 0) {
        out.resize(n - 1);
        MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], n);
    }
    return out;
}

int UIWebViewDComp_AddOverlay(UIWebViewDComp* dc,
                              int x, int y, int w, int h,
                              float radius,
                              UIWVDCompColor fill) {
    if (!dc) { fprintf(stderr, "[webview/comp] AddOverlay: dc=NULL\n"); return -1; }
    if (!EnsureDevice2(dc)) {
        fprintf(stderr, "[webview/comp] AddOverlay: EnsureDevice2 failed\n");
        return -1;
    }
    if (!EnsureD2DStack(dc)) {
        fprintf(stderr, "[webview/comp] AddOverlay: EnsureD2DStack failed\n");
        return -1;
    }

    OverlayCell cell;
    cell.x = x; cell.y = y; cell.w = w; cell.h = h;
    cell.radius = radius;
    cell.fill = fill;

    if (!RebuildOverlaySurface(dc, cell)) {
        fprintf(stderr, "[webview/comp] AddOverlay: RebuildOverlaySurface failed\n");
        return -1;
    }
    fprintf(stderr, "[webview/comp] AddOverlay OK at (%d,%d %dx%d) radius=%.1f visual=%p surface=%p\n",
            x, y, w, h, radius, (void*)cell.visual, (void*)cell.surface);
    fflush(stderr);
    RedrawOverlay(dc, cell);

    // Find a free slot, else append.
    for (size_t i = 0; i < dc->overlays.size(); ++i) {
        if (!dc->overlays[i].alive) {
            dc->overlays[i] = cell;
            return (int)i;
        }
    }
    dc->overlays.push_back(cell);
    return (int)dc->overlays.size() - 1;
}

void UIWebViewDComp_SetOverlayText(UIWebViewDComp* dc, int handle,
                                   const char* utf8Text,
                                   const char* family,
                                   float fontSize,
                                   UIWVDCompColor textColor,
                                   float pad) {
    if (!dc || handle < 0 || handle >= (int)dc->overlays.size()) return;
    OverlayCell& cell = dc->overlays[handle];
    if (!cell.alive) return;
    cell.text = Utf8ToWide(utf8Text);
    cell.fontFamily = (family && *family) ? Utf8ToWide(family) : std::wstring(L"Segoe UI");
    cell.fontSize = fontSize > 0 ? fontSize : 14.0f;
    cell.textColor = textColor;
    cell.pad = pad >= 0 ? pad : 14.0f;
    RedrawOverlay(dc, cell);
}

void UIWebViewDComp_MoveOverlay(UIWebViewDComp* dc, int handle,
                                int x, int y, int w, int h) {
    if (!dc || handle < 0 || handle >= (int)dc->overlays.size()) return;
    OverlayCell& cell = dc->overlays[handle];
    if (!cell.alive) return;
    bool sizeChanged = (w != cell.w || h != cell.h);
    cell.x = x; cell.y = y; cell.w = w; cell.h = h;
    if (sizeChanged) {
        // Have to rebuild surface at the new size.
        RebuildOverlaySurface(dc, cell);
        RedrawOverlay(dc, cell);
    } else if (cell.visual) {
        cell.visual->SetOffsetX((float)x);
        cell.visual->SetOffsetY((float)y);
        if (dc->device2) dc->device2->Commit();
        else if (dc->device) dc->device->Commit();
    }
}

void UIWebViewDComp_RemoveOverlay(UIWebViewDComp* dc, int handle) {
    if (!dc || handle < 0 || handle >= (int)dc->overlays.size()) return;
    OverlayCell& cell = dc->overlays[handle];
    if (!cell.alive) return;
    if (cell.visual && dc->rootVisual) {
        dc->rootVisual->RemoveVisual(cell.visual);
    }
    FreeOverlay(cell);
    if (dc->device2) dc->device2->Commit();
    else if (dc->device) dc->device->Commit();
}

} // extern "C"

#endif // _WIN32
