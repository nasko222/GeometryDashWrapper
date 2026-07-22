/*
 * ARM-only Android ELF execution probe for Geometry Dash 1.0-1.4 research.
 *
 * This is deliberately separate from the production x86 wrapper.  It maps an
 * ARMv5/Thumb-1 shared object into a 32-bit guest address space, applies the
 * Android REL relocations, and executes its real .init_array and JNI_OnLoad
 * through Unicorn.  Host services are guest-address traps; no ARM pointer is
 * ever cast to a native Windows function pointer.
 */

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unicorn/arm.h>
#include <unicorn/unicorn.h>

#include "elf32.h"
#include "../third_party/zlib/zlib.h"

#define EM_ARM 40
#define R_ARM_NONE 0
#define R_ARM_ABS32 2
#define R_ARM_GLOB_DAT 21
#define R_ARM_JUMP_SLOT 22
#define R_ARM_RELATIVE 23

#define GUEST_IMAGE_BASE 0x10000000u
#define GUEST_RETURN_BASE 0x0e000000u
#define GUEST_IMPORT_BASE 0x0f000000u
#define GUEST_IMPORT_SIZE 0x00100000u
#define GUEST_OBJECT_BASE 0x20000000u
#define GUEST_OBJECT_SIZE 0x00400000u
#define GUEST_HEAP_BASE 0x30000000u
#define GUEST_HEAP_SIZE 0x08000000u
#define GUEST_JNI_BASE 0x60000000u
#define GUEST_JNI_SIZE 0x00100000u
#define GUEST_STACK_BASE 0x70000000u
#define GUEST_STACK_SIZE 0x00800000u
#define GUEST_STACK_TOP (GUEST_STACK_BASE + GUEST_STACK_SIZE)
#define GUEST_KUSER_BASE 0xffff0000u
#define GUEST_KUSER_SIZE 0x00010000u

#define MAX_IMPORTS 4096
#define MAX_OBJECTS 256
#define MAX_ALLOCS 32768
#define MAX_STRING 65536

typedef struct {
    char *name;
    uint32_t address;
    unsigned calls;
} ArmImport;

typedef struct {
    uint32_t address;
    uint32_t size;
} GuestAllocation;

typedef struct {
    uc_engine *uc;
    unsigned char *file_data;
    size_t file_size;
    unsigned char *image_data;
    uint32_t image_size;
    const Elf32_Ehdr *header;
    const Elf32_Phdr *program_headers;
    const Elf32_Shdr *section_headers;
    const char *section_names;
    ArmImport imports[MAX_IMPORTS];
    unsigned import_count;
    ArmImport objects[MAX_OBJECTS];
    unsigned object_count;
    GuestAllocation allocations[MAX_ALLOCS];
    unsigned allocation_count;
    uint32_t heap_next;
    uint32_t errno_address;
    uint32_t current_entry;
    int returned;
    int failed;
    char failure[256];
} ArmProbe;

static void probe_log(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    putchar('\n');
    fflush(stdout);
    va_end(arguments);
}

static char *copy_string(const char *source) {
    size_t size = strlen(source) + 1;
    char *result = (char *)malloc(size);
    if (result) memcpy(result, source, size);
    return result;
}

