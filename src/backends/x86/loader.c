#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader.h"
#include "runtime.h"
#include "zlib.h"

static int range_valid(size_t offset, size_t length, size_t total) {
    return offset <= total && length <= total - offset;
}

static const Elf32_Shdr *section_at(const ElfImage *image, uint32_t index) {
    if (index >= image->header->e_shnum) {
        return NULL;
    }
    return &image->section_headers[index];
}

static int read_entire_file(const char *path, unsigned char **data, size_t *size) {
    FILE *stream = fopen(path, "rb");
    long length;
    unsigned char *buffer;
    if (!stream) {
        runtime_log("ERROR: cannot open %s", path);
        return 0;
    }
    if (fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) <= 0 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        runtime_log("ERROR: cannot determine ELF file size");
        return 0;
    }
    buffer = (unsigned char *)malloc((size_t)length);
    if (!buffer || fread(buffer, 1, (size_t)length, stream) != (size_t)length) {
        free(buffer);
        fclose(stream);
        runtime_log("ERROR: cannot read complete ELF file");
        return 0;
    }
    fclose(stream);
    *data = buffer;
    *size = (size_t)length;
    return 1;
}

static uint16_t read_u16(const unsigned char *value) {
    return (uint16_t)((uint16_t)value[0] | ((uint16_t)value[1] << 8));
}

