#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "loader.h"
#include "runtime.h"

typedef int (*JniOnLoadFunction)(void *java_vm, void *reserved);

static void use_executable_directory(void) {
    char path[MAX_PATH];
    char *slash;
    DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        return;
    }
    slash = strrchr(path, '\\');
    if (slash) {
        *slash = 0;
        SetCurrentDirectoryA(path);
    }
}

int main(int argc, char **argv) {
    const char *library_path = "libcocos2dcpp.so";
    int relocate_only = 0;
    ElfImage image;
    void *jni_on_load;
    void *fake_vm_table = NULL;
    void **fake_vm = &fake_vm_table;
    int result;
    int i;

    use_executable_directory();
    runtime_initialize("gd18-wrapper.log");
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--relocate-only") == 0) {
            relocate_only = 1;
        } else {
            library_path = argv[i];
        }
    }
    runtime_log("Probe mode: %s", relocate_only ? "relocation only" : "constructors + JNI_OnLoad");
    if (!elf_image_load(&image, library_path)) {
        runtime_log("RESULT: ELF_LOAD_FAILED");
        runtime_shutdown();
        return 2;
    }
    runtime_log("RESULT: ELF_RELOCATION_OK");
    if (relocate_only) {
        elf_image_unload(&image);
        runtime_shutdown();
        return 0;
    }
    if (!elf_image_run_constructors(&image)) {
        runtime_log("RESULT: ELF_CONSTRUCTORS_FAILED");
        elf_image_unload(&image);
        runtime_shutdown();
        return 3;
    }
    runtime_log("RESULT: ELF_CONSTRUCTORS_OK");
    jni_on_load = elf_image_find_export(&image, "JNI_OnLoad");
    if (!jni_on_load) {
        runtime_log("RESULT: JNI_ONLOAD_EXPORT_MISSING");
        elf_image_unload(&image);
        runtime_shutdown();
        return 4;
    }
    runtime_log("Calling the real 1.8 JNI_OnLoad at %p", jni_on_load);
    result = ((JniOnLoadFunction)jni_on_load)(fake_vm, NULL);
    runtime_log("JNI_OnLoad returned 0x%08x", result);
    if (result != 0x00010004) {
        runtime_log("RESULT: JNI_ONLOAD_UNEXPECTED");
        elf_image_unload(&image);
        runtime_shutdown();
        return 5;
    }
    runtime_log("RESULT: NATIVE_1_8_PROBE_OK");
    runtime_log("This milestone loaded and executed the authentic Android 1.8 native code.");
    runtime_log("Rendering/JNI/audio/input are the next wrapper layer; this probe is not playable yet.");
    elf_image_unload(&image);
    runtime_shutdown();
    return 0;
}
