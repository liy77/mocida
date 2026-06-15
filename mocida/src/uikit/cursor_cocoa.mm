// uikit/cursor_cocoa.mm — macOS-specific cursor update.
//
// The shared cursor.c uses SDL_SetCursor, which on macOS calls
// [NSCursor set] internally. AppKit only refreshes the visible
// cursor when the next mouseMoved: event arrives, so the first
// application of a new cursor right after a hot-region change
// (e.g. moving the mouse over a Button that has `cursor: pointer`)
// appears to do nothing — the user sees the cursor stay as the
// previous shape until they actually nudge the mouse. On Windows
// the OS polls the cursor position, so it updates immediately.
//
// Fix: after SDL_SetCursor, also call [NSCursor set] directly
// with the matching NSCursor. AppKit refreshes the visible cursor
// synchronously on this code path, regardless of the next mouse
// event. The shape picked is the AppKit-side equivalent of the
// SDL cursor kind we're trying to install.

#import <TargetConditionals.h>
#if defined(__APPLE__) && TARGET_OS_OSX

#import <Cocoa/Cocoa.h>

#import <uikit/cursor.h>
#import <SDL3/SDL.h>

// Map a UICursor to the matching NSCursor. `arrow` / `IBeam` / etc.
// are the AppKit built-ins that the SDL_SYSTEM_CURSOR_* enums map
// to internally; we ask AppKit for the same shape so the visible
// cursor matches what SDL intends.
static NSCursor* NSCursorForKind(UICursor kind) {
    switch (kind) {
        case UI_CURSOR_POINTER:     return [NSCursor pointingHandCursor];
        case UI_CURSOR_TEXT:        return [NSCursor IBeamCursor];
        case UI_CURSOR_CROSSHAIR:   return [NSCursor crosshairCursor];
        case UI_CURSOR_MOVE:        return [NSCursor openHandCursor];
        case UI_CURSOR_NOT_ALLOWED: return [NSCursor operationNotAllowedCursor];
        case UI_CURSOR_WAIT:        return [NSCursor disappearingItemCursor];
        case UI_CURSOR_PROGRESS:    return [NSCursor closedHandCursor];
        case UI_CURSOR_EW_RESIZE:   return [NSCursor resizeLeftRightCursor];
        case UI_CURSOR_NS_RESIZE:   return [NSCursor resizeUpDownCursor];
        case UI_CURSOR_NWSE_RESIZE: return [NSCursor resizeUpDownCursor];
        case UI_CURSOR_NESW_RESIZE: return [NSCursor resizeUpDownCursor];
        case UI_CURSOR_DEFAULT:
        default:                    return [NSCursor arrowCursor];
    }
}

// Called from the shared UICursor_Apply via a weak hook (see
// cursor.c's UICursor__hook_apply_done) so the shared platform-
// neutral file stays C-only. Pushing the matching NSCursor to
// AppKit refreshes the visible cursor immediately, fixing the
// "stuck cursor on first hover" bug.
extern "C" void ui_cursor__hook_apply_done(UICursor kind) {
    NSCursor* ns = NSCursorForKind(kind);
    if (ns) [ns set];
}

#endif // __APPLE__ && TARGET_OS_OSX
