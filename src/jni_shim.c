#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_win.h"
#include "jni_shim.h"
#include "runtime.h"

#define JNI_TABLE_SIZE 233
#define JNI_VERSION_1_4 0x00010004
#define JNI_OK 0
#define JNI_EDETACHED (-2)

enum FakeKind {
    FAKE_CLASS = 1,
    FAKE_METHOD,
    FAKE_STRING,
    FAKE_BYTE_ARRAY,
    FAKE_INT_ARRAY,
    FAKE_FLOAT_ARRAY,
    FAKE_OBJECT
};

typedef struct FakeRef {
    uint32_t magic;
    enum FakeKind kind;
    char *class_name;
    char *name;
    char *signature;
    void *data;
    size_t length;
    unsigned calls;
} FakeRef;

typedef struct {
    void **functions;
} FakeJNIEnv;

typedef struct {
    void **functions;
} FakeJavaVM;

static void *g_jni_table[JNI_TABLE_SIZE];
static void *g_vm_table[8];
static FakeJNIEnv g_environment = {g_jni_table};
static FakeJavaVM g_java_vm = {g_vm_table};
static char g_writable_path[MAX_PATH * 2];
static double g_frame_interval = 1.0 / 60.0;
static int g_text_input_active;

static char *duplicate_string(const char *value) {
    size_t length;
    char *copy;
    if (!value) {
        value = "";
    }
    length = strlen(value) + 1;
    copy = (char *)malloc(length);
    if (copy) {
        memcpy(copy, value, length);
    }
    return copy;
}

static FakeRef *new_reference(enum FakeKind kind) {
    FakeRef *reference = (FakeRef *)calloc(1, sizeof(*reference));
    if (reference) {
        reference->magic = 0x4a4e4938u;
        reference->kind = kind;
    }
    return reference;
}

static FakeRef *checked_reference(void *value) {
    FakeRef *reference = (FakeRef *)value;
    if (!reference || reference->magic != 0x4a4e4938u) {
        return NULL;
    }
    return reference;
}

void *jni_shim_new_string(const char *value) {
    FakeRef *string = new_reference(FAKE_STRING);
    if (!string) {
        return NULL;
    }
    string->data = duplicate_string(value);
    string->length = string->data ? strlen((const char *)string->data) : 0;
    return string;
}

static void *new_array(enum FakeKind kind, const void *values, size_t count,
                       size_t element_size) {
    FakeRef *array = new_reference(kind);
    if (!array) {
        return NULL;
    }
    array->length = count;
    if (count) {
        array->data = calloc(count, element_size);
        if (!array->data) {
            return NULL;
        }
        if (values) {
            memcpy(array->data, values, count * element_size);
        }
    }
    return array;
}

void *jni_shim_new_int_array(const int32_t *values, size_t count) {
    return new_array(FAKE_INT_ARRAY, values, count, sizeof(int32_t));
}

void *jni_shim_new_float_array(const float *values, size_t count) {
    return new_array(FAKE_FLOAT_ARRAY, values, count, sizeof(float));
}

int jni_shim_update_int_array(void *array_object, const int32_t *values,
                              size_t count) {
    FakeRef *array = checked_reference(array_object);
    if (!array || array->kind != FAKE_INT_ARRAY || count > array->length ||
        (count && !values)) {
        return 0;
    }
    if (count) {
        memcpy(array->data, values, count * sizeof(*values));
    }
    return 1;
}

int jni_shim_update_float_array(void *array_object, const float *values,
                                size_t count) {
    FakeRef *array = checked_reference(array_object);
    if (!array || array->kind != FAKE_FLOAT_ARRAY || count > array->length ||
        (count && !values)) {
        return 0;
    }
    if (count) {
        memcpy(array->data, values, count * sizeof(*values));
    }
    return 1;
}

static uintptr_t jni_stub_zero(void) {
    return 0;
}

static int jni_get_version(void *environment) {
    (void)environment;
    return JNI_VERSION_1_4;
}

static void *jni_find_class(void *environment, const char *name) {
    FakeRef *reference = new_reference(FAKE_CLASS);
    (void)environment;
    if (reference) {
        reference->class_name = duplicate_string(name);
    }
    runtime_log("JNI FindClass: %s", name ? name : "<null>");
    return reference;
}

