#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "extras_menu_win.h"
#include "runtime_settings.h"

static int eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int starts_ci(const char *text, const char *prefix) {
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (!*text || tolower((unsigned char)*text) !=
                          tolower((unsigned char)*prefix)) return 0;
        ++text;
        ++prefix;
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

static int hit(float x, float y, float cx, float cy, float w, float h) {
    return x >= cx - w * 0.5f && x <= cx + w * 0.5f &&
           y >= cy - h * 0.5f && y <= cy + h * 0.5f;
}

void gd_extras_menu_init(GdExtrasMenu *menu) {
    const char *version;
    if (!menu) return;
    memset(menu, 0, sizeof(*menu));
    menu->enabled = 0;
    version = getenv("GD_GAME_VERSION");
    menu->early_full_version = is_early_full_version(version);
    menu->time_machine_beta_available =
        menu->early_full_version && version && eq_ci(version, "1.02");
}

int gd_extras_menu_attach(GdExtrasMenu *menu, void *parent_window) {
    (void)parent_window;
    return menu && menu->enabled;
}

void gd_extras_menu_set_visible(GdExtrasMenu *menu, int visible) {
    if (!menu) return;
    menu->visible = visible != 0;
    if (!menu->visible) {
        menu->overlay_open = 0;
        menu->pointer_capture = 0;
    }
}

int gd_extras_menu_handle_command(GdExtrasMenu *menu, unsigned long wparam) {
    (void)menu;
    (void)wparam;
    return GD_EXTRAS_ACTION_NONE;
}

void gd_extras_menu_destroy(GdExtrasMenu *menu) {
    if (!menu) return;
    menu->visible = 0;
    menu->overlay_open = 0;
    menu->pointer_capture = 0;
}

void gd_extras_menu_get_layout(const GdExtrasMenu *menu,
                               int native_width, int native_height,
                               GdExtrasLayout *layout) {
    float logical_width = 568.8889f;
    (void)menu;
    if (!layout) return;
    memset(layout, 0, sizeof(*layout));
    if (native_width > 0 && native_height > 0)
        logical_width = 320.0f * (float)native_width / (float)native_height;
    if (logical_width < 420.0f) logical_width = 420.0f;
    layout->logical_width = logical_width;
    layout->logical_height = 320.0f;
    layout->main_x = logical_width - 57.0f;
    layout->main_y = 28.0f;
    layout->placeholder_x = logical_width * 0.5f;
    layout->placeholder_y = menu && menu->time_machine_beta_available
                                ? 190.0f : 170.0f;
    layout->time_machine_x = logical_width * 0.5f;
    layout->time_machine_y = 140.0f;
    layout->close_x = logical_width * 0.5f;
    layout->close_y = menu && menu->early_full_version ? 82.0f : 112.0f;
    layout->empty_x = logical_width * 0.5f;
    layout->empty_y = 176.0f;
}

int gd_extras_menu_pointer_event(GdExtrasMenu *menu, int phase,
                                 float native_x, float native_y,
                                 int native_width, int native_height,
                                 int *consumed) {
    enum { HIT_NONE = 0, HIT_MAIN = 1, HIT_PLACEHOLDER = 2,
           HIT_TIME = 3, HIT_CLOSE = 4, HIT_BACKGROUND = 5 };
    GdExtrasLayout layout;
    float scale;
    float x;
    float y;
    int candidate = HIT_NONE;
    int action = GD_EXTRAS_ACTION_NONE;

    if (consumed) *consumed = 0;
    if (!menu || !menu->enabled || !menu->visible || native_height <= 0)
        return GD_EXTRAS_ACTION_NONE;

    gd_extras_menu_get_layout(menu, native_width, native_height, &layout);
    scale = (float)native_height / 320.0f;
    x = native_x / scale;
    y = 320.0f - native_y / scale;

    if (menu->overlay_open) {
        if (menu->early_full_version &&
            hit(x, y, layout.placeholder_x, layout.placeholder_y, 260.0f, 42.0f))
            candidate = HIT_PLACEHOLDER;
        else if (menu->time_machine_beta_available &&
                 hit(x, y, layout.time_machine_x, layout.time_machine_y, 260.0f, 42.0f))
            candidate = HIT_TIME;
        else if (hit(x, y, layout.close_x, layout.close_y, 120.0f, 40.0f))
            candidate = HIT_CLOSE;
        else
            candidate = HIT_BACKGROUND;
    } else if (hit(x, y, layout.main_x, layout.main_y, 112.0f, 42.0f)) {
        candidate = HIT_MAIN;
    }

    if (phase == GD_EXTRAS_POINTER_BEGIN) {
        if (candidate != HIT_NONE) {
            menu->pointer_capture = candidate;
            if (consumed) *consumed = 1;
        }
        return GD_EXTRAS_ACTION_NONE;
    }

    if (phase == GD_EXTRAS_POINTER_MOVE) {
        if (menu->pointer_capture != HIT_NONE && consumed) *consumed = 1;
        return GD_EXTRAS_ACTION_NONE;
    }

    if (phase != GD_EXTRAS_POINTER_END || menu->pointer_capture == HIT_NONE)
        return GD_EXTRAS_ACTION_NONE;

    if (consumed) *consumed = 1;
    if (candidate == menu->pointer_capture) {
        switch (candidate) {
        case HIT_MAIN:
            menu->overlay_open = 1;
            action = GD_EXTRAS_ACTION_UI_CHANGED;
            break;
        case HIT_PLACEHOLDER:
            menu->overlay_open = 0;
            action = GD_EXTRAS_ACTION_PLAY_PLACEHOLDER;
            break;
        case HIT_TIME:
            menu->overlay_open = 0;
            action = GD_EXTRAS_ACTION_PLAY_TIME_MACHINE_BETA;
            break;
        case HIT_CLOSE:
            menu->overlay_open = 0;
            action = GD_EXTRAS_ACTION_UI_CHANGED;
            break;
        default:
            break;
        }
    }
    menu->pointer_capture = HIT_NONE;
    return action;
}

int gd_extras_menu_pointer_captured(const GdExtrasMenu *menu) {
    return menu && menu->pointer_capture != 0;
}
