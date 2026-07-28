#include "window_icon_win.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdlib.h>

static HICON gd_big_icon;
static HICON gd_small_icon;

int gd_apply_window_icon(void *native_window) {
    HWND window = (HWND)native_window;
    const char *path = getenv("GD_WINDOW_ICON");
    HICON big_icon;
    HICON small_icon;
    if (!window || !path || !*path) return 0;

    big_icon = (HICON)LoadImageA(NULL, path, IMAGE_ICON,
                                GetSystemMetrics(SM_CXICON),
                                GetSystemMetrics(SM_CYICON),
                                LR_LOADFROMFILE);
    small_icon = (HICON)LoadImageA(NULL, path, IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON),
                                  LR_LOADFROMFILE);
    if (!big_icon && !small_icon) return 0;
    if (!big_icon) big_icon = small_icon;
    if (!small_icon) small_icon = big_icon;

    if (gd_big_icon && gd_big_icon != gd_small_icon) DestroyIcon(gd_big_icon);
    if (gd_small_icon) DestroyIcon(gd_small_icon);
    gd_big_icon = big_icon;
    gd_small_icon = small_icon;
    SendMessageA(window, WM_SETICON, ICON_BIG, (LPARAM)gd_big_icon);
    SendMessageA(window, WM_SETICON, ICON_SMALL, (LPARAM)gd_small_icon);
    // Some Windows shells read the class icon instead of WM_GETICON during
    // taskbar regrouping or immediately after the OpenGL window is recreated.
    // Keep both sources synchronized so Lite/World/SubZero cannot inherit a
    // stale icon from the previous process.
    SetClassLongPtrA(window, GCLP_HICON, (LONG_PTR)gd_big_icon);
    SetClassLongPtrA(window, GCLP_HICONSM, (LONG_PTR)gd_small_icon);
    SendMessageA(window, WM_SETTINGCHANGE, 0, 0);
    return 1;
}
#else
int gd_apply_window_icon(void *native_window) {
    (void)native_window;
    return 0;
}
#endif
