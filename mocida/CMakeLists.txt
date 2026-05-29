cmake_minimum_required(VERSION 3.11)

# OBJCXX must be a project() language (not enable_language'd mid-configure,
# which fails to load the OBJCXX archive/compile rules under CMake 4 with a
# 3.11 minimum) so the macOS WKWebView backend (webview_cocoa.mm) compiles.
# APPLE isn't known until project() runs; CMAKE_HOST_APPLE is set earlier,
# so it drives the language list. Non-Apple hosts have no ObjC compiler and
# must NOT request OBJCXX.
if(CMAKE_HOST_APPLE)
    project(mocida C CXX OBJCXX)
else()
    project(mocida C CXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release optimisations. We APPEND to CMake's defaults rather than
# overwrite them — overwriting strips out the runtime-library flags
# (/MD, --dependent-lib=msvcrt, etc.) that CMake injects per
# compiler, leading to "undefined symbol: memcpy / _DllMainCRTStartup"
# at link time. The optimisations we add are toolchain-specific:
#   - MSVC / clang-cl: /GL (LTCG), /Gw /Gy /GF (data/section dedup),
#     /LTCG /OPT:REF /OPT:ICF on the linker.
#   - Plain clang (GCC-CLI mode): the default -O3 -DNDEBUG is plenty;
#     don't append slash-flags clang would reject.
if(MSVC)
    string(APPEND CMAKE_C_FLAGS_RELEASE          " /GL /Gw /Gy /GF")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE        " /GL /Gw /Gy /GF")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /LTCG /OPT:REF /OPT:ICF")
endif()

# GCC / Clang Release: CMake's default is just -O3 -DNDEBUG. We add LTO
# and host-CPU codegen so the render hot path (CPU-side rasterization,
# strcmp dispatch on widget types, the per-pixel coverage loops) gets
# the same level of treatment Windows already gets via /GL /LTCG.
#
# -march=native: produces a non-portable binary tuned for the build
#   host's CPU. Fine for a local dev / WSL2 measurement loop; if you
#   later ship Linux binaries to other machines, drop this or set
#   -march=x86-64-v3 for a broadly compatible SIMD floor.
# -fno-plt: removes a layer of indirection in calls to shared libs
#   (SDL3, libcurl). Tiny but free.
# -fno-semantic-interposition: lets the compiler inline across the
#   mocida shared lib boundary on Linux ELF (where the default is to
#   leave a slot for LD_PRELOAD-style overrides). Safe — we don't rely
#   on interposition.
# -funroll-loops: GCC defaults this off even at -O3 (Clang has its own
#   policy); the render-batch vertex builder + analytic-coverage circle
#   rasterizer are small fixed-trip loops that benefit measurably.
# -ffunction-sections / -fdata-sections + linker --gc-sections: strip
#   unreachable static helpers from the final binary. Slightly smaller
#   image, slightly better i-cache locality on the hot path.
# -fmerge-all-constants: aggressively pool identical literals/constants
#   across TUs. Helps the UIWidget_TypeIs pointer-compare fast path —
#   the default -fmerge-constants only merges within a TU.
if(NOT MSVC AND (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"))
    # Flags portable across Linux/macOS/Windows-clang.
    string(APPEND CMAKE_C_FLAGS_RELEASE   " -flto -march=native -funroll-loops -fmerge-all-constants")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -flto -march=native -funroll-loops -fmerge-all-constants")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE    " -flto")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " -flto")

    # Linux/macOS-only: -fno-plt and -fno-semantic-interposition affect
    # ELF PLT and ELF/Mach-O symbol interposition — both no-ops (and
    # warnings) on Windows where clang targets the MS-COFF + lld-link
    # toolchain. --gc-sections is GNU-ld / lld-elf only; lld-link
    # ignores it with a warning. Splitting these out avoids the noise
    # on `build.bat --release`.
    if(NOT WIN32)
        string(APPEND CMAKE_C_FLAGS_RELEASE   " -fno-plt -fno-semantic-interposition -ffunction-sections -fdata-sections")
        string(APPEND CMAKE_CXX_FLAGS_RELEASE " -fno-plt -fno-semantic-interposition -ffunction-sections -fdata-sections")
        string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE    " -Wl,--gc-sections")
        string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " -Wl,--gc-sections")
    endif()
endif()

# Default to a Debug build if the user didn't pick one — keeps the
# UI_TRACE/UI_DEBUG macros (gated behind NDEBUG in debug.h) live.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# ----------------------------------------------------------------------
# Vendored dependencies. The flags below MUST be set before
# add_subdirectory so SDL_image / SDL_ttf use their bundled libs
# (libpng, freetype, etc.) instead of looking for them in the system.
#
# When mocida is a static archive we also build SDL3 / SDL_image /
# SDL_ttf as STATIC libs so the SDL object code is baked into the
# final exe and there is no SDL3*.dll alongside it. Shared mocida
# keeps the conventional DLL flavour.
#
# The dance with the SDL3::SDL3 alias below is required because
# SDL_image / SDL_ttf consume the SDL3 in-tree target by name —
# without the alias, they fall through to find_package(SDL3) which
# (correctly) reports SDL3 is not installed system-wide.
# ----------------------------------------------------------------------
if(NOT DEFINED MOCIDA_BUILD_SHARED)
    set(MOCIDA_BUILD_SHARED OFF)
endif()
if(MOCIDA_BUILD_SHARED)
    set(_M_SDL_SHARED ON)
    set(_M_SDL_STATIC OFF)
else()
    set(_M_SDL_SHARED OFF)
    set(_M_SDL_STATIC ON)
endif()

