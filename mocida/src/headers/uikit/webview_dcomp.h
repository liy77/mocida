#ifndef UIKIT_WEBVIEW_DCOMP_H
#define UIKIT_WEBVIEW_DCOMP_H

/* ----------------------------------------------------------------- */
/* C-callable shim around DirectComposition.                          */
/*                                                                    */
/* The Windows SDK's dcomp.h declares overloaded COM methods that    */
/* fail to compile under C (duplicate struct members). We isolate    */
/* DComp use to a C++ translation unit (webview_dcomp.cpp) which     */
/* includes dcomp.h normally; this header is the C bridge.           */
/* ----------------------------------------------------------------- */

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>

typedef struct UIWebViewDComp UIWebViewDComp;

/* Color carried across the C/C++ boundary - kept independent of UIColor
 * so this header doesn't need to drag the full UI kit in. */
typedef struct {
    int r, g, b;        /* 0..255 */
    float a;            /* 0..1   */
} UIWVDCompColor;

/* Builds a DirectComposition pipeline tied to `hwnd` (topmost target).
 * Returns NULL on failure. */
UIWebViewDComp* UIWebViewDComp_Create(HWND hwnd);

/* Returns the IDCompositionVisual* (cast to IUnknown*) the caller
 * should pass to ICoreWebView2CompositionController::put_RootVisualTarget. */
void* UIWebViewDComp_GetRootVisualAsIUnknown(UIWebViewDComp* dc);

/* Re-positions the webview visual and clips it to the given rect.
 * `borderW` insets the webview content so a SDL-painted border ring
 * at (x, y, w, h) remains visible AROUND the visual.
 *
 * When `radius` > 0 a D2D-rendered corner mask is built that paints
 * `cornerColor` over the 4 corner triangles of the webview visual
 * (outside the rounded shape, inside the rect), giving the webview
 * actual rounded edges. Pass radius = 0 to remove the mask.
 *
 * Coordinates are in window pixel space. Triggers a Commit. */
void UIWebViewDComp_SetBounds(UIWebViewDComp* dc, int x, int y, int w, int h,
                              int borderW, float radius,
                              UIWVDCompColor cornerColor);

/* Forces a Commit (useful after the very first put_RootVisualTarget). */
void UIWebViewDComp_Commit(UIWebViewDComp* dc);

/* Releases every DComp object and frees the context. */
void UIWebViewDComp_Destroy(UIWebViewDComp* dc);

/* ----------------------------------------------------------------- */
/* D2D-rendered overlays inside the DComp tree.                       */
/*                                                                    */
/* Adds a child visual rendered via Direct2D + DirectWrite onto a     */
/* DComp surface. Because the visual has per-pixel alpha, the rounded */
/* shape composes cleanly over the webview - no SetWindowRgn punch    */
/* artifacts (the "fundinho" at the corners).                         */
/*                                                                    */
/* Coordinates are in window pixel space. Returns -1 on failure,      */
/* otherwise a non-negative handle the caller can update later.       */
/* ----------------------------------------------------------------- */
int UIWebViewDComp_AddOverlay(UIWebViewDComp* dc,
                              int x, int y, int w, int h,
                              float radius,
                              UIWVDCompColor fill);

/* Sets a text string drawn inside the overlay. Pass NULL to clear.
 * `family` defaults to "Segoe UI" if NULL. Coordinates are inside
 * the overlay (top-left origin), with padding `pad` from each edge. */
void UIWebViewDComp_SetOverlayText(UIWebViewDComp* dc, int handle,
                                   const char* utf8Text,
                                   const char* family,
                                   float fontSize,
                                   UIWVDCompColor textColor,
                                   float pad);

/* Re-positions and resizes an existing overlay. */
void UIWebViewDComp_MoveOverlay(UIWebViewDComp* dc, int handle,
                                int x, int y, int w, int h);

/* Removes an overlay and releases its surface/visual. */
void UIWebViewDComp_RemoveOverlay(UIWebViewDComp* dc, int handle);

/* ----------------------------------------------------------------- */
/* WebView2 environment-options helper. C-friendly factory that      */
/* returns an ICoreWebView2EnvironmentOptions* (as IUnknown*) with   */
/* the given additionalBrowserArguments string applied. The caller   */
/* releases it after passing it to CreateCoreWebView2EnvironmentWith- */
/* Options.                                                           */
/* ----------------------------------------------------------------- */
void* UIWebViewOptions_Create(const char* additionalArgsUtf8);
void  UIWebViewOptions_Release(void* options);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* UIKIT_WEBVIEW_DCOMP_H */
