#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <GL/gl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jni_shim.h"
#include "loader.h"
#include "runtime.h"
#include "build_info.h"
#include "runtime_settings.h"
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
typedef void (__cdecl *CCNodeSetVisibleFunction)(void *node, int visible);
typedef void *(__cdecl *PauseMenuItemCreateFunction)(
    void *normal_sprite, void *selected_sprite, void *target,
    uintptr_t selector_function, uintptr_t selector_adjustment);
typedef void (__cdecl *UiCheckpointFunction)(void *self, void *sender);
typedef void (__cdecl *UiCheckpointNoSenderFunction)(void *self);
typedef int (__cdecl *CcNodeGetTagFunction)(void *self);
typedef void (__cdecl *CcNodeSetTagFunction)(void *self, int tag);
typedef void (__cdecl *EditorMoveObjectCallFunction)(void *self, void *sender);
typedef void (__cdecl *EditorMoveEditCommandFunction)(void *self, int command);
typedef void (__cdecl *EditorTransformObjectCallFunction)(void *self, void *sender);
typedef void (__cdecl *EditorTransformEditCommandFunction)(void *self, int command);
typedef void *(__cdecl *CcDirectorSharedFunction)(void);
typedef void *(__cdecl *CcNodeGetChildrenFunction)(void *self);
typedef unsigned int (__cdecl *CcNodeGetChildrenCountFunction)(void *self);
typedef void *(__cdecl *CcArrayObjectAtIndexFunction)(void *self, unsigned int index);
typedef void *(__cdecl *ButtonSpriteCreateFunction)(const char *text);
typedef void (__cdecl *CcNodeAddChildFunction)(void *self, void *child);
typedef void (__cdecl *CcNodeAddChildZFunction)(void *self, void *child, int z);
typedef void (__cdecl *CcNodeSetPositionFunction)(void *self, float x, float y);
typedef void (__cdecl *CcNodeRemoveFunction)(void *self, int cleanup);
typedef struct { unsigned char r, g, b, a; } GdCcColor4B;
typedef void *(__cdecl *CcLayerColorCreateFunction)(const GdCcColor4B *color);

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
    int keyboard_down;
    int native_paused;
    int window_active;
    int closing;
    int fullscreen;
    LONG_PTR windowed_style;
    LONG_PTR windowed_ex_style;
    WINDOWPLACEMENT windowed_placement;
    int vsync_enabled;
    int remove_pause_button_option;
    int pause_touch_blocked;
    int hide_cursor_option;
    int cursor_hidden;
    int cursor_force_visible;
    int cursor_pause_click_seen;
    int pause_overlay_seen;
    ULONGLONG gameplay_cache_time;
    int gameplay_cache_value;
    int editor_cache_value;
    void *active_play_layer;
    void *active_editor_layer;
    void *active_pause_layer;
    void *pause_hidden_for_play_layer;
    size_t pause_button_offset;
    CCNodeSetVisibleFunction node_set_visible;
    GameManagerSharedStateFunction game_manager_shared_state;
    UiCheckpointFunction ui_on_check;
    UiCheckpointFunction ui_on_delete_check;
    UiCheckpointNoSenderFunction ui_on_check_no_sender;
    UiCheckpointNoSenderFunction ui_on_delete_check_no_sender;
    CcNodeGetTagFunction ccnode_get_tag;
    CcNodeSetTagFunction ccnode_set_tag;
    EditorMoveObjectCallFunction editor_move_object_call;
    EditorMoveEditCommandFunction editor_move_edit_command;
    EditorTransformObjectCallFunction editor_transform_object_call;
    EditorTransformEditCommandFunction editor_transform_edit_command;
    CcDirectorSharedFunction cc_director_shared;
    CcNodeGetChildrenFunction ccnode_get_children;
    CcNodeGetChildrenCountFunction ccnode_get_children_count;
    CcArrayObjectAtIndexFunction ccarray_object_at_index;
    ButtonSpriteCreateFunction button_sprite_create;
    CcNodeAddChildFunction ccnode_add_child;
    CcNodeAddChildZFunction ccnode_add_child_z;
    CcNodeSetPositionFunction ccnode_set_position;
    CcNodeRemoveFunction ccnode_remove;
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
    unsigned int editor_hotkey_miss_logs;
    GdExtrasMenu extras_menu;
    uint32_t practice_mode_offset;
    LARGE_INTEGER frame_clock_frequency;
    LONGLONG next_frame_deadline;
} GameHost;

