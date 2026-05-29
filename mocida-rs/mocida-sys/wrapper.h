/*
 * mocida-sys: bindgen entry point.
 *
 * bindgen parses this file (with -I <MOCIDA_INCLUDE_DIR>) and emits one
 * Rust declaration per C symbol reachable from it.
 *
 * The mocida headers transitively pull in <SDL3/SDL.h>, <SDL3_ttf/...>,
 * and a couple of Win32 headers. To stay buildable without SDL on PATH,
 * the build.rs `BindgenStubs` pass supplies tiny opaque shims for the
 * SDL types used in the public API (SDL_Surface, SDL_Texture,
 * SDL_Window, SDL_Scancode, Uint16, Uint64). Enable the `sdl3-headers`
 * feature on mocida-sys when you DO have SDL3 on the include path and
 * want bindgen to consume the real definitions.
 */

#ifndef MOCIDA_SYS_WRAPPER_H
#define MOCIDA_SYS_WRAPPER_H

#include <uikit/app.h>
#include <uikit/event.h>
#include <uikit/reactive.h>
#include <uikit/bind.h>
#include <uikit/arena.h>
#include <uikit/asset.h>
#include <uikit/walker.h>
#ifdef _WIN32
#include <uikit/webview_dcomp.h>
#endif

#endif /* MOCIDA_SYS_WRAPPER_H */