static void *jni_new_global_ref(void *environment, void *reference) {
    (void)environment;
    return reference;
}

static void jni_delete_ref(void *environment, void *reference) {
    (void)environment;
    (void)reference;
    /* References are intentionally arena-like for this diagnostic process. */
}

static int jni_is_same_object(void *environment, void *first, void *second) {
    (void)environment;
    return first == second;
}

static void *jni_get_object_class(void *environment, void *object) {
    FakeRef *reference = checked_reference(object);
    (void)environment;
    if (reference && reference->class_name) {
        return jni_find_class(environment, reference->class_name);
    }
    return jni_find_class(environment, "java/lang/Object");
}

static void *new_method(void *class_object, const char *name, const char *signature) {
    FakeRef *class_reference = checked_reference(class_object);
    FakeRef *method = new_reference(FAKE_METHOD);
    if (!method) {
        return NULL;
    }
    method->class_name = duplicate_string(
        class_reference && class_reference->class_name ? class_reference->class_name : "?");
    method->name = duplicate_string(name);
    method->signature = duplicate_string(signature);
    runtime_log("JNI method: %s.%s %s", method->class_name, method->name,
                method->signature);
    return method;
}

static void *jni_get_method_id(void *environment, void *class_object,
                               const char *name, const char *signature) {
    (void)environment;
    return new_method(class_object, name, signature);
}

static void *jni_get_static_method_id(void *environment, void *class_object,
                                      const char *name, const char *signature) {
    (void)environment;
    return new_method(class_object, name, signature);
}

static void log_first_call(FakeRef *method) {
    if (method && method->calls++ == 0) {
        runtime_log("JNI call: %s.%s %s", method->class_name ? method->class_name : "?",
                    method->name ? method->name : "?",
                    method->signature ? method->signature : "?");
    }
}

static void *dispatch_object(FakeRef *method, va_list arguments) {
    const char *name = method && method->name ? method->name : "";
    log_first_call(method);
    if (strcmp(name, "getCocos2dxPackageName") == 0) {
        return jni_shim_new_string("com.robtopx.geometryjump");
    }
    if (strcmp(name, "getCocos2dxWritablePath") == 0) {
        return jni_shim_new_string(g_writable_path);
    }
    if (strcmp(name, "getCurrentLanguage") == 0) {
        return jni_shim_new_string("en");
    }
    if (strcmp(name, "getDeviceModel") == 0) {
        return jni_shim_new_string("Windows Native Wrapper");
    }
    if (strcmp(name, "getUserID") == 0) {
        return jni_shim_new_string("0");
    }
    if (strcmp(name, "getStringForKey") == 0) {
        (void)va_arg(arguments, void *);
        return va_arg(arguments, void *);
    }
    if (strcmp(name, "getStringWithEllipsis") == 0) {
        return va_arg(arguments, void *);
    }
    if (strcmp(name, "loadAndDecryptFileToString") == 0 ||
        strcmp(name, "getItem") == 0) {
        return jni_shim_new_string("");
    }
    return new_reference(FAKE_OBJECT);
}

static int dispatch_boolean(FakeRef *method, va_list arguments) {
    const char *name = method && method->name ? method->name : "";
    log_first_call(method);
    if (strcmp(name, "getBoolForKey") == 0) {
        (void)va_arg(arguments, void *);
        return va_arg(arguments, int) ? 1 : 0;
    }
    if (strcmp(name, "shouldResumeSound") == 0) {
        return 1;
    }
    if (strcmp(name, "isBackgroundMusicPlaying") == 0) {
        return audio_is_background_playing();
    }
    if (strcmp(name, "doesFileExist") == 0) {
        FakeRef *path = checked_reference(va_arg(arguments, void *));
        return path && path->data &&
               GetFileAttributesA((const char *)path->data) != INVALID_FILE_ATTRIBUTES;
    }
    return 0;
}

