// Windows-subsystem entry-point shim.
//
// Mocida apps link as the GUI ("windows") subsystem so the OS never
// auto-allocates a console window. That switches the default entry
// point from mainCRTStartup (which calls user's `main`) to
// WinMainCRTStartup (which calls `WinMain`). The user code in every
// Mocida app is still written with `int main(...)`, so we provide a
// trivial WinMain here that delegates straight to main using the
// argc / argv globals exposed by the MSVC CRT.
//
// In Debug builds UIApp_Create then attaches a console at runtime
// (see EnsureDebugConsole in app.c). In Release no console is ever
// opened.
//
// On non-Windows targets this file compiles to nothing.

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>   /* __argc / __argv macros (CRT thread-local lookups) */

extern int main(int argc, char** argv);

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR     lpCmdLine,
                   int       nShowCmd) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

    /* __argc / __argv come from <stdlib.h>; on the MSVC CRT they
     * expand to *__p___argc() / *__p___argv(), the per-thread CRT
     * accessors populated before user code runs. */
    return main(__argc, __argv);
}

#endif
