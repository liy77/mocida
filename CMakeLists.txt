cmake_minimum_required(VERSION 3.11)
project(mocida C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release optimizations (MSVC / Clang / MinGW).
set(CMAKE_C_FLAGS_RELEASE "/O2 /GL /Gw /Gy /GF /DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF")

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
if(MOCIDA_BUILD_SHARED)
    add_library(mocida SHARED ${MOCIDA_SOURCES} ${MOCIDA_SOURCES_CXX})
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
    add_library(mocida STATIC ${MOCIDA_SOURCES} ${MOCIDA_SOURCES_CXX})
    message(STATUS "Mocida: building as static library")
endif()

target_include_directories(mocida PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/headers
)

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
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE mocida)

    if(WIN32)
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
# Optional GUI installer (installer/installer.c). The installer itself
# is a Mocida app — it ships the headers + DLLs from this build dir to
# a user-chosen target folder and publishes MOCIDA_INCLUDE_DIR /
# MOCIDA_LIB_DIR / PATH via the Windows registry.
#
# Requires MOCIDA_BUILD_SHARED=ON because the payload it distributes is
# mocida.dll plus the import .lib.
# ----------------------------------------------------------------------
option(MOCIDA_BUILD_INSTALLER "Build the GUI installer (installer/installer.c)" OFF)
if(MOCIDA_BUILD_INSTALLER)
    if(NOT MOCIDA_BUILD_SHARED)
        message(FATAL_ERROR
            "MOCIDA_BUILD_INSTALLER requires MOCIDA_BUILD_SHARED=ON — the installer "
            "ships mocida.dll, so it has to be built as a DLL.")
    endif()
    mocida_add_executable(mocida_installer installer/installer.c)
    if(WIN32)
        # shlwapi: StrStrIA (case-insensitive substring for PATH probing).
        # advapi32: registry APIs (HKLM/HKCU\\Environment).
        # user32:   SendMessageTimeoutA (WM_SETTINGCHANGE broadcast).
        target_link_libraries(mocida_installer PRIVATE shlwapi advapi32 user32)
    endif()

    # Stage the public headers next to the installer.exe so they form
    # part of the payload it copies at install time.
    add_custom_command(TARGET mocida_installer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/src/headers
            $<TARGET_FILE_DIR:mocida_installer>/include
        COMMENT "Staging Mocida headers next to the installer"
    )
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
