#ifndef GD_EXTRAS_MENU_WIN_H
#define GD_EXTRAS_MENU_WIN_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GD_EXTRAS_ACTION_NONE = 0,
    GD_EXTRAS_ACTION_PLAY_PLACEHOLDER = 1,
    GD_EXTRAS_ACTION_PLAY_TIME_MACHINE_BETA = 2,
    GD_EXTRAS_ACTION_UI_CHANGED = 3
};

enum {
    GD_EXTRAS_POINTER_BEGIN = 1,
    GD_EXTRAS_POINTER_MOVE = 2,
    GD_EXTRAS_POINTER_END = 3
};

typedef struct GdExtrasLayout {
    float logical_width;
    float logical_height;
    float main_x;
    float main_y;
    float placeholder_x;
    float placeholder_y;
    float time_machine_x;
    float time_machine_y;
    float close_x;
    float close_y;
    float empty_x;
    float empty_y;
} GdExtrasLayout;

typedef struct GdExtrasMenu {
    int enabled;
    int early_full_version;
    int time_machine_beta_available;
    int visible;
    int overlay_open;
    int pointer_capture;
} GdExtrasMenu;

void gd_extras_menu_init(GdExtrasMenu *menu);
/* Kept as no-op compatibility shims for older backend call sites. The UI is
   rendered inside cocos2d by the backend starting with gdpsfixes5. */
int gd_extras_menu_attach(GdExtrasMenu *menu, void *parent_window);
void gd_extras_menu_set_visible(GdExtrasMenu *menu, int visible);
int gd_extras_menu_handle_command(GdExtrasMenu *menu, unsigned long wparam);
void gd_extras_menu_destroy(GdExtrasMenu *menu);

void gd_extras_menu_get_layout(const GdExtrasMenu *menu,
                               int native_width, int native_height,
                               GdExtrasLayout *layout);
int gd_extras_menu_pointer_event(GdExtrasMenu *menu, int phase,
                                 float native_x, float native_y,
                                 int native_width, int native_height,
                                 int *consumed);
int gd_extras_menu_pointer_captured(const GdExtrasMenu *menu);

#ifdef __cplusplus
}
#endif

#endif
