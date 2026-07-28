#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jni_shim.h"
#include "loader.h"
#include "runtime.h"
#include "build_info.h"
#include "runtime_settings.h"
#include "window_icon_win.h"

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
} GameHost;

static GameHost g_host;

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
    size_t index;
    if (gd_settings_hack_icons()) {
        icon_patches = patch_x86_return_true_exports(
            image, icon_checks, sizeof(icon_checks) / sizeof(icon_checks[0]));
    }
    if (gd_settings_force_highest_graphics()) {
        high_graphics_patches = patch_x86_return_true_exports(
            image, high_graphics_checks,
            sizeof(high_graphics_checks) / sizeof(high_graphics_checks[0]));
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
                "highest-graphics=%s hd=%u low-memory=%u music-pulse-max=%.3f",
                gd_settings_server(),
                gd_settings_hack_icons() ? "true" : "false", icon_patches,
                gd_settings_full_bypass() ? "true" : "false", bypass_patches,
                online_patches,
                gd_settings_force_highest_graphics() ? "true" : "false",
                high_graphics_patches, low_memory_patches,
                gd_settings_music_pulse_max());
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

static void client_to_native(HWND window, float *x, float *y) {
    RECT area;
    if (!x || !y || !GetClientRect(window, &area) || area.right <= area.left ||
        area.bottom <= area.top || g_host.native_width <= 0 ||
        g_host.native_height <= 0) {
        return;
    }
    *x = *x * (float)g_host.native_width / (float)(area.right - area.left);
    *y = *y * (float)g_host.native_height / (float)(area.bottom - area.top);
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
            pause_native_game("window deactivated");
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
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
    case WM_LBUTTONDOWN:
        g_host.mouse_down = 1;
        SetFocus(window);
        SetCapture(window);
        send_touch_begin(x, y);
        return 0;
    case WM_MOUSEMOVE:
        if (g_host.mouse_down) {
            send_touch_move(x, y);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_host.mouse_down) {
            g_host.mouse_down = 0;
            ReleaseCapture();
            send_touch_end(x, y);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (g_host.mouse_down) {
            g_host.mouse_down = 0;
            send_touch_end(g_host.last_touch_x, g_host.last_touch_y);
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE && g_host.native_ready && g_host.key_down) {
            g_host.key_down(jni_shim_env(), NULL, 4); /* Android KEYCODE_BACK */
            return 0;
        }
        if ((wparam == VK_SPACE || wparam == VK_UP) && !g_host.keyboard_down &&
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
        g_host.window = CreateWindowExA(
        0, window_class.lpszClassName, "Geometry Dash Wrapper - Unified x86",
        WS_OVERLAPPEDWINDOW, window_x > 0 ? window_x : 0,
        window_y > 0 ? window_y : 0,
        window_width, window_height,
        NULL, NULL, window_class.hInstance, NULL);
    }
    if (!g_host.window) {
        runtime_log("ERROR: CreateWindow failed: %lu", (unsigned long)GetLastError());
        return 0;
    }
    if (gd_apply_window_icon(g_host.window)) {
        runtime_log("Window icon applied from GD_WINDOW_ICON");
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
    swap_interval = (SwapIntervalFunction)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap_interval) {
        swap_interval(1);
    }
    runtime_log("OpenGL vendor: %s", glGetString(GL_VENDOR));
    runtime_log("OpenGL renderer: %s", glGetString(GL_RENDERER));
    runtime_log("OpenGL version: %s", glGetString(GL_VERSION));
    ShowWindow(g_host.window, SW_SHOW);
    UpdateWindow(g_host.window);
    return 1;
}

static void destroy_opengl_window(void) {
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
    runtime_log("RESULT: RENDER_LOOP_ENTERED");
    while (IsWindow(g_host.window)) {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return 0;
            }
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        if (g_host.render && g_host.window_active) {
            g_host.render(jni_shim_env(), NULL);
            SwapBuffers(g_host.device);
            Sleep(1);
        } else {
            /* Do not alternate stale front/back buffers while the app is
               inactive. This also avoids advancing the game behind a pause. */
            Sleep(16);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *library_path = NULL;
    const char *apk_path = "game.apk";
    int mode = 2; /* 0 = relocate, 1 = probe, 2 = graphical boot */
    ElfImage image;
    JniOnLoadFunction jni_on_load;
    NativeSetApkPathFunction set_apk_path;
    NativeInitFunction native_init;
    char directory[MAX_PATH];
    char absolute_apk[MAX_PATH * 2];
    void *apk_string;
    int result;
    int i;

    memset(&g_host, 0, sizeof(g_host));
    g_host.native_width = 1280;
    g_host.native_height = 720;
    g_host.window_active = 1;
    if (!executable_directory(directory, sizeof(directory))) {
        strcpy(directory, ".");
    }
    runtime_initialize("gd-wrapper.log");
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
        } else {
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