static int dispatch_int(FakeRef *method, va_list arguments) {
    const char *name = method && method->name ? method->name : "";
    log_first_call(method);
    if (strcmp(name, "getDPI") == 0) {
        return 96;
    }
    if (strcmp(name, "getIntegerForKey") == 0) {
        (void)va_arg(arguments, void *);
        return va_arg(arguments, int);
    }
    if (strcmp(name, "getFontSizeAccordingHeight") == 0) {
        return va_arg(arguments, int);
    }
    if (strcmp(name, "playEffect") == 0) {
        FakeRef *path = checked_reference(va_arg(arguments, void *));
        int loop = va_arg(arguments, int);
        return (int)audio_play_effect(
            path && path->data ? (const char *)path->data : "", loop);
    }
    return 0;
}

static float dispatch_float(FakeRef *method, va_list arguments) {
    const char *name = method && method->name ? method->name : "";
    log_first_call(method);
    if (strcmp(name, "getFloatForKey") == 0) {
        (void)va_arg(arguments, void *);
        return (float)va_arg(arguments, double);
    }
    if (strcmp(name, "getBackgroundMusicVolume") == 0)
        return audio_get_background_volume();
    if (strcmp(name, "getBackgroundMusicTime") == 0)
        return audio_get_background_time();
    if (strcmp(name, "getEffectsVolume") == 0)
        return audio_get_effects_volume();
    return 0.0f;
}

static double dispatch_double(FakeRef *method, va_list arguments) {
    const char *name = method && method->name ? method->name : "";
    log_first_call(method);
    if (strcmp(name, "getDoubleForKey") == 0) {
        (void)va_arg(arguments, void *);
        return va_arg(arguments, double);
    }
    return 0.0;
}

static void dispatch_void(FakeRef *method, va_list arguments) {
    const char *name = method && method->name ? method->name : "";
    log_first_call(method);
    if (strcmp(name, "setAnimationInterval") == 0) {
        double interval = va_arg(arguments, double);
        if (interval > 0.001 && interval < 1.0) {
            g_frame_interval = interval;
            runtime_log("JNI animation interval: %.6f", interval);
        }
    } else if (strcmp(name, "terminateProcess") == 0) {
        PostQuitMessage(0);
    } else if (strcmp(name, "showDialog") == 0) {
        FakeRef *title = checked_reference(va_arg(arguments, void *));
        FakeRef *message = checked_reference(va_arg(arguments, void *));
        runtime_log("Dialog requested: %s - %s",
                    title && title->data ? (char *)title->data : "",
                    message && message->data ? (char *)message->data : "");
    } else if (strcmp(name, "openIMEKeyboard") == 0) {
        g_text_input_active = 1;
        runtime_log("Text input bridge: active");
    } else if (strcmp(name, "closeIMEKeyboard") == 0) {
        g_text_input_active = 0;
        runtime_log("Text input bridge: inactive");
    } else if (strcmp(name, "setKeyboardState") == 0) {
        g_text_input_active = va_arg(arguments, int) != 0;
        runtime_log("Text input bridge: %s",
                    g_text_input_active ? "active" : "inactive");
    } else if (strcmp(name, "showEditTextDialog") == 0) {
        g_text_input_active = 1;
        runtime_log("Text input bridge: edit dialog active");
    } else if (strcmp(name, "playBackgroundMusic") == 0) {
        FakeRef *path = checked_reference(va_arg(arguments, void *));
        int loop = va_arg(arguments, int);
        audio_play_background(
            path && path->data ? (const char *)path->data : "", loop);
    } else if (strcmp(name, "preloadBackgroundMusic") == 0) {
        FakeRef *path = checked_reference(va_arg(arguments, void *));
        audio_preload_background(
            path && path->data ? (const char *)path->data : "");
    } else if (strcmp(name, "stopBackgroundMusic") == 0) {
        audio_stop_background();
    } else if (strcmp(name, "pauseBackgroundMusic") == 0) {
        audio_pause_background();
    } else if (strcmp(name, "resumeBackgroundMusic") == 0) {
        audio_resume_background();
    } else if (strcmp(name, "rewindBackgroundMusic") == 0) {
        audio_rewind_background();
    } else if (strcmp(name, "setBackgroundMusicTime") == 0) {
        audio_set_background_time((float)va_arg(arguments, double));
    } else if (strcmp(name, "setBackgroundMusicVolume") == 0) {
        audio_set_background_volume((float)va_arg(arguments, double));
    } else if (strcmp(name, "preloadEffect") == 0) {
        FakeRef *path = checked_reference(va_arg(arguments, void *));
        audio_preload_effect(
            path && path->data ? (const char *)path->data : "");
    } else if (strcmp(name, "pauseEffect") == 0) {
        audio_pause_effect((unsigned)va_arg(arguments, int));
    } else if (strcmp(name, "resumeEffect") == 0) {
        audio_resume_effect((unsigned)va_arg(arguments, int));
    } else if (strcmp(name, "stopEffect") == 0) {
        audio_stop_effect((unsigned)va_arg(arguments, int));
    } else if (strcmp(name, "pauseAllEffects") == 0) {
        audio_pause_all_effects();
    } else if (strcmp(name, "resumeAllEffects") == 0) {
        audio_resume_all_effects();
    } else if (strcmp(name, "stopAllEffects") == 0) {
        audio_stop_all_effects();
    } else if (strcmp(name, "unloadEffect") == 0) {
        FakeRef *path = checked_reference(va_arg(arguments, void *));
        audio_unload_effect(
            path && path->data ? (const char *)path->data : "");
    } else if (strcmp(name, "setEffectsVolume") == 0) {
        audio_set_effects_volume((float)va_arg(arguments, double));
    } else if (strcmp(name, "end") == 0) {
        audio_shutdown();
    }
}

