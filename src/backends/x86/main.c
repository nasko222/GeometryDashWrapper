#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <GL/gl.h>

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jni_shim.h"
#include "loader.h"
#include "runtime.h"
#include "build_info.h"
#include "runtime_settings.h"
#include "frame_pacing_win.h"
#include "antialias_win.h"
#include "audio_win.h"
#include "extras_menu_win.h"
#include "window_icon_win.h"
#include "win_dpi.h"

typedef int (*JniOnLoadFunction)(void *java_vm, void *reserved);
typedef void (*NativeSetApkPathFunction)(void *environment, void *object,
                                         void *path);
typedef void (*NativeInitFunction)(void *environment, void *object,
                                   int width, int height);
typedef void (*NativeRenderFunction)(void *environment, void *object);
typedef void (*NativeTouchFunction)(void *environment, void *object, int id,
                                    float x, float y);
typedef void (*NativeTouchesFunction)(void *environment, void *object,
                                      void *ids, void *xs, void *ys);
typedef int (*NativeKeyFunction)(void *environment, void *object, int key);
typedef void (*NativeInsertTextFunction)(void *environment, void *object,
                                         void *text);
typedef void (*NativeDeleteBackwardFunction)(void *environment, void *object);
typedef void (*NativeLifecycleFunction)(void *environment, void *object);
typedef void *(__cdecl *GameManagerSharedStateFunction)(void);
typedef void *(__cdecl *GameManagerGetPlayLayerFunction)(void *self);
typedef void (__cdecl *GameManagerSetPlayLayerFunction)(void *self, void *play_layer);
typedef void (__cdecl *UiCheckpointFunction)(void *self, void *sender);
typedef void (__cdecl *UiCheckpointNoSenderFunction)(void *self);
typedef int (__cdecl *CcNodeGetTagFunction)(void *self);
typedef void (__cdecl *CcNodeSetTagFunction)(void *self, int tag);
typedef int (__cdecl *CcNodeIsVisibleFunction)(void *self);
typedef void (__cdecl *EditorMoveObjectCallFunction)(void *self, void *sender);
typedef void (__cdecl *EditorMoveEditCommandFunction)(void *self, int command);
typedef void (__cdecl *EditorTransformObjectCallFunction)(void *self, void *sender);
typedef void (__cdecl *EditorTransformEditCommandFunction)(void *self, int command);
typedef void (__cdecl *EditorDeleteFunction)(void *self, void *sender);
typedef void (__cdecl *EditorDeleteNoSenderFunction)(void *self);
typedef void (__cdecl *PauseRestartFunction)(void *self, void *sender);
typedef void (__cdecl *PauseRestartNoSenderFunction)(void *self);
typedef void (__cdecl *PlayLayerResumeAndRestartFunction)(void *self);
typedef void *(__cdecl *CcDirectorSharedFunction)(void);
typedef void *(__cdecl *CcDirectorGetRunningSceneFunction)(void *self);
typedef void *(__cdecl *CcNodeGetChildrenFunction)(void *self);
typedef unsigned int (__cdecl *CcNodeGetChildrenCountFunction)(void *self);
typedef void *(__cdecl *CcArrayObjectAtIndexFunction)(void *self, unsigned int index);
typedef void *(__cdecl *ButtonSpriteCreateFunction)(const char *text);
typedef void (__cdecl *CcNodeAddChildFunction)(void *self, void *child);
typedef void (__cdecl *CcNodeAddChildZFunction)(void *self, void *child, int z);
typedef void (__cdecl *CcNodeSetPositionFunction)(void *self, float x, float y);
typedef struct { float x, y; } GdCcPoint;
typedef void (__cdecl *EndPortalSetPositionFunction)(void *self, const GdCcPoint *point);
typedef float (__cdecl *CcNodeGetPositionFunction)(void *self);
typedef void *(__cdecl *CcNodeGetCameraFunction)(void *self);
typedef void (__cdecl *CcCameraGetXYZFunction)(void *self, float *x, float *y, float *z);
typedef void (__cdecl *CcNodeSetScaleFunction)(void *self, float scale);
typedef void (__cdecl *CcNodeSetFloatFunction)(void *self, float value);
typedef void *(__cdecl *CcNodeCreateFunction)(void);
typedef void (__cdecl *CcNodeRemoveFunction)(void *self, int cleanup);
typedef void (__cdecl *CcNodeSetVisibleFunction)(void *self, int visible);
typedef void (__cdecl *CcLayerSetBoolFunction)(void *self, int enabled);
typedef void (__cdecl *CcNodeNoArgFunction)(void *self);
typedef void (__cdecl *CcObjectRefFunction)(void *self);
typedef void *(__cdecl *LevelEditorGetLevelFunction)(void *self);
typedef void *(__cdecl *NodeGetterFunction)(void *self);
typedef int (__cdecl *IntGetterFunction)(void *self);
typedef void (__cdecl *IntSetterFunction)(void *self, int value);
typedef void *(__cdecl *NoArgCreateFunction)(void);
typedef void (__cdecl *GJGameLevelSetLevelStringFunction)(void *self,
                                                          void *string_object);
typedef void *(__cdecl *PlayLayerCreateFunction)(void *level);
typedef void (__cdecl *PlayLayerStartGameFunction)(void *self);
typedef void *(__cdecl *CcSpriteCreateWithFrameFunction)(const char *name);
typedef void *(__cdecl *CcSpriteCreateFileFunction)(const char *name);
typedef struct { unsigned char r, g, b; } GdCcColor3B;
typedef void (__cdecl *CcSpriteSetColorFunction)(void *self, const GdCcColor3B *color);
typedef void *(__cdecl *CcMenuCreateFunction)(void);
typedef void *(__cdecl *CcMenuItemSpriteExtraCreateFunction)(
    void *normal, void *selected, void *target, uintptr_t member_function,
    intptr_t this_adjustment);
typedef struct { unsigned char r, g, b, a; } GdCcColor4B;
typedef void *(__cdecl *CcLayerColorCreateFunction)(const GdCcColor4B *color);

extern void gd_call_sret_string_x86(void *function, void *output, void *self);
extern void gd_call_sret_color3b_x86(void *function, void *output, void *self, int index);

typedef struct {
    HWND window;
    HDC device;
    HGLRC context;
    NativeRenderFunction render;
    NativeTouchFunction touch_begin;
    NativeTouchFunction touch_end;
    NativeTouchesFunction touch_move;
    NativeKeyFunction key_down;
    NativeInsertTextFunction insert_text;
    NativeDeleteBackwardFunction delete_backward;
    NativeLifecycleFunction pause;
    NativeLifecycleFunction resume;
    void *touch_ids;
    void *touch_xs;
    void *touch_ys;
    int native_width;
    int native_height;
    float last_touch_x;
    float last_touch_y;
    int native_ready;
    int mouse_down;
    int mouse_touch_forwarded;
    int old_playtest_button_pointer_down;
    int keyboard_down;
    int native_paused;
    int window_active;
    int closing;
    int fullscreen;
    LONG_PTR windowed_style;
    LONG_PTR windowed_ex_style;
    WINDOWPLACEMENT windowed_placement;
    int vsync_enabled;
    ULONGLONG gameplay_cache_time;
    int gameplay_cache_value;
    int editor_cache_value;
    void *active_play_layer;
    void *active_editor_layer;
    void *active_pause_layer;
    GameManagerSharedStateFunction game_manager_shared_state;
    GameManagerGetPlayLayerFunction game_manager_get_play_layer;
    GameManagerSetPlayLayerFunction game_manager_set_play_layer;
    IntGetterFunction game_manager_get_edit_mode;
    IntSetterFunction game_manager_set_edit_mode;
    UiCheckpointFunction ui_on_check;
    UiCheckpointFunction ui_on_delete_check;
    UiCheckpointNoSenderFunction ui_on_check_no_sender;
    UiCheckpointNoSenderFunction ui_on_delete_check_no_sender;
    CcNodeGetTagFunction ccnode_get_tag;
    CcNodeSetTagFunction ccnode_set_tag;
    CcNodeIsVisibleFunction ccnode_is_visible;
    EditorMoveObjectCallFunction editor_move_object_call;
    EditorMoveEditCommandFunction editor_move_edit_command;
    EditorTransformObjectCallFunction editor_transform_object_call;
    EditorTransformEditCommandFunction editor_transform_edit_command;
    EditorDeleteFunction editor_on_delete;
    EditorDeleteNoSenderFunction editor_on_delete_no_sender;
    PauseRestartFunction pause_layer_on_restart;
    PauseRestartNoSenderFunction pause_layer_on_restart_no_sender;
    PlayLayerResumeAndRestartFunction play_layer_resume_and_restart;
    CcDirectorSharedFunction cc_director_shared;
    CcDirectorGetRunningSceneFunction cc_director_get_running_scene;
    size_t cc_director_running_scene_offset;
    CcNodeGetChildrenFunction ccnode_get_children;
    CcNodeGetChildrenCountFunction ccnode_get_children_count;
    CcArrayObjectAtIndexFunction ccarray_object_at_index;
    ButtonSpriteCreateFunction button_sprite_create;
    CcNodeAddChildFunction ccnode_add_child;
    CcNodeAddChildZFunction ccnode_add_child_z;
    CcNodeSetPositionFunction ccnode_set_position;
    EndPortalSetPositionFunction end_portal_set_position;
    void *end_portal_trigger_object;
    CcNodeGetPositionFunction ccnode_get_position_x;
    CcNodeGetPositionFunction ccnode_get_position_y;
    CcNodeGetPositionFunction ccnode_get_rotation;
    CcNodeGetPositionFunction ccnode_get_scale_x;
    CcNodeGetPositionFunction ccnode_get_scale_y;
    CcNodeGetCameraFunction ccnode_get_camera;
    CcCameraGetXYZFunction cccamera_get_center_xyz;
    CcNodeSetScaleFunction ccnode_set_scale;
    CcNodeSetFloatFunction ccnode_set_rotation;
    CcNodeSetFloatFunction ccnode_set_scale_x;
    CcNodeSetFloatFunction ccnode_set_scale_y;
    CcNodeCreateFunction ccnode_create;
    CcNodeRemoveFunction ccnode_remove;
    CcNodeSetVisibleFunction ccnode_set_visible;
    CcLayerSetBoolFunction cclayer_set_touch_enabled;
    IntGetterFunction cclayer_is_touch_enabled;
    CcLayerSetBoolFunction cclayer_set_keypad_enabled;
    CcNodeNoArgFunction ccnode_unschedule_update;
    CcNodeNoArgFunction ccnode_unschedule_all_selectors;
    CcNodeNoArgFunction ccnode_stop_all_actions;
    CcObjectRefFunction ccobject_retain;
    CcObjectRefFunction ccobject_release;
    LevelEditorGetLevelFunction level_editor_get_level;
    NodeGetterFunction level_editor_get_game_layer;
    void *level_editor_get_level_string;
    GJGameLevelSetLevelStringFunction gj_game_level_set_level_string;
    NoArgCreateFunction gj_game_level_create;
    IntGetterFunction gj_game_level_get_audio_track;
    IntSetterFunction gj_game_level_set_audio_track;
    IntGetterFunction gj_game_level_get_level_type;
    IntSetterFunction gj_game_level_set_level_type;
    PlayLayerCreateFunction play_layer_create;
    PlayLayerStartGameFunction play_layer_start_game;
    NodeGetterFunction play_layer_get_level;
    void *play_layer_reset_level;
    void *play_layer_update_attempts;
    void *play_layer_destroy_player;
    void *play_layer_get_test_mode;
    NodeGetterFunction play_layer_get_player;
    NodeGetterFunction play_layer_get_game_layer;
    NodeGetterFunction play_layer_get_ui_layer;
    IntGetterFunction player_get_is_dead;
    IntGetterFunction player_get_on_ground;
    IntGetterFunction player_get_gravity_flipped;
    IntGetterFunction player_get_fly_mode;
    IntGetterFunction player_get_roll_mode;
    IntGetterFunction player_get_bird_mode;
    IntGetterFunction game_manager_get_player_frame;
    IntGetterFunction game_manager_get_player_ship;
    IntGetterFunction game_manager_get_player_ball;
    IntGetterFunction game_manager_get_player_bird;
    IntGetterFunction game_manager_get_player_color;
    IntGetterFunction game_manager_get_player_color2;
    void *game_manager_color_for_idx;
    CcSpriteCreateWithFrameFunction sprite_create_with_frame;
    CcSpriteCreateFileFunction sprite_create_file;
    CcSpriteSetColorFunction sprite_set_color;
    CcMenuCreateFunction cc_menu_create;
    CcMenuItemSpriteExtraCreateFunction menu_item_sprite_extra_create;
    IntGetterFunction ccmenu_is_enabled;
    IntSetterFunction ccmenu_set_enabled;
    CcNodeNoArgFunction editor_ui_update_slider;
    void *play_layer_toggle_flipped;
    CcLayerColorCreateFunction cclayer_color_create;
    void *active_editor_ui;
    void *active_menu_layer;
    void *active_scene_root;
    void *extras_scene_root;
    void *extras_main_button;
    void *extras_overlay;
    void *extras_placeholder_button;
    void *extras_time_button;
    void *extras_close_button;
    void *extras_empty_button;
    volatile LONG restart_request;
    ULONGLONG restart_check_time;
    void *restart_scene;
    void *restart_pause_layer;
    void *restart_menu;
    void *restart_button;
    int restart_native_present;
    int restart_unavailable_logged;
    volatile LONG old_playtest_request;
    ULONGLONG old_playtest_check_time;
    void *old_playtest_scene;
    void *old_playtest_editor;
    void *old_playtest_ui;
    void *old_playtest_play_menu;
    void *old_playtest_play_button;
    void *old_playtest_stop_button;
    void *old_playtest_layer;
    void *old_playtest_player;
    void *old_playtest_play_game_layer;
    void *old_playtest_editor_game_layer;
    void *old_playtest_proxy_root;
    void *old_playtest_proxy_primary;
    void *old_playtest_proxy_secondary;
    void *old_playtest_proxy_tertiary;
    void *old_playtest_proxy_quaternary;
    void *old_playtest_proxy_quinary;
    int old_playtest_proxy_mode;
    int old_playtest_proxy_icon;
    unsigned int old_playtest_proxy_poll_counter;
    void *old_playtest_level_clone;
    void *old_playtest_previous_play_layer;
    int old_playtest_previous_edit_mode;
    int old_playtest_previous_edit_mode_valid;
    int old_playtest_editor_input_suspended;
    int old_playtest_editor_ui_touch_was_enabled;
    int old_playtest_editor_layer_touch_was_enabled;
    void *old_playtest_end_portal;
    int old_playtest_end_portal_scanned;
    unsigned char old_playtest_end_trigger_original;
    int old_playtest_end_trigger_suppressed;
    unsigned char old_playtest_mirror_original;
    int old_playtest_mirror_suppressed;
    unsigned char old_playtest_destroy_player_original;
    int old_playtest_destroy_player_suppressed;
    unsigned char old_playtest_reset_level_original;
    int old_playtest_reset_level_suppressed;
    ULONGLONG old_playtest_death_grace_until;
    void *old_playtest_trail;
    float old_playtest_trail_last_x;
    float old_playtest_trail_last_y;
    int old_playtest_trail_has_last;
    unsigned int old_playtest_trail_segments;
    float old_playtest_editor_camera_original_x;
    float old_playtest_editor_camera_original_y;
    float old_playtest_editor_camera_original_scale_x;
    float old_playtest_editor_camera_original_scale_y;
    int old_playtest_editor_camera_original_valid;
    int old_playtest_camera_fallback_logged;
    int old_playtest_constrained_camera_valid;
    int old_playtest_constrained_camera_mode;
    float old_playtest_constrained_camera_y;
    void *old_playtest_editor_menus[128];
    unsigned char old_playtest_editor_menu_enabled[128];
    unsigned int old_playtest_editor_menu_count;
    void *old_playtest_editor_sliders[32];
    unsigned char old_playtest_editor_slider_touch_enabled[32];
    unsigned int old_playtest_editor_slider_count;
    uint32_t old_playtest_test_mode_offset;
    int old_playtest_unavailable_logged;
    unsigned int editor_hotkey_miss_logs;
    int editor_hotkey_negative_cached;
    GdExtrasMenu extras_menu;
    uint32_t practice_mode_offset;
    GdFramePacer frame_pacer;
    double fps_limit;
} GameHost;

static GameHost g_host;

#define OLD_PLAYTEST_BUTTON_X 30.0f
#define OLD_PLAYTEST_BUTTON_Y 186.0f
#define WRAPPER_RESTART_BUTTON_X 465.0f
#define WRAPPER_RESTART_BUTTON_Y 130.0f
/* GJ_playBtn2 is about 82 px high; the pause icon is about 40 px. Scale the
   play sprites themselves, not CCMenuItemSpriteExtra, so its press animation
   cannot restore the item to an oversized scale. */
#define OLD_PLAYTEST_PLAY_SPRITE_SCALE 0.49f
#define OLD_PLAYTEST_CAMERA_ANCHOR_X 120.0f
/* The editor game layer is a CCLayer-sized 570x320 surface. Scaling that node
   happens around its logical center, while the scene-root proxy/trail node has
   a zero-sized anchor. Keep the two transforms separate or the player drifts
   vertically from level objects as soon as playtest zoom != 1.0. */
#define OLD_PLAYTEST_CAMERA_PIVOT_X 285.0f
#define OLD_PLAYTEST_CAMERA_PIVOT_Y 160.0f
#define OLD_PLAYTEST_CUBE_ZOOM_OUT_SCALE 0.90f
#define OLD_PLAYTEST_CONSTRAINED_ZOOM_OUT_SCALE 0.70f
#define OLD_PLAYTEST_CUBE_GROUND_WORLD_Y 105.0f
#define OLD_PLAYTEST_CUBE_CAMERA_LIFT_Y 25.0f
#define OLD_PLAYTEST_BALL_UFO_CAMERA_LIFT_Y 45.0f
#define OLD_PLAYTEST_END_PORTAL_AHEAD_X 100000.0f
#define OLD_PLAYTEST_DEATH_GRACE_MS 1500u
#define OLD_PLAYTEST_LINE_TEXTURE_WIDTH 16.0f
#define OLD_PLAYTEST_TRAIL_STEP 16.0f
#define OLD_PLAYTEST_TRAIL_MAX_SEGMENTS 256u
#define OLD_PLAYTEST_RAD_TO_DEG 57.29577951308232f

enum {
    OLD_PLAYTEST_MODE_CUBE = 0,
    OLD_PLAYTEST_MODE_SHIP = 1,
    OLD_PLAYTEST_MODE_BALL = 2,
    OLD_PLAYTEST_MODE_BIRD = 3
};

typedef unsigned char *(__cdecl *AndroidFileDataFunction)(
    void *self, const char *filename, const char *mode,
    unsigned long *size, int asynchronous);

static AndroidFileDataFunction g_original_android_file_data;
typedef int (__cdecl *TinyXmlParseFunction)(void *document,
                                             const char *xml,
                                             unsigned int size);
static TinyXmlParseFunction g_original_tinyxml_parse;
typedef void (__cdecl *MusicDownloadCompletedFunction)(void *self,
                                                        void *client,
                                                        void *response);
static MusicDownloadCompletedFunction g_original_music_download_completed;

static unsigned char *trace_android_file_data(
    void *self, const char *filename, const char *mode,
    unsigned long *size, int asynchronous) {
    unsigned char *result;
    runtime_log("APK asset read: %s (mode=%s async=%d)",
                filename ? filename : "<null>", mode ? mode : "<null>",
                asynchronous != 0);
    result = g_original_android_file_data(
        self, filename, mode, size, asynchronous);
    runtime_log("APK asset result: %s -> %s (%lu bytes)",
                filename ? filename : "<null>", result ? "ok" : "MISSING",
                size ? *size : 0ul);
    return result;
}

static int trace_tinyxml_parse(void *document, const char *xml,
                               unsigned int size) {
    int result = g_original_tinyxml_parse(document, xml, size);
    if (size == 5554u || size == 17466u) {
        int document_error = document
                                 ? *(const int *)((const unsigned char *)document +
                                                  0x30)
                                 : -1;
        runtime_log("TinyXML objectDefinitions: size=%u result=%d documentError=%d",
                    size, result, document_error);
    }
    return result;
}

static void copy_readable_text(const char *source, char *destination,
                               size_t capacity) {
    MEMORY_BASIC_INFORMATION memory;
    const char *region_end;
    size_t index = 0;
    if (!destination || !capacity) return;
    destination[0] = 0;
    if (!source ||
        !VirtualQuery(source, &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        snprintf(destination, capacity, "<unavailable>");
        return;
    }
    region_end = (const char *)memory.BaseAddress + memory.RegionSize;
    while (index + 1 < capacity && source + index < region_end &&
           source[index]) {
        unsigned char character = (unsigned char)source[index];
        destination[index] = character >= 0x20 && character < 0x7f
                                 ? (char)character
                                 : ' ';
        ++index;
    }
    destination[index] = 0;
    if (!index) snprintf(destination, capacity, "<empty>");
}

static void trace_music_download_completed(void *self, void *client,
                                           void *response) {
    int success = 0;
    int response_code = 0;
    size_t response_size = 0;
    char error[192] = "<unavailable>";
    if (response) {
        const unsigned char *object = (const unsigned char *)response;
        const unsigned char *begin = *(const unsigned char *const *)(object + 0x28);
        const unsigned char *end = *(const unsigned char *const *)(object + 0x2c);
        const char *error_source = *(const char *const *)(object + 0x44);
        success = object[0x24] != 0;
        response_code = *(const int *)(object + 0x40);
        if (begin && (uintptr_t)end >= (uintptr_t)begin) {
            response_size = (size_t)((uintptr_t)end - (uintptr_t)begin);
        }
        copy_readable_text(error_source, error, sizeof(error));
    }
    runtime_log("Song HTTP completion: success=%s status=%d bytes=%lu error=%s",
                success ? "yes" : "no", response_code,
                (unsigned long)response_size, error);
    g_original_music_download_completed(self, client, response);
}

static int install_x86_detour(void *target, const unsigned char *expected,
                              size_t overwrite_size, void *replacement,
                              void **original) {
#if defined(__i386__)
    unsigned char *entry = (unsigned char *)target;
    unsigned char *trampoline;
    int32_t displacement;
    DWORD old_protection;
    DWORD ignored_protection;
    size_t index;
    if (!entry || !replacement || !original || overwrite_size < 5 ||
        memcmp(entry, expected, overwrite_size) != 0) {
        return 0;
    }
    trampoline = (unsigned char *)VirtualAlloc(
        NULL, overwrite_size + 5, MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE);
    if (!trampoline) return 0;
    memcpy(trampoline, entry, overwrite_size);
    trampoline[overwrite_size] = 0xe9;
    displacement = (int32_t)((uintptr_t)entry + overwrite_size -
                             ((uintptr_t)trampoline + overwrite_size + 5));
    memcpy(trampoline + overwrite_size + 1, &displacement,
           sizeof(displacement));
    if (!VirtualProtect(entry, overwrite_size, PAGE_EXECUTE_READWRITE,
                        &old_protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return 0;
    }
    entry[0] = 0xe9;
    displacement = (int32_t)((uintptr_t)replacement -
                             ((uintptr_t)entry + 5));
    memcpy(entry + 1, &displacement, sizeof(displacement));
    for (index = 5; index < overwrite_size; ++index) entry[index] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), entry, overwrite_size);
    VirtualProtect(entry, overwrite_size, old_protection,
                   &ignored_protection);
    *original = trampoline;
    return 1;
#else
    (void)target;
    (void)expected;
    (void)overwrite_size;
    (void)replacement;
    (void)original;
    return 0;
#endif
}

