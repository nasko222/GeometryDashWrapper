#ifndef GD18_RUNTIME_H
#define GD18_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "elf32.h"

void runtime_initialize(const char *log_path);
void runtime_shutdown(void);
void runtime_log(const char *format, ...);
void runtime_set_elf_info(void *base, size_t size, const char *path,
                          const Elf32_Phdr *phdr, uint16_t phnum);
void runtime_set_display_size(int native_width, int native_height,
                              int client_width, int client_height);

void *runtime_resolve_function(const char *name, uint32_t id);
void *runtime_resolve_object(const char *name);
void *runtime_make_lazy_thunk(uint32_t id);
void runtime_set_import_name(uint32_t id, const char *name);

#endif