#define DEFINE_OBJECT_CALL(prefix) \
    static void *prefix(void *environment, void *target, void *method_id, ...) { \
        va_list args; void *result; (void)environment; (void)target; \
        va_start(args, method_id); result = dispatch_object(checked_reference(method_id), args); \
        va_end(args); return result; } \
    static void *prefix##_v(void *environment, void *target, void *method_id, va_list args) { \
        (void)environment; (void)target; return dispatch_object(checked_reference(method_id), args); }

#define DEFINE_BOOLEAN_CALL(prefix) \
    static unsigned char prefix(void *environment, void *target, void *method_id, ...) { \
        va_list args; int result; (void)environment; (void)target; \
        va_start(args, method_id); result = dispatch_boolean(checked_reference(method_id), args); \
        va_end(args); return (unsigned char)result; } \
    static unsigned char prefix##_v(void *environment, void *target, void *method_id, va_list args) { \
        (void)environment; (void)target; return (unsigned char)dispatch_boolean(checked_reference(method_id), args); }

#define DEFINE_INT_CALL(prefix) \
    static int prefix(void *environment, void *target, void *method_id, ...) { \
        va_list args; int result; (void)environment; (void)target; \
        va_start(args, method_id); result = dispatch_int(checked_reference(method_id), args); \
        va_end(args); return result; } \
    static int prefix##_v(void *environment, void *target, void *method_id, va_list args) { \
        (void)environment; (void)target; return dispatch_int(checked_reference(method_id), args); }

#define DEFINE_FLOAT_CALL(prefix) \
    static float prefix(void *environment, void *target, void *method_id, ...) { \
        va_list args; float result; (void)environment; (void)target; \
        va_start(args, method_id); result = dispatch_float(checked_reference(method_id), args); \
        va_end(args); return result; } \
    static float prefix##_v(void *environment, void *target, void *method_id, va_list args) { \
        (void)environment; (void)target; return dispatch_float(checked_reference(method_id), args); }

#define DEFINE_DOUBLE_CALL(prefix) \
    static double prefix(void *environment, void *target, void *method_id, ...) { \
        va_list args; double result; (void)environment; (void)target; \
        va_start(args, method_id); result = dispatch_double(checked_reference(method_id), args); \
        va_end(args); return result; } \
    static double prefix##_v(void *environment, void *target, void *method_id, va_list args) { \
        (void)environment; (void)target; return dispatch_double(checked_reference(method_id), args); }

#define DEFINE_VOID_CALL(prefix) \
    static void prefix(void *environment, void *target, void *method_id, ...) { \
        va_list args; (void)environment; (void)target; \
        va_start(args, method_id); dispatch_void(checked_reference(method_id), args); va_end(args); } \
    static void prefix##_v(void *environment, void *target, void *method_id, va_list args) { \
        (void)environment; (void)target; dispatch_void(checked_reference(method_id), args); }