set(SDL_SHARED ${_M_SDL_SHARED} CACHE BOOL "" FORCE)
set(SDL_STATIC ${_M_SDL_STATIC} CACHE BOOL "" FORCE)
# BUILD_SHARED_LIBS is the toggle SDL_image / SDL_ttf inspect when
# deciding whether to register their `*-shared` or `*-static` targets.
set(BUILD_SHARED_LIBS ${_M_SDL_SHARED} CACHE BOOL "" FORCE)

set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)

# ----------------------------------------------------------------------
# Local SDL3 patch: SDL_tray.c global-symbol fix.
#
# Upstream SDL3 (libsdl-org/SDL, every commit through 877399b at least)
# declares two GLib function pointers in src/tray/unix/SDL_tray.c
# WITHOUT the `static` keyword:
#
#     gpointer (*g_object_ref_sink)(gpointer object);
#     gpointer (*g_object_ref)(gpointer object);
#
# All other ~30 similar lookup pointers in the same file ARE `static`.
# These two are a long-standing oversight (no obvious reason to expose
# them as ELF globals — they're written once via dlsym at init and read
# from this file only).
#
# The non-static names collide with libgobject's real functions of the
# same name. When SDL3 is statically linked into the same binary as
# libgobject (which happens any time mocida pulls in libgstreamer for
# UIVideo or libwebkit2gtk for UIWebView on Linux), the dynamic linker
# resolves every libgobject `g_object_ref_sink` reference to SDL3's
# uninitialised BSS pointer. The first call segfaults inside
# `gst_init` → `g_option_context_parse` → SDL's NULL.
#
# setup.ps1 (re-)clones SDL3 from the upstream commit each time, so a
# manual local edit of SDL_tray.c does not survive a fresh checkout.
# We patch the file at configure time instead — idempotent, runs once
# per configure, no-op when the file is already patched (either by a
# previous configure, by upstream eventually fixing it, or by hand).
# ----------------------------------------------------------------------
set(_sdl_tray_unix "${CMAKE_CURRENT_SOURCE_DIR}/SDL/src/tray/unix/SDL_tray.c")
if(EXISTS "${_sdl_tray_unix}")
    file(READ "${_sdl_tray_unix}" _sdltray_content)
    if(_sdltray_content MATCHES "\ngpointer \\(\\*g_object_ref_sink\\)")
        string(REPLACE
            "\ngpointer (*g_object_ref_sink)(gpointer object);"
            "\nstatic gpointer (*g_object_ref_sink)(gpointer object);"
            _sdltray_content "${_sdltray_content}")
        string(REPLACE
            "\ngpointer (*g_object_ref)(gpointer object);"
            "\nstatic gpointer (*g_object_ref)(gpointer object);"
            _sdltray_content "${_sdltray_content}")
        file(WRITE "${_sdl_tray_unix}" "${_sdltray_content}")
        message(NOTICE "Mocida: patched SDL/src/tray/unix/SDL_tray.c (static-symbol fix for libgobject co-link — see CMakeLists.txt comment)")
    endif()
endif()

add_subdirectory(SDL)

# When SDL was built static-only, expose SDL3::SDL3 as an alias for
# SDL3-static so SDL_image / SDL_ttf's lookup of the SDL3 target
# inside the same tree succeeds (otherwise they fall through to
# find_package(SDL3) which can't see in-tree targets).
if(NOT TARGET SDL3::SDL3 AND TARGET SDL3-static)
    add_library(SDL3::SDL3 ALIAS SDL3-static)
endif()

add_subdirectory(SDL_image)
add_subdirectory(SDL_ttf)

find_package(CURL REQUIRED)

# WebView2 (Microsoft Edge Chromium) - only on Windows. Provides the
# native browser surface used by UIWebView. The runtime DLL ships next
# to the exe via target_link_libraries below; the Edge runtime itself
# is already installed on every Windows 10+ machine.
if(WIN32)
    find_package(unofficial-webview2 CONFIG REQUIRED)
endif()

# ----------------------------------------------------------------------
# Optional: link Microsoft mimalloc as the allocator. When the vendored
# mimalloc/ directory exists (cloned by setup.ps1), build the static
# library and arrange for malloc/free in our code to route through
# mi_malloc/mi_free via the mocida_alloc.h hook.
#
# To opt out of mimalloc:
#     cmake -DMOCIDA_USE_MIMALLOC=OFF ..
# ----------------------------------------------------------------------
option(MOCIDA_USE_MIMALLOC "Use Microsoft mimalloc for malloc/free" ON)
set(MOCIDA_MIMALLOC_AVAILABLE OFF)

if(MOCIDA_USE_MIMALLOC AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mimalloc/CMakeLists.txt)
    set(MI_OVERRIDE     ON  CACHE BOOL "" FORCE)
    set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(MI_INSTALL_TOPLEVEL OFF CACHE BOOL "" FORCE)
    add_subdirectory(mimalloc)
    set(MOCIDA_MIMALLOC_AVAILABLE ON)
    message(STATUS "Mocida: using mimalloc as the allocator")
endif()

# ----------------------------------------------------------------------
# Build flavour switches.
#
#   MOCIDA_BUILD_SHARED  : OFF -> `mocida` is a static lib (.lib / .a).
#                         ON  -> `mocida` is a shared lib (.dll + import
#                                .lib on Windows, .so / .dylib elsewhere).
#                                On Windows we use WINDOWS_EXPORT_ALL_SYMBOLS
#                                so every public function and global is
#                                exported automatically, no annotation
#                                needed in the source.
#   MOCIDA_BUILD_TESTS   : OFF -> tests/test_*.c are excluded from the
#                                default build. Opt in explicitly when
#                                you actually want to compile them.
#   MOCIDA_BUILD_DEMO    : ON  -> the showcase exe (src/main.c) is built.
# ----------------------------------------------------------------------
option(MOCIDA_BUILD_SHARED "Build mocida as a shared library (DLL) instead of a static archive" OFF)
option(MOCIDA_BUILD_TESTS  "Build the tests/test_*.c programs"                                  OFF)
option(MOCIDA_BUILD_DEMO   "Build the main demo executable (src/main.c)"                       ON)

