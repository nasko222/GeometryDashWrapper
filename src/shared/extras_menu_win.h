#ifndef GD_EXTRAS_MENU_WIN_H
#define GD_EXTRAS_MENU_WIN_H

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GD_EXTRAS_ACTION_NONE = 0,
    GD_EXTRAS_ACTION_PLAY_PLACEHOLDER = 1,
    GD_EXTRAS_ACTION_PLAY_TIME_MACHINE_BETA = 2
};

typedef struct GdExtrasMenu {
#ifdef _WIN32
    HWND parent;
    HWND button;
#endif
    int enabled;
    int early_full_version;
    int time_machine_beta_available;
} GdExtrasMenu;

void gd_extras_menu_init(GdExtrasMenu *menu);
int gd_extras_menu_attach(GdExtrasMenu *menu, void *parent_window);
void gd_extras_menu_set_visible(GdExtrasMenu *menu, int visible);
int gd_extras_menu_handle_command(GdExtrasMenu *menu, unsigned long wparam);
void gd_extras_menu_destroy(GdExtrasMenu *menu);

#ifdef __cplusplus
}
#endif

#endif