DEFINE_OBJECT_CALL(jni_call_object)
DEFINE_BOOLEAN_CALL(jni_call_boolean)
DEFINE_INT_CALL(jni_call_int)
DEFINE_FLOAT_CALL(jni_call_float)
DEFINE_DOUBLE_CALL(jni_call_double)
DEFINE_VOID_CALL(jni_call_void)

DEFINE_OBJECT_CALL(jni_call_static_object)
DEFINE_BOOLEAN_CALL(jni_call_static_boolean)
DEFINE_INT_CALL(jni_call_static_int)
DEFINE_FLOAT_CALL(jni_call_static_float)
DEFINE_DOUBLE_CALL(jni_call_static_double)
DEFINE_VOID_CALL(jni_call_static_void)

static void *jni_new_object(void *environment, void *class_object, void *method_id, ...) {
    FakeRef *object = new_reference(FAKE_OBJECT);
    FakeRef *class_reference = checked_reference(class_object);
    (void)environment;
    (void)method_id;
    if (object && class_reference) {
        object->class_name = duplicate_string(class_reference->class_name);
    }
    return object;
}

static void *jni_new_object_v(void *environment, void *class_object, void *method_id,
                              va_list arguments) {
    (void)arguments;
    return jni_new_object(environment, class_object, method_id);
}

static void *jni_new_string_utf(void *environment, const char *value) {
    (void)environment;
    return jni_shim_new_string(value);
}

static int jni_get_string_utf_length(void *environment, void *string_object) {
    FakeRef *string = checked_reference(string_object);
    (void)environment;
    return string && string->kind == FAKE_STRING ? (int)string->length : 0;
}

static const char *jni_get_string_utf_chars(void *environment, void *string_object,
                                            unsigned char *is_copy) {
    FakeRef *string = checked_reference(string_object);
    (void)environment;
    if (is_copy) {
        *is_copy = 0;
    }
    return string && string->kind == FAKE_STRING ? (const char *)string->data : "";
}

static void jni_release_string_utf_chars(void *environment, void *string_object,
                                         const char *characters) {
    (void)environment;
    (void)string_object;
    (void)characters;
}

static int jni_get_array_length(void *environment, void *array_object) {
    FakeRef *array = checked_reference(array_object);
    (void)environment;
    return array ? (int)array->length : 0;
}

static void *jni_new_byte_array(void *environment, int length) {
    (void)environment;
    return new_array(FAKE_BYTE_ARRAY, NULL, length > 0 ? (size_t)length : 0, 1);
}

static void *jni_new_int_array(void *environment, int length) {
    (void)environment;
    return new_array(FAKE_INT_ARRAY, NULL, length > 0 ? (size_t)length : 0,
                     sizeof(int32_t));
}

static void *jni_new_float_array(void *environment, int length) {
    (void)environment;
    return new_array(FAKE_FLOAT_ARRAY, NULL, length > 0 ? (size_t)length : 0,
                     sizeof(float));
}

static void *jni_get_array_elements(void *environment, void *array_object,
                                    unsigned char *is_copy) {
    FakeRef *array = checked_reference(array_object);
    (void)environment;
    if (is_copy) {
        *is_copy = 0;
    }
    return array ? array->data : NULL;
}

static void jni_release_array_elements(void *environment, void *array_object,
                                       void *elements, int mode) {
    (void)environment;
    (void)array_object;
    (void)elements;
    (void)mode;
}

static void jni_get_byte_array_region(void *environment, void *array_object,
                                      int start, int length, int8_t *values) {
    FakeRef *array = checked_reference(array_object);
    (void)environment;
    if (array && array->kind == FAKE_BYTE_ARRAY && values && start >= 0 &&
        length >= 0 && (size_t)start <= array->length &&
        (size_t)length <= array->length - (size_t)start) {
        memcpy(values, (const int8_t *)array->data + start, (size_t)length);
    }
}

