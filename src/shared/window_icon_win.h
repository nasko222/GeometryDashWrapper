#ifndef GD_WINDOW_ICON_WIN_H
#define GD_WINDOW_ICON_WIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Applies GD_WINDOW_ICON to a native Win32 window. Returns nonzero on success. */
int gd_apply_window_icon(void *native_window);

#ifdef __cplusplus
}
#endif

#endif