static GameHost g_host;
static PauseMenuItemCreateFunction g_original_pause_menu_item_create;

static void * __cdecl create_hidden_pause_menu_item(
    void *normal_sprite, void *selected_sprite, void *target,
    uintptr_t selector_function, uintptr_t selector_adjustment) {
    void *item = NULL;
    if (g_original_pause_menu_item_create) {
        item = g_original_pause_menu_item_create(
            normal_sprite, selected_sprite, target,
            selector_function, selector_adjustment);
    }
    if (item && g_host.remove_pause_button_option && g_host.node_set_visible)
        g_host.node_set_visible(item, 0);
    return item;
}

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

/* Derive the UILayer member that receives the pause CCMenuItemSpriteExtra.
   The matching create call is also returned so REMOVE_PAUSE_BUTTON can suppress
   the item before the first frame rather than waiting for gameplay polling. */
static size_t discover_x86_pause_button_offset(const ElfImage *image,
                                                unsigned char **create_callsite,
                                                void **create_function) {
    unsigned char *initializer = (unsigned char *)elf_image_find_export(
        image, "_ZN7UILayer4initEv");
    unsigned char *create_item = (unsigned char *)elf_image_find_export(
        image,
        "_ZN21CCMenuItemSpriteExtra6createEPN7cocos2d6CCNodeES2_"
        "PNS0_8CCObjectEMS3_FvS4_E");
    size_t offset;
    if (create_callsite) *create_callsite = NULL;
    if (create_function) *create_function = create_item;
    if (!initializer || !create_item) return 0u;
    for (offset = 0u; offset + 5u < 1024u; ++offset) {
        int32_t displacement;
        unsigned char *call_target;
        size_t after;
        if (initializer[offset] != 0xE8u) continue;
        memcpy(&displacement, initializer + offset + 1u, sizeof(displacement));
        call_target = initializer + offset + 5u + displacement;
        if (call_target != create_item) continue;
        for (after = offset + 5u;
             after + 6u <= offset + 40u && after + 6u <= 1024u; ++after) {
            uint32_t field;
            const unsigned char modrm = initializer[after + 1u];
            if (initializer[after] != 0x89u ||
                (modrm & 0xF8u) != 0x80u || (modrm & 0x07u) == 0x04u)
                continue;
            memcpy(&field, initializer + after + 2u, sizeof(field));
            if (field >= 0x80u && field <= 0x800u) {
                if (create_callsite) *create_callsite = initializer + offset;
                return field;
            }
        }
        break;
    }
    return 0u;
}