static void jni_get_int_array_region(void *environment, void *array_object,
                                     int start, int length, int32_t *values) {
    FakeRef *array = checked_reference(array_object);
    (void)environment;
    if (array && array->kind == FAKE_INT_ARRAY && values && start >= 0 &&
        length >= 0 && (size_t)start <= array->length &&
        (size_t)length <= array->length - (size_t)start) {
        memcpy(values, (const int32_t *)array->data + start,
               (size_t)length * sizeof(*values));
    }
}

static void jni_get_float_array_region(void *environment, void *array_object,
                                       int start, int length, float *values) {
    FakeRef *array = checked_reference(array_object);
    (void)environment;
    if (array && array->kind == FAKE_FLOAT_ARRAY && values && start >= 0 &&
        length >= 0 && (size_t)start <= array->length &&
        (size_t)length <= array->length - (size_t)start) {
        memcpy(values, (const float *)array->data + start,
               (size_t)length * sizeof(*values));
    }
}

static void jni_set_byte_array_region(void *environment, void *array_object,
                                      int start, int length, const int8_t *values) {
    FakeRef *array = checked_reference(array_object);
    (void)environment;
    if (array && array->kind == FAKE_BYTE_ARRAY && start >= 0 && length >= 0 &&
        (size_t)(start + length) <= array->length) {
        memcpy((unsigned char *)array->data + start, values, (size_t)length);
    }
}

static void jni_set_int_array_region(void *environment, void *array_object,
                                     int start, int length, const int32_t *values) {
    FakeRef *array = checked_reference(array_object);
    (void)environment;
    if (array && array->kind == FAKE_INT_ARRAY && start >= 0 && length >= 0 &&
        (size_t)(start + length) <= array->length) {
        memcpy((int32_t *)array->data + start, values, (size_t)length * sizeof(int32_t));
    }
}

static void jni_set_float_array_region(void *environment, void *array_object,
                                       int start, int length, const float *values) {
    FakeRef *array = checked_reference(array_object);
    (void)environment;
    if (array && array->kind == FAKE_FLOAT_ARRAY && start >= 0 && length >= 0 &&
        (size_t)(start + length) <= array->length) {
        memcpy((float *)array->data + start, values, (size_t)length * sizeof(float));
    }
}

static int jni_get_java_vm(void *environment, void **java_vm) {
    (void)environment;
    if (java_vm) {
        *java_vm = &g_java_vm;
    }
    return JNI_OK;
}

static int jni_exception_check(void *environment) {
    (void)environment;
    return 0;
}

static void *jni_exception_occurred(void *environment) {
    (void)environment;
    return NULL;
}

static void jni_exception_clear(void *environment) {
    (void)environment;
}

static int vm_attach_current_thread(void *java_vm, void **environment, void *arguments) {
    (void)java_vm;
    (void)arguments;
    if (environment) {
        *environment = &g_environment;
    }
    return JNI_OK;
}

static int vm_detach_current_thread(void *java_vm) {
    (void)java_vm;
    return JNI_OK;
}

static int vm_get_env(void *java_vm, void **environment, int version) {
    (void)java_vm;
    if (version > JNI_VERSION_1_4) {
        return JNI_EDETACHED;
    }
    if (environment) {
        *environment = &g_environment;
    }
    return JNI_OK;
}