static int range_valid(size_t offset, size_t length, size_t total) {
    return offset <= total && length <= total - offset;
}

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t read_u32_host(const unsigned char *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t read_u16_host(const unsigned char *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void write_u32_host(unsigned char *data, uint32_t value) {
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static int load_file(const char *path, unsigned char **data, size_t *size) {
    FILE *stream = fopen(path, "rb");
    long length;
    unsigned char *buffer;
    if (!stream) {
        probe_log("ERROR: cannot open %s", path);
        return 0;
    }
    if (fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) <= 0 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        probe_log("ERROR: cannot determine ARM ELF size");
        return 0;
    }
    buffer = (unsigned char *)malloc((size_t)length);
    if (!buffer || fread(buffer, 1, (size_t)length, stream) != (size_t)length) {
        fclose(stream);
        free(buffer);
        probe_log("ERROR: cannot read complete ARM ELF");
        return 0;
    }
    fclose(stream);
    *data = buffer;
    *size = (size_t)length;
    return 1;
}

static int apk_extract_one(const unsigned char *apk, size_t apk_size,
                           const char *member_name, unsigned char **output,
                           size_t *output_size) {
    size_t eocd;
    size_t search_limit;
    size_t central;
    uint16_t entries;
    uint16_t entry_index;
    size_t wanted_length = strlen(member_name);
    if (apk_size < 22) return 0;
    eocd = apk_size - 22;
    search_limit = eocd > 0xffffu ? eocd - 0xffffu : 0;
    for (;;) {
        if (range_valid(eocd, 22, apk_size) &&
            read_u32_host(apk + eocd) == 0x06054b50u) break;
        if (eocd == search_limit) return 0;
        --eocd;
    }
    if (read_u16_host(apk + eocd + 4) != 0 ||
        read_u16_host(apk + eocd + 6) != 0) return 0;
    entries = read_u16_host(apk + eocd + 10);
    central = read_u32_host(apk + eocd + 16);
    if (!range_valid(central, read_u32_host(apk + eocd + 12), apk_size))
        return 0;
    for (entry_index = 0; entry_index < entries; ++entry_index) {
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
        if (!range_valid(central, 46, apk_size) ||
            read_u32_host(apk + central) != 0x02014b50u) return 0;
        name_length = read_u16_host(apk + central + 28);
        extra_length = read_u16_host(apk + central + 30);
        comment_length = read_u16_host(apk + central + 32);
        entry_size = 46u + name_length + extra_length + comment_length;
        if (!range_valid(central, entry_size, apk_size)) return 0;
        if (name_length != wanted_length ||
            memcmp(apk + central + 46, member_name, wanted_length) != 0) {
            central += entry_size;
            continue;
        }
        flags = read_u16_host(apk + central + 8);
        method = read_u16_host(apk + central + 10);
        expected_crc = read_u32_host(apk + central + 16);
        compressed_size = read_u32_host(apk + central + 20);
        uncompressed_size = read_u32_host(apk + central + 24);
        local_offset = read_u32_host(apk + central + 42);
        if ((flags & 1u) != 0 || (method != 0 && method != 8) ||
            !uncompressed_size || !range_valid(local_offset, 30, apk_size) ||
            read_u32_host(apk + local_offset) != 0x04034b50u) return 0;
        data_offset = (size_t)local_offset + 30u +
                      read_u16_host(apk + local_offset + 26) +
                      read_u16_host(apk + local_offset + 28);
        if (!range_valid(data_offset, compressed_size, apk_size)) return 0;
        payload = (unsigned char *)malloc(uncompressed_size);
        if (!payload) return 0;
        if (method == 0) {
            if (compressed_size != uncompressed_size) {
                free(payload);
                return 0;
            }
            memcpy(payload, apk + data_offset, uncompressed_size);
        } else {
            z_stream stream;
            int inflate_result;
            memset(&stream, 0, sizeof(stream));
            stream.next_in = (Bytef *)(apk + data_offset);
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
                return 0;
            }
        }
        if ((uint32_t)crc32(crc32(0L, Z_NULL, 0), payload,
                            uncompressed_size) != expected_crc) {
            free(payload);
            return 0;
        }
        *output = payload;
        *output_size = uncompressed_size;
        probe_log("Loaded %s directly from APK (%u bytes)", member_name,
                  uncompressed_size);
        return 1;
    }
    return 0;
}

static int load_arm_input(const char *path, unsigned char **data,
                          size_t *size) {
    static const char *const members[] = {
        "lib/armeabi/libcocos2dcpp.so",
        "lib/armeabi/libgame.so",
        "lib/armeabi-v7a/libcocos2dcpp.so",
        "lib/armeabi-v7a/libgame.so",
    };
    unsigned char *input = NULL;
    size_t input_size = 0;
    size_t index;
    if (!load_file(path, &input, &input_size)) return 0;
    if (input_size >= 4 && memcmp(input, "\x7f" "ELF", 4) == 0) {
        *data = input;
        *size = input_size;
        return 1;
    }
    for (index = 0; index < sizeof(members) / sizeof(members[0]); ++index) {
        if (apk_extract_one(input, input_size, members[index], data, size)) {
            free(input);
            return 1;
        }
    }
    free(input);
    probe_log("ERROR: APK contains no ARM game library in a supported ABI directory");
    return 0;
}

static const Elf32_Shdr *section_at(const ArmProbe *probe, uint32_t index) {
    if (index >= probe->header->e_shnum) return NULL;
    return &probe->section_headers[index];
}

static int validate_elf(ArmProbe *probe) {
    const Elf32_Ehdr *header;
    const Elf32_Shdr *names;
    if (probe->file_size < sizeof(Elf32_Ehdr)) return 0;
    header = (const Elf32_Ehdr *)probe->file_data;
    if (memcmp(header->e_ident, "\x7f" "ELF", 4) != 0 ||
        header->e_ident[4] != 1 || header->e_ident[5] != 1 ||
        header->e_type != ET_DYN || header->e_machine != EM_ARM ||
        header->e_phentsize != sizeof(Elf32_Phdr) ||
        header->e_shentsize != sizeof(Elf32_Shdr) ||
        !range_valid(header->e_phoff,
                     (size_t)header->e_phnum * sizeof(Elf32_Phdr),
                     probe->file_size) ||
        !range_valid(header->e_shoff,
                     (size_t)header->e_shnum * sizeof(Elf32_Shdr),
                     probe->file_size)) {
        probe_log("ERROR: expected a complete little-endian ARM ELF32 shared object");
        return 0;
    }
    probe->header = header;
    probe->program_headers =
        (const Elf32_Phdr *)(probe->file_data + header->e_phoff);
    probe->section_headers =
        (const Elf32_Shdr *)(probe->file_data + header->e_shoff);
    names = section_at(probe, header->e_shstrndx);
    if (!names || !range_valid(names->sh_offset, names->sh_size,
                               probe->file_size)) {
        probe_log("ERROR: malformed section-name table");
        return 0;
    }
    probe->section_names = (const char *)(probe->file_data + names->sh_offset);
    return 1;
}

static int build_image(ArmProbe *probe) {
    uint32_t maximum = 0;
    uint16_t index;
    for (index = 0; index < probe->header->e_phnum; ++index) {
        const Elf32_Phdr *segment = &probe->program_headers[index];
        uint64_t end;
        if (segment->p_type != PT_LOAD) continue;
        end = (uint64_t)segment->p_vaddr + segment->p_memsz;
        if (end > UINT32_MAX ||
            !range_valid(segment->p_offset, segment->p_filesz,
                         probe->file_size) ||
            segment->p_filesz > segment->p_memsz) {
            probe_log("ERROR: malformed PT_LOAD segment %u", (unsigned)index);
            return 0;
        }
        if ((uint32_t)end > maximum) maximum = (uint32_t)end;
    }
    probe->image_size = align_up(maximum, 0x1000u);
    probe->image_data = (unsigned char *)calloc(1, probe->image_size);
    if (!probe->image_data) return 0;
    for (index = 0; index < probe->header->e_phnum; ++index) {
        const Elf32_Phdr *segment = &probe->program_headers[index];
        if (segment->p_type == PT_LOAD && segment->p_filesz) {
            memcpy(probe->image_data + segment->p_vaddr,
                   probe->file_data + segment->p_offset, segment->p_filesz);
        }
    }
    probe_log("ARM image prepared: guest 0x%08x-0x%08x (%u bytes)",
              GUEST_IMAGE_BASE, GUEST_IMAGE_BASE + probe->image_size,
              probe->image_size);
    return 1;
}

static uint32_t ensure_import(ArmProbe *probe, const char *name) {
    unsigned index;
    for (index = 0; index < probe->import_count; ++index) {
        if (strcmp(probe->imports[index].name, name) == 0)
            return probe->imports[index].address;
    }
    if (probe->import_count >= MAX_IMPORTS) return 0;
    index = probe->import_count++;
    probe->imports[index].name = copy_string(name);
    probe->imports[index].address = GUEST_IMPORT_BASE + index * 4u + 1u;
    return probe->imports[index].address;
}

static void initialize_object(ArmProbe *probe, const char *name,
                              uint32_t address) {
    uint32_t value;
    if (strcmp(name, "__stack_chk_guard") == 0) {
        value = 0xa59c71e3u;
        uc_mem_write(probe->uc, address, &value, sizeof(value));
    } else if (strcmp(name, "optind") == 0) {
        value = 1;
        uc_mem_write(probe->uc, address, &value, sizeof(value));
    }
}

static uint32_t ensure_object(ArmProbe *probe, const char *name) {
    unsigned index;
    for (index = 0; index < probe->object_count; ++index) {
        if (strcmp(probe->objects[index].name, name) == 0)
            return probe->objects[index].address;
    }
    if (probe->object_count >= MAX_OBJECTS) return 0;
    index = probe->object_count++;
    probe->objects[index].name = copy_string(name);
    probe->objects[index].address = GUEST_OBJECT_BASE + index * 0x1000u;
    return probe->objects[index].address;
}

static int apply_relocation_section(ArmProbe *probe,
                                    const Elf32_Shdr *rel_section) {
    const Elf32_Shdr *sym_section = section_at(probe, rel_section->sh_link);
    const Elf32_Shdr *str_section;
    const Elf32_Sym *symbols;
    const char *strings;
    const Elf32_Rel *relocations;
    uint32_t symbol_count;
    uint32_t relocation_count;
    uint32_t index;
    if (!sym_section || sym_section->sh_entsize != sizeof(Elf32_Sym) ||
        !range_valid(sym_section->sh_offset, sym_section->sh_size,
                     probe->file_size)) return 0;
    str_section = section_at(probe, sym_section->sh_link);
    if (!str_section ||
        !range_valid(str_section->sh_offset, str_section->sh_size,
                     probe->file_size) ||
        rel_section->sh_entsize != sizeof(Elf32_Rel) ||
        !range_valid(rel_section->sh_offset, rel_section->sh_size,
                     probe->file_size)) return 0;
    symbols = (const Elf32_Sym *)(probe->file_data + sym_section->sh_offset);
    strings = (const char *)(probe->file_data + str_section->sh_offset);
    relocations =
        (const Elf32_Rel *)(probe->file_data + rel_section->sh_offset);
    symbol_count = sym_section->sh_size / sizeof(Elf32_Sym);
    relocation_count = rel_section->sh_size / sizeof(Elf32_Rel);
    for (index = 0; index < relocation_count; ++index) {
        const Elf32_Rel *relocation = &relocations[index];
        uint32_t symbol_index = ELF32_R_SYM(relocation->r_info);
        uint32_t type = ELF32_R_TYPE(relocation->r_info);
        uint32_t addend;
        uint32_t value = 0;
        unsigned char *where;
        const char *name = "";
        if (relocation->r_offset > probe->image_size - 4u) return 0;
        where = probe->image_data + relocation->r_offset;
        addend = read_u32_host(where);
        if (symbol_index) {
            const Elf32_Sym *symbol;
            if (symbol_index >= symbol_count) return 0;
            symbol = &symbols[symbol_index];
            if (symbol->st_name >= str_section->sh_size) return 0;
            name = strings + symbol->st_name;
            if (symbol->st_shndx != SHN_UNDEF) {
                value = GUEST_IMAGE_BASE + symbol->st_value;
            } else if (ELF32_ST_TYPE(symbol->st_info) == STT_OBJECT) {
                value = ensure_object(probe, name);
            } else {
                value = ensure_import(probe, name);
            }
            if (!value) {
                probe_log("ERROR: cannot allocate relocation target for %s", name);
                return 0;
            }
        }
        switch (type) {
        case R_ARM_NONE:
            break;
        case R_ARM_ABS32:
            write_u32_host(where, value + addend);
            break;
        case R_ARM_GLOB_DAT:
        case R_ARM_JUMP_SLOT:
            write_u32_host(where, value);
            break;
        case R_ARM_RELATIVE:
            write_u32_host(where, GUEST_IMAGE_BASE + addend);
            break;
        default:
            probe_log("ERROR: unsupported ARM relocation %u at 0x%08x (%s)",
                      type, relocation->r_offset, name);
            return 0;
        }
    }
    return 1;
}

static int apply_relocations(ArmProbe *probe) {
    uint16_t index;
    for (index = 0; index < probe->header->e_shnum; ++index) {
        const Elf32_Shdr *section = &probe->section_headers[index];
        if (section->sh_type == SHT_RELA) {
            probe_log("ERROR: ARM image uses unsupported RELA relocations");
            return 0;
        }
        if (section->sh_type == SHT_REL &&
            !apply_relocation_section(probe, section)) {
            probe_log("ERROR: failed relocation section %u", (unsigned)index);
            return 0;
        }
    }
    probe_log("ARM relocations applied: %u function imports, %u objects",
              probe->import_count, probe->object_count);
    return 1;
}

static uint32_t guest_alloc(ArmProbe *probe, uint32_t size) {
    uint32_t address;
    if (!size) size = 1;
    size = align_up(size, 16u);
    if (probe->allocation_count >= MAX_ALLOCS ||
        probe->heap_next > GUEST_HEAP_BASE + GUEST_HEAP_SIZE - size) {
        return 0;
    }
    address = probe->heap_next;
    probe->heap_next += size;
    probe->allocations[probe->allocation_count].address = address;
    probe->allocations[probe->allocation_count].size = size;
    ++probe->allocation_count;
    return address;
}

static uint32_t guest_allocation_size(const ArmProbe *probe, uint32_t address) {
    unsigned index;
    for (index = 0; index < probe->allocation_count; ++index) {
        if (probe->allocations[index].address == address)
            return probe->allocations[index].size;
    }
    return 0;
}

static int guest_read_string(ArmProbe *probe, uint32_t address,
                             char *buffer, size_t capacity) {
    size_t index;
    if (!address || !buffer || capacity < 1) return 0;
    for (index = 0; index + 1 < capacity; ++index) {
        if (uc_mem_read(probe->uc, address + index, buffer + index, 1) !=
            UC_ERR_OK) return 0;
        if (!buffer[index]) return 1;
    }
    buffer[capacity - 1] = 0;
    return 1;
}

static int guest_write_string(ArmProbe *probe, uint32_t address,
                              const char *value, uint32_t capacity) {
    size_t length = strlen(value);
    if (!address || !capacity) return 0;
    if (length >= capacity) length = capacity - 1u;
    if (uc_mem_write(probe->uc, address, value, length) != UC_ERR_OK)
        return 0;
    {
        const char zero = 0;
        return uc_mem_write(probe->uc, address + length, &zero, 1) == UC_ERR_OK;
    }
}

static void set_r0(uc_engine *uc, uint32_t value) {
    uc_reg_write(uc, UC_ARM_REG_R0, &value);
}

static void set_r0_r1_u64(uc_engine *uc, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    uc_reg_write(uc, UC_ARM_REG_R0, &low);
    uc_reg_write(uc, UC_ARM_REG_R1, &high);
}

static uint64_t join_u64(uint32_t low, uint32_t high) {
    return (uint64_t)low | ((uint64_t)high << 32);
}

static float bits_float(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double bits_double(uint32_t low, uint32_t high) {
    uint64_t bits = join_u64(low, high);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int ascii_casecmp(const char *first, const char *second,
                         size_t maximum, int bounded) {
    size_t index = 0;
    for (;;) {
        unsigned char a;
        unsigned char b;
        if (bounded && index >= maximum) return 0;
        a = (unsigned char)tolower((unsigned char)first[index]);
        b = (unsigned char)tolower((unsigned char)second[index]);
        if (a != b || !a || !b) return (int)a - (int)b;
        ++index;
    }
}

static int guest_copy_memory(ArmProbe *probe, uint32_t destination,
                             uint32_t source, uint32_t size) {
    unsigned char *temporary;
    if (!size) return 1;
    if (size > 64u * 1024u * 1024u) return 0;
    temporary = (unsigned char *)malloc(size);
    if (!temporary) return 0;
    if (uc_mem_read(probe->uc, source, temporary, size) != UC_ERR_OK ||
        uc_mem_write(probe->uc, destination, temporary, size) != UC_ERR_OK) {
        free(temporary);
        return 0;
    }
    free(temporary);
    return 1;
}

static void dispatch_import(ArmProbe *probe, ArmImport *import,
                            uint32_t r0, uint32_t r1,
                            uint32_t r2, uint32_t r3) {
    const char *name = import->name;
    char first[MAX_STRING];
    char second[MAX_STRING];
    uint32_t result = 0;
    if (import->calls++ == 0)
        probe_log("  import: %s", name);

    if (strcmp(name, "malloc") == 0) {
        result = guest_alloc(probe, r0);
    } else if (strcmp(name, "calloc") == 0) {
        uint64_t size = (uint64_t)r0 * r1;
        result = size <= UINT32_MAX ? guest_alloc(probe, (uint32_t)size) : 0;
    } else if (strcmp(name, "realloc") == 0) {
        uint32_t old_size = guest_allocation_size(probe, r0);
        result = guest_alloc(probe, r1);
        if (r0 && result)
            guest_copy_memory(probe, result, r0,
                              old_size < r1 ? old_size : r1);
    } else if (strcmp(name, "free") == 0 ||
               strcmp(name, "__cxa_finalize") == 0) {
        result = 0;
    } else if (strcmp(name, "__cxa_atexit") == 0) {
        result = 0;
    } else if (strcmp(name, "__errno") == 0) {
        result = probe->errno_address;
    } else if (strcmp(name, "memcpy") == 0 ||
               strcmp(name, "memmove") == 0) {
        result = guest_copy_memory(probe, r0, r1, r2) ? r0 : 0;
    } else if (strcmp(name, "memset") == 0) {
        unsigned char block[4096];
        uint32_t offset = 0;
        memset(block, (unsigned char)r1, sizeof(block));
        while (offset < r2) {
            uint32_t part = r2 - offset;
            if (part > sizeof(block)) part = sizeof(block);
            if (uc_mem_write(probe->uc, r0 + offset, block, part) != UC_ERR_OK)
                break;
            offset += part;
        }
        result = r0;
    } else if (strcmp(name, "memcmp") == 0) {
        unsigned char *a = (unsigned char *)malloc(r2 ? r2 : 1);
        unsigned char *b = (unsigned char *)malloc(r2 ? r2 : 1);
        if (a && b && uc_mem_read(probe->uc, r0, a, r2) == UC_ERR_OK &&
            uc_mem_read(probe->uc, r1, b, r2) == UC_ERR_OK)
            result = (uint32_t)memcmp(a, b, r2);
        free(a);
        free(b);
    } else if (strcmp(name, "strlen") == 0) {
        if (guest_read_string(probe, r0, first, sizeof(first)))
            result = (uint32_t)strlen(first);
    } else if (strcmp(name, "strcmp") == 0 ||
               strcmp(name, "strcasecmp") == 0 ||
               strcmp(name, "strncmp") == 0 ||
               strcmp(name, "strncasecmp") == 0 ||
               strcmp(name, "strcoll") == 0) {
        if (guest_read_string(probe, r0, first, sizeof(first)) &&
            guest_read_string(probe, r1, second, sizeof(second))) {
            if (strcmp(name, "strcasecmp") == 0)
                result = (uint32_t)ascii_casecmp(first, second, 0, 0);
            else if (strcmp(name, "strncasecmp") == 0)
                result = (uint32_t)ascii_casecmp(first, second, r2, 1);
            else if (strcmp(name, "strncmp") == 0)
                result = (uint32_t)strncmp(first, second, r2);
            else
                result = (uint32_t)strcmp(first, second);
        }
    } else if (strcmp(name, "strcpy") == 0 ||
               strcmp(name, "strncpy") == 0 ||
               strcmp(name, "strcat") == 0) {
        if (guest_read_string(probe, r1, second, sizeof(second))) {
            if (strcmp(name, "strcat") == 0 &&
                guest_read_string(probe, r0, first, sizeof(first))) {
                size_t used = strlen(first);
                size_t remaining = sizeof(first) - used;
                if (remaining > 1) strncat(first, second, remaining - 1);
                guest_write_string(probe, r0, first,
                                   (uint32_t)(strlen(first) + 1));
            } else if (strcmp(name, "strncpy") == 0) {
                size_t length = strlen(second);
                unsigned char *temporary;
                if (length > r2) length = r2;
                temporary = (unsigned char *)calloc(1, r2 ? r2 : 1);
                if (temporary) {
                    memcpy(temporary, second, length);
                    uc_mem_write(probe->uc, r0, temporary, r2);
                    free(temporary);
                }
            } else {
                guest_write_string(probe, r0, second,
                                   (uint32_t)(strlen(second) + 1));
            }
        }
        result = r0;
    } else if (strcmp(name, "strdup") == 0) {
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            result = guest_alloc(probe, (uint32_t)strlen(first) + 1u);
            if (result)
                guest_write_string(probe, result, first,
                                   (uint32_t)strlen(first) + 1u);
        }
    } else if (strcmp(name, "atoi") == 0 || strcmp(name, "strtol") == 0 ||
               strcmp(name, "strtoul") == 0 || strcmp(name, "strtoll") == 0) {
        if (guest_read_string(probe, r0, first, sizeof(first)))
            result = (uint32_t)strtol(first, NULL, strcmp(name, "atoi") == 0 ? 10 : (int)r2);
    } else if (strcmp(name, "strtod") == 0) {
        double value = 0.0;
        if (guest_read_string(probe, r0, first, sizeof(first)))
            value = strtod(first, NULL);
        set_r0_r1_u64(probe->uc, double_bits(value));
        return;
    } else if (strcmp(name, "__android_log_print") == 0) {
        if (!guest_read_string(probe, r1, first, sizeof(first))) strcpy(first, "?");
        if (!guest_read_string(probe, r2, second, sizeof(second))) strcpy(second, "?");
        probe_log("  android log [%s]: %s", first, second);
    } else if (strcmp(name, "printf") == 0 || strcmp(name, "fprintf") == 0 ||
               strcmp(name, "vfprintf") == 0 || strcmp(name, "fputs") == 0 ||
               strcmp(name, "puts") == 0) {
        uint32_t format_address = strcmp(name, "printf") == 0 ? r0 : r1;
        if (guest_read_string(probe, format_address, first, sizeof(first)))
            probe_log("  guest stdio: %s", first);
    } else if (strcmp(name, "snprintf") == 0 ||
               strcmp(name, "vsnprintf") == 0 ||
               strcmp(name, "sprintf") == 0 ||
               strcmp(name, "vsprintf") == 0) {
        uint32_t capacity = (strcmp(name, "sprintf") == 0 ||
                             strcmp(name, "vsprintf") == 0) ? MAX_STRING : r1;
        guest_write_string(probe, r0, "", capacity);
        result = 0;
    } else if (strcmp(name, "getenv") == 0 || strcmp(name, "dlerror") == 0 ||
               strcmp(name, "dlsym") == 0 || strcmp(name, "fopen") == 0 ||
               strcmp(name, "fdopen") == 0 || strcmp(name, "gzopen") == 0) {
        result = 0;
    } else if (strcmp(name, "dlopen") == 0) {
        result = 1;
    } else if (strcmp(name, "pthread_mutex_init") == 0 ||
               strcmp(name, "pthread_mutex_destroy") == 0 ||
               strcmp(name, "pthread_mutex_lock") == 0 ||
               strcmp(name, "pthread_mutex_unlock") == 0 ||
               strcmp(name, "pthread_once") == 0 ||
               strcmp(name, "pthread_setspecific") == 0 ||
               strcmp(name, "pthread_key_delete") == 0 ||
               strcmp(name, "pthread_detach") == 0 ||
               strcmp(name, "sem_init") == 0 ||
               strcmp(name, "sem_destroy") == 0 ||
               strcmp(name, "sem_post") == 0 ||
               strcmp(name, "sem_wait") == 0 ||
               strcmp(name, "setjmp") == 0 ||
               strcmp(name, "sigsetjmp") == 0 ||
               strcmp(name, "sigaction") == 0 || strcmp(name, "raise") == 0) {
        result = 0;
    } else if (strcmp(name, "pthread_key_create") == 0) {
        static uint32_t next_key = 1;
        uc_mem_write(probe->uc, r0, &next_key, sizeof(next_key));
        ++next_key;
        result = 0;
    } else if (strcmp(name, "pthread_getspecific") == 0) {
        result = 0;
    } else if (strcmp(name, "time") == 0 || strcmp(name, "clock") == 0 ||
               strcmp(name, "arc4random") == 0 || strcmp(name, "lrand48") == 0) {
        result = (uint32_t)time(NULL);
        if (r0 && strcmp(name, "time") == 0)
            uc_mem_write(probe->uc, r0, &result, sizeof(result));
    } else if (strcmp(name, "gettimeofday") == 0) {
        uint32_t values[2] = {(uint32_t)time(NULL), 0};
        if (r0) uc_mem_write(probe->uc, r0, values, sizeof(values));
        result = 0;
    } else if (strcmp(name, "clock_gettime") == 0) {
        uint32_t values[2] = {(uint32_t)time(NULL), 0};
        if (r1) uc_mem_write(probe->uc, r1, values, sizeof(values));
        result = 0;
    } else if (strcmp(name, "sinf") == 0 || strcmp(name, "cosf") == 0 ||
               strcmp(name, "tanf") == 0 || strcmp(name, "sqrtf") == 0 ||
               strcmp(name, "floorf") == 0 || strcmp(name, "ceilf") == 0 ||
               strcmp(name, "roundf") == 0 || strcmp(name, "expf") == 0 ||
               strcmp(name, "logf") == 0 || strcmp(name, "acosf") == 0 ||
               strcmp(name, "asinf") == 0) {
        float value = bits_float(r0);
        if (strcmp(name, "sinf") == 0) value = sinf(value);
        else if (strcmp(name, "cosf") == 0) value = cosf(value);
        else if (strcmp(name, "tanf") == 0) value = tanf(value);
        else if (strcmp(name, "sqrtf") == 0) value = sqrtf(value);
        else if (strcmp(name, "floorf") == 0) value = floorf(value);
        else if (strcmp(name, "ceilf") == 0) value = ceilf(value);
        else if (strcmp(name, "roundf") == 0) value = roundf(value);
        else if (strcmp(name, "expf") == 0) value = expf(value);
        else if (strcmp(name, "logf") == 0) value = logf(value);
        else if (strcmp(name, "acosf") == 0) value = acosf(value);
        else value = asinf(value);
        result = float_bits(value);
    } else if (strcmp(name, "atan2f") == 0 || strcmp(name, "powf") == 0 ||
               strcmp(name, "fmodf") == 0) {
        float first_value = bits_float(r0);
        float second_value = bits_float(r1);
        if (strcmp(name, "atan2f") == 0) first_value = atan2f(first_value, second_value);
        else if (strcmp(name, "powf") == 0) first_value = powf(first_value, second_value);
        else first_value = fmodf(first_value, second_value);
        result = float_bits(first_value);
    } else if (strcmp(name, "sin") == 0 || strcmp(name, "cos") == 0 ||
               strcmp(name, "sqrt") == 0 || strcmp(name, "floor") == 0 ||
               strcmp(name, "ceil") == 0 || strcmp(name, "round") == 0 ||
               strcmp(name, "exp") == 0 || strcmp(name, "log") == 0 ||
               strcmp(name, "acos") == 0) {
        double value = bits_double(r0, r1);
        if (strcmp(name, "sin") == 0) value = sin(value);
        else if (strcmp(name, "cos") == 0) value = cos(value);
        else if (strcmp(name, "sqrt") == 0) value = sqrt(value);
        else if (strcmp(name, "floor") == 0) value = floor(value);
        else if (strcmp(name, "ceil") == 0) value = ceil(value);
        else if (strcmp(name, "round") == 0) value = round(value);
        else if (strcmp(name, "exp") == 0) value = exp(value);
        else if (strcmp(name, "log") == 0) value = log(value);
        else value = acos(value);
        set_r0_r1_u64(probe->uc, double_bits(value));
        return;
    } else if (strcmp(name, "atan2") == 0 || strcmp(name, "pow") == 0 ||
               strcmp(name, "fmod") == 0) {
        double first_value = bits_double(r0, r1);
        double second_value = bits_double(r2, r3);
        if (strcmp(name, "atan2") == 0) first_value = atan2(first_value, second_value);
        else if (strcmp(name, "pow") == 0) first_value = pow(first_value, second_value);
        else first_value = fmod(first_value, second_value);
        set_r0_r1_u64(probe->uc, double_bits(first_value));
        return;
    } else if (strncmp(name, "gl", 2) == 0) {
        result = 0;
    } else if (strcmp(name, "abort") == 0 || strcmp(name, "exit") == 0 ||
               strcmp(name, "__stack_chk_fail") == 0 ||
               strcmp(name, "longjmp") == 0 ||
               strcmp(name, "siglongjmp") == 0 ||
               strcmp(name, "pthread_exit") == 0) {
        probe->failed = 1;
        snprintf(probe->failure, sizeof(probe->failure),
                 "guest called fatal import %s", name);
        uc_emu_stop(probe->uc);
        return;
    } else {
        /* The probe intentionally keeps unknown services deterministic.  The
           first-use line above makes every missing semantic visible. */
        result = 0;
    }
    set_r0(probe->uc, result);
}

static void import_hook(uc_engine *uc, uint64_t address, uint32_t size,
                        void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0;
    unsigned index;
    (void)size;
    if (address == GUEST_RETURN_BASE) {
        probe->returned = 1;
        uc_emu_stop(uc);
        return;
    }
    if (address < GUEST_IMPORT_BASE || address >= GUEST_IMPORT_BASE +
        probe->import_count * 4u) return;
    index = (unsigned)((address - GUEST_IMPORT_BASE) / 4u);
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    uc_reg_read(uc, UC_ARM_REG_R2, &r2);
    uc_reg_read(uc, UC_ARM_REG_R3, &r3);
    dispatch_import(probe, &probe->imports[index], r0, r1, r2, r3);
}

static void kuser_hook(uc_engine *uc, uint64_t address, uint32_t size,
                       void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t r0 = 0, r1 = 0, r2 = 0;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    uc_reg_read(uc, UC_ARM_REG_R2, &r2);
    switch ((uint32_t)address) {
    case 0xffff0fa0u: /* __kuser_memory_barrier */
        break;
    case 0xffff0fc0u: { /* __kuser_cmpxchg */
        uint32_t current = 0;
        if (uc_mem_read(uc, r2, &current, sizeof(current)) != UC_ERR_OK) {
            probe->failed = 1;
            snprintf(probe->failure, sizeof(probe->failure),
                     "__kuser_cmpxchg cannot read 0x%08x", r2);
            uc_emu_stop(uc);
            return;
        }
        if (current == r0) {
            uc_mem_write(uc, r2, &r1, sizeof(r1));
            r0 = 0;
        } else {
            r0 = 1;
        }
        set_r0(uc, r0);
        break;
    }
    case 0xffff0fe0u: /* __kuser_get_tls */
        set_r0(uc, GUEST_JNI_BASE + 0x8000u);
        break;
    case 0xffff0f60u: { /* __kuser_cmpxchg64 */
        uint64_t expected = 0, desired = 0, current = 0;
        if (uc_mem_read(uc, r0, &expected, sizeof(expected)) != UC_ERR_OK ||
            uc_mem_read(uc, r1, &desired, sizeof(desired)) != UC_ERR_OK ||
            uc_mem_read(uc, r2, &current, sizeof(current)) != UC_ERR_OK) {
            probe->failed = 1;
            snprintf(probe->failure, sizeof(probe->failure),
                     "__kuser_cmpxchg64 received an invalid pointer");
            uc_emu_stop(uc);
            return;
        }
        if (current == expected) {
            uc_mem_write(uc, r2, &desired, sizeof(desired));
            set_r0(uc, 0);
        } else {
            set_r0(uc, 1);
        }
        break;
    }
    default:
        probe->failed = 1;
        snprintf(probe->failure, sizeof(probe->failure),
                 "unknown Android kuser helper 0x%08llx",
                 (unsigned long long)address);
        uc_emu_stop(uc);
        break;
    }
}

static bool invalid_memory_hook(uc_engine *uc, uc_mem_type type,
                                uint64_t address, int size, int64_t value,
                                void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t pc = 0;
    uint32_t lr = 0;
    (void)value;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    probe->failed = 1;
    snprintf(probe->failure, sizeof(probe->failure),
             "invalid guest memory type=%d address=0x%08llx size=%d pc=0x%08x lr=0x%08x",
             (int)type, (unsigned long long)address, size, pc, lr);
    return false;
}

static int initialize_unicorn(ArmProbe *probe) {
    uc_err error;
    uc_hook import_trace;
    uc_hook return_trace;
    uc_hook invalid_trace;
    uc_hook kuser_trace;
    unsigned char *stubs;
    unsigned index;
    uint32_t object_value;
    error = uc_open(UC_ARCH_ARM, UC_MODE_THUMB, &probe->uc);
    if (error != UC_ERR_OK) {
        probe_log("ERROR: uc_open: %s", uc_strerror(error));
        return 0;
    }
    if (uc_mem_map(probe->uc, GUEST_IMAGE_BASE, probe->image_size,
                   UC_PROT_ALL) != UC_ERR_OK ||
        uc_mem_map(probe->uc, GUEST_RETURN_BASE, 0x1000u, UC_PROT_ALL) !=
            UC_ERR_OK ||
        uc_mem_map(probe->uc, GUEST_IMPORT_BASE, GUEST_IMPORT_SIZE,
                   UC_PROT_ALL) != UC_ERR_OK ||
        uc_mem_map(probe->uc, GUEST_OBJECT_BASE, GUEST_OBJECT_SIZE,
                   UC_PROT_ALL) != UC_ERR_OK ||
        uc_mem_map(probe->uc, GUEST_HEAP_BASE, GUEST_HEAP_SIZE,
                   UC_PROT_ALL) != UC_ERR_OK ||
        uc_mem_map(probe->uc, GUEST_JNI_BASE, GUEST_JNI_SIZE,
                   UC_PROT_ALL) != UC_ERR_OK ||
        uc_mem_map(probe->uc, GUEST_STACK_BASE, GUEST_STACK_SIZE,
                   UC_PROT_ALL) != UC_ERR_OK ||
        uc_mem_map(probe->uc, GUEST_KUSER_BASE, GUEST_KUSER_SIZE,
                   UC_PROT_ALL) != UC_ERR_OK) {
        probe_log("ERROR: cannot map ARM guest address space");
        return 0;
    }
    if (uc_mem_write(probe->uc, GUEST_IMAGE_BASE, probe->image_data,
                     probe->image_size) != UC_ERR_OK) return 0;
    stubs = (unsigned char *)calloc(1, probe->import_count * 4u);
    if (!stubs) return 0;
    for (index = 0; index < probe->import_count; ++index) {
        stubs[index * 4u] = 0x70;     /* bx lr */
        stubs[index * 4u + 1u] = 0x47;
        stubs[index * 4u + 2u] = 0xc0; /* nop */
        stubs[index * 4u + 3u] = 0x46;
    }
    error = uc_mem_write(probe->uc, GUEST_IMPORT_BASE, stubs,
                         probe->import_count * 4u);
    free(stubs);
    if (error != UC_ERR_OK) return 0;
    {
        static const unsigned char bx_lr[] = {0x1e, 0xff, 0x2f, 0xe1};
        static const uint32_t helpers[] = {
            0xffff0f60u, 0xffff0fa0u, 0xffff0fc0u, 0xffff0fe0u
        };
        for (index = 0; index < sizeof(helpers) / sizeof(helpers[0]); ++index)
            uc_mem_write(probe->uc, helpers[index], bx_lr, sizeof(bx_lr));
    }
    for (index = 0; index < probe->object_count; ++index)
        initialize_object(probe, probe->objects[index].name,
                          probe->objects[index].address);
    probe->errno_address = GUEST_OBJECT_BASE + GUEST_OBJECT_SIZE - 0x1000u;
    object_value = 0;
    uc_mem_write(probe->uc, probe->errno_address, &object_value,
                 sizeof(object_value));
    if (uc_hook_add(probe->uc, &import_trace, UC_HOOK_CODE,
                    import_hook, probe, GUEST_IMPORT_BASE,
                    GUEST_IMPORT_BASE + probe->import_count * 4u - 1u) !=
            UC_ERR_OK ||
        uc_hook_add(probe->uc, &return_trace, UC_HOOK_CODE,
                    import_hook, probe, GUEST_RETURN_BASE,
                    GUEST_RETURN_BASE) != UC_ERR_OK ||
        uc_hook_add(probe->uc, &kuser_trace, UC_HOOK_CODE,
                    kuser_hook, probe, 0xffff0f60u,
                    0xffff0fe0u) != UC_ERR_OK ||
        uc_hook_add(probe->uc, &invalid_trace, UC_HOOK_MEM_INVALID,
                    invalid_memory_hook, probe, 1, 0) != UC_ERR_OK) {
        probe_log("ERROR: cannot install ARM guest hooks");
        return 0;
    }
    probe->heap_next = GUEST_HEAP_BASE;
    probe_log("Unicorn ARMv5/Thumb guest initialized");
    return 1;
}

static uint32_t find_export(const ArmProbe *probe, const char *name) {
    uint16_t section_index;
    for (section_index = 0; section_index < probe->header->e_shnum;
         ++section_index) {
        const Elf32_Shdr *symbols_section =
            &probe->section_headers[section_index];
        const Elf32_Shdr *strings_section;
        const Elf32_Sym *symbols;
        const char *strings;
        uint32_t count;
        uint32_t index;
        if (symbols_section->sh_type != SHT_DYNSYM ||
            symbols_section->sh_entsize != sizeof(Elf32_Sym)) continue;
        strings_section = section_at(probe, symbols_section->sh_link);
        if (!strings_section) continue;
        symbols = (const Elf32_Sym *)(probe->file_data +
                                      symbols_section->sh_offset);
        strings = (const char *)(probe->file_data + strings_section->sh_offset);
        count = symbols_section->sh_size / sizeof(Elf32_Sym);
        for (index = 0; index < count; ++index) {
            if (symbols[index].st_shndx != SHN_UNDEF &&
                symbols[index].st_name < strings_section->sh_size &&
                strcmp(strings + symbols[index].st_name, name) == 0)
                return GUEST_IMAGE_BASE + symbols[index].st_value;
        }
    }
    return 0;
}

static int run_guest(ArmProbe *probe, uint32_t entry,
                     const uint32_t *arguments, unsigned argument_count,
                     uint32_t *return_value) {
    uint32_t registers[4] = {0, 0, 0, 0};
    uint32_t stack = GUEST_STACK_TOP - 0x100u;
    uint32_t link = GUEST_RETURN_BASE | 1u;
    uc_err error;
    unsigned index;
    for (index = 0; index < argument_count && index < 4; ++index)
        registers[index] = arguments[index];
    uc_reg_write(probe->uc, UC_ARM_REG_R0, &registers[0]);
    uc_reg_write(probe->uc, UC_ARM_REG_R1, &registers[1]);
    uc_reg_write(probe->uc, UC_ARM_REG_R2, &registers[2]);
    uc_reg_write(probe->uc, UC_ARM_REG_R3, &registers[3]);
    uc_reg_write(probe->uc, UC_ARM_REG_SP, &stack);
    uc_reg_write(probe->uc, UC_ARM_REG_LR, &link);
    probe->returned = 0;
    probe->failed = 0;
    probe->failure[0] = 0;
    probe->current_entry = entry;
    error = uc_emu_start(probe->uc, entry, UINT64_MAX, 0, 20000000u);
    if (return_value)
        uc_reg_read(probe->uc, UC_ARM_REG_R0, return_value);
    if (probe->failed) {
        probe_log("ERROR: %s", probe->failure);
        return 0;
    }
    if (error != UC_ERR_OK) {
        uint32_t pc = 0;
        uint32_t lr = 0;
        uc_reg_read(probe->uc, UC_ARM_REG_PC, &pc);
        uc_reg_read(probe->uc, UC_ARM_REG_LR, &lr);
        probe_log("ERROR: ARM emulation stopped: %s pc=0x%08x lr=0x%08x",
                  uc_strerror(error), pc, lr);
        return 0;
    }
    if (!probe->returned) {
        probe_log("ERROR: ARM call hit instruction limit without returning");
        return 0;
    }
    return 1;
}

static int run_constructors(ArmProbe *probe) {
    uint16_t section_index;
    for (section_index = 0; section_index < probe->header->e_shnum;
         ++section_index) {
        const Elf32_Shdr *section = &probe->section_headers[section_index];
        const char *name;
        uint32_t count;
        uint32_t index;
        if (section->sh_name >=
            probe->section_headers[probe->header->e_shstrndx].sh_size)
            continue;
        name = probe->section_names + section->sh_name;
        if (strcmp(name, ".init_array") != 0) continue;
        count = section->sh_size / sizeof(uint32_t);
        probe_log("Running %u authentic ARM constructors", count);
        for (index = 0; index < count; ++index) {
            uint32_t entry = 0;
            if (uc_mem_read(probe->uc,
                            GUEST_IMAGE_BASE + section->sh_addr + index * 4u,
                            &entry, sizeof(entry)) != UC_ERR_OK)
                return 0;
            if (!entry || entry == UINT32_MAX) continue;
            probe_log("constructor %u/%u: guest 0x%08x (ELF+0x%08x)",
                      index + 1u, count, entry,
                      entry - GUEST_IMAGE_BASE);
            if (!run_guest(probe, entry, NULL, 0, NULL)) {
                probe_log("RESULT: ARM_CONSTRUCTOR_FAILED index=%u", index + 1u);
                return 0;
            }
        }
        probe_log("RESULT: ARM_CONSTRUCTORS_OK count=%u", count);
        return 1;
    }
    probe_log("ERROR: ARM ELF has no .init_array");
    return 0;
}

static void destroy_probe(ArmProbe *probe) {
    unsigned index;
    if (probe->uc) uc_close(probe->uc);
    for (index = 0; index < probe->import_count; ++index)
        free(probe->imports[index].name);
    for (index = 0; index < probe->object_count; ++index)
        free(probe->objects[index].name);
    free(probe->image_data);
    free(probe->file_data);
}

int main(int argc, char **argv) {
    ArmProbe probe;
    const char *path = argc > 1 ? argv[1] : "libgame.so";
    uint32_t jni_on_load;
    uint32_t arguments[2] = {GUEST_JNI_BASE + 0x1000u, 0};
    uint32_t result = 0;
    int exit_code = 1;
    memset(&probe, 0, sizeof(probe));
    probe_log("Geometry Dash ARM native compatibility probe 0.9.4-arm-probe1");
    probe_log("Input: %s", path);
    if (!load_arm_input(path, &probe.file_data, &probe.file_size) ||
        !validate_elf(&probe) || !build_image(&probe) ||
        !apply_relocations(&probe) || !initialize_unicorn(&probe)) {
        probe_log("RESULT: ARM_LOAD_FAILED");
        goto finished;
    }
    probe_log("RESULT: ARM_RELOCATION_OK");
    if (!run_constructors(&probe)) goto finished;
    jni_on_load = find_export(&probe, "JNI_OnLoad");
    if (!jni_on_load) {
        probe_log("ERROR: JNI_OnLoad export is missing");
        goto finished;
    }
    probe_log("Calling authentic ARM JNI_OnLoad at guest 0x%08x", jni_on_load);
    if (!run_guest(&probe, jni_on_load, arguments, 2, &result)) {
        probe_log("RESULT: ARM_JNI_ONLOAD_FAILED");
        goto finished;
    }
    probe_log("ARM JNI_OnLoad returned 0x%08x", result);
    if (result != 0x00010004u) {
        probe_log("RESULT: ARM_JNI_ONLOAD_UNEXPECTED");
        goto finished;
    }
    probe_log("RESULT: ARM_NATIVE_PROBE_OK");
    exit_code = 0;
finished:
    destroy_probe(&probe);
    return exit_code;
}