static void install_android_asset_trace(const ElfImage *image) {
    static const unsigned char expected[] = {
        0x8d, 0x64, 0x24, 0xa4, 0x8b, 0x4c, 0x24, 0x68
    };
    void *target = elf_image_find_export(
        image,
        "_ZN7cocos2d18CCFileUtilsAndroid13doGetFileDataEPKcS2_Pmb");
    if (!target) {
        runtime_log("APK asset trace: Cocos Android reader export unavailable");
        return;
    }
    if (install_x86_detour(target, expected, sizeof(expected),
                           trace_android_file_data,
                           (void **)&g_original_android_file_data)) {
        runtime_log("APK asset trace: installed");
    } else {
        runtime_log("APK asset trace: skipped (unknown Cocos prologue)");
    }
}

static void install_tinyxml_trace(const ElfImage *image) {
    static const unsigned char expected[] = {
        0x8d, 0x64, 0x24, 0xd4, 0x89, 0x5c, 0x24, 0x1c
    };
    void *target = elf_image_find_export(
        image, "_ZN8tinyxml211XMLDocument5ParseEPKcj");
    if (!target) {
        runtime_log("TinyXML trace: parser export unavailable");
        return;
    }
    if (install_x86_detour(target, expected, sizeof(expected),
                           trace_tinyxml_parse,
                           (void **)&g_original_tinyxml_parse)) {
        runtime_log("TinyXML trace: installed");
    } else {
        runtime_log("TinyXML trace: skipped (unknown parser prologue)");
    }
}

static void install_music_download_trace(const ElfImage *image) {
    static const unsigned char expected[] = {
        0x8d, 0x64, 0x24, 0xb4, 0x89, 0x5c, 0x24, 0x3c
    };
    void *target = elf_image_find_export(
        image,
        "_ZN20MusicDownloadManager23onDownloadSongCompletedEPN7cocos2d9extension12CCHttpClientEPNS1_14CCHttpResponseE");
    if (!target) {
        runtime_log("Song HTTP trace: callback export unavailable");
        return;
    }
    if (install_x86_detour(target, expected, sizeof(expected),
                           trace_music_download_completed,
                           (void **)&g_original_music_download_completed)) {
        runtime_log("Song HTTP trace: installed");
    } else {
        runtime_log("Song HTTP trace: skipped (unknown callback prologue)");
    }
}

static int patch_x86_code(void *target, const void *bytes, size_t size) {
#if defined(__i386__)
    DWORD old_protection;
    DWORD ignored_protection;
    if (!target || !bytes || !size ||
        !VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protection)) {
        return 0;
    }
    memcpy(target, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    VirtualProtect(target, size, old_protection, &ignored_protection);
    return 1;
#else
    (void)target;
    (void)bytes;
    (void)size;
    return 0;
#endif
}

static int patch_x86_return_true(void *target) {
    static const unsigned char code[] = {
        0xb8, 0x01, 0x00, 0x00, 0x00, /* mov eax, 1 */
        0xc3                          /* ret */
    };
    return patch_x86_code(target, code, sizeof(code));
}

static int patch_x86_return_false(void *target) {
    static const unsigned char code[] = {
        0x31, 0xc0, /* xor eax, eax */
        0xc3        /* ret */
    };
    return patch_x86_code(target, code, sizeof(code));
}

static int patch_x86_return_void(void *target) {
    static const unsigned char code[] = {0xc3}; /* ret */
    return patch_x86_code(target, code, sizeof(code));
}

typedef struct {
    unsigned offset_markers;
    unsigned void_callbacks;
    unsigned bool_callbacks;
} DesktopKeyboardPatchCounts;

static int desktop_keyboard_marker_visitor(const char *name, void *address,
                                           uint32_t size, void *opaque) {
    DesktopKeyboardPatchCounts *counts =
        (DesktopKeyboardPatchCounts *)opaque;
    (void)address;
    (void)size;
    if (!name || !counts) return 1;
    if (strstr(name, "forceOffset") != NULL ||
        strstr(name, "textInputShouldOffset") != NULL ||
        strstr(name, "doAnimationWhenKeyboardMove") != NULL) {
        ++counts->offset_markers;
    }
    return 1;
}

static int desktop_keyboard_export_visitor(const char *name, void *address,
                                           uint32_t size, void *opaque) {
    DesktopKeyboardPatchCounts *counts =
        (DesktopKeyboardPatchCounts *)opaque;
    (void)size;
    if (!name || !address || !counts) return 1;
    if (strstr(name, "textInputShouldOffset") != NULL) {
        if (patch_x86_return_false(address)) {
            ++counts->bool_callbacks;
            runtime_log("Desktop keyboard: return false patched %s", name);
        }
    } else if (strstr(name, "forceOffset") != NULL ||
               strstr(name, "doAnimationWhenKeyboardMove") != NULL) {
        if (patch_x86_return_void(address)) {
            ++counts->void_callbacks;
            runtime_log("Desktop keyboard: no-op patched %s", name);
        }
    }
    return 1;
}

static void install_desktop_keyboard_offset_patches(const ElfImage *image) {
    DesktopKeyboardPatchCounts counts;
    memset(&counts, 0, sizeof(counts));
    if (!elf_image_visit_exports(image, desktop_keyboard_marker_visitor,
                                 &counts)) {
        runtime_log("Desktop keyboard marker scan ended early");
        return;
    }
    /* Early versions such as 1.6 do not expose the Android scene-offset
       callbacks. Leave their keyboard path byte-identical instead of applying
       a broad keyboardWillShow/Hide patch to every historical build. */
    if (!counts.offset_markers) {
        runtime_log("Desktop keyboard offset patches: not required");
        return;
    }
    if (!elf_image_visit_exports(image, desktop_keyboard_export_visitor,
                                 &counts)) {
        runtime_log("Desktop keyboard patch scan ended early");
    }
    runtime_log("Desktop keyboard offset patches: markers=%u void=%u bool=%u",
                counts.offset_markers, counts.void_callbacks,
                counts.bool_callbacks);
}

static int patch_x86_tail_jump(void *source, void *destination) {
#if defined(__i386__)
    unsigned char code[5];
    int32_t displacement;
    if (!source || !destination) return 0;
    code[0] = 0xe9;
    displacement = (int32_t)((uintptr_t)destination -
                             ((uintptr_t)source + sizeof(code)));
    memcpy(code + 1, &displacement, sizeof(displacement));
    return patch_x86_code(source, code, sizeof(code));
#else
    (void)source;
    (void)destination;
    return 0;
#endif
}

static unsigned patch_x86_return_true_exports(
    const ElfImage *image, const char *const *names, size_t count) {
    unsigned patched = 0;
    size_t index;
    for (index = 0; index < count; ++index) {
        void *target = elf_image_find_export(image, names[index]);
        if (target && patch_x86_return_true(target)) {
            runtime_log("Launch hack: return true patched %s", names[index]);
            ++patched;
        }
    }
    return patched;
}


static unsigned patch_x86_force_high_texture_quality(const ElfImage *image) {
    unsigned char *target = (unsigned char *)elf_image_find_export(
        image, "_ZN7cocos2d10CCDirector18updateContentScaleENS_14TextureQualityE");
    size_t offset;
    if (!target) return 0;
    /* 2.11/World compare the requested TextureQuality against 2 before the
       high-resolution branch. cmp eax,2 -> cmp eax,eax + nop makes that branch
       deterministic without replacing the rest of CCDirector's setup. */
    for (offset = 0; offset + 3 <= 64; ++offset) {
        static const unsigned char replacement[3] = {0x39, 0xc0, 0x90};
        if (target[offset] == 0x83 && target[offset + 1] == 0xf8 &&
            target[offset + 2] == 0x02 &&
            patch_x86_code(target + offset, replacement, sizeof(replacement))) {
            runtime_log("Launch hack: forced highest CCDirector texture quality");
            return 1;
        }
    }
    return 0;
}

static unsigned patch_x86_world_creator_buttons(const ElfImage *image) {
    unsigned char *target = (unsigned char *)elf_image_find_export(
        image, "_ZN12CreatorLayer4initEv");
    size_t offset;
    if (!target) return 0;
    /* Geometry Dash World first loads each button's real callback, then this
       exact conditional swaps selected entries to onOnlyFullVersion and tints
       their sprites dark. Make only that conditional unconditional so the real
       callbacks and normal bright sprites survive. */
    for (offset = 0; offset + 9 <= 2048; ++offset) {
        if (target[offset] == 0x80 && target[offset + 1] == 0xbd &&
            target[offset + 6] == 0x00 && target[offset + 7] == 0x74 &&
            target[offset + 8] >= 0x10) {
            const unsigned char jump = 0xeb;
            if (patch_x86_code(target + offset + 7, &jump, 1)) {
                runtime_log("Launch hack: enabled native Geometry Dash World Creator callbacks");
                return 1;
            }
        }
    }
    return 0;
}

static void install_configurable_x86_hacks(const ElfImage *image) {
    static const char *const icon_checks[] = {
        "_ZN11GameManager14isIconUnlockedEi",
        "_ZN11GameManager14isIconUnlockedEi8IconType",
        "_ZN11GameManager15isColorUnlockedEi",
        "_ZN11GameManager15isColorUnlockedEi10UnlockType",
        "_ZN11GameManager15isColorUnlockedEib",
    };
    static const char *const high_graphics_checks[] = {
        "_ZN15PlatformToolbox4isHDEv",
    };
    static const char *const low_memory_checks[] = {
        "_ZN15PlatformToolbox17isLowMemoryDeviceEv",
        "_ZN16EveryplayToolbox14isLowEndDeviceEv",
    };
    static const char *const online_checks[] = {
        "_ZN12CreatorLayer19canPlayOnlineLevelsEv",
    };
    static const struct {
        const char *locked;
        const char *unlocked;
        const char *description;
    } bypass_pairs[] = {
        {
            "_ZN9MenuLayer13onFullVersionEPN7cocos2d8CCObjectE",
            "_ZN9MenuLayer9onCreatorEPN7cocos2d8CCObjectE",
            "MenuLayer full-version->Creator",
        },
        {
            "_ZN9MenuLayer13onFullVersionEv",
            "_ZN9MenuLayer9onCreatorEv",
            "MenuLayer full-version->Creator",
        },
    };
    unsigned icon_patches = 0;
    unsigned bypass_patches = 0;
    unsigned online_patches = 0;
    unsigned high_graphics_patches = 0;
    unsigned low_memory_patches = 0;
    unsigned texture_quality_patches = 0;
    unsigned world_creator_patches = 0;
    size_t index;
    if (gd_settings_hack_icons()) {
        icon_patches = patch_x86_return_true_exports(
            image, icon_checks, sizeof(icon_checks) / sizeof(icon_checks[0]));
    }
    if (gd_settings_force_highest_graphics()) {
        high_graphics_patches = patch_x86_return_true_exports(
            image, high_graphics_checks,
            sizeof(high_graphics_checks) / sizeof(high_graphics_checks[0]));
        texture_quality_patches = patch_x86_force_high_texture_quality(image);
        for (index = 0;
             index < sizeof(low_memory_checks) / sizeof(low_memory_checks[0]);
             ++index) {
            void *target = elf_image_find_export(image, low_memory_checks[index]);
            if (target && patch_x86_return_false(target)) {
                runtime_log("Launch hack: return false patched %s",
                            low_memory_checks[index]);
                ++low_memory_patches;
            }
        }
    }
    if (gd_settings_full_bypass()) {
        online_patches = patch_x86_return_true_exports(
            image, online_checks, sizeof(online_checks) / sizeof(online_checks[0]));
        world_creator_patches = patch_x86_world_creator_buttons(image);
        for (index = 0;
             index < sizeof(bypass_pairs) / sizeof(bypass_pairs[0]); ++index) {
            void *locked = elf_image_find_export(image, bypass_pairs[index].locked);
            void *unlocked = elf_image_find_export(image, bypass_pairs[index].unlocked);
            if (locked && unlocked && patch_x86_tail_jump(locked, unlocked)) {
                runtime_log("Launch hack: %s", bypass_pairs[index].description);
                ++bypass_patches;
            }
        }
    }
    runtime_log("Launch settings applied: server=%s hack-icons-colors=%s patches=%u "
                "full-bypass=%s redirects=%u online-checks=%u "
                "highest-graphics=%s hd=%u low-memory=%u texture-quality=%u "
                "world-creator=%u music-pulse-max=%.3f",
                gd_settings_server(),
                gd_settings_hack_icons() ? "true" : "false", icon_patches,
                gd_settings_full_bypass() ? "true" : "false", bypass_patches,
                online_patches,
                gd_settings_force_highest_graphics() ? "true" : "false",
                high_graphics_patches, low_memory_patches,
                texture_quality_patches, world_creator_patches,
                gd_settings_music_pulse_max());
}

/*
 * Detect a live PlayLayer without hard-coding a GameManager field offset.
 * Android x86 builds use the Itanium C++ ABI: object[0] is a vtable,
 * vtable[-1] is type_info, and type_info[1] points to the class name.
 */
