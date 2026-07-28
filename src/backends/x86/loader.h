#ifndef GD18_LOADER_H
#define GD18_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "elf32.h"

typedef int (*ElfExportVisitor)(const char *name, void *address,
                                uint32_t size, void *opaque);

typedef struct {
    unsigned char *file_data;
    size_t file_size;
    unsigned char *base;
    size_t mapped_size;
    const Elf32_Ehdr *header;
    const Elf32_Phdr *program_headers;
    const Elf32_Shdr *section_headers;
    const char *section_names;
    uint32_t imported_functions;
    uint32_t imported_objects;
} ElfImage;

int elf_image_load(ElfImage *image, const char *path);
int elf_image_load_from_apk(ElfImage *image, const char *apk_path,
                            const char *member_name);
int elf_image_load_game_from_apk(ElfImage *image, const char *apk_path);
int apk_extract_member(const char *apk_path, const char *member_name,
                       unsigned char **output, size_t *output_size);
void elf_image_unload(ElfImage *image);
void *elf_image_find_export(const ElfImage *image, const char *name);
int elf_image_visit_exports(const ElfImage *image, ElfExportVisitor visitor,
                            void *opaque);
int elf_image_run_constructors(const ElfImage *image);

#endif