# ----------------------------------------------------------------------
# Library `mocida`: every .c under src/uikit/.
# Executables (demo + tests) link against it.
# ----------------------------------------------------------------------
file(GLOB_RECURSE MOCIDA_SOURCES   "${CMAKE_CURRENT_SOURCE_DIR}/src/uikit/*.c")
file(GLOB_RECURSE MOCIDA_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/uikit/*.cpp")

# macOS: the WKWebView backend (webview_cocoa.mm) is Objective-C++. The
# globs above only catch .c/.cpp, so it's added explicitly. OBJCXX must
# be enabled as a project language before the .mm can be compiled.
set(MOCIDA_SOURCES_OBJCXX "")
if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(MOCIDA_SOURCES_OBJCXX
        "${CMAKE_CURRENT_SOURCE_DIR}/src/uikit/webview_cocoa.mm"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/uikit/video_avf.mm")
endif()

# winmain.c is the executable-side entry-point shim that delegates
# WinMain → main. It has no place inside the library itself (which is
# a DLL when MOCIDA_BUILD_SHARED=ON, and DLLs don't have an exe entry
# point — including it makes the linker complain about an undefined
# `main` reference from WinMain). Keep it in a separate variable that
# the executable helpers add into their own source list.
set(MOCIDA_WINMAIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/uikit/winmain.c")
list(FILTER MOCIDA_SOURCES EXCLUDE REGEX "/winmain\\.c$")
if(MOCIDA_BUILD_SHARED)
    add_library(mocida SHARED ${MOCIDA_SOURCES} ${MOCIDA_SOURCES_CXX} ${MOCIDA_SOURCES_OBJCXX})
    set_target_properties(mocida PROPERTIES
        # Auto-emit a .def listing every function + global variable;
        # callers don't need __declspec(dllexport) in the headers.
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        # Keep the shared output named "mocida.dll" / "libmocida.so" —
        # without these CMake on some platforms prepends/appends extras.
        OUTPUT_NAME mocida
    )
    message(STATUS "Mocida: building as shared library (DLL)")
else()
    add_library(mocida STATIC ${MOCIDA_SOURCES} ${MOCIDA_SOURCES_CXX} ${MOCIDA_SOURCES_OBJCXX})
    message(STATUS "Mocida: building as static library")
endif()

target_include_directories(mocida PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/headers
)

# macOS WKWebView backend: compile in the Objective-C++ source, link the
# WebKit + Cocoa frameworks, and flip MOCIDA_HAS_WKWEBVIEW so webview.c
# yields its symbols to webview_cocoa.mm instead of the no-backend stub.
# The .mm opts out of the C precompiled header below (PCH is per-language;
# the C stdlib PCH is not valid for an Objective-C++ translation unit).
if(APPLE)
    target_compile_definitions(mocida PRIVATE
        MOCIDA_HAS_WKWEBVIEW=1
        MOCIDA_HAS_AVFOUNDATION=1)
    target_link_libraries(mocida PUBLIC
        "-framework WebKit" "-framework Cocoa"
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")
    set_source_files_properties(${MOCIDA_SOURCES_OBJCXX} PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON)
    message(STATUS "Mocida: macOS UIWebView backend = WKWebView (webview_cocoa.mm)")
    message(STATUS "Mocida: macOS UIVideo  backend = AVFoundation (video_avf.mm)")
endif()

# ----------------------------------------------------------------------
# Precompiled headers. SDL3/SDL.h alone pulls in ~30k lines of system
# headers per TU; pre-parsing it once and reusing it cuts a full mocida
# rebuild by roughly a third. We only PCH headers that change very
# rarely (SDL public surface + C stdlib) — anything from src/headers/
# would invalidate the PCH on every internal edit and make things
# slower, not faster.
#
# Opt out with -DMOCIDA_USE_PCH=OFF if a TU breaks due to ordering
# (e.g. a file that needs WIN32_LEAN_AND_MEAN defined before windows.h).
# ----------------------------------------------------------------------
option(MOCIDA_USE_PCH "Precompile SDL3/SDL.h + C stdlib for faster builds" ON)
if(MOCIDA_USE_PCH)
    target_precompile_headers(mocida PRIVATE
        <SDL3/SDL.h>
        <stdio.h>
        <stdlib.h>
        <string.h>
        <math.h>
        <stdint.h>
        <stdbool.h>
    )
endif()

# Pick the SDL target flavour that matches mocida's build mode:
#   shared mocida → link the SDL3::SDL3 / SDL3_image / SDL3_ttf shared
#                   aliases (DLLs ship alongside the exe).
#   static mocida → link the *-static targets so SDL object code is
#                   baked into the final exe and there's no SDL3.dll.
# Helpers fall back gracefully if a flavour isn't registered (e.g. an
# SDL_image build that only produces the shared target).
function(_mocida_pick_sdl_target out_var shared_name static_name)
    if(MOCIDA_BUILD_SHARED)
        if(TARGET ${shared_name})
            set(${out_var} ${shared_name} PARENT_SCOPE)
        else()
            set(${out_var} ${static_name} PARENT_SCOPE)
        endif()
    else()
        if(TARGET ${static_name})
            set(${out_var} ${static_name} PARENT_SCOPE)
        else()
            set(${out_var} ${shared_name} PARENT_SCOPE)
        endif()
    endif()
endfunction()

_mocida_pick_sdl_target(_M_SDL3      SDL3::SDL3            SDL3::SDL3-static)
_mocida_pick_sdl_target(_M_SDL3_IMG  SDL3_image::SDL3_image SDL3_image::SDL3_image-static)
_mocida_pick_sdl_target(_M_SDL3_TTF  SDL3_ttf::SDL3_ttf    SDL3_ttf::SDL3_ttf-static)

message(STATUS "Mocida: linking SDL3      via ${_M_SDL3}")
message(STATUS "Mocida: linking SDL3_image via ${_M_SDL3_IMG}")
message(STATUS "Mocida: linking SDL3_ttf  via ${_M_SDL3_TTF}")

target_link_libraries(mocida PUBLIC
    ${_M_SDL3}
    ${_M_SDL3_IMG}
    ${_M_SDL3_TTF}
    CURL::libcurl
)

# Windows Media Foundation (UIVideo backend) + WebView2 (UIWebView).
# All MF libs ship with the Windows SDK. WebView2 has two delivery
# channels we know how to consume:
#
#   - vcpkg's `unofficial::webview2::webview2` target -> dynamic loader
#     (WebView2Loader.dll alongside the exe). Used when MOCIDA_BUILD_SHARED=ON
#     or the static-loader cache isn't populated.
#   - Microsoft's NuGet package, fetched once by build.bat into
#     `.webview2/`. Provides WebView2LoaderStatic.lib so a static mocida
#     can bake the loader into the final exe (no DLL alongside).
#
# MOCIDA_WEBVIEW2_NUGET_DIR is passed by build.bat after the one-time
# download succeeds. We auto-select the static loader for static builds
# whenever the static lib is on disk; everything else falls back to the
# dynamic loader.
set(MOCIDA_WEBVIEW2_STATIC OFF CACHE INTERNAL "")
set(_MOCIDA_WEBVIEW2_USE_STATIC FALSE)
if(WIN32 AND NOT MOCIDA_BUILD_SHARED AND DEFINED MOCIDA_WEBVIEW2_NUGET_DIR)
    set(_wv2_static_lib "${MOCIDA_WEBVIEW2_NUGET_DIR}/build/native/x64/WebView2LoaderStatic.lib")
    if(EXISTS "${_wv2_static_lib}")
        set(_MOCIDA_WEBVIEW2_USE_STATIC TRUE)
        set(MOCIDA_WEBVIEW2_STATIC ON CACHE INTERNAL "")
    endif()
endif()

if(WIN32)
    target_link_libraries(mocida PUBLIC
        mfplat
        mfreadwrite
        mfuuid
        ole32
        propsys
        ws2_32
        dbghelp
        dcomp           # ICoreWebView2CompositionController target
        d3d11           # D3D11 device backing D2D for overlays
        d2d1            # D2D rounded rect / drawing
        dwrite          # text in overlays
        dxgi            # surface allocation
        version         # WebView2LoaderStatic.lib calls GetFileVersionInfo*
        shlwapi         # path helpers WebView2LoaderStatic.lib needs
    )

    # WebView2 headers ALWAYS come from vcpkg (older API, what the
    # webview_dcomp.cpp was written against). The newer NuGet headers
    # ship breaking changes (kInternalChannel, CoreWebView2EnvironmentOptionsBaseClass,
    # ...) that don't compile our existing source. The loader ABI is
    # stable across versions, so linking the NuGet's static loader
    # binary against vcpkg's headers works.
    get_target_property(_wv2_vcpkg_includes unofficial::webview2::webview2 INTERFACE_INCLUDE_DIRECTORIES)
    if(_wv2_vcpkg_includes)
        target_include_directories(mocida PUBLIC ${_wv2_vcpkg_includes})
    endif()

    if(_MOCIDA_WEBVIEW2_USE_STATIC)
        # Replace vcpkg's WebView2Loader.dll.lib (import lib) with the
        # NuGet's WebView2LoaderStatic.lib — same exported symbols
        # (CreateCoreWebView2Environment*, GetAvailableCoreWebView2BrowserVersionString,
        # ...), no DLL needed at runtime.
        target_link_libraries(mocida PUBLIC "${_wv2_static_lib}")
        message(STATUS "Mocida: bundling WebView2LoaderStatic.lib (${_wv2_static_lib})")
    else()
        target_link_libraries(mocida PUBLIC unofficial::webview2::webview2)
        message(STATUS "Mocida: linking dynamic WebView2Loader (vcpkg)")
    endif()
endif()

# ----------------------------------------------------------------------
# Optional sanitizers (clang/gcc only). Enable with:
#   cmake -DMOCIDA_SANITIZE=address       # AddressSanitizer
#   cmake -DMOCIDA_SANITIZE=undefined     # UBSan
#   cmake -DMOCIDA_SANITIZE=address,undefined
# Catches a wide class of memory & UB bugs the crash handler can't see.
# Not for production binaries.
# ----------------------------------------------------------------------
set(MOCIDA_SANITIZE "" CACHE STRING "Comma-separated sanitizers to enable (e.g. address,undefined)")
if(MOCIDA_SANITIZE)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        message(STATUS "Mocida: enabling sanitizers: ${MOCIDA_SANITIZE}")
        target_compile_options(mocida PUBLIC -fsanitize=${MOCIDA_SANITIZE} -fno-omit-frame-pointer)
        target_link_options   (mocida PUBLIC -fsanitize=${MOCIDA_SANITIZE})
    else()
        message(WARNING "Mocida: MOCIDA_SANITIZE only supported under clang/gcc — ignored.")
    endif()
endif()

# Force the MOCIDA_DEBUG marker on for Debug/RelWithDebInfo builds, so
# UI_TRACE / UI_DEBUG / UI_TRACK_* stay live there. Release strips them.
target_compile_definitions(mocida PUBLIC
    $<$<CONFIG:Debug>:MOCIDA_DEBUG=1>
    $<$<CONFIG:RelWithDebInfo>:MOCIDA_DEBUG=1>
)

# Silence MSVC's _CRT_INSECURE_DEPRECATE warnings for fopen / strcpy /
# strdup / etc. The "secure" *_s alternatives are MSVC-only and not
# portable; standard C functions are fine for our use cases (lifetime
# and bound-checking is already handled at the call sites). Defined
# unconditionally because non-MSVC toolchains simply ignore the
# macros. PUBLIC so every test / demo linking against mocida inherits
# them.
target_compile_definitions(mocida PUBLIC
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
)

# `_strdup` is the MSVC spelling of POSIX's `strdup`. The codebase uses
# `_strdup` everywhere so the Windows build matches the rest of the
# Microsoft CRT naming. On non-Windows, alias it back to the standard
# name so the same source compiles unmodified. Public so tests/demos
# inherit the alias.
if(NOT WIN32)
    target_compile_definitions(mocida PUBLIC _strdup=strdup)
    # strdup + a few other things we rely on are gated behind glibc
    # feature-test macros. _GNU_SOURCE is the catch-all on Linux that
    # exposes everything POSIX, XSI, BSD, and GNU.
    target_compile_definitions(mocida PUBLIC _GNU_SOURCE)
    # mimalloc-override.h re-defines _strdup -> mi_strdup, which (correctly)
    # supersedes the shim above. Suppress the cosmetic redefinition warning.
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(mocida PUBLIC -Wno-macro-redefined)
    endif()
endif()

# ----------------------------------------------------------------------
# Linux video backend: GStreamer for the UIVideo widget.
#
# The Windows build uses Media Foundation (linked unconditionally
# above via `mfplat`/`mfreadwrite`/etc). On Linux we use GStreamer's
# `playbin` because:
#   - it covers MP4 / MKV / WebM / AVI / etc. via the standard plugin
#     set (gst-plugins-good / bad / ugly / libav).
#   - it routes to NVDEC / VAAPI / V4L2 hw decoders when available
#     (matching the hardware path Media Foundation takes on Windows).
#   - it gives us a non-FFmpeg architecture as requested.
#
# Detection is best-effort: when GStreamer dev packages are missing
# we leave MOCIDA_HAS_GSTREAMER undefined and video.c falls back to
# the stub branch. Build still succeeds; UIVideo_Create just returns
# NULL on Linux. Install with:
#   sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
#                    gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
#                    gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
#                    gstreamer1.0-libav
# The RUNTIME `gstreamer1.0-plugins-base` package is critical — it
# provides the `playbin` and `appsink` elements we use. The -dev
# package only ships headers; without the runtime plugin,
# gst_element_factory_make returns NULL and UIVideo_Create bails.
# ----------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GSTREAMER QUIET
            gstreamer-1.0
            gstreamer-app-1.0
            gstreamer-audio-1.0
            gstreamer-video-1.0)
        if(GSTREAMER_FOUND)
            message(STATUS "Mocida: linking GStreamer ${GSTREAMER_VERSION} (UIVideo backend)")
            target_include_directories(mocida PRIVATE ${GSTREAMER_INCLUDE_DIRS})
            target_link_libraries     (mocida PRIVATE ${GSTREAMER_LIBRARIES})
            target_compile_definitions(mocida PRIVATE MOCIDA_HAS_GSTREAMER=1)
        else()
            message(STATUS "Mocida: GStreamer not found — UIVideo will be a stub on Linux. "
                           "Install libgstreamer1.0-dev + plugins to enable.")
        endif()
    endif()
endif()

# ----------------------------------------------------------------------
# Linux video / webview backends.
#
# The webview backend on Linux uses WebKitGTK (the same engine Epiphany,
# Geary and GNOME Web are built on). It pulls in GTK3 + WebKit2 +
# transitive GLib / Cairo / SoupHTTP. Detection is best-effort, identical
# pattern to GStreamer above:
#
#   - found    : MOCIDA_HAS_WEBKITGTK=1, real backend compiled
#   - missing  : UIWebView_Create returns NULL on Linux, app still builds
#
# We try webkit2gtk-4.1 first (Ubuntu 24.04+, Fedora 40+, Debian trixie),
# then fall back to webkit2gtk-4.0 (Ubuntu 22.04 LTS and earlier). Both
# expose the same C API in our usage.
#
# To install on Ubuntu/Debian:
#   sudo apt install libwebkit2gtk-4.1-dev      # preferred (newer)
#   sudo apt install libwebkit2gtk-4.0-dev      # LTS fallback
# To install on Fedora:
#   sudo dnf install webkit2gtk4.1-devel
# To install on Arch:
#   sudo pacman -S webkit2gtk-4.1
# ----------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(WEBKITGTK QUIET webkit2gtk-4.1)
        if(NOT WEBKITGTK_FOUND)
            pkg_check_modules(WEBKITGTK QUIET webkit2gtk-4.0)
        endif()
        if(WEBKITGTK_FOUND)
            message(STATUS "Mocida: linking WebKitGTK ${WEBKITGTK_VERSION} (UIWebView backend)")
            target_include_directories(mocida PRIVATE ${WEBKITGTK_INCLUDE_DIRS})
            target_link_libraries     (mocida PRIVATE ${WEBKITGTK_LIBRARIES})
            target_compile_definitions(mocida PRIVATE MOCIDA_HAS_WEBKITGTK=1)
        else()
            message(STATUS "Mocida: WebKitGTK not found — UIWebView will be a stub on Linux. "
                           "Install libwebkit2gtk-4.1-dev (or 4.0) to enable.")
        endif()
    endif()
endif()

# Wire mimalloc into the lib's public link interface so that:
#  - mocida.lib (and every consumer like demo / tests) get mi_malloc
#    linked in.
#  - MOCIDA_USE_MIMALLOC is defined for any TU that includes mocida
#    headers, so widget.h's bottom #include of mocida_alloc.h kicks in.
if(MOCIDA_MIMALLOC_AVAILABLE)
    target_link_libraries(mocida PUBLIC mimalloc-static)
    target_compile_definitions(mocida PUBLIC MOCIDA_USE_MIMALLOC=1)
endif()

# ----------------------------------------------------------------------
# Helper: declares an executable that links against `mocida` and, on
# Windows, copies the SDL DLLs next to the output binary so it can run.
# ----------------------------------------------------------------------
function(mocida_add_executable target_name source_file)
    # WinMain shim is compiled into every Mocida exe so the GUI
    # subsystem (WIN32_EXECUTABLE TRUE) routes WinMain → user `main`.
    add_executable(${target_name} ${source_file} ${MOCIDA_WINMAIN_SRC})
    target_link_libraries(${target_name} PRIVATE mocida)

    # Linux: hide symbols of every static archive we link in (SDL3,
    # SDL3_image, SDL3_ttf, bundled harfbuzz / freetype / etc., mimalloc)
    # from the exe's exported dynamic symbol table. Otherwise the dynamic
    # linker resolves system libs' calls (libpango / libcairo / libgtk
    # via WebKitGTK, all of which pull in the system harfbuzz) back to
    # OUR statically-bundled copies — different structs, different
    # versions, garbage memory access, SIGSEGV inside hb_face_t::
    # reference_table the first time pango asks for a glyph.
    # `--exclude-libs,ALL` is the GNU-ld / lld-elf flag that does this;
    # MSVC's linker has different semantics and doesn't need it.
    if(UNIX AND NOT APPLE)
        target_link_options(${target_name} PRIVATE
            "LINKER:--exclude-libs,ALL")
    endif()

    if(WIN32)
        # Link as the Windows (GUI) subsystem so no console window
        # pops up automatically. In Debug builds the Mocida runtime
        # calls AllocConsole() from UIApp_Create when logs are useful;
        # the MOCIDA_NO_CONSOLE env var (or UIApp_HideConsole()) opts
        # out. In Release no console is ever attached.
        set_target_properties(${target_name} PROPERTIES
            WIN32_EXECUTABLE TRUE)

        # Only copy SDL3*.dll alongside the exe when those targets were
        # actually built as shared libraries. In the default static
        # build (MOCIDA_BUILD_SHARED=OFF) they are STATIC archives
        # linked directly into the exe and there is no DLL to copy —
        # any attempt would emit a CMake error about missing
        # TARGET_FILE for a static archive.
        foreach(dep SDL3 SDL3_ttf SDL3_image)
            if(TARGET ${dep}::${dep})
                get_target_property(_deptype ${dep}::${dep} TYPE)
                if(_deptype STREQUAL "SHARED_LIBRARY")
                    add_custom_command(TARGET ${target_name} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${dep}::${dep}>
                            $<TARGET_FILE_DIR:${target_name}>
                    )
                endif()
            endif()
        endforeach()
        # WebView2Loader.dll comes from vcpkg's x64-windows triplet
        # and only needs to ship alongside the exe when mocida is
        # linked against the dynamic loader. The static-loader path
        # (MOCIDA_WEBVIEW2_STATIC=ON, see the WebView2 selector above)
        # bakes the loader into the binary so the copy is skipped.
        if(NOT MOCIDA_WEBVIEW2_STATIC AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/bin/WebView2Loader.dll")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/bin/WebView2Loader.dll"
                    $<TARGET_FILE_DIR:${target_name}>
            )
        endif()
        # When mocida itself is a shared library, ship mocida.dll next
        # to each executable so it loads without PATH gymnastics.
        if(MOCIDA_BUILD_SHARED)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:mocida>
                    $<TARGET_FILE_DIR:${target_name}>
            )
        endif()
    endif()
endfunction()

# ----------------------------------------------------------------------
# Copy assets/ next to the executables in the build directory.
# Together with UIAsset_LoadTexture/Surface (which also probes paths
# relative to SDL_GetBasePath), this guarantees that the tests find
# the assets no matter which CWD the .exe is launched from.
# ----------------------------------------------------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    add_custom_target(mocida_copy_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${CMAKE_CURRENT_BINARY_DIR}/assets
        COMMENT "Copying assets/ into the build directory"
    )
endif()

# ----------------------------------------------------------------------
# Main demo (the former main.c / uikit.exe). Gated by MOCIDA_BUILD_DEMO
# so library consumers can disable it (or build only the lib).
# ----------------------------------------------------------------------
if(MOCIDA_BUILD_DEMO)
    mocida_add_executable(demo src/main.c)
endif()

# ----------------------------------------------------------------------
# Test programs: one .exe per file in tests/test_*.c. Each is a focused
# visual scenario for a single feature. They are NOT built by default
# anymore (separate build flavour) — pass -DMOCIDA_BUILD_TESTS=ON or
# `build.bat --tests` to include them.
# ----------------------------------------------------------------------
if(MOCIDA_BUILD_TESTS)
    file(GLOB MOCIDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.c")
    foreach(test_src ${MOCIDA_TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        mocida_add_executable(${test_name} ${test_src})
    endforeach()
    message(STATUS "Mocida: building tests (${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.c)")
endif()

# ----------------------------------------------------------------------
# Optional GUI installer (installer/installer.c). The installer is a
# Mocida app that downloads the SDK (mocida.dll + headers + SDL DLLs)
# from a GitHub release at install time and publishes MOCIDA_INCLUDE_DIR
# / MOCIDA_LIB_DIR / PATH via the Windows registry.
#
# The installer is built against a private STATIC copy of mocida so the
# resulting .exe does NOT depend on mocida.dll at runtime — it can't,
# because what it ships *is* mocida.dll. It still needs SDL3*.dll +
# WebView2Loader.dll alongside it (these come from vcpkg / vendored
# SDL subdirs and are shared libraries by design).
# ----------------------------------------------------------------------
option(MOCIDA_BUILD_INSTALLER "Build the GUI installer (installer/installer.c)" OFF)
if(MOCIDA_BUILD_INSTALLER)
    # Parallel static lib reusing the same sources as the main `mocida`
    # target. Compiles the TU set twice when MOCIDA_BUILD_SHARED is also
    # ON, which is fine — the installer is built rarely.
    #
    # libcurl is intentionally NOT linked here: the installer talks to
    # GitHub via WinINet (a Windows system DLL that's always present),
    # so we don't drag libcurl.dll + zlib.dll + their shared deps next
    # to the installer.exe. Mocida's other libcurl users (if any) still
    # see CURL in the regular mocida target.
    add_library(mocida_static_installer STATIC
        ${MOCIDA_SOURCES} ${MOCIDA_SOURCES_CXX})
    target_include_directories(mocida_static_installer PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/headers)
    target_link_libraries(mocida_static_installer PUBLIC
        ${_M_SDL3}
        ${_M_SDL3_IMG}
        ${_M_SDL3_TTF})
    if(WIN32)
        target_link_libraries(mocida_static_installer PUBLIC
            mfplat mfreadwrite mfuuid ole32 propsys ws2_32 dbghelp
            dcomp d3d11 d2d1 dwrite dxgi
            version shlwapi)

        # Pull in the static WebView2 loader when build.bat staged the
        # NuGet (see MOCIDA_WEBVIEW2_NUGET_DIR up top). That replaces
        # the runtime WebView2Loader.dll dependency with a baked-in
        # static lib — no DLL shipped next to the installer.
        if(DEFINED MOCIDA_WEBVIEW2_NUGET_DIR)
            set(_wv2_static_lib_inst
                "${MOCIDA_WEBVIEW2_NUGET_DIR}/build/native/x64/WebView2LoaderStatic.lib")
            if(EXISTS "${_wv2_static_lib_inst}")
                target_link_libraries(mocida_static_installer PUBLIC
                    "${_wv2_static_lib_inst}")
                get_target_property(_wv2_inc_inst
                    unofficial::webview2::webview2 INTERFACE_INCLUDE_DIRECTORIES)
                if(_wv2_inc_inst)
                    target_include_directories(mocida_static_installer PUBLIC
                        ${_wv2_inc_inst})
                endif()
                message(STATUS "Installer: bundling WebView2LoaderStatic.lib")
            else()
                # Fall back to the dynamic loader. The installer will
                # then ship with WebView2Loader.dll alongside it.
                target_link_libraries(mocida_static_installer PUBLIC
                    unofficial::webview2::webview2)
                message(WARNING
                    "Installer: WebView2LoaderStatic.lib missing — installer will need WebView2Loader.dll next to the exe.")
            endif()
        else()
            target_link_libraries(mocida_static_installer PUBLIC
                unofficial::webview2::webview2)
        endif()
    endif()
    target_compile_definitions(mocida_static_installer PUBLIC
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS)
    target_compile_definitions(mocida_static_installer PUBLIC
        $<$<CONFIG:Debug>:MOCIDA_DEBUG=1>
        $<$<CONFIG:RelWithDebInfo>:MOCIDA_DEBUG=1>)
    if(MOCIDA_MIMALLOC_AVAILABLE)
        target_link_libraries(mocida_static_installer PUBLIC mimalloc-static)
        target_compile_definitions(mocida_static_installer PUBLIC
            MOCIDA_USE_MIMALLOC=1)
    endif()

    # ------------------------------------------------------------------
    # Embed assets/logo.svg directly into the installer binary so the
    # installer ships as a single self-contained .exe with no nearby
    # assets/ folder. Generated at configure time; re-cmake when the
    # SVG changes (declared via CMAKE_CONFIGURE_DEPENDS below).
    # ------------------------------------------------------------------
    set(_inst_assets_dir "${CMAKE_CURRENT_BINARY_DIR}/installer_generated")
    set(_inst_assets_h   "${_inst_assets_dir}/installer_assets.h")
    set(_inst_logo_src   "${CMAKE_CURRENT_SOURCE_DIR}/assets/logo.svg")
    file(MAKE_DIRECTORY "${_inst_assets_dir}")
    if(EXISTS "${_inst_logo_src}")
        file(READ "${_inst_logo_src}" _logo_hex HEX)
        string(LENGTH "${_logo_hex}" _logo_hex_len)
        math(EXPR _logo_bytes_count "${_logo_hex_len} / 2")
        # Convert "aabbcc..." -> "0xaa,0xbb,0xcc,..." and chunk into
        # 16-byte rows for a readable header.
        string(REGEX REPLACE "(..)" "0x\\1," _logo_bytes "${_logo_hex}")
        string(REGEX REPLACE "((0x..,){16})" "\\1\n    " _logo_bytes "${_logo_bytes}")
        file(WRITE "${_inst_assets_h}"
"// AUTO-GENERATED by CMake from assets/logo.svg — do not edit.
#ifndef INSTALLER_ASSETS_H
#define INSTALLER_ASSETS_H

#include <stddef.h>

static const unsigned char g_logo_svg[] = {
    ${_logo_bytes}
};
static const size_t g_logo_svg_len = ${_logo_bytes_count};

#endif
")
        # Tell CMake to re-configure if the SVG is touched so the byte
        # array stays in sync with the source asset.
        set_property(DIRECTORY APPEND PROPERTY
            CMAKE_CONFIGURE_DEPENDS "${_inst_logo_src}")
    else()
        message(WARNING "Installer: assets/logo.svg missing — embedding empty array.")
        file(WRITE "${_inst_assets_h}"
"// Generated stub: assets/logo.svg was missing at configure time.
#ifndef INSTALLER_ASSETS_H
#define INSTALLER_ASSETS_H
#include <stddef.h>
static const unsigned char g_logo_svg[1] = { 0 };
static const size_t g_logo_svg_len = 0;
#endif
")
    endif()

    # ------------------------------------------------------------------
    # Generate logo.ico from assets/logo.svg, embed it as a Windows
    # resource so Explorer / the taskbar paint the icon on the .exe
    # itself (visible BEFORE the program runs and SDL paints a window).
    #
    # Build pipeline:
    #   svg2ico (tools/svg2ico.c)  →  logo.ico
    #   logo.ico                   →  installer.rc  (1 ICON "...")
    #   installer.rc               →  linked into mocida_installer.exe
    # ------------------------------------------------------------------
    set(_inst_ico "${_inst_assets_dir}/logo.ico")
    set(_inst_rc  "${_inst_assets_dir}/installer.rc")

    if(WIN32 AND EXISTS "${_inst_logo_src}")
        add_executable(svg2ico tools/svg2ico.c)
        target_link_libraries(svg2ico PRIVATE
            ${_M_SDL3} ${_M_SDL3_IMG})

        # In MOCIDA_BUILD_SHARED=ON configurations SDL3 / SDL3_image
        # are DLLs — copy them next to svg2ico.exe so the custom
        # command can actually launch it (without DLLs alongside it
        # crashes with STATUS_DLL_NOT_FOUND / 0xC0000135).
        foreach(_dep SDL3 SDL3_ttf SDL3_image)
            if(TARGET ${_dep}::${_dep})
                get_target_property(_t ${_dep}::${_dep} TYPE)
                if(_t STREQUAL "SHARED_LIBRARY")
                    add_custom_command(TARGET svg2ico POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${_dep}::${_dep}>
                            $<TARGET_FILE_DIR:svg2ico>)
                endif()
            endif()
        endforeach()

        add_custom_command(
            OUTPUT "${_inst_ico}"
            COMMAND $<TARGET_FILE:svg2ico>
                    "${_inst_logo_src}"
                    "${_inst_ico}"
            DEPENDS svg2ico "${_inst_logo_src}"
            COMMENT "Rendering logo.svg → logo.ico (taskbar / explorer icon)"
            VERBATIM
        )

        # The .rc references logo.ico by absolute path. CMake's resource
        # compiler step doesn't auto-track that, so OBJECT_DEPENDS is
        # set below so an .ico rebuild forces an .rc recompile.
        file(WRITE "${_inst_rc}"
"// AUTO-GENERATED by CMake — do not edit.\n\
// Resource ID 1 makes this the default application icon shown by\n\
// Explorer / taskbar / Alt+Tab on the .exe itself.\n\
1 ICON \"${_inst_ico}\"\n")
        set_source_files_properties("${_inst_rc}" PROPERTIES
            OBJECT_DEPENDS "${_inst_ico}")

        set(_installer_resources "${_inst_rc}")
    else()
        set(_installer_resources "")
    endif()

    add_executable(mocida_installer
        installer/installer.c
        ${_installer_resources}
        ${MOCIDA_WINMAIN_SRC})
    target_link_libraries(mocida_installer PRIVATE mocida_static_installer)
    target_include_directories(mocida_installer PRIVATE "${_inst_assets_dir}")
    if(WIN32)
        # GUI subsystem — same as mocida_add_executable. Auto-console
        # in Debug, none in Release.
        set_target_properties(mocida_installer PROPERTIES
            WIN32_EXECUTABLE TRUE)
    endif()
    if(WIN32)
        # shlwapi:  StrStrIA (case-insensitive substring for PATH probing).
        # advapi32: registry APIs (HKLM/HKCU\\Environment).
        # user32:   SendMessageTimeoutA (WM_SETTINGCHANGE broadcast).
        # wininet:  the HTTP client used to fetch the SDK zip from
        #           GitHub. Statically linked (no DLL alongside the exe).
        target_link_libraries(mocida_installer PRIVATE
            shlwapi advapi32 user32 wininet)

        # Belt-and-suspenders: only copy SDL DLLs if those targets ended
        # up SHARED (they shouldn't in this configuration — SDL is
        # picked as -static via _M_SDL3 above — but guard anyway). When
        # SDL is static, this loop is a no-op and the installer is a
        # single .exe.
        foreach(dep SDL3 SDL3_ttf SDL3_image)
            if(TARGET ${dep}::${dep})
                get_target_property(_deptype ${dep}::${dep} TYPE)
                if(_deptype STREQUAL "SHARED_LIBRARY")
                    add_custom_command(TARGET mocida_installer POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${dep}::${dep}>
                            $<TARGET_FILE_DIR:mocida_installer>)
                endif()
            endif()
        endforeach()
        # Only ship WebView2Loader.dll when the static loader wasn't
        # available (fallback path warned about above).
        if(NOT EXISTS "${MOCIDA_WEBVIEW2_NUGET_DIR}/build/native/x64/WebView2LoaderStatic.lib" AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/bin/WebView2Loader.dll")
            add_custom_command(TARGET mocida_installer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/bin/WebView2Loader.dll"
                    $<TARGET_FILE_DIR:mocida_installer>)
        endif()
    endif()
endif()

# ----------------------------------------------------------------------
# Optional `docs` target: runs Doxygen from the project root, dropping
# the generated site into docs/generated/html. Only registered when
# doxygen is found on PATH. Run with:
#     cmake --build build --target docs
# or directly via the convenience wrapper:
#     docs.bat
# ----------------------------------------------------------------------
find_program(DOXYGEN_EXECUTABLE doxygen)
if(DOXYGEN_EXECUTABLE)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
endif()