static int memory_range_is_readable(const void *address, size_t size) {
    MEMORY_BASIC_INFORMATION memory;
    uintptr_t begin = (uintptr_t)address;
    uintptr_t end;
    if (!address || size == 0 || begin > UINTPTR_MAX - size) return 0;
    end = begin + size;
    while (begin < end) {
        uintptr_t region_end;
        if (!VirtualQuery((const void *)begin, &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
        region_end = (uintptr_t)memory.BaseAddress + memory.RegionSize;
        if (region_end <= begin) return 0;
        begin = region_end < end ? region_end : end;
    }
    return 1;
}


static uint32_t derive_practice_mode_offset(const ElfImage *image) {
    const unsigned char *code = (const unsigned char *)elf_image_find_export(
        image, "_ZN9PlayLayer18togglePracticeModeEb");
    size_t compare_index;
    if (!code || !memory_range_is_readable(code, 192u)) return 0u;
    for (compare_index = 0u; compare_index + 6u <= 192u; ++compare_index) {
        unsigned char compare_modrm;
        uint32_t displacement;
        size_t store_index;
        if (code[compare_index] != 0x38u) continue;
        compare_modrm = code[compare_index + 1u];
        if ((compare_modrm & 0xc0u) != 0x80u) continue;
        memcpy(&displacement, code + compare_index + 2u, sizeof(displacement));
        if (displacement < 0x100u || displacement > 0x10000u) continue;
        for (store_index = compare_index + 6u;
             store_index + 6u <= 192u; ++store_index) {
            uint32_t store_displacement;
            if (code[store_index] != 0x88u ||
                code[store_index + 1u] != compare_modrm) continue;
            memcpy(&store_displacement, code + store_index + 2u,
                   sizeof(store_displacement));
            if (store_displacement == displacement) return displacement;
        }
    }
    return 0u;
}

static uint32_t derive_old_playtest_mode_offset(const ElfImage *image) {
    const unsigned char *code = (const unsigned char *)elf_image_find_export(
        image, "_ZNK9PlayLayer11getTestModeEv");
    size_t index;
    if (!code || !memory_range_is_readable(code, 64u)) return 0u;
    for (index = 0u; index + 7u <= 64u; ++index) {
        uint32_t displacement;
        if (code[index] != 0x0fu || code[index + 1u] != 0xb6u ||
            (code[index + 2u] & 0xc0u) != 0x80u) continue;
        memcpy(&displacement, code + index + 3u, sizeof(displacement));
        if (displacement >= 0x100u && displacement <= 0x10000u)
            return displacement;
    }
    return 0u;
}

/*
 * Old cocos2d-x Android x86 builds often inline getRunningScene(), so there is
 * no exported accessor. Derive m_pRunningScene from CCDirector::drawScene()
 * instead of guessing by scanning every CCScene pointer in the director.
 *
 * GCC's 32-bit code loads m_pRunningScene, tests it, then virtually calls
 * CCNode::visit at vtable+0x110. Support both disp8 and disp32 member loads.
 */
static size_t derive_running_scene_offset(const ElfImage *image) {
    const unsigned char *code = (const unsigned char *)elf_image_find_export(
        image, "_ZN7cocos2d10CCDirector9drawSceneEv");
    size_t i;
    if (!code || !memory_range_is_readable(code, 512u)) return 0u;

    for (i = 0u; i + 18u <= 512u; ++i) {
        /* mov eax,[esi+disp8]; test eax,eax; jz rel8; mov edx,[eax];
           ...; call [edx+0x110] */
        if (code[i] == 0x8Bu && code[i + 1u] == 0x46u &&
            code[i + 3u] == 0x85u && code[i + 4u] == 0xC0u &&
            code[i + 5u] == 0x74u) {
            size_t j;
            for (j = i + 7u; j + 6u <= i + 24u && j + 6u <= 512u; ++j) {
                if (code[j] == 0xFFu && code[j + 1u] == 0x92u &&
                    code[j + 2u] == 0x10u && code[j + 3u] == 0x01u &&
                    code[j + 4u] == 0x00u && code[j + 5u] == 0x00u)
                    return (size_t)code[i + 2u];
            }
        }
        /* mov eax,[esi+disp32] equivalent */
        if (code[i] == 0x8Bu && code[i + 1u] == 0x86u &&
            i + 9u < 512u && code[i + 6u] == 0x85u &&
            code[i + 7u] == 0xC0u && code[i + 8u] == 0x74u) {
            uint32_t displacement;
            size_t j;
            memcpy(&displacement, code + i + 2u, sizeof(displacement));
            if (displacement > 0x1000u) continue;
            for (j = i + 10u; j + 6u <= i + 28u && j + 6u <= 512u; ++j) {
                if (code[j] == 0xFFu && code[j + 1u] == 0x92u &&
                    code[j + 2u] == 0x10u && code[j + 3u] == 0x01u &&
                    code[j + 4u] == 0x00u && code[j + 5u] == 0x00u)
                    return (size_t)displacement;
            }
        }
    }
    return 0u;
}

static int object_type_contains(const void *object, const char *needle) {
    const void *vtable;
    const void *type_info;
    const char *name;
    size_t index;
    if (!object || !needle ||
        !memory_range_is_readable(object, sizeof(void *))) return 0;
    vtable = *(const void *const *)object;
    if (!vtable ||
        !memory_range_is_readable((const unsigned char *)vtable - sizeof(void *),
                                  sizeof(void *))) return 0;
    type_info = *((const void *const *)vtable - 1);
    if (!type_info ||
        !memory_range_is_readable((const unsigned char *)type_info + sizeof(void *),
                                  sizeof(void *))) return 0;
    name = *(const char *const *)((const unsigned char *)type_info + sizeof(void *));
    if (!name || !memory_range_is_readable(name, 1)) return 0;
    for (index = 0; index < 96u; ++index) {
        if (!memory_range_is_readable(name + index, 1)) return 0;
        if (!name[index]) break;
    }
    if (index == 96u) return 0;
    return strstr(name, needle) != NULL;
}

static void *find_running_scene(void) {
    void *director;
    size_t offset;
    if (!g_host.cc_director_shared) return NULL;
    director = g_host.cc_director_shared();
    if (!director || !memory_range_is_readable(director, sizeof(void *))) return NULL;

    /* Prefer Cocos' own accessor. The old fallback scanned CCDirector memory and
       could select a retained previous CCScene, which made cached EditorUI state
       survive into level-select/menu screens. */
    if (g_host.cc_director_get_running_scene) {
        void *running = g_host.cc_director_get_running_scene(director);
        if (object_type_contains(running, "CCScene")) return running;
    }

    if (!memory_range_is_readable(director, 0x500u)) return NULL;
    for (offset = 0; offset + sizeof(void *) <= 0x500u; offset += sizeof(void *)) {
        void *candidate = *(void **)((unsigned char *)director + offset);
        if (object_type_contains(candidate, "CCScene")) return candidate;
    }
    return NULL;
}

static void walk_scene_tree(void *node, unsigned int depth, unsigned int *visited) {
    unsigned int count, index;
    void *children;
    if (!node || !visited || depth > 12u || *visited >= 4096u ||
        !object_type_contains(node, "")) return;
    ++*visited;
    {
        const int is_menu = object_type_contains(node, "MenuLayer");
        const int is_play = object_type_contains(node, "PlayLayer");
        const int is_editor = object_type_contains(node, "LevelEditorLayer");
        const int is_editor_ui = object_type_contains(node, "EditorUI");
        const int is_pause = object_type_contains(node, "PauseLayer") &&
                             !object_type_contains(node, "EditorPauseLayer");
        if (!g_host.active_menu_layer && is_menu) g_host.active_menu_layer = node;
        if (!g_host.active_pause_layer && is_pause) g_host.active_pause_layer = node;
        if (!g_host.active_play_layer && is_play) g_host.active_play_layer = node;
        if (!g_host.active_editor_layer && is_editor) g_host.active_editor_layer = node;
        if (!g_host.active_editor_ui && is_editor_ui) g_host.active_editor_ui = node;
        if ((is_menu || is_play || is_editor_ui) && !is_editor) return;
    }
    if (!g_host.ccnode_get_children || !g_host.ccnode_get_children_count ||
        !g_host.ccarray_object_at_index) return;
    count = g_host.ccnode_get_children_count(node);
    if (!count) return;
    if (count > 512u) count = 512u;
    children = g_host.ccnode_get_children(node);
    if (!children) return;
    for (index = 0; index < count && *visited < 4096u; ++index) {
        void *child = g_host.ccarray_object_at_index(children, index);
        if (child) walk_scene_tree(child, depth + 1u, visited);
    }
}

static void refresh_scene_tree_state(void) {
    unsigned int visited = 0;
    g_host.active_play_layer = NULL;
    g_host.active_editor_layer = NULL;
    g_host.active_pause_layer = NULL;
    g_host.active_editor_ui = NULL;
    g_host.active_menu_layer = NULL;
    g_host.active_scene_root = find_running_scene();
    if (g_host.active_scene_root)
        walk_scene_tree(g_host.active_scene_root, 0u, &visited);
    g_host.gameplay_cache_value = g_host.active_play_layer != NULL;
    g_host.editor_cache_value = g_host.active_editor_layer != NULL;
    g_host.gameplay_cache_time = GetTickCount64();
}

/*
 * Pause/cursor features used to call the full recursive Cocos scene walk from
 * the render loop every 500 ms. On old x86 builds (notably 1.5/1.6) that made
 * level-page swipes visibly freeze at the same cadence. The recurrent path now
 * looks only through GameManager's small state block. Full scene traversal is
 * reserved for editor-hotkey/Extras discovery and pause-menu inspection.
 */
static int detect_gameplay_active(void) {
    ULONGLONG now = GetTickCount64();
    void *manager;
    size_t offset;
    if (now - g_host.gameplay_cache_time < 250u)
        return g_host.gameplay_cache_value;
    g_host.gameplay_cache_time = now;
    g_host.gameplay_cache_value = 0;
    g_host.editor_cache_value = 0;
    g_host.active_play_layer = NULL;
    g_host.active_editor_layer = NULL;
    if (g_host.game_manager_shared_state) {
        manager = g_host.game_manager_shared_state();
        if (manager && memory_range_is_readable(manager, 0x600u)) {
            for (offset = 0x40u; offset + sizeof(void *) <= 0x600u;
                 offset += sizeof(void *)) {
                void *candidate = *(void **)((unsigned char *)manager + offset);
                if (!candidate) continue;
                if (!g_host.active_editor_layer &&
                    object_type_contains(candidate, "LevelEditorLayer"))
                    g_host.active_editor_layer = candidate;
                if (!g_host.active_play_layer &&
                    object_type_contains(candidate, "PlayLayer"))
                    g_host.active_play_layer = candidate;
                if (g_host.active_play_layer && g_host.active_editor_layer)
                    break;
            }
        }
    }
    g_host.gameplay_cache_value = g_host.active_play_layer != NULL;
    g_host.editor_cache_value = g_host.active_editor_layer != NULL;
    return g_host.gameplay_cache_value;
}

static void *find_active_ui_layer(void) {
    size_t offset;
    unsigned char *play_layer;
    if (!detect_gameplay_active() || !g_host.active_play_layer) return NULL;
    play_layer = (unsigned char *)g_host.active_play_layer;
    for (offset = 0x100u; offset + sizeof(void *) <= 0x3000u;
         offset += sizeof(void *)) {
        void *candidate;
        if (!memory_range_is_readable(play_layer + offset, sizeof(void *)))
            continue;
        candidate = *(void **)(play_layer + offset);
        if (object_type_contains(candidate, "UILayer")) return candidate;
    }
    return NULL;
}





/*
 * Editor shortcut lookup is scene-cached.  A/D/W/S/Q/E must never trigger a
 * full Cocos traversal repeatedly in menus, but x86 1.5/2.11-era builds also
 * do not reliably register LevelEditorLayer in GameManager.  The tweaks12
 * GameManager hard gate therefore disabled editor controls completely.
 *
 * Use CCDirector::getRunningScene when exported, cache both positive and
 * negative EditorUI discovery for that exact scene, and rescan only when the
 * running scene changes.  A hidden EditorUI (editor playtest) remains a hard
 * no-op, so edit shortcuts cannot fire while Play owns keyboard input.
 */
static void *find_active_editor_ui(void) {
    unsigned int visited = 0;
    void *scene = find_running_scene();

    /*
     * Restore the discovery path that actually worked before tweaks12/13.
     * Those builds made a speculative CCDirector running-scene derivation a
     * hard gate; on the user's real 1.50 x86 APK it selected a scene that did
     * not expose EditorUI and disabled every editor shortcut.
     *
     * Keep the good part of the optimization: a failed discovery is cached so
     * A/D/W/S/Q/E in menus do not recursively walk thousands of Cocos nodes on
     * every keypress.  The miss is scoped to the current CCScene; entering the
     * editor changes the scene and automatically permits one fresh discovery.
     */
    if (scene && scene == g_host.active_scene_root &&
        g_host.active_editor_ui &&
        object_type_contains(g_host.active_editor_ui, "EditorUI")) {
        if (g_host.ccnode_is_visible &&
            !g_host.ccnode_is_visible(g_host.active_editor_ui))
            return NULL;
        return g_host.active_editor_ui;
    }

    if (scene && scene == g_host.active_scene_root &&
        g_host.editor_hotkey_negative_cached)
        return NULL;

    refresh_scene_tree_state();
    if (g_host.active_editor_ui &&
        object_type_contains(g_host.active_editor_ui, "EditorUI")) {
        g_host.editor_hotkey_negative_cached = 0;
        if (g_host.ccnode_is_visible &&
            !g_host.ccnode_is_visible(g_host.active_editor_ui))
            return NULL;
        return g_host.active_editor_ui;
    }

    /* Some 1.5/2.11-era layouts expose LevelEditorLayer in the scene while
       EditorUI is reachable only below that layer.  tweaks11 had this second
       pass and it is required on the user's working x86 editor-control build. */
    if (g_host.active_editor_layer) {
        walk_scene_tree(g_host.active_editor_layer, 0u, &visited);
        if (g_host.active_editor_ui &&
            object_type_contains(g_host.active_editor_ui, "EditorUI")) {
            g_host.editor_hotkey_negative_cached = 0;
            if (g_host.ccnode_is_visible &&
                !g_host.ccnode_is_visible(g_host.active_editor_ui))
                return NULL;
            return g_host.active_editor_ui;
        }
    }

    g_host.editor_hotkey_negative_cached = 1;
    if (g_host.editor_hotkey_miss_logs++ < 8u)
        runtime_log("Editor controls: key ignored; no active visible EditorUI found (cached for current scene)");
    return NULL;
}

static void remove_extras_node(void **node) {
    if (!node || !*node) return;
    if (g_host.ccnode_remove && memory_range_is_readable(*node, sizeof(void *)))
        g_host.ccnode_remove(*node, 1);
    *node = NULL;
}

static int add_extras_child(void *parent, void *child, int z) {
    if (!parent || !child) return 0;
    if (g_host.ccnode_add_child_z) { g_host.ccnode_add_child_z(parent, child, z); return 1; }
    if (g_host.ccnode_add_child) { g_host.ccnode_add_child(parent, child); return 1; }
    return 0;
}

static void *create_extras_button(const char *text, void *parent,
                                  float x, float y, int z) {
    void *button;
    if (!g_host.button_sprite_create || !g_host.ccnode_set_position || !parent)
        return NULL;
    button = g_host.button_sprite_create(text);
    if (!button) return NULL;
    g_host.ccnode_set_position(button, x, y);
    if (!add_extras_child(parent, button, z)) return NULL;
    return button;
}

static void *find_direct_pause_layer(void *scene) {
    unsigned int count, index;
    void *children;
    if (!scene || !g_host.ccnode_get_children_count ||
        !g_host.ccnode_get_children || !g_host.ccarray_object_at_index)
        return NULL;
    count = g_host.ccnode_get_children_count(scene);
    if (!count) return NULL;
    if (count > 128u) count = 128u;
    children = g_host.ccnode_get_children(scene);
    if (!children) return NULL;
    for (index = 0; index < count; ++index) {
        void *child = g_host.ccarray_object_at_index(children, index);
        if (child && object_type_contains(child, "PauseLayer") &&
            !object_type_contains(child, "EditorPauseLayer"))
            return child;
    }
    return NULL;
}

static void __cdecl restart_button_callback(void *self, void *sender) {
    (void)self;
    (void)sender;
    InterlockedExchange(&g_host.restart_request, 1);
}

static int restart_button_symbols_ready(void) {
    return g_host.cc_menu_create && g_host.menu_item_sprite_extra_create &&
           g_host.ccnode_set_position &&
           (g_host.ccnode_add_child_z || g_host.ccnode_add_child) &&
           (g_host.pause_layer_on_restart ||
            g_host.pause_layer_on_restart_no_sender ||
            g_host.play_layer_resume_and_restart);
}

static int active_level_already_has_native_restart(int *native_restart) {
    void *level;
    if (native_restart) *native_restart = 0;
    if (!g_host.active_play_layer || !g_host.play_layer_get_level ||
        !g_host.gj_game_level_get_level_type) return 1;
    level = g_host.play_layer_get_level(g_host.active_play_layer);
    if (!level) return 1;
    if (native_restart)
        *native_restart = g_host.gj_game_level_get_level_type(level) == 2;
    return 1;
}

static void *create_restart_menu_item(void *pause_layer) {
    void *normal = NULL, *selected = NULL;
    if (!g_host.menu_item_sprite_extra_create || !pause_layer) return NULL;
    if (g_host.sprite_create_with_frame) {
        normal = g_host.sprite_create_with_frame("GJ_replayBtn_001.png");
        selected = g_host.sprite_create_with_frame("GJ_replayBtn_001.png");
    }
    if ((!normal || !selected) && g_host.button_sprite_create) {
        normal = g_host.button_sprite_create("Restart");
        selected = g_host.button_sprite_create("Restart");
    }
    if (!normal || !selected) return NULL;
    return g_host.menu_item_sprite_extra_create(
        normal, selected, pause_layer,
        (uintptr_t)(void (__cdecl *)(void *, void *))restart_button_callback,
        0);
}

static int ensure_restart_button(void) {
    void *scene;
    void *pause_layer;
    void *menu;
    void *button;
    int native_restart = 0;

    if (!gd_settings_restart_button()) {
        g_host.restart_pause_layer = NULL;
        g_host.restart_menu = NULL;
        g_host.restart_button = NULL;
        g_host.restart_native_present = 0;
        return 1;
    }

    /* WM_LBUTTONUP already invalidates the gameplay cache after forwarding the
       native touch. Removing only the separate 250 ms restart poll means the
       newly-created PauseLayer can be discovered immediately without adding
       another recurring scene/game-state scan to the render loop. */
    if (!detect_gameplay_active() || g_host.editor_cache_value ||
        g_host.old_playtest_layer) {
        g_host.restart_pause_layer = NULL;
        g_host.restart_menu = NULL;
        g_host.restart_button = NULL;
        g_host.restart_native_present = 0;
        return 1;
    }
    scene = find_running_scene();
    if (g_host.restart_scene != scene) {
        g_host.restart_scene = scene;
        g_host.restart_pause_layer = NULL;
        g_host.restart_menu = NULL;
        g_host.restart_button = NULL;
        g_host.restart_native_present = 0;
    }
    pause_layer = find_direct_pause_layer(scene);
    if (!pause_layer) {
        g_host.restart_pause_layer = NULL;
        g_host.restart_menu = NULL;
        g_host.restart_button = NULL;
        g_host.restart_native_present = 0;
        return 1;
    }
    if (g_host.restart_pause_layer != pause_layer) {
        g_host.restart_pause_layer = pause_layer;
        g_host.restart_menu = NULL;
        g_host.restart_button = NULL;
        g_host.restart_native_present = 0;
    }
    if (g_host.restart_button || g_host.restart_native_present) return 1;
    active_level_already_has_native_restart(&native_restart);
    if (native_restart) {
        g_host.restart_native_present = 1;
        runtime_log("RESULT: X86_RESTART_BUTTON native=1 wrapper=0 reason=local-level");
        return 1;
    }
    if (!restart_button_symbols_ready()) {
        if (!g_host.restart_unavailable_logged) {
            g_host.restart_unavailable_logged = 1;
            runtime_log("RESULT: X86_RESTART_BUTTON_UNAVAILABLE reason=missing-symbol");
        }
        return 1;
    }
    menu = g_host.cc_menu_create();
    button = create_restart_menu_item(pause_layer);
    if (!menu || !button || !add_extras_child(menu, button, 0)) return 0;
    g_host.ccnode_set_position(menu, 0.0f, 0.0f);
    g_host.ccnode_set_position(button, WRAPPER_RESTART_BUTTON_X,
                               WRAPPER_RESTART_BUTTON_Y);
    if (!add_extras_child(pause_layer, menu, 30000)) return 0;
    g_host.restart_menu = menu;
    g_host.restart_button = button;
    runtime_log("RESULT: X86_RESTART_BUTTON_READY mode=pause-overlay callback=%s",
                (g_host.pause_layer_on_restart ||
                 g_host.pause_layer_on_restart_no_sender)
                    ? "PauseLayer::onRestart"
                    : "PlayLayer::resumeAndRestart");
    return 1;
}

static int process_restart_request(void) {
    LONG request = InterlockedExchange(&g_host.restart_request, 0);
    void *pause_layer;
    if (!request) return 1;
    g_host.gameplay_cache_time = 0;
    (void)detect_gameplay_active();
    pause_layer = find_direct_pause_layer(find_running_scene());
    if (!pause_layer && g_host.restart_pause_layer &&
        memory_range_is_readable(g_host.restart_pause_layer, sizeof(void *)) &&
        object_type_contains(g_host.restart_pause_layer, "PauseLayer"))
        pause_layer = g_host.restart_pause_layer;

    if (pause_layer && g_host.pause_layer_on_restart) {
        g_host.pause_layer_on_restart(
            pause_layer, g_host.restart_button ? g_host.restart_button : pause_layer);
        runtime_log("RESULT: X86_RESTART_INVOKED path=PauseLayer::onRestart");
    } else if (pause_layer && g_host.pause_layer_on_restart_no_sender) {
        g_host.pause_layer_on_restart_no_sender(pause_layer);
        runtime_log("RESULT: X86_RESTART_INVOKED path=PauseLayer::onRestart-no-sender");
    } else if (g_host.active_play_layer && g_host.play_layer_resume_and_restart) {
        g_host.play_layer_resume_and_restart(g_host.active_play_layer);
        if (pause_layer && g_host.ccnode_remove &&
            memory_range_is_readable(pause_layer, sizeof(void *)))
            g_host.ccnode_remove(pause_layer, 1);
        runtime_log("RESULT: X86_RESTART_INVOKED path=PlayLayer::resumeAndRestart");
    } else {
        runtime_log("RESULT: X86_RESTART_IGNORED reason=no-active-pause-or-callback");
    }
    g_host.restart_pause_layer = NULL;
    g_host.restart_menu = NULL;
    g_host.restart_button = NULL;
    g_host.restart_native_present = 0;
    g_host.gameplay_cache_time = 0;
    return 1;
}

static void refresh_extras_visuals(void) {
    GdExtrasLayout layout;
    if (!g_host.extras_menu.enabled) return;
    if (g_host.active_scene_root != g_host.extras_scene_root) {
        g_host.extras_scene_root = g_host.active_scene_root;
        g_host.extras_main_button = NULL;
        g_host.extras_overlay = NULL;
        g_host.extras_placeholder_button = NULL;
        g_host.extras_time_button = NULL;
        g_host.extras_close_button = NULL;
        g_host.extras_empty_button = NULL;
    }
    if (!g_host.extras_menu.visible || !g_host.active_menu_layer ||
        !g_host.active_scene_root) {
        remove_extras_node(&g_host.extras_overlay);
        return;
    }
    gd_extras_menu_get_layout(&g_host.extras_menu, g_host.native_width,
                              g_host.native_height, &layout);
    if (!g_host.extras_main_button) {
        g_host.extras_main_button = create_extras_button(
            "Extras", g_host.active_menu_layer, layout.main_x, layout.main_y, 10000);
        if (g_host.extras_main_button)
            runtime_log("RESULT: X86_EXTRAS_GD_BUTTON_READY");
    }
    if (!g_host.extras_menu.overlay_open) {
        remove_extras_node(&g_host.extras_overlay);
        g_host.extras_placeholder_button = g_host.extras_time_button = NULL;
        g_host.extras_close_button = g_host.extras_empty_button = NULL;
        return;
    }
    if (g_host.extras_overlay || !g_host.cclayer_color_create) return;
    {
        GdCcColor4B color = {0, 0, 0, 180};
        g_host.extras_overlay = g_host.cclayer_color_create(&color);
    }
    if (!g_host.extras_overlay ||
        !add_extras_child(g_host.active_scene_root, g_host.extras_overlay, 20000)) {
        g_host.extras_overlay = NULL;
        return;
    }
    if (g_host.extras_menu.early_full_version)
        g_host.extras_placeholder_button = create_extras_button(
            "Play Placeholder Level", g_host.extras_overlay,
            layout.placeholder_x, layout.placeholder_y, 1);
    if (g_host.extras_menu.time_machine_beta_available)
        g_host.extras_time_button = create_extras_button(
            "Play Time Machine Beta", g_host.extras_overlay,
            layout.time_machine_x, layout.time_machine_y, 1);
    if (!g_host.extras_menu.early_full_version)
        g_host.extras_empty_button = create_extras_button(
            "No extras for this version", g_host.extras_overlay,
            layout.empty_x, layout.empty_y, 1);
    g_host.extras_close_button = create_extras_button(
        "Close", g_host.extras_overlay, layout.close_x, layout.close_y, 1);
    runtime_log("RESULT: X86_EXTRAS_GD_OVERLAY_READY");
}

static void *find_old_playtest_descendant_by_type(void *node,
                                                     const char *type_name,
                                                     unsigned int depth,
                                                     unsigned int *visited) {
    unsigned int count, index;
    void *children;
    if (!node || !type_name || !visited || depth > 16u ||
        *visited >= 8192u || !object_type_contains(node, "")) return NULL;
    ++*visited;
    if (object_type_contains(node, type_name)) return node;
    if (!g_host.ccnode_get_children || !g_host.ccnode_get_children_count ||
        !g_host.ccarray_object_at_index) return NULL;
    count = g_host.ccnode_get_children_count(node);
    if (!count) return NULL;
    if (count > 1024u) count = 1024u;
    children = g_host.ccnode_get_children(node);
    if (!children) return NULL;
    for (index = 0u; index < count && *visited < 8192u; ++index) {
        void *child = g_host.ccarray_object_at_index(children, index);
        void *match = child ? find_old_playtest_descendant_by_type(
                                  child, type_name, depth + 1u, visited)
                            : NULL;
        if (match) return match;
    }
    return NULL;
}

static int set_old_playtest_end_trigger_suppressed(int suppress) {
    unsigned char replacement;
    if (!g_host.end_portal_trigger_object) return 0;
    if (suppress) {
        if (g_host.old_playtest_end_trigger_suppressed) return 1;
        if (!memory_range_is_readable(g_host.end_portal_trigger_object, 1u)) return 0;
        g_host.old_playtest_end_trigger_original =
            *(const unsigned char *)g_host.end_portal_trigger_object;
        replacement = 0xc3u; /* ret: inline editor playtest has no level end. */
        if (!patch_x86_code(g_host.end_portal_trigger_object, &replacement, 1u))
            return 0;
        g_host.old_playtest_end_trigger_suppressed = 1;
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_END_DISABLED triggerObject=ret");
        return 1;
    }
    if (!g_host.old_playtest_end_trigger_suppressed) return 1;
    replacement = g_host.old_playtest_end_trigger_original;
    if (!patch_x86_code(g_host.end_portal_trigger_object, &replacement, 1u))
        return 0;
    g_host.old_playtest_end_trigger_suppressed = 0;
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_END_RESTORED");
    return 1;
}

static int set_old_playtest_mirror_suppressed(int suppress) {
    unsigned char replacement;
    if (!g_host.play_layer_toggle_flipped) return 1;
    if (suppress) {
        if (g_host.old_playtest_mirror_suppressed) return 1;
        if (!memory_range_is_readable(g_host.play_layer_toggle_flipped, 1u)) return 0;
        g_host.old_playtest_mirror_original =
            *(const unsigned char *)g_host.play_layer_toggle_flipped;
        replacement = 0xc3u; /* RET: mirror portals are inert in editor playtest. */
        if (!patch_x86_code(g_host.play_layer_toggle_flipped, &replacement, 1u))
            return 0;
        g_host.old_playtest_mirror_suppressed = 1;
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_MIRROR_DISABLED toggleFlipped=ret");
        return 1;
    }
    if (!g_host.old_playtest_mirror_suppressed) return 1;
    replacement = g_host.old_playtest_mirror_original;
    if (!patch_x86_code(g_host.play_layer_toggle_flipped, &replacement, 1u))
        return 0;
    g_host.old_playtest_mirror_suppressed = 0;
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_MIRROR_RESTORED");
    return 1;
}

static int set_old_playtest_reset_level_suppressed(int suppress) {
    unsigned char replacement;
    if (!g_host.play_layer_reset_level) return 1;
    if (suppress) {
        if (g_host.old_playtest_reset_level_suppressed) return 1;
        if (!memory_range_is_readable(g_host.play_layer_reset_level, 1u)) return 0;
        g_host.old_playtest_reset_level_original =
            *(const unsigned char *)g_host.play_layer_reset_level;
        replacement = 0xc3u;
        if (!patch_x86_code(g_host.play_layer_reset_level, &replacement, 1u))
            return 0;
        g_host.old_playtest_reset_level_suppressed = 1;
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_AUTORETRY_RESET_GUARD enabled=1");
        return 1;
    }
    if (!g_host.old_playtest_reset_level_suppressed) return 1;
    replacement = g_host.old_playtest_reset_level_original;
    if (!patch_x86_code(g_host.play_layer_reset_level, &replacement, 1u))
        return 0;
    g_host.old_playtest_reset_level_suppressed = 0;
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_AUTORETRY_RESET_GUARD restored");
    return 1;
}

static int set_old_playtest_destroy_player_suppressed(int suppress) {
    unsigned char replacement;
    if (!g_host.play_layer_destroy_player) return 1;
    if (suppress) {
        if (g_host.old_playtest_destroy_player_suppressed) return 1;
        if (!memory_range_is_readable(g_host.play_layer_destroy_player, 1u)) return 0;
        g_host.old_playtest_destroy_player_original =
            *(const unsigned char *)g_host.play_layer_destroy_player;
        replacement = 0xc3u; /* startup grace: ignore destroyPlayer briefly */
        if (!patch_x86_code(g_host.play_layer_destroy_player, &replacement, 1u))
            return 0;
        g_host.old_playtest_destroy_player_suppressed = 1;
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_STARTUP_DEATH_GUARD enabled=%ums",
                    (unsigned)OLD_PLAYTEST_DEATH_GRACE_MS);
        return 1;
    }
    if (!g_host.old_playtest_destroy_player_suppressed) return 1;
    replacement = g_host.old_playtest_destroy_player_original;
    if (!patch_x86_code(g_host.play_layer_destroy_player, &replacement, 1u))
        return 0;
    g_host.old_playtest_destroy_player_suppressed = 0;
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_STARTUP_DEATH_GUARD restored");
    return 1;
}

static void suppress_old_playtest_end_portal(void *play_layer, float player_x) {
    if (!play_layer || !g_host.ccnode_set_position ||
        !g_host.ccnode_get_position_y) return;
    if (!g_host.old_playtest_end_portal &&
        !g_host.old_playtest_end_portal_scanned) {
        unsigned int visited = 0u;
        g_host.old_playtest_end_portal_scanned = 1;
        g_host.old_playtest_end_portal = find_old_playtest_descendant_by_type(
            play_layer, "EndPortalObject", 0u, &visited);
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_END_PORTAL %s nodes=%u",
                    g_host.old_playtest_end_portal ? "found" : "not-found",
                    visited);
    }
    if (g_host.old_playtest_end_portal &&
        memory_range_is_readable(g_host.old_playtest_end_portal, sizeof(void *))) {
        const float y = g_host.ccnode_get_position_y(g_host.old_playtest_end_portal);
        const GdCcPoint point = {player_x + OLD_PLAYTEST_END_PORTAL_AHEAD_X, y};
        if (g_host.end_portal_set_position)
            g_host.end_portal_set_position(g_host.old_playtest_end_portal, &point);
        else
            g_host.ccnode_set_position(g_host.old_playtest_end_portal, point.x, point.y);
    }
}

static void log_old_playtest_unavailable(const char *reason) {
    if (g_host.old_playtest_unavailable_logged) return;
    g_host.old_playtest_unavailable_logged = 1;
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_UNAVAILABLE reason=%s",
                reason ? reason : "unknown");
}

