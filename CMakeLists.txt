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
# add_subdirectory so that SDL_image / SDL_ttf use their bundled libs
# (libpng, freetype, etc.) instead of looking for them in the system.
#
# Note on static linking: ideally a static `mocida` build would also
# bundle SDL3 / SDL_image / SDL_ttf into the final exe so users only
# distribute one file. In practice the SDL3 ecosystem expects the
# `SDL3::SDL3` shared alias to be present whenever SDL_image or
# SDL_ttf are added as in-tree subdirs (their find_package(SDL3)
# resolves through it), so flipping SDL_STATIC=ON breaks the SDL_image
# / SDL_ttf configure step with "Could not find a package
# configuration file provided by SDL3".
#
# Until we wire a custom SDL3_DIR / SDL3_INCLUDE_DIRS / SDL3_LIBRARIES
# bridge, SDL3* always ship as DLLs alongside the exe. mocida_add_executable
# is already future-proofed: it inspects the SDL target type and only
# copies a DLL when one was actually produced.
# ----------------------------------------------------------------------
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(SDL)
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

# Note on the DLL situation: SDL3, SDL3_image and SDL3_ttf are always
# built as shared libs from this tree, even in the MOCIDA_BUILD_SHARED=OFF
# (static-mocida) path. The SDL3_image / SDL3_ttf CMakeLists insist on
# the SDL3 shared alias being available for their find_package(SDL3),
# and they export INTERFACE_SDL3_SHARED=ON through their public link
# interface — linking mocida against SDL3-static while SDL3_image still
# wants SDL3-shared makes CMake's generate step refuse the combination
# with "INTERFACE_SDL3_SHARED ... does not agree".
#
# In practice this means: a static mocida.lib still ships next to
# SDL3.dll / SDL3_image.dll / SDL3_ttf.dll / WebView2Loader.dll. Only
# mocida's own code is baked into the consumer .exe. Bundling SDL into
# the exe too is a future item that would require either:
#   - switching the vcpkg triplet to x64-windows-static and rebuilding
#     SDL_image / SDL_ttf with their static loaders, or
#   - patching SDL_image / SDL_ttf to expose SDL3_image-static /
#     SDL3_ttf-static targets compatible with SDL3-static.
target_link_libraries(mocida PUBLIC
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    CURL::libcurl
)

# Windows Media Foundation (UIVideo backend) + WebView2 (UIWebView).
# All MF libs ship with the Windows SDK. WebView2 is the vcpkg target.
# ws2_32 is the WinSock2 import lib used by debug.c's TCP sink.
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
        unofficial::webview2::webview2
    )
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
        # WebView2Loader.dll + libcurl.dll come from vcpkg's
        # x64-windows triplet (DLLs). They have to be copied next to
        # the exe regardless of how mocida is built; switching them
        # to fully-static would require swapping the vcpkg triplet to
        # x64-windows-static and rebuilding the world.
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/bin/WebView2Loader.dll")
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
