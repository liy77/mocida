#ifndef UIKIT_OVERLAY_H
#define UIKIT_OVERLAY_H

/**
 * Mocida's debug overlay — draws on top of the regular frame, in the
 * same renderer, after every UIWindow_Render. Cheap and self-contained:
 * uses SDL_RenderDebugText so it doesn't need a font asset.
 *
 * Opt-in: the overlay is OFF until the application enables it with
 * UIDebugOverlay_SetEnabled(1). While disabled it costs nothing and the
 * hotkeys below are ignored (the keys pass through to your widgets).
 * This works in any build (Debug or Release) — it is controlled at
 * runtime, not by a compile flag.
 *
 *   UIDebugOverlay_SetEnabled(1);                 // turn the overlay on
 *   UIDebugOverlay_SetFlags(UI_OVERLAY_STATS);    // pick what to show
 *
 * Once enabled, the UIApp event loop routes these hotkeys through
 * UIDebugOverlay_HandleScancode:
 *
 *   F9   toggle bounds outline
 *   F10  toggle stats HUD
 *   F8   toggle depth heatmap
 *   F12  toggle ALL on, second press clears
 *
 * (F11 is intentionally NOT used — it is the conventional fullscreen
 * key.) You can also drive flags programmatically via SetFlags /
 * ToggleFlag.
 */

#include <SDL3/SDL_stdinc.h>
#include <uikit/debug.h>
#include <uikit/window.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_OVERLAY_NONE     = 0,
    UI_OVERLAY_BOUNDS   = 1 << 0,  /**< colored outlines around every widget */
    UI_OVERLAY_STATS    = 1 << 1,  /**< FPS / phase-time / memory HUD       */
    UI_OVERLAY_HEATMAP  = 1 << 2,  /**< tints widgets by depth (overdraw proxy) */
    UI_OVERLAY_IDS      = 1 << 3,  /**< prints widget IDs next to their rect */
    UI_OVERLAY_ALL      = 0xFFFFu
} UIOverlayFlag;

/**
 * Master on/off switch (default OFF). The overlay only draws and only
 * reacts to its hotkeys while enabled — call this from your app to opt
 * in. Independent of the build type.
 */
void UIDebugOverlay_SetEnabled(int enabled);
int  UIDebugOverlay_IsEnabled(void);

void     UIDebugOverlay_SetFlags(unsigned flags);
unsigned UIDebugOverlay_GetFlags(void);
void     UIDebugOverlay_ToggleFlag(UIOverlayFlag flag);

/**
 * Pass an SDL_Scancode here from your event loop (UIApp_Run already
 * does it). Returns 1 if the key matched an overlay binding so callers
 * can swallow it.
 */
int UIDebugOverlay_HandleScancode(int scancode);

/* ---- Internal — called by UIWindow_Render. Safe to call always; it
 *      is a no-op when no flag is set. ---- */
void UIDebugOverlay_Draw(UIWindow* window);

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_OVERLAY_H */