static int old_playtest_symbols_ready(void) {
    return g_host.level_editor_get_level &&
           g_host.level_editor_get_level_string &&
           g_host.gj_game_level_set_level_string && g_host.gj_game_level_create &&
           g_host.ccobject_retain &&
           g_host.game_manager_get_edit_mode && g_host.game_manager_set_edit_mode &&
           g_host.play_layer_create && g_host.play_layer_start_game &&
           g_host.play_layer_reset_level && g_host.play_layer_get_test_mode &&
           g_host.sprite_create_with_frame && g_host.cc_menu_create &&
           g_host.menu_item_sprite_extra_create && g_host.ccmenu_set_enabled &&
           g_host.ccnode_set_visible &&
           g_host.ccnode_set_position && g_host.ccnode_get_position_x &&
           g_host.ccnode_get_position_y && g_host.ccnode_get_rotation &&
           g_host.ccnode_get_scale_x && g_host.ccnode_get_scale_y &&
           g_host.ccnode_set_scale && g_host.ccnode_set_rotation &&
           g_host.ccnode_set_scale_x && g_host.ccnode_set_scale_y &&
           g_host.ccnode_create && g_host.ccnode_remove &&
           g_host.cclayer_set_touch_enabled &&
           g_host.level_editor_get_game_layer && g_host.play_layer_get_player &&
           g_host.play_layer_get_game_layer && g_host.player_get_is_dead &&
           g_host.end_portal_trigger_object &&
           g_host.sprite_create_file &&
           g_host.sprite_set_color &&
           (g_host.ccnode_add_child_z || g_host.ccnode_add_child);
}

static void __cdecl old_playtest_button_callback(void *self, void *sender) {
    (void)self;
    (void)sender;
    InterlockedExchange(&g_host.old_playtest_request,
                        g_host.old_playtest_layer ? 2 : 1);
}

static void *create_old_playtest_item(void *target, const char *frame,
                                      float sprite_scale) {
    void *normal;
    void *selected;
    if (!g_host.sprite_create_with_frame ||
        !g_host.menu_item_sprite_extra_create) return NULL;
    normal = g_host.sprite_create_with_frame(frame);
    selected = g_host.sprite_create_with_frame(frame);
    if (!normal || !selected) return NULL;
    /* Scale the actual normal/selected sprites before CCMenuItemSpriteExtra is
       created. Its pressed-state animation is then free to scale the menu item
       around 1.0 without ever snapping the icon back to the old huge size. */
    if (sprite_scale > 0.0f && sprite_scale != 1.0f) {
        g_host.ccnode_set_scale(normal, sprite_scale);
        g_host.ccnode_set_scale(selected, sprite_scale);
    }
    return g_host.menu_item_sprite_extra_create(
        normal, selected, target,
        (uintptr_t)(void (__cdecl *)(void *, void *))old_playtest_button_callback,
        0);
}

static int start_old_playtest_preserving_first_attempt(void *play_layer) {
    unsigned char original;
    unsigned char replacement = 0xc3u; /* RET */
    if (!play_layer || !g_host.play_layer_start_game) return 0;

    /* resetLevel() must run: besides resetting the player it initializes the
       spawn queues consumed by checkSpawnObjects().  newera6 skipped the whole
       reset, which produced a null CCArray crash on the first frame.  Suppress
       only updateAttempts() for this one startGame() so the full gameplay reset
       happens without advancing the visible Attempt counter. */
    if (!g_host.play_layer_update_attempts ||
        !memory_range_is_readable(g_host.play_layer_update_attempts, 1u)) {
        g_host.play_layer_start_game(play_layer);
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_FIRST_ATTEMPT_FALLBACK updateAttempts=missing resetLevel=full");
        return 1;
    }
    original = *(const unsigned char *)g_host.play_layer_update_attempts;
    if (!patch_x86_code(g_host.play_layer_update_attempts, &replacement, 1u))
        return 0;
    g_host.play_layer_start_game(play_layer);
    if (!patch_x86_code(g_host.play_layer_update_attempts, &original, 1u))
        return 0;
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_FIRST_ATTEMPT_PRESERVED updateAttempts=suppressed-once resetLevel=full");
    return 1;
}

static int old_playtest_detect_mode(void) {
    if (!g_host.old_playtest_player) return OLD_PLAYTEST_MODE_CUBE;
    /* UFO/bird may also be considered a flying mode by old builds, so test
       the more specific modes before the generic ship/fly flag. */
    if (g_host.player_get_bird_mode &&
        g_host.player_get_bird_mode(g_host.old_playtest_player))
        return OLD_PLAYTEST_MODE_BIRD;
    if (g_host.player_get_roll_mode &&
        g_host.player_get_roll_mode(g_host.old_playtest_player))
        return OLD_PLAYTEST_MODE_BALL;
    if (g_host.player_get_fly_mode &&
        g_host.player_get_fly_mode(g_host.old_playtest_player))
        return OLD_PLAYTEST_MODE_SHIP;
    return OLD_PLAYTEST_MODE_CUBE;
}

static int old_playtest_icon_for_mode(int mode) {
    void *manager = g_host.game_manager_shared_state
                        ? g_host.game_manager_shared_state() : NULL;
    int icon = 1;
    if (!manager) return icon;
    if (mode == OLD_PLAYTEST_MODE_SHIP && g_host.game_manager_get_player_ship)
        icon = g_host.game_manager_get_player_ship(manager);
    else if (mode == OLD_PLAYTEST_MODE_BALL && g_host.game_manager_get_player_ball)
        icon = g_host.game_manager_get_player_ball(manager);
    else if (mode == OLD_PLAYTEST_MODE_BIRD && g_host.game_manager_get_player_bird)
        icon = g_host.game_manager_get_player_bird(manager);
    else if (g_host.game_manager_get_player_frame)
        icon = g_host.game_manager_get_player_frame(manager);
    if (icon < 0 || icon > 99) icon = 1;
    if ((mode == OLD_PLAYTEST_MODE_SHIP || mode == OLD_PLAYTEST_MODE_BIRD) &&
        icon == 0) icon = 1;
    return icon;
}

static void remove_old_playtest_proxy_visuals(void) {
    /* All proxy sprites live under one local-space root. Removing that root
       removes every vehicle/cube layer together and avoids touching children
       individually while Cocos is walking the scene tree. */
    if (g_host.old_playtest_proxy_root &&
        memory_range_is_readable(g_host.old_playtest_proxy_root, sizeof(void *)))
        g_host.ccnode_remove(g_host.old_playtest_proxy_root, 1);
    g_host.old_playtest_proxy_root = NULL;
    g_host.old_playtest_proxy_primary = NULL;
    g_host.old_playtest_proxy_secondary = NULL;
    g_host.old_playtest_proxy_tertiary = NULL;
    g_host.old_playtest_proxy_quaternary = NULL;
    g_host.old_playtest_proxy_quinary = NULL;
}

static int old_playtest_get_player_colors(GdCcColor3B *primary,
                                           GdCcColor3B *secondary) {
    void *manager;
    int primary_idx, secondary_idx;
    if (!primary || !secondary) return 0;
    *primary = (GdCcColor3B){255u, 255u, 255u};
    *secondary = (GdCcColor3B){255u, 255u, 255u};
    if (!g_host.game_manager_shared_state ||
        !g_host.game_manager_get_player_color ||
        !g_host.game_manager_get_player_color2 ||
        !g_host.game_manager_color_for_idx) return 0;
    manager = g_host.game_manager_shared_state();
    if (!manager) return 0;
    primary_idx = g_host.game_manager_get_player_color(manager);
    secondary_idx = g_host.game_manager_get_player_color2(manager);
    gd_call_sret_color3b_x86(g_host.game_manager_color_for_idx,
                             primary, manager, primary_idx);
    gd_call_sret_color3b_x86(g_host.game_manager_color_for_idx,
                             secondary, manager, secondary_idx);
    return 1;
}

static int rebuild_old_playtest_proxy_visuals(int force) {
    int mode, icon, cube_icon;
    char name[64];
    const char *prefix;
    GdCcColor3B primary_color = {255u, 255u, 255u};
    GdCcColor3B secondary_color = {255u, 255u, 255u};
    GdCcColor3B white = {255u, 255u, 255u};
    void *manager;
    float vehicle_y = 0.0f;
    if (!g_host.old_playtest_player || !g_host.old_playtest_trail) return 1;
    mode = old_playtest_detect_mode();
    icon = old_playtest_icon_for_mode(mode);
    if (!force && g_host.old_playtest_proxy_root &&
        mode == g_host.old_playtest_proxy_mode &&
        icon == g_host.old_playtest_proxy_icon) return 1;

    remove_old_playtest_proxy_visuals();
    (void)old_playtest_get_player_colors(&primary_color, &secondary_color);
    g_host.old_playtest_proxy_root = g_host.ccnode_create();
    if (!g_host.old_playtest_proxy_root ||
        !add_extras_child(g_host.old_playtest_trail,
                          g_host.old_playtest_proxy_root, 9998)) {
        g_host.old_playtest_proxy_root = NULL;
        return 0;
    }

    if (mode == OLD_PLAYTEST_MODE_SHIP || mode == OLD_PLAYTEST_MODE_BIRD) {
        /* Match the old PlayerObject layout instead of stacking everything at
           one world-space point. 1.7 uses a cube at local (0,+5) scaled to
           0.55; ship is at (0,-5), UFO/bird at (0,-7). The local proxy root
           is rotated/scaled as a whole, so these offsets rotate correctly. */
        vehicle_y = mode == OLD_PLAYTEST_MODE_SHIP ? -5.0f : -7.0f;
        snprintf(name, sizeof(name),
                 mode == OLD_PLAYTEST_MODE_SHIP ? "ship_%02d_001.png" :
                                                  "bird_%02d_001.png", icon);
        g_host.old_playtest_proxy_primary = g_host.sprite_create_with_frame(name);
        if (mode == OLD_PLAYTEST_MODE_BIRD || g_host.player_get_bird_mode) {
            if (mode == OLD_PLAYTEST_MODE_SHIP)
                snprintf(name, sizeof(name), "ship_%02d_2_001.png", icon);
            else
                snprintf(name, sizeof(name), "bird_%02d_2_001.png", icon);
            g_host.old_playtest_proxy_secondary = g_host.sprite_create_with_frame(name);
        }
        if (mode == OLD_PLAYTEST_MODE_BIRD) {
            snprintf(name, sizeof(name), "bird_%02d_3_001.png", icon);
            g_host.old_playtest_proxy_tertiary = g_host.sprite_create_with_frame(name);
        }

        manager = g_host.game_manager_shared_state
                      ? g_host.game_manager_shared_state() : NULL;
        cube_icon = (manager && g_host.game_manager_get_player_frame)
                        ? g_host.game_manager_get_player_frame(manager) : 1;
        if (cube_icon < 1 || cube_icon > 99) cube_icon = 1;
        snprintf(name, sizeof(name), "player_%02d_001.png", cube_icon);
        g_host.old_playtest_proxy_quaternary = g_host.sprite_create_with_frame(name);
        snprintf(name, sizeof(name), "player_%02d_2_001.png", cube_icon);
        g_host.old_playtest_proxy_quinary = g_host.sprite_create_with_frame(name);

        if (g_host.old_playtest_proxy_primary) {
            g_host.sprite_set_color(g_host.old_playtest_proxy_primary, &primary_color);
            g_host.ccnode_set_position(g_host.old_playtest_proxy_primary, 0.0f, vehicle_y);
            if (!add_extras_child(g_host.old_playtest_proxy_root,
                                  g_host.old_playtest_proxy_primary, 0)) return 0;
        }
        if (g_host.old_playtest_proxy_secondary) {
            g_host.sprite_set_color(g_host.old_playtest_proxy_secondary, &secondary_color);
            g_host.ccnode_set_position(g_host.old_playtest_proxy_secondary, 0.0f, vehicle_y);
            if (!add_extras_child(g_host.old_playtest_proxy_root,
                                  g_host.old_playtest_proxy_secondary, 1)) return 0;
        }
        if (g_host.old_playtest_proxy_tertiary) {
            /* bird _3 is a neutral detail layer in the original object */
            g_host.sprite_set_color(g_host.old_playtest_proxy_tertiary, &white);
            g_host.ccnode_set_position(g_host.old_playtest_proxy_tertiary, 0.0f, vehicle_y);
            if (!add_extras_child(g_host.old_playtest_proxy_root,
                                  g_host.old_playtest_proxy_tertiary, 2)) return 0;
        }
        if (g_host.old_playtest_proxy_quaternary) {
            g_host.sprite_set_color(g_host.old_playtest_proxy_quaternary, &primary_color);
            g_host.ccnode_set_position(g_host.old_playtest_proxy_quaternary, 0.0f, 5.0f);
            g_host.ccnode_set_scale_x(g_host.old_playtest_proxy_quaternary, 0.55f);
            g_host.ccnode_set_scale_y(g_host.old_playtest_proxy_quaternary, 0.55f);
            if (!add_extras_child(g_host.old_playtest_proxy_root,
                                  g_host.old_playtest_proxy_quaternary, 3)) return 0;
        }
        if (g_host.old_playtest_proxy_quinary) {
            g_host.sprite_set_color(g_host.old_playtest_proxy_quinary, &secondary_color);
            g_host.ccnode_set_position(g_host.old_playtest_proxy_quinary, 0.0f, 5.0f);
            g_host.ccnode_set_scale_x(g_host.old_playtest_proxy_quinary, 0.55f);
            g_host.ccnode_set_scale_y(g_host.old_playtest_proxy_quinary, 0.55f);
            if (!add_extras_child(g_host.old_playtest_proxy_root,
                                  g_host.old_playtest_proxy_quinary, 4)) return 0;
        }
    } else {
        prefix = mode == OLD_PLAYTEST_MODE_BALL ? "player_ball" : "player";
        snprintf(name, sizeof(name), "%s_%02d_001.png", prefix, icon);
        g_host.old_playtest_proxy_primary = g_host.sprite_create_with_frame(name);
        snprintf(name, sizeof(name), "%s_%02d_2_001.png", prefix, icon);
        g_host.old_playtest_proxy_secondary = g_host.sprite_create_with_frame(name);
        if (!g_host.old_playtest_proxy_primary) {
            icon = mode == OLD_PLAYTEST_MODE_BALL ? 0 : 1;
            snprintf(name, sizeof(name), "%s_%02d_001.png", prefix, icon);
            g_host.old_playtest_proxy_primary = g_host.sprite_create_with_frame(name);
            snprintf(name, sizeof(name), "%s_%02d_2_001.png", prefix, icon);
            g_host.old_playtest_proxy_secondary = g_host.sprite_create_with_frame(name);
        }
        if (!g_host.old_playtest_proxy_primary && mode != OLD_PLAYTEST_MODE_CUBE) {
            mode = OLD_PLAYTEST_MODE_CUBE;
            icon = 1;
            g_host.old_playtest_proxy_primary =
                g_host.sprite_create_with_frame("player_01_001.png");
            g_host.old_playtest_proxy_secondary =
                g_host.sprite_create_with_frame("player_01_2_001.png");
        }
        if (g_host.old_playtest_proxy_primary) {
            g_host.sprite_set_color(g_host.old_playtest_proxy_primary, &primary_color);
            if (!add_extras_child(g_host.old_playtest_proxy_root,
                                  g_host.old_playtest_proxy_primary, 0)) return 0;
        }
        if (g_host.old_playtest_proxy_secondary) {
            g_host.sprite_set_color(g_host.old_playtest_proxy_secondary, &secondary_color);
            if (!add_extras_child(g_host.old_playtest_proxy_root,
                                  g_host.old_playtest_proxy_secondary, 1)) return 0;
        }
    }
    if (!g_host.old_playtest_proxy_primary) {
        remove_old_playtest_proxy_visuals();
        return 0;
    }
    g_host.old_playtest_proxy_mode = mode;
    g_host.old_playtest_proxy_icon = icon;
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_PROXY_MODE mode=%s icon=%d colors=player layout=authentic-local",
                mode == OLD_PLAYTEST_MODE_SHIP ? "ship" :
                mode == OLD_PLAYTEST_MODE_BALL ? "ball" :
                mode == OLD_PLAYTEST_MODE_BIRD ? "bird" : "cube",
                icon);
    return 1;
}

static void restore_old_playtest_edit_mode(void);
static void release_old_playtest_editor_control_refs(int restore_state);

static int ensure_old_playtest_button(void) {
    void *editor_ui;
    void *menu;
    void *button;
    void *stop_button;
    if (!gd_settings_old_ver_playtest() ||
        !gd_settings_old_ver_playtest_supported_version()) return 1;
    /* The old 250 ms poll made the scene-overlay button visibly pop in after
       the editor itself. EditorUI discovery is scene-cached, so checking each
       host frame is cheap once the editor has been found. */
    editor_ui = find_active_editor_ui();
    if (g_host.old_playtest_scene != g_host.active_scene_root) {
        g_host.old_playtest_scene = g_host.active_scene_root;
        g_host.old_playtest_play_menu = NULL;
        g_host.old_playtest_play_button = NULL;
        g_host.old_playtest_stop_button = NULL;
        if (g_host.old_playtest_layer) {
            (void)set_old_playtest_reset_level_suppressed(0);
            (void)set_old_playtest_destroy_player_suppressed(0);
            (void)set_old_playtest_end_trigger_suppressed(0);
            (void)set_old_playtest_mirror_suppressed(0);
            restore_old_playtest_edit_mode();
            audio_stop_background();
            g_host.old_playtest_layer = NULL;
            g_host.old_playtest_ui = NULL;
            g_host.old_playtest_player = NULL;
            g_host.old_playtest_play_game_layer = NULL;
            g_host.old_playtest_editor_game_layer = NULL;
            g_host.old_playtest_proxy_root = NULL;
            g_host.old_playtest_proxy_primary = NULL;
            g_host.old_playtest_proxy_secondary = NULL;
            g_host.old_playtest_proxy_tertiary = NULL;
            g_host.old_playtest_proxy_quaternary = NULL;
            g_host.old_playtest_proxy_quinary = NULL;
            g_host.old_playtest_proxy_mode = -1;
            g_host.old_playtest_proxy_icon = -1;
            g_host.old_playtest_proxy_poll_counter = 0u;
            g_host.old_playtest_level_clone = NULL;
            g_host.old_playtest_previous_play_layer = NULL;
            g_host.old_playtest_end_portal = NULL;
            g_host.old_playtest_end_portal_scanned = 0;
            InterlockedExchange(&g_host.old_playtest_request, 0);
        }
        g_host.old_playtest_trail = NULL;
        release_old_playtest_editor_control_refs(0);
        g_host.old_playtest_camera_fallback_logged = 0;
    }
    if (!editor_ui || g_host.old_playtest_layer ||
        g_host.old_playtest_play_button) return 1;
    if (!old_playtest_symbols_ready()) {
        log_old_playtest_unavailable("missing-game-symbol");
        return 1;
    }
    menu = g_host.cc_menu_create();
    button = create_old_playtest_item(editor_ui, "GJ_playBtn2_001.png",
                                      OLD_PLAYTEST_PLAY_SPRITE_SCALE);
    stop_button = create_old_playtest_item(editor_ui, "GJ_pauseBtn_001.png", 1.0f);
    if (!menu || !button || !stop_button ||
        !add_extras_child(menu, button, 0) ||
        !add_extras_child(menu, stop_button, 1)) return 0;
    g_host.ccnode_set_position(menu, 0.0f, 0.0f);
    g_host.ccnode_set_position(button, OLD_PLAYTEST_BUTTON_X, OLD_PLAYTEST_BUTTON_Y);
    g_host.ccnode_set_position(stop_button, OLD_PLAYTEST_BUTTON_X, OLD_PLAYTEST_BUTTON_Y);
    g_host.ccnode_set_visible(stop_button, 0);
    /* Keep wrapper controls completely outside LevelEditorLayer/EditorUI.
       Older editors assume their own child arrays contain only game-owned UI
       nodes; transient wrapper children were the last common mutation before
       the reproducible post-play object-placement crash. */
    if (!add_extras_child(g_host.active_scene_root, menu, 30000)) return 0;
    g_host.old_playtest_play_menu = menu;
    g_host.old_playtest_play_button = button;
    g_host.old_playtest_stop_button = stop_button;
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_BUTTON_READY mode=scene-overlay sprite=GJ_playBtn2_001.png pause=persistent");
    return 1;
}

static void clear_old_playtest_trail(void) {
    if (g_host.old_playtest_trail &&
        memory_range_is_readable(g_host.old_playtest_trail, sizeof(void *)))
        g_host.ccnode_remove(g_host.old_playtest_trail, 1);
    g_host.old_playtest_trail = NULL;
    g_host.old_playtest_trail_has_last = 0;
    g_host.old_playtest_trail_segments = 0;
}

static void set_old_playtest_editor_input_enabled(int enabled) {
    if (!g_host.cclayer_set_touch_enabled) return;
    if (!enabled) {
        g_host.old_playtest_editor_ui_touch_was_enabled = 1;
        g_host.old_playtest_editor_layer_touch_was_enabled = 1;
        if (g_host.cclayer_is_touch_enabled) {
            if (g_host.old_playtest_ui &&
                memory_range_is_readable(g_host.old_playtest_ui, sizeof(void *)))
                g_host.old_playtest_editor_ui_touch_was_enabled =
                    g_host.cclayer_is_touch_enabled(g_host.old_playtest_ui) != 0;
            if (g_host.old_playtest_editor &&
                g_host.old_playtest_editor != g_host.old_playtest_ui &&
                memory_range_is_readable(g_host.old_playtest_editor, sizeof(void *)))
                g_host.old_playtest_editor_layer_touch_was_enabled =
                    g_host.cclayer_is_touch_enabled(g_host.old_playtest_editor) != 0;
        }
    }
    if (g_host.old_playtest_ui &&
        memory_range_is_readable(g_host.old_playtest_ui, sizeof(void *)))
        g_host.cclayer_set_touch_enabled(
            g_host.old_playtest_ui,
            enabled ? g_host.old_playtest_editor_ui_touch_was_enabled : 0);
    if (g_host.old_playtest_editor &&
        g_host.old_playtest_editor != g_host.old_playtest_ui &&
        memory_range_is_readable(g_host.old_playtest_editor, sizeof(void *)))
        g_host.cclayer_set_touch_enabled(
            g_host.old_playtest_editor,
            enabled ? g_host.old_playtest_editor_layer_touch_was_enabled : 0);
    g_host.old_playtest_editor_input_suspended = enabled ? 0 : 1;
    if (enabled)
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_EDITOR_INPUT_RESTORED ui=%d layer=%d",
                    g_host.old_playtest_editor_ui_touch_was_enabled,
                    g_host.old_playtest_editor_layer_touch_was_enabled);
    else
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_EDITOR_INPUT_SUSPENDED ui-prev=%d layer-prev=%d",
                    g_host.old_playtest_editor_ui_touch_was_enabled,
                    g_host.old_playtest_editor_layer_touch_was_enabled);
}

static void restore_old_playtest_edit_mode(void) {
    if (g_host.old_playtest_previous_edit_mode_valid &&
        g_host.game_manager_shared_state && g_host.game_manager_set_edit_mode) {
        void *manager = g_host.game_manager_shared_state();
        if (manager) {
            g_host.game_manager_set_edit_mode(
                manager, g_host.old_playtest_previous_edit_mode);
            runtime_log("RESULT: X86_OLD_VER_PLAYTEST_EDIT_MODE_RESTORED value=%d",
                        g_host.old_playtest_previous_edit_mode);
        }
    }
    g_host.old_playtest_previous_edit_mode_valid = 0;
}