static int install_x86_pause_creation_suppression(const ElfImage *image) {
#if defined(__i386__)
    unsigned char *callsite = NULL;
    void *create_function = NULL;
    int32_t displacement;
    DWORD old_protection;
    DWORD ignored_protection;
    size_t field;
    if (!g_host.remove_pause_button_option || !g_host.node_set_visible)
        return 0;
    field = discover_x86_pause_button_offset(
        image, &callsite, &create_function);
    if (!field || !callsite || !create_function || callsite[0] != 0xE8u)
        return 0;
    g_original_pause_menu_item_create =
        (PauseMenuItemCreateFunction)create_function;
    displacement = (int32_t)((uintptr_t)create_hidden_pause_menu_item -
                             ((uintptr_t)callsite + 5u));
    if (!VirtualProtect(callsite + 1u, 4u, PAGE_EXECUTE_READWRITE,
                        &old_protection))
        return 0;
    memcpy(callsite + 1u, &displacement, sizeof(displacement));
    FlushInstructionCache(GetCurrentProcess(), callsite, 5u);
    VirtualProtect(callsite + 1u, 4u, old_protection, &ignored_protection);
    runtime_log("PC pause creation suppression: installed field=0x%lx",
                (unsigned long)field);
    return 1;
#else
    (void)image;
    return 0;
#endif
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
    g_host.remove_pause_button_option = gd_settings_remove_pause_button();
    g_host.hide_cursor_option = gd_settings_hide_cursor_when_playing();
    g_host.pause_button_offset = discover_x86_pause_button_offset(
        image, NULL, NULL);
    g_host.node_set_visible = (CCNodeSetVisibleFunction)elf_image_find_export(
        image, "_ZN7cocos2d6CCNode10setVisibleEb");
    if (g_host.remove_pause_button_option &&
        !install_x86_pause_creation_suppression(image)) {
        runtime_log("PC pause creation suppression: unavailable; runtime hide fallback active");
    }
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
    runtime_log("PC desktop tweaks: remove-pause=%s pause-field=0x%lx setVisible=%s hide-cursor=%s",
                g_host.remove_pause_button_option ? "true" : "false",
                (unsigned long)g_host.pause_button_offset,
                g_host.node_set_visible ? "ready" : "missing",
                g_host.hide_cursor_option ? "true" : "false");
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
    if (!director || !memory_range_is_readable(director, 0x500u)) return NULL;
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

static void set_cursor_hidden(int hidden) {
    hidden = hidden != 0;
    if (g_host.cursor_hidden == hidden) return;
    g_host.cursor_hidden = hidden;
    if (g_host.window)
        SetCursor(hidden ? NULL : LoadCursorA(NULL, IDC_ARROW));
}

static void hide_pause_button_visual(void) {
    void *ui_layer;
    void *pause_item;
    unsigned char *field;
    if (!g_host.remove_pause_button_option || !detect_gameplay_active() ||
        g_host.editor_cache_value || !g_host.active_play_layer ||
        !g_host.pause_button_offset || !g_host.node_set_visible)
        return;
    ui_layer = find_active_ui_layer();
    if (!ui_layer) return;
    field = (unsigned char *)ui_layer + g_host.pause_button_offset;
    if (!memory_range_is_readable(field, sizeof(void *))) return;
    pause_item = *(void **)field;
    if (!object_type_contains(pause_item, "CCMenuItem")) return;
    g_host.node_set_visible(pause_item, 0);
    if (g_host.pause_hidden_for_play_layer != g_host.active_play_layer) {
        g_host.pause_hidden_for_play_layer = g_host.active_play_layer;
        runtime_log("PC gameplay: pause button hidden at UILayer+0x%lx; Escape preserved",
                    (unsigned long)g_host.pause_button_offset);
    }
}

static int point_is_pause_button(float x, float y) {
    return g_host.remove_pause_button_option && detect_gameplay_active() &&
           !g_host.editor_cache_value && g_host.native_width > 0 &&
           g_host.native_height > 0 &&
           x >= (float)g_host.native_width * 0.86f &&
           y <= (float)g_host.native_height * 0.22f;
}

static void refresh_cursor_and_pause_features(void) {
    int gameplay = detect_gameplay_active();
    if (gameplay && g_host.cursor_force_visible) {
        refresh_scene_tree_state();
        gameplay = g_host.gameplay_cache_value;
    }
    if (!gameplay || g_host.editor_cache_value) {
        g_host.cursor_force_visible = 0;
        g_host.cursor_pause_click_seen = 0;
        g_host.pause_overlay_seen = 0;
        g_host.pause_hidden_for_play_layer = NULL;
    } else if (g_host.active_pause_layer) {
        g_host.pause_overlay_seen = 1;
        g_host.cursor_pause_click_seen = 0;
        g_host.cursor_force_visible = 1;
    } else if (g_host.pause_overlay_seen) {
        g_host.pause_overlay_seen = 0;
        g_host.cursor_pause_click_seen = 0;
        g_host.cursor_force_visible = 0;
    }
    if (gameplay && !g_host.editor_cache_value) hide_pause_button_visual();
    set_cursor_hidden(g_host.hide_cursor_option && gameplay &&
                      !g_host.editor_cache_value && g_host.window_active &&
                      !jni_shim_text_input_active() &&
                      !g_host.cursor_force_visible);
}

static void *find_active_editor_ui(void) {
    unsigned int visited = 0;
    refresh_scene_tree_state();
    if (g_host.active_editor_ui) return g_host.active_editor_ui;
    if (g_host.active_editor_layer) {
        walk_scene_tree(g_host.active_editor_layer, 0u, &visited);
        if (g_host.active_editor_ui) return g_host.active_editor_ui;
    }
    if (g_host.editor_hotkey_miss_logs++ < 8u)
        runtime_log("Editor controls: key ignored; no active EditorUI found (editor-layer=%s)",
                    g_host.active_editor_layer ? "yes" : "no");
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

static int send_editor_hotkey(int tag, int virtual_key) {
    void *editor_ui;
    int old_tag;
    const int movement = tag >= 1 && tag <= 8;
    EditorTransformObjectCallFunction sender;
    EditorTransformEditCommandFunction direct;

    if (!gd_settings_editor_controls()) return 0;
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
    LARGE_INTEGER now;
    double interval = jni_shim_frame_interval();
    LONGLONG interval_ticks;

    /* Broken clients occasionally report zero or absurd values. */
    if (interval < 1.0 / 240.0 || interval > 1.0 / 20.0)
        interval = 1.0 / 60.0;
    if (!g_host.frame_clock_frequency.QuadPart &&
        !QueryPerformanceFrequency(&g_host.frame_clock_frequency))
        return;
    QueryPerformanceCounter(&now);
    interval_ticks = (LONGLONG)(
        interval * (double)g_host.frame_clock_frequency.QuadPart + 0.5);
    if (interval_ticks < 1) interval_ticks = 1;

    if (!g_host.next_frame_deadline) {
        /* Let the first SwapBuffers establish the display's phase. */
        g_host.next_frame_deadline = now.QuadPart;
        return;
    }
    if (now.QuadPart >
        g_host.next_frame_deadline + interval_ticks * 3) {
        g_host.next_frame_deadline = now.QuadPart;
    }
    g_host.next_frame_deadline += interval_ticks;

    for (;;) {
        double remaining_ms;
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= g_host.next_frame_deadline) break;
        remaining_ms =
            (double)(g_host.next_frame_deadline - now.QuadPart) * 1000.0 /
            (double)g_host.frame_clock_frequency.QuadPart;
        if (remaining_ms > 2.0) {
            Sleep((DWORD)(remaining_ms - 1.0));
        } else {
            SwitchToThread();
        }
    }
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
            set_cursor_hidden(0);
            pause_native_game("window deactivated");
        }
        refresh_cursor_and_pause_features();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        update_display_size(window);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && g_host.cursor_hidden) {
            SetCursor(NULL);
            return TRUE;
        }
        break;
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
        if (point_is_pause_button(x, y)) {
            g_host.pause_touch_blocked = 1;
            return 0;
        }
        g_host.pause_touch_blocked = 0;
        if (detect_gameplay_active() && !g_host.editor_cache_value &&
            !g_host.active_pause_layer) {
            if (g_host.cursor_force_visible && !g_host.cursor_pause_click_seen) {
                g_host.cursor_pause_click_seen = 1;
            } else {
                g_host.cursor_force_visible = 0;
                g_host.cursor_pause_click_seen = 0;
                refresh_cursor_and_pause_features();
            }
        }
        g_host.mouse_down = 1;
        SetCapture(window);
        action = gd_extras_menu_pointer_event(&g_host.extras_menu,
            GD_EXTRAS_POINTER_BEGIN, x, y, g_host.native_width,
            g_host.native_height, &consumed);
        if (action == GD_EXTRAS_ACTION_UI_CHANGED) refresh_extras_visuals();
        else if (action != GD_EXTRAS_ACTION_NONE)
            runtime_log("Extras action %d is unavailable on x86", action);
        if (!consumed) send_touch_begin(x, y);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_host.mouse_down) {
            int consumed = 0;
            int action = gd_extras_menu_pointer_event(&g_host.extras_menu,
                GD_EXTRAS_POINTER_MOVE, x, y, g_host.native_width,
                g_host.native_height, &consumed);
            if (action == GD_EXTRAS_ACTION_UI_CHANGED) refresh_extras_visuals();
            else if (action != GD_EXTRAS_ACTION_NONE)
                runtime_log("Extras action %d is unavailable on x86", action);
            if (!consumed) send_touch_move(x, y);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_host.pause_touch_blocked) {
            g_host.pause_touch_blocked = 0;
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
            if (!consumed) send_touch_end(x, y);
            /* A release can synchronously enter/leave PlayLayer. Force the
               next pre-render suppression pass to see the new scene. */
            g_host.gameplay_cache_time = 0;
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_host.pause_touch_blocked = 0;
        if (g_host.mouse_down) {
            int consumed = 0;
            int action;
            g_host.mouse_down = 0;
            action = gd_extras_menu_pointer_event(&g_host.extras_menu,
                GD_EXTRAS_POINTER_END, g_host.last_touch_x, g_host.last_touch_y,
                g_host.native_width, g_host.native_height, &consumed);
            if (action == GD_EXTRAS_ACTION_UI_CHANGED) refresh_extras_visuals();
            else if (action != GD_EXTRAS_ACTION_NONE)
                runtime_log("Extras action %d is unavailable on x86", action);
            if (!consumed) send_touch_end(g_host.last_touch_x, g_host.last_touch_y);
            g_host.gameplay_cache_time = 0;
        }
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
        if (!(lparam & (1L << 30)) && !jni_shim_text_input_active()) {
            const int editor_tag = editor_tag_for_key(wparam);
            if (editor_tag && send_editor_hotkey(editor_tag, (int)wparam))
                return 0;
        }
        if (wparam == VK_ESCAPE && g_host.native_ready && g_host.key_down) {
            g_host.cursor_force_visible = 1;
            g_host.cursor_pause_click_seen = 0;
            set_cursor_hidden(0);
            g_host.key_down(jni_shim_env(), NULL, 4); /* Android KEYCODE_BACK */
            return 0;
        }
        if (!(lparam & (1L << 30)) && !jni_shim_text_input_active() &&
            (wparam == 'Z' || wparam == 'X') &&
            send_practice_checkpoint_hotkey(wparam == 'Z')) {
            return 0;
        }
        if ((wparam == VK_SPACE || wparam == VK_UP) && !g_host.keyboard_down &&
            !jni_shim_text_input_active()) {
            g_host.cursor_force_visible = 0;
            g_host.cursor_pause_click_seen = 0;
            refresh_cursor_and_pause_features();
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
    pixel_format = ChoosePixelFormat(g_host.device, &descriptor);
    if (!pixel_format || !SetPixelFormat(g_host.device, pixel_format, &descriptor)) {
        runtime_log("ERROR: OpenGL pixel format setup failed: %lu",
                    (unsigned long)GetLastError());
        return 0;
    }
    g_host.context = wglCreateContext(g_host.device);
    if (!g_host.context || !wglMakeCurrent(g_host.device, g_host.context)) {
        runtime_log("ERROR: wglCreateContext/wglMakeCurrent failed: %lu",
                    (unsigned long)GetLastError());
        return 0;
    }
    update_display_size(g_host.window);
    swap_interval = (SwapIntervalFunction)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap_interval) {
        g_host.vsync_enabled = swap_interval(1) != FALSE;
    }
    runtime_log("Frame pacing: swap-interval=%s jni-deadline-scheduler=enabled",
                g_host.vsync_enabled ? "1" : "unavailable");
    runtime_log("OpenGL vendor: %s", glGetString(GL_VENDOR));
    runtime_log("OpenGL renderer: %s", glGetString(GL_RENDERER));
    runtime_log("OpenGL version: %s", glGetString(GL_VERSION));
    ShowWindow(g_host.window, SW_SHOW);
    UpdateWindow(g_host.window);
    return 1;
}

