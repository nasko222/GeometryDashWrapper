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
    int closing;
} GameHost;

static GameHost g_host;

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
    g_host.window = CreateWindowExA(
        0, window_class.lpszClassName, "Geometry Dash - Android native wrapper",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        NULL, NULL, window_class.hInstance, NULL);
    if (!g_host.window) {
        runtime_log("ERROR: CreateWindow failed: %lu", (unsigned long)GetLastError());
        return 0;
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
        if (g_host.render) {
            g_host.render(jni_shim_env(), NULL);
            SwapBuffers(g_host.device);
        }
        Sleep(1);
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
                       : elf_image_load_from_apk(
                             &image, apk_path, "lib/x86/libcocos2dcpp.so"))) {
        runtime_log("RESULT: ELF_LOAD_FAILED");
        runtime_shutdown();
        return 2;
    }
    runtime_log("RESULT: ELF_RELOCATION_OK");
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
    set_apk_path = (NativeSetApkPathFunction)required_export(
        &image, "Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath");
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