static void collect_old_playtest_editor_controls(void *node,
                                                 unsigned int depth,
                                                 unsigned int *visited) {
    unsigned int i, count;
    void *children;
    if (!node || !visited || depth > 16u || *visited >= 4096u ||
        !memory_range_is_readable(node, sizeof(void *))) return;
    ++*visited;

    /* RTTI substring matching must not treat CCMenuItemSpriteExtra as a
       CCMenu. Calling CCMenu::setEnabled() with a CCMenuItem `this` corrupts
       old x86 editor item state/scale (Build/Edit/Delete shrinking after a
       playtest, and earlier selected/purple controls). Only real CCMenu
       containers are suspended. */
    if (g_host.ccmenu_set_enabled &&
        object_type_contains(node, "CCMenu") &&
        !object_type_contains(node, "CCMenuItem") &&
        g_host.old_playtest_editor_menu_count < 128u) {
        unsigned int slot = g_host.old_playtest_editor_menu_count++;
        g_host.ccobject_retain(node);
        g_host.old_playtest_editor_menus[slot] = node;
        g_host.old_playtest_editor_menu_enabled[slot] =
            g_host.ccmenu_is_enabled ? (unsigned char)(g_host.ccmenu_is_enabled(node) != 0) : 1u;
        g_host.ccmenu_set_enabled(node, 0);
    }
    if (!g_host.ccnode_get_children || !g_host.ccnode_get_children_count ||
        !g_host.ccarray_object_at_index) return;
    count = g_host.ccnode_get_children_count(node);
    if (!count || count > 1024u) return;
    children = g_host.ccnode_get_children(node);
    if (!children) return;
    for (i = 0u; i < count && *visited < 4096u; ++i) {
        void *child = g_host.ccarray_object_at_index(children, i);
        if (child) collect_old_playtest_editor_controls(child, depth + 1u, visited);
    }
}

static void release_old_playtest_editor_control_refs(int restore_state) {
    unsigned int i;
    const unsigned int menu_count = g_host.old_playtest_editor_menu_count;
    const unsigned int slider_count = g_host.old_playtest_editor_slider_count;
    for (i = 0u; i < menu_count; ++i) {
        void *menu = g_host.old_playtest_editor_menus[i];
        if (!menu) continue;
        if (restore_state && g_host.ccmenu_set_enabled)
            /* Hidden PlayLayer creation flips GameManager out of edit mode
               before the old editor controls are suspended. Some historical
               builds react by marking their editor CCMenus disabled. Restoring
               that captured false state is what left Build/Edit/Delete locked
               after playtest. Once edit mode is restored, every retained editor
               CCMenu must be enabled again. Hidden/inapplicable buttons remain
               governed by visibility and their callbacks. */
            g_host.ccmenu_set_enabled(menu, 1);
        if (g_host.ccobject_release) g_host.ccobject_release(menu);
    }
    for (i = 0u; i < slider_count; ++i) {
        void *slider = g_host.old_playtest_editor_sliders[i];
        if (!slider) continue;
        if (restore_state && g_host.cclayer_set_touch_enabled)
            g_host.cclayer_set_touch_enabled(
                slider, g_host.old_playtest_editor_slider_touch_enabled[i] != 0);
        if (g_host.ccobject_release) g_host.ccobject_release(slider);
    }
    memset(g_host.old_playtest_editor_menus, 0, sizeof(g_host.old_playtest_editor_menus));
    memset(g_host.old_playtest_editor_sliders, 0, sizeof(g_host.old_playtest_editor_sliders));
    g_host.old_playtest_editor_menu_count = 0u;
    g_host.old_playtest_editor_slider_count = 0u;
}

static void set_old_playtest_editor_controls_enabled(int enabled) {
    if (!enabled) {
        unsigned int visited = 0u;
        /* The old editor may rebuild descendants while PlayLayer temporarily
           owns GameManager state. Keep a reference to every control we disable
           so stop never has to recursively walk a half-rebuilt Cocos tree. */
        release_old_playtest_editor_control_refs(0);
        if (g_host.old_playtest_editor && g_host.ccobject_retain &&
            g_host.ccobject_release)
            collect_old_playtest_editor_controls(g_host.old_playtest_editor, 0u, &visited);
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_EDITOR_CONTROLS_SUSPENDED menus=%u sliders=%u retained=1 slider-policy=untouched",
                    g_host.old_playtest_editor_menu_count,
                    g_host.old_playtest_editor_slider_count);
        return;
    }
    {
        const unsigned int menu_count = g_host.old_playtest_editor_menu_count;
        const unsigned int slider_count = g_host.old_playtest_editor_slider_count;
        release_old_playtest_editor_control_refs(1);
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_EDITOR_CONTROLS_RESTORED menus=%u sliders=%u mode=retained-force-enabled slider-policy=untouched",
                    menu_count, slider_count);
    }
}

static int read_old_playtest_real_camera(float *world_x, float *world_y) {
    void *camera;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!g_host.old_playtest_play_game_layer ||
        !g_host.ccnode_get_camera || !g_host.cccamera_get_center_xyz)
        return 0;
    camera = g_host.ccnode_get_camera(g_host.old_playtest_play_game_layer);
    if (!camera || !memory_range_is_readable(camera, sizeof(void *))) return 0;
    g_host.cccamera_get_center_xyz(camera, &x, &y, &z);
    if (!isfinite(x) || !isfinite(y) ||
        fabsf(x) > 100000.0f || fabsf(y) > 100000.0f)
        return 0;
    if (world_x) *world_x = x;
    if (world_y) *world_y = y;
    return 1;
}

static int apply_old_playtest_camera(float player_x) {
    int mode = g_host.old_playtest_proxy_mode;
    float base_y;
    float camera_x;
    float editor_camera_x, editor_camera_y;
    float overlay_camera_x, overlay_camera_y;
    float zoom;

    if (!g_host.old_playtest_editor_game_layer ||
        !g_host.old_playtest_play_game_layer ||
        !g_host.old_playtest_player)
        return 0;

    /*
       NEWERA15 IS THE BASELINE.

       Horizontal playtest scrolling is the known-good pre-newera11 path. Cube
       Y never reads PlayerObject::Y. Constrained modes sample the historical
       PlayLayer CCCamera exactly once on mode entry, then freeze that corridor
       center so ship/ball/UFO never vertically follow the player.
    */
    camera_x = OLD_PLAYTEST_CAMERA_ANCHOR_X - player_x;
    if (camera_x > 0.0f) camera_x = 0.0f;
    base_y = g_host.ccnode_get_position_y(g_host.old_playtest_play_game_layer);

    zoom = mode == OLD_PLAYTEST_MODE_CUBE
        ? OLD_PLAYTEST_CUBE_ZOOM_OUT_SCALE
        : OLD_PLAYTEST_CONSTRAINED_ZOOM_OUT_SCALE;

    /*
       IMPORTANT: LevelEditorLayer's game layer and the scene-root proxy/trail
       do NOT scale around the same local pivot. The game layer scales around
       the logical 570x320 center (285,160); the plain CCNode overlay scales
       around (0,0). Giving both nodes the same position is exactly what made
       the cube render below a block at the same world Y in newera16-newera21.

       Compute the editor-layer transform first, then add (1-scale)*pivot to the
       overlay transform. For any world point P, both paths then produce the
       exact same screen coordinate.
    */
    editor_camera_x = camera_x +
        (1.0f - zoom) * (player_x - OLD_PLAYTEST_CAMERA_PIVOT_X);
    overlay_camera_x = editor_camera_x +
        (1.0f - zoom) * OLD_PLAYTEST_CAMERA_PIVOT_X;

    if (mode == OLD_PLAYTEST_MODE_CUBE) {
        g_host.old_playtest_constrained_camera_valid = 0;
        g_host.old_playtest_constrained_camera_mode = -1;
        /* Fixed-Y cube camera. Ground world Y=105 keeps the same relationship
           to level objects, then the whole play viewport is lifted 25 points
           clear of the editor selector. Jumping cannot alter this value. */
        editor_camera_y = base_y +
            (1.0f - zoom) *
                (OLD_PLAYTEST_CUBE_GROUND_WORLD_Y - OLD_PLAYTEST_CAMERA_PIVOT_Y) +
            OLD_PLAYTEST_CUBE_CAMERA_LIFT_Y;
    } else {
        /* NEWERA24: do not guess a fixed world corridor and do not follow the
           player. NEWERA15 already proved that the hidden PlayLayer CCCamera
           knows the correct vertical corridor for the exact historical build
           and portal location. Capture that camera ONCE on gamemode entry,
           freeze it, then zoom out around the logical screen center.

           This is deliberately not a live CCCamera mirror: after the capture,
           ship/ball/UFO movement cannot move the editor camera. */
        if (!g_host.old_playtest_constrained_camera_valid ||
            g_host.old_playtest_constrained_camera_mode != mode) {
            float real_camera_x = 0.0f, real_camera_y = 0.0f;
            if (read_old_playtest_real_camera(&real_camera_x, &real_camera_y)) {
                (void)real_camera_x;
                g_host.old_playtest_constrained_camera_y = base_y - real_camera_y;
                g_host.old_playtest_constrained_camera_valid = 1;
                g_host.old_playtest_constrained_camera_mode = mode;
                runtime_log(
                    "RESULT: X86_OLD_VER_PLAYTEST_STATIC_CORRIDOR_CAPTURE mode=%s source=PlayLayer-CCCamera camera-y=%.3f zoom=%.2f frozen=1",
                    mode == OLD_PLAYTEST_MODE_SHIP ? "ship" :
                    mode == OLD_PLAYTEST_MODE_BALL ? "ball" : "bird",
                    g_host.old_playtest_constrained_camera_y,
                    OLD_PLAYTEST_CONSTRAINED_ZOOM_OUT_SCALE);
            } else {
                /* Capability fallback: freeze the hidden game-layer Y once.
                   Never read PlayerObject::Y here, so this still cannot become
                   a player-following camera. */
                g_host.old_playtest_constrained_camera_y = base_y;
                g_host.old_playtest_constrained_camera_valid = 1;
                g_host.old_playtest_constrained_camera_mode = mode;
                runtime_log(
                    "RESULT: X86_OLD_VER_PLAYTEST_STATIC_CORRIDOR_CAPTURE mode=%s source=game-layer-y camera-y=%.3f zoom=%.2f frozen=1",
                    mode == OLD_PLAYTEST_MODE_SHIP ? "ship" :
                    mode == OLD_PLAYTEST_MODE_BALL ? "ball" : "bird",
                    g_host.old_playtest_constrained_camera_y,
                    OLD_PLAYTEST_CONSTRAINED_ZOOM_OUT_SCALE);
            }
        }

        /* Zoom the frozen NEWERA15 camera around screen Y=160. For an unscaled
           camera translation C, scaling the whole viewport around the screen
           center requires the editor-layer translation z*C. Ship framing is
           already correct; ball and UFO/bird sit visually low in the old
           editor bridge, so lift only those two STATIC cameras. This is a
           constant framing correction and never reads PlayerObject::Y. */
        editor_camera_y = zoom * g_host.old_playtest_constrained_camera_y;
        if (mode == OLD_PLAYTEST_MODE_BALL ||
            mode == OLD_PLAYTEST_MODE_BIRD)
            editor_camera_y += OLD_PLAYTEST_BALL_UFO_CAMERA_LIFT_Y;
    }
    overlay_camera_y = editor_camera_y +
        (1.0f - zoom) * OLD_PLAYTEST_CAMERA_PIVOT_Y;

    g_host.ccnode_set_scale_x(g_host.old_playtest_editor_game_layer, zoom);
    g_host.ccnode_set_scale_y(g_host.old_playtest_editor_game_layer, zoom);
    g_host.ccnode_set_position(g_host.old_playtest_editor_game_layer,
                               editor_camera_x, editor_camera_y);

    if (g_host.old_playtest_trail) {
        g_host.ccnode_set_scale_x(g_host.old_playtest_trail, zoom);
        g_host.ccnode_set_scale_y(g_host.old_playtest_trail, zoom);
        g_host.ccnode_set_position(g_host.old_playtest_trail,
                                   overlay_camera_x, overlay_camera_y);
    }
    return 1;
}

static void position_old_playtest_line_sprite(void *sprite,
                                               float x1, float y1,
                                               float x2, float y2,
                                               float thickness) {
    float dx, dy, length;
    if (!sprite) return;
    dx = x2 - x1;
    dy = y2 - y1;
    length = sqrtf(dx * dx + dy * dy);
    if (length < 0.05f) {
        g_host.ccnode_set_visible(sprite, 0);
        return;
    }
    g_host.ccnode_set_visible(sprite, 1);
    g_host.ccnode_set_position(sprite, (x1 + x2) * 0.5f, (y1 + y2) * 0.5f);
    g_host.ccnode_set_rotation(sprite, -atan2f(dy, dx) * OLD_PLAYTEST_RAD_TO_DEG);
    g_host.ccnode_set_scale_x(sprite,
        (length / OLD_PLAYTEST_LINE_TEXTURE_WIDTH) * 1.25f);
    g_host.ccnode_set_scale_y(sprite, thickness);
}

static int append_old_playtest_trail_segment(float x, float y) {
    float dx, dy, distance_squared;
    void *segment;
    GdCcColor3B green = {0u, 255u, 0u};
    if (!g_host.old_playtest_trail) return 1;
    if (!g_host.old_playtest_trail_has_last) {
        g_host.old_playtest_trail_last_x = x;
        g_host.old_playtest_trail_last_y = y;
        g_host.old_playtest_trail_has_last = 1;
        return 1;
    }
    dx = x - g_host.old_playtest_trail_last_x;
    dy = y - g_host.old_playtest_trail_last_y;
    distance_squared = dx * dx + dy * dy;
    if (distance_squared < OLD_PLAYTEST_TRAIL_STEP * OLD_PLAYTEST_TRAIL_STEP)
        return 1;
    if (dx > 192.0f || dx < -192.0f || dy > 192.0f || dy < -192.0f ||
        g_host.old_playtest_trail_segments >= OLD_PLAYTEST_TRAIL_MAX_SEGMENTS) {
        g_host.old_playtest_trail_last_x = x;
        g_host.old_playtest_trail_last_y = y;
        return 1;
    }
    segment = g_host.sprite_create_file("square.png");
    if (!segment) return 0;
    g_host.sprite_set_color(segment, &green);
    position_old_playtest_line_sprite(segment,
                                      g_host.old_playtest_trail_last_x,
                                      g_host.old_playtest_trail_last_y,
                                      x, y, 0.090f);
    if (!add_extras_child(g_host.old_playtest_trail, segment,
                          (int)g_host.old_playtest_trail_segments)) return 0;
    ++g_host.old_playtest_trail_segments;
    g_host.old_playtest_trail_last_x = x;
    g_host.old_playtest_trail_last_y = y;
    return 1;
}

static int update_old_playtest_proxy_transform(void) {
    float x, y, rotation, scale_x, scale_y;
    if (!g_host.old_playtest_player) return 1;
    /* Mode/icon probing crosses the guest ABI and is much more expensive than
       moving the already-built local proxy. Four-frame polling is visually
       immediate at 60 Hz while cutting those calls by 75%. */
    ++g_host.old_playtest_proxy_poll_counter;
    if (!g_host.old_playtest_proxy_root ||
        (g_host.old_playtest_proxy_poll_counter & 3u) == 0u) {
        if (!rebuild_old_playtest_proxy_visuals(0)) return 0;
    }
    if (!g_host.old_playtest_proxy_root) return 1;
    x = g_host.ccnode_get_position_x(g_host.old_playtest_player);
    y = g_host.ccnode_get_position_y(g_host.old_playtest_player);
    rotation = g_host.ccnode_get_rotation(g_host.old_playtest_player);
    scale_x = g_host.ccnode_get_scale_x(g_host.old_playtest_player);
    scale_y = g_host.ccnode_get_scale_y(g_host.old_playtest_player);
    /* PlayerObject world position is already the correct proxy root origin.
       The extra +5 introduced in newera14 is what made cube render low/offset. */
    g_host.ccnode_set_position(g_host.old_playtest_proxy_root, x, y);
    g_host.ccnode_set_rotation(g_host.old_playtest_proxy_root, rotation);
    g_host.ccnode_set_scale_x(g_host.old_playtest_proxy_root, scale_x);
    g_host.ccnode_set_scale_y(g_host.old_playtest_proxy_root, scale_y);
    return append_old_playtest_trail_segment(x, y);
}


static int stop_inline_old_playtest(void);

static int start_inline_old_playtest(void) {
    void *editor_ui;
    void *editor_level;
    void *level_clone;
    void *game_manager = NULL;
    void *level_string = NULL;
    void *play_layer;
    void *player;
    void *play_game_layer;
    void *editor_game_layer;
    unsigned char *test_mode;
    float player_x;
    if (g_host.old_playtest_layer) return 1;
    editor_ui = find_active_editor_ui();
    if (!editor_ui || !g_host.active_editor_layer ||
        !g_host.active_scene_root) return 1;
    if (!old_playtest_symbols_ready()) {
        log_old_playtest_unavailable("missing-game-symbol");
        return 1;
    }
    if (!g_host.old_playtest_test_mode_offset) {
        log_old_playtest_unavailable("unknown-PlayLayer-test-mode-layout");
        return 1;
    }
    clear_old_playtest_trail();
    editor_level = g_host.level_editor_get_level(g_host.active_editor_layer);
    if (!editor_level) return 0;
    gd_call_sret_string_x86(g_host.level_editor_get_level_string,
                            &level_string, g_host.active_editor_layer);
    if (!level_string) return 0;

    /* PlayLayer::init registers itself in GameManager. Capture the editor's
       previous value (normally NULL) so manual PlayLayer teardown cannot leave
       GameManager pointing at a dead hidden test layer. */
    g_host.old_playtest_previous_play_layer = NULL;
    g_host.old_playtest_previous_edit_mode_valid = 0;
    if (g_host.game_manager_shared_state) {
        game_manager = g_host.game_manager_shared_state();
        if (game_manager && g_host.game_manager_get_play_layer)
            g_host.old_playtest_previous_play_layer =
                g_host.game_manager_get_play_layer(game_manager);
        if (game_manager && g_host.game_manager_get_edit_mode &&
            g_host.game_manager_set_edit_mode) {
            g_host.old_playtest_previous_edit_mode =
                g_host.game_manager_get_edit_mode(game_manager);
            g_host.old_playtest_previous_edit_mode_valid = 1;
            runtime_log("RESULT: X86_OLD_VER_PLAYTEST_EDIT_MODE_SAVED value=%d",
                        g_host.old_playtest_previous_edit_mode);
        }
    }

    /* Never hand the editor's live GJGameLevel to PlayLayer. Old PlayLayer
       mutates bookkeeping/state on the level object; sharing it with
       LevelEditorLayer left the editor corrupted after a test and made later
       object/portal placement crash. A private temporary level owns only the
       unsaved level string and the small metadata needed for playback. */
    level_clone = g_host.gj_game_level_create();
    if (!level_clone) return 0;
    if (g_host.ccobject_retain) g_host.ccobject_retain(level_clone);
    if (g_host.gj_game_level_get_audio_track &&
        g_host.gj_game_level_set_audio_track)
        g_host.gj_game_level_set_audio_track(
            level_clone, g_host.gj_game_level_get_audio_track(editor_level));
    if (g_host.gj_game_level_get_level_type &&
        g_host.gj_game_level_set_level_type)
        g_host.gj_game_level_set_level_type(
            level_clone, g_host.gj_game_level_get_level_type(editor_level));
    g_host.gj_game_level_set_level_string(level_clone, &level_string);

    play_layer = g_host.play_layer_create(level_clone);
    if (!play_layer) {
        if (g_host.ccobject_release) g_host.ccobject_release(level_clone);
        restore_old_playtest_edit_mode();
        return 0;
    }
    /* Hold an explicit retain on the hidden PlayLayer. Stop parks it in the
       scene without onExit/destruction while the old editor remains alive. */
    if (g_host.ccobject_retain) g_host.ccobject_retain(play_layer);
    test_mode = (unsigned char *)play_layer +
                g_host.old_playtest_test_mode_offset;
    if (!memory_range_is_readable(test_mode, 1u)) {
        if (g_host.ccobject_release) g_host.ccobject_release(level_clone);
        restore_old_playtest_edit_mode();
        log_old_playtest_unavailable("invalid-PlayLayer-test-mode-layout");
        return 1;
    }
    *test_mode = 1u;

    if (!add_extras_child(g_host.active_scene_root, play_layer, -10000)) {
        if (g_host.ccobject_release) g_host.ccobject_release(level_clone);
        restore_old_playtest_edit_mode();
        return 0;
    }
    if (!start_old_playtest_preserving_first_attempt(play_layer)) {
        g_host.ccnode_remove(play_layer, 1);
        if (g_host.ccobject_release) g_host.ccobject_release(level_clone);
        restore_old_playtest_edit_mode();
        return 0;
    }
    player = g_host.play_layer_get_player(play_layer);
    play_game_layer = g_host.play_layer_get_game_layer(play_layer);
    editor_game_layer = g_host.level_editor_get_game_layer(g_host.active_editor_layer);
    if (!player || !play_game_layer || !editor_game_layer) {
        g_host.ccnode_remove(play_layer, 1);
        if (g_host.ccobject_release) g_host.ccobject_release(level_clone);
        restore_old_playtest_edit_mode();
        return 0;
    }

    g_host.old_playtest_scene = g_host.active_scene_root;
    g_host.old_playtest_editor = g_host.active_editor_layer;
    g_host.old_playtest_ui = editor_ui;
    g_host.old_playtest_layer = play_layer;
    g_host.old_playtest_player = player;
    g_host.old_playtest_play_game_layer = play_game_layer;
    g_host.old_playtest_editor_game_layer = editor_game_layer;
    g_host.old_playtest_level_clone = level_clone;
    g_host.old_playtest_proxy_mode = -1;
    g_host.old_playtest_proxy_icon = -1;
    g_host.old_playtest_proxy_poll_counter = 0u;
    g_host.old_playtest_camera_fallback_logged = 0;
    g_host.old_playtest_constrained_camera_valid = 0;
    g_host.old_playtest_constrained_camera_mode = -1;
    g_host.old_playtest_constrained_camera_y = 0.0f;
    g_host.old_playtest_end_portal = NULL;
    g_host.old_playtest_end_portal_scanned = 0;
    g_host.old_playtest_editor_camera_original_x =
        g_host.ccnode_get_position_x(editor_game_layer);
    g_host.old_playtest_editor_camera_original_y =
        g_host.ccnode_get_position_y(editor_game_layer);
    g_host.old_playtest_editor_camera_original_scale_x =
        g_host.ccnode_get_scale_x(editor_game_layer);
    g_host.old_playtest_editor_camera_original_scale_y =
        g_host.ccnode_get_scale_y(editor_game_layer);
    g_host.old_playtest_editor_camera_original_valid = 1;
    /* Gameplay touches must never fall through to EditorUI/LevelEditorLayer.
       newera9 proved teardown was not the placement-crash source; the editor
       was still consuming every jump touch behind the hidden PlayLayer. */
    set_old_playtest_editor_input_enabled(0);
    set_old_playtest_editor_controls_enabled(0);
    /* startGame() already completed the one reset we actually need, with only
       updateAttempts() suppressed for that synchronous call. From this point
       on, keep destroyPlayer() fully live so solids/hazards kill immediately,
       but suppress FUTURE resetLevel() calls. Old auto-retry schedules a reset
       after death; allowing that reset is the source of the synthetic Attempt 2
       / second-attempt bug. Splitting the two paths preserves lethal collision
       from frame zero without letting the hidden PlayLayer restart itself. */
    if (!set_old_playtest_destroy_player_suppressed(0) ||
        !set_old_playtest_reset_level_suppressed(1)) {
        runtime_log("ERROR: could not install playtest auto-retry reset guard");
        (void)stop_inline_old_playtest();
        return 0;
    }
    g_host.old_playtest_death_grace_until = 0;

    player_x = g_host.ccnode_get_position_x(player);
    suppress_old_playtest_end_portal(play_layer, player_x);

    /* The hidden PlayLayer and every wrapper visual live directly under the
       scene root. Do not mutate LevelEditorLayer's child array at all: the 1.1
       crash is reproducible only after playtest teardown, and its next editor
       placement later walks a bogus low string pointer (0x210). */
    g_host.old_playtest_trail = g_host.ccnode_create();
    if (!g_host.old_playtest_trail ||
        !add_extras_child(g_host.active_scene_root,
                          g_host.old_playtest_trail, 20000)) {
        g_host.old_playtest_trail = NULL;
        (void)stop_inline_old_playtest();
        return 0;
    }
    if (!rebuild_old_playtest_proxy_visuals(1)) {
        (void)stop_inline_old_playtest();
        return 0;
    }

    /* Keep the real PlayerObject in its authentic hidden PlayLayer hierarchy.
       Match the gameplay camera vertically with no extra +20 offset; that
       extra offset pushed screen-limited ship/ball movement off the top. */
    g_host.ccnode_set_visible(play_layer, 0);
    if (!apply_old_playtest_camera(player_x)) {
        (void)stop_inline_old_playtest();
        return 0;
    }
    if (!update_old_playtest_proxy_transform()) {
        (void)stop_inline_old_playtest();
        return 0;
    }

    /* Play/pause controls are persistent scene-root siblings. Starting and
       stopping only flips visibility; no EditorUI child insertion/removal is
       allowed during a playtest. */
    if (g_host.old_playtest_play_button)
        g_host.ccnode_set_visible(g_host.old_playtest_play_button, 0);
    if (g_host.old_playtest_stop_button)
        g_host.ccnode_set_visible(g_host.old_playtest_stop_button, 1);

    if (!set_old_playtest_end_trigger_suppressed(1)) {
        runtime_log("ERROR: could not disable EndPortalObject::triggerObject");
        (void)stop_inline_old_playtest();
        return 0;
    }
    if (!set_old_playtest_mirror_suppressed(1)) {
        runtime_log("ERROR: could not disable PlayLayer::toggleFlipped");
        (void)stop_inline_old_playtest();
        return 0;
    }
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_STARTED mode=editor-bridge-safe unsaved-level=clone first-attempt=preserved player=dynamic-proxy playlayer=hidden end=disabled mirror=disabled camera=newera28-mode-framing collision=live-from-frame0 autoretry-reset=blocked attempt2=blocked editor-zoom-independent=1 scene-isolated=1 editor-input=suspended editor-controls=menus-only slider=untouched");
    return 1;
}