static void destroy_opengl_window(void) {
    gd_extras_menu_destroy(&g_host.extras_menu);
    if (g_host.context) {
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
        if (g_host.render && g_host.window_active) {
            /* Apply suppression before guest drawing so the pause button never
               reaches the presented back buffer when the option is enabled. */
            refresh_cursor_and_pause_features();
            g_host.render(jni_shim_env(), NULL);
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
                    "I_LOST_THE_GAME is false. You lost the game.\n\nLaunch Geometry Dash Wrapper through RUN_AUTO_GDPS.cmd or RUN_AUTO_BOOMLINGS.cmd.",
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
    g_host.native_width = 1280;
    g_host.native_height = 720;
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
    g_host.ccnode_get_tag = (CcNodeGetTagFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode6getTagEv");
    if (!g_host.ccnode_get_tag)
        g_host.ccnode_get_tag = (CcNodeGetTagFunction)elf_image_find_export(
            &image, "_ZNK7cocos2d6CCNode6getTagEv");
    g_host.ccnode_set_tag = (CcNodeSetTagFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode6setTagEi");
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
    g_host.cc_director_shared = (CcDirectorSharedFunction)elf_image_find_export(
        &image, "_ZN7cocos2d10CCDirector14sharedDirectorEv");
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
    g_host.ccnode_remove = (CcNodeRemoveFunction)elf_image_find_export(
        &image, "_ZN7cocos2d6CCNode26removeFromParentAndCleanupEb");
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
    runtime_log("Practice Z/X callbacks: place=%s remove=%s guard_offset=0x%lx abi=%s",
                (g_host.ui_on_check || g_host.ui_on_check_no_sender)
                    ? "ready" : "unavailable",
                (g_host.ui_on_delete_check || g_host.ui_on_delete_check_no_sender)
                    ? "ready" : "unavailable",
                (unsigned long)g_host.practice_mode_offset,
                (g_host.ui_on_check || g_host.ui_on_delete_check)
                    ? "sender" : "legacy-no-sender");
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
