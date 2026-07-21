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

typedef struct {
    HWND window;
    HDC device;
    HGLRC context;
    NativeRenderFunction render;
    NativeTouchFunction touch_begin;
    NativeTouchFunction touch_end;
    NativeTouchesFunction touch_move;
    NativeKeyFunction key_down;
    int native_ready;
    int mouse_down;
    int keyboard_down;
} GameHost;

static GameHost g_host;

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
    void *ids;
    void *xs;
    void *ys;
    if (!g_host.native_ready || !g_host.touch_move) {
        return;
    }
    ids = jni_shim_new_int_array(&id, 1);
    xs = jni_shim_new_float_array(&x, 1);
    ys = jni_shim_new_float_array(&y, 1);
    g_host.touch_move(jni_shim_env(), NULL, ids, xs, ys);
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message,
                                         WPARAM wparam, LPARAM lparam) {
    float x = (float)GET_X_LPARAM(lparam);
    float y = (float)GET_Y_LPARAM(lparam);
    switch (message) {
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        g_host.mouse_down = 1;
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
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE && g_host.native_ready && g_host.key_down) {
            g_host.key_down(jni_shim_env(), NULL, 4); /* Android KEYCODE_BACK */
            return 0;
        }
        if ((wparam == VK_SPACE || wparam == VK_UP) && !g_host.keyboard_down) {
            RECT area;
            GetClientRect(window, &area);
            g_host.keyboard_down = 1;
            send_touch_begin((float)(area.right / 2), (float)(area.bottom / 2));
            return 0;
        }
        break;
    case WM_KEYUP:
        if ((wparam == VK_SPACE || wparam == VK_UP) && g_host.keyboard_down) {
            RECT area;
            GetClientRect(window, &area);
            g_host.keyboard_down = 0;
            send_touch_end((float)(area.right / 2), (float)(area.bottom / 2));
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
    window_class.lpszClassName = "GD18NativeWrapperWindow";
    if (!RegisterClassA(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        runtime_log("ERROR: RegisterClass failed: %lu", (unsigned long)GetLastError());
        return 0;
    }

    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    g_host.window = CreateWindowExA(
        0, window_class.lpszClassName, "Geometry Dash 1.8 — native wrapper",
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
    const char *library_path = "libcocos2dcpp.so";
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
    if (!executable_directory(directory, sizeof(directory))) {
        strcpy(directory, ".");
    }
    runtime_initialize("gd18-wrapper.log");
    jni_shim_initialize(directory);
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--relocate-only") == 0) {
            mode = 0;
        } else if (strcmp(argv[i], "--probe") == 0) {
            mode = 1;
        } else if (strncmp(argv[i], "--apk=", 6) == 0) {
            apk_path = argv[i] + 6;
        } else {
            library_path = argv[i];
        }
    }
    runtime_log("Mode: %s", mode == 0 ? "relocation only" :
                mode == 1 ? "constructors + JNI_OnLoad" : "graphical native boot");
    if (!elf_image_load(&image, library_path)) {
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
    runtime_log("RESULT: NATIVE_1_8_PROBE_OK");
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
    if (!set_apk_path || !native_init || !g_host.render || !g_host.touch_begin ||
        !g_host.touch_end || !g_host.touch_move || !g_host.key_down) {
        runtime_shutdown();
        return 7;
    }

    apk_string = jni_shim_new_string(absolute_apk);
    runtime_log("Setting APK path: %s", absolute_apk);
    set_apk_path(jni_shim_env(), NULL, apk_string);
    runtime_log("RESULT: APK_PATH_SET");

    if (!create_opengl_window(1280, 720)) {
        runtime_log("RESULT: OPENGL_HOST_FAILED");
        runtime_shutdown();
        return 8;
    }
    runtime_log("RESULT: OPENGL_HOST_OK");
    runtime_log("Calling authentic Android 1.8 nativeInit(1280, 720)");
    native_init(jni_shim_env(), NULL, 1280, 720);
    g_host.native_ready = 1;
    runtime_log("RESULT: NATIVE_INIT_RETURNED");
    run_message_loop();

    g_host.native_ready = 0;
    destroy_opengl_window();
    elf_image_unload(&image);
    runtime_shutdown();
    return 0;
}
