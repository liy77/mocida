#ifndef UIKIT_OVERLAY_H
#define UIKIT_OVERLAY_H

/**
 * Mocida's debug overlay — draws on top of the regular frame, in the
 * same renderer, after every UIWindow_Render. Cheap and self-contained:
 * uses SDL_RenderDebugText so it doesn't need a font asset, and only
 * runs when MOCIDA_DEBUG is set and at least one flag is on. Anything
 * else costs nothing.
 *
 * Default hotkeys (UIDebugOverlay_HandleScancode picks them up; the
 * UIApp event loop already calls into it):
 *
 *   F9   toggle bounds outline
 *   F10  toggle stats HUD
 *   F11  toggle depth heatmap
 *   F12  toggle ALL on, second press clears
 *
 * You can also set flags programmatically via SetFlags / ToggleFlag.
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
