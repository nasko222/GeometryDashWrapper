#ifndef GD18_JNI_SHIM_H
#define GD18_JNI_SHIM_H

#include <stddef.h>
#include <stdint.h>

void jni_shim_initialize(const char *executable_directory);
void jni_shim_shutdown(void);
void *jni_shim_env(void);
void *jni_shim_vm(void);
void *jni_shim_new_string(const char *value);
void *jni_shim_new_int_array(const int32_t *values, size_t count);
void *jni_shim_new_float_array(const float *values, size_t count);
double jni_shim_frame_interval(void);
int jni_shim_text_input_active(void);

#endif
