#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "extras_menu_win.h"
#include "runtime_settings.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define GD_EXTRAS_BUTTON_ID 0x6E10
#define GD_EXTRAS_PLACEHOLDER_ID 0x6E11
#define GD_EXTRAS_TIMEMACHINE_ID 0x6E12

static int eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

static int starts_ci(const char *text, const char *prefix) {
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (!*text || tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) return 0;
        ++text; ++prefix;
    }
    return 1;
}

static int is_full_geometry_dash(void) {
    const char *package_name = getenv("GD_GAME_PACKAGE");
    const char *title = getenv("GD_GAME_TITLE");
    if (package_name && *package_name) {
        return eq_ci(package_name, "com.robtopx.geometryjump") ||
               eq_ci(package_name, "com.robtop.geometryjump");
    }
    return title && eq_ci(title, "Geometry Dash");
}

static int is_early_full_version(const char *version) {
    if (!version || !*version || !is_full_geometry_dash()) return 0;
    return starts_ci(version, "1.0") || starts_ci(version, "1.1") ||
           starts_ci(version, "1.2") || starts_ci(version, "1.3");
}

void gd_extras_menu_init(GdExtrasMenu *menu) {
    const char *version;
    if (!menu) return;
    memset(menu, 0, sizeof(*menu));
    menu->enabled = gd_settings_extras_menu();
    version = getenv("GD_GAME_VERSION");
    menu->early_full_version = is_early_full_version(version);
    menu->time_machine_beta_available =
        menu->early_full_version && version && eq_ci(version, "1.02");
}

int gd_extras_menu_attach(GdExtrasMenu *menu, void *parent_window) {
    HWND parent = (HWND)parent_window;
    if (!menu || !parent || !menu->enabled) return 0;
    menu->parent = parent;
    menu->button = CreateWindowExA(
        0, "BUTTON", "Extras",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        8, 8, 78, 26, parent, (HMENU)(INT_PTR)GD_EXTRAS_BUTTON_ID,
        GetModuleHandleA(NULL), NULL);
    return menu->button != NULL;
}

void gd_extras_menu_set_visible(GdExtrasMenu *menu, int visible) {
    if (!menu || !menu->button) return;
    ShowWindow(menu->button, visible ? SW_SHOW : SW_HIDE);
}

static int show_popup(GdExtrasMenu *menu) {
    HMENU popup;
    RECT rect;
    UINT result;
    if (!menu || !menu->button) return GD_EXTRAS_ACTION_NONE;
    popup = CreatePopupMenu();
    if (!popup) return GD_EXTRAS_ACTION_NONE;
    if (menu->early_full_version) {
        AppendMenuA(popup, MF_STRING, GD_EXTRAS_PLACEHOLDER_ID,
                    "Play Placeholder Level");
    }
    if (menu->time_machine_beta_available) {
        AppendMenuA(popup, MF_STRING, GD_EXTRAS_TIMEMACHINE_ID,
                    "Play Time Machine Beta");
    }
    if (!menu->early_full_version && !menu->time_machine_beta_available) {
        AppendMenuA(popup, MF_STRING | MF_GRAYED, 0, "No extras for this version yet");
    }
    GetWindowRect(menu->button, &rect);
    result = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                            rect.left, rect.bottom, 0, menu->parent, NULL);
    DestroyMenu(popup);
    if (result == GD_EXTRAS_PLACEHOLDER_ID) return GD_EXTRAS_ACTION_PLAY_PLACEHOLDER;
    if (result == GD_EXTRAS_TIMEMACHINE_ID) return GD_EXTRAS_ACTION_PLAY_TIME_MACHINE_BETA;
    return GD_EXTRAS_ACTION_NONE;
}

int gd_extras_menu_handle_command(GdExtrasMenu *menu, unsigned long wparam) {
    if (!menu || !menu->enabled) return GD_EXTRAS_ACTION_NONE;
    if (LOWORD(wparam) != GD_EXTRAS_BUTTON_ID || HIWORD(wparam) != BN_CLICKED)
        return GD_EXTRAS_ACTION_NONE;
    return show_popup(menu);
}

void gd_extras_menu_destroy(GdExtrasMenu *menu) {
    if (!menu) return;
    if (menu->button) DestroyWindow(menu->button);
    menu->button = NULL;
    menu->parent = NULL;
}

#else
void gd_extras_menu_init(GdExtrasMenu *menu) {
    if (!menu) return;
    memset(menu, 0, sizeof(*menu));
    menu->enabled = gd_settings_extras_menu();
}
int gd_extras_menu_attach(GdExtrasMenu *menu, void *parent_window) {
    (void)menu; (void)parent_window; return 0;
}
void gd_extras_menu_set_visible(GdExtrasMenu *menu, int visible) {
    (void)menu; (void)visible;
}
int gd_extras_menu_handle_command(GdExtrasMenu *menu, unsigned long wparam) {
    (void)menu; (void)wparam; return GD_EXTRAS_ACTION_NONE;
}
void gd_extras_menu_destroy(GdExtrasMenu *menu) { (void)menu; }
#endif