static int stop_inline_old_playtest(void) {
    void *retired_layer;
    void *retired_ui = NULL;
    if (!g_host.old_playtest_layer) {
        if (g_host.old_playtest_editor_input_suspended)
            set_old_playtest_editor_input_enabled(1);
        (void)set_old_playtest_reset_level_suppressed(0);
        (void)set_old_playtest_destroy_player_suppressed(0);
        (void)set_old_playtest_end_trigger_suppressed(0);
        (void)set_old_playtest_mirror_suppressed(0);
        /* Edit mode must be back before menus are force-enabled. */
        restore_old_playtest_edit_mode();
        set_old_playtest_editor_controls_enabled(1);
        if (g_host.old_playtest_play_button)
            g_host.ccnode_set_visible(g_host.old_playtest_play_button, 1);
        if (g_host.old_playtest_stop_button)
            g_host.ccnode_set_visible(g_host.old_playtest_stop_button, 0);
        return 1;
    }
    retired_layer = g_host.old_playtest_layer;
    if (g_host.play_layer_get_ui_layer)
        retired_ui = g_host.play_layer_get_ui_layer(retired_layer);
    audio_stop_background();
    (void)set_old_playtest_reset_level_suppressed(0);
    (void)set_old_playtest_destroy_player_suppressed(0);

    if (g_host.old_playtest_editor_camera_original_valid &&
        g_host.old_playtest_editor_game_layer &&
        memory_range_is_readable(g_host.old_playtest_editor_game_layer,
                                 sizeof(void *))) {
        g_host.ccnode_set_scale_x(g_host.old_playtest_editor_game_layer,
            g_host.old_playtest_editor_camera_original_scale_x);
        g_host.ccnode_set_scale_y(g_host.old_playtest_editor_game_layer,
            g_host.old_playtest_editor_camera_original_scale_y);
        g_host.ccnode_set_position(g_host.old_playtest_editor_game_layer,
            g_host.old_playtest_editor_camera_original_x,
            g_host.old_playtest_editor_camera_original_y);
    }
    g_host.old_playtest_editor_camera_original_valid = 0;
    g_host.old_playtest_camera_fallback_logged = 0;
    if (g_host.old_playtest_editor_input_suspended)
        set_old_playtest_editor_input_enabled(1);

    /* Do not remove ANY playtest node while the old editor scene is alive.
       newera8 still reproduced strlen(0x210) after remove(..., false), proving
       that merely running CCNode::onExit / touch-unregister teardown is enough
       to leave these ancient editor builds in a poisoned state. Park the whole
       subtree in-place, invisible and inert, and let scene destruction own it. */
    if (g_host.old_playtest_trail &&
        memory_range_is_readable(g_host.old_playtest_trail, sizeof(void *)))
        g_host.ccnode_set_visible(g_host.old_playtest_trail, 0);
    if (g_host.old_playtest_player &&
        memory_range_is_readable(g_host.old_playtest_player, sizeof(void *))) {
        if (g_host.ccnode_stop_all_actions)
            g_host.ccnode_stop_all_actions(g_host.old_playtest_player);
        if (g_host.ccnode_unschedule_all_selectors)
            g_host.ccnode_unschedule_all_selectors(g_host.old_playtest_player);
    }
    if (retired_layer && memory_range_is_readable(retired_layer, sizeof(void *))) {
        g_host.ccnode_set_visible(retired_layer, 0);
        if (g_host.cclayer_set_touch_enabled)
            g_host.cclayer_set_touch_enabled(retired_layer, 0);
        if (g_host.cclayer_set_keypad_enabled)
            g_host.cclayer_set_keypad_enabled(retired_layer, 0);
        if (g_host.ccnode_stop_all_actions)
            g_host.ccnode_stop_all_actions(retired_layer);
        if (g_host.ccnode_unschedule_all_selectors)
            g_host.ccnode_unschedule_all_selectors(retired_layer);
        if (g_host.ccnode_unschedule_update)
            g_host.ccnode_unschedule_update(retired_layer);
    }
    if (retired_ui && memory_range_is_readable(retired_ui, sizeof(void *))) {
        if (g_host.cclayer_set_touch_enabled)
            g_host.cclayer_set_touch_enabled(retired_ui, 0);
        if (g_host.cclayer_set_keypad_enabled)
            g_host.cclayer_set_keypad_enabled(retired_ui, 0);
        if (g_host.ccnode_stop_all_actions)
            g_host.ccnode_stop_all_actions(retired_ui);
        if (g_host.ccnode_unschedule_all_selectors)
            g_host.ccnode_unschedule_all_selectors(retired_ui);
    }
    if (g_host.game_manager_shared_state && g_host.game_manager_set_play_layer) {
        void *manager = g_host.game_manager_shared_state();
        if (manager)
            g_host.game_manager_set_play_layer(
                manager, g_host.old_playtest_previous_play_layer);
    }
    g_host.old_playtest_previous_play_layer = NULL;
    restore_old_playtest_edit_mode();
    /* Restore retained CCMenu states only after the real editor state is back.
       The horizontal Slider is intentionally never collected, disabled, retained,
       restored, or force-resynchronized: that path caused the purple selected
       state and x86 freeze after playtest. */
    set_old_playtest_editor_controls_enabled(1);
    /* The scene parent + explicit retain intentionally keep the retired
       PlayLayer and private level alive. This is diagnostic/stability-first:
       zero teardown is much safer than a repeatable post-play editor UAF. */
    g_host.old_playtest_level_clone = NULL;
    if (!set_old_playtest_end_trigger_suppressed(0))
        runtime_log("ERROR: failed to restore EndPortalObject::triggerObject");
    if (!set_old_playtest_mirror_suppressed(0))
        runtime_log("ERROR: failed to restore PlayLayer::toggleFlipped");

    if (g_host.old_playtest_play_button)
        g_host.ccnode_set_visible(g_host.old_playtest_play_button, 1);
    if (g_host.old_playtest_stop_button)
        g_host.ccnode_set_visible(g_host.old_playtest_stop_button, 0);
    g_host.old_playtest_layer = NULL;
    g_host.old_playtest_player = NULL;
    g_host.old_playtest_play_game_layer = NULL;
    g_host.old_playtest_editor_game_layer = NULL;
    g_host.old_playtest_proxy_root = NULL;
    g_host.old_playtest_proxy_primary = NULL;
    g_host.old_playtest_proxy_secondary = NULL;
    g_host.old_playtest_proxy_tertiary = NULL;
    g_host.old_playtest_proxy_quaternary = NULL;
    g_host.old_playtest_proxy_quinary = NULL;
    g_host.old_playtest_proxy_mode = -1;
    g_host.old_playtest_proxy_icon = -1;
    g_host.old_playtest_proxy_poll_counter = 0u;
    g_host.old_playtest_trail = NULL;
    g_host.old_playtest_trail_has_last = 0;
    g_host.old_playtest_trail_segments = 0;
    g_host.old_playtest_end_portal = NULL;
    g_host.old_playtest_end_portal_scanned = 0;
    g_host.old_playtest_death_grace_until = 0;
    g_host.old_playtest_constrained_camera_valid = 0;
    g_host.old_playtest_constrained_camera_mode = -1;
    g_host.old_playtest_constrained_camera_y = 0.0f;
    g_host.gameplay_cache_time = 0;
    runtime_log("RESULT: X86_OLD_VER_PLAYTEST_STOPPED mode=scene-isolated visuals=parked music=stopped end=restored camera=editor-position-scale-restored playlayer=parked-attached-inert no-onExit=1 editor-input=restored editor-controls=menus-force-enabled edit-mode=restored slider=untouched");
    return 1;
}

static int update_inline_old_playtest(void) {
    float player_x;
    void *current_player;
    if (!g_host.old_playtest_layer) return 1;
    /* resetLevel() stays intentionally suppressed for the entire playtest so
       death cannot auto-retry into Attempt 2. destroyPlayer() is never guarded
       by newera26; this fallback only clears a stale legacy death guard. */
    if (g_host.old_playtest_destroy_player_suppressed &&
        GetTickCount64() >= g_host.old_playtest_death_grace_until) {
        if (!set_old_playtest_destroy_player_suppressed(0)) return 0;
    }
    if (!g_host.old_playtest_player || !g_host.old_playtest_play_game_layer ||
        !g_host.old_playtest_editor_game_layer) return 0;
    current_player = g_host.play_layer_get_player(g_host.old_playtest_layer);
    if (!current_player || current_player != g_host.old_playtest_player) {
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_AUTO_STOP reason=player-replaced");
        return stop_inline_old_playtest();
    }
    if (g_host.player_get_is_dead(g_host.old_playtest_player)) {
        runtime_log("RESULT: X86_OLD_VER_PLAYTEST_AUTO_STOP reason=player-dead");
        return stop_inline_old_playtest();
    }

    player_x = g_host.ccnode_get_position_x(g_host.old_playtest_player);
    suppress_old_playtest_end_portal(g_host.old_playtest_layer, player_x);
    if (!update_old_playtest_proxy_transform()) return 0;
    return apply_old_playtest_camera(player_x);
}

static int process_old_playtest_request(void) {
    LONG request = InterlockedExchange(&g_host.old_playtest_request, 0);
    if (request == 1) return start_inline_old_playtest();
    if (request == 2) return stop_inline_old_playtest();
    return 1;
}

static int send_editor_delete_hotkey(void) {
    void *editor_ui;
    if (!gd_settings_editor_controls()) return 0;
    if (g_host.old_playtest_layer) return 0;
    editor_ui = find_active_editor_ui();
    if (!editor_ui) return 0;
    if (g_host.editor_on_delete) {
        g_host.editor_on_delete(editor_ui, editor_ui);
        runtime_log("RESULT: X86_EDITOR_DELETE key=DELETE path=sender");
        return 1;
    }
    if (g_host.editor_on_delete_no_sender) {
        g_host.editor_on_delete_no_sender(editor_ui);
        runtime_log("RESULT: X86_EDITOR_DELETE key=DELETE path=no-sender");
        return 1;
    }
    runtime_log("RESULT: X86_EDITOR_DELETE_UNAVAILABLE reason=missing-onDelete-symbol");
    return 1;
}

static int send_editor_hotkey(int tag, int virtual_key) {
    void *editor_ui;
    int old_tag;
    const int movement = tag >= 1 && tag <= 8;
    EditorTransformObjectCallFunction sender;
    EditorTransformEditCommandFunction direct;

    if (!gd_settings_editor_controls()) return 0;
    if (g_host.old_playtest_layer) return 0;
    editor_ui = find_active_editor_ui();
    if (!editor_ui) return 0;

    /* The move buttons and rotate/flip buttons use different EditorUI
       callbacks in the real game. Keep those paths separate and never route
       desktop editor shortcuts through EditorUI::keyDown. */
    sender = movement
        ? (EditorTransformObjectCallFunction)g_host.editor_move_object_call
        : g_host.editor_transform_object_call;
    direct = movement
        ? (EditorTransformEditCommandFunction)g_host.editor_move_edit_command
        : g_host.editor_transform_edit_command;

    if (direct) {
        direct(editor_ui, tag);
        runtime_log("Editor controls: key=%c tag=%d family=%s via direct EditCommand",
                    virtual_key, tag, movement ? "move" : "transform");
        return 1;
    }
    if (!g_host.ccnode_get_tag || !g_host.ccnode_set_tag || !sender) {
        runtime_log("Editor controls: active editor found but %s callback is unavailable",
                    movement ? "move" : "transform");
        return 1;
    }
    old_tag = g_host.ccnode_get_tag(editor_ui);
    g_host.ccnode_set_tag(editor_ui, tag);
    sender(editor_ui, editor_ui);
    g_host.ccnode_set_tag(editor_ui, old_tag);
    runtime_log("Editor controls: key=%c tag=%d family=%s via sender tag",
                virtual_key, tag, movement ? "move" : "transform");
    return 1;
}

static int editor_tag_for_key(WPARAM key) {
    const int small = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (key) {
    case 'A': return small ? 1 : 5;
    case 'D': return small ? 2 : 6;
    case 'W': return small ? 3 : 7;
    case 'S': return small ? 4 : 8;
    case 'E': return 11; /* clockwise on the x86-era editor ABI */
    case 'Q': return 12; /* counter-clockwise */
    default: return 0;
    }
}

static int send_practice_checkpoint_hotkey(int place) {
    UiCheckpointFunction callback = place ? g_host.ui_on_check
                                          : g_host.ui_on_delete_check;
    UiCheckpointNoSenderFunction old_callback =
        place ? g_host.ui_on_check_no_sender
              : g_host.ui_on_delete_check_no_sender;
    void *ui_layer;
    unsigned char *play_layer;
    if (!callback && !old_callback) return 0;
    if (!detect_gameplay_active() || !g_host.active_play_layer) return 0;
    if (g_host.editor_cache_value) return 1;
    play_layer = (unsigned char *)g_host.active_play_layer;
    if (!g_host.practice_mode_offset ||
        !memory_range_is_readable(
            play_layer + g_host.practice_mode_offset, 1u)) {
        runtime_log("Practice hotkey ignored: no proven Practice Mode field");
        return 1;
    }
    if (play_layer[g_host.practice_mode_offset] == 0u) {
        runtime_log("Practice hotkey ignored: %c while mode=normal",
                    place ? 'Z' : 'X');
        return 1;
    }
    ui_layer = find_active_ui_layer();
    if (!ui_layer) return 1;
    if (callback) callback(ui_layer, NULL);
    else old_callback(ui_layer);
    runtime_log("Practice hotkey: %c mode=practice -> UILayer::%s abi=%s",
                place ? 'Z' : 'X', place ? "onCheck" : "onDeleteCheck",
                callback ? "sender" : "legacy-no-sender");
    return 1;
}

static void pace_x86_frame(void) {
    gd_frame_pacer_wait(&g_host.frame_pacer);
}

static void pause_native_game(const char *reason) {
    if (!g_host.native_ready || g_host.native_paused || !g_host.pause) return;
    runtime_log("Android lifecycle: nativeOnPause (%s)",
                reason ? reason : "unspecified");
    g_host.pause(jni_shim_env(), NULL);
    g_host.native_paused = 1;
    runtime_log("Android lifecycle: nativeOnPause returned");
}

static void resume_native_game(const char *reason) {
    if (!g_host.native_ready || !g_host.native_paused || !g_host.resume ||
        g_host.closing) {
        return;
    }
    runtime_log("Android lifecycle: nativeOnResume (%s)",
                reason ? reason : "unspecified");
    g_host.resume(jni_shim_env(), NULL);
    g_host.native_paused = 0;
}

static void update_display_size(HWND window) {
    RECT area;
    if (!window || !GetClientRect(window, &area) ||
        area.right <= area.left || area.bottom <= area.top) {
        return;
    }
    runtime_set_display_size(
        g_host.native_width, g_host.native_height,
        area.right - area.left, area.bottom - area.top);
}

static void client_to_native(HWND window, float *x, float *y) {
    RECT area;
    float client_width;
    float client_height;
    float sx;
    float sy;
    float scale;
    float content_width;
    float content_height;
    float offset_x;
    float offset_y;
    if (!x || !y || !GetClientRect(window, &area) || area.right <= area.left ||
        area.bottom <= area.top || g_host.native_width <= 0 ||
        g_host.native_height <= 0) {
        return;
    }
    client_width = (float)(area.right - area.left);
    client_height = (float)(area.bottom - area.top);
    sx = client_width / (float)g_host.native_width;
    sy = client_height / (float)g_host.native_height;
    scale = sx < sy ? sx : sy;
    if (scale < 0.0001f) scale = 0.0001f;
    content_width = (float)g_host.native_width * scale;
    content_height = (float)g_host.native_height * scale;
    offset_x = (client_width - content_width) * 0.5f;
    offset_y = (client_height - content_height) * 0.5f;
    *x = (*x - offset_x) / scale;
    *y = (*y - offset_y) / scale;
    if (*x < 0.0f) *x = 0.0f;
    if (*y < 0.0f) *y = 0.0f;
    if (*x > (float)g_host.native_width) *x = (float)g_host.native_width;
    if (*y > (float)g_host.native_height) *y = (float)g_host.native_height;
}

