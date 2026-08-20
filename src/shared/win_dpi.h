#ifndef GD_WIN_DPI_H
#define GD_WIN_DPI_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/*
 * Make every wrapper render at real physical pixels by default. This is the
 * programmatic equivalent of Windows Compatibility -> High DPI scaling
 * override -> Application, with Per-Monitor-V2 preferred where available.
 * It is intentionally resolved dynamically so the same binaries keep working
 * on older Windows releases.
 */
static int gd_enable_application_dpi_awareness(void) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(HANDLE);
        SetProcessDpiAwarenessContextFn set_context =
            (SetProcessDpiAwarenessContextFn)(void*)GetProcAddress(
                user32, "SetProcessDpiAwarenessContext");
        if (set_context) {
            /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4 */
            if (set_context((HANDLE)(INT_PTR)-4)) return 2;
        }
    }

    {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            typedef HRESULT (WINAPI *SetProcessDpiAwarenessFn)(int);
            SetProcessDpiAwarenessFn set_awareness =
                (SetProcessDpiAwarenessFn)(void*)GetProcAddress(
                    shcore, "SetProcessDpiAwareness");
            if (set_awareness) {
                /* PROCESS_PER_MONITOR_DPI_AWARE == 2 */
                HRESULT result = set_awareness(2);
                FreeLibrary(shcore);
                if (SUCCEEDED(result)) return 1;
            } else {
                FreeLibrary(shcore);
            }
        }
    }

    if (user32) {
        typedef BOOL (WINAPI *SetProcessDPIAwareFn)(void);
        SetProcessDPIAwareFn set_aware =
            (SetProcessDPIAwareFn)(void*)GetProcAddress(user32,
                                                         "SetProcessDPIAware");
        if (set_aware && set_aware()) return 1;
    }
    return 0;
}
#else
static int gd_enable_application_dpi_awareness(void) { return 0; }
#endif

#endif