static uint32_t read_u32(const unsigned char *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int apk_extract_member_internal(const char *apk_path,
                                       const char *member_name,
                                       unsigned char **output,
                                       size_t *output_size,
                                       int report_missing) {
    unsigned char *apk = NULL;
    size_t apk_size = 0;
    size_t eocd_position;
    size_t search_limit;
    size_t central_position;
    uint16_t entry_count;
    uint16_t entry_index;
    size_t member_length = strlen(member_name);
    int found = 0;

    if (!read_entire_file(apk_path, &apk, &apk_size) || apk_size < 22) {
        free(apk);
        return 0;
    }
    eocd_position = apk_size - 22;
    search_limit = eocd_position > 0xffffu ? eocd_position - 0xffffu : 0;
    for (;;) {
        if (read_u32(apk + eocd_position) == 0x06054b50u &&
            range_valid(eocd_position, 22, apk_size) &&
            range_valid(eocd_position + 22,
                        read_u16(apk + eocd_position + 20), apk_size)) {
            found = 1;
            break;
        }
        if (eocd_position == search_limit) {
            break;
        }
        --eocd_position;
    }
    if (!found || read_u16(apk + eocd_position + 4) != 0 ||
        read_u16(apk + eocd_position + 6) != 0) {
        runtime_log("ERROR: APK has no supported ZIP central directory");
        free(apk);
        return 0;
    }
    entry_count = read_u16(apk + eocd_position + 10);
    central_position = read_u32(apk + eocd_position + 16);
    if (!range_valid(central_position,
                     read_u32(apk + eocd_position + 12), apk_size)) {
        runtime_log("ERROR: APK central directory is outside the file");
        free(apk);
        return 0;
    }

    for (entry_index = 0; entry_index < entry_count; ++entry_index) {
        uint16_t name_length;
        uint16_t extra_length;
        uint16_t comment_length;
        size_t entry_size;
        uint16_t flags;
        uint16_t method;
        uint32_t expected_crc;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint32_t local_offset;
        size_t data_offset;
        unsigned char *payload;

        if (!range_valid(central_position, 46, apk_size) ||
            read_u32(apk + central_position) != 0x02014b50u) {
            runtime_log("ERROR: malformed APK central-directory entry");
            break;
        }
        name_length = read_u16(apk + central_position + 28);
        extra_length = read_u16(apk + central_position + 30);
        comment_length = read_u16(apk + central_position + 32);
        entry_size = 46u + name_length + extra_length + comment_length;
        if (!range_valid(central_position, entry_size, apk_size)) {
            runtime_log("ERROR: truncated APK central-directory entry");
            break;
        }
        if (name_length != member_length ||
            memcmp(apk + central_position + 46, member_name, member_length) != 0) {
            central_position += entry_size;
            continue;
        }

        flags = read_u16(apk + central_position + 8);
        method = read_u16(apk + central_position + 10);
        expected_crc = read_u32(apk + central_position + 16);
        compressed_size = read_u32(apk + central_position + 20);
        uncompressed_size = read_u32(apk + central_position + 24);
        local_offset = read_u32(apk + central_position + 42);
        if ((flags & 1u) != 0 || (method != 0 && method != 8) ||
            !uncompressed_size || !range_valid(local_offset, 30, apk_size) ||
            read_u32(apk + local_offset) != 0x04034b50u) {
            runtime_log("ERROR: unsupported or malformed APK member: %s",
                        member_name);
            break;
        }
        data_offset = (size_t)local_offset + 30u +
                      read_u16(apk + local_offset + 26) +
                      read_u16(apk + local_offset + 28);
        if (!range_valid(data_offset, compressed_size, apk_size)) {
            runtime_log("ERROR: APK member data is truncated: %s", member_name);
            break;
        }
        payload = (unsigned char *)malloc(uncompressed_size);
        if (!payload) {
            runtime_log("ERROR: cannot allocate %lu bytes for APK member",
                        (unsigned long)uncompressed_size);
            break;
        }
        if (method == 0) {
            if (compressed_size != uncompressed_size) {
                free(payload);
                runtime_log("ERROR: invalid stored APK member size");
                break;
            }
            memcpy(payload, apk + data_offset, uncompressed_size);
        } else {
            z_stream stream;
            int inflate_result;
            memset(&stream, 0, sizeof(stream));
            stream.next_in = apk + data_offset;
            stream.avail_in = compressed_size;
            stream.next_out = payload;
            stream.avail_out = uncompressed_size;
            inflate_result = inflateInit2(&stream, -MAX_WBITS);
            if (inflate_result == Z_OK) {
                inflate_result = inflate(&stream, Z_FINISH);
                inflateEnd(&stream);
            }
            if (inflate_result != Z_STREAM_END ||
                stream.total_out != uncompressed_size) {
                free(payload);
                runtime_log("ERROR: cannot inflate APK member: %s", member_name);
                break;
            }
        }
        if ((uint32_t)crc32(crc32(0L, Z_NULL, 0), payload,
                            uncompressed_size) != expected_crc) {
            free(payload);
            runtime_log("ERROR: APK member CRC mismatch: %s", member_name);
            break;
        }
        *output = payload;
        *output_size = uncompressed_size;
        runtime_log("Loaded %s directly from APK (%lu bytes)", member_name,
                    (unsigned long)uncompressed_size);
        free(apk);
        return 1;
    }
    if (report_missing) {
        runtime_log("ERROR: APK does not contain a usable %s", member_name);
    }
    free(apk);
    return 0;
}

int apk_extract_member(const char *apk_path, const char *member_name,
                       unsigned char **output, size_t *output_size) {
    return apk_extract_member_internal(apk_path, member_name, output,
                                       output_size, 1);
}

static int validate_headers(ElfImage *image) {
    const Elf32_Ehdr *header;
    const Elf32_Shdr *name_section;
    if (image->file_size < sizeof(Elf32_Ehdr)) {
        runtime_log("ERROR: file is too small for an ELF header");
        return 0;
    }
    header = (const Elf32_Ehdr *)image->file_data;
    if (memcmp(header->e_ident, "\x7f" "ELF", 4) != 0 ||
        header->e_ident[4] != 1 || header->e_ident[5] != 1 ||
        header->e_type != ET_DYN || header->e_machine != EM_386) {
        runtime_log("ERROR: expected a little-endian ELF32 i386 shared object");
        return 0;
    }
    if (header->e_phentsize != sizeof(Elf32_Phdr) ||
        !range_valid(header->e_phoff,
                     (size_t)header->e_phnum * sizeof(Elf32_Phdr), image->file_size) ||
        header->e_shentsize != sizeof(Elf32_Shdr) ||
        !range_valid(header->e_shoff,
                     (size_t)header->e_shnum * sizeof(Elf32_Shdr), image->file_size)) {
        runtime_log("ERROR: malformed ELF program or section table");
        return 0;
    }
    image->header = header;
    image->program_headers =
        (const Elf32_Phdr *)(image->file_data + header->e_phoff);
    image->section_headers =
        (const Elf32_Shdr *)(image->file_data + header->e_shoff);
    name_section = section_at(image, header->e_shstrndx);
    if (!name_section ||
        !range_valid(name_section->sh_offset, name_section->sh_size, image->file_size)) {
        runtime_log("ERROR: malformed ELF section-name table");
        return 0;
    }
    image->section_names = (const char *)(image->file_data + name_section->sh_offset);
    return 1;
}

static int map_segments(ElfImage *image) {
    uint32_t maximum = 0;
    uint16_t i;
    for (i = 0; i < image->header->e_phnum; ++i) {
        const Elf32_Phdr *segment = &image->program_headers[i];
        uint64_t end;
        if (segment->p_type != PT_LOAD) {
            continue;
        }
        end = (uint64_t)segment->p_vaddr + segment->p_memsz;
        if (end > UINT32_MAX ||
            !range_valid(segment->p_offset, segment->p_filesz, image->file_size) ||
            segment->p_filesz > segment->p_memsz) {
            runtime_log("ERROR: malformed PT_LOAD segment %u", (unsigned)i);
            return 0;
        }
        if ((uint32_t)end > maximum) {
            maximum = (uint32_t)end;
        }
    }
    image->mapped_size = (maximum + 0xffffu) & ~0xffffu;
    image->base = (unsigned char *)VirtualAlloc(
        NULL, image->mapped_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!image->base) {
        runtime_log("ERROR: VirtualAlloc(%lu bytes) failed: %lu",
                    (unsigned long)image->mapped_size, (unsigned long)GetLastError());
        return 0;
    }
    for (i = 0; i < image->header->e_phnum; ++i) {
        const Elf32_Phdr *segment = &image->program_headers[i];
        if (segment->p_type == PT_LOAD && segment->p_filesz) {
            memcpy(image->base + segment->p_vaddr,
                   image->file_data + segment->p_offset, segment->p_filesz);
        }
    }
    runtime_log("Mapped ELF at %p (%lu bytes)", image->base,
                (unsigned long)image->mapped_size);
    return 1;
}

static int apply_relocation_section(ElfImage *image, const Elf32_Shdr *rel_section) {
    const Elf32_Shdr *sym_section = section_at(image, rel_section->sh_link);
    const Elf32_Shdr *str_section;
    const Elf32_Sym *symbols;
    const char *strings;
    const Elf32_Rel *relocations;
    uint32_t relocation_count;
    uint32_t symbol_count;
    uint32_t i;
    if (!sym_section || sym_section->sh_entsize != sizeof(Elf32_Sym) ||
        !range_valid(sym_section->sh_offset, sym_section->sh_size, image->file_size)) {
        runtime_log("ERROR: relocation section references an invalid symbol table");
        return 0;
    }
    str_section = section_at(image, sym_section->sh_link);
    if (!str_section ||
        !range_valid(str_section->sh_offset, str_section->sh_size, image->file_size) ||
        rel_section->sh_entsize != sizeof(Elf32_Rel) ||
        !range_valid(rel_section->sh_offset, rel_section->sh_size, image->file_size)) {
        runtime_log("ERROR: malformed relocation metadata");
        return 0;
    }
    symbols = (const Elf32_Sym *)(image->file_data + sym_section->sh_offset);
    strings = (const char *)(image->file_data + str_section->sh_offset);
    relocations = (const Elf32_Rel *)(image->file_data + rel_section->sh_offset);
    symbol_count = sym_section->sh_size / sizeof(Elf32_Sym);
    relocation_count = rel_section->sh_size / sizeof(Elf32_Rel);

    for (i = 0; i < relocation_count; ++i) {
        const Elf32_Rel *rel = &relocations[i];
        uint32_t type = ELF32_R_TYPE(rel->r_info);
        uint32_t symbol_index = ELF32_R_SYM(rel->r_info);
        uint32_t *where;
        uintptr_t value = 0;
        uintptr_t addend;
        const char *name = "";
        if (rel->r_offset > image->mapped_size - sizeof(uint32_t)) {
            runtime_log("ERROR: relocation target 0x%08lx is outside mapped image",
                        (unsigned long)rel->r_offset);
            return 0;
        }
        where = (uint32_t *)(image->base + rel->r_offset);
        addend = *where;
        if (symbol_index) {
            const Elf32_Sym *symbol;
            uint32_t symbol_type;
            if (symbol_index >= symbol_count) {
                runtime_log("ERROR: invalid relocation symbol index %lu",
                            (unsigned long)symbol_index);
                return 0;
            }
            symbol = &symbols[symbol_index];
            symbol_type = ELF32_ST_TYPE(symbol->st_info);
            if (symbol->st_name >= str_section->sh_size) {
                runtime_log("ERROR: invalid dynamic symbol name offset");
                return 0;
            }
            name = strings + symbol->st_name;
            if (symbol->st_shndx != SHN_UNDEF) {
                value = (uintptr_t)(image->base + symbol->st_value);
            } else if (symbol_type == STT_OBJECT) {
                value = (uintptr_t)runtime_resolve_object(name);
                if (!value) {
                    runtime_log("ERROR: unresolved imported object: %s", name);
                    return 0;
                }
                ++image->imported_objects;
            } else {
                uint32_t import_id = image->imported_functions++;
                runtime_set_import_name(import_id, name);
                value = (uintptr_t)runtime_make_lazy_thunk(import_id);
                if (!value) {
                    runtime_log("ERROR: could not allocate thunk for %s", name);
                    return 0;
                }
            }
        }

        switch (type) {
        case R_386_NONE:
            break;
        case R_386_32:
            *where = (uint32_t)(value + addend);
            break;
        case R_386_PC32:
            *where = (uint32_t)(value + addend - (uintptr_t)where);
            break;
        case R_386_GLOB_DAT:
        case R_386_JMP_SLOT:
            *where = (uint32_t)value;
            break;
        case R_386_RELATIVE:
            *where = (uint32_t)((uintptr_t)image->base + addend);
            break;
        case R_386_COPY:
            runtime_log("ERROR: unsupported R_386_COPY relocation for %s", name);
            return 0;
        default:
            runtime_log("ERROR: unsupported relocation type %lu at 0x%08lx",
                        (unsigned long)type, (unsigned long)rel->r_offset);
            return 0;
        }
    }
    return 1;
}

static int apply_relocations(ElfImage *image) {
    uint16_t i;
    for (i = 0; i < image->header->e_shnum; ++i) {
        const Elf32_Shdr *section = &image->section_headers[i];
        if (section->sh_type == SHT_RELA) {
            runtime_log("ERROR: ELF uses unsupported RELA relocations");
            return 0;
        }
        if (section->sh_type == SHT_REL && !apply_relocation_section(image, section)) {
            return 0;
        }
    }
    runtime_log("Applied relocations: %lu function imports, %lu object imports",
                (unsigned long)image->imported_functions,
                (unsigned long)image->imported_objects);
    return 1;
}

int elf_image_load(ElfImage *image, const char *path) {
    memset(image, 0, sizeof(*image));
    runtime_log("Opening Android game library: %s", path);
    if (!read_entire_file(path, &image->file_data, &image->file_size) ||
        !validate_headers(image) || !map_segments(image)) {
        elf_image_unload(image);
        return 0;
    }
    runtime_set_elf_info(image->base, image->mapped_size, path,
                         (const Elf32_Phdr *)(image->base + image->header->e_phoff),
                         image->header->e_phnum);
    if (!apply_relocations(image)) {
        elf_image_unload(image);
        return 0;
    }
    return 1;
}

int elf_image_load_from_apk(ElfImage *image, const char *apk_path,
                            const char *member_name) {
    memset(image, 0, sizeof(*image));
    runtime_log("Opening Android game APK: %s", apk_path);
    if (!apk_extract_member(apk_path, member_name, &image->file_data,
                            &image->file_size) ||
        !validate_headers(image) || !map_segments(image)) {
        elf_image_unload(image);
        return 0;
    }
    runtime_set_elf_info(image->base, image->mapped_size, apk_path,
                         (const Elf32_Phdr *)(image->base + image->header->e_phoff),
                         image->header->e_phnum);
    if (!apply_relocations(image)) {
        elf_image_unload(image);
        return 0;
    }
    return 1;
}

int elf_image_load_game_from_apk(ElfImage *image, const char *apk_path) {
    static const char *const members[] = {
        "lib/x86/libcocos2dcpp.so",
        "lib/x86/libgame.so",
    };
    const char *selected = NULL;
    size_t index;
    memset(image, 0, sizeof(*image));
    runtime_log("Opening Android game APK: %s", apk_path);
    for (index = 0; index < sizeof(members) / sizeof(members[0]); ++index) {
        if (apk_extract_member_internal(apk_path, members[index],
                                        &image->file_data, &image->file_size,
                                        0)) {
            selected = members[index];
            break;
        }
    }
    if (!selected) {
        runtime_log("ERROR: APK has no supported x86 game library "
                    "(expected libcocos2dcpp.so or libgame.so)");
        elf_image_unload(image);
        return 0;
    }
    runtime_log("Selected Android x86 game library: %s", selected);
    if (!validate_headers(image) || !map_segments(image)) {
        elf_image_unload(image);
        return 0;
    }
    runtime_set_elf_info(image->base, image->mapped_size, apk_path,
                         (const Elf32_Phdr *)(image->base + image->header->e_phoff),
                         image->header->e_phnum);
    if (!apply_relocations(image)) {
        elf_image_unload(image);
        return 0;
    }
    return 1;
}

void elf_image_unload(ElfImage *image) {
    if (image->base) {
        VirtualFree(image->base, 0, MEM_RELEASE);
    }
    free(image->file_data);
    memset(image, 0, sizeof(*image));
}

int elf_image_visit_exports(const ElfImage *image, ElfExportVisitor visitor,
                            void *opaque) {
    uint16_t i;
    if (!image || !image->header || !visitor) return 0;
    for (i = 0; i < image->header->e_shnum; ++i) {
        const Elf32_Shdr *symbols_section = &image->section_headers[i];
        const Elf32_Shdr *strings_section;
        const Elf32_Sym *symbols;
        const char *strings;
        uint32_t count;
        uint32_t index;
        if (symbols_section->sh_type != SHT_DYNSYM ||
            symbols_section->sh_entsize != sizeof(Elf32_Sym)) {
            continue;
        }
        strings_section = section_at(image, symbols_section->sh_link);
        if (!strings_section) continue;
        symbols = (const Elf32_Sym *)(image->file_data + symbols_section->sh_offset);
        strings = (const char *)(image->file_data + strings_section->sh_offset);
        count = symbols_section->sh_size / sizeof(Elf32_Sym);
        for (index = 0; index < count; ++index) {
            const Elf32_Sym *symbol = &symbols[index];
            const char *name;
            if (symbol->st_shndx == SHN_UNDEF || symbol->st_value == 0 ||
                symbol->st_name >= strings_section->sh_size) {
                continue;
            }
            name = strings + symbol->st_name;
            if (!*name) continue;
            if (!visitor(name, image->base + symbol->st_value,
                         symbol->st_size, opaque)) {
                return 0;
            }
        }
    }
    return 1;
}

void *elf_image_find_export(const ElfImage *image, const char *name) {
    uint16_t i;
    for (i = 0; i < image->header->e_shnum; ++i) {
        const Elf32_Shdr *symbols_section = &image->section_headers[i];
        const Elf32_Shdr *strings_section;
        const Elf32_Sym *symbols;
        const char *strings;
        uint32_t count;
        uint32_t index;
        if (symbols_section->sh_type != SHT_DYNSYM ||
            symbols_section->sh_entsize != sizeof(Elf32_Sym)) {
            continue;
        }
        strings_section = section_at(image, symbols_section->sh_link);
        if (!strings_section) {
            continue;
        }
        symbols = (const Elf32_Sym *)(image->file_data + symbols_section->sh_offset);
        strings = (const char *)(image->file_data + strings_section->sh_offset);
        count = symbols_section->sh_size / sizeof(Elf32_Sym);
        for (index = 0; index < count; ++index) {
            if (symbols[index].st_shndx != SHN_UNDEF &&
                symbols[index].st_name < strings_section->sh_size &&
                strcmp(strings + symbols[index].st_name, name) == 0) {
                return image->base + symbols[index].st_value;
            }
        }
    }
    return NULL;
}

int elf_image_run_constructors(const ElfImage *image) {
    uint16_t i;
    for (i = 0; i < image->header->e_shnum; ++i) {
        const Elf32_Shdr *section = &image->section_headers[i];
        const char *name;
        uint32_t count;
        uint32_t index;
        uintptr_t *functions;
        if (section->sh_name >=
            image->section_headers[image->header->e_shstrndx].sh_size) {
            continue;
        }
        name = image->section_names + section->sh_name;
        if (strcmp(name, ".init_array") != 0) {
            continue;
        }
        functions = (uintptr_t *)(image->base + section->sh_addr);
        count = section->sh_size / sizeof(uint32_t);
        runtime_log("Running %lu ELF constructors", (unsigned long)count);
        for (index = 0; index < count; ++index) {
            void (*constructor)(void) = (void (*)(void))functions[index];
            uintptr_t address = (uintptr_t)constructor;
            if (!address || address == UINT32_MAX) {
                continue;
            }
            runtime_log("  constructor %lu/%lu at ELF+0x%08lx", (unsigned long)(index + 1),
                        (unsigned long)count,
                        (unsigned long)(address - (uintptr_t)image->base));
            constructor();
        }
        runtime_log("All ELF constructors returned");
        return 1;
    }
    runtime_log("ERROR: ELF has no .init_array section");
    return 0;
}