static void toggle_fullscreen(HWND window) {
    MONITORINFO monitor_info;
    HMONITOR monitor;
    if (!window) return;
    if (!g_host.fullscreen) {
        g_host.windowed_style = GetWindowLongPtrA(window, GWL_STYLE);
        g_host.windowed_ex_style = GetWindowLongPtrA(window, GWL_EXSTYLE);
        memset(&g_host.windowed_placement, 0, sizeof(g_host.windowed_placement));
        g_host.windowed_placement.length = sizeof(g_host.windowed_placement);
        GetWindowPlacement(window, &g_host.windowed_placement);
        memset(&monitor_info, 0, sizeof(monitor_info));
        monitor_info.cbSize = sizeof(monitor_info);
        monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoA(monitor, &monitor_info)) return;
        SetWindowLongPtrA(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrA(
            window, GWL_EXSTYLE,
            g_host.windowed_ex_style & ~(LONG_PTR)WS_EX_WINDOWEDGE);
        SetWindowPos(
            window, HWND_TOP,
            monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
            monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
            monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        g_host.fullscreen = 1;
    } else {
        SetWindowLongPtrA(window, GWL_STYLE, g_host.windowed_style);
        SetWindowLongPtrA(window, GWL_EXSTYLE, g_host.windowed_ex_style);
        SetWindowPlacement(window, &g_host.windowed_placement);
        SetWindowPos(
            window, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_host.fullscreen = 0;
    }
    update_display_size(window);
    runtime_log("Window mode: %s toggle=F11/Alt+Enter",
                g_host.fullscreen ? "fullscreen" : "windowed");
}

static void send_touch_begin(float x, float y) {
    if (g_host.native_ready && g_host.touch_begin) {
        g_host.touch_begin(jni_shim_env(), NULL, 0, x, y);
    }
}

static void send_touch_end(float x, float y) {
    if (g_host.native_ready && g_host.touch_end) {
        g_host.touch_end(jni_shim_env(), NULL, 0, x, y);
    }
}

static void send_touch_move(float x, float y) {
    int32_t id = 0;
    if (!g_host.native_ready || !g_host.touch_move) {
        return;
    }
    if (!g_host.touch_ids) {
        g_host.touch_ids = jni_shim_new_int_array(&id, 1);
        g_host.touch_xs = jni_shim_new_float_array(&x, 1);
        g_host.touch_ys = jni_shim_new_float_array(&y, 1);
    }
    if (!jni_shim_update_int_array(g_host.touch_ids, &id, 1) ||
        !jni_shim_update_float_array(g_host.touch_xs, &x, 1) ||
        !jni_shim_update_float_array(g_host.touch_ys, &y, 1)) {
        return;
    }
    g_host.touch_move(jni_shim_env(), NULL, g_host.touch_ids,
                      g_host.touch_xs, g_host.touch_ys);
}

/*
   Windows can cancel capture/focus without delivering the matching mouse-up or
   key-up (Alt/system-menu transitions, Print Screen helpers, task switching,
   etc.). Cocos 2.x keeps touch id 0 latched until touchesEnded arrives; if the
   wrapper loses that END, every later editor/playtest button can appear dead.

   Always close whatever gesture WE actually forwarded to Cocos, reset wrapper
   pointer flags, and release Win32 capture. This is also called before a new
   mouse-down so one lost Windows message can never permanently soft-lock the
   session.
*/
static void cancel_native_input_state(HWND window, const char *reason) {
    int had_state = 0;

    if (g_host.old_playtest_button_pointer_down) {
        g_host.old_playtest_button_pointer_down = 0;
        had_state = 1;
    }

    if (g_host.mouse_down) {
        int consumed = 0;
        int action = gd_extras_menu_pointer_event(
            &g_host.extras_menu, GD_EXTRAS_POINTER_END,
            g_host.last_touch_x, g_host.last_touch_y,
            g_host.native_width, g_host.native_height, &consumed);
        if (action == GD_EXTRAS_ACTION_UI_CHANGED) refresh_extras_visuals();
        else if (action != GD_EXTRAS_ACTION_NONE)
            runtime_log("Extras action %d is unavailable on x86", action);
        if (g_host.mouse_touch_forwarded)
            send_touch_end(g_host.last_touch_x, g_host.last_touch_y);
        g_host.mouse_down = 0;
        g_host.mouse_touch_forwarded = 0;
        had_state = 1;
    } else if (g_host.mouse_touch_forwarded) {
        /* Defensive recovery for an impossible-but-dangerous half-cleared
           wrapper state: Cocos saw BEGIN but our mouse_down bit was lost. */
        send_touch_end(g_host.last_touch_x, g_host.last_touch_y);
        g_host.mouse_touch_forwarded = 0;
        had_state = 1;
    }

    if (g_host.keyboard_down) {
        g_host.keyboard_down = 0;
        send_touch_end((float)g_host.native_width * 0.5f,
                       (float)g_host.native_height * 0.5f);
        had_state = 1;
    }

    if (window && GetCapture() == window) {
        ReleaseCapture();
        had_state = 1;
    }

    if (had_state) {
        g_host.gameplay_cache_time = 0;
        runtime_log("RESULT: X86_INPUT_STATE_RECOVERED reason=%s",
                    reason ? reason : "unspecified");
    }
}

static void send_text_character(WPARAM character) {
    WCHAR utf16[3] = {0, 0, 0};
    char utf8[12];
    int utf16_length = 1;
    int utf8_length;
    void *text;
    if (!g_host.native_ready || !g_host.insert_text) return;
    if (character == '\r') character = '\n';
    if (character < 0x20 && character != '\n' && character != '\t') return;
    if (character <= 0xffff) {
        utf16[0] = (WCHAR)character;
    } else if (character <= 0x10ffff) {
        character -= 0x10000;
        utf16[0] = (WCHAR)(0xd800 + (character >> 10));
        utf16[1] = (WCHAR)(0xdc00 + (character & 0x3ff));
        utf16_length = 2;
    } else {
        return;
    }
    utf8_length = WideCharToMultiByte(CP_UTF8, 0, utf16, utf16_length,
                                      utf8, sizeof(utf8) - 1, NULL, NULL);
    if (utf8_length <= 0) return;
    utf8[utf8_length] = 0;
    text = jni_shim_new_string(utf8);
    g_host.insert_text(jni_shim_env(), NULL, text);
}

static void paste_clipboard_text(HWND window) {
    HANDLE handle;
    const WCHAR *wide;
    char *utf8;
    int utf8_length;
    int converted = 0;
    void *text;
    if (!g_host.native_ready || !g_host.insert_text ||
        !jni_shim_text_input_active() ||
        !IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(window)) {
        return;
    }
    handle = GetClipboardData(CF_UNICODETEXT);
    wide = handle ? (const WCHAR *)GlobalLock(handle) : NULL;
    if (!wide || !*wide) {
        if (wide) GlobalUnlock(handle);
        CloseClipboard();
        return;
    }
    utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0,
                                      NULL, NULL);
    utf8 = utf8_length > 1 ? (char *)malloc((size_t)utf8_length) : NULL;
    if (utf8) {
        converted = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8,
                                        utf8_length, NULL, NULL) > 0;
    }
    GlobalUnlock(handle);
    CloseClipboard();
    if (converted) {
        text = jni_shim_new_string(utf8);
        if (text) g_host.insert_text(jni_shim_env(), NULL, text);
    }
    free(utf8);
}