void jni_shim_initialize(const char *executable_directory) {
    size_t i;
    snprintf(g_writable_path, sizeof(g_writable_path), "%s/save/",
             executable_directory ? executable_directory : ".");
    for (i = 0; g_writable_path[i]; ++i) {
        if (g_writable_path[i] == '\\') {
            g_writable_path[i] = '/';
        }
    }
    CreateDirectoryA(g_writable_path, NULL);
    audio_initialize(executable_directory);

    for (i = 0; i < JNI_TABLE_SIZE; ++i) {
        g_jni_table[i] = (void *)jni_stub_zero;
    }
    memset(g_vm_table, 0, sizeof(g_vm_table));

    g_jni_table[4] = (void *)jni_get_version;
    g_jni_table[6] = (void *)jni_find_class;
    g_jni_table[15] = (void *)jni_exception_occurred;
    g_jni_table[17] = (void *)jni_exception_clear;
    g_jni_table[21] = (void *)jni_new_global_ref;
    g_jni_table[22] = (void *)jni_delete_ref;
    g_jni_table[23] = (void *)jni_delete_ref;
    g_jni_table[24] = (void *)jni_is_same_object;
    g_jni_table[25] = (void *)jni_new_global_ref;
    g_jni_table[28] = (void *)jni_new_object;
    g_jni_table[29] = (void *)jni_new_object_v;
    g_jni_table[31] = (void *)jni_get_object_class;
    g_jni_table[33] = (void *)jni_get_method_id;
    g_jni_table[34] = (void *)jni_call_object;
    g_jni_table[35] = (void *)jni_call_object_v;
    g_jni_table[37] = (void *)jni_call_boolean;
    g_jni_table[38] = (void *)jni_call_boolean_v;
    g_jni_table[49] = (void *)jni_call_int;
    g_jni_table[50] = (void *)jni_call_int_v;
    g_jni_table[55] = (void *)jni_call_float;
    g_jni_table[56] = (void *)jni_call_float_v;
    g_jni_table[58] = (void *)jni_call_double;
    g_jni_table[59] = (void *)jni_call_double_v;
    g_jni_table[61] = (void *)jni_call_void;
    g_jni_table[62] = (void *)jni_call_void_v;
    g_jni_table[113] = (void *)jni_get_static_method_id;
    g_jni_table[114] = (void *)jni_call_static_object;
    g_jni_table[115] = (void *)jni_call_static_object_v;
    g_jni_table[117] = (void *)jni_call_static_boolean;
    g_jni_table[118] = (void *)jni_call_static_boolean_v;
    g_jni_table[129] = (void *)jni_call_static_int;
    g_jni_table[130] = (void *)jni_call_static_int_v;
    g_jni_table[135] = (void *)jni_call_static_float;
    g_jni_table[136] = (void *)jni_call_static_float_v;
    g_jni_table[138] = (void *)jni_call_static_double;
    g_jni_table[139] = (void *)jni_call_static_double_v;
    g_jni_table[141] = (void *)jni_call_static_void;
    g_jni_table[142] = (void *)jni_call_static_void_v;
    g_jni_table[167] = (void *)jni_new_string_utf;
    g_jni_table[168] = (void *)jni_get_string_utf_length;
    g_jni_table[169] = (void *)jni_get_string_utf_chars;
    g_jni_table[170] = (void *)jni_release_string_utf_chars;
    g_jni_table[171] = (void *)jni_get_array_length;
    g_jni_table[176] = (void *)jni_new_byte_array;
    g_jni_table[179] = (void *)jni_new_int_array;
    g_jni_table[181] = (void *)jni_new_float_array;
    g_jni_table[184] = (void *)jni_get_array_elements;
    g_jni_table[187] = (void *)jni_get_array_elements;
    g_jni_table[189] = (void *)jni_get_array_elements;
    g_jni_table[192] = (void *)jni_release_array_elements;
    g_jni_table[195] = (void *)jni_release_array_elements;
    g_jni_table[197] = (void *)jni_release_array_elements;
    g_jni_table[200] = (void *)jni_get_byte_array_region;
    g_jni_table[203] = (void *)jni_get_int_array_region;
    g_jni_table[205] = (void *)jni_get_float_array_region;
    g_jni_table[208] = (void *)jni_set_byte_array_region;
    g_jni_table[211] = (void *)jni_set_int_array_region;
    g_jni_table[213] = (void *)jni_set_float_array_region;
    g_jni_table[219] = (void *)jni_get_java_vm;
    g_jni_table[228] = (void *)jni_exception_check;

    g_vm_table[4] = (void *)vm_attach_current_thread;
    g_vm_table[5] = (void *)vm_detach_current_thread;
    g_vm_table[6] = (void *)vm_get_env;
    g_vm_table[7] = (void *)vm_attach_current_thread;
    runtime_log("JNI shim initialized; writable path: %s", g_writable_path);
}

void jni_shim_shutdown(void) {
    audio_shutdown();
}

void *jni_shim_env(void) {
    return &g_environment;
}

void *jni_shim_vm(void) {
    return &g_java_vm;
}

double jni_shim_frame_interval(void) {
    return g_frame_interval;
}

int jni_shim_text_input_active(void) {
    return g_text_input_active;
}