static int old_playtest_button_hit_test(float x, float view_y) {
    float cocos_y;
    if (!gd_settings_old_ver_playtest() ||
        !gd_settings_old_ver_playtest_supported_version() ||
        (!g_host.old_playtest_play_button && !g_host.old_playtest_stop_button) ||
        g_host.native_height <= 0)
        return 0;
    /* Windows input is top-left; Cocos node positions are bottom-left. */
    cocos_y = (float)g_host.native_height - view_y;
    return fabsf(x - OLD_PLAYTEST_BUTTON_X) <= 28.0f &&
           fabsf(cocos_y - OLD_PLAYTEST_BUTTON_Y) <= 28.0f;
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message,
                                         WPARAM wparam, LPARAM lparam) {
    float x = (float)GET_X_LPARAM(lparam);
    float y = (float)GET_Y_LPARAM(lparam);
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_MOUSEMOVE) {
        client_to_native(window, &x, &y);
        g_host.last_touch_x = x;
        g_host.last_touch_y = y;
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
        !(lparam & (1L << 30)) &&
        (wparam == VK_F11 ||
         (wparam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000)))) {
        toggle_fullscreen(window);
        return 0;
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
        wparam == VK_SNAPSHOT) {
        /* Print Screen itself should still reach Windows; only repair a touch
           that would otherwise be stranded by screenshot/focus helpers. */
        cancel_native_input_state(window, "print screen");
    }
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        wparam == VK_MENU) {
        cancel_native_input_state(window, "alt key");
        return 0;
    }
    switch (message) {
    case WM_CLOSE:
        g_host.closing = 1;
        pause_native_game("window close");
        DestroyWindow(window);
        return 0;
    case WM_QUERYENDSESSION:
        pause_native_game("Windows session ending");
        return TRUE;
    case WM_ENDSESSION:
        if (wparam) {
            g_host.closing = 1;
            pause_native_game("Windows session ended");
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ACTIVATEAPP:
        g_host.window_active = wparam != 0;
        if (wparam) {
            resume_native_game("window activated");
        } else {
            cancel_native_input_state(window, "window deactivated");
            pause_native_game("window deactivated");
        }
        return 0;
    case WM_KILLFOCUS:
        cancel_native_input_state(window, "focus lost");
        return 0;
    case WM_CANCELMODE:
        cancel_native_input_state(window, "cancel mode");
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0u) == SC_KEYMENU) {
            /* Do not let a bare Alt/F10 enter the Win32 menu state. There is
               no native menu here, and old Cocos touch capture can otherwise
               be left half-open until the next focus transition. */
            cancel_native_input_state(window, "system key menu");
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        update_display_size(window);
        return 0;
    case WM_CHAR:
        if (wparam == '\b') {
            if (g_host.native_ready && g_host.delete_backward) {
                g_host.delete_backward(jni_shim_env(), NULL);
            }
        } else {
            send_text_character(wparam);
        }
        return 0;
    case WM_UNICHAR:
        if (wparam == UNICODE_NOCHAR) return TRUE;
        send_text_character(wparam);
        return 0;
    case WM_LBUTTONDOWN: {
        int consumed = 0;
        int action;
        SetFocus(window);
        /* A brand-new DOWN is also a recovery point. If Windows dropped a
           previous UP/CANCEL, close that old gesture before starting another. */
        if (g_host.old_playtest_button_pointer_down || g_host.mouse_down ||
            g_host.keyboard_down || g_host.mouse_touch_forwarded)
            cancel_native_input_state(window, "new pointer down");
        /* Consume wrapper Play/Pause before the editor underneath sees DOWN. */
        if (old_playtest_button_hit_test(x, y)) {
            g_host.old_playtest_button_pointer_down = 1;
            SetCapture(window);
            return 0;
        }
        g_host.mouse_down = 1;
        g_host.mouse_touch_forwarded = 0;
        SetCapture(window);
        action = gd_extras_menu_pointer_event(&g_host.extras_menu,
            GD_EXTRAS_POINTER_BEGIN, x, y, g_host.native_width,
            g_host.native_height, &consumed);
        if (action == GD_EXTRAS_ACTION_UI_CHANGED) refresh_extras_visuals();
        else if (action != GD_EXTRAS_ACTION_NONE)
            runtime_log("Extras action %d is unavailable on x86", action);
        if (!consumed) {
            send_touch_begin(x, y);
            g_host.mouse_touch_forwarded = 1;
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_host.old_playtest_button_pointer_down)
            return 0;
        if (g_host.mouse_down) {
            int consumed = 0;
            int action = gd_extras_menu_pointer_event(&g_host.extras_menu,
                GD_EXTRAS_POINTER_MOVE, x, y, g_host.native_width,
                g_host.native_height, &consumed);
            if (action == GD_EXTRAS_ACTION_UI_CHANGED) refresh_extras_visuals();
            else if (action != GD_EXTRAS_ACTION_NONE)
                runtime_log("Extras action %d is unavailable on x86", action);
            if (g_host.mouse_touch_forwarded) send_touch_move(x, y);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_host.old_playtest_button_pointer_down) {
            const int activate = old_playtest_button_hit_test(x, y);
            g_host.old_playtest_button_pointer_down = 0;
            ReleaseCapture();
            if (activate) {
                InterlockedExchange(&g_host.old_playtest_request,
                                    g_host.old_playtest_layer ? 2 : 1);
                runtime_log("RESULT: X86_OLD_VER_PLAYTEST_WRAPPER_BUTTON_CLICK host-consumed=1 action=%s",
                            g_host.old_playtest_layer ? "stop" : "start");
            }
            return 0;
        }
        if (g_host.mouse_down) {
            int consumed = 0;
            int action;
            g_host.mouse_down = 0;
            ReleaseCapture();
            action = gd_extras_menu_pointer_event(&g_host.extras_menu,
                GD_EXTRAS_POINTER_END, x, y, g_host.native_width,
                g_host.native_height, &consumed);
            if (action == GD_EXTRAS_ACTION_UI_CHANGED) refresh_extras_visuals();
            else if (action != GD_EXTRAS_ACTION_NONE)
                runtime_log("Extras action %d is unavailable on x86", action);
            if (g_host.mouse_touch_forwarded) send_touch_end(x, y);
            g_host.mouse_touch_forwarded = 0;
            /* A release can synchronously enter/leave PlayLayer.  Editor
               hotkey misses are cached per scene and are invalidated naturally
               when find_running_scene() observes the next scene. */
            g_host.gameplay_cache_time = 0;
        }
        return 0;
    case WM_CAPTURECHANGED:
        cancel_native_input_state(NULL, "capture changed");
        return 0;
    case WM_COMMAND: {
        int action = gd_extras_menu_handle_command(&g_host.extras_menu,
                                                    (unsigned long)wparam);
        if (action != GD_EXTRAS_ACTION_NONE) {
            runtime_log("Extras action %d is not available on the x86 backend/version", action);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (!(lparam & (1L << 30)) && (GetKeyState(VK_CONTROL) & 0x8000) &&
            (wparam == 'V') && jni_shim_text_input_active()) {
            paste_clipboard_text(window);
            return 0;
        }
        if (!(lparam & (1L << 30)) && !jni_shim_text_input_active()) {
            if (wparam == VK_DELETE && send_editor_delete_hotkey())
                return 0;
            const int editor_tag = editor_tag_for_key(wparam);
            if (editor_tag && send_editor_hotkey(editor_tag, (int)wparam))
                return 0;
        }
        if (wparam == VK_ESCAPE && g_host.old_playtest_layer) {
            InterlockedExchange(&g_host.old_playtest_request, 2);
            return 0;
        }
        if (wparam == VK_ESCAPE && g_host.native_ready && g_host.key_down) {
            g_host.key_down(jni_shim_env(), NULL, 4); /* Android KEYCODE_BACK */
            return 0;
        }
        if (!(lparam & (1L << 30)) && !jni_shim_text_input_active() &&
            (wparam == 'Z' || wparam == 'X') &&
            send_practice_checkpoint_hotkey(wparam == 'Z')) {
            return 0;
        }
        if ((wparam == VK_SPACE || wparam == VK_UP) && !g_host.keyboard_down &&
            !g_host.mouse_down && !g_host.old_playtest_button_pointer_down &&
            !jni_shim_text_input_active()) {
            g_host.keyboard_down = 1;
            send_touch_begin((float)g_host.native_width * 0.5f,
                             (float)g_host.native_height * 0.5f);
            return 0;
        }
        break;
    case WM_KEYUP:
        if ((wparam == VK_SPACE || wparam == VK_UP) && g_host.keyboard_down) {
            g_host.keyboard_down = 0;
            send_touch_end((float)g_host.native_width * 0.5f,
                           (float)g_host.native_height * 0.5f);
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static int create_opengl_window(int client_width, int client_height) {
    WNDCLASSA window_class;
    RECT rectangle = {0, 0, client_width, client_height};
    PIXELFORMATDESCRIPTOR descriptor;
    int pixel_format;
    typedef BOOL (WINAPI *SwapIntervalFunction)(int);
    SwapIntervalFunction swap_interval;

    memset(&window_class, 0, sizeof(window_class));
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = GetModuleHandleA(NULL);
    window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
    window_class.lpszClassName = "GDAndroidNativeWrapperWindow";
    if (!RegisterClassA(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        runtime_log("ERROR: RegisterClass failed: %lu", (unsigned long)GetLastError());
        return 0;
    }

    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    {
        const int window_width = rectangle.right - rectangle.left;
        const int window_height = rectangle.bottom - rectangle.top;
        const int window_x = (GetSystemMetrics(SM_CXSCREEN) - window_width) / 2;
        const int window_y = (GetSystemMetrics(SM_CYSCREEN) - window_height) / 2;
        {
        const char *configured_title = getenv("GD_GAME_TITLE");
        const char *window_title = configured_title && configured_title[0]
                                       ? configured_title : "Geometry Dash";
        g_host.window = CreateWindowExA(
        0, window_class.lpszClassName, window_title,
        WS_OVERLAPPEDWINDOW, window_x > 0 ? window_x : 0,
        window_y > 0 ? window_y : 0,
        window_width, window_height,
        NULL, NULL, window_class.hInstance, NULL);
        }
    }
    if (!g_host.window) {
        runtime_log("ERROR: CreateWindow failed: %lu", (unsigned long)GetLastError());
        return 0;
    }
    if (gd_apply_window_icon(g_host.window)) {
        runtime_log("Window icon applied from GD_WINDOW_ICON");
    }
    gd_extras_menu_init(&g_host.extras_menu);
    if (g_host.extras_menu.enabled) {
        gd_extras_menu_attach(&g_host.extras_menu, g_host.window);
        runtime_log("Extras menu: enabled in-game cocos2d UI");
    }

    g_host.device = GetDC(g_host.window);
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;
    descriptor.iLayerType = PFD_MAIN_PLANE;
    {
        int actual_msaa = 0;
        const int requested_msaa = gd_settings_msaa_samples();
        pixel_format = gd_gl_choose_pixel_format(g_host.device, &descriptor,
                                                 requested_msaa, &actual_msaa);
        if (pixel_format)
            DescribePixelFormat(g_host.device, pixel_format, sizeof(descriptor),
                                &descriptor);
        if (!pixel_format || !SetPixelFormat(g_host.device, pixel_format, &descriptor)) {
            runtime_log("ERROR: OpenGL pixel format setup failed: %lu",
                        (unsigned long)GetLastError());
            return 0;
        }
        runtime_log("Antialiasing: requested=%s MSAA=%dx%s FXAA=%s",
                    gd_settings_antialiasing_name(), actual_msaa,
                    requested_msaa && !actual_msaa ? " (fallback=off)" : "",
                    gd_settings_fxaa() ? "on" : "off");
    }
    g_host.context = wglCreateContext(g_host.device);
    if (!g_host.context || !wglMakeCurrent(g_host.device, g_host.context)) {
        runtime_log("ERROR: wglCreateContext/wglMakeCurrent failed: %lu",
                    (unsigned long)GetLastError());
        return 0;
    }
    update_display_size(g_host.window);
    g_host.fps_limit = gd_settings_fps_limit();
    swap_interval = (SwapIntervalFunction)wglGetProcAddress("wglSwapIntervalEXT");
    if (gd_settings_fps_vsync()) {
        if (swap_interval) g_host.vsync_enabled = swap_interval(1) != FALSE;
        if (!g_host.vsync_enabled) {
            /* Preserve a sane fallback if the driver has no swap-control extension. */
            gd_frame_pacer_init(&g_host.frame_pacer, 60.0);
        }
        runtime_log("Frame pacing: FPS=VSYNC swap-interval=%s",
                    g_host.vsync_enabled ? "1" : "unavailable; fallback=60");
    } else {
        if (swap_interval) (void)swap_interval(0);
        gd_frame_pacer_init(&g_host.frame_pacer, g_host.fps_limit);
        runtime_log("Frame pacing: FPS=%.3f swap-interval=0 host-cap=enabled",
                    g_host.fps_limit);
    }
    runtime_log("OpenGL vendor: %s", glGetString(GL_VENDOR));
    runtime_log("OpenGL renderer: %s", glGetString(GL_RENDERER));
    runtime_log("OpenGL version: %s", glGetString(GL_VERSION));
    ShowWindow(g_host.window, SW_SHOW);
    UpdateWindow(g_host.window);
    return 1;
}

static void destroy_opengl_window(void) {
    gd_frame_pacer_destroy(&g_host.frame_pacer);
    gd_extras_menu_destroy(&g_host.extras_menu);
    if (g_host.context) {
        if (wglGetCurrentContext() == g_host.context) gd_fxaa_shutdown();
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_host.context);
        g_host.context = NULL;
    }
    if (g_host.device && g_host.window) {
        ReleaseDC(g_host.window, g_host.device);
        g_host.device = NULL;
    }
    if (g_host.window) {
        DestroyWindow(g_host.window);
        g_host.window = NULL;
    }
}

static int executable_directory(char *destination, size_t capacity) {
    char *slash;
    DWORD length = GetModuleFileNameA(NULL, destination, (DWORD)capacity);
    if (!length || length >= capacity) {
        return 0;
    }
    slash = strrchr(destination, '\\');
    if (slash) {
        *slash = 0;
    }
    return SetCurrentDirectoryA(destination) != 0;
}

static void *required_export(const ElfImage *image, const char *name) {
    void *address = elf_image_find_export(image, name);
    if (!address) {
        runtime_log("ERROR: required ELF export is missing: %s", name);
    }
    return address;
}

static int run_message_loop(void) {
    MSG message;
    int result = 0;
    const int one_millisecond_timer =
        timeBeginPeriod(1u) == TIMERR_NOERROR;

    /*
     * The QPC scheduler below uses Sleep only for the coarse portion of a
     * frame. Windows' default timer quantum can be about 15.6 ms, which makes
     * an otherwise stable 60 Hz loop feel closer to 50 Hz. Request a 1 ms
     * process timer while the game window exists, then always release it.
     */
    runtime_log("Frame pacing: Windows timer period=%s",
                one_millisecond_timer ? "1ms" : "default");
    runtime_log("RESULT: RENDER_LOOP_ENTERED");
    while (IsWindow(g_host.window)) {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) goto finished;
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        if (g_host.extras_menu.enabled) {
            refresh_scene_tree_state();
            gd_extras_menu_set_visible(&g_host.extras_menu,
                g_host.active_menu_layer && !g_host.gameplay_cache_value &&
                !g_host.editor_cache_value);
            refresh_extras_visuals();
        }
        if (!process_restart_request()) {
            runtime_log("ERROR: restart button operation failed");
        }
        if (!ensure_restart_button()) {
            runtime_log("ERROR: restart button creation failed");
        }
        if (!process_old_playtest_request()) {
            runtime_log("ERROR: inline old-version playtest operation failed");
        }
        if (!ensure_old_playtest_button()) {
            runtime_log("ERROR: inline old-version playtest button creation failed");
        }
        if (!update_inline_old_playtest()) {
            runtime_log("ERROR: inline old-version playtest bridge update failed");
            InterlockedExchange(&g_host.old_playtest_request, 2);
        }
        if (g_host.render && g_host.window_active) {
            const int old_playtest_active_before_render =
                g_host.old_playtest_layer != NULL;
            g_host.render(jni_shim_env(), NULL);
            if (old_playtest_active_before_render && g_host.old_playtest_layer) {
                if (!update_inline_old_playtest()) {
                    runtime_log("ERROR: inline old-version playtest post-render check failed");
                    (void)stop_inline_old_playtest();
                }
                if (!g_host.old_playtest_layer) {
                    /* nativeRender advances scheduled gameplay before drawing.
                       A death/retry can therefore be discovered only after that
                       call. Re-render the now-restored editor before presenting
                       so an Attempt 2/end-wall frame never reaches the screen. */
                    g_host.render(jni_shim_env(), NULL);
                }
            }
            if (gd_settings_fxaa()) (void)gd_fxaa_apply(g_host.window);
            SwapBuffers(g_host.device);
            /*
             * The game explicitly requests its animation interval through JNI.
             * Vsync alone follows the monitor (for example 144 Hz), while an
             * unconditional Sleep(1) produces uneven 15-18 ms frames. Keep
             * vblank synchronization and cap the render loop to the requested
             * interval with a monotonic high-resolution deadline.
             */
            pace_x86_frame();
        } else {
            gd_frame_pacer_reset(&g_host.frame_pacer);
            /* Do not alternate stale front/back buffers while the app is
               inactive. This also avoids advancing the game behind a pause. */
            Sleep(16);
        }
    }

finished:
    if (one_millisecond_timer) timeEndPeriod(1u);
    return result;
}

int main(int argc, char **argv) {
    (void)gd_enable_application_dpi_awareness();
    if (!gd_settings_i_lost_the_game()) {
        MessageBoxA(NULL,
                    "You lost the game. Launch through launch.cmd instead",
                    "Geometry Dash Wrapper", MB_OK | MB_ICONINFORMATION);
        return 69;
    }
    const char *library_path = NULL;
    const char *apk_path = "game.apk";
    int mode = 2; /* 0 = relocate, 1 = probe, 2 = graphical boot */
    ElfImage image;
    JniOnLoadFunction jni_on_load;
    NativeSetApkPathFunction set_apk_path;
    NativeInitFunction native_init;
    char directory[MAX_PATH];
    char absolute_apk[MAX_PATH * 2];
    char absolute_log[MAX_PATH * 4];
    const char *requested_log = NULL;
    const char *environment_log;
    void *apk_string;
    int result;
    int i;

    memset(&g_host, 0, sizeof(g_host));
    gd_settings_resolution(&g_host.native_width, &g_host.native_height);
    g_host.window_active = 1;

    /*
     * Read the log destination before executable_directory() changes the
     * process working directory. The native launcher passes an absolute path,
     * but accepting a relative --log value from developers is useful too.
     */
    environment_log = getenv("GD_LOG_PATH");
    if (environment_log && environment_log[0]) requested_log = environment_log;
    for (i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--log=", 6) == 0 && argv[i][6]) {
            requested_log = argv[i] + 6;
        }
    }
    absolute_log[0] = 0;
    if (requested_log && requested_log[0]) {
        DWORD log_length = GetFullPathNameA(
            requested_log, (DWORD)sizeof(absolute_log), absolute_log, NULL);
        if (!log_length || log_length >= sizeof(absolute_log)) {
            absolute_log[0] = 0;
        }
    }

    if (!executable_directory(directory, sizeof(directory))) {
        strcpy(directory, ".");
    }
    runtime_initialize(absolute_log[0] ? absolute_log : "gd-wrapper.log");
    jni_shim_initialize(directory);
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--relocate-only") == 0) {
            mode = 0;
        } else if (strcmp(argv[i], "--probe") == 0) {
            mode = 1;
        } else if (strncmp(argv[i], "--apk=", 6) == 0) {
            apk_path = argv[i] + 6;
        } else if (strncmp(argv[i], "--library=", 10) == 0) {
            library_path = argv[i] + 10;
        } else if (strncmp(argv[i], "--log=", 6) == 0) {
            /* Already handled before the working-directory change. */
        } else if (argv[i][0] != '-') {
            library_path = argv[i];
        }
    }
    runtime_log("Mode: %s", mode == 0 ? "relocation only" :
                mode == 1 ? "constructors + JNI_OnLoad" : "graphical native boot");
    if (!(library_path ? elf_image_load(&image, library_path)
                       : elf_image_load_game_from_apk(&image, apk_path))) {
        runtime_log("RESULT: ELF_LOAD_FAILED");
        runtime_shutdown();
        return 2;
    }
    runtime_log("RESULT: ELF_RELOCATION_OK");
    g_host.game_manager_shared_state =
        (GameManagerSharedStateFunction)elf_image_find_export(
            &image, "_ZN11GameManager11sharedStateEv");
    g_host.game_manager_get_play_layer =
        (GameManagerGetPlayLayerFunction)elf_image_find_export(
            &image, "_ZNK11GameManager12getPlayLayerEv");
    g_host.game_manager_set_play_layer =
        (GameManagerSetPlayLayerFunction)elf_image_find_export(
            &image, "_ZN11GameManager12setPlayLayerEP9PlayLayer");
    g_host.game_manager_get_edit_mode =
        (IntGetterFunction)elf_image_find_export(
            &image, "_ZNK11GameManager11getEditModeEv");
    g_host.game_manager_set_edit_mode =
        (IntSetterFunction)elf_image_find_export(
            &image, "_ZN11GameManager11setEditModeEb");
    g_host.ccnode_get_tag = (CcNodeGetTagFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode6getTagEv");
    if (!g_host.ccnode_get_tag)
        g_host.ccnode_get_tag = (CcNodeGetTagFunction)elf_image_find_export(
            &image, "_ZNK7cocos2d6CCNode6getTagEv");
    g_host.ccnode_set_tag = (CcNodeSetTagFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode6setTagEi");
    g_host.ccnode_is_visible = (CcNodeIsVisibleFunction)elf_image_find_export(
        &image, "_ZNK7cocos2d6CCNode9isVisibleEv");
    if (!g_host.ccnode_is_visible)
        g_host.ccnode_is_visible = (CcNodeIsVisibleFunction)elf_image_find_export(
            &image, "_ZN7cocos2d6CCNode9isVisibleEv");
    g_host.editor_move_object_call =
        (EditorMoveObjectCallFunction)elf_image_find_export(
            &image, "_ZN8EditorUI14moveObjectCallEPN7cocos2d6CCNodeE");
    if (!g_host.editor_move_object_call)
        g_host.editor_move_object_call =
            (EditorMoveObjectCallFunction)elf_image_find_export(
                &image, "_ZN8EditorUI14moveObjectCallEPN7cocos2d8CCObjectE");
    g_host.editor_move_edit_command =
        (EditorMoveEditCommandFunction)elf_image_find_export(
            &image, "_ZN8EditorUI14moveObjectCallE11EditCommand");
    g_host.editor_transform_object_call =
        (EditorTransformObjectCallFunction)elf_image_find_export(
            &image, "_ZN8EditorUI19transformObjectCallEPN7cocos2d6CCNodeE");
    if (!g_host.editor_transform_object_call)
        g_host.editor_transform_object_call =
            (EditorTransformObjectCallFunction)elf_image_find_export(
                &image, "_ZN8EditorUI19transformObjectCallEPN7cocos2d8CCObjectE");
    g_host.editor_transform_edit_command =
        (EditorTransformEditCommandFunction)elf_image_find_export(
            &image, "_ZN8EditorUI19transformObjectCallE11EditCommand");
    g_host.editor_on_delete = (EditorDeleteFunction)elf_image_find_export(
        &image, "_ZN8EditorUI8onDeleteEPN7cocos2d8CCObjectE");
    if (!g_host.editor_on_delete)
        g_host.editor_on_delete = (EditorDeleteFunction)elf_image_find_export(
            &image, "_ZN8EditorUI8onDeleteEPN7cocos2d6CCNodeE");
    if (!g_host.editor_on_delete)
        g_host.editor_on_delete_no_sender =
            (EditorDeleteNoSenderFunction)elf_image_find_export(
                &image, "_ZN8EditorUI8onDeleteEv");
    g_host.pause_layer_on_restart = (PauseRestartFunction)elf_image_find_export(
        &image, "_ZN10PauseLayer9onRestartEPN7cocos2d8CCObjectE");
    if (!g_host.pause_layer_on_restart)
        g_host.pause_layer_on_restart = (PauseRestartFunction)elf_image_find_export(
            &image, "_ZN10PauseLayer9onRestartEPN7cocos2d6CCNodeE");
    if (!g_host.pause_layer_on_restart)
        g_host.pause_layer_on_restart_no_sender =
            (PauseRestartNoSenderFunction)elf_image_find_export(
                &image, "_ZN10PauseLayer9onRestartEv");
    g_host.play_layer_resume_and_restart =
        (PlayLayerResumeAndRestartFunction)elf_image_find_export(
            &image, "_ZN9PlayLayer16resumeAndRestartEv");
    g_host.cc_director_shared = (CcDirectorSharedFunction)elf_image_find_export(
        &image, "_ZN7cocos2d10CCDirector14sharedDirectorEv");
    g_host.cc_director_get_running_scene =
        (CcDirectorGetRunningSceneFunction)elf_image_find_export(
            &image, "_ZN7cocos2d10CCDirector15getRunningSceneEv");
    g_host.cc_director_running_scene_offset =
        derive_running_scene_offset(&image);
    g_host.ccnode_get_children = (CcNodeGetChildrenFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode11getChildrenEv");
    g_host.ccnode_get_children_count = (CcNodeGetChildrenCountFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode16getChildrenCountEv");
    if (!g_host.ccnode_get_children_count)
        g_host.ccnode_get_children_count = (CcNodeGetChildrenCountFunction)elf_image_find_export(
            &image, "_ZNK7cocos2d6CCNode16getChildrenCountEv");
    g_host.ccarray_object_at_index = (CcArrayObjectAtIndexFunction)elf_image_find_export(
        &image, "_ZN7cocos2d7CCArray13objectAtIndexEj");
    g_host.button_sprite_create = (ButtonSpriteCreateFunction)elf_image_find_export(
        &image, "_ZN12ButtonSprite6createEPKc");
    g_host.ccnode_add_child = (CcNodeAddChildFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode8addChildEPS0_");
    g_host.ccnode_add_child_z = (CcNodeAddChildZFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode8addChildEPS0_i");
    g_host.ccnode_set_position = (CcNodeSetPositionFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode11setPositionEff");
    g_host.end_portal_set_position = (EndPortalSetPositionFunction)elf_image_find_export(
        &image, "_ZN15EndPortalObject11setPositionERKN7cocos2d7CCPointE");
    g_host.end_portal_trigger_object = elf_image_find_export(
        &image, "_ZN15EndPortalObject13triggerObjectEv");
    g_host.ccnode_get_position_x = (CcNodeGetPositionFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode12getPositionXEv");
    g_host.ccnode_get_position_y = (CcNodeGetPositionFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode12getPositionYEv");
    g_host.ccnode_get_rotation = (CcNodeGetPositionFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode11getRotationEv");
    g_host.ccnode_get_scale_x = (CcNodeGetPositionFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode9getScaleXEv");
    g_host.ccnode_get_scale_y = (CcNodeGetPositionFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode9getScaleYEv");
    g_host.ccnode_get_camera = (CcNodeGetCameraFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode9getCameraEv");
    g_host.cccamera_get_center_xyz = (CcCameraGetXYZFunction)elf_image_find_export(
        &image, "_ZN7cocos2d8CCCamera12getCenterXYZEPfS1_S1_");
    g_host.ccnode_set_scale = (CcNodeSetScaleFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode8setScaleEf");
    g_host.ccnode_set_rotation = (CcNodeSetFloatFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode11setRotationEf");
    g_host.ccnode_set_scale_x = (CcNodeSetFloatFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode9setScaleXEf");
    g_host.ccnode_set_scale_y = (CcNodeSetFloatFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode9setScaleYEf");
    g_host.ccnode_create = (CcNodeCreateFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode6createEv");
    g_host.ccnode_remove = (CcNodeRemoveFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode26removeFromParentAndCleanupEb");
    g_host.ccnode_set_visible = (CcNodeSetVisibleFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode10setVisibleEb");
    g_host.cclayer_set_touch_enabled = (CcLayerSetBoolFunction)elf_image_find_export(
        &image, "_ZN7cocos2d7CCLayer15setTouchEnabledEb");
    g_host.cclayer_is_touch_enabled = (IntGetterFunction)elf_image_find_export(
        &image, "_ZN7cocos2d7CCLayer14isTouchEnabledEv");
    g_host.cclayer_set_keypad_enabled = (CcLayerSetBoolFunction)elf_image_find_export(
        &image, "_ZN7cocos2d7CCLayer16setKeypadEnabledEb");
    g_host.ccnode_unschedule_update = (CcNodeNoArgFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode16unscheduleUpdateEv");
    g_host.ccnode_unschedule_all_selectors = (CcNodeNoArgFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode22unscheduleAllSelectorsEv");
    g_host.ccnode_stop_all_actions = (CcNodeNoArgFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode14stopAllActionsEv");
    g_host.ccobject_retain = (CcObjectRefFunction)elf_image_find_export(
        &image, "_ZN7cocos2d8CCObject6retainEv");
    g_host.ccobject_release = (CcObjectRefFunction)elf_image_find_export(
        &image, "_ZN7cocos2d8CCObject7releaseEv");
    g_host.level_editor_get_level =
        (LevelEditorGetLevelFunction)elf_image_find_export(
            &image, "_ZNK16LevelEditorLayer8getLevelEv");
    g_host.level_editor_get_game_layer = (NodeGetterFunction)elf_image_find_export(
        &image, "_ZNK16LevelEditorLayer12getGameLayerEv");
    g_host.level_editor_get_level_string = elf_image_find_export(
        &image, "_ZN16LevelEditorLayer14getLevelStringEv");
    g_host.gj_game_level_set_level_string =
        (GJGameLevelSetLevelStringFunction)elf_image_find_export(
            &image, "_ZN11GJGameLevel14setLevelStringESs");
    g_host.gj_game_level_create = (NoArgCreateFunction)elf_image_find_export(
        &image, "_ZN11GJGameLevel6createEv");
    g_host.gj_game_level_get_audio_track = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK11GJGameLevel13getAudioTrackEv");
    g_host.gj_game_level_set_audio_track = (IntSetterFunction)elf_image_find_export(
        &image, "_ZN11GJGameLevel13setAudioTrackEi");
    g_host.gj_game_level_get_level_type = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK11GJGameLevel12getLevelTypeEv");
    g_host.gj_game_level_set_level_type = (IntSetterFunction)elf_image_find_export(
        &image, "_ZN11GJGameLevel12setLevelTypeE11GJLevelType");
    g_host.play_layer_create = (PlayLayerCreateFunction)elf_image_find_export(
        &image, "_ZN9PlayLayer6createEP11GJGameLevel");
    g_host.play_layer_start_game =
        (PlayLayerStartGameFunction)elf_image_find_export(
            &image, "_ZN9PlayLayer9startGameEv");
    g_host.play_layer_get_level = (NodeGetterFunction)elf_image_find_export(
        &image, "_ZNK9PlayLayer8getLevelEv");
    if (!g_host.play_layer_get_level)
        g_host.play_layer_get_level = (NodeGetterFunction)elf_image_find_export(
            &image, "_ZN9PlayLayer8getLevelEv");
    g_host.play_layer_reset_level = elf_image_find_export(
        &image, "_ZN9PlayLayer10resetLevelEv");
    g_host.play_layer_update_attempts = elf_image_find_export(
        &image, "_ZN9PlayLayer14updateAttemptsEv");
    g_host.play_layer_destroy_player = elf_image_find_export(
        &image, "_ZN9PlayLayer13destroyPlayerEv");
    g_host.play_layer_get_test_mode = elf_image_find_export(
        &image, "_ZNK9PlayLayer11getTestModeEv");
    g_host.play_layer_get_player = (NodeGetterFunction)elf_image_find_export(
        &image, "_ZNK9PlayLayer9getPlayerEv");
    g_host.play_layer_get_game_layer = (NodeGetterFunction)elf_image_find_export(
        &image, "_ZNK9PlayLayer12getGameLayerEv");
    g_host.play_layer_get_ui_layer = (NodeGetterFunction)elf_image_find_export(
        &image, "_ZNK9PlayLayer10getUILayerEv");
    g_host.player_get_is_dead = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK12PlayerObject9getIsDeadEv");
    g_host.player_get_on_ground = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK12PlayerObject11getOnGroundEv");
    g_host.player_get_gravity_flipped = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK12PlayerObject17getGravityFlippedEv");
    g_host.player_get_fly_mode = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK12PlayerObject10getFlyModeEv");
    g_host.player_get_roll_mode = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK12PlayerObject11getRollModeEv");
    g_host.player_get_bird_mode = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK12PlayerObject11getBirdModeEv");
    g_host.game_manager_get_player_frame = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK11GameManager14getPlayerFrameEv");
    g_host.game_manager_get_player_ship = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK11GameManager13getPlayerShipEv");
    g_host.game_manager_get_player_ball = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK11GameManager13getPlayerBallEv");
    g_host.game_manager_get_player_bird = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK11GameManager13getPlayerBirdEv");
    g_host.game_manager_get_player_color = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK11GameManager14getPlayerColorEv");
    g_host.game_manager_get_player_color2 = (IntGetterFunction)elf_image_find_export(
        &image, "_ZNK11GameManager15getPlayerColor2Ev");
    g_host.game_manager_color_for_idx = elf_image_find_export(
        &image, "_ZN11GameManager11colorForIdxEi");
    g_host.sprite_create_with_frame =
        (CcSpriteCreateWithFrameFunction)elf_image_find_export(
            &image, "_ZN7cocos2d8CCSprite25createWithSpriteFrameNameEPKc");
    g_host.sprite_create_file = (CcSpriteCreateFileFunction)elf_image_find_export(
        &image, "_ZN7cocos2d8CCSprite6createEPKc");
    g_host.sprite_set_color = (CcSpriteSetColorFunction)elf_image_find_export(
        &image, "_ZN7cocos2d8CCSprite8setColorERKNS_10_ccColor3BE");
    g_host.cc_menu_create = (CcMenuCreateFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCMenu6createEv");
    g_host.ccmenu_is_enabled = (IntGetterFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCMenu9isEnabledEv");
    g_host.ccmenu_set_enabled = (IntSetterFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCMenu10setEnabledEb");
    g_host.editor_ui_update_slider = (CcNodeNoArgFunction)elf_image_find_export(
        &image, "_ZN8EditorUI12updateSliderEv");
    g_host.play_layer_toggle_flipped = elf_image_find_export(
        &image, "_ZN9PlayLayer13toggleFlippedEbb");
    g_host.menu_item_sprite_extra_create =
        (CcMenuItemSpriteExtraCreateFunction)elf_image_find_export(
            &image,
            "_ZN21CCMenuItemSpriteExtra6createEPN7cocos2d6CCNodeES2_PNS0_8CCObjectEMS3_FvS4_E");
    g_host.cclayer_color_create = (CcLayerColorCreateFunction)elf_image_find_export(
        &image, "_ZN7cocos2d12CCLayerColor6createERKNS_10_ccColor4BE");
    g_host.ui_on_check = (UiCheckpointFunction)elf_image_find_export(
        &image, "_ZN7UILayer7onCheckEPN7cocos2d8CCObjectE");
    g_host.ui_on_delete_check = (UiCheckpointFunction)elf_image_find_export(
        &image, "_ZN7UILayer13onDeleteCheckEPN7cocos2d8CCObjectE");
    if (!g_host.ui_on_check)
        g_host.ui_on_check_no_sender =
            (UiCheckpointNoSenderFunction)elf_image_find_export(
                &image, "_ZN7UILayer7onCheckEv");
    if (!g_host.ui_on_delete_check)
        g_host.ui_on_delete_check_no_sender =
            (UiCheckpointNoSenderFunction)elf_image_find_export(
                &image, "_ZN7UILayer13onDeleteCheckEv");
    g_host.practice_mode_offset = derive_practice_mode_offset(&image);
    g_host.old_playtest_test_mode_offset =
        derive_old_playtest_mode_offset(&image);
    runtime_log("PC gameplay detection: GameManager::sharedState=%s",
                g_host.game_manager_shared_state ? "ready" : "unavailable");
    runtime_log("Editor controls: toggle=%s move-direct=%s move-sender=%s transform-direct=%s transform-sender=%s getTag=%s setTag=%s",
                gd_settings_editor_controls() ? "on" : "off",
                g_host.editor_move_edit_command ? "ready" : "missing",
                g_host.editor_move_object_call ? "ready" : "missing",
                g_host.editor_transform_edit_command ? "ready" : "missing",
                g_host.editor_transform_object_call ? "ready" : "missing",
                g_host.ccnode_get_tag ? "ready" : "missing",
                g_host.ccnode_set_tag ? "ready" : "missing");
    runtime_log("Editor controls: cached-ui visibility-guard=%s running-scene=%s fallback=director-scene-scan miss-cache=per-scene",
                g_host.ccnode_is_visible ? "ready" : "unavailable",
                g_host.cc_director_get_running_scene ? "accessor" : "fallback-scan");
    runtime_log("Practice Z/X callbacks: place=%s remove=%s guard_offset=0x%lx abi=%s",
                (g_host.ui_on_check || g_host.ui_on_check_no_sender)
                    ? "ready" : "unavailable",
                (g_host.ui_on_delete_check || g_host.ui_on_delete_check_no_sender)
                    ? "ready" : "unavailable",
                (unsigned long)g_host.practice_mode_offset,
                (g_host.ui_on_check || g_host.ui_on_delete_check)
                    ? "sender" : "legacy-no-sender");
    runtime_log("Old-version playtest: toggle=%s version=%s symbols=%s test-mode-offset=0x%lx",
                gd_settings_old_ver_playtest() ? "on" : "off",
                gd_settings_old_ver_playtest_supported_version()
                    ? "supported" : "outside-1.0-1.7",
                old_playtest_symbols_ready() ? "ready" : "missing",
                (unsigned long)g_host.old_playtest_test_mode_offset);
    install_desktop_keyboard_offset_patches(&image);
    install_configurable_x86_hacks(&image);
    if (mode == 0) {
        elf_image_unload(&image);
        runtime_shutdown();
        return 0;
    }
    if (!elf_image_run_constructors(&image)) {
        runtime_log("RESULT: ELF_CONSTRUCTORS_FAILED");
        runtime_shutdown();
        return 3;
    }
    runtime_log("RESULT: ELF_CONSTRUCTORS_OK");
    jni_on_load = (JniOnLoadFunction)required_export(&image, "JNI_OnLoad");
    if (!jni_on_load) {
        runtime_shutdown();
        return 4;
    }
    result = jni_on_load(jni_shim_vm(), NULL);
    runtime_log("JNI_OnLoad returned 0x%08x", result);
    if (result != 0x00010004) {
        runtime_log("RESULT: JNI_ONLOAD_UNEXPECTED");
        runtime_shutdown();
        return 5;
    }
    runtime_log("RESULT: NATIVE_PROBE_OK");
    if (mode == 1) {
        elf_image_unload(&image);
        runtime_shutdown();
        return 0;
    }

    if (!GetFullPathNameA(apk_path, sizeof(absolute_apk), absolute_apk, NULL) ||
        GetFileAttributesA(absolute_apk) == INVALID_FILE_ATTRIBUTES) {
        runtime_log("ERROR: game APK not found: %s", apk_path);
        runtime_shutdown();
        return 6;
    }
    set_apk_path = (NativeSetApkPathFunction)elf_image_find_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath");
    if (set_apk_path) {
        runtime_log("APK path bridge: Cocos2dxHelper.nativeSetApkPath");
    } else {
        set_apk_path = (NativeSetApkPathFunction)elf_image_find_export(
            &image, "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths");
        if (set_apk_path) {
            runtime_log("APK path bridge: legacy Cocos2dxActivity.nativeSetPaths");
        } else {
            runtime_log("ERROR: required APK path setter export is missing");
        }
    }
    native_init = (NativeInitFunction)required_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");
    g_host.render = (NativeRenderFunction)required_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender");
    g_host.touch_begin = (NativeTouchFunction)required_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin");
    g_host.touch_end = (NativeTouchFunction)required_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd");
    g_host.touch_move = (NativeTouchesFunction)required_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove");
    g_host.key_down = (NativeKeyFunction)required_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown");
    g_host.insert_text = (NativeInsertTextFunction)required_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInsertText");
    g_host.delete_backward = (NativeDeleteBackwardFunction)required_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeDeleteBackward");
    g_host.pause = (NativeLifecycleFunction)elf_image_find_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause");
    g_host.resume = (NativeLifecycleFunction)elf_image_find_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnResume");
    runtime_log("Android lifecycle exports: pause=%s resume=%s",
                g_host.pause ? "yes" : "no", g_host.resume ? "yes" : "no");
    if (!set_apk_path || !native_init || !g_host.render || !g_host.touch_begin ||
        !g_host.touch_end || !g_host.touch_move || !g_host.key_down ||
        !g_host.insert_text || !g_host.delete_backward) {
        runtime_shutdown();
        return 7;
    }

    install_android_asset_trace(&image);
    install_tinyxml_trace(&image);
    install_music_download_trace(&image);

    apk_string = jni_shim_new_string(absolute_apk);
    jni_shim_set_apk_path(absolute_apk);
    runtime_log("Setting APK path: %s", absolute_apk);
    set_apk_path(jni_shim_env(), NULL, apk_string);
    runtime_log("RESULT: APK_PATH_SET");

    if (!create_opengl_window(g_host.native_width, g_host.native_height)) {
        runtime_log("RESULT: OPENGL_HOST_FAILED");
        runtime_shutdown();
        return 8;
    }
    runtime_log("RESULT: OPENGL_HOST_OK");
    runtime_log("Render surface: %dx%d texture-filtering=%s",
                g_host.native_width, g_host.native_height,
                gd_settings_linear_texture_filtering() ? "linear" : "game");
    runtime_log("Calling authentic Android nativeInit(%d, %d)",
                g_host.native_width, g_host.native_height);
    native_init(jni_shim_env(), NULL, g_host.native_width, g_host.native_height);
    g_host.native_ready = 1;
    runtime_log("RESULT: NATIVE_INIT_RETURNED");
    run_message_loop();

    pause_native_game("wrapper shutdown");
    g_host.native_ready = 0;
    destroy_opengl_window();
    jni_shim_shutdown();
    elf_image_unload(&image);
    runtime_shutdown();
    return 0;
}
