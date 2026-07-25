/*
 * Experimental ARM graphical bootstrap for Geometry Dash 1.0-1.4.
 *
 * The Android ARM ELF remains inside a Unicorn ARMv5/Thumb guest. Windows owns
 * the window, OpenGL context, audio, save directory and JNI facade. Every host
 * service is reached through a guest trap; ARM pointers are never cast to host
 * pointers. This grows the successful 0.9.4-arm-probe1 loader into the first
 * complete nativeInit/nativeRender lifecycle host. OverkillTest2 deliberately
 * removes optional subsystems and can hot-patch cosmetic/render functions so the
 * remaining ARM game logic can be measured without audio, particles or real GL.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <GL/gl.h>

#include <ctype.h>
#include <errno.h>
#include <io.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
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
#include "audio_win.h"
#include "storage_win.h"
#include "runtime.h"
#include "../third_party/zlib/zlib.h"

#define EM_ARM 40
#define R_ARM_NONE 0
#define R_ARM_ABS32 2
#define R_ARM_GLOB_DAT 21
#define R_ARM_JUMP_SLOT 22
#define R_ARM_RELATIVE 23

#define GUEST_IMAGE_BASE 0x10000000u
#define GUEST_RETURN_BASE 0x0e000000u
#define GUEST_CALLBACK_RETURN (GUEST_RETURN_BASE + 0x00000004u)
#define GUEST_IMPORT_BASE 0x0f000000u
#define GUEST_IMPORT_SIZE 0x00100000u
#define GUEST_TCG_BUFFER_SIZE (256u * 1024u * 1024u)
#define GUEST_OBJECT_BASE 0x20000000u
#define GUEST_OBJECT_SIZE 0x00400000u
#define GUEST_HEAP_BASE 0x30000000u
#define GUEST_HEAP_SIZE 0x20000000u
#define GUEST_JNI_BASE 0x60000000u
#define GUEST_JNI_SIZE 0x01000000u
#define GUEST_FILE_BASE 0x50000000u
#define GUEST_FILE_SIZE 0x00100000u
#define GUEST_STACK_BASE 0x70000000u
#define GUEST_STACK_SIZE 0x00800000u
#define GUEST_STACK_TOP (GUEST_STACK_BASE + GUEST_STACK_SIZE)
#define GUEST_KUSER_BASE 0xffff0000u
#define GUEST_KUSER_SIZE 0x00010000u

#define MAX_IMPORTS 4096
#define MAX_BLOCK_PROFILE_ENTRIES 65536u
#define MAX_OBJECTS 256
#define MAX_ALLOCS 1048576
#define MAX_STRING 65536
#define MAX_GUEST_CSTRING (64u * 1024u * 1024u)
#define MAX_GUEST_FORMAT (16u * 1024u * 1024u)
#define MAX_GUEST_REFS 4096
#define MAX_GUEST_FILES 256
#define MAX_APK_MEMBER_CACHE 512
#define MAX_APK_INDEX_ENTRIES 65535u
#define MAX_APK_MEMBER_CACHE_BYTES (256u * 1024u * 1024u)
#define MAX_GUEST_ZSTREAMS 64
#define MAX_REGISTERED_NATIVES 512
#define MAX_GL_VERTEX_ATTRIBS 32
#define MAX_DS_SEEN_NODES 4096
#define MAX_COMPAT_PARTICLE_HOLDS 512
#define MAX_QSORT_BYTES (64u * 1024u * 1024u)
#define MAX_CALLBACK_DEPTH 32
#define MAX_OVERKILL_PATCHES 32
#define MAX_PTHREAD_KEYS 64
#define GUEST_FREE_BIN_COUNT 32
#define MAX_GUEST_ZLIB_BUFFER (64u * 1024u * 1024u)
#define MAX_GL_CLIENT_ARRAY_BYTES (64u * 1024u * 1024u)
#define HOST_IO_SCRATCH_BYTES (1024u * 1024u)
#define GUEST_ALLOCATION_FREE 0x80000000u
#define GUEST_ALLOCATION_SIZE_MASK 0x7fffffffu
#define DEFAULT_GUEST_INSTRUCTION_LIMIT 20000000u
#define NATIVE_INIT_INSTRUCTION_LIMIT 250000000u
#define NATIVE_RUNTIME_INSTRUCTION_LIMIT 0u
#define CLAIM_PARTICLE_GUARD_OFFSET 0x001404eeu
#define CLAIM_PARTICLE_REMOVE_SOURCE_OFFSET 0x001404f6u
#define CLAIM_PARTICLE_NULL_RETURN_OFFSET 0x0014050au
#define UNCLAIM_PARTICLE_GUARD_OFFSET 0x001403e0u
#define UNCLAIM_PARTICLE_DEACTIVATE_OFFSET 0x001403eau
#define LABEL_BMFONT_SET_STRING_OFFSET 0x001cc314u
#define LABEL_BMFONT_GUARD_OFFSET 0x001cbcc4u
#define LABEL_BMFONT_SKIP_CHARACTER_OFFSET 0x001cbfdcu
#define ATTEMPT_ONE_LITERAL_OFFSET 0x0032361cu
#define GUEST_QSORT_CHOOSE_CHILD 1
#define GUEST_QSORT_COMPARE_ROOT 2
#define JNI_TABLE_SIZE 233
#define JNI_VERSION_1_4 0x00010004u
#define GUEST_ENV_OBJECT (GUEST_JNI_BASE + 0x1000u)
#define GUEST_ENV_TABLE  (GUEST_JNI_BASE + 0x2000u)
#define GUEST_VM_OBJECT  (GUEST_JNI_BASE + 0x3000u)
#define GUEST_VM_TABLE   (GUEST_JNI_BASE + 0x4000u)
#define GUEST_JNI_TRAPS  (GUEST_JNI_BASE + 0x10000u)
#define GUEST_VM_TRAPS   (GUEST_JNI_BASE + 0x20000u)
#define GUEST_REF_BASE   (GUEST_JNI_BASE + 0x400000u)

typedef enum {
    IMPORT_SLOW = 0,
    IMPORT_MALLOC,
    IMPORT_CALLOC,
    IMPORT_REALLOC,
    IMPORT_FREE,
    IMPORT_MEMCPY,
    IMPORT_MEMMOVE,
    IMPORT_MEMSET,
    IMPORT_MEMCMP,
    IMPORT_STRLEN,
    IMPORT_STRCMP,
    IMPORT_STRNCMP,
    IMPORT_PTHREAD_NOOP,
    IMPORT_LRAND48,
    IMPORT_FREAD,
    IMPORT_SIN,
    IMPORT_COS,
    IMPORT_ACOS,
    IMPORT_ATAN2,
    IMPORT_POW,
    IMPORT_FMOD,
    IMPORT_AEABI_UIDIV,
    IMPORT_AEABI_IDIV,
    IMPORT_AEABI_UIDIVMOD,
    IMPORT_AEABI_IDIVMOD,
    IMPORT_OPENGL
} ArmImportKind;

typedef enum {
    ARM_GL_SPECIAL_NONE = 0,
    ARM_GL_SPECIAL_BIND_BUFFER,
    ARM_GL_SPECIAL_ENABLE_ATTRIB,
    ARM_GL_SPECIAL_DISABLE_ATTRIB,
    ARM_GL_SPECIAL_VERTEX_ATTRIB_POINTER,
    ARM_GL_SPECIAL_DRAW_ARRAYS,
    ARM_GL_SPECIAL_DRAW_ELEMENTS,
    ARM_GL_SPECIAL_BUFFER_DATA,
    ARM_GL_SPECIAL_BUFFER_SUB_DATA,
    ARM_GL_SPECIAL_DELETE_BUFFERS,
    ARM_GL_SPECIAL_UNIFORM_VECTOR
} ArmGlSpecial;

typedef struct {
    char *name;
    uint32_t address;
    unsigned calls;
    ArmImportKind kind;
    PROC host_function;
    unsigned char gl_argument_count;
    unsigned char gl_special;
} ArmImport;

typedef struct {
    uint32_t base;
    uint32_t size;
    unsigned char *host;
    int direct_write;
    int owned;
} GuestMemoryRegion;

typedef struct {
    const char *name;
    uint32_t address;
    unsigned char original[4];
    int return_zero;
    int ready;
    int active;
} GuestNoopPatch;

typedef struct {
    uint32_t address;
    uint32_t size;
    uint64_t calls;
} ArmBlockProfileEntry;

#define MAX_GUEST_MEMORY_REGIONS 9

typedef struct {
    uint32_t address;
    uint32_t size;
} GuestAllocation;

typedef enum {
    GREF_CLASS = 1,
    GREF_METHOD,
    GREF_STRING,
    GREF_BYTE_ARRAY,
    GREF_INT_ARRAY,
    GREF_FLOAT_ARRAY,
    GREF_OBJECT
} GuestRefKind;

typedef struct {
    uint32_t handle;
    GuestRefKind kind;
    char *class_name;
    char *name;
    char *signature;
    uint32_t data_address;
    uint32_t length;
    unsigned calls;
} GuestRef;

typedef struct {
    uint32_t handle;
    FILE *host;
    unsigned char *payload;
    size_t size;
    size_t position;
    size_t bytes_read;
    size_t bytes_written;
    int owns_payload;
    int writable;
    int transactional;
    int eof;
    char *transaction_path;
    char *final_path;
    char diagnostic_name[40];
} GuestFile;

typedef struct {
    char *name;
    unsigned char *payload;
    size_t size;
    uint64_t hits;
} ApkMemberCacheEntry;

typedef struct {
    char *name;
    uint32_t data_offset;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t expected_crc;
    uint16_t method;
    uint16_t flags;
} ApkIndexEntry;

typedef enum {
    GUEST_ZSTREAM_NONE = 0,
    GUEST_ZSTREAM_INFLATE,
    GUEST_ZSTREAM_DEFLATE
} GuestZStreamKind;

typedef struct {
    uint32_t next_in;
    uint32_t avail_in;
    uint32_t total_in;
    uint32_t next_out;
    uint32_t avail_out;
    uint32_t total_out;
    uint32_t msg;
    uint32_t state;
    uint32_t zalloc;
    uint32_t zfree;
    uint32_t opaque;
    int32_t data_type;
    uint32_t adler;
    uint32_t reserved;
} GuestZStreamLayout;

_Static_assert(sizeof(GuestZStreamLayout) == 56,
               "ARM z_stream layout must remain 32-bit");

/* Android/Bionic's 32-bit struct tm includes the BSD timezone extensions. */
typedef struct {
    int32_t tm_sec;
    int32_t tm_min;
    int32_t tm_hour;
    int32_t tm_mday;
    int32_t tm_mon;
    int32_t tm_year;
    int32_t tm_wday;
    int32_t tm_yday;
    int32_t tm_isdst;
    int32_t tm_gmtoff;
    uint32_t tm_zone;
} GuestTmLayout;

_Static_assert(sizeof(GuestTmLayout) == 44,
               "ARM/Bionic struct tm layout must remain 32-bit");

typedef struct {
    int32_t time;
    uint16_t millitm;
    int16_t timezone;
    int16_t dstflag;
    uint16_t padding;
} GuestTimebLayout;

_Static_assert(sizeof(GuestTimebLayout) == 12,
               "ARM/Bionic struct timeb layout must remain 32-bit");

typedef struct {
    uint32_t width;
    uint32_t rowbytes;
    uint8_t color_type;
    uint8_t bit_depth;
    uint8_t channels;
    uint8_t pixel_depth;
} GuestPngRowInfo;

_Static_assert(sizeof(GuestPngRowInfo) == 12,
               "ARM libpng row-info layout must remain 32-bit");

typedef struct {
    uint32_t guest_address;
    z_stream host;
    GuestZStreamKind kind;
    int active;
} GuestZStream;

typedef struct {
    uint32_t size;
    uint32_t type;
    uint32_t normalized;
    uint32_t stride;
    uint32_t guest_pointer;
    int enabled;
    int client_memory;
    int logged;
} GuestGlVertexAttrib;

typedef struct {
    char *class_name;
    char *name;
    char *signature;
    uint32_t function;
} RegisteredNative;

typedef enum {
    GUEST_CALLBACK_NONE = 0,
    GUEST_CALLBACK_QSORT,
    GUEST_CALLBACK_PTHREAD_ONCE
} GuestCallbackKind;

typedef struct {
    GuestCallbackKind kind;
    uint32_t return_address;
    uint32_t once_control;
} GuestCallbackFrame;

typedef struct {
    HWND window;
    HDC device;
    HGLRC context;
    HMODULE opengl;
    uint32_t set_apk_path;
    uint32_t native_init;
    uint32_t render;
    uint32_t touch_begin;
    uint32_t touch_end;
    uint32_t touch_move;
    uint32_t key_down;
    uint32_t insert_text;
    uint32_t delete_backward;
    uint32_t pause;
    uint32_t resume;
    uint32_t touch_ids;
    uint32_t touch_xs;
    uint32_t touch_ys;
    int native_width;
    int native_height;
    float last_touch_x;
    float last_touch_y;
    int native_ready;
    int mouse_down;
    int keyboard_down;
    int native_paused;
    int window_active;
    int vsync_enabled;
    int closing;
} ArmHost;

typedef struct {
    uc_engine *uc;
    GuestMemoryRegion memory_regions[MAX_GUEST_MEMORY_REGIONS];
    unsigned memory_region_count;
    uint64_t direct_memory_reads;
    uint64_t direct_memory_writes;
    int deep_diagnostics;
    int disable_particle_guards;
    int disable_audio;
    int disable_particles;
    int disable_node_draws;
    int skip_draw_calls;
    int skip_scene_visit;
    int skip_native_render;
    int headless_gl;
    int skip_texture_uploads;
    int solid_textures;
    int disable_vsync;
    int uncapped;
    int profile_import_timing;
    int profile_arm_blocks;
    DWORD block_profile_deadline;
    LARGE_INTEGER profile_import_frequency;
    ArmBlockProfileEntry *block_profile_entries;
    uc_hook arm_block_trace;
    uint64_t profile_block_dropped;
    unsigned char *file_data;
    size_t file_size;
    unsigned char *apk_data;
    size_t apk_size;
    ApkIndexEntry *apk_index;
    unsigned apk_index_count;
    unsigned char *image_data;
    int image_data_virtual;
    uint32_t image_size;
    const Elf32_Ehdr *header;
    const Elf32_Phdr *program_headers;
    const Elf32_Shdr *section_headers;
    const char *section_names;
    ArmImport imports[MAX_IMPORTS];
    unsigned import_count;
    ArmImport objects[MAX_OBJECTS];
    unsigned object_count;
    GuestAllocation *allocations;
    unsigned allocation_count;
    uint32_t *free_allocation_next;
    uint32_t free_allocation_heads[GUEST_FREE_BIN_COUNT];
    uint32_t heap_next;
    uint32_t errno_address;
    uint32_t current_entry;
    GuestRef refs[MAX_GUEST_REFS];
    unsigned ref_count;
    GuestFile files[MAX_GUEST_FILES];
    unsigned file_count;
    ApkMemberCacheEntry apk_member_cache[MAX_APK_MEMBER_CACHE];
    unsigned apk_member_cache_count;
    size_t apk_member_cache_bytes;
    uint64_t apk_member_cache_hits;
    uint64_t apk_member_cache_misses;
    uint64_t zip_accel_get_hits;
    uint64_t zip_accel_exist_hits;
    uint64_t zip_accel_misses;
    uint64_t zip_accel_fallbacks;
    GuestZStream zstreams[MAX_GUEST_ZSTREAMS];
    unsigned zstream_count;
    RegisteredNative natives[MAX_REGISTERED_NATIVES];
    unsigned native_count;
    ArmHost host;
    char input_path[MAX_PATH * 2];
    char executable_directory[MAX_PATH * 2];
    char writable_path[MAX_PATH * 2];
    double frame_interval;
    uint64_t lrand48_state;
    uint32_t strtok_next;
    uint32_t tm_storage;
    uint32_t tm_zone_storage;
    uint32_t gl_array_buffer_binding;
    uint32_t gl_element_array_buffer_binding;
    PROC gl_bind_buffer_function;
    PROC gl_vertex_attrib_pointer_function;
    GuestGlVertexAttrib gl_vertex_attribs[MAX_GL_VERTEX_ATTRIBS];
    unsigned char *gl_client_buffers[MAX_GL_VERTEX_ATTRIBS];
    size_t gl_client_buffer_capacities[MAX_GL_VERTEX_ATTRIBS];
    unsigned char *gl_index_buffer;
    size_t gl_index_buffer_capacity;
    unsigned char *io_buffer;
    size_t io_buffer_capacity;
    unsigned gl_draw_logs;
    unsigned apk_cache_open_logs;
    unsigned zlib_stream_logs;
    unsigned native_render_calls;
    int runtime_fast_path_logged;
    unsigned png_filter_rows;
    unsigned allocation_failures;
    unsigned allocation_record_reclaims;
    unsigned free_allocation_count;
    uint64_t ds_step_calls;
    uint64_t ds_step_iterations;
    uint64_t ds_step_cycles;
    unsigned particle_claim_guards;
    unsigned particle_unclaim_guards;
    unsigned attempt_label_repairs;
    unsigned suspicious_label_inputs;
    unsigned unsupported_font_characters;
    unsigned readonly_writes;
    unsigned host_readonly_writes;
    uint32_t immutable_start;
    uint32_t immutable_end;
    int host_write_guard_enabled;
    unsigned qsort_calls;
    uint64_t qsort_comparisons;
    uint64_t qsort_swaps;
    uint32_t qsort_base;
    uint32_t qsort_count;
    uint32_t qsort_size;
    uint32_t qsort_comparator;
    uint32_t qsort_return_address;
    uint32_t qsort_heap_end;
    uint32_t qsort_build_root;
    uint32_t qsort_sift_root;
    uint32_t qsort_sift_child;
    uint32_t qsort_selected_child;
    int qsort_building;
    int qsort_phase;
    int qsort_active;
    GuestCallbackFrame callback_frames[MAX_CALLBACK_DEPTH];
    unsigned callback_depth;
    uint32_t pthread_values[MAX_PTHREAD_KEYS];
    uint32_t pthread_destructors[MAX_PTHREAD_KEYS];
    uint32_t pthread_next_key;
    unsigned pthread_once_calls;
    uint64_t profile_render_microseconds;
    uint64_t profile_render_max_microseconds;
    uint64_t profile_last_render_microseconds;
    uint64_t profile_swap_microseconds;
    uint64_t profile_swap_max_microseconds;
    uint64_t profile_gl_draw_calls;
    uint64_t profile_gl_vertices;
    uint64_t profile_gl_client_copies;
    uint64_t profile_gl_direct_arrays;
    uint64_t profile_gl_client_bytes;
    uint64_t profile_gl_proc_cache_hits;
    uint64_t profile_gl_proc_cache_misses;
    uint64_t profile_gl_redundant_binds;
    uint64_t profile_gl_direct_uploads;
    uint64_t overkill_audio_skips;
    uint64_t overkill_particle_toggles;
    uint64_t overkill_node_draw_toggles;
    uint64_t overkill_scene_toggles;
    uint64_t overkill_native_render_skips;
    uint64_t overkill_skipped_draws;
    uint64_t overkill_headless_gl_calls;
    uint64_t overkill_texture_skips;
    uint32_t overkill_fake_audio_id;
    uint32_t overkill_next_gl_id;
    GuestNoopPatch overkill_particle_patches[MAX_OVERKILL_PATCHES];
    unsigned overkill_particle_patch_count;
    GuestNoopPatch overkill_node_draw_patches[MAX_OVERKILL_PATCHES];
    unsigned overkill_node_draw_patch_count;
    GuestNoopPatch overkill_scene_visit_patch;
    uint64_t profile_alloc_calls;
    uint64_t profile_alloc_bytes;
    uint64_t profile_free_calls;
    uint64_t profile_file_opens;
    uint64_t profile_apk_opens;
    uint64_t profile_file_read_bytes;
    uint64_t profile_zlib_calls;
    uint64_t profile_frames_over_20ms;
    uint64_t profile_frames_over_33ms;
    uint64_t profile_frames_over_50ms;
    unsigned profile_slow_native_logs;
    unsigned profile_import_baseline[MAX_IMPORTS];
    uint64_t profile_import_ticks[MAX_IMPORTS];
    uint64_t profile_direct_read_baseline;
    uint64_t profile_direct_write_baseline;
    uint64_t profile_apk_cache_hit_baseline;
    uint64_t profile_apk_cache_miss_baseline;
    uint32_t level_trace_play_init;
    uint32_t level_trace_play_create;
    uint32_t level_trace_editor_init;
    uint32_t level_trace_editor_create;
    uint32_t level_trace_set_string;
    uint32_t level_trace_encode;
    uint32_t level_trace_save;
    unsigned level_trace_calls;
    uint32_t compatibility_particle_holds[MAX_COMPAT_PARTICLE_HOLDS];
    unsigned compatibility_particle_hold_count;
    uint32_t ds_seen_nodes[MAX_DS_SEEN_NODES];
    unsigned ds_seen_count;
    char ds_step_key[128];
    int text_input_active;
    int returned;
    int failed;
    char failure[256];
} ArmProbe;

static uint32_t find_export(const ArmProbe *probe, const char *name);
static int arm_block_profiler_set(ArmProbe *probe, int enabled,
                                  int timed_sample);

static ArmProbe *g_active_probe;
static FILE *g_log_stream;

static void log_v(const char *format, va_list arguments) {
    va_list copy;
    va_copy(copy, arguments);
    vprintf(format, arguments);
    putchar('\n');
    fflush(stdout);
    if (g_log_stream) {
        vfprintf(g_log_stream, format, copy);
        fputc('\n', g_log_stream);
        fflush(g_log_stream);
    }
    va_end(copy);
}

static void probe_log(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_v(format, arguments);
    va_end(arguments);
}

static GuestMemoryRegion *guest_memory_region(ArmProbe *probe,
                                                uint64_t address,
                                                size_t size) {
    uint64_t end;
    unsigned index;
    if (!probe || !size || address > UINT32_MAX ||
        size - 1u > UINT32_MAX - address)
        return NULL;
    end = address + size;
    for (index = 0; index < probe->memory_region_count; ++index) {
        GuestMemoryRegion *region = &probe->memory_regions[index];
        uint64_t region_end = (uint64_t)region->base + region->size;
        if (address >= region->base && end <= region_end)
            return region;
    }
    return NULL;
}

static int register_guest_memory_region(ArmProbe *probe, uint32_t base,
                                        uint32_t size, unsigned char *host,
                                        int direct_write, int owned) {
    GuestMemoryRegion *region;
    if (!probe || !host || !size ||
        probe->memory_region_count >= MAX_GUEST_MEMORY_REGIONS)
        return 0;
    region = &probe->memory_regions[probe->memory_region_count++];
    region->base = base;
    region->size = size;
    region->host = host;
    region->direct_write = direct_write;
    region->owned = owned;
    return 1;
}

static int map_guest_memory(ArmProbe *probe, uint32_t base, uint32_t size,
                            uint32_t permissions, unsigned char *existing,
                            int direct_write) {
    unsigned char *backing = existing;
    int owned = 0;
    uc_err error;
    if (!backing) {
        backing = (unsigned char *)VirtualAlloc(
            NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        owned = backing != NULL;
    }
    if (backing) {
        error = uc_mem_map_ptr(probe->uc, base, size, permissions, backing);
        if (error == UC_ERR_OK) {
            if (!register_guest_memory_region(probe, base, size, backing,
                                              direct_write, owned)) {
                probe_log("ERROR: direct guest memory region table full");
                return 0;
            }
            return 1;
        }
        if (owned) VirtualFree(backing, 0, MEM_RELEASE);
        probe_log("ARM direct memory map fallback: guest 0x%08x size=%.2f MiB (%s)",
                  base, (double)size / (1024.0 * 1024.0), uc_strerror(error));
    }
    return uc_mem_map(probe->uc, base, size, permissions) == UC_ERR_OK;
}

static uc_err direct_uc_mem_read(uc_engine *uc, uint64_t address,
                                 void *bytes, size_t size) {
    ArmProbe *probe = g_active_probe;
    GuestMemoryRegion *region = probe && probe->uc == uc
                                    ? guest_memory_region(probe, address, size)
                                    : NULL;
    if (region) {
        memcpy(bytes, region->host + (size_t)(address - region->base), size);
        ++probe->direct_memory_reads;
        return UC_ERR_OK;
    }
    return uc_mem_read(uc, address, bytes, size);
}

static uc_err guarded_uc_mem_write(uc_engine *uc, uint64_t address,
                                   const void *bytes, size_t size,
                                   const char *function, int line) {
    ArmProbe *probe = g_active_probe;
    uint64_t end = size && address <= UINT64_MAX - (size - 1u)
                       ? address + size - 1u : UINT64_MAX;
    if (probe && probe->uc == uc && probe->host_write_guard_enabled && size &&
        address <= probe->immutable_end && end >= probe->immutable_start) {
        uint64_t value = 0;
        uint32_t pc = 0, lr = 0;
        size_t captured = size < sizeof(value) ? size : sizeof(value);
        if (bytes && captured) memcpy(&value, bytes, captured);
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        ++probe->host_readonly_writes;
        if (probe->host_readonly_writes <= 64u)
            probe_log("ARM blocked host write into immutable game image: "
                      "count=%u bridge=%s:%d address=0x%08llx size=%llu "
                      "value=0x%016llx pc=0x%08x lr=0x%08x",
                      probe->host_readonly_writes,
                      function ? function : "?", line,
                      (unsigned long long)address,
                      (unsigned long long)size,
                      (unsigned long long)value, pc, lr);
        return UC_ERR_WRITE_PROT;
    }
    if (probe && probe->uc == uc && size) {
        GuestMemoryRegion *region = guest_memory_region(probe, address, size);
        if (region && region->direct_write) {
            memcpy(region->host + (size_t)(address - region->base), bytes, size);
            ++probe->direct_memory_writes;
            return UC_ERR_OK;
        }
    }
    return uc_mem_write(uc, address, bytes, size);
}

#define uc_mem_read(uc, address, bytes, size) \
    direct_uc_mem_read((uc), (address), (bytes), (size))
#define uc_mem_write(uc, address, bytes, size) \
    guarded_uc_mem_write((uc), (address), (bytes), (size), __func__, __LINE__)

static LONG WINAPI log_unhandled_exception(EXCEPTION_POINTERS *exception) {
    if (exception && exception->ExceptionRecord) {
        probe_log("ERROR: unhandled host exception code=0x%08lx address=%p",
                  (unsigned long)exception->ExceptionRecord->ExceptionCode,
                  exception->ExceptionRecord->ExceptionAddress);
    } else {
        probe_log("ERROR: unhandled host exception");
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void runtime_log(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_v(format, arguments);
    va_end(arguments);
}

static char *copy_string(const char *source) {
    size_t size = strlen(source) + 1;
    char *result = (char *)malloc(size);
    if (result) memcpy(result, source, size);
    return result;
}

static int host_buffer_reserve(unsigned char **buffer, size_t *capacity,
                               size_t required) {
    size_t next;
    unsigned char *grown;
    if (!buffer || !capacity) return 0;
    if (required <= *capacity) return 1;
    next = *capacity ? *capacity : 4096u;
    while (next < required) {
        if (next > SIZE_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    grown = (unsigned char *)realloc(*buffer, next);
    if (!grown) return 0;
    *buffer = grown;
    *capacity = next;
    return 1;
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

static int apk_index_compare(const void *left, const void *right) {
    const ApkIndexEntry *first = (const ApkIndexEntry *)left;
    const ApkIndexEntry *second = (const ApkIndexEntry *)right;
    return strcmp(first->name, second->name);
}

static const ApkIndexEntry *apk_index_find(const ArmProbe *probe,
                                            const char *member_name) {
    unsigned low = 0;
    unsigned high;
    if (!probe || !probe->apk_index || !member_name) return NULL;
    high = probe->apk_index_count;
    while (low < high) {
        unsigned middle = low + (high - low) / 2u;
        int comparison = strcmp(member_name, probe->apk_index[middle].name);
        if (!comparison) return &probe->apk_index[middle];
        if (comparison < 0) high = middle;
        else low = middle + 1u;
    }
    return NULL;
}

static int apk_build_index(ArmProbe *probe) {
    size_t eocd;
    size_t search_limit;
    size_t central;
    uint16_t entries;
    uint16_t entry_index;
    ApkIndexEntry *index;
    if (!probe || !probe->apk_data || probe->apk_size < 22u) return 0;
    eocd = probe->apk_size - 22u;
    search_limit = eocd > 0xffffu ? eocd - 0xffffu : 0u;
    for (;;) {
        if (range_valid(eocd, 22u, probe->apk_size) &&
            read_u32_host(probe->apk_data + eocd) == 0x06054b50u)
            break;
        if (eocd == search_limit) return 0;
        --eocd;
    }
    if (read_u16_host(probe->apk_data + eocd + 4u) != 0u ||
        read_u16_host(probe->apk_data + eocd + 6u) != 0u)
        return 0;
    entries = read_u16_host(probe->apk_data + eocd + 10u);
    if (!entries) return 0;
    central = read_u32_host(probe->apk_data + eocd + 16u);
    if (!range_valid(central,
                     read_u32_host(probe->apk_data + eocd + 12u),
                     probe->apk_size))
        return 0;
    index = (ApkIndexEntry *)calloc(entries, sizeof(*index));
    if (!index) return 0;
    for (entry_index = 0; entry_index < entries; ++entry_index) {
        uint16_t name_length;
        uint16_t extra_length;
        uint16_t comment_length;
        uint32_t local_offset;
        size_t entry_size;
        size_t data_offset;
        if (!range_valid(central, 46u, probe->apk_size) ||
            read_u32_host(probe->apk_data + central) != 0x02014b50u)
            goto failed;
        name_length = read_u16_host(probe->apk_data + central + 28u);
        extra_length = read_u16_host(probe->apk_data + central + 30u);
        comment_length = read_u16_host(probe->apk_data + central + 32u);
        entry_size = 46u + name_length + extra_length + comment_length;
        if (!name_length || !range_valid(central, entry_size, probe->apk_size))
            goto failed;
        index[entry_index].name = (char *)malloc((size_t)name_length + 1u);
        if (!index[entry_index].name) goto failed;
        memcpy(index[entry_index].name, probe->apk_data + central + 46u,
               name_length);
        index[entry_index].name[name_length] = 0;
        index[entry_index].flags =
            read_u16_host(probe->apk_data + central + 8u);
        index[entry_index].method =
            read_u16_host(probe->apk_data + central + 10u);
        index[entry_index].expected_crc =
            read_u32_host(probe->apk_data + central + 16u);
        index[entry_index].compressed_size =
            read_u32_host(probe->apk_data + central + 20u);
        index[entry_index].uncompressed_size =
            read_u32_host(probe->apk_data + central + 24u);
        local_offset = read_u32_host(probe->apk_data + central + 42u);
        if (!range_valid(local_offset, 30u, probe->apk_size) ||
            read_u32_host(probe->apk_data + local_offset) != 0x04034b50u)
            goto failed;
        data_offset = (size_t)local_offset + 30u +
                      read_u16_host(probe->apk_data + local_offset + 26u) +
                      read_u16_host(probe->apk_data + local_offset + 28u);
        if (data_offset > UINT32_MAX ||
            !range_valid(data_offset, index[entry_index].compressed_size,
                         probe->apk_size))
            goto failed;
        index[entry_index].data_offset = (uint32_t)data_offset;
        central += entry_size;
    }
    qsort(index, entries, sizeof(*index), apk_index_compare);
    probe->apk_index = index;
    probe->apk_index_count = entries;
    probe_log("ARM APK central-directory index ready: entries=%u", entries);
    return 1;

failed:
    for (entry_index = 0; entry_index < entries; ++entry_index)
        free(index[entry_index].name);
    free(index);
    return 0;
}

static int apk_extract_indexed(ArmProbe *probe, const ApkIndexEntry *entry,
                               unsigned char **output,
                               size_t *output_size) {
    unsigned char *payload;
    int inflate_result;
    if (!probe || !entry || !output || !output_size ||
        (entry->flags & 1u) != 0u ||
        (entry->method != 0u && entry->method != 8u) ||
        !entry->uncompressed_size)
        return 0;
    payload = (unsigned char *)malloc(entry->uncompressed_size);
    if (!payload) return 0;
    if (entry->method == 0u) {
        if (entry->compressed_size != entry->uncompressed_size) {
            free(payload);
            return 0;
        }
        memcpy(payload, probe->apk_data + entry->data_offset,
               entry->uncompressed_size);
    } else {
        z_stream stream;
        memset(&stream, 0, sizeof(stream));
        stream.next_in = (Bytef *)(probe->apk_data + entry->data_offset);
        stream.avail_in = entry->compressed_size;
        stream.next_out = payload;
        stream.avail_out = entry->uncompressed_size;
        inflate_result = inflateInit2(&stream, -MAX_WBITS);
        if (inflate_result == Z_OK) {
            inflate_result = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
        }
        if (inflate_result != Z_STREAM_END ||
            stream.total_out != entry->uncompressed_size) {
            free(payload);
            return 0;
        }
    }
    if ((uint32_t)crc32(crc32(0L, Z_NULL, 0), payload,
                        entry->uncompressed_size) != entry->expected_crc) {
        free(payload);
        return 0;
    }
    *output = payload;
    *output_size = entry->uncompressed_size;
    probe_log("Loaded %s through indexed APK lookup (%u bytes)",
              entry->name, entry->uncompressed_size);
    return 1;
}

static int load_arm_input(const char *path, unsigned char **data,
                          size_t *size, unsigned char **apk_data,
                          size_t *apk_size) {
    static const char *const members[] = {
        "lib/armeabi/libcocos2dcpp.so",
        "lib/armeabi/libgame.so",
        "lib/armeabi-v7a/libcocos2dcpp.so",
        "lib/armeabi-v7a/libgame.so",
    };
    unsigned char *input = NULL;
    size_t input_size = 0;
    size_t index;
    if (apk_data) *apk_data = NULL;
    if (apk_size) *apk_size = 0;
    if (!load_file(path, &input, &input_size)) return 0;
    if (input_size >= 4 && memcmp(input, "\x7f" "ELF", 4) == 0) {
        *data = input;
        *size = input_size;
        return 1;
    }
    for (index = 0; index < sizeof(members) / sizeof(members[0]); ++index) {
        if (apk_extract_one(input, input_size, members[index], data, size)) {
            if (apk_data && apk_size) {
                *apk_data = input;
                *apk_size = input_size;
            } else {
                free(input);
            }
            return 1;
        }
    }
    free(input);
    probe_log("ERROR: APK contains no ARM game library in a supported ABI directory");
    return 0;
}

int apk_extract_member(const char *apk_path, const char *member_name,
                       unsigned char **output, size_t *output_size) {
    unsigned char *apk = NULL;
    size_t apk_size = 0;
    int result;
    if (!apk_path || !member_name || !output || !output_size) return 0;
    *output = NULL;
    *output_size = 0;
    if (g_active_probe && g_active_probe->apk_data &&
        _stricmp(apk_path, g_active_probe->input_path) == 0) {
        return apk_extract_one(g_active_probe->apk_data,
                               g_active_probe->apk_size,
                               member_name, output, output_size);
    }
    if (!load_file(apk_path, &apk, &apk_size)) return 0;
    result = apk_extract_one(apk, apk_size, member_name, output, output_size);
    free(apk);
    return result;
}

static int apk_extract_member_cached(ArmProbe *probe,
                                     const char *member_name,
                                     unsigned char **output,
                                     size_t *output_size,
                                     int *owned) {
    unsigned index;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    if (output) *output = NULL;
    if (output_size) *output_size = 0;
    if (owned) *owned = 0;
    if (!probe || !member_name || !output || !output_size ||
        !probe->apk_data)
        return 0;
    for (index = 0; index < probe->apk_member_cache_count; ++index) {
        ApkMemberCacheEntry *entry = &probe->apk_member_cache[index];
        if (strcmp(entry->name, member_name) == 0) {
            ++entry->hits;
            ++probe->apk_member_cache_hits;
            *output = entry->payload;
            *output_size = entry->size;
            return 1;
        }
    }
    ++probe->apk_member_cache_misses;
    {
        const ApkIndexEntry *indexed = apk_index_find(probe, member_name);
        if (indexed) {
            if (!apk_extract_indexed(probe, indexed, &payload, &payload_size))
                return 0;
        } else if (!apk_extract_one(probe->apk_data, probe->apk_size,
                                    member_name, &payload, &payload_size)) {
            return 0;
        }
    }
    if (probe->apk_member_cache_count < MAX_APK_MEMBER_CACHE &&
        payload_size <= MAX_APK_MEMBER_CACHE_BYTES -
                            probe->apk_member_cache_bytes) {
        ApkMemberCacheEntry *entry =
            &probe->apk_member_cache[probe->apk_member_cache_count++];
        entry->name = copy_string(member_name);
        if (entry->name) {
            entry->payload = payload;
            entry->size = payload_size;
            probe->apk_member_cache_bytes += payload_size;
            *output = payload;
            *output_size = payload_size;
            return 1;
        }
        --probe->apk_member_cache_count;
    }
    *output = payload;
    *output_size = payload_size;
    if (owned) *owned = 1;
    return 1;
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
    probe->image_data = (unsigned char *)VirtualAlloc(
        NULL, probe->image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    probe->image_data_virtual = probe->image_data != NULL;
    if (!probe->image_data)
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

static ArmImportKind classify_import(const char *name) {
    if (!name) return IMPORT_SLOW;
    if (strcmp(name, "malloc") == 0) return IMPORT_MALLOC;
    if (strcmp(name, "calloc") == 0) return IMPORT_CALLOC;
    if (strcmp(name, "realloc") == 0) return IMPORT_REALLOC;
    if (strcmp(name, "free") == 0) return IMPORT_FREE;
    if (strcmp(name, "memcpy") == 0 || strcmp(name, "__aeabi_memcpy") == 0 ||
        strcmp(name, "__aeabi_memcpy4") == 0 ||
        strcmp(name, "__aeabi_memcpy8") == 0)
        return IMPORT_MEMCPY;
    if (strcmp(name, "memmove") == 0 || strcmp(name, "__aeabi_memmove") == 0 ||
        strcmp(name, "__aeabi_memmove4") == 0 ||
        strcmp(name, "__aeabi_memmove8") == 0)
        return IMPORT_MEMMOVE;
    /* ARM EABI memset uses (destination, length, value), unlike libc memset's
       (destination, value, length), so keep __aeabi_memset* on its dedicated
       slow-dispatch branch below. */
    if (strcmp(name, "memset") == 0) return IMPORT_MEMSET;
    if (strcmp(name, "memcmp") == 0) return IMPORT_MEMCMP;
    if (strcmp(name, "strlen") == 0) return IMPORT_STRLEN;
    if (strcmp(name, "strcmp") == 0) return IMPORT_STRCMP;
    if (strcmp(name, "strncmp") == 0) return IMPORT_STRNCMP;
    if (strcmp(name, "pthread_mutex_lock") == 0 ||
        strcmp(name, "pthread_mutex_unlock") == 0 ||
        strcmp(name, "pthread_mutex_init") == 0 ||
        strcmp(name, "pthread_mutex_destroy") == 0 ||
        strcmp(name, "pthread_cond_signal") == 0 ||
        strcmp(name, "pthread_cond_broadcast") == 0)
        return IMPORT_PTHREAD_NOOP;
    if (strcmp(name, "lrand48") == 0) return IMPORT_LRAND48;
    if (strcmp(name, "fread") == 0) return IMPORT_FREAD;
    if (strcmp(name, "sin") == 0) return IMPORT_SIN;
    if (strcmp(name, "cos") == 0) return IMPORT_COS;
    if (strcmp(name, "acos") == 0) return IMPORT_ACOS;
    if (strcmp(name, "atan2") == 0) return IMPORT_ATAN2;
    if (strcmp(name, "pow") == 0) return IMPORT_POW;
    if (strcmp(name, "fmod") == 0) return IMPORT_FMOD;
    if (strcmp(name, "__aeabi_uidiv") == 0) return IMPORT_AEABI_UIDIV;
    if (strcmp(name, "__aeabi_idiv") == 0) return IMPORT_AEABI_IDIV;
    if (strcmp(name, "__aeabi_uidivmod") == 0)
        return IMPORT_AEABI_UIDIVMOD;
    if (strcmp(name, "__aeabi_idivmod") == 0)
        return IMPORT_AEABI_IDIVMOD;
    if (name[0] == 'g' && name[1] == 'l') return IMPORT_OPENGL;
    return IMPORT_SLOW;
}

static ArmGlSpecial classify_gl_special(const char *name) {
    if (!name) return ARM_GL_SPECIAL_NONE;
    if (strcmp(name, "glBindBuffer") == 0)
        return ARM_GL_SPECIAL_BIND_BUFFER;
    if (strcmp(name, "glEnableVertexAttribArray") == 0)
        return ARM_GL_SPECIAL_ENABLE_ATTRIB;
    if (strcmp(name, "glDisableVertexAttribArray") == 0)
        return ARM_GL_SPECIAL_DISABLE_ATTRIB;
    if (strcmp(name, "glVertexAttribPointer") == 0)
        return ARM_GL_SPECIAL_VERTEX_ATTRIB_POINTER;
    if (strcmp(name, "glDrawArrays") == 0)
        return ARM_GL_SPECIAL_DRAW_ARRAYS;
    if (strcmp(name, "glDrawElements") == 0)
        return ARM_GL_SPECIAL_DRAW_ELEMENTS;
    if (strcmp(name, "glBufferData") == 0)
        return ARM_GL_SPECIAL_BUFFER_DATA;
    if (strcmp(name, "glBufferSubData") == 0)
        return ARM_GL_SPECIAL_BUFFER_SUB_DATA;
    if (strcmp(name, "glDeleteBuffers") == 0)
        return ARM_GL_SPECIAL_DELETE_BUFFERS;
    if (strncmp(name, "glUniform", 9) == 0 &&
        (strstr(name, "fv") || strstr(name, "iv")))
        return ARM_GL_SPECIAL_UNIFORM_VECTOR;
    return ARM_GL_SPECIAL_NONE;
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
    probe->imports[index].kind = classify_import(name);
    probe->imports[index].host_function = NULL;
    probe->imports[index].gl_argument_count = 0xffu;
    probe->imports[index].gl_special =
        (unsigned char)classify_gl_special(name);
    return probe->imports[index].address;
}

static int initialize_bionic_ctype_object(ArmProbe *probe, const char *name,
                                          uint32_t address) {
    uint32_t table_address = address + sizeof(uint32_t);
    unsigned index;

    if (uc_mem_write(probe->uc, address, &table_address,
                     sizeof(table_address)) != UC_ERR_OK)
        return 0;

    if (strcmp(name, "_ctype_") == 0) {
        unsigned char table[257] = {0};
        enum {
            BIONIC_CTYPE_UPPER = 0x01,
            BIONIC_CTYPE_LOWER = 0x02,
            BIONIC_CTYPE_NUMBER = 0x04,
            BIONIC_CTYPE_SPACE = 0x08,
            BIONIC_CTYPE_PUNCT = 0x10,
            BIONIC_CTYPE_CONTROL = 0x20,
            BIONIC_CTYPE_HEX = 0x40,
            BIONIC_CTYPE_BLANK = 0x80
        };

        for (index = 0; index < 256u; ++index) {
            unsigned char flags = 0;
            if (index <= 0x1fu || (index >= 0x7fu && index <= 0x9fu))
                flags |= BIONIC_CTYPE_CONTROL;
            if (index >= 0xa0u)
                flags |= BIONIC_CTYPE_PUNCT;
            if (index == ' ')
                flags |= BIONIC_CTYPE_SPACE | BIONIC_CTYPE_BLANK;
            else if (index >= '\t' && index <= '\r')
                flags |= BIONIC_CTYPE_SPACE;
            if (index >= '0' && index <= '9')
                flags |= BIONIC_CTYPE_NUMBER;
            if (index >= 'A' && index <= 'Z')
                flags |= BIONIC_CTYPE_UPPER;
            if (index >= 'a' && index <= 'z')
                flags |= BIONIC_CTYPE_LOWER;
            if ((index >= 'A' && index <= 'F') ||
                (index >= 'a' && index <= 'f'))
                flags |= BIONIC_CTYPE_HEX;
            if ((index >= 0x21u && index <= 0x2fu) ||
                (index >= 0x3au && index <= 0x40u) ||
                (index >= 0x5bu && index <= 0x60u) ||
                (index >= 0x7bu && index <= 0x7eu))
                flags |= BIONIC_CTYPE_PUNCT;
            table[index + 1u] = flags;
        }
        return uc_mem_write(probe->uc, table_address, table,
                            sizeof(table)) == UC_ERR_OK;
    }

    {
        int16_t table[257];
        int make_lower = strcmp(name, "_tolower_tab_") == 0;
        table[0] = -1;
        for (index = 0; index < 256u; ++index) {
            unsigned value = index;
            if (make_lower && index >= 'A' && index <= 'Z')
                value = index + ('a' - 'A');
            else if (!make_lower && index >= 'a' && index <= 'z')
                value = index - ('a' - 'A');
            table[index + 1u] = (int16_t)value;
        }
        return uc_mem_write(probe->uc, table_address, table,
                            sizeof(table)) == UC_ERR_OK;
    }
}

static int initialize_object(ArmProbe *probe, const char *name,
                             uint32_t address) {
    uint32_t value;
    probe_log("ARM imported object: %s at 0x%08x", name, address);
    if (strcmp(name, "__stack_chk_guard") == 0) {
        value = 0xa59c71e3u;
        return uc_mem_write(probe->uc, address, &value,
                            sizeof(value)) == UC_ERR_OK;
    } else if (strcmp(name, "optind") == 0) {
        value = 1;
        return uc_mem_write(probe->uc, address, &value,
                            sizeof(value)) == UC_ERR_OK;
    } else if (strcmp(name, "_ctype_") == 0 ||
               strcmp(name, "_tolower_tab_") == 0 ||
               strcmp(name, "_toupper_tab_") == 0) {
        if (!initialize_bionic_ctype_object(probe, name, address)) return 0;
        probe_log("ARM Bionic character table ready: %s -> 0x%08x",
                  name, address + (uint32_t)sizeof(uint32_t));
    }
    return 1;
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

static unsigned guest_allocation_bin(uint32_t size) {
    unsigned bin = 0;
    while (size > 1u && bin + 1u < GUEST_FREE_BIN_COUNT) {
        size >>= 1u;
        ++bin;
    }
    return bin;
}

static uint32_t guest_take_free_allocation(ArmProbe *probe, uint32_t size,
                                           unsigned first_bin,
                                           unsigned last_bin) {
    unsigned search_bin;
    if (!probe->free_allocation_count || !probe->free_allocation_next ||
        first_bin >= GUEST_FREE_BIN_COUNT)
        return 0;
    if (last_bin >= GUEST_FREE_BIN_COUNT) last_bin = GUEST_FREE_BIN_COUNT - 1u;
    for (search_bin = first_bin; search_bin <= last_bin; ++search_bin) {
        uint32_t *link = &probe->free_allocation_heads[search_bin];
        unsigned visited = 0;
        while (*link && visited++ <= probe->allocation_count) {
            GuestAllocation *allocation;
            uint32_t block_size;
            unsigned index = *link - 1u;
            if (index >= probe->allocation_count) {
                *link = 0;
                break;
            }
            allocation = &probe->allocations[index];
            block_size = allocation->size & GUEST_ALLOCATION_SIZE_MASK;
            if ((allocation->size & GUEST_ALLOCATION_FREE) != 0u &&
                block_size >= size) {
                *link = probe->free_allocation_next[index];
                probe->free_allocation_next[index] = 0;
                allocation->size = block_size;
                --probe->free_allocation_count;
                return allocation->address;
            }
            link = &probe->free_allocation_next[index];
        }
        if (*link) {
            probe_log("ARM allocator repaired a cyclic free bin: bin=%u",
                      search_bin);
            *link = 0;
        }
    }
    return 0;
}

static void guest_rebuild_free_bins(ArmProbe *probe) {
    unsigned index;
    memset(probe->free_allocation_heads, 0,
           sizeof(probe->free_allocation_heads));
    probe->free_allocation_count = 0;
    if (!probe->free_allocation_next) return;
    memset(probe->free_allocation_next, 0,
           (size_t)probe->allocation_count *
               sizeof(*probe->free_allocation_next));
    for (index = 0; index < probe->allocation_count; ++index) {
        uint32_t size = probe->allocations[index].size;
        if ((size & GUEST_ALLOCATION_FREE) != 0u) {
            unsigned bin = guest_allocation_bin(
                size & GUEST_ALLOCATION_SIZE_MASK);
            probe->free_allocation_next[index] =
                probe->free_allocation_heads[bin];
            probe->free_allocation_heads[bin] = index + 1u;
            ++probe->free_allocation_count;
        }
    }
}

/* Allocation records are kept in ascending guest-address order for fast
   free/realloc lookup.  If the metadata table ever fills, discard the
   smallest already-free block, close the record gap, rebuild the bin indices,
   and reuse that record for the next bump allocation.  The discarded block
   was not live guest memory; losing that small hole is preferable to turning
   one level-load failure into a permanent std::bad_alloc state. */
static int guest_reclaim_free_allocation_record(ArmProbe *probe) {
    unsigned index;
    unsigned candidate = UINT_MAX;
    uint32_t candidate_size = UINT32_MAX;
    if (!probe->free_allocation_count) return 0;
    for (index = 0; index < probe->allocation_count; ++index) {
        uint32_t tagged_size = probe->allocations[index].size;
        uint32_t size = tagged_size & GUEST_ALLOCATION_SIZE_MASK;
        if ((tagged_size & GUEST_ALLOCATION_FREE) != 0u &&
            size < candidate_size) {
            candidate = index;
            candidate_size = size;
        }
    }
    if (candidate == UINT_MAX) {
        guest_rebuild_free_bins(probe);
        return 0;
    }
    if (candidate + 1u < probe->allocation_count) {
        memmove(&probe->allocations[candidate],
                &probe->allocations[candidate + 1u],
                (size_t)(probe->allocation_count - candidate - 1u) *
                    sizeof(*probe->allocations));
    }
    --probe->allocation_count;
    guest_rebuild_free_bins(probe);
    ++probe->allocation_record_reclaims;
    if (probe->allocation_record_reclaims <= 8u) {
        probe_log("ARM allocator recycled free metadata record: "
                  "discarded=%u records=%u/%u free=%u",
                  candidate_size, probe->allocation_count, MAX_ALLOCS,
                  probe->free_allocation_count);
    }
    return 1;
}

static void guest_allocation_free_statistics(const ArmProbe *probe,
                                             uint64_t *total,
                                             uint32_t *largest) {
    unsigned index;
    uint64_t free_total = 0;
    uint32_t free_largest = 0;
    for (index = 0; index < probe->allocation_count; ++index) {
        uint32_t tagged_size = probe->allocations[index].size;
        if ((tagged_size & GUEST_ALLOCATION_FREE) != 0u) {
            uint32_t size = tagged_size & GUEST_ALLOCATION_SIZE_MASK;
            free_total += size;
            if (size > free_largest) free_largest = size;
        }
    }
    if (total) *total = free_total;
    if (largest) *largest = free_largest;
}

static uint32_t guest_alloc(ArmProbe *probe, uint32_t size) {
    uint32_t address;
    unsigned bin;
    int heap_has_room;
    if (!size) size = 1;
    if (size > UINT32_MAX - 15u) return 0;
    size = align_up(size, 16u);
    ++probe->profile_alloc_calls;
    probe->profile_alloc_bytes += size;
    bin = guest_allocation_bin(size);

    /* Prefer only the matching size class while unused heap remains.  The old
       policy gave 100 KiB image buffers to tiny C++ allocations, which rapidly
       fragmented the 256 MiB arena during level changes. */
    address = guest_take_free_allocation(probe, size, bin, bin);
    if (address) return address;

    heap_has_room = size <= GUEST_HEAP_SIZE &&
        probe->heap_next <= GUEST_HEAP_BASE + GUEST_HEAP_SIZE - size;
    if (heap_has_room && probe->allocation_count < MAX_ALLOCS) {
        address = probe->heap_next;
        probe->heap_next += size;
        probe->allocations[probe->allocation_count].address = address;
        probe->allocations[probe->allocation_count].size = size;
        probe->free_allocation_next[probe->allocation_count] = 0;
        ++probe->allocation_count;
        return address;
    }

    /* When bumping is impossible, accepting a larger free block is still much
       better than failing. */
    if (bin + 1u < GUEST_FREE_BIN_COUNT) {
        address = guest_take_free_allocation(
            probe, size, bin + 1u, GUEST_FREE_BIN_COUNT - 1u);
        if (address) return address;
    }

    if (heap_has_room && probe->allocation_count >= MAX_ALLOCS &&
        guest_reclaim_free_allocation_record(probe)) {
        address = probe->heap_next;
        probe->heap_next += size;
        probe->allocations[probe->allocation_count].address = address;
        probe->allocations[probe->allocation_count].size = size;
        probe->free_allocation_next[probe->allocation_count] = 0;
        ++probe->allocation_count;
        return address;
    }

    ++probe->allocation_failures;
    if (probe->allocation_failures <= 8u) {
        uint64_t free_bytes = 0;
        uint32_t largest_free = 0;
        guest_allocation_free_statistics(probe, &free_bytes, &largest_free);
        probe_log("ARM guest allocation failed: size=%u heap_used=%u/%u "
                  "records=%u/%u free_records=%u free_bytes=%llu "
                  "largest_free=%u",
                  size, probe->heap_next - GUEST_HEAP_BASE, GUEST_HEAP_SIZE,
                  probe->allocation_count, MAX_ALLOCS,
                  probe->free_allocation_count,
                  (unsigned long long)free_bytes, largest_free);
    }
    return 0;
}

static int guest_allocation_index(const ArmProbe *probe, uint32_t address) {
    unsigned low = 0, high = probe->allocation_count;
    while (low < high) {
        unsigned middle = low + (high - low) / 2u;
        uint32_t candidate = probe->allocations[middle].address;
        if (candidate < address) low = middle + 1u;
        else high = middle;
    }
    return low < probe->allocation_count &&
           probe->allocations[low].address == address ? (int)low : -1;
}

static uint32_t guest_allocation_size(const ArmProbe *probe, uint32_t address) {
    int index = guest_allocation_index(probe, address);
    if (index >= 0 &&
        (probe->allocations[index].size & GUEST_ALLOCATION_FREE) == 0u)
        return probe->allocations[index].size;
    return 0;
}

static void guest_free(ArmProbe *probe, uint32_t address) {
    int index;
    uint32_t size;
    unsigned bin = 0;
    if (!address) return;
    index = guest_allocation_index(probe, address);
    if (index < 0 ||
        (probe->allocations[index].size & GUEST_ALLOCATION_FREE) != 0u)
        return;
    ++probe->profile_free_calls;
    size = probe->allocations[index].size & GUEST_ALLOCATION_SIZE_MASK;
    probe->allocations[index].size = size | GUEST_ALLOCATION_FREE;
    while (size > 1u && bin + 1u < GUEST_FREE_BIN_COUNT) {
        size >>= 1u;
        ++bin;
    }
    if (probe->free_allocation_next) {
        probe->free_allocation_next[index] =
            probe->free_allocation_heads[bin];
        probe->free_allocation_heads[bin] = (uint32_t)index + 1u;
    }
    ++probe->free_allocation_count;
}

static int guest_zero_memory(ArmProbe *probe, uint32_t address,
                             uint32_t size) {
    static const unsigned char zero[4096] = {0};
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t part = size - offset;
        if (part > sizeof(zero)) part = sizeof(zero);
        if (uc_mem_write(probe->uc, address + offset, zero, part) != UC_ERR_OK)
            return 0;
        offset += part;
    }
    return 1;
}

static int guest_read_string(ArmProbe *probe, uint32_t address,
                             char *buffer, size_t capacity) {
    size_t offset = 0;
    if (!probe || !address || !buffer || capacity < 1u) return 0;
    while (offset + 1u < capacity) {
        uint64_t current64 = (uint64_t)address + offset;
        uint32_t current;
        size_t wanted = capacity - 1u - offset;
        GuestMemoryRegion *region;
        const unsigned char *source;
        const unsigned char *zero;
        if (current64 > UINT32_MAX) return 0;
        current = (uint32_t)current64;
        region = guest_memory_region(probe, current, 1u);
        if (region) {
            size_t available = (size_t)region->size -
                               (size_t)(current - region->base);
            size_t part = wanted < available ? wanted : available;
            source = region->host + (size_t)(current - region->base);
            zero = (const unsigned char *)memchr(source, 0, part);
            if (zero) part = (size_t)(zero - source) + 1u;
            memcpy(buffer + offset, source, part);
            ++probe->direct_memory_reads;
            offset += part;
            if (zero) return 1;
        } else {
            unsigned char byte = 0;
            if (uc_mem_read(probe->uc, current, &byte, 1u) != UC_ERR_OK)
                return 0;
            buffer[offset++] = (char)byte;
            if (!byte) return 1;
        }
    }
    buffer[capacity - 1u] = 0;
    return 1;
}

static int guest_cstring_nlength(ArmProbe *probe, uint32_t address,
                                 uint32_t limit, uint32_t *length,
                                 int *terminated) {
    unsigned char block[4096];
    uint32_t offset = 0;
    if (length) *length = 0;
    if (terminated) *terminated = 0;
    if (!probe || !address) return 0;
    while (offset < limit) {
        uint32_t part = limit - offset;
        GuestMemoryRegion *region;
        const unsigned char *bytes;
        unsigned char *zero;
        if (part > sizeof(block)) part = sizeof(block);
        if (offset > UINT32_MAX - address) return 0;
        region = guest_memory_region(probe, address + offset, part);
        if (region) {
            bytes = region->host +
                    (size_t)(address + offset - region->base);
            ++probe->direct_memory_reads;
        } else {
            if (uc_mem_read(probe->uc, address + offset, block, part) !=
                UC_ERR_OK)
                return 0;
            bytes = block;
        }
        zero = (unsigned char *)memchr(bytes, 0, part);
        if (zero) {
            if (length) *length = offset + (uint32_t)(zero - bytes);
            if (terminated) *terminated = 1;
            return 1;
        }
        offset += part;
    }
    if (length) *length = limit;
    return 1;
}

static int guest_cstring_length(ArmProbe *probe, uint32_t address,
                                uint32_t *length) {
    int terminated = 0;
    return guest_cstring_nlength(probe, address, MAX_GUEST_CSTRING,
                                 length, &terminated) && terminated;
}

static char *guest_duplicate_cstring(ArmProbe *probe, uint32_t address,
                                     uint32_t maximum,
                                     uint32_t *length_out) {
    uint32_t length = 0;
    int terminated = 0;
    char *copy;
    if (!guest_cstring_nlength(probe, address, maximum, &length,
                              &terminated) || !terminated ||
        (size_t)length + 1u < length)
        return NULL;
    copy = (char *)malloc((size_t)length + 1u);
    if (!copy || uc_mem_read(probe->uc, address, copy,
                             (size_t)length + 1u) != UC_ERR_OK) {
        free(copy);
        return NULL;
    }
    if (length_out) *length_out = length;
    return copy;
}

static int guest_character_set(ArmProbe *probe, uint32_t address,
                               unsigned char table[256]) {
    unsigned char block[256];
    uint32_t offset = 0;
    memset(table, 0, 256);
    if (!address) return 0;
    while (offset < 4096u) {
        uint32_t index;
        if (offset > UINT32_MAX - address ||
            uc_mem_read(probe->uc, address + offset, block,
                        sizeof(block)) != UC_ERR_OK)
            return 0;
        for (index = 0; index < sizeof(block); ++index) {
            if (!block[index]) return 1;
            table[block[index]] = 1;
        }
        offset += sizeof(block);
    }
    return 0;
}

static int guest_span_set(ArmProbe *probe, uint32_t address,
                          const unsigned char table[256], int members,
                          uint32_t maximum, uint32_t *span,
                          unsigned char *stop_character) {
    unsigned char block[4096];
    uint32_t offset = 0;
    if (span) *span = 0;
    if (stop_character) *stop_character = 0;
    if (!address) return 0;
    while (offset < maximum) {
        uint32_t part = maximum - offset;
        uint32_t index;
        if (part > sizeof(block)) part = sizeof(block);
        if (offset > UINT32_MAX - address ||
            uc_mem_read(probe->uc, address + offset, block, part) != UC_ERR_OK)
            return 0;
        for (index = 0; index < part; ++index) {
            unsigned char value = block[index];
            if (!value || ((table[value] != 0) != (members != 0))) {
                if (span) *span = offset + index;
                if (stop_character) *stop_character = value;
                return 1;
            }
        }
        offset += part;
    }
    if (span) *span = maximum;
    return 1;
}

static int guest_compare_cstrings(ArmProbe *probe, uint32_t first_address,
                                  uint32_t second_address, uint32_t limit,
                                  int ignore_case, int *comparison) {
    unsigned char first[4096];
    unsigned char second[4096];
    uint32_t offset = 0;
    if (comparison) *comparison = 0;
    if (!first_address || !second_address) return 0;
    while (offset < limit) {
        uint32_t part = limit - offset;
        uint32_t index;
        const unsigned char *first_bytes = first;
        const unsigned char *second_bytes = second;
        GuestMemoryRegion *first_region;
        GuestMemoryRegion *second_region;
        if (part > sizeof(first)) part = sizeof(first);
        if (offset > UINT32_MAX - first_address ||
            offset > UINT32_MAX - second_address)
            return 0;
        first_region = guest_memory_region(probe, first_address + offset, part);
        second_region = guest_memory_region(probe, second_address + offset, part);
        if (first_region)
            first_bytes = first_region->host +
                          (size_t)(first_address + offset - first_region->base);
        else if (uc_mem_read(probe->uc, first_address + offset, first, part) !=
                     UC_ERR_OK)
            return 0;
        if (second_region)
            second_bytes = second_region->host +
                           (size_t)(second_address + offset - second_region->base);
        else if (uc_mem_read(probe->uc, second_address + offset, second, part) !=
                     UC_ERR_OK)
            return 0;
        probe->direct_memory_reads += (first_region != NULL) +
                                      (second_region != NULL);
        for (index = 0; index < part; ++index) {
            unsigned first_value = first_bytes[index];
            unsigned second_value = second_bytes[index];
            if (ignore_case) {
                first_value = (unsigned)tolower((int)first_value);
                second_value = (unsigned)tolower((int)second_value);
            }
            if (first_value != second_value) {
                if (comparison)
                    *comparison = first_value < second_value ? -1 : 1;
                return 1;
            }
            if (!first_bytes[index]) return 1;
        }
        offset += part;
    }
    return 1;
}

static uint32_t guest_find_character(ArmProbe *probe, uint32_t address,
                                     unsigned char character, int reverse) {
    unsigned char block[4096];
    uint32_t offset = 0;
    uint32_t found = 0;
    while (offset < MAX_GUEST_CSTRING) {
        uint32_t part = MAX_GUEST_CSTRING - offset;
        uint32_t index;
        if (part > sizeof(block)) part = sizeof(block);
        if (offset > UINT32_MAX - address ||
            uc_mem_read(probe->uc, address + offset, block, part) != UC_ERR_OK)
            return 0;
        for (index = 0; index < part; ++index) {
            if (block[index] == character) {
                found = address + offset + index;
                if (!reverse) return found;
            }
            if (!block[index]) return found;
        }
        offset += part;
    }
    return found;
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

static uint32_t guest_tm_zone(ArmProbe *probe, const char *zone) {
    if (!probe->tm_zone_storage)
        probe->tm_zone_storage = guest_alloc(probe, 16u);
    if (!probe->tm_zone_storage ||
        !guest_write_string(probe, probe->tm_zone_storage, zone, 16u))
        return 0;
    return probe->tm_zone_storage;
}

static int guest_tm_write(ArmProbe *probe, uint32_t destination,
                          const struct tm *source, const char *zone) {
    GuestTmLayout value;
    uint32_t zone_address;
    if (!destination || !source) return 0;
    zone_address = guest_tm_zone(probe, zone);
    if (!zone_address) return 0;
    value.tm_sec = source->tm_sec;
    value.tm_min = source->tm_min;
    value.tm_hour = source->tm_hour;
    value.tm_mday = source->tm_mday;
    value.tm_mon = source->tm_mon;
    value.tm_year = source->tm_year;
    value.tm_wday = source->tm_wday;
    value.tm_yday = source->tm_yday;
    value.tm_isdst = source->tm_isdst;
    value.tm_gmtoff = 0;
    value.tm_zone = zone_address;
    return uc_mem_write(probe->uc, destination, &value, sizeof(value)) ==
           UC_ERR_OK;
}

static int guest_tm_read(ArmProbe *probe, uint32_t source,
                         struct tm *destination) {
    GuestTmLayout value;
    if (!source || !destination ||
        uc_mem_read(probe->uc, source, &value, sizeof(value)) != UC_ERR_OK)
        return 0;
    memset(destination, 0, sizeof(*destination));
    destination->tm_sec = value.tm_sec;
    destination->tm_min = value.tm_min;
    destination->tm_hour = value.tm_hour;
    destination->tm_mday = value.tm_mday;
    destination->tm_mon = value.tm_mon;
    destination->tm_year = value.tm_year;
    destination->tm_wday = value.tm_wday;
    destination->tm_yday = value.tm_yday;
    destination->tm_isdst = value.tm_isdst;
    return 1;
}

static uint32_t guest_time_to_tm(ArmProbe *probe, uint32_t time_address,
                                 uint32_t destination, int local) {
    int32_t guest_seconds;
    time_t host_seconds;
    struct tm *host_value;
    struct tm copied_value;
    if (!time_address ||
        uc_mem_read(probe->uc, time_address, &guest_seconds,
                    sizeof(guest_seconds)) != UC_ERR_OK)
        return 0;
    host_seconds = (time_t)guest_seconds;
    host_value = local ? localtime(&host_seconds) : gmtime(&host_seconds);
    if (!host_value) return 0;
    copied_value = *host_value;
    if (!destination) {
        if (!probe->tm_storage)
            probe->tm_storage = guest_alloc(probe, sizeof(GuestTmLayout));
        destination = probe->tm_storage;
    }
    if (!guest_tm_write(probe, destination, &copied_value,
                        local ? "local" : "UTC"))
        return 0;
    return destination;
}

static uint64_t host_system_unix_100ns(void) {
    FILETIME file_time;
    uint64_t ticks;
    const uint64_t epoch = UINT64_C(116444736000000000);
    GetSystemTimeAsFileTime(&file_time);
    ticks = (uint64_t)file_time.dwLowDateTime |
            ((uint64_t)file_time.dwHighDateTime << 32u);
    return ticks >= epoch ? ticks - epoch : 0;
}

static LARGE_INTEGER g_time_frequency;
static LARGE_INTEGER g_time_origin;
static uint64_t g_time_origin_unix_microseconds;
static int g_time_ready;
static int g_time_uses_qpc;

static void host_time_initialize(void) {
    if (g_time_ready) return;
    if (QueryPerformanceFrequency(&g_time_frequency) &&
        QueryPerformanceCounter(&g_time_origin) &&
        g_time_frequency.QuadPart > 0) {
        g_time_uses_qpc = 1;
    } else {
        g_time_frequency.QuadPart = 1000;
        g_time_origin.QuadPart = GetTickCount();
    }
    g_time_origin_unix_microseconds =
        host_system_unix_100ns() / UINT64_C(10);
    g_time_ready = 1;
    probe_log("ARM high-resolution timer ready: %lld Hz",
              (long long)g_time_frequency.QuadPart);
}

static uint64_t host_monotonic_units(uint64_t units_per_second) {
    LARGE_INTEGER now;
    uint64_t elapsed, frequency;
    host_time_initialize();
    if (!g_time_uses_qpc || !QueryPerformanceCounter(&now))
        now.QuadPart = GetTickCount();
    elapsed = now.QuadPart >= g_time_origin.QuadPart
                  ? (uint64_t)(now.QuadPart - g_time_origin.QuadPart) : 0;
    frequency = (uint64_t)g_time_frequency.QuadPart;
    return (elapsed / frequency) * units_per_second +
           ((elapsed % frequency) * units_per_second) / frequency;
}

static uint64_t host_unix_microseconds(void) {
    host_time_initialize();
    return g_time_origin_unix_microseconds +
           host_monotonic_units(UINT64_C(1000000));
}

static uint64_t host_unix_milliseconds(void) {
    return host_unix_microseconds() / UINT64_C(1000);
}

static int guest_ftime(ArmProbe *probe, uint32_t destination) {
    GuestTimebLayout value;
    TIME_ZONE_INFORMATION zone;
    DWORD zone_state;
    LONG bias;
    uint64_t milliseconds;
    if (!destination) return -1;
    milliseconds = host_unix_milliseconds();
    memset(&zone, 0, sizeof(zone));
    zone_state = GetTimeZoneInformation(&zone);
    bias = zone_state == TIME_ZONE_ID_INVALID ? 0 : zone.Bias;
    if (zone_state == TIME_ZONE_ID_DAYLIGHT)
        bias += zone.DaylightBias;
    else if (zone_state == TIME_ZONE_ID_STANDARD)
        bias += zone.StandardBias;
    if (bias < INT16_MIN) bias = INT16_MIN;
    if (bias > INT16_MAX) bias = INT16_MAX;
    memset(&value, 0, sizeof(value));
    value.time = (int32_t)(milliseconds / 1000u);
    value.millitm = (uint16_t)(milliseconds % 1000u);
    value.timezone = (int16_t)bias;
    value.dstflag = zone_state == TIME_ZONE_ID_DAYLIGHT;
    return uc_mem_write(probe->uc, destination, &value, sizeof(value)) ==
                   UC_ERR_OK
               ? 0
               : -1;
}

static void set_r0(uc_engine *uc, uint32_t value) {
    uc_reg_write(uc, UC_ARM_REG_R0, &value);
}

static void set_r0_r1_u64(uc_engine *uc, uint64_t value) {
    static const int registers[2] = {UC_ARM_REG_R0, UC_ARM_REG_R1};
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    void *values[2] = {&low, &high};
    uc_reg_write_batch(uc, registers, values, 2);
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

static int guest_copy_memory(ArmProbe *probe, uint32_t destination,
                             uint32_t source, uint32_t size);

typedef struct {
    ArmProbe *probe;
    uint32_t registers[4];
    uint32_t sp;
    uint32_t word_position;
    uint32_t direct_address;
    int jvalue_array;
} GuestArgCursor;

static GuestRef *guest_ref(ArmProbe *probe, uint32_t handle) {
    uint32_t offset;
    unsigned index;
    if (handle < GUEST_REF_BASE) return NULL;
    offset = handle - GUEST_REF_BASE;
    if (offset % 0x20u) return NULL;
    index = offset / 0x20u;
    if (index >= probe->ref_count || probe->refs[index].handle != handle)
        return NULL;
    return &probe->refs[index];
}

static GuestRef *guest_new_ref(ArmProbe *probe, GuestRefKind kind) {
    GuestRef *reference;
    if (probe->ref_count >= MAX_GUEST_REFS) return NULL;
    reference = &probe->refs[probe->ref_count];
    memset(reference, 0, sizeof(*reference));
    reference->handle = GUEST_REF_BASE + probe->ref_count * 0x20u;
    reference->kind = kind;
    ++probe->ref_count;
    return reference;
}

static uint32_t guest_new_string_ref(ArmProbe *probe, const char *value) {
    GuestRef *reference = guest_new_ref(probe, GREF_STRING);
    size_t length;
    if (!reference) return 0;
    if (!value) value = "";
    length = strlen(value);
    reference->data_address = guest_alloc(probe, (uint32_t)length + 1u);
    reference->length = (uint32_t)length;
    if (!reference->data_address ||
        !guest_write_string(probe, reference->data_address, value,
                            (uint32_t)length + 1u)) {
        return 0;
    }
    return reference->handle;
}

static uint32_t guest_new_array_ref(ArmProbe *probe, GuestRefKind kind,
                                    uint32_t length, uint32_t element_size) {
    GuestRef *reference = guest_new_ref(probe, kind);
    uint64_t bytes = (uint64_t)length * element_size;
    if (!reference || bytes > UINT32_MAX) return 0;
    reference->length = length;
    if (bytes) {
        reference->data_address = guest_alloc(probe, (uint32_t)bytes);
        if (!reference->data_address) return 0;
        {
            unsigned char zero[256] = {0};
            uint32_t offset = 0;
            while (offset < (uint32_t)bytes) {
                uint32_t part = (uint32_t)bytes - offset;
                if (part > sizeof(zero)) part = sizeof(zero);
                if (uc_mem_write(probe->uc, reference->data_address + offset,
                                 zero, part) != UC_ERR_OK) return 0;
                offset += part;
            }
        }
    }
    return reference->handle;
}

static const char *guest_ref_string(ArmProbe *probe, uint32_t handle,
                                    char *buffer, size_t capacity) {
    GuestRef *reference = guest_ref(probe, handle);
    if (!reference || reference->kind != GREF_STRING ||
        !guest_read_string(probe, reference->data_address, buffer, capacity)) {
        if (capacity) buffer[0] = 0;
    }
    return buffer;
}

static GuestFile *guest_file(ArmProbe *probe, uint32_t handle) {
    uint32_t offset;
    unsigned index;
    if (handle < GUEST_FILE_BASE) return NULL;
    offset = handle - GUEST_FILE_BASE;
    if (offset % 0x100u) return NULL;
    index = offset / 0x100u;
    if (index >= probe->file_count || probe->files[index].handle != handle)
        return NULL;
    return &probe->files[index];
}

static void path_slashes(char *path) {
    size_t index;
    for (index = 0; path && path[index]; ++index) {
        if (path[index] == '/') path[index] = '\\';
    }
}

static int path_has_prefix(const char *value, const char *prefix) {
    return value && prefix && _strnicmp(value, prefix, strlen(prefix)) == 0;
}

static void translate_guest_path(ArmProbe *probe, const char *path,
                                 char *destination, size_t capacity) {
    const char *tail = path ? path : "";
    if (path_has_prefix(tail, "/save/")) tail += 6;
    else if (path_has_prefix(tail, "save/")) tail += 5;
    else if (storage_is_game_file_name(strrchr(tail, '/') ? strrchr(tail, '/') + 1 : tail)) {
        const char *slash = strrchr(tail, '/');
        const char *backslash = strrchr(tail, '\\');
        if (slash && (!backslash || slash > backslash)) tail = slash + 1;
        else if (backslash) tail = backslash + 1;
    } else {
        snprintf(destination, capacity, "%s", tail);
        path_slashes(destination);
        return;
    }
    snprintf(destination, capacity, "%s\\save\\%s",
             probe->executable_directory[0] ? probe->executable_directory : ".",
             tail);
    path_slashes(destination);
}

static int save_file_is_incomplete(FILE *stream, long length,
                                   const char **reason) {
    char prefix[4097];
    char suffix[513];
    size_t prefix_size;
    size_t suffix_size = 0;
    char *start;
    int xml;
    if (reason) *reason = "incomplete";
    if (!stream || length < 0) return 0;
    if (length == 0) {
        if (reason) *reason = "empty";
        return 1;
    }
    if (length == 22) {
        if (reason) *reason = "known 22-byte incomplete";
        return 1;
    }
    if (fseek(stream, 0, SEEK_SET) != 0) return 0;
    prefix_size = (size_t)length;
    if (prefix_size >= sizeof(prefix)) prefix_size = sizeof(prefix) - 1u;
    prefix_size = fread(prefix, 1, prefix_size, stream);
    prefix[prefix_size] = 0;
    start = prefix;
    if (prefix_size >= 3u &&
        (unsigned char)start[0] == 0xefu &&
        (unsigned char)start[1] == 0xbbu &&
        (unsigned char)start[2] == 0xbfu)
        start += 3;
    while (*start && isspace((unsigned char)*start)) ++start;
    xml = strncmp(start, "<?xml", 5u) == 0 ||
          strncmp(start, "<plist", 6u) == 0;
    if (!xml) {
        fseek(stream, 0, SEEK_SET);
        return 0;
    }
    if (!strstr(start, "<plist") || !strstr(start, "<dict")) {
        if (reason) *reason = "XML header without plist data";
        fseek(stream, 0, SEEK_SET);
        return 1;
    }
    if (length > 0) {
        long suffix_offset = length > (long)(sizeof(suffix) - 1u)
                                 ? length - (long)(sizeof(suffix) - 1u) : 0;
        if (fseek(stream, suffix_offset, SEEK_SET) == 0) {
            suffix_size = fread(suffix, 1, sizeof(suffix) - 1u, stream);
            suffix[suffix_size] = 0;
        }
    }
    fseek(stream, 0, SEEK_SET);
    if (!suffix_size || !strstr(suffix, "</plist>")) {
        if (reason) *reason = "truncated XML plist";
        return 1;
    }
    return 0;
}

static uint32_t guest_open_file(ArmProbe *probe, const char *path,
                                const char *mode, int asset_only) {
    GuestFile *file;
    char translated[MAX_PATH * 4];
    char transaction[MAX_PATH * 4 + 32];
    const char *slash;
    const char *backslash;
    const char *name;
    FILE *host = NULL;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    unsigned slot;
    int owns_payload = 0;
    int writable = mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'));
    int game_file;
    int transactional = 0;
    int ignored_corrupt_save = 0;
    if (!path || !mode) return 0;

    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    name = slash && (!backslash || slash > backslash)
               ? slash + 1u : backslash ? backslash + 1u : path;
    game_file = storage_is_game_file_name(name);

    for (slot = 0; slot < probe->file_count; ++slot)
        if (!probe->files[slot].handle) break;
    if (slot == probe->file_count && probe->file_count >= MAX_GUEST_FILES) {
        probe_log("ARM file table exhausted: %u slots", MAX_GUEST_FILES);
        return 0;
    }

    if (!asset_only) {
        translate_guest_path(probe, path, translated, sizeof(translated));
        if (!writable && probe->apk_data &&
            _stricmp(translated, probe->input_path) == 0) {
            payload = probe->apk_data;
            payload_size = probe->apk_size;
        } else if (writable && game_file && strchr(mode, 'w')) {
            snprintf(transaction, sizeof(transaction), "%s.wrapper.tmp",
                     translated);
            host = fopen(transaction, mode);
            transactional = host != NULL;
        } else {
            host = fopen(translated, mode);
        }
        if (host && !writable && game_file) {
            long length = -1;
            const char *recovery_reason = NULL;
            if (fseek(host, 0, SEEK_END) == 0) length = ftell(host);
            fseek(host, 0, SEEK_SET);
            if (save_file_is_incomplete(host, length, &recovery_reason)) {
                fclose(host);
                host = NULL;
                ignored_corrupt_save = 1;
                probe_log("ARM save recovery: ignored %s %s (%ld bytes)",
                          recovery_reason ? recovery_reason : "incomplete",
                          name, length);
            }
        }
    }
    if (!host && !payload && !writable && !ignored_corrupt_save &&
        probe->input_path[0]) {
        char member[MAX_PATH * 4];
        const char *source = path;
        while (*source == '/' || *source == '\\') ++source;
        snprintf(member, sizeof(member), "%s", source);
        {
            size_t index;
            for (index = 0; member[index]; ++index)
                if (member[index] == '\\') member[index] = '/';
        }
        if (!apk_extract_member_cached(probe, member, &payload,
                                       &payload_size, &owns_payload) &&
            !path_has_prefix(member, "assets/")) {
            char with_assets[MAX_PATH * 4];
            snprintf(with_assets, sizeof(with_assets), "assets/%s", member);
            apk_extract_member_cached(probe, with_assets, &payload,
                                      &payload_size, &owns_payload);
        }
    }
    if (!host && !payload) {
        probe_log("ARM file open failed: %s mode=%s", path, mode);
        return 0;
    }
    file = &probe->files[slot];
    memset(file, 0, sizeof(*file));
    file->handle = GUEST_FILE_BASE + slot * 0x100u;
    file->host = host;
    file->payload = payload;
    file->size = payload_size;
    file->owns_payload = owns_payload;
    file->writable = writable;
    file->transactional = transactional;
    if (transactional) {
        file->transaction_path = copy_string(transaction);
        file->final_path = copy_string(translated);
        if (!file->transaction_path || !file->final_path) {
            fclose(host);
            DeleteFileA(transaction);
            free(file->transaction_path);
            free(file->final_path);
            memset(file, 0, sizeof(*file));
            probe_log("ARM file open failed: cannot track save transaction %s",
                      name);
            return 0;
        }
    }
    if (game_file)
        snprintf(file->diagnostic_name,
                 sizeof(file->diagnostic_name), "%s", name);
    if (host && strchr(mode, 'a')) fseek(host, 0, SEEK_END);
    if (slot == probe->file_count) ++probe->file_count;
    ++probe->profile_file_opens;
    if (payload == probe->apk_data) {
        ++probe->profile_apk_opens;
        if (probe->apk_cache_open_logs++ < 2u)
            probe_log("ARM file open: %s mode=%s source=shared APK cache "
                      "bytes=%u", path, mode, (unsigned)payload_size);
    } else if (host)
        probe_log("ARM file open: %s mode=%s source=host%s", path, mode,
                  transactional ? " transaction=atomic" : "");
    else
        probe_log("ARM file open: %s mode=%s source=APK member cache bytes=%u",
                  path, mode, (unsigned)payload_size);
    return file->handle;
}

static size_t guest_file_read(ArmProbe *probe, GuestFile *file,
                              uint32_t destination, size_t bytes) {
    size_t total = 0;
    if (!file || !destination || !bytes) return 0;
    if (!file->host) {
        size_t actual = file->position < file->size
                            ? file->size - file->position : 0;
        if (actual > bytes) actual = bytes;
        if (actual &&
            uc_mem_write(probe->uc, destination,
                         file->payload + file->position, actual) == UC_ERR_OK) {
            file->position += actual;
            total = actual;
        }
        file->eof = file->position >= file->size;
    } else {
        size_t scratch_size = bytes < HOST_IO_SCRATCH_BYTES
                                  ? bytes : HOST_IO_SCRATCH_BYTES;
        if (!host_buffer_reserve(&probe->io_buffer,
                                 &probe->io_buffer_capacity,
                                 scratch_size))
            return 0;
        while (total < bytes) {
            size_t part = bytes - total;
            size_t actual;
            if (part > scratch_size) part = scratch_size;
            if ((uint64_t)destination + total > UINT32_MAX) break;
            actual = fread(probe->io_buffer, 1, part, file->host);
            if (actual &&
                uc_mem_write(probe->uc, destination + (uint32_t)total,
                             probe->io_buffer, actual) != UC_ERR_OK)
                break;
            total += actual;
            if (actual < part) break;
        }
        file->eof = feof(file->host) != 0;
    }
    file->bytes_read += total;
    probe->profile_file_read_bytes += total;
    return total;
}

static int guest_file_get_character(GuestFile *file) {
    int value;
    if (!file) return EOF;
    if (file->host) {
        value = fgetc(file->host);
        file->eof = value == EOF && feof(file->host) != 0;
    } else if (file->position < file->size) {
        value = file->payload[file->position++];
        file->eof = file->position >= file->size;
    } else {
        value = EOF;
        file->eof = 1;
    }
    if (value != EOF) ++file->bytes_read;
    return value;
}

static size_t guest_file_write(ArmProbe *probe, GuestFile *file,
                               uint32_t source, size_t bytes) {
    size_t total = 0;
    size_t scratch_size;
    if (!file || !file->host || !source || !bytes) return 0;
    scratch_size = bytes < HOST_IO_SCRATCH_BYTES
                       ? bytes : HOST_IO_SCRATCH_BYTES;
    if (!host_buffer_reserve(&probe->io_buffer,
                             &probe->io_buffer_capacity, scratch_size))
        return 0;
    while (total < bytes) {
        size_t part = bytes - total;
        size_t actual;
        if (part > scratch_size) part = scratch_size;
        if ((uint64_t)source + total > UINT32_MAX ||
            uc_mem_read(probe->uc, source + (uint32_t)total,
                        probe->io_buffer, part) != UC_ERR_OK)
            break;
        actual = fwrite(probe->io_buffer, 1, part, file->host);
        total += actual;
        if (actual < part) break;
    }
    file->bytes_written += total;
    return total;
}

static int guest_file_seek(GuestFile *file, long offset, int origin) {
    if (!file) return -1;
    file->eof = 0;
    if (file->host) return fseek(file->host, offset, origin);
    {
        int64_t base = origin == SEEK_SET ? 0 :
                       origin == SEEK_CUR ? (int64_t)file->position :
                       origin == SEEK_END ? (int64_t)file->size : -1;
        int64_t position = base + offset;
        if (base < 0 || position < 0 || (uint64_t)position > file->size) return -1;
        file->position = (size_t)position;
        return 0;
    }
}

static long guest_file_tell(GuestFile *file) {
    if (!file) return -1;
    return file->host ? ftell(file->host) : (long)file->position;
}

static int guest_file_close(ArmProbe *probe, GuestFile *file) {
    int result = 0;
    if (!file) return -1;
    if (file->diagnostic_name[0]) {
        long position = file->host ? ftell(file->host) : (long)file->position;
        long length = -1;
        if (file->host && position >= 0 && fseek(file->host, 0, SEEK_END) == 0) {
            length = ftell(file->host);
            fseek(file->host, position, SEEK_SET);
        } else if (!file->host) {
            length = (long)file->size;
        }
        probe_log("ARM data file close: %s read=%llu written=%llu "
                  "length=%ld",
                  file->diagnostic_name,
                  (unsigned long long)file->bytes_read,
                  (unsigned long long)file->bytes_written, length);
    }
    if (file->host) {
        if (file->transactional &&
            (fflush(file->host) != 0 || _commit(_fileno(file->host)) != 0))
            result = EOF;
        if (fclose(file->host) != 0) result = EOF;
        file->host = NULL;
    }
    if (file->transactional && file->transaction_path && file->final_path) {
        if (result == 0 && probe && !probe->allocation_failures) {
            if (MoveFileExA(file->transaction_path, file->final_path,
                            MOVEFILE_REPLACE_EXISTING |
                                MOVEFILE_WRITE_THROUGH)) {
                probe_log("ARM save transaction committed: %s",
                          file->diagnostic_name[0]
                              ? file->diagnostic_name : file->final_path);
            } else {
                result = EOF;
                probe_log("ARM save transaction failed: %s error=%lu",
                          file->diagnostic_name[0]
                              ? file->diagnostic_name : file->final_path,
                          (unsigned long)GetLastError());
                DeleteFileA(file->transaction_path);
            }
        } else {
            DeleteFileA(file->transaction_path);
            if (probe && probe->allocation_failures) {
                probe_log("ARM save transaction quarantined after allocation "
                          "failure: %s (previous save preserved)",
                          file->diagnostic_name[0]
                              ? file->diagnostic_name : file->final_path);
                /* The guest completed its write; keeping the previous file is
                   a host-side recovery policy, not a guest I/O error. */
                if (result == 0) result = 0;
            }
        }
    }
    if (file->owns_payload) free(file->payload);
    free(file->transaction_path);
    free(file->final_path);
    file->payload = NULL;
    file->transaction_path = NULL;
    file->final_path = NULL;
    file->handle = 0;
    return result;
}

static GuestZStream *guest_zstream(ArmProbe *probe, uint32_t guest_address) {
    unsigned index;
    for (index = 0; index < probe->zstream_count; ++index) {
        if (probe->zstreams[index].guest_address == guest_address)
            return &probe->zstreams[index];
    }
    return NULL;
}

static void guest_zstream_release(GuestZStream *stream) {
    if (!stream || !stream->active) return;
    if (stream->kind == GUEST_ZSTREAM_INFLATE)
        inflateEnd(&stream->host);
    else if (stream->kind == GUEST_ZSTREAM_DEFLATE)
        deflateEnd(&stream->host);
    stream->active = 0;
    stream->kind = GUEST_ZSTREAM_NONE;
    memset(&stream->host, 0, sizeof(stream->host));
}

static GuestZStream *guest_zstream_prepare(ArmProbe *probe,
                                           uint32_t guest_address,
                                           GuestZStreamKind kind) {
    GuestZStream *stream = guest_zstream(probe, guest_address);
    unsigned index;
    if (!guest_address) return NULL;
    if (!stream) {
        for (index = 0; index < probe->zstream_count; ++index)
            if (!probe->zstreams[index].active) break;
        if (index == probe->zstream_count) {
            if (probe->zstream_count >= MAX_GUEST_ZSTREAMS) return NULL;
            ++probe->zstream_count;
        }
        stream = &probe->zstreams[index];
        memset(stream, 0, sizeof(*stream));
        stream->guest_address = guest_address;
    } else {
        guest_zstream_release(stream);
    }
    stream->kind = kind;
    return stream;
}

static int guest_zstream_read_layout(ArmProbe *probe, uint32_t address,
                                     GuestZStreamLayout *layout) {
    return address && layout &&
           uc_mem_read(probe->uc, address, layout, sizeof(*layout)) == UC_ERR_OK;
}

static int guest_zstream_write_layout(ArmProbe *probe, GuestZStream *stream,
                                      GuestZStreamLayout *layout) {
    layout->total_in = (uint32_t)stream->host.total_in;
    layout->total_out = (uint32_t)stream->host.total_out;
    layout->msg = 0;
    layout->state = stream->active ? stream->guest_address : 0;
    layout->data_type = stream->host.data_type;
    layout->adler = (uint32_t)stream->host.adler;
    layout->reserved = (uint32_t)stream->host.reserved;
    return uc_mem_write(probe->uc, stream->guest_address, layout,
                        sizeof(*layout)) == UC_ERR_OK;
}

static int guest_inflate_init(ArmProbe *probe, uint32_t guest_address,
                              int use_window_bits, int window_bits) {
    GuestZStreamLayout layout;
    GuestZStream *stream;
    int result;
    if (!guest_zstream_read_layout(probe, guest_address, &layout))
        return Z_STREAM_ERROR;
    stream = guest_zstream_prepare(probe, guest_address,
                                   GUEST_ZSTREAM_INFLATE);
    if (!stream) return Z_MEM_ERROR;
    result = use_window_bits
                 ? inflateInit2_(&stream->host, window_bits, ZLIB_VERSION,
                                 (int)sizeof(z_stream))
                 : inflateInit_(&stream->host, ZLIB_VERSION,
                                (int)sizeof(z_stream));
    stream->active = result == Z_OK;
    if (!guest_zstream_write_layout(probe, stream, &layout)) {
        guest_zstream_release(stream);
        return Z_STREAM_ERROR;
    }
    if (result == Z_OK) {
        unsigned log_index = probe->zlib_stream_logs++;
        if (log_index < 16u)
            probe_log("ARM zlib inflate stream ready: guest=0x%08x window=%d",
                      guest_address, use_window_bits ? window_bits : MAX_WBITS);
        else if (log_index == 16u)
            probe_log("ARM zlib stream-ready logs suppressed; profiling remains active");
    }
    return result;
}

static int guest_deflate_init(ArmProbe *probe, uint32_t guest_address,
                              int level, int extended, int method,
                              int window_bits, int mem_level, int strategy) {
    GuestZStreamLayout layout;
    GuestZStream *stream;
    int result;
    if (!guest_zstream_read_layout(probe, guest_address, &layout))
        return Z_STREAM_ERROR;
    stream = guest_zstream_prepare(probe, guest_address,
                                   GUEST_ZSTREAM_DEFLATE);
    if (!stream) return Z_MEM_ERROR;
    result = extended
                 ? deflateInit2_(&stream->host, level, method, window_bits,
                                 mem_level, strategy, ZLIB_VERSION,
                                 (int)sizeof(z_stream))
                 : deflateInit_(&stream->host, level, ZLIB_VERSION,
                                (int)sizeof(z_stream));
    stream->active = result == Z_OK;
    if (!guest_zstream_write_layout(probe, stream, &layout)) {
        guest_zstream_release(stream);
        return Z_STREAM_ERROR;
    }
    if (result == Z_OK) {
        unsigned log_index = probe->zlib_stream_logs++;
        if (log_index < 16u)
            probe_log("ARM zlib deflate stream ready: guest=0x%08x",
                      guest_address);
        else if (log_index == 16u)
            probe_log("ARM zlib stream-ready logs suppressed; profiling remains active");
    }
    return result;
}

static int guest_zstream_process(ArmProbe *probe, uint32_t guest_address,
                                 GuestZStreamKind kind, int flush) {
    GuestZStream *stream = guest_zstream(probe, guest_address);
    GuestZStreamLayout layout;
    GuestMemoryRegion *input_region = NULL;
    GuestMemoryRegion *output_region = NULL;
    unsigned char *input = NULL;
    unsigned char *output = NULL;
    int owns_input = 0;
    int owns_output = 0;
    uint32_t input_size;
    uint32_t output_size;
    uint32_t consumed = 0;
    uint32_t produced = 0;
    int result = Z_STREAM_ERROR;
    ++probe->profile_zlib_calls;
    if (!stream || !stream->active || stream->kind != kind ||
        !guest_zstream_read_layout(probe, guest_address, &layout))
        return Z_STREAM_ERROR;
    input_size = layout.avail_in;
    output_size = layout.avail_out;
    if (input_size > MAX_GUEST_ZLIB_BUFFER ||
        output_size > MAX_GUEST_ZLIB_BUFFER)
        return Z_MEM_ERROR;
    if (input_size) {
        if (!layout.next_in) goto finished;
        input_region = guest_memory_region(probe, layout.next_in, input_size);
        if (input_region) {
            input = input_region->host +
                    (size_t)(layout.next_in - input_region->base);
            ++probe->direct_memory_reads;
        } else {
            input = (unsigned char *)malloc(input_size);
            owns_input = input != NULL;
            if (!input || uc_mem_read(probe->uc, layout.next_in,
                                      input, input_size) != UC_ERR_OK)
                goto finished;
        }
    }
    if (output_size) {
        if (!layout.next_out) goto finished;
        output_region = guest_memory_region(probe, layout.next_out, output_size);
        if (output_region && output_region->direct_write) {
            output = output_region->host +
                     (size_t)(layout.next_out - output_region->base);
            ++probe->direct_memory_writes;
        } else {
            output = (unsigned char *)malloc(output_size);
            owns_output = output != NULL;
            if (!output) goto finished;
        }
    }
    stream->host.next_in = input;
    stream->host.avail_in = input_size;
    stream->host.next_out = output;
    stream->host.avail_out = output_size;
    result = kind == GUEST_ZSTREAM_INFLATE
                 ? inflate(&stream->host, flush)
                 : deflate(&stream->host, flush);
    consumed = input_size - stream->host.avail_in;
    produced = output_size - stream->host.avail_out;
    if (owns_output && produced &&
        uc_mem_write(probe->uc, layout.next_out, output, produced) != UC_ERR_OK) {
        result = Z_STREAM_ERROR;
        goto finished;
    }
    layout.next_in += consumed;
    layout.avail_in -= consumed;
    layout.next_out += produced;
    layout.avail_out -= produced;
    if (!guest_zstream_write_layout(probe, stream, &layout))
        result = Z_STREAM_ERROR;

finished:
    stream->host.next_in = Z_NULL;
    stream->host.avail_in = 0;
    stream->host.next_out = Z_NULL;
    stream->host.avail_out = 0;
    if (owns_input) free(input);
    if (owns_output) free(output);
    return result;
}

static int guest_zstream_reset(ArmProbe *probe, uint32_t guest_address,
                               GuestZStreamKind kind) {
    GuestZStream *stream = guest_zstream(probe, guest_address);
    GuestZStreamLayout layout;
    int result;
    if (!stream || !stream->active || stream->kind != kind ||
        !guest_zstream_read_layout(probe, guest_address, &layout))
        return Z_STREAM_ERROR;
    result = kind == GUEST_ZSTREAM_INFLATE
                 ? inflateReset(&stream->host) : deflateReset(&stream->host);
    if (!guest_zstream_write_layout(probe, stream, &layout))
        return Z_STREAM_ERROR;
    return result;
}

static int guest_zstream_end(ArmProbe *probe, uint32_t guest_address,
                             GuestZStreamKind kind) {
    GuestZStream *stream = guest_zstream(probe, guest_address);
    GuestZStreamLayout layout;
    int result;
    if (!stream || !stream->active || stream->kind != kind ||
        !guest_zstream_read_layout(probe, guest_address, &layout))
        return Z_STREAM_ERROR;
    result = kind == GUEST_ZSTREAM_INFLATE
                 ? inflateEnd(&stream->host) : deflateEnd(&stream->host);
    stream->active = 0;
    stream->kind = GUEST_ZSTREAM_NONE;
    if (!guest_zstream_write_layout(probe, stream, &layout))
        return Z_STREAM_ERROR;
    memset(&stream->host, 0, sizeof(stream->host));
    return result;
}

static int guest_inflate_sync(ArmProbe *probe, uint32_t guest_address) {
    GuestZStream *stream = guest_zstream(probe, guest_address);
    GuestZStreamLayout layout;
    GuestMemoryRegion *input_region = NULL;
    unsigned char *input = NULL;
    int owns_input = 0;
    uint32_t input_size;
    uint32_t consumed = 0;
    int result = Z_STREAM_ERROR;
    if (!stream || !stream->active ||
        stream->kind != GUEST_ZSTREAM_INFLATE ||
        !guest_zstream_read_layout(probe, guest_address, &layout))
        return Z_STREAM_ERROR;
    input_size = layout.avail_in;
    if (input_size > MAX_GUEST_ZLIB_BUFFER) return Z_MEM_ERROR;
    if (input_size) {
        if (!layout.next_in) goto finished;
        input_region = guest_memory_region(probe, layout.next_in, input_size);
        if (input_region) {
            input = input_region->host +
                    (size_t)(layout.next_in - input_region->base);
            ++probe->direct_memory_reads;
        } else {
            input = (unsigned char *)malloc(input_size);
            owns_input = input != NULL;
            if (!input || uc_mem_read(probe->uc, layout.next_in,
                                      input, input_size) != UC_ERR_OK)
                goto finished;
        }
    }
    stream->host.next_in = input;
    stream->host.avail_in = input_size;
    result = inflateSync(&stream->host);
    consumed = input_size - stream->host.avail_in;
    layout.next_in += consumed;
    layout.avail_in -= consumed;
    if (!guest_zstream_write_layout(probe, stream, &layout))
        result = Z_STREAM_ERROR;

finished:
    stream->host.next_in = Z_NULL;
    stream->host.avail_in = 0;
    if (owns_input) free(input);
    return result;
}

static int guest_inflate_copy(ArmProbe *probe, uint32_t destination_address,
                              uint32_t source_address) {
    GuestZStream *source = guest_zstream(probe, source_address);
    GuestZStream *destination;
    GuestZStreamLayout layout;
    int result;
    if (!source || !source->active ||
        source->kind != GUEST_ZSTREAM_INFLATE ||
        !guest_zstream_read_layout(probe, source_address, &layout))
        return Z_STREAM_ERROR;
    destination = guest_zstream_prepare(probe, destination_address,
                                        GUEST_ZSTREAM_INFLATE);
    if (!destination) return Z_MEM_ERROR;
    result = inflateCopy(&destination->host, &source->host);
    destination->active = result == Z_OK;
    if (!guest_zstream_write_layout(probe, destination, &layout)) {
        guest_zstream_release(destination);
        return Z_STREAM_ERROR;
    }
    return result;
}

static int guest_deflate_params(ArmProbe *probe, uint32_t guest_address,
                                int level, int strategy) {
    GuestZStream *stream = guest_zstream(probe, guest_address);
    GuestZStreamLayout layout;
    GuestMemoryRegion *output_region = NULL;
    unsigned char *output = NULL;
    int owns_output = 0;
    uint32_t output_size;
    uint32_t produced = 0;
    int result = Z_STREAM_ERROR;
    if (!stream || !stream->active ||
        stream->kind != GUEST_ZSTREAM_DEFLATE ||
        !guest_zstream_read_layout(probe, guest_address, &layout))
        return Z_STREAM_ERROR;
    output_size = layout.avail_out;
    if (output_size > MAX_GUEST_ZLIB_BUFFER) return Z_MEM_ERROR;
    if (output_size) {
        if (!layout.next_out) goto finished;
        output_region = guest_memory_region(probe, layout.next_out, output_size);
        if (output_region && output_region->direct_write) {
            output = output_region->host +
                     (size_t)(layout.next_out - output_region->base);
            ++probe->direct_memory_writes;
        } else {
            output = (unsigned char *)malloc(output_size);
            owns_output = output != NULL;
            if (!output) goto finished;
        }
    }
    stream->host.next_out = output;
    stream->host.avail_out = output_size;
    result = deflateParams(&stream->host, level, strategy);
    produced = output_size - stream->host.avail_out;
    if (owns_output && produced &&
        uc_mem_write(probe->uc, layout.next_out, output, produced) != UC_ERR_OK) {
        result = Z_STREAM_ERROR;
        goto finished;
    }
    layout.next_out += produced;
    layout.avail_out -= produced;
    if (!guest_zstream_write_layout(probe, stream, &layout))
        result = Z_STREAM_ERROR;

finished:
    stream->host.next_out = Z_NULL;
    stream->host.avail_out = 0;
    if (owns_output) free(output);
    return result;
}

static uint32_t cursor_word(GuestArgCursor *cursor) {
    uint32_t result = 0;
    if (cursor->direct_address) {
        uc_mem_read(cursor->probe->uc, cursor->direct_address, &result, sizeof(result));
        cursor->direct_address += cursor->jvalue_array ? 8u : 4u;
        return result;
    }
    if (cursor->word_position < 4u)
        result = cursor->registers[cursor->word_position];
    else
        uc_mem_read(cursor->probe->uc,
                    cursor->sp + (cursor->word_position - 4u) * 4u,
                    &result, sizeof(result));
    ++cursor->word_position;
    return result;
}

static uint64_t cursor_u64(GuestArgCursor *cursor) {
    uint32_t low, high;
    if (cursor->direct_address) {
        if (!cursor->jvalue_array)
            cursor->direct_address = align_up(cursor->direct_address, 8u);
        uc_mem_read(cursor->probe->uc, cursor->direct_address, &low, sizeof(low));
        uc_mem_read(cursor->probe->uc, cursor->direct_address + 4u, &high, sizeof(high));
        cursor->direct_address += 8u;
        return join_u64(low, high);
    }
    if (cursor->word_position & 1u) ++cursor->word_position;
    low = cursor_word(cursor);
    high = cursor_word(cursor);
    return join_u64(low, high);
}

static float cursor_float_argument(GuestArgCursor *cursor) {
    if (cursor->jvalue_array && cursor->direct_address) {
        uint32_t bits = 0;
        uc_mem_read(cursor->probe->uc, cursor->direct_address, &bits, sizeof(bits));
        cursor->direct_address += 8u;
        return bits_float(bits);
    }
    {
        uint64_t bits = cursor_u64(cursor);
        return (float)bits_double((uint32_t)bits, (uint32_t)(bits >> 32));
    }
}

static void cursor_initialize(GuestArgCursor *cursor, ArmProbe *probe,
                              uint32_t r0, uint32_t r1, uint32_t r2,
                              uint32_t r3, uint32_t sp, int indirect_mode) {
    memset(cursor, 0, sizeof(*cursor));
    cursor->probe = probe;
    cursor->registers[0] = r0;
    cursor->registers[1] = r1;
    cursor->registers[2] = r2;
    cursor->registers[3] = r3;
    cursor->sp = sp;
    cursor->word_position = 3u;
    if (indirect_mode) cursor->direct_address = r3;
    cursor->jvalue_array = indirect_mode == 2;
}


typedef struct {
    char *data;
    size_t capacity;
    size_t stored;
    size_t total;
} GuestFormatOutput;

static void format_append(GuestFormatOutput *output, const char *text,
                          size_t length) {
    size_t available = 0;
    size_t copy = 0;
    if (!output || !text) return;
    if (output->capacity && output->stored < output->capacity - 1u)
        available = output->capacity - 1u - output->stored;
    copy = length < available ? length : available;
    if (copy) memcpy(output->data + output->stored, text, copy);
    output->stored += copy;
    output->total += length;
    if (output->capacity) output->data[output->stored] = 0;
}

static void format_append_repeat(GuestFormatOutput *output, char value,
                                 size_t count) {
    char block[64];
    memset(block, value, sizeof(block));
    while (count) {
        size_t part = count < sizeof(block) ? count : sizeof(block);
        format_append(output, block, part);
        count -= part;
    }
}

static void format_append_padded_text(GuestFormatOutput *output,
                                      const char *text, size_t length,
                                      int width, int left_aligned) {
    size_t padding = width > 0 && (size_t)width > length
                         ? (size_t)width - length : 0;
    if (!left_aligned) format_append_repeat(output, ' ', padding);
    format_append(output, text, length);
    if (left_aligned) format_append_repeat(output, ' ', padding);
}

static int format_append_guest_string(ArmProbe *probe,
                                      GuestFormatOutput *output,
                                      uint32_t address, int width,
                                      int precision, int left_aligned) {
    static const char null_text[] = "(null)";
    static const char invalid_text[] = "<invalid>";
    unsigned char block[4096];
    uint32_t limit = precision >= 0 && (uint32_t)precision < MAX_GUEST_CSTRING
                         ? (uint32_t)precision : MAX_GUEST_CSTRING;
    uint32_t length = 0;
    uint32_t offset = 0;
    int terminated = 0;
    size_t padding;
    if (!address) {
        size_t length_value = sizeof(null_text) - 1u;
        if (precision >= 0 && (size_t)precision < length_value)
            length_value = (size_t)precision;
        format_append_padded_text(output, null_text, length_value, width,
                                  left_aligned);
        return 1;
    }
    if (!guest_cstring_nlength(probe, address, limit, &length, &terminated) ||
        (precision < 0 && !terminated)) {
        format_append_padded_text(output, invalid_text,
                                  sizeof(invalid_text) - 1u, width,
                                  left_aligned);
        return 0;
    }
    padding = width > 0 && (size_t)width > length
                  ? (size_t)width - length : 0;
    if (!left_aligned) format_append_repeat(output, ' ', padding);
    if (!output->capacity) {
        output->total += length;
    } else {
        while (offset < length) {
            uint32_t part = length - offset;
            if (part > sizeof(block)) part = sizeof(block);
            if (offset > UINT32_MAX - address ||
                uc_mem_read(probe->uc, address + offset, block, part) !=
                    UC_ERR_OK) {
                format_append(output, invalid_text,
                              sizeof(invalid_text) - 1u);
                return 0;
            }
            format_append(output, (const char *)block, part);
            offset += part;
        }
    }
    if (left_aligned) format_append_repeat(output, ' ', padding);
    return 1;
}

static void cursor_setup(GuestArgCursor *cursor, ArmProbe *probe,
                         uint32_t r0, uint32_t r1, uint32_t r2,
                         uint32_t r3, uint32_t sp, unsigned word_position,
                         uint32_t direct_address) {
    memset(cursor, 0, sizeof(*cursor));
    cursor->probe = probe;
    cursor->registers[0] = r0;
    cursor->registers[1] = r1;
    cursor->registers[2] = r2;
    cursor->registers[3] = r3;
    cursor->sp = sp;
    cursor->word_position = word_position;
    cursor->direct_address = direct_address;
}

static int guest_vformat(ArmProbe *probe, char *destination, size_t capacity,
                         const char *format, GuestArgCursor *arguments) {
    GuestFormatOutput output;
    const char *cursor = format ? format : "";
    char temporary[8192];
    memset(&output, 0, sizeof(output));
    output.data = destination;
    output.capacity = capacity;
    if (capacity) destination[0] = 0;
    while (*cursor) {
        const char *literal = cursor;
        char flags[16] = "";
        size_t flag_count = 0;
        int width = -1;
        int precision = -1;
        char length[3] = "";
        char specifier;
        char token[96];
        int token_length;
        if (*cursor != '%') {
            while (*cursor && *cursor != '%') ++cursor;
            format_append(&output, literal, (size_t)(cursor - literal));
            continue;
        }
        ++cursor;
        if (*cursor == '%') {
            format_append(&output, "%", 1);
            ++cursor;
            continue;
        }
        while (*cursor && strchr("-+ #0'", *cursor)) {
            if (flag_count + 1u < sizeof(flags)) flags[flag_count++] = *cursor;
            ++cursor;
        }
        flags[flag_count] = 0;
        if (*cursor == '*') {
            width = (int32_t)cursor_word(arguments);
            ++cursor;
            if (width < 0) {
                width = -width;
                if (!strchr(flags, '-') && flag_count + 1u < sizeof(flags)) {
                    flags[flag_count++] = '-';
                    flags[flag_count] = 0;
                }
            }
        } else if (isdigit((unsigned char)*cursor)) {
            width = 0;
            while (isdigit((unsigned char)*cursor)) {
                if (width < 100000) width = width * 10 + (*cursor - '0');
                ++cursor;
            }
        }
        if (*cursor == '.') {
            ++cursor;
            if (*cursor == '*') {
                precision = (int32_t)cursor_word(arguments);
                ++cursor;
                if (precision < 0) precision = -1;
            } else {
                precision = 0;
                while (isdigit((unsigned char)*cursor)) {
                    if (precision < 100000)
                        precision = precision * 10 + (*cursor - '0');
                    ++cursor;
                }
            }
        }
        if (*cursor == 'h' || *cursor == 'l') {
            length[0] = *cursor++;
            if (*cursor == length[0]) length[1] = *cursor++;
        } else if (*cursor == 'z' || *cursor == 't' || *cursor == 'j' ||
                   *cursor == 'L') {
            length[0] = *cursor++;
        }
        specifier = *cursor;
        if (!specifier) break;
        ++cursor;
        token_length = snprintf(token, sizeof(token), "%%%s%s%s%s%c", flags,
                                width >= 0 ? "*" : "",
                                precision >= 0 ? ".*" : "", length, specifier);
        if (token_length <= 0 || (size_t)token_length >= sizeof(token)) continue;
#define FORMAT_VALUE(value) \
        do { \
            if (width >= 0 && precision >= 0) \
                token_length = snprintf(temporary, sizeof(temporary), token, width, precision, (value)); \
            else if (width >= 0) \
                token_length = snprintf(temporary, sizeof(temporary), token, width, (value)); \
            else if (precision >= 0) \
                token_length = snprintf(temporary, sizeof(temporary), token, precision, (value)); \
            else \
                token_length = snprintf(temporary, sizeof(temporary), token, (value)); \
        } while (0)
        token_length = 0;
        switch (specifier) {
        case 's': {
            uint32_t address = cursor_word(arguments);
            format_append_guest_string(probe, &output, address, width,
                                       precision, strchr(flags, '-') != NULL);
            token_length = 0;
            break;
        }
        case 'c': FORMAT_VALUE((int)cursor_word(arguments)); break;
        case 'd': case 'i': {
            if (strcmp(length, "ll") == 0 || strcmp(length, "j") == 0)
                FORMAT_VALUE((long long)(int64_t)cursor_u64(arguments));
            else if (strcmp(length, "l") == 0)
                FORMAT_VALUE((long)(int32_t)cursor_word(arguments));
            else FORMAT_VALUE((int)(int32_t)cursor_word(arguments));
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            if (strcmp(length, "ll") == 0 || strcmp(length, "j") == 0)
                FORMAT_VALUE((unsigned long long)cursor_u64(arguments));
            else if (strcmp(length, "l") == 0 || strcmp(length, "z") == 0 ||
                     strcmp(length, "t") == 0)
                FORMAT_VALUE((unsigned long)cursor_word(arguments));
            else FORMAT_VALUE((unsigned)cursor_word(arguments));
            break;
        }
        case 'p': FORMAT_VALUE((void *)(uintptr_t)cursor_word(arguments)); break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
        case 'a': case 'A': {
            uint64_t bits = cursor_u64(arguments);
            FORMAT_VALUE(bits_double((uint32_t)bits, (uint32_t)(bits >> 32)));
            break;
        }
        case 'n': {
            uint32_t address = cursor_word(arguments);
            uint32_t count = (uint32_t)output.total;
            if (address) uc_mem_write(probe->uc, address, &count, sizeof(count));
            token_length = 0;
            break;
        }
        default:
            format_append(&output, "%", 1);
            format_append(&output, &specifier, 1);
            token_length = 0;
            break;
        }
#undef FORMAT_VALUE
        if (token_length > 0) {
            size_t available = sizeof(temporary) - 1u;
            size_t actual = (size_t)token_length < available
                                ? (size_t)token_length : available;
            format_append(&output, temporary, actual);
            if ((size_t)token_length > actual)
                format_append_repeat(&output, ' ', (size_t)token_length - actual);
        }
    }
    return output.total > INT32_MAX ? INT32_MAX : (int)output.total;
}

typedef enum {
    GUEST_SCAN_LENGTH_DEFAULT = 0,
    GUEST_SCAN_LENGTH_HH,
    GUEST_SCAN_LENGTH_H,
    GUEST_SCAN_LENGTH_L,
    GUEST_SCAN_LENGTH_LL,
    GUEST_SCAN_LENGTH_J,
    GUEST_SCAN_LENGTH_Z,
    GUEST_SCAN_LENGTH_T,
    GUEST_SCAN_LENGTH_CAPITAL_L
} GuestScanLength;

static size_t guest_scan_integer_size(GuestScanLength length) {
    switch (length) {
    case GUEST_SCAN_LENGTH_HH: return 1u;
    case GUEST_SCAN_LENGTH_H: return 2u;
    case GUEST_SCAN_LENGTH_LL:
    case GUEST_SCAN_LENGTH_J: return 8u;
    case GUEST_SCAN_LENGTH_DEFAULT:
    case GUEST_SCAN_LENGTH_L:
    case GUEST_SCAN_LENGTH_Z:
    case GUEST_SCAN_LENGTH_T:
    case GUEST_SCAN_LENGTH_CAPITAL_L:
    default: return 4u;
    }
}

static int guest_scan_write_integer(ArmProbe *probe, uint32_t address,
                                    uint64_t value, GuestScanLength length) {
    size_t size = guest_scan_integer_size(length);
    if (!address) return 0;
    if (size == 1u) {
        uint8_t narrowed = (uint8_t)value;
        return uc_mem_write(probe->uc, address, &narrowed,
                            sizeof(narrowed)) == UC_ERR_OK;
    }
    if (size == 2u) {
        uint16_t narrowed = (uint16_t)value;
        return uc_mem_write(probe->uc, address, &narrowed,
                            sizeof(narrowed)) == UC_ERR_OK;
    }
    if (size == 8u)
        return uc_mem_write(probe->uc, address, &value,
                            sizeof(value)) == UC_ERR_OK;
    {
        uint32_t narrowed = (uint32_t)value;
        return uc_mem_write(probe->uc, address, &narrowed,
                            sizeof(narrowed)) == UC_ERR_OK;
    }
}

static int guest_scan_write_float(ArmProbe *probe, uint32_t address,
                                  double value, GuestScanLength length) {
    if (!address) return 0;
    if (length == GUEST_SCAN_LENGTH_L ||
        length == GUEST_SCAN_LENGTH_CAPITAL_L) {
        return uc_mem_write(probe->uc, address, &value,
                            sizeof(value)) == UC_ERR_OK;
    }
    {
        float narrowed = (float)value;
        return uc_mem_write(probe->uc, address, &narrowed,
                            sizeof(narrowed)) == UC_ERR_OK;
    }
}

static int guest_scan_write_text(ArmProbe *probe, uint32_t address,
                                 const char *text, size_t length,
                                 int terminate, int wide) {
    if (!address) return 0;
    if (!wide) {
        unsigned char zero = 0;
        if (length && uc_mem_write(probe->uc, address, text, length) != UC_ERR_OK)
            return 0;
        return !terminate ||
               uc_mem_write(probe->uc, address + (uint32_t)length,
                            &zero, sizeof(zero)) == UC_ERR_OK;
    }
    {
        size_t elements = length + (terminate ? 1u : 0u);
        uint32_t *wide_text;
        size_t index;
        if (elements > UINT32_MAX / sizeof(*wide_text)) return 0;
        wide_text = (uint32_t *)calloc(elements ? elements : 1u,
                                       sizeof(*wide_text));
        if (!wide_text) return 0;
        for (index = 0; index < length; ++index)
            wide_text[index] = (unsigned char)text[index];
        index = uc_mem_write(probe->uc, address, wide_text,
                             elements * sizeof(*wide_text)) == UC_ERR_OK;
        free(wide_text);
        return (int)index;
    }
}

static int guest_scan_set_contains(unsigned char value, const char *begin,
                                   const char *end) {
    const char *cursor = begin;
    while (cursor < end) {
        if (cursor + 2 < end && cursor[1] == '-') {
            unsigned char first = (unsigned char)cursor[0];
            unsigned char last = (unsigned char)cursor[2];
            if ((first <= value && value <= last) ||
                (last <= value && value <= first))
                return 1;
            cursor += 3;
        } else {
            if ((unsigned char)*cursor == value) return 1;
            ++cursor;
        }
    }
    return 0;
}

static int guest_vscan(ArmProbe *probe, const char *input, const char *format,
                       GuestArgCursor *arguments) {
    const char *input_begin = input ? input : "";
    const char *input_cursor = input_begin;
    const char *format_cursor = format ? format : "";
    int assignments = 0;
    int input_failure = 0;

    while (*format_cursor) {
        int suppress = 0;
        size_t width = 0;
        int has_width = 0;
        GuestScanLength length = GUEST_SCAN_LENGTH_DEFAULT;
        char specifier;

        if (isspace((unsigned char)*format_cursor)) {
            while (isspace((unsigned char)*format_cursor)) ++format_cursor;
            while (isspace((unsigned char)*input_cursor)) ++input_cursor;
            continue;
        }
        if (*format_cursor != '%') {
            if (!*input_cursor) {
                input_failure = 1;
                break;
            }
            if (*input_cursor != *format_cursor) break;
            ++input_cursor;
            ++format_cursor;
            continue;
        }
        ++format_cursor;
        if (*format_cursor == '%') {
            if (!*input_cursor) {
                input_failure = 1;
                break;
            }
            if (*input_cursor != '%') break;
            ++input_cursor;
            ++format_cursor;
            continue;
        }
        if (*format_cursor == '*') {
            suppress = 1;
            ++format_cursor;
        }
        if (isdigit((unsigned char)*format_cursor)) {
            has_width = 1;
            while (isdigit((unsigned char)*format_cursor)) {
                if (width < MAX_STRING)
                    width = width * 10u + (unsigned)(*format_cursor - '0');
                if (width >= MAX_STRING) width = MAX_STRING - 1u;
                ++format_cursor;
            }
            if (!width) break;
        }
        if (*format_cursor == 'h') {
            ++format_cursor;
            if (*format_cursor == 'h') {
                length = GUEST_SCAN_LENGTH_HH;
                ++format_cursor;
            } else length = GUEST_SCAN_LENGTH_H;
        } else if (*format_cursor == 'l') {
            ++format_cursor;
            if (*format_cursor == 'l') {
                length = GUEST_SCAN_LENGTH_LL;
                ++format_cursor;
            } else length = GUEST_SCAN_LENGTH_L;
        } else if (*format_cursor == 'j') {
            length = GUEST_SCAN_LENGTH_J;
            ++format_cursor;
        } else if (*format_cursor == 'z') {
            length = GUEST_SCAN_LENGTH_Z;
            ++format_cursor;
        } else if (*format_cursor == 't') {
            length = GUEST_SCAN_LENGTH_T;
            ++format_cursor;
        } else if (*format_cursor == 'L') {
            length = GUEST_SCAN_LENGTH_CAPITAL_L;
            ++format_cursor;
        }
        specifier = *format_cursor;
        if (!specifier) break;
        ++format_cursor;

        if (specifier != 'c' && specifier != '[' && specifier != 'n')
            while (isspace((unsigned char)*input_cursor)) ++input_cursor;

        if (specifier == 'n') {
            if (!suppress) {
                uint32_t destination = cursor_word(arguments);
                if (!guest_scan_write_integer(
                        probe, destination,
                        (uint64_t)(size_t)(input_cursor - input_begin),
                        length))
                    break;
            }
            continue;
        }

        if (specifier == 'c' || specifier == 's' || specifier == '[') {
            const char *text_begin = input_cursor;
            size_t text_length = 0;
            size_t limit = has_width ? width : MAX_STRING - 1u;
            int terminate = specifier != 'c';
            int wide = length == GUEST_SCAN_LENGTH_L;

            if (specifier == 'c') {
                if (!has_width) limit = 1u;
                while (text_length < limit && input_cursor[text_length])
                    ++text_length;
                if (text_length != limit) {
                    input_failure = 1;
                    break;
                }
            } else if (specifier == 's') {
                while (text_length < limit && input_cursor[text_length] &&
                       !isspace((unsigned char)input_cursor[text_length]))
                    ++text_length;
                if (!text_length) {
                    if (!*input_cursor) input_failure = 1;
                    break;
                }
            } else {
                const char *set_begin = format_cursor;
                const char *set_end;
                int negate = 0;
                if (*set_begin == '^') {
                    negate = 1;
                    ++set_begin;
                }
                set_end = set_begin;
                if (*set_end == ']') ++set_end;
                while (*set_end && *set_end != ']') ++set_end;
                if (*set_end != ']') break;
                format_cursor = set_end + 1;
                while (text_length < limit && input_cursor[text_length]) {
                    int contains = guest_scan_set_contains(
                        (unsigned char)input_cursor[text_length], set_begin,
                        set_end);
                    if (contains == negate) break;
                    ++text_length;
                }
                if (!text_length) {
                    if (!*input_cursor) input_failure = 1;
                    break;
                }
            }
            if (!suppress) {
                uint32_t destination = cursor_word(arguments);
                if (!guest_scan_write_text(probe, destination, text_begin,
                                           text_length, terminate, wide))
                    break;
                ++assignments;
            }
            input_cursor += text_length;
            continue;
        }

        if (strchr("diouxXp", specifier) != NULL) {
            size_t available = strlen(input_cursor);
            size_t limit = has_width && width < available ? width : available;
            char *number;
            char *end = NULL;
            uint64_t value;
            int base;
            int signed_conversion = specifier == 'd' || specifier == 'i';
            if (!limit) {
                input_failure = 1;
                break;
            }
            number = (char *)malloc(limit + 1u);
            if (!number) break;
            memcpy(number, input_cursor, limit);
            number[limit] = 0;
            if (specifier == 'i') base = 0;
            else if (specifier == 'o') base = 8;
            else if (specifier == 'x' || specifier == 'X' || specifier == 'p')
                base = 16;
            else base = 10;
            errno = 0;
            if (signed_conversion)
                value = (uint64_t)strtoll(number, &end, base);
            else value = strtoull(number, &end, base);
            if (end == number) {
                free(number);
                break;
            }
            limit = (size_t)(end - number);
            free(number);
            if (!suppress) {
                uint32_t destination = cursor_word(arguments);
                GuestScanLength store_length = specifier == 'p'
                                                   ? GUEST_SCAN_LENGTH_DEFAULT
                                                   : length;
                if (!guest_scan_write_integer(probe, destination, value,
                                               store_length))
                    break;
                ++assignments;
            }
            input_cursor += limit;
            continue;
        }

        if (strchr("fFeEgGaA", specifier) != NULL) {
            size_t available = strlen(input_cursor);
            size_t limit = has_width && width < available ? width : available;
            char *number;
            char *end = NULL;
            double value;
            if (!limit) {
                input_failure = 1;
                break;
            }
            number = (char *)malloc(limit + 1u);
            if (!number) break;
            memcpy(number, input_cursor, limit);
            number[limit] = 0;
            errno = 0;
            value = strtod(number, &end);
            if (end == number) {
                free(number);
                break;
            }
            limit = (size_t)(end - number);
            free(number);
            if (!suppress) {
                uint32_t destination = cursor_word(arguments);
                if (!guest_scan_write_float(probe, destination, value, length))
                    break;
                ++assignments;
            }
            input_cursor += limit;
            continue;
        }

        break;
    }

    if (input_failure && assignments == 0) return -1;
    return assignments;
}

static uint32_t guest_format_to_memory(ArmProbe *probe, uint32_t destination,
                                       uint32_t capacity, uint32_t format_address,
                                       GuestArgCursor *arguments) {
    char *format;
    char *buffer;
    int result;
    GuestArgCursor measure_arguments;
    size_t host_capacity;
    format = guest_duplicate_cstring(probe, format_address, MAX_STRING, NULL);
    if (!format) return 0;
    measure_arguments = *arguments;
    result = guest_vformat(probe, NULL, 0, format, &measure_arguments);
    host_capacity = capacity ? (size_t)capacity : 1u;
    if (host_capacity > MAX_GUEST_FORMAT) host_capacity = MAX_GUEST_FORMAT;
    if (host_capacity > (size_t)result + 1u)
        host_capacity = (size_t)result + 1u;
    if (!host_capacity) host_capacity = 1u;
    buffer = (char *)calloc(1, host_capacity);
    if (!buffer) {
        free(format);
        return 0;
    }
    result = guest_vformat(probe, buffer, host_capacity, format, arguments);
    if (destination && capacity)
        uc_mem_write(probe->uc, destination, buffer,
                     strlen(buffer) + 1u <= host_capacity
                         ? strlen(buffer) + 1u : host_capacity);
    free(format);
    free(buffer);
    return (uint32_t)result;
}

static uint32_t guest_new_class(ArmProbe *probe, const char *name) {
    const char *class_name = name ? name : "?";
    GuestRef *reference;
    unsigned index;
    for (index = 0; index < probe->ref_count; ++index) {
        reference = &probe->refs[index];
        if (reference->kind == GREF_CLASS && reference->class_name &&
            strcmp(reference->class_name, class_name) == 0)
            return reference->handle;
    }
    reference = guest_new_ref(probe, GREF_CLASS);
    if (!reference) return 0;
    reference->class_name = copy_string(class_name);
    probe_log("JNI FindClass: %s", name ? name : "<null>");
    return reference->handle;
}

static uint32_t guest_new_method(ArmProbe *probe, uint32_t class_handle,
                                 const char *name, const char *signature) {
    GuestRef *class_reference = guest_ref(probe, class_handle);
    const char *class_name = class_reference && class_reference->class_name
                                 ? class_reference->class_name : "?";
    const char *method_name = name ? name : "?";
    const char *method_signature = signature ? signature : "?";
    GuestRef *method;
    unsigned index;
    for (index = 0; index < probe->ref_count; ++index) {
        method = &probe->refs[index];
        if (method->kind == GREF_METHOD && method->class_name && method->name &&
            method->signature && strcmp(method->class_name, class_name) == 0 &&
            strcmp(method->name, method_name) == 0 &&
            strcmp(method->signature, method_signature) == 0)
            return method->handle;
    }
    method = guest_new_ref(probe, GREF_METHOD);
    if (!method) return 0;
    method->class_name = copy_string(class_name);
    method->name = copy_string(method_name);
    method->signature = copy_string(method_signature);
    probe_log("JNI method: %s.%s %s", method->class_name, method->name,
              method->signature);
    return method->handle;
}

static void guest_method_first_call(GuestRef *method) {
    if (method && method->calls++ == 0)
        probe_log("JNI call: %s.%s %s",
                  method->class_name ? method->class_name : "?",
                  method->name ? method->name : "?",
                  method->signature ? method->signature : "?");
}

static void open_external_target(const char *target) {
    char translated[2048];
    const char *open_target = target;
    if (!target || !target[0]) return;
    if (strncmp(target, "market://details?id=", 20) == 0) {
        snprintf(translated, sizeof(translated),
                 "https://play.google.com/store/apps/details?id=%s", target + 20);
        open_target = translated;
    }
    if (strncmp(open_target, "http://", 7) != 0 &&
        strncmp(open_target, "https://", 8) != 0 &&
        strncmp(open_target, "mailto:", 7) != 0) return;
    ShellExecuteA(NULL, "open", open_target, NULL, NULL, SW_SHOWNORMAL);
}

static uint32_t jni_dispatch_object(ArmProbe *probe, GuestRef *method,
                                    GuestArgCursor *arguments) {
    const char *name = method && method->name ? method->name : "";
    char first[MAX_STRING], second[MAX_STRING];
    guest_method_first_call(method);
    if (strcmp(name, "getCocos2dxPackageName") == 0 ||
        strcmp(name, "getPackageName") == 0)
        return guest_new_string_ref(probe, "com.robtopx.geometryjump");
    if (strcmp(name, "getCocos2dxWritablePath") == 0)
        return guest_new_string_ref(probe, "/save");
    if (strcmp(name, "getCurrentLanguage") == 0)
        return guest_new_string_ref(probe, "en");
    if (strcmp(name, "getDeviceModel") == 0)
        return guest_new_string_ref(probe, "Windows ARM Wrapper");
    if (strcmp(name, "getUserID") == 0)
        return guest_new_string_ref(probe, "57494e41524d3031");
    if (strcmp(name, "getStringForKey") == 0) {
        uint32_t key = cursor_word(arguments);
        uint32_t fallback = cursor_word(arguments);
        char *value = storage_get_string_copy(
            guest_ref_string(probe, key, first, sizeof(first)),
            guest_ref_string(probe, fallback, second, sizeof(second)));
        uint32_t result = guest_new_string_ref(probe, value ? value : "");
        free(value);
        return result;
    }
    if (strcmp(name, "getStringWithEllipsis") == 0)
        return cursor_word(arguments);
    if (strcmp(name, "loadAndDecryptFileToString") == 0) {
        uint32_t path = cursor_word(arguments);
        char *value = storage_read_game_file(
            guest_ref_string(probe, path, first, sizeof(first)), NULL);
        uint32_t result = guest_new_string_ref(probe, value ? value : "");
        free(value);
        return result;
    }
    if (strcmp(name, "getItem") == 0)
        return guest_new_string_ref(probe, "");
    {
        GuestRef *object = guest_new_ref(probe, GREF_OBJECT);
        return object ? object->handle : 0;
    }
}

static uint32_t jni_dispatch_boolean(ArmProbe *probe, GuestRef *method,
                                     GuestArgCursor *arguments) {
    const char *name = method && method->name ? method->name : "";
    char key[MAX_STRING], path[MAX_STRING];
    guest_method_first_call(method);
    if (strcmp(name, "getBoolForKey") == 0)
        return (uint32_t)storage_get_bool(
            guest_ref_string(probe, cursor_word(arguments), key, sizeof(key)),
            (int)cursor_word(arguments));
    if (strcmp(name, "shouldResumeSound") == 0 ||
        strcmp(name, "isNetworkAvailable") == 0) return 1;
    if (strcmp(name, "isBackgroundMusicPlaying") == 0) {
        if (probe->disable_audio) {
            ++probe->overkill_audio_skips;
            return 1;
        }
        return (uint32_t)audio_is_background_playing();
    }
    if (strcmp(name, "doesFileExist") == 0)
        return (uint32_t)storage_file_exists(
            guest_ref_string(probe, cursor_word(arguments), path, sizeof(path)));
    return 0;
}

static uint32_t jni_dispatch_int(ArmProbe *probe, GuestRef *method,
                                 GuestArgCursor *arguments) {
    const char *name = method && method->name ? method->name : "";
    char key[MAX_STRING], path[MAX_STRING];
    guest_method_first_call(method);
    if (strcmp(name, "getDPI") == 0) return 96;
    if (strcmp(name, "getIntegerForKey") == 0)
        return (uint32_t)storage_get_integer(
            guest_ref_string(probe, cursor_word(arguments), key, sizeof(key)),
            (int32_t)cursor_word(arguments));
    if (strcmp(name, "getFontSizeAccordingHeight") == 0)
        return cursor_word(arguments);
    if (strcmp(name, "playEffect") == 0) {
        uint32_t path_ref = cursor_word(arguments);
        int loop = (int)cursor_word(arguments);
        if (probe->disable_audio) {
            (void)path_ref;
            (void)loop;
            ++probe->overkill_audio_skips;
            if (++probe->overkill_fake_audio_id == 0)
                probe->overkill_fake_audio_id = 1;
            return probe->overkill_fake_audio_id;
        }
        return audio_play_effect(
            guest_ref_string(probe, path_ref, path, sizeof(path)), loop);
    }
    return 0;
}

static uint32_t jni_dispatch_float(ArmProbe *probe, GuestRef *method,
                                   GuestArgCursor *arguments) {
    const char *name = method && method->name ? method->name : "";
    char key[MAX_STRING];
    float result = 0.0f;
    guest_method_first_call(method);
    if (strcmp(name, "getFloatForKey") == 0) {
        uint32_t key_ref = cursor_word(arguments);
        float fallback = cursor_float_argument(arguments);
        result = storage_get_float(
            guest_ref_string(probe, key_ref, key, sizeof(key)), fallback);
    } else if (strcmp(name, "getBackgroundMusicVolume") == 0)
        result = probe->disable_audio ? 0.0f : audio_get_background_volume();
    else if (strcmp(name, "getBackgroundMusicTime") == 0)
        result = probe->disable_audio ? 0.0f : audio_get_background_time();
    else if (strcmp(name, "getEffectsVolume") == 0)
        result = probe->disable_audio ? 0.0f : audio_get_effects_volume();
    return float_bits(result);
}

static uint64_t jni_dispatch_double(ArmProbe *probe, GuestRef *method,
                                    GuestArgCursor *arguments) {
    const char *name = method && method->name ? method->name : "";
    char key[MAX_STRING];
    double result = 0.0;
    guest_method_first_call(method);
    if (strcmp(name, "getDoubleForKey") == 0) {
        uint32_t key_ref = cursor_word(arguments);
        uint64_t fallback_bits = cursor_u64(arguments);
        result = storage_get_double(
            guest_ref_string(probe, key_ref, key, sizeof(key)),
            bits_double((uint32_t)fallback_bits, (uint32_t)(fallback_bits >> 32)));
    }
    return double_bits(result);
}

static int overkill_is_audio_void_method(const char *name) {
    static const char *const methods[] = {
        "playBackgroundMusic", "preloadBackgroundMusic",
        "stopBackgroundMusic", "pauseBackgroundMusic",
        "resumeBackgroundMusic", "rewindBackgroundMusic",
        "setBackgroundMusicTime", "setBackgroundMusicVolume",
        "preloadEffect", "pauseEffect", "resumeEffect", "stopEffect",
        "pauseAllEffects", "resumeAllEffects", "stopAllEffects",
        "unloadEffect", "setEffectsVolume", "end"
    };
    unsigned index;
    for (index = 0; index < sizeof(methods) / sizeof(methods[0]); ++index)
        if (strcmp(name, methods[index]) == 0) return 1;
    return 0;
}

static void jni_dispatch_void(ArmProbe *probe, GuestRef *method,
                              GuestArgCursor *arguments) {
    const char *name = method && method->name ? method->name : "";
    char first[MAX_STRING], second[MAX_STRING];
    guest_method_first_call(method);
    if (probe->disable_audio && overkill_is_audio_void_method(name)) {
        ++probe->overkill_audio_skips;
        return;
    }
    if (strcmp(name, "setAnimationInterval") == 0) {
        uint64_t bits = cursor_u64(arguments);
        double interval = bits_double((uint32_t)bits, (uint32_t)(bits >> 32));
        if (interval > 0.001 && interval < 1.0) probe->frame_interval = interval;
    } else if (strcmp(name, "setStringForKey") == 0) {
        uint32_t key = cursor_word(arguments), value = cursor_word(arguments);
        storage_set_string(guest_ref_string(probe, key, first, sizeof(first)),
                           guest_ref_string(probe, value, second, sizeof(second)));
    } else if (strcmp(name, "setBoolForKey") == 0) {
        uint32_t key = cursor_word(arguments);
        storage_set_bool(guest_ref_string(probe, key, first, sizeof(first)),
                         (int)cursor_word(arguments));
    } else if (strcmp(name, "setIntegerForKey") == 0) {
        uint32_t key = cursor_word(arguments);
        storage_set_integer(guest_ref_string(probe, key, first, sizeof(first)),
                            (int32_t)cursor_word(arguments));
    } else if (strcmp(name, "setFloatForKey") == 0) {
        uint32_t key = cursor_word(arguments);
        storage_set_float(guest_ref_string(probe, key, first, sizeof(first)),
                          cursor_float_argument(arguments));
    } else if (strcmp(name, "setDoubleForKey") == 0) {
        uint32_t key = cursor_word(arguments);
        uint64_t bits = cursor_u64(arguments);
        storage_set_double(guest_ref_string(probe, key, first, sizeof(first)),
                           bits_double((uint32_t)bits, (uint32_t)(bits >> 32)));
    } else if (strcmp(name, "saveAndEncryptStringToFile") == 0) {
        uint32_t value = cursor_word(arguments), path = cursor_word(arguments);
        GuestRef *value_ref = guest_ref(probe, value);
        guest_ref_string(probe, value, first, sizeof(first));
        storage_write_game_file(
            guest_ref_string(probe, path, second, sizeof(second)), first,
            value_ref ? value_ref->length : strlen(first));
    } else if (strcmp(name, "openURL") == 0) {
        open_external_target(guest_ref_string(
            probe, cursor_word(arguments), first, sizeof(first)));
    } else if (strcmp(name, "openAppPage") == 0) {
        open_external_target("https://play.google.com/store/apps/details?id=com.robtopx.geometryjump");
    } else if (strcmp(name, "terminateProcess") == 0) {
        PostQuitMessage(0);
    } else if (strcmp(name, "openIMEKeyboard") == 0 ||
               strcmp(name, "showEditTextDialog") == 0) {
        probe->text_input_active = 1;
    } else if (strcmp(name, "closeIMEKeyboard") == 0) {
        probe->text_input_active = 0;
    } else if (strcmp(name, "setKeyboardState") == 0) {
        probe->text_input_active = cursor_word(arguments) != 0;
    } else if (strcmp(name, "showMessageBox") == 0) {
        uint32_t first_ref = cursor_word(arguments);
        uint32_t second_ref = cursor_word(arguments);
        probe_log("JNI message box: %s | %s",
                  guest_ref_string(probe, first_ref, first, sizeof(first)),
                  guest_ref_string(probe, second_ref, second, sizeof(second)));
    } else if (strcmp(name, "playBackgroundMusic") == 0) {
        uint32_t path = cursor_word(arguments);
        audio_play_background(guest_ref_string(probe, path, first, sizeof(first)),
                              (int)cursor_word(arguments));
    } else if (strcmp(name, "preloadBackgroundMusic") == 0) {
        audio_preload_background(guest_ref_string(
            probe, cursor_word(arguments), first, sizeof(first)));
    } else if (strcmp(name, "stopBackgroundMusic") == 0) audio_stop_background();
    else if (strcmp(name, "pauseBackgroundMusic") == 0) audio_pause_background();
    else if (strcmp(name, "resumeBackgroundMusic") == 0) audio_resume_background();
    else if (strcmp(name, "rewindBackgroundMusic") == 0) audio_rewind_background();
    else if (strcmp(name, "setBackgroundMusicTime") == 0) {
        uint64_t bits = cursor_u64(arguments);
        audio_set_background_time((float)bits_double((uint32_t)bits,
                                                      (uint32_t)(bits >> 32)));
    } else if (strcmp(name, "setBackgroundMusicVolume") == 0) {
        uint64_t bits = cursor_u64(arguments);
        audio_set_background_volume((float)bits_double((uint32_t)bits,
                                                        (uint32_t)(bits >> 32)));
    } else if (strcmp(name, "preloadEffect") == 0) {
        audio_preload_effect(guest_ref_string(
            probe, cursor_word(arguments), first, sizeof(first)));
    } else if (strcmp(name, "pauseEffect") == 0) audio_pause_effect(cursor_word(arguments));
    else if (strcmp(name, "resumeEffect") == 0) audio_resume_effect(cursor_word(arguments));
    else if (strcmp(name, "stopEffect") == 0) audio_stop_effect(cursor_word(arguments));
    else if (strcmp(name, "pauseAllEffects") == 0) audio_pause_all_effects();
    else if (strcmp(name, "resumeAllEffects") == 0) audio_resume_all_effects();
    else if (strcmp(name, "stopAllEffects") == 0) audio_stop_all_effects();
    else if (strcmp(name, "unloadEffect") == 0)
        audio_unload_effect(guest_ref_string(
            probe, cursor_word(arguments), first, sizeof(first)));
    else if (strcmp(name, "setEffectsVolume") == 0) {
        audio_set_effects_volume(cursor_float_argument(arguments));
    } else if (strcmp(name, "end") == 0) audio_shutdown();
}

static int jni_is_v_index(unsigned index) {
    switch (index) {
    case 35: case 38: case 50: case 56: case 59: case 62:
    case 115: case 118: case 130: case 136: case 139: case 142:
        return 1;
    default: return 0;
    }
}

static int jni_is_a_index(unsigned index) {
    switch (index) {
    case 36: case 39: case 51: case 57: case 60: case 63:
    case 116: case 119: case 131: case 137: case 140: case 143:
        return 1;
    default: return 0;
    }
}

static void dispatch_jni(ArmProbe *probe, unsigned index,
                         uint32_t r0, uint32_t r1, uint32_t r2,
                         uint32_t r3, uint32_t sp) {
    char first[MAX_STRING], second[MAX_STRING];
    GuestArgCursor arguments;
    GuestRef *method;
    uint32_t result = 0;
    cursor_initialize(&arguments, probe, r0, r1, r2, r3, sp,
                      jni_is_a_index(index) ? 2 : jni_is_v_index(index));
    switch (index) {
    case 4: result = JNI_VERSION_1_4; break;
    case 6:
        if (guest_read_string(probe, r1, first, sizeof(first)))
            result = guest_new_class(probe, first);
        break;
    case 15: case 17: case 228: result = 0; break;
    case 21: case 25: result = r1; break;
    case 22: case 23: result = 0; break;
    case 24: result = r1 == r2; break;
    case 28: case 29: case 30: {
        GuestRef *object = guest_new_ref(probe, GREF_OBJECT);
        GuestRef *class_reference = guest_ref(probe, r1);
        if (object && class_reference && class_reference->class_name)
            object->class_name = copy_string(class_reference->class_name);
        result = object ? object->handle : 0;
        break;
    }
    case 31: {
        GuestRef *object = guest_ref(probe, r1);
        result = guest_new_class(probe, object && object->class_name
                                         ? object->class_name : "java/lang/Object");
        break;
    }
    case 32: result = 1; break;
    case 33: case 113:
        if (guest_read_string(probe, r2, first, sizeof(first)) &&
            guest_read_string(probe, r3, second, sizeof(second)))
            result = guest_new_method(probe, r1, first, second);
        break;
    case 34: case 35: case 36: case 114: case 115: case 116:
        method = guest_ref(probe, r2);
        result = jni_dispatch_object(probe, method, &arguments);
        break;
    case 37: case 38: case 39: case 117: case 118: case 119:
        method = guest_ref(probe, r2);
        result = jni_dispatch_boolean(probe, method, &arguments);
        break;
    case 49: case 50: case 51: case 129: case 130: case 131:
        method = guest_ref(probe, r2);
        result = jni_dispatch_int(probe, method, &arguments);
        break;
    case 55: case 56: case 57: case 135: case 136: case 137:
        method = guest_ref(probe, r2);
        result = jni_dispatch_float(probe, method, &arguments);
        break;
    case 58: case 59: case 60: case 138: case 139: case 140: {
        uint64_t value;
        method = guest_ref(probe, r2);
        value = jni_dispatch_double(probe, method, &arguments);
        set_r0_r1_u64(probe->uc, value);
        return;
    }
    case 61: case 62: case 63: case 141: case 142: case 143:
        method = guest_ref(probe, r2);
        jni_dispatch_void(probe, method, &arguments);
        result = 0;
        break;
    case 167:
        if (guest_read_string(probe, r1, first, sizeof(first)))
            result = guest_new_string_ref(probe, first);
        break;
    case 168: {
        GuestRef *reference = guest_ref(probe, r1);
        result = reference && reference->kind == GREF_STRING ? reference->length : 0;
        break;
    }
    case 169: {
        GuestRef *reference = guest_ref(probe, r1);
        if (r2) { unsigned char zero = 0; uc_mem_write(probe->uc, r2, &zero, 1); }
        result = reference && reference->kind == GREF_STRING
                     ? reference->data_address : 0;
        break;
    }
    case 170: result = 0; break;
    case 171: {
        GuestRef *reference = guest_ref(probe, r1);
        result = reference ? reference->length : 0;
        break;
    }
    case 176: result = guest_new_array_ref(probe, GREF_BYTE_ARRAY, r1, 1); break;
    case 179: result = guest_new_array_ref(probe, GREF_INT_ARRAY, r1, 4); break;
    case 181: result = guest_new_array_ref(probe, GREF_FLOAT_ARRAY, r1, 4); break;
    case 184: case 187: case 189: {
        GuestRef *reference = guest_ref(probe, r1);
        if (r2) { unsigned char zero = 0; uc_mem_write(probe->uc, r2, &zero, 1); }
        result = reference ? reference->data_address : 0;
        break;
    }
    case 192: case 195: case 197: result = 0; break;
    case 200: case 203: case 205: {
        GuestRef *reference = guest_ref(probe, r1);
        uint32_t element_size = index == 200 ? 1u : 4u;
        uint32_t buffer = 0;
        uc_mem_read(probe->uc, sp, &buffer, sizeof(buffer));
        if (reference && buffer && r2 <= reference->length &&
            r3 <= reference->length - r2)
            guest_copy_memory(probe, buffer,
                              reference->data_address + r2 * element_size,
                              r3 * element_size);
        break;
    }
    case 208: case 211: case 213: {
        GuestRef *reference = guest_ref(probe, r1);
        uint32_t element_size = index == 208 ? 1u : 4u;
        uint32_t buffer = 0;
        uc_mem_read(probe->uc, sp, &buffer, sizeof(buffer));
        if (reference && buffer && r2 <= reference->length &&
            r3 <= reference->length - r2)
            guest_copy_memory(probe,
                              reference->data_address + r2 * element_size,
                              buffer, r3 * element_size);
        break;
    }
    case 215: {
        GuestRef *class_reference = guest_ref(probe, r1);
        int count = (int)r3;
        int item;
        for (item = 0; item < count && probe->native_count < MAX_REGISTERED_NATIVES;
             ++item) {
            uint32_t fields[3] = {0, 0, 0};
            RegisteredNative *native = &probe->natives[probe->native_count];
            if (uc_mem_read(probe->uc, r2 + item * 12u, fields, sizeof(fields)) != UC_ERR_OK)
                break;
            if (!guest_read_string(probe, fields[0], first, sizeof(first)) ||
                !guest_read_string(probe, fields[1], second, sizeof(second))) continue;
            native->class_name = copy_string(class_reference && class_reference->class_name
                                                  ? class_reference->class_name : "?");
            native->name = copy_string(first);
            native->signature = copy_string(second);
            native->function = fields[2];
            ++probe->native_count;
            probe_log("JNI RegisterNatives: %s.%s %s -> 0x%08x",
                      native->class_name, native->name, native->signature,
                      native->function);
        }
        result = 0;
        break;
    }
    case 216: result = 0; break;
    case 219:
        if (r1) uc_mem_write(probe->uc, r1, &(uint32_t){GUEST_VM_OBJECT}, 4);
        result = 0;
        break;
    default:
        probe_log("JNI unimplemented table slot %u", index);
        result = 0;
        break;
    }
    set_r0(probe->uc, result);
}

static void jni_hook(uc_engine *uc, uint64_t address, uint32_t size,
                     void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0, sp = 0;
    unsigned index = (unsigned)((address - GUEST_JNI_TRAPS) / 4u);
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    uc_reg_read(uc, UC_ARM_REG_R2, &r2);
    uc_reg_read(uc, UC_ARM_REG_R3, &r3);
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    dispatch_jni(probe, index, r0, r1, r2, r3, sp);
}

static void vm_hook(uc_engine *uc, uint64_t address, uint32_t size,
                    void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t r0 = 0, r1 = 0, r2 = 0;
    unsigned index = (unsigned)((address - GUEST_VM_TRAPS) / 4u);
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    uc_reg_read(uc, UC_ARM_REG_R2, &r2);
    (void)r0;
    if (index == 4 || index == 7) {
        if (r1) uc_mem_write(probe->uc, r1, &(uint32_t){GUEST_ENV_OBJECT}, 4);
        set_r0(uc, 0);
    } else if (index == 6) {
        if (r1 && r2 <= JNI_VERSION_1_4)
            uc_mem_write(probe->uc, r1, &(uint32_t){GUEST_ENV_OBJECT}, 4);
        set_r0(uc, r2 > JNI_VERSION_1_4 ? (uint32_t)-2 : 0);
    } else {
        set_r0(uc, 0);
    }
}

static int initialize_guest_jni(ArmProbe *probe) {
    uint32_t env_entries[JNI_TABLE_SIZE];
    uint32_t vm_entries[8];
    unsigned char *stubs;
    unsigned index;
    uint32_t table;
    for (index = 0; index < JNI_TABLE_SIZE; ++index)
        env_entries[index] = GUEST_JNI_TRAPS + index * 4u + 1u;
    for (index = 0; index < 8; ++index)
        vm_entries[index] = GUEST_VM_TRAPS + index * 4u + 1u;
    stubs = (unsigned char *)calloc(1, JNI_TABLE_SIZE * 4u);
    if (!stubs) return 0;
    for (index = 0; index < JNI_TABLE_SIZE; ++index) {
        stubs[index * 4u] = 0x70; stubs[index * 4u + 1u] = 0x47;
        stubs[index * 4u + 2u] = 0xc0; stubs[index * 4u + 3u] = 0x46;
    }
    if (uc_mem_write(probe->uc, GUEST_ENV_TABLE, env_entries,
                     sizeof(env_entries)) != UC_ERR_OK ||
        uc_mem_write(probe->uc, GUEST_JNI_TRAPS, stubs,
                     JNI_TABLE_SIZE * 4u) != UC_ERR_OK) {
        free(stubs); return 0;
    }
    free(stubs);
    stubs = (unsigned char *)calloc(1, sizeof(vm_entries));
    if (!stubs) return 0;
    for (index = 0; index < 8; ++index) {
        stubs[index * 4u] = 0x70; stubs[index * 4u + 1u] = 0x47;
        stubs[index * 4u + 2u] = 0xc0; stubs[index * 4u + 3u] = 0x46;
    }
    if (uc_mem_write(probe->uc, GUEST_VM_TABLE, vm_entries,
                     sizeof(vm_entries)) != UC_ERR_OK ||
        uc_mem_write(probe->uc, GUEST_VM_TRAPS, stubs,
                     sizeof(vm_entries)) != UC_ERR_OK) {
        free(stubs); return 0;
    }
    free(stubs);
    table = GUEST_ENV_TABLE;
    uc_mem_write(probe->uc, GUEST_ENV_OBJECT, &table, 4);
    table = GUEST_VM_TABLE;
    uc_mem_write(probe->uc, GUEST_VM_OBJECT, &table, 4);
    return 1;
}

static int guest_copy_memory(ArmProbe *probe, uint32_t destination,
                             uint32_t source, uint32_t size) {
    GuestMemoryRegion *source_region;
    GuestMemoryRegion *destination_region;
    unsigned char *source_pointer;
    unsigned char *destination_pointer;
    unsigned char *temporary;
    if (!size) return 1;
    if (size > 64u * 1024u * 1024u) return 0;
    source_region = guest_memory_region(probe, source, size);
    destination_region = guest_memory_region(probe, destination, size);
    source_pointer = source_region
                         ? source_region->host + (size_t)(source - source_region->base)
                         : NULL;
    destination_pointer = destination_region && destination_region->direct_write
                              ? destination_region->host +
                                    (size_t)(destination - destination_region->base)
                              : NULL;
    if (source_pointer && destination_pointer) {
        memmove(destination_pointer, source_pointer, size);
        ++probe->direct_memory_reads;
        ++probe->direct_memory_writes;
        return 1;
    }
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

static int guest_compare_memory(ArmProbe *probe, uint32_t first_address,
                                uint32_t second_address, uint32_t size,
                                int *comparison) {
    GuestMemoryRegion *first_region;
    GuestMemoryRegion *second_region;
    unsigned char *first_pointer;
    unsigned char *second_pointer;
    unsigned char *first_copy;
    unsigned char *second_copy;
    if (comparison) *comparison = 0;
    if (!size) return 1;
    first_region = guest_memory_region(probe, first_address, size);
    second_region = guest_memory_region(probe, second_address, size);
    first_pointer = first_region
                        ? first_region->host +
                              (size_t)(first_address - first_region->base)
                        : NULL;
    second_pointer = second_region
                         ? second_region->host +
                               (size_t)(second_address - second_region->base)
                         : NULL;
    if (first_pointer && second_pointer) {
        if (comparison) *comparison = memcmp(first_pointer, second_pointer, size);
        probe->direct_memory_reads += 2u;
        return 1;
    }
    first_copy = (unsigned char *)malloc(size);
    second_copy = (unsigned char *)malloc(size);
    if (!first_copy || !second_copy ||
        uc_mem_read(probe->uc, first_address, first_copy, size) != UC_ERR_OK ||
        uc_mem_read(probe->uc, second_address, second_copy, size) != UC_ERR_OK) {
        free(first_copy);
        free(second_copy);
        return 0;
    }
    if (comparison) *comparison = memcmp(first_copy, second_copy, size);
    free(first_copy);
    free(second_copy);
    return 1;
}

static uint32_t guest_find_byte(ArmProbe *probe, uint32_t address,
                                unsigned char value, uint32_t size,
                                int reverse) {
    unsigned char block[4096];
    if (!size) return 0;
    if (!reverse) {
        uint32_t offset = 0;
        while (offset < size) {
            uint32_t part = size - offset;
            unsigned char *found;
            if (part > sizeof(block)) part = sizeof(block);
            if ((uint64_t)address + offset + part > (uint64_t)UINT32_MAX + 1u ||
                uc_mem_read(probe->uc, address + offset, block, part) != UC_ERR_OK)
                return 0;
            found = (unsigned char *)memchr(block, value, part);
            if (found)
                return address + offset + (uint32_t)(found - block);
            offset += part;
        }
    } else {
        uint32_t remaining = size;
        while (remaining) {
            uint32_t part = remaining;
            uint32_t offset;
            uint32_t index;
            if (part > sizeof(block)) part = sizeof(block);
            offset = remaining - part;
            if ((uint64_t)address + offset + part > (uint64_t)UINT32_MAX + 1u ||
                uc_mem_read(probe->uc, address + offset, block, part) != UC_ERR_OK)
                return 0;
            for (index = part; index > 0; --index)
                if (block[index - 1u] == value)
                    return address + offset + index - 1u;
            remaining = offset;
        }
    }
    return 0;
}

static uint32_t guest_find_wide_character(ArmProbe *probe, uint32_t address,
                                          uint32_t value, uint32_t count) {
    uint32_t block[1024];
    uint32_t offset = 0;
    while (offset < count) {
        uint32_t part = count - offset;
        uint32_t index;
        uint64_t byte_offset = (uint64_t)offset * sizeof(uint32_t);
        uint64_t bytes;
        if (part > sizeof(block) / sizeof(block[0]))
            part = sizeof(block) / sizeof(block[0]);
        bytes = (uint64_t)part * sizeof(uint32_t);
        if ((uint64_t)address + byte_offset + bytes >
                (uint64_t)UINT32_MAX + 1u ||
            uc_mem_read(probe->uc, address + (uint32_t)byte_offset,
                        block, (size_t)bytes) != UC_ERR_OK)
            return 0;
        for (index = 0; index < part; ++index)
            if (block[index] == value)
                return address + (offset + index) * sizeof(uint32_t);
        offset += part;
    }
    return 0;
}

static uint32_t import_argument(ArmProbe *probe, uint32_t r0, uint32_t r1,
                                uint32_t r2, uint32_t r3, uint32_t sp,
                                unsigned index) {
    uint32_t registers[4] = {r0, r1, r2, r3};
    uint32_t value = 0;
    if (index < 4) return registers[index];
    uc_mem_read(probe->uc, sp + (index - 4u) * 4u, &value, sizeof(value));
    return value;
}

static PROC resolve_gl(ArmProbe *probe, const char *name) {
    PROC function = NULL;
    if (!probe->host.opengl) probe->host.opengl = LoadLibraryA("opengl32.dll");
    if (probe->host.opengl)
        function = GetProcAddress(probe->host.opengl, name);
    if (!function && probe->host.context)
        function = wglGetProcAddress(name);
    return function;
}

static size_t gl_get_value_count(ArmProbe *probe, uint32_t parameter) {
    switch (parameter) {
    case 0x0ba2u: /* GL_VIEWPORT */
    case 0x0c10u: /* GL_SCISSOR_BOX */
    case 0x0c22u: /* GL_COLOR_CLEAR_VALUE */
    case 0x0c23u: /* GL_COLOR_WRITEMASK */
    case 0x8005u: /* GL_BLEND_COLOR */
        return 4u;
    case 0x0b70u: /* GL_DEPTH_RANGE */
    case 0x0d3au: /* GL_MAX_VIEWPORT_DIMS */
    case 0x846du: /* GL_ALIASED_POINT_SIZE_RANGE */
    case 0x846eu: /* GL_ALIASED_LINE_WIDTH_RANGE */
        return 2u;
    case 0x86a3u: /* GL_COMPRESSED_TEXTURE_FORMATS */
    case 0x8df8u: { /* GL_SHADER_BINARY_FORMATS */
        if (probe && probe->headless_gl) return 1u;
        typedef void (WINAPI *GetIntegerFunction)(uint32_t, int *);
        GetIntegerFunction getter =
            (GetIntegerFunction)resolve_gl(probe, "glGetIntegerv");
        int count = 0;
        uint32_t count_parameter = parameter == 0x86a3u ? 0x86a2u : 0x8df9u;
        if (getter) getter(count_parameter, &count);
        if (count > 0 && count <= 4096) return (size_t)count;
        return 1u;
    }
    default: return 1u;
    }
}

typedef uint32_t (WINAPI *GlCall0)(void);
typedef uint32_t (WINAPI *GlCall1)(uint32_t);
typedef uint32_t (WINAPI *GlCall2)(uint32_t,uint32_t);
typedef uint32_t (WINAPI *GlCall3)(uint32_t,uint32_t,uint32_t);
typedef uint32_t (WINAPI *GlCall4)(uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint32_t (WINAPI *GlCall5)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint32_t (WINAPI *GlCall6)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint32_t (WINAPI *GlCall7)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint32_t (WINAPI *GlCall8)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint32_t (WINAPI *GlCall9)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);

static uint32_t call_gl_raw(PROC function, const uint32_t *arguments,
                            unsigned count) {
    if (!function) return 0;
    switch (count) {
    case 0: return ((GlCall0)function)();
    case 1: return ((GlCall1)function)(arguments[0]);
    case 2: return ((GlCall2)function)(arguments[0],arguments[1]);
    case 3: return ((GlCall3)function)(arguments[0],arguments[1],arguments[2]);
    case 4: return ((GlCall4)function)(arguments[0],arguments[1],arguments[2],arguments[3]);
    case 5: return ((GlCall5)function)(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4]);
    case 6: return ((GlCall6)function)(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5]);
    case 7: return ((GlCall7)function)(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],arguments[6]);
    case 8: return ((GlCall8)function)(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],arguments[6],arguments[7]);
    case 9: return ((GlCall9)function)(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],arguments[6],arguments[7],arguments[8]);
    default: return 0;
    }
}

typedef struct { const char *name; unsigned count; } GlDescriptor;
static const GlDescriptor g_gl_descriptors[] = {
    {"glActiveTexture",1},{"glAttachShader",2},{"glBindAttribLocation",3},
    {"glBindBuffer",2},{"glBindFramebuffer",2},{"glBindRenderbuffer",2},
    {"glBindTexture",2},{"glBlendColor",4},{"glBlendEquation",1},
    {"glBlendEquationSeparate",2},{"glBlendFunc",2},{"glBlendFuncSeparate",4},
    {"glBufferData",4},{"glBufferSubData",4},{"glCheckFramebufferStatus",1},
    {"glClear",1},{"glClearColor",4},{"glClearDepthf",1},{"glClearStencil",1},{"glColorMask",4},
    {"glCompileShader",1},{"glCompressedTexImage2D",8},
    {"glCompressedTexSubImage2D",9},{"glCopyTexImage2D",8},
    {"glCopyTexSubImage2D",8},{"glCreateProgram",0},{"glCreateShader",1},
    {"glCullFace",1},{"glDeleteBuffers",2},{"glDeleteFramebuffers",2},
    {"glDeleteProgram",1},{"glDeleteRenderbuffers",2},{"glDeleteShader",1},
    {"glDeleteTextures",2},{"glDepthFunc",1},{"glDepthMask",1},
    {"glDepthRangef",2},{"glDetachShader",2},{"glDisable",1},
    {"glDisableVertexAttribArray",1},{"glDrawArrays",3},{"glDrawElements",4},
    {"glEnable",1},{"glEnableVertexAttribArray",1},{"glFinish",0},{"glFlush",0},
    {"glFramebufferRenderbuffer",4},{"glFramebufferTexture2D",5},
    {"glFrontFace",1},{"glGenBuffers",2},{"glGenerateMipmap",1},
    {"glGenFramebuffers",2},{"glGenRenderbuffers",2},{"glGenTextures",2},
    {"glGetAttribLocation",2},{"glGetBooleanv",2},{"glGetError",0},
    {"glGetFloatv",2},{"glGetIntegerv",2},{"glGetProgramInfoLog",4},
    {"glGetProgramiv",3},{"glGetShaderInfoLog",4},{"glGetShaderiv",3},
    {"glGetShaderSource",4},{"glGetString",1},{"glGetUniformLocation",2},
    {"glHint",2},{"glIsBuffer",1},{"glIsEnabled",1},{"glIsFramebuffer",1},
    {"glIsProgram",1},{"glIsRenderbuffer",1},{"glIsShader",1},{"glIsTexture",1},
    {"glLineWidth",1},{"glLinkProgram",1},{"glPixelStorei",2},
    {"glPolygonOffset",2},{"glReadPixels",7},{"glRenderbufferStorage",4},
    {"glSampleCoverage",2},{"glScissor",4},{"glStencilFunc",3},
    {"glStencilFuncSeparate",4},{"glStencilMask",1},{"glStencilMaskSeparate",2},
    {"glStencilOp",3},{"glStencilOpSeparate",4},{"glShaderSource",4},
    {"glTexImage2D",9},
    {"glTexParameteri",3},{"glTexParameterf",3},{"glTexSubImage2D",9},
    {"glUniform1f",2},{"glUniform1fv",3},{"glUniform1i",2},{"glUniform1iv",3},
    {"glUniform2f",3},{"glUniform2fv",3},{"glUniform2i",3},{"glUniform2iv",3},
    {"glUniform3f",4},{"glUniform3fv",3},{"glUniform3i",4},{"glUniform3iv",3},
    {"glUniform4f",5},{"glUniform4fv",3},{"glUniform4i",5},{"glUniform4iv",3},
    {"glUniformMatrix2fv",4},{"glUniformMatrix3fv",4},{"glUniformMatrix4fv",4},
    {"glUseProgram",1},{"glValidateProgram",1},{"glVertexAttrib1f",2},
    {"glVertexAttrib2f",3},{"glVertexAttrib3f",4},{"glVertexAttrib4f",5},
    {"glVertexAttribPointer",6},{"glViewport",4}
};

static unsigned gl_argument_count(const char *name) {
    size_t index;
    for (index = 0; index < sizeof(g_gl_descriptors)/sizeof(g_gl_descriptors[0]); ++index)
        if (strcmp(name, g_gl_descriptors[index].name) == 0)
            return g_gl_descriptors[index].count;
    return UINT32_MAX;
}

static size_t gl_pixel_bytes(uint32_t width, uint32_t height,
                             uint32_t format, uint32_t type) {
    uint64_t components = 4;
    uint64_t bytes_per_component = 1;
    if (format == 0x1906u || format == 0x1909u) components = 1;
    else if (format == 0x190au) components = 2;
    else if (format == 0x1907u) components = 3;
    if (type == 0x1403u || type == 0x1402u || type == 0x8363u ||
        type == 0x8033u || type == 0x8034u) bytes_per_component = 2;
    if (type == 0x1406u || type == 0x1405u || type == 0x1404u)
        bytes_per_component = 4;
    if (type == 0x8363u || type == 0x8033u || type == 0x8034u)
        components = 1;
    {
        uint64_t total = (uint64_t)width * height * components * bytes_per_component;
        return total <= 256u * 1024u * 1024u ? (size_t)total : 0;
    }
}

static void *guest_buffer_copy(ArmProbe *probe, uint32_t address, size_t size) {
    void *buffer;
    if (!address || !size) return NULL;
    buffer = malloc(size);
    if (!buffer) return NULL;
    if (uc_mem_read(probe->uc, address, buffer, size) != UC_ERR_OK) {
        free(buffer); return NULL;
    }
    return buffer;
}

static const void *guest_buffer_view(ArmProbe *probe, uint32_t address,
                                     size_t size, void **owned) {
    GuestMemoryRegion *region;
    if (owned) *owned = NULL;
    if (!address || !size) return NULL;
    region = guest_memory_region(probe, address, size);
    if (region) {
        ++probe->direct_memory_reads;
        return region->host + (size_t)(address - region->base);
    }
    if (owned) {
        *owned = guest_buffer_copy(probe, address, size);
        return *owned;
    }
    return NULL;
}

static size_t gl_vertex_element_bytes(uint32_t components, uint32_t type) {
    size_t component_size;
    if (components < 1u || components > 4u) return 0;
    switch (type) {
    case 0x1400u: /* GL_BYTE */
    case 0x1401u: /* GL_UNSIGNED_BYTE */
        component_size = 1u;
        break;
    case 0x1402u: /* GL_SHORT */
    case 0x1403u: /* GL_UNSIGNED_SHORT */
    case 0x140bu: /* GL_HALF_FLOAT */
        component_size = 2u;
        break;
    case 0x1404u: /* GL_INT */
    case 0x1405u: /* GL_UNSIGNED_INT */
    case 0x1406u: /* GL_FLOAT */
    case 0x140cu: /* GL_FIXED */
        component_size = 4u;
        break;
    default:
        return 0;
    }
    return (size_t)components * component_size;
}

static int gl_prepare_client_arrays(ArmProbe *probe, uint32_t vertex_limit,
                                    void **copies, unsigned *prepared_count) {
    PROC vertex_pointer = probe->gl_vertex_attrib_pointer_function;
    PROC bind_buffer = probe->gl_bind_buffer_function;
    if (!vertex_pointer) {
        vertex_pointer = resolve_gl(probe, "glVertexAttribPointer");
        probe->gl_vertex_attrib_pointer_function = vertex_pointer;
    }
    if (!bind_buffer) {
        bind_buffer = resolve_gl(probe, "glBindBuffer");
        probe->gl_bind_buffer_function = bind_buffer;
    }
    unsigned index;
    if (prepared_count) *prepared_count = 0;
    if (!vertex_limit) return 1;
    if (!vertex_pointer || !bind_buffer) return 0;

    if (probe->gl_array_buffer_binding) {
        uint32_t bind_arguments[2] = {0x8892u, 0}; /* GL_ARRAY_BUFFER */
        call_gl_raw(bind_buffer, bind_arguments, 2);
    }
    for (index = 0; index < MAX_GL_VERTEX_ATTRIBS; ++index) {
        GuestGlVertexAttrib *attribute = &probe->gl_vertex_attribs[index];
        size_t element_size;
        size_t stride;
        uint64_t required;
        uint32_t pointer_arguments[6];
        if (!attribute->enabled || !attribute->client_memory) continue;
        element_size = gl_vertex_element_bytes(attribute->size, attribute->type);
        stride = attribute->stride ? attribute->stride : element_size;
        if (!element_size || stride < element_size) goto failed;
        required = (uint64_t)(vertex_limit - 1u) * stride + element_size;
        if (required > MAX_GL_CLIENT_ARRAY_BYTES) goto failed;
        {
            GuestMemoryRegion *region = guest_memory_region(
                probe, attribute->guest_pointer, (size_t)required);
            if (region) {
                copies[index] = region->host +
                    (size_t)(attribute->guest_pointer - region->base);
                ++probe->profile_gl_direct_arrays;
                ++probe->direct_memory_reads;
            } else {
                if (!host_buffer_reserve(&probe->gl_client_buffers[index],
                                         &probe->gl_client_buffer_capacities[index],
                                         (size_t)required) ||
                    uc_mem_read(probe->uc, attribute->guest_pointer,
                                probe->gl_client_buffers[index],
                                (size_t)required) != UC_ERR_OK)
                    goto failed;
                copies[index] = probe->gl_client_buffers[index];
                ++probe->profile_gl_client_copies;
                probe->profile_gl_client_bytes += required;
            }
        }
        pointer_arguments[0] = index;
        pointer_arguments[1] = attribute->size;
        pointer_arguments[2] = attribute->type;
        pointer_arguments[3] = attribute->normalized;
        pointer_arguments[4] = attribute->stride;
        pointer_arguments[5] = (uint32_t)(uintptr_t)copies[index];
        call_gl_raw(vertex_pointer, pointer_arguments, 6);
        if (prepared_count) ++*prepared_count;
    }
    if (probe->gl_array_buffer_binding) {
        uint32_t bind_arguments[2] = {
            0x8892u, probe->gl_array_buffer_binding
        };
        call_gl_raw(bind_buffer, bind_arguments, 2);
    }
    return 1;

failed:
    if (probe->gl_array_buffer_binding) {
        uint32_t bind_arguments[2] = {
            0x8892u, probe->gl_array_buffer_binding
        };
        call_gl_raw(bind_buffer, bind_arguments, 2);
    }
    probe_log("ERROR: OpenGL client vertex array could not be read "
              "attrib=%u guest=0x%08x vertices=%u",
              index, probe->gl_vertex_attribs[index].guest_pointer,
              vertex_limit);
    return 0;
}

static size_t gl_index_element_bytes(uint32_t type) {
    if (type == 0x1401u) return 1u; /* GL_UNSIGNED_BYTE */
    if (type == 0x1403u) return 2u; /* GL_UNSIGNED_SHORT */
    if (type == 0x1405u) return 4u; /* GL_UNSIGNED_INT */
    return 0;
}

static uint32_t gl_index_vertex_limit(const void *indices, uint32_t count,
                                      uint32_t type) {
    uint32_t maximum = 0;
    uint32_t index;
    for (index = 0; index < count; ++index) {
        uint32_t value;
        if (type == 0x1401u)
            value = ((const uint8_t *)indices)[index];
        else if (type == 0x1403u)
            value = ((const uint16_t *)indices)[index];
        else
            value = ((const uint32_t *)indices)[index];
        if (value > maximum) maximum = value;
    }
    return count && maximum != UINT32_MAX ? maximum + 1u : 0;
}

static uint32_t overkill_next_gl_id(ArmProbe *probe) {
    if (++probe->overkill_next_gl_id == 0)
        probe->overkill_next_gl_id = 1;
    return probe->overkill_next_gl_id;
}

static void overkill_write_gl_ids(ArmProbe *probe, uint32_t count,
                                  uint32_t destination) {
    uint32_t *values;
    uint32_t index;
    if (!count || !destination || count > 65536u) return;
    values = (uint32_t *)malloc((size_t)count * sizeof(*values));
    if (!values) return;
    for (index = 0; index < count; ++index)
        values[index] = overkill_next_gl_id(probe);
    uc_mem_write(probe->uc, destination, values,
                 (size_t)count * sizeof(*values));
    free(values);
}

static uint32_t dispatch_gl_headless(ArmProbe *probe, const char *name,
                                     ArmGlSpecial special,
                                     uint32_t *arguments, unsigned count) {
    uint32_t value = 0;
    (void)count;
    ++probe->overkill_headless_gl_calls;
    if (special == ARM_GL_SPECIAL_BIND_BUFFER) {
        if (arguments[0] == 0x8892u) probe->gl_array_buffer_binding = arguments[1];
        if (arguments[0] == 0x8893u) probe->gl_element_array_buffer_binding = arguments[1];
        return 0;
    }
    if (special == ARM_GL_SPECIAL_ENABLE_ATTRIB ||
        special == ARM_GL_SPECIAL_DISABLE_ATTRIB) {
        if (arguments[0] < MAX_GL_VERTEX_ATTRIBS)
            probe->gl_vertex_attribs[arguments[0]].enabled =
                special == ARM_GL_SPECIAL_ENABLE_ATTRIB;
        return 0;
    }
    if (special == ARM_GL_SPECIAL_VERTEX_ATTRIB_POINTER) {
        if (arguments[0] < MAX_GL_VERTEX_ATTRIBS) {
            GuestGlVertexAttrib *attribute = &probe->gl_vertex_attribs[arguments[0]];
            attribute->size = arguments[1];
            attribute->type = arguments[2];
            attribute->normalized = arguments[3];
            attribute->stride = arguments[4];
            attribute->guest_pointer = arguments[5];
            attribute->client_memory = probe->gl_array_buffer_binding == 0;
        }
        return 0;
    }
    if (special == ARM_GL_SPECIAL_DRAW_ARRAYS ||
        special == ARM_GL_SPECIAL_DRAW_ELEMENTS) {
        int32_t draw_count = (int32_t)arguments[
            special == ARM_GL_SPECIAL_DRAW_ARRAYS ? 2 : 1];
        ++probe->profile_gl_draw_calls;
        ++probe->overkill_skipped_draws;
        if (draw_count > 0) probe->profile_gl_vertices += (uint32_t)draw_count;
        return 0;
    }
    if (strcmp(name, "glCreateProgram") == 0 ||
        strcmp(name, "glCreateShader") == 0)
        return overkill_next_gl_id(probe);
    if (strcmp(name, "glGenBuffers") == 0 ||
        strcmp(name, "glGenFramebuffers") == 0 ||
        strcmp(name, "glGenRenderbuffers") == 0 ||
        strcmp(name, "glGenTextures") == 0) {
        overkill_write_gl_ids(probe, arguments[0], arguments[1]);
        return 0;
    }
    if (strcmp(name, "glGetString") == 0) {
        const char *text = "OverkillTest2 headless OpenGL ES 2.0";
        uint32_t address = guest_alloc(probe, (uint32_t)strlen(text) + 1u);
        if (address) guest_write_string(probe, address, text,
                                        (uint32_t)strlen(text) + 1u);
        return address;
    }
    if (strcmp(name, "glCheckFramebufferStatus") == 0)
        return 0x8cd5u; /* GL_FRAMEBUFFER_COMPLETE */
    if (strcmp(name, "glGetError") == 0) return 0;
    if (strcmp(name, "glIsBuffer") == 0 || strcmp(name, "glIsEnabled") == 0 ||
        strcmp(name, "glIsFramebuffer") == 0 || strcmp(name, "glIsProgram") == 0 ||
        strcmp(name, "glIsRenderbuffer") == 0 || strcmp(name, "glIsShader") == 0 ||
        strcmp(name, "glIsTexture") == 0) return 1;
    if (strcmp(name, "glGetAttribLocation") == 0 ||
        strcmp(name, "glGetUniformLocation") == 0) return 0;
    if (strcmp(name, "glGetProgramiv") == 0 ||
        strcmp(name, "glGetShaderiv") == 0) {
        uint32_t parameter = arguments[1];
        if (parameter == 0x8b81u || parameter == 0x8b82u ||
            parameter == 0x8b83u) value = 1;
        uc_mem_write(probe->uc, arguments[2], &value, sizeof(value));
        return 0;
    }
    if (strcmp(name, "glGetBooleanv") == 0 ||
        strcmp(name, "glGetFloatv") == 0 ||
        strcmp(name, "glGetIntegerv") == 0) {
        size_t elements = gl_get_value_count(probe, arguments[0]);
        uint32_t values[16] = {0};
        if (elements > 16u) elements = 16u;
        if (strcmp(name, "glGetIntegerv") == 0) {
            if (arguments[0] == 0x0d33u) values[0] = 4096u; /* MAX_TEXTURE_SIZE */
            else if (arguments[0] == 0x8869u) values[0] = 8u; /* MAX_VERTEX_ATTRIBS */
            else if (arguments[0] == 0x8872u || arguments[0] == 0x8b4du)
                values[0] = 8u;
            else if (arguments[0] == 0x8894u)
                values[0] = probe->gl_array_buffer_binding;
            else if (arguments[0] == 0x8895u)
                values[0] = probe->gl_element_array_buffer_binding;
            else if (arguments[0] == 0x0ba2u && elements >= 4u) {
                values[2] = (uint32_t)probe->host.native_width;
                values[3] = (uint32_t)probe->host.native_height;
            }
        }
        uc_mem_write(probe->uc, arguments[1], values,
                     elements * (strcmp(name, "glGetBooleanv") == 0 ? 1u : 4u));
        return 0;
    }
    if (strcmp(name, "glGetProgramInfoLog") == 0 ||
        strcmp(name, "glGetShaderInfoLog") == 0 ||
        strcmp(name, "glGetShaderSource") == 0) {
        uint32_t zero = 0;
        if (arguments[2]) uc_mem_write(probe->uc, arguments[2], &zero, sizeof(zero));
        if (arguments[3]) uc_mem_write(probe->uc, arguments[3], &zero, 1u);
        return 0;
    }
    if (strcmp(name, "glReadPixels") == 0 && arguments[6]) {
        size_t bytes = gl_pixel_bytes(arguments[2], arguments[3],
                                      arguments[4], arguments[5]);
        if (bytes && bytes <= MAX_GL_CLIENT_ARRAY_BYTES) {
            unsigned char *zeroes = (unsigned char *)calloc(1, bytes);
            if (zeroes) {
                uc_mem_write(probe->uc, arguments[6], zeroes, bytes);
                free(zeroes);
            }
        }
        return 0;
    }
    if (strcmp(name, "glTexImage2D") == 0 ||
        strcmp(name, "glTexSubImage2D") == 0 ||
        strcmp(name, "glCompressedTexImage2D") == 0 ||
        strcmp(name, "glCompressedTexSubImage2D") == 0)
        ++probe->overkill_texture_skips;
    return 0;
}

static uint32_t dispatch_gl(ArmProbe *probe, ArmImport *import,
                            uint32_t r0, uint32_t r1, uint32_t r2,
                            uint32_t r3, uint32_t sp) {
    const char *name = import->name;
    ArmGlSpecial special = (ArmGlSpecial)import->gl_special;
    uint32_t arguments[9] = {0};
    unsigned count;
    if (import->gl_argument_count == 0xffu) {
        count = gl_argument_count(name);
        import->gl_argument_count = count <= 9u ? (unsigned char)count : 0xfeu;
    } else {
        count = import->gl_argument_count == 0xfeu
                    ? UINT32_MAX : import->gl_argument_count;
    }
    unsigned index;
    PROC function;
    if (count == UINT32_MAX) {
        probe_log("OpenGL bridge missing descriptor: %s", name);
        return 0;
    }
    for (index = 0; index < count && index < 9; ++index)
        arguments[index] = import_argument(probe, r0,r1,r2,r3,sp,index);

    if (probe->headless_gl)
        return dispatch_gl_headless(probe, name, special, arguments, count);
    if (probe->skip_texture_uploads &&
        (strcmp(name, "glTexImage2D") == 0 ||
         strcmp(name, "glTexSubImage2D") == 0 ||
         strcmp(name, "glCompressedTexImage2D") == 0 ||
         strcmp(name, "glCompressedTexSubImage2D") == 0)) {
        ++probe->overkill_texture_skips;
        return 0;
    }

    if (strcmp(name, "glClearDepthf") == 0) {
        typedef void (WINAPI *ClearDepthFunction)(double);
        ClearDepthFunction clear_depth = (ClearDepthFunction)resolve_gl(probe, "glClearDepth");
        if (clear_depth) clear_depth((double)bits_float(r0));
        return 0;
    }
    function = import->host_function;
    if (function) {
        ++probe->profile_gl_proc_cache_hits;
    } else {
        function = resolve_gl(probe, name);
        import->host_function = function;
        ++probe->profile_gl_proc_cache_misses;
    }
    if (!function) {
        probe_log("OpenGL host function unavailable: %s", name);
        return 0;
    }
    if (special == ARM_GL_SPECIAL_BIND_BUFFER) {
        uint32_t *binding = NULL;
        if (arguments[0] == 0x8892u) /* GL_ARRAY_BUFFER */
            binding = &probe->gl_array_buffer_binding;
        else if (arguments[0] == 0x8893u) /* GL_ELEMENT_ARRAY_BUFFER */
            binding = &probe->gl_element_array_buffer_binding;
        if (binding && *binding == arguments[1]) {
            ++probe->profile_gl_redundant_binds;
            return 0;
        }
        if (binding) *binding = arguments[1];
        return call_gl_raw(function, arguments, 2);
    }
    if (special == ARM_GL_SPECIAL_ENABLE_ATTRIB ||
        special == ARM_GL_SPECIAL_DISABLE_ATTRIB) {
        if (arguments[0] < MAX_GL_VERTEX_ATTRIBS)
            probe->gl_vertex_attribs[arguments[0]].enabled =
                special == ARM_GL_SPECIAL_ENABLE_ATTRIB;
        return call_gl_raw(function, arguments, 1);
    }
    if (special == ARM_GL_SPECIAL_VERTEX_ATTRIB_POINTER) {
        if (arguments[0] < MAX_GL_VERTEX_ATTRIBS) {
            GuestGlVertexAttrib *attribute =
                &probe->gl_vertex_attribs[arguments[0]];
            attribute->size = arguments[1];
            attribute->type = arguments[2];
            attribute->normalized = arguments[3];
            attribute->stride = arguments[4];
            attribute->guest_pointer = arguments[5];
            attribute->client_memory = probe->gl_array_buffer_binding == 0;
            if (attribute->client_memory && !attribute->logged) {
                probe_log("OpenGL client vertex array captured: "
                          "attrib=%u size=%u type=0x%04x stride=%u "
                          "guest=0x%08x",
                          arguments[0], arguments[1], arguments[2],
                          arguments[4], arguments[5]);
                attribute->logged = 1;
            }
            if (attribute->client_memory) return 0;
        }
        return call_gl_raw(function, arguments, 6);
    }
    if (special == ARM_GL_SPECIAL_DRAW_ARRAYS) {
        void *copies[MAX_GL_VERTEX_ATTRIBS] = {0};
        int32_t first = (int32_t)arguments[1];
        int32_t draw_count = (int32_t)arguments[2];
        uint64_t limit = first >= 0 && draw_count >= 0
                             ? (uint64_t)(uint32_t)first +
                                   (uint32_t)draw_count
                             : UINT64_MAX;
        unsigned prepared = 0;
        unsigned log_draw = probe->gl_draw_logs++;
        uint32_t draw_result = 0;
        ++probe->profile_gl_draw_calls;
        if (draw_count > 0) probe->profile_gl_vertices += (uint32_t)draw_count;
        if (probe->skip_draw_calls) {
            ++probe->overkill_skipped_draws;
            return 0;
        }
        if (limit > UINT32_MAX ||
            !gl_prepare_client_arrays(probe, (uint32_t)limit,
                                      copies, &prepared)) {
            return 0;
        }
        if (log_draw < 8u)
            probe_log("OpenGL draw begin: glDrawArrays first=%d count=%d "
                      "client_attribs=%u", first, draw_count, prepared);
        draw_result = call_gl_raw(function, arguments, 3);
        if (log_draw < 8u) probe_log("OpenGL draw returned: glDrawArrays");
        return draw_result;
    }
    if (special == ARM_GL_SPECIAL_DRAW_ELEMENTS) {
        void *copies[MAX_GL_VERTEX_ATTRIBS] = {0};
        void *indices = NULL;
        int32_t draw_count = (int32_t)arguments[1];
        size_t index_size = gl_index_element_bytes(arguments[2]);
        uint32_t vertex_limit = draw_count > 0 ? (uint32_t)draw_count : 0;
        unsigned prepared = 0;
        unsigned log_draw = probe->gl_draw_logs++;
        uint32_t draw_result = 0;
        if (draw_count < 0 || !index_size) return 0;
        ++probe->profile_gl_draw_calls;
        if (draw_count > 0) probe->profile_gl_vertices += (uint32_t)draw_count;
        if (probe->skip_draw_calls) {
            ++probe->overkill_skipped_draws;
            return 0;
        }
        if (!probe->gl_element_array_buffer_binding && draw_count) {
            uint64_t bytes = (uint64_t)(uint32_t)draw_count * index_size;
            GuestMemoryRegion *region;
            if (bytes > MAX_GL_CLIENT_ARRAY_BYTES) return 0;
            region = guest_memory_region(probe, arguments[3], (size_t)bytes);
            if (region) {
                indices = region->host +
                    (size_t)(arguments[3] - region->base);
                ++probe->profile_gl_direct_arrays;
                ++probe->direct_memory_reads;
            } else {
                if (!host_buffer_reserve(&probe->gl_index_buffer,
                                         &probe->gl_index_buffer_capacity,
                                         (size_t)bytes) ||
                    uc_mem_read(probe->uc, arguments[3],
                                probe->gl_index_buffer,
                                (size_t)bytes) != UC_ERR_OK)
                    return 0;
                indices = probe->gl_index_buffer;
                ++probe->profile_gl_client_copies;
                probe->profile_gl_client_bytes += bytes;
            }
            vertex_limit = gl_index_vertex_limit(
                indices, (uint32_t)draw_count, arguments[2]);
            arguments[3] = (uint32_t)(uintptr_t)indices;
        }
        if (!gl_prepare_client_arrays(probe, vertex_limit,
                                      copies, &prepared)) {
            return 0;
        }
        if (log_draw < 8u)
            probe_log("OpenGL draw begin: glDrawElements count=%d "
                      "client_attribs=%u client_indices=%u",
                      draw_count, prepared, indices != NULL);
        draw_result = call_gl_raw(function, arguments, 4);
        if (log_draw < 8u) probe_log("OpenGL draw returned: glDrawElements");
        return draw_result;
    }
    if (strcmp(name, "glGetString") == 0) {
        typedef const unsigned char *(WINAPI *Function)(uint32_t);
        const unsigned char *text = ((Function)function)(arguments[0]);
        uint32_t length = text ? (uint32_t)strlen((const char *)text) + 1u : 1u;
        uint32_t address = guest_alloc(probe, length);
        if (address)
            guest_write_string(probe, address, text ? (const char *)text : "", length);
        return address;
    }
    if (strcmp(name, "glBindAttribLocation") == 0 ||
        strcmp(name, "glGetUniformLocation") == 0 ||
        strcmp(name, "glGetAttribLocation") == 0) {
        char text[MAX_STRING];
        uint32_t text_address = arguments[1 + (strcmp(name,"glBindAttribLocation")==0)];
        if (!guest_read_string(probe, text_address, text, sizeof(text))) return 0;
        if (strcmp(name, "glBindAttribLocation") == 0) {
            typedef void (WINAPI *Function)(uint32_t,uint32_t,const char *);
            ((Function)function)(arguments[0],arguments[1],text); return 0;
        } else {
            typedef int (WINAPI *Function)(uint32_t,const char *);
            return (uint32_t)((Function)function)(arguments[0],text);
        }
    }
    if (strcmp(name, "glShaderSource") == 0) {
        typedef void (WINAPI *Function)(uint32_t,int,const char *const *,const int *);
        int source_count = (int)arguments[1];
        char **strings = NULL;
        int *lengths = NULL;
        uint32_t *guest_strings = NULL;
        if (source_count <= 0 || source_count > 1024) return 0;
        strings = (char **)calloc((size_t)source_count, sizeof(*strings));
        guest_strings = (uint32_t *)calloc((size_t)source_count, 4);
        if (arguments[3]) lengths = (int *)guest_buffer_copy(probe,arguments[3],(size_t)source_count*4);
        if (!strings || !guest_strings ||
            uc_mem_read(probe->uc, arguments[2], guest_strings,(size_t)source_count*4)!=UC_ERR_OK) goto shader_done;
        for (index=0; index<(unsigned)source_count; ++index) {
            size_t capacity = lengths && lengths[index] >= 0 ? (size_t)lengths[index]+1u : MAX_STRING;
            if (capacity > MAX_STRING) capacity = MAX_STRING;
            strings[index]=(char*)calloc(1,capacity);
            if (strings[index]) guest_read_string(probe,guest_strings[index],strings[index],capacity);
        }
        ((Function)function)(arguments[0],source_count,(const char *const *)strings,lengths);
shader_done:
        if (strings) for(index=0;index<(unsigned)source_count;++index) free(strings[index]);
        free(strings); free(lengths); free(guest_strings); return 0;
    }
    if (special == ARM_GL_SPECIAL_BUFFER_DATA ||
        special == ARM_GL_SPECIAL_BUFFER_SUB_DATA) {
        int sub_data = special == ARM_GL_SPECIAL_BUFFER_SUB_DATA;
        size_t size = arguments[1 + sub_data];
        uint32_t data_address = arguments[2 + sub_data];
        void *owned = NULL;
        const void *data = guest_buffer_view(probe, data_address, size, &owned);
        if (data && !owned) ++probe->profile_gl_direct_uploads;
        if (special == ARM_GL_SPECIAL_BUFFER_DATA) {
            typedef void (WINAPI *Function)(uint32_t,ptrdiff_t,const void*,uint32_t);
            ((Function)function)(arguments[0],(ptrdiff_t)arguments[1],data,arguments[3]);
        } else {
            typedef void (WINAPI *Function)(uint32_t,ptrdiff_t,ptrdiff_t,const void*);
            ((Function)function)(arguments[0],(ptrdiff_t)arguments[1],(ptrdiff_t)arguments[2],data);
        }
        free(owned); return 0;
    }
    if (probe->solid_textures &&
        (strcmp(name,"glTexImage2D")==0 || strcmp(name,"glTexSubImage2D")==0 ||
         strcmp(name,"glCompressedTexImage2D")==0 ||
         strcmp(name,"glCompressedTexSubImage2D")==0)) {
        static const unsigned char white_pixel[4] = {255,255,255,255};
        PROC texture_function = strcmp(name,"glTexImage2D")==0
            ? function : resolve_gl(probe, "glTexImage2D");
        uint32_t tiny[9] = {arguments[0], arguments[1], 0x1908u, 1u, 1u,
                            0u, 0x1908u, 0x1401u,
                            (uint32_t)(uintptr_t)white_pixel};
        if (texture_function) call_gl_raw(texture_function, tiny, 9);
        ++probe->overkill_texture_skips;
        return 0;
    }
    if (strcmp(name,"glTexImage2D")==0 || strcmp(name,"glTexSubImage2D")==0) {
        uint32_t width_index = strcmp(name,"glTexImage2D")==0 ? 3u : 4u;
        uint32_t height_index = strcmp(name,"glTexImage2D")==0 ? 4u : 5u;
        size_t size = gl_pixel_bytes(arguments[width_index],arguments[height_index],
                                     arguments[6],arguments[7]);
        void *pixels = guest_buffer_copy(probe,arguments[8],size);
        arguments[8]=(uint32_t)(uintptr_t)pixels;
        call_gl_raw(function,arguments,9); free(pixels); return 0;
    }
    if (strcmp(name,"glCompressedTexImage2D")==0) {
        void *pixels=guest_buffer_copy(probe,arguments[7],arguments[6]);
        arguments[7]=(uint32_t)(uintptr_t)pixels;
        call_gl_raw(function,arguments,8); free(pixels); return 0;
    }
    if (strcmp(name,"glCompressedTexSubImage2D")==0) {
        void *pixels=guest_buffer_copy(probe,arguments[8],arguments[7]);
        arguments[8]=(uint32_t)(uintptr_t)pixels;
        call_gl_raw(function,arguments,9); free(pixels); return 0;
    }
    if (special == ARM_GL_SPECIAL_DELETE_BUFFERS) {
        size_t bytes = (size_t)arguments[0] * sizeof(uint32_t);
        uint32_t *values = (uint32_t *)guest_buffer_copy(
            probe, arguments[1], bytes);
        unsigned value_index;
        if (values) {
            for (value_index = 0; value_index < arguments[0]; ++value_index) {
                if (values[value_index] == probe->gl_array_buffer_binding)
                    probe->gl_array_buffer_binding = 0;
                if (values[value_index] == probe->gl_element_array_buffer_binding)
                    probe->gl_element_array_buffer_binding = 0;
            }
        }
        arguments[1] = (uint32_t)(uintptr_t)values;
        call_gl_raw(function, arguments, 2);
        free(values);
        return 0;
    }
    if (strcmp(name,"glGenBuffers")==0 || strcmp(name,"glGenFramebuffers")==0 ||
        strcmp(name,"glGenRenderbuffers")==0 || strcmp(name,"glGenTextures")==0 ||
        strcmp(name,"glDeleteFramebuffers")==0 ||
        strcmp(name,"glDeleteRenderbuffers")==0 || strcmp(name,"glDeleteTextures")==0) {
        int deleting = strncmp(name,"glDelete",8)==0;
        size_t bytes=(size_t)arguments[0]*4u;
        void *values = deleting ? guest_buffer_copy(probe,arguments[1],bytes) : calloc(1,bytes);
        uint32_t guest_destination = arguments[1];
        arguments[1]=(uint32_t)(uintptr_t)values;
        call_gl_raw(function,arguments,2);
        if (!deleting && values && guest_destination)
            uc_mem_write(probe->uc,guest_destination,values,bytes);
        free(values); return 0;
    }
    if (strcmp(name,"glGetBooleanv")==0 || strcmp(name,"glGetFloatv")==0 ||
        strcmp(name,"glGetIntegerv")==0) {
        size_t elements = gl_get_value_count(probe, arguments[0]);
        size_t element_size = strcmp(name, "glGetBooleanv") == 0 ? 1u : 4u;
        size_t bytes = elements * element_size;
        void *values = calloc(elements, element_size);
        uint32_t destination=arguments[1];
        if (!values) return 0;
        arguments[1]=(uint32_t)(uintptr_t)values;
        call_gl_raw(function,arguments,2);
        if (destination) uc_mem_write(probe->uc,destination,values,bytes);
        free(values); return 0;
    }
    if (strcmp(name,"glGetProgramiv")==0 || strcmp(name,"glGetShaderiv")==0) {
        int value=0; uint32_t destination=arguments[2];
        arguments[2]=(uint32_t)(uintptr_t)&value;
        call_gl_raw(function,arguments,3);
        uc_mem_write(probe->uc,destination,&value,4); return 0;
    }
    if (strcmp(name,"glGetProgramInfoLog")==0 || strcmp(name,"glGetShaderInfoLog")==0 ||
        strcmp(name,"glGetShaderSource")==0) {
        int capacity=(int)arguments[1], length=0; char *buffer;
        uint32_t length_address=arguments[2], buffer_address=arguments[3];
        if (capacity<1 || capacity>16*1024*1024) return 0;
        buffer=(char*)calloc(1,(size_t)capacity);
        arguments[2]=(uint32_t)(uintptr_t)&length;
        arguments[3]=(uint32_t)(uintptr_t)buffer;
        call_gl_raw(function,arguments,4);
        if(length_address) uc_mem_write(probe->uc,length_address,&length,4);
        if(buffer_address&&buffer) uc_mem_write(probe->uc,buffer_address,buffer,(size_t)capacity);
        free(buffer); return 0;
    }
    if (special == ARM_GL_SPECIAL_UNIFORM_VECTOR) {
        unsigned components=1;
        if (strstr(name,"Matrix2")) components=4;
        else if (strstr(name,"Matrix3")) components=9;
        else if (strstr(name,"Matrix4")) components=16;
        else if (name[9]>='1'&&name[9]<='4') components=(unsigned)(name[9]-'0');
        {
            unsigned pointer_index=strstr(name,"Matrix")?3u:2u;
            size_t bytes=(size_t)arguments[1]*components*4u;
            void *owned = NULL;
            const void *values = guest_buffer_view(
                probe, arguments[pointer_index], bytes, &owned);
            if (values && !owned) ++probe->profile_gl_direct_uploads;
            arguments[pointer_index]=(uint32_t)(uintptr_t)values;
            call_gl_raw(function,arguments,count); free(owned); return 0;
        }
    }
    if (strcmp(name,"glReadPixels")==0) {
        size_t bytes=gl_pixel_bytes(arguments[2],arguments[3],arguments[4],arguments[5]);
        void *pixels=calloc(1,bytes); uint32_t destination=arguments[6];
        arguments[6]=(uint32_t)(uintptr_t)pixels;
        call_gl_raw(function,arguments,7);
        if(pixels&&destination) uc_mem_write(probe->uc,destination,pixels,bytes);
        free(pixels); return 0;
    }
    return call_gl_raw(function,arguments,count);
}

static int guest_callback_push(ArmProbe *probe, GuestCallbackKind kind,
                               uint32_t return_address,
                               uint32_t once_control) {
    GuestCallbackFrame *frame;
    if (probe->callback_depth >= MAX_CALLBACK_DEPTH) return 0;
    frame = &probe->callback_frames[probe->callback_depth++];
    frame->kind = kind;
    frame->return_address = return_address;
    frame->once_control = once_control;
    return 1;
}

static int guest_callback_pop(ArmProbe *probe, GuestCallbackFrame *frame) {
    if (!probe->callback_depth || !frame) return 0;
    *frame = probe->callback_frames[--probe->callback_depth];
    memset(&probe->callback_frames[probe->callback_depth], 0,
           sizeof(probe->callback_frames[probe->callback_depth]));
    return 1;
}

static void guest_qsort_fail(ArmProbe *probe, const char *reason) {
    probe->qsort_active = 0;
    probe->failed = 1;
    snprintf(probe->failure, sizeof(probe->failure),
             "guest qsort bridge failed: %s", reason);
    uc_emu_stop(probe->uc);
}

static uint32_t guest_qsort_element(const ArmProbe *probe, uint32_t index) {
    uint64_t address = (uint64_t)probe->qsort_base +
                       (uint64_t)index * probe->qsort_size;
    return address <= UINT32_MAX ? (uint32_t)address : 0;
}

static int guest_qsort_swap(ArmProbe *probe, uint32_t first_index,
                            uint32_t second_index) {
    uint32_t first_address;
    uint32_t second_address;
    unsigned char *first;
    unsigned char *second;
    if (first_index == second_index) return 1;
    first_address = guest_qsort_element(probe, first_index);
    second_address = guest_qsort_element(probe, second_index);
    if (!first_address || !second_address || !probe->qsort_size) return 0;
    first = (unsigned char *)malloc(probe->qsort_size);
    second = (unsigned char *)malloc(probe->qsort_size);
    if (!first || !second ||
        uc_mem_read(probe->uc, first_address, first, probe->qsort_size) !=
            UC_ERR_OK ||
        uc_mem_read(probe->uc, second_address, second, probe->qsort_size) !=
            UC_ERR_OK ||
        uc_mem_write(probe->uc, first_address, second, probe->qsort_size) !=
            UC_ERR_OK ||
        uc_mem_write(probe->uc, second_address, first, probe->qsort_size) !=
            UC_ERR_OK) {
        free(first);
        free(second);
        return 0;
    }
    free(first);
    free(second);
    ++probe->qsort_swaps;
    return 1;
}

static int guest_qsort_compare(ArmProbe *probe, uint32_t first_index,
                               uint32_t second_index, int phase) {
    uint32_t arguments[2];
    uint32_t callback_return = GUEST_CALLBACK_RETURN | 1u;
    arguments[0] = guest_qsort_element(probe, first_index);
    arguments[1] = guest_qsort_element(probe, second_index);
    if (!arguments[0] || !arguments[1] || !probe->qsort_comparator)
        return 0;
    if (!guest_callback_push(probe, GUEST_CALLBACK_QSORT, 0, 0)) return 0;
    probe->qsort_phase = phase;
    uc_reg_write(probe->uc, UC_ARM_REG_R0, &arguments[0]);
    uc_reg_write(probe->uc, UC_ARM_REG_R1, &arguments[1]);
    uc_reg_write(probe->uc, UC_ARM_REG_LR, &callback_return);
    uc_reg_write(probe->uc, UC_ARM_REG_PC, &probe->qsort_comparator);
    return 1;
}

static void guest_qsort_complete(ArmProbe *probe) {
    uint32_t zero = 0;
    uint32_t return_address = probe->qsort_return_address;
    unsigned call = probe->qsort_calls;
    probe->qsort_active = 0;
    probe->qsort_phase = 0;
    set_r0(probe->uc, zero);
    uc_reg_write(probe->uc, UC_ARM_REG_LR, &return_address);
    uc_reg_write(probe->uc, UC_ARM_REG_PC, &return_address);
    if (call <= 12u)
        probe_log("ARM qsort completed: call=%u count=%u size=%u "
                  "comparisons=%llu swaps=%llu",
                  call, probe->qsort_count, probe->qsort_size,
                  (unsigned long long)probe->qsort_comparisons,
                  (unsigned long long)probe->qsort_swaps);
}

static void guest_qsort_begin_sift(ArmProbe *probe);

static void guest_qsort_finish_sift(ArmProbe *probe) {
    if (probe->qsort_building) {
        if (probe->qsort_build_root) {
            --probe->qsort_build_root;
            probe->qsort_sift_root = probe->qsort_build_root;
            guest_qsort_begin_sift(probe);
            return;
        }
        probe->qsort_building = 0;
    }
    if (probe->qsort_heap_end <= 1u) {
        guest_qsort_complete(probe);
        return;
    }
    if (!guest_qsort_swap(probe, 0, probe->qsort_heap_end - 1u)) {
        guest_qsort_fail(probe, "cannot swap guest elements");
        return;
    }
    --probe->qsort_heap_end;
    if (probe->qsort_heap_end <= 1u) {
        guest_qsort_complete(probe);
        return;
    }
    probe->qsort_sift_root = 0;
    guest_qsort_begin_sift(probe);
}

static void guest_qsort_begin_sift(ArmProbe *probe) {
    uint64_t child = (uint64_t)probe->qsort_sift_root * 2u + 1u;
    if (child >= probe->qsort_heap_end) {
        guest_qsort_finish_sift(probe);
        return;
    }
    probe->qsort_sift_child = (uint32_t)child;
    probe->qsort_selected_child = (uint32_t)child;
    if (child + 1u < probe->qsort_heap_end) {
        if (!guest_qsort_compare(probe, (uint32_t)child,
                                 (uint32_t)child + 1u,
                                 GUEST_QSORT_CHOOSE_CHILD))
            guest_qsort_fail(probe, "cannot invoke guest comparator");
    } else if (!guest_qsort_compare(probe, probe->qsort_sift_root,
                                    (uint32_t)child,
                                    GUEST_QSORT_COMPARE_ROOT)) {
        guest_qsort_fail(probe, "cannot invoke guest comparator");
    }
}

static int guest_qsort_start(ArmProbe *probe, uint32_t base,
                             uint32_t count, uint32_t size,
                             uint32_t comparator) {
    uint64_t bytes = (uint64_t)count * size;
    uint32_t return_address = 0;
    ++probe->qsort_calls;
    uc_reg_read(probe->uc, UC_ARM_REG_LR, &return_address);
    if (probe->qsort_calls <= 12u)
        probe_log("ARM qsort bridge: call=%u base=0x%08x count=%u size=%u "
                  "comparator=0x%08x caller=0x%08x",
                  probe->qsort_calls, base, count, size, comparator,
                  return_address);
    if (count < 2u) return 0;
    if (probe->qsort_active) {
        guest_qsort_fail(probe, "nested qsort is unsupported");
        return 1;
    }
    if (!base || !size || !comparator || bytes > MAX_QSORT_BYTES ||
        (uint64_t)base + bytes > UINT32_MAX + UINT64_C(1)) {
        guest_qsort_fail(probe, "invalid array or comparator");
        return 1;
    }
    probe->qsort_base = base;
    probe->qsort_count = count;
    probe->qsort_size = size;
    probe->qsort_comparator = comparator;
    probe->qsort_return_address = return_address;
    probe->qsort_heap_end = count;
    probe->qsort_build_root = count / 2u - 1u;
    probe->qsort_sift_root = probe->qsort_build_root;
    probe->qsort_building = 1;
    probe->qsort_phase = 0;
    probe->qsort_active = 1;
    guest_qsort_begin_sift(probe);
    return 1;
}

static void guest_callback_return_hook(uc_engine *uc, uint64_t address,
                                       uint32_t size, void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    GuestCallbackFrame frame;
    uint32_t comparison = 0;
    (void)uc;
    (void)address;
    (void)size;
    if (!guest_callback_pop(probe, &frame)) {
        guest_qsort_fail(probe, "unexpected callback return");
        return;
    }
    if (frame.kind == GUEST_CALLBACK_PTHREAD_ONCE) {
        uint32_t complete = 2u;
        if (!frame.once_control ||
            uc_mem_write(probe->uc, frame.once_control, &complete,
                         sizeof(complete)) != UC_ERR_OK) {
            probe->failed = 1;
            snprintf(probe->failure, sizeof(probe->failure),
                     "pthread_once cannot complete control 0x%08x",
                     frame.once_control);
            uc_emu_stop(probe->uc);
            return;
        }
        set_r0(probe->uc, 0);
        uc_reg_write(probe->uc, UC_ARM_REG_LR, &frame.return_address);
        uc_reg_write(probe->uc, UC_ARM_REG_PC, &frame.return_address);
        if (probe->pthread_once_calls <= 16u)
            probe_log("ARM pthread_once initializer returned: "
                      "control=0x%08x resume=0x%08x depth=%u",
                      frame.once_control, frame.return_address,
                      probe->callback_depth);
        return;
    }
    if (frame.kind != GUEST_CALLBACK_QSORT || !probe->qsort_active) {
        guest_qsort_fail(probe, "mismatched callback return");
        return;
    }
    uc_reg_read(probe->uc, UC_ARM_REG_R0, &comparison);
    ++probe->qsort_comparisons;
    if (probe->qsort_phase == GUEST_QSORT_CHOOSE_CHILD) {
        if ((int32_t)comparison < 0)
            probe->qsort_selected_child = probe->qsort_sift_child + 1u;
        else
            probe->qsort_selected_child = probe->qsort_sift_child;
        if (!guest_qsort_compare(probe, probe->qsort_sift_root,
                                 probe->qsort_selected_child,
                                 GUEST_QSORT_COMPARE_ROOT))
            guest_qsort_fail(probe, "cannot continue guest comparator");
        return;
    }
    if (probe->qsort_phase != GUEST_QSORT_COMPARE_ROOT) {
        guest_qsort_fail(probe, "invalid comparator phase");
        return;
    }
    if ((int32_t)comparison < 0) {
        if (!guest_qsort_swap(probe, probe->qsort_sift_root,
                              probe->qsort_selected_child)) {
            guest_qsort_fail(probe, "cannot swap compared elements");
            return;
        }
        probe->qsort_sift_root = probe->qsort_selected_child;
        guest_qsort_begin_sift(probe);
    } else {
        guest_qsort_finish_sift(probe);
    }
}

static int guest_pthread_once_start(ArmProbe *probe, uint32_t control,
                                    uint32_t function) {
    uint32_t state = 0;
    uint32_t return_address = 0;
    uint32_t callback_return = GUEST_CALLBACK_RETURN | 1u;
    ++probe->pthread_once_calls;
    if (!control ||
        uc_mem_read(probe->uc, control, &state, sizeof(state)) != UC_ERR_OK)
        return -1;
    if (probe->pthread_once_calls <= 16u)
        probe_log("ARM pthread_once: call=%u control=0x%08x state=%u "
                  "initializer=0x%08x depth=%u",
                  probe->pthread_once_calls, control, state, function,
                  probe->callback_depth);
    if (state == 2u) return 0;
    if (state == 1u) {
        /* The ARM guest currently runs on one host thread. Seeing the active
           state here means an initializer recursively reached the same once
           object; the outer callback will publish state 2 when it returns. */
        return 0;
    }
    if (state != 0u) return -1;
    if (!function) {
        state = 2u;
        return uc_mem_write(probe->uc, control, &state, sizeof(state)) ==
                       UC_ERR_OK
                   ? 0 : -1;
    }
    uc_reg_read(probe->uc, UC_ARM_REG_LR, &return_address);
    if (!guest_callback_push(probe, GUEST_CALLBACK_PTHREAD_ONCE,
                             return_address, control))
        return -1;
    state = 1u;
    if (uc_mem_write(probe->uc, control, &state, sizeof(state)) != UC_ERR_OK) {
        GuestCallbackFrame ignored;
        guest_callback_pop(probe, &ignored);
        return -1;
    }
    uc_reg_write(probe->uc, UC_ARM_REG_LR, &callback_return);
    uc_reg_write(probe->uc, UC_ARM_REG_PC, &function);
    return 1;
}

static void dispatch_import(ArmProbe *probe, ArmImport *import,
                            uint32_t r0, uint32_t r1,
                            uint32_t r2, uint32_t r3, uint32_t sp) {
    const char *name = import->name;
    char first[MAX_STRING];
    char second[MAX_STRING];
    uint32_t result = 0;
    if (import->calls++ == 0)
        probe_log("  import: %s", name);

    switch (import->kind) {
    case IMPORT_MALLOC:
        result = guest_alloc(probe, r0);
        goto import_done;
    case IMPORT_CALLOC: {
        uint64_t size = (uint64_t)r0 * r1;
        if (size <= UINT32_MAX) {
            uint32_t bytes = size ? (uint32_t)size : 1u;
            result = guest_alloc(probe, bytes);
            if (result && !guest_zero_memory(probe, result, bytes)) {
                guest_free(probe, result);
                result = 0;
            }
        }
        goto import_done;
    }
    case IMPORT_REALLOC: {
        uint32_t old_size = guest_allocation_size(probe, r0);
        if (!r0) result = guest_alloc(probe, r1);
        else if (!r1) { guest_free(probe, r0); result = 0; }
        else if (old_size >= r1) result = r0;
        else {
            result = guest_alloc(probe, r1);
            if (result && old_size &&
                guest_copy_memory(probe, result, r0, old_size))
                guest_free(probe, r0);
            else if (result && old_size) {
                guest_free(probe, result);
                result = 0;
            }
        }
        goto import_done;
    }
    case IMPORT_FREE:
        guest_free(probe, r0);
        result = 0;
        goto import_done;
    case IMPORT_MEMCPY:
    case IMPORT_MEMMOVE:
        result = guest_copy_memory(probe, r0, r1, r2) ? r0 : 0;
        goto import_done;
    case IMPORT_MEMSET: {
        GuestMemoryRegion *region = guest_memory_region(probe, r0, r2);
        if (region && region->direct_write) {
            memset(region->host + (size_t)(r0 - region->base),
                   (unsigned char)r1, r2);
            ++probe->direct_memory_writes;
        } else {
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
        }
        result = r0;
        goto import_done;
    }
    case IMPORT_MEMCMP: {
        int comparison = 0;
        if (guest_compare_memory(probe, r0, r1, r2, &comparison))
            result = (uint32_t)comparison;
        goto import_done;
    }
    case IMPORT_STRLEN:
        guest_cstring_length(probe, r0, &result);
        goto import_done;
    case IMPORT_STRCMP:
    case IMPORT_STRNCMP: {
        int comparison = 0;
        uint32_t limit = import->kind == IMPORT_STRNCMP
                             ? r2 : MAX_GUEST_CSTRING;
        if (!limit || guest_compare_cstrings(probe, r0, r1, limit, 0,
                                             &comparison))
            result = (uint32_t)comparison;
        goto import_done;
    }
    case IMPORT_PTHREAD_NOOP:
        result = 0;
        goto import_done;
    case IMPORT_LRAND48:
        if (!probe->lrand48_state)
            probe->lrand48_state = UINT64_C(0x1234abcd330e);
        probe->lrand48_state =
            (UINT64_C(0x5deece66d) * probe->lrand48_state + UINT64_C(0xb)) &
            UINT64_C(0x0000ffffffffffff);
        result = (uint32_t)(probe->lrand48_state >> 17u);
        goto import_done;
    case IMPORT_FREAD: {
        GuestFile *file = guest_file(probe, r3);
        uint64_t requested = (uint64_t)r1 * r2;
        size_t actual = requested <= SIZE_MAX
                            ? guest_file_read(probe, file, r0,
                                              (size_t)requested)
                            : 0;
        result = r1 ? (uint32_t)(actual / r1) : 0;
        goto import_done;
    }
    case IMPORT_SIN:
    case IMPORT_COS:
    case IMPORT_ACOS: {
        double value = bits_double(r0, r1);
        if (import->kind == IMPORT_SIN) value = sin(value);
        else if (import->kind == IMPORT_COS) value = cos(value);
        else value = acos(value);
        set_r0_r1_u64(probe->uc, double_bits(value));
        return;
    }
    case IMPORT_ATAN2:
    case IMPORT_POW:
    case IMPORT_FMOD: {
        double first_value = bits_double(r0, r1);
        double second_value = bits_double(r2, r3);
        if (import->kind == IMPORT_ATAN2)
            first_value = atan2(first_value, second_value);
        else if (import->kind == IMPORT_POW)
            first_value = pow(first_value, second_value);
        else
            first_value = fmod(first_value, second_value);
        set_r0_r1_u64(probe->uc, double_bits(first_value));
        return;
    }
    case IMPORT_AEABI_UIDIV:
        result = r1 ? r0 / r1 : 0;
        goto import_done;
    case IMPORT_AEABI_IDIV:
        result = r1 ? (uint32_t)((int32_t)r0 / (int32_t)r1) : 0;
        goto import_done;
    case IMPORT_AEABI_UIDIVMOD: {
        uint32_t quotient = r1 ? r0 / r1 : 0;
        uint32_t remainder = r1 ? r0 % r1 : 0;
        uc_reg_write(probe->uc, UC_ARM_REG_R0, &quotient);
        uc_reg_write(probe->uc, UC_ARM_REG_R1, &remainder);
        return;
    }
    case IMPORT_AEABI_IDIVMOD: {
        int32_t dividend = (int32_t)r0;
        int32_t divisor = (int32_t)r1;
        int32_t quotient = divisor ? dividend / divisor : 0;
        int32_t remainder = divisor ? dividend % divisor : 0;
        uc_reg_write(probe->uc, UC_ARM_REG_R0, &quotient);
        uc_reg_write(probe->uc, UC_ARM_REG_R1, &remainder);
        return;
    }
    case IMPORT_OPENGL:
        result = dispatch_gl(probe, import, r0, r1, r2, r3, sp);
        goto import_done;
    case IMPORT_SLOW:
    default:
        break;
    }

    if (strcmp(name, "__aeabi_memcpy") == 0 ||
        strcmp(name, "__aeabi_memcpy4") == 0 ||
        strcmp(name, "__aeabi_memcpy8") == 0 ||
        strcmp(name, "__aeabi_memmove") == 0 ||
        strcmp(name, "__aeabi_memmove4") == 0 ||
        strcmp(name, "__aeabi_memmove8") == 0) {
        result = guest_copy_memory(probe, r0, r1, r2) ? r0 : 0;
    } else if (strcmp(name, "__aeabi_memset") == 0 ||
               strcmp(name, "__aeabi_memset4") == 0 ||
               strcmp(name, "__aeabi_memset8") == 0) {
        unsigned char block[4096]; uint32_t offset=0;
        memset(block,(unsigned char)r2,sizeof(block));
        while(offset<r1){uint32_t part=r1-offset;if(part>sizeof(block))part=sizeof(block);if(uc_mem_write(probe->uc,r0+offset,block,part)!=UC_ERR_OK)break;offset+=part;}
        result=r0;
    } else if (strcmp(name, "__aeabi_memclr") == 0 ||
               strcmp(name, "__aeabi_memclr4") == 0 ||
               strcmp(name, "__aeabi_memclr8") == 0) {
        unsigned char block[4096]={0}; uint32_t offset=0;
        while(offset<r1){uint32_t part=r1-offset;if(part>sizeof(block))part=sizeof(block);if(uc_mem_write(probe->uc,r0+offset,block,part)!=UC_ERR_OK)break;offset+=part;}
        result=r0;
    } else if (strcmp(name, "__aeabi_uidiv") == 0) {
        result = r1 ? r0 / r1 : 0;
    } else if (strcmp(name, "__aeabi_idiv") == 0) {
        result = r1 ? (uint32_t)((int32_t)r0 / (int32_t)r1) : 0;
    } else if (strcmp(name, "__aeabi_uidivmod") == 0) {
        uint32_t quotient = r1 ? r0 / r1 : 0;
        uint32_t remainder = r1 ? r0 % r1 : 0;
        uc_reg_write(probe->uc, UC_ARM_REG_R0, &quotient);
        uc_reg_write(probe->uc, UC_ARM_REG_R1, &remainder);
        return;
    } else if (strcmp(name, "__aeabi_idivmod") == 0) {
        int32_t dividend = (int32_t)r0, divisor = (int32_t)r1;
        int32_t quotient = divisor ? dividend / divisor : 0;
        int32_t remainder = divisor ? dividend % divisor : 0;
        uc_reg_write(probe->uc, UC_ARM_REG_R0, &quotient);
        uc_reg_write(probe->uc, UC_ARM_REG_R1, &remainder);
        return;
    } else if (strcmp(name, "__aeabi_uldivmod") == 0 ||
               strcmp(name, "__aeabi_ldivmod") == 0) {
        uint64_t dividend = join_u64(r0, r1);
        uint64_t divisor = join_u64(r2, r3);
        uint64_t quotient = 0, remainder = 0;
        uint32_t values[4];
        if (divisor) {
            if (strcmp(name, "__aeabi_ldivmod") == 0) {
                int64_t signed_dividend = (int64_t)dividend;
                int64_t signed_divisor = (int64_t)divisor;
                quotient = (uint64_t)(signed_dividend / signed_divisor);
                remainder = (uint64_t)(signed_dividend % signed_divisor);
            } else {
                quotient = dividend / divisor;
                remainder = dividend % divisor;
            }
        }
        values[0] = (uint32_t)quotient;
        values[1] = (uint32_t)(quotient >> 32);
        values[2] = (uint32_t)remainder;
        values[3] = (uint32_t)(remainder >> 32);
        uc_reg_write(probe->uc, UC_ARM_REG_R0, &values[0]);
        uc_reg_write(probe->uc, UC_ARM_REG_R1, &values[1]);
        uc_reg_write(probe->uc, UC_ARM_REG_R2, &values[2]);
        uc_reg_write(probe->uc, UC_ARM_REG_R3, &values[3]);
        return;
    } else if (strcmp(name, "malloc") == 0) {
        result = guest_alloc(probe, r0);
    } else if (strcmp(name, "calloc") == 0) {
        uint64_t size = (uint64_t)r0 * r1;
        if (size <= UINT32_MAX) {
            uint32_t bytes = size ? (uint32_t)size : 1u;
            result = guest_alloc(probe, bytes);
            if (result && !guest_zero_memory(probe, result, bytes)) {
                guest_free(probe, result);
                result = 0;
            }
        }
    } else if (strcmp(name, "realloc") == 0) {
        uint32_t old_size = guest_allocation_size(probe, r0);
        if (!r0) {
            result = guest_alloc(probe, r1);
        } else if (!r1) {
            guest_free(probe, r0);
            result = 0;
        } else if (old_size >= r1) {
            result = r0;
        } else {
            result = guest_alloc(probe, r1);
            if (result && old_size &&
                guest_copy_memory(probe, result, r0, old_size)) {
                guest_free(probe, r0);
            } else if (result && old_size) {
                guest_free(probe, result);
                result = 0;
            }
        }
    } else if (strcmp(name, "free") == 0) {
        guest_free(probe, r0);
        result = 0;
    } else if (strcmp(name, "qsort") == 0) {
        if (guest_qsort_start(probe, r0, r1, r2, r3)) return;
        result = 0;
    } else if (strcmp(name, "__cxa_finalize") == 0) {
        result = 0;
    } else if (strcmp(name, "__cxa_atexit") == 0) {
        result = 0;
    } else if (strcmp(name, "__errno") == 0) {
        result = probe->errno_address;
    } else if (strcmp(name, "tolower") == 0) {
        result = (uint32_t)tolower((unsigned char)r0);
    } else if (strcmp(name, "toupper") == 0) {
        result = (uint32_t)toupper((unsigned char)r0);
    } else if (strcmp(name, "isalnum") == 0 || strcmp(name, "isalpha") == 0 ||
               strcmp(name, "iscntrl") == 0 || strcmp(name, "isdigit") == 0 ||
               strcmp(name, "isgraph") == 0 || strcmp(name, "islower") == 0 ||
               strcmp(name, "isprint") == 0 || strcmp(name, "ispunct") == 0 ||
               strcmp(name, "isspace") == 0 || strcmp(name, "isupper") == 0 ||
               strcmp(name, "isxdigit") == 0 || strcmp(name, "isascii") == 0) {
        int character = (unsigned char)r0;
        if (strcmp(name, "isalnum") == 0) result = isalnum(character) != 0;
        else if (strcmp(name, "isalpha") == 0) result = isalpha(character) != 0;
        else if (strcmp(name, "iscntrl") == 0) result = iscntrl(character) != 0;
        else if (strcmp(name, "isdigit") == 0) result = isdigit(character) != 0;
        else if (strcmp(name, "isgraph") == 0) result = isgraph(character) != 0;
        else if (strcmp(name, "islower") == 0) result = islower(character) != 0;
        else if (strcmp(name, "isprint") == 0) result = isprint(character) != 0;
        else if (strcmp(name, "ispunct") == 0) result = ispunct(character) != 0;
        else if (strcmp(name, "isspace") == 0) result = isspace(character) != 0;
        else if (strcmp(name, "isupper") == 0) result = isupper(character) != 0;
        else if (strcmp(name, "isxdigit") == 0) result = isxdigit(character) != 0;
        else result = character <= 0x7f;
    } else if (strcmp(name, "wctob") == 0) {
        result = r0 <= 0xffu ? r0 : (uint32_t)-1;
    } else if (strcmp(name, "btowc") == 0) {
        result = r0 == (uint32_t)-1 ? (uint32_t)-1 : (unsigned char)r0;
    } else if (strcmp(name, "wctype") == 0) {
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            static const char *const classes[] = {
                "alnum", "alpha", "blank", "cntrl", "digit", "graph",
                "lower", "print", "punct", "space", "upper", "xdigit"
            };
            unsigned class_index;
            for (class_index = 0; class_index < sizeof(classes)/sizeof(classes[0]); ++class_index)
                if (strcmp(first, classes[class_index]) == 0) {
                    result = class_index + 1u;
                    break;
                }
        }
    } else if (strcmp(name, "iswctype") == 0) {
        int character = r0 <= 0xffu ? (int)r0 : 0;
        switch (r1) {
        case 1: result = isalnum(character) != 0; break;
        case 2: result = isalpha(character) != 0; break;
        case 3: result = character == ' ' || character == '\t'; break;
        case 4: result = iscntrl(character) != 0; break;
        case 5: result = isdigit(character) != 0; break;
        case 6: result = isgraph(character) != 0; break;
        case 7: result = islower(character) != 0; break;
        case 8: result = isprint(character) != 0; break;
        case 9: result = ispunct(character) != 0; break;
        case 10: result = isspace(character) != 0; break;
        case 11: result = isupper(character) != 0; break;
        case 12: result = isxdigit(character) != 0; break;
        default: result = 0; break;
        }
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
    } else if (strcmp(name, "memchr") == 0 ||
               strcmp(name, "memrchr") == 0) {
        result = guest_find_byte(probe, r0, (unsigned char)r1, r2,
                                 strcmp(name, "memrchr") == 0);
    } else if (strcmp(name, "wmemchr") == 0) {
        result = guest_find_wide_character(probe, r0, r1, r2);
    } else if (strcmp(name, "memcmp") == 0) {
        int comparison = 0;
        if (guest_compare_memory(probe, r0, r1, r2, &comparison))
            result = (uint32_t)comparison;
    } else if (strcmp(name, "strlen") == 0) {
        guest_cstring_length(probe, r0, &result);
    } else if (strcmp(name, "strchr") == 0 || strcmp(name, "strrchr") == 0) {
        result = guest_find_character(probe, r0, (unsigned char)r1,
                                      strcmp(name, "strrchr") == 0);
    } else if (strcmp(name, "strstr") == 0) {
        uint32_t first_length = 0, second_length = 0;
        char *first_value = guest_duplicate_cstring(
            probe, r0, MAX_GUEST_CSTRING, &first_length);
        char *second_value = guest_duplicate_cstring(
            probe, r1, MAX_GUEST_CSTRING, &second_length);
        if (first_value && second_value) {
            char *found = strstr(first_value, second_value);
            result = found ? r0 + (uint32_t)(found - first_value) : 0;
        }
        free(first_value);
        free(second_value);
    } else if (strcmp(name, "strspn") == 0 || strcmp(name, "strcspn") == 0) {
        unsigned char table[256];
        unsigned char stop = 0;
        if (guest_character_set(probe, r1, table))
            guest_span_set(probe, r0, table, strcmp(name, "strspn") == 0,
                           MAX_GUEST_CSTRING, &result, &stop);
    } else if (strcmp(name, "strtok") == 0 ||
               strcmp(name, "strtok_r") == 0) {
        int reentrant = strcmp(name, "strtok_r") == 0;
        uint32_t current = r0;
        uint32_t next = 0;
        unsigned char table[256];
        unsigned char stop = 0;
        uint32_t skipped = 0;
        uint32_t token_length = 0;
        if (!current) {
            if (reentrant) {
                if (!r2 || uc_mem_read(probe->uc, r2, &current,
                                       sizeof(current)) != UC_ERR_OK)
                    current = 0;
            } else {
                current = probe->strtok_next;
            }
        }
        if (current && guest_character_set(probe, r1, table) &&
            guest_span_set(probe, current, table, 1, MAX_GUEST_CSTRING,
                           &skipped, &stop) && stop) {
            result = current + skipped;
            if (guest_span_set(probe, result, table, 0,
                               MAX_GUEST_CSTRING - skipped,
                               &token_length, &stop)) {
                const unsigned char zero = 0;
                next = result + token_length;
                if (stop &&
                    uc_mem_write(probe->uc, next, &zero, 1) == UC_ERR_OK)
                    ++next;
            }
        }
        if (!next && current) next = current + skipped;
        if (reentrant) {
            if (r2) uc_mem_write(probe->uc, r2, &next, sizeof(next));
        } else {
            probe->strtok_next = next;
        }
    } else if (strcmp(name, "strcmp") == 0 ||
               strcmp(name, "strcasecmp") == 0 ||
               strcmp(name, "strncmp") == 0 ||
               strcmp(name, "strncasecmp") == 0 ||
               strcmp(name, "strcoll") == 0) {
        int comparison = 0;
        uint32_t limit = strcmp(name, "strncmp") == 0 ||
                         strcmp(name, "strncasecmp") == 0
                             ? r2 : MAX_GUEST_CSTRING;
        int ignore_case = strcmp(name, "strcasecmp") == 0 ||
                          strcmp(name, "strncasecmp") == 0;
        if (!limit || guest_compare_cstrings(probe, r0, r1, limit,
                                             ignore_case, &comparison))
            result = (uint32_t)comparison;
    } else if (strcmp(name, "strcpy") == 0 ||
               strcmp(name, "strncpy") == 0 ||
               strcmp(name, "strcat") == 0) {
        uint32_t source_length = 0;
        int terminated = 0;
        if (strcmp(name, "strncpy") == 0) {
            if (r2 && guest_cstring_nlength(probe, r1, r2, &source_length,
                                            &terminated) &&
                guest_copy_memory(probe, r0, r1,
                                  terminated ? source_length + 1u
                                             : source_length) &&
                terminated && source_length + 1u < r2)
                guest_zero_memory(probe, r0 + source_length + 1u,
                                  r2 - source_length - 1u);
        } else if (guest_cstring_length(probe, r1, &source_length)) {
            uint32_t destination_address = r0;
            if (strcmp(name, "strcat") == 0) {
                uint32_t destination_length = 0;
                if (!guest_cstring_length(probe, r0, &destination_length) ||
                    destination_length > UINT32_MAX - r0)
                    destination_address = 0;
                else
                    destination_address = r0 + destination_length;
            }
            if (destination_address && source_length < MAX_GUEST_CSTRING)
                guest_copy_memory(probe, destination_address, r1,
                                  source_length + 1u);
        }
        result = r0;
    } else if (strcmp(name, "strdup") == 0) {
        uint32_t length = 0;
        if (guest_cstring_length(probe, r0, &length) &&
            length < MAX_GUEST_CSTRING) {
            result = guest_alloc(probe, length + 1u);
            if (result && !guest_copy_memory(probe, result, r0, length + 1u)) {
                guest_free(probe, result);
                result = 0;
            }
        }
    } else if (strcmp(name, "atoi") == 0 || strcmp(name, "atol") == 0 ||
               strcmp(name, "strtol") == 0 || strcmp(name, "strtoul") == 0) {
        char *end = NULL;
        int takes_end_pointer = strcmp(name, "strtol") == 0 ||
                                strcmp(name, "strtoul") == 0;
        int base = (strcmp(name, "atoi") == 0 || strcmp(name, "atol") == 0)
                       ? 10 : (int)r2;
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            if (strcmp(name, "strtoul") == 0)
                result = (uint32_t)strtoul(first, &end, base);
            else result = (uint32_t)strtol(first, &end, base);
            if (takes_end_pointer && r1 && end) {
                uint32_t guest_end = r0 + (uint32_t)(end - first);
                uc_mem_write(probe->uc, r1, &guest_end, sizeof(guest_end));
            }
        }
    } else if (strcmp(name, "strtoll") == 0 ||
               strcmp(name, "strtoull") == 0) {
        char *end = NULL;
        uint64_t value = 0;
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            value = strcmp(name, "strtoull") == 0
                        ? (uint64_t)strtoull(first, &end, (int)r2)
                        : (uint64_t)strtoll(first, &end, (int)r2);
            if (r1 && end) {
                uint32_t guest_end = r0 + (uint32_t)(end - first);
                uc_mem_write(probe->uc, r1, &guest_end, sizeof(guest_end));
            }
        }
        set_r0_r1_u64(probe->uc, value);
        return;
    } else if (strcmp(name, "atof") == 0 || strcmp(name, "strtod") == 0) {
        char *end = NULL;
        double value = 0.0;
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            value = strtod(first, &end);
            if (strcmp(name, "strtod") == 0 && r1 && end) {
                uint32_t guest_end = r0 + (uint32_t)(end - first);
                uc_mem_write(probe->uc, r1, &guest_end, sizeof(guest_end));
            }
        }
        set_r0_r1_u64(probe->uc, double_bits(value));
        return;
    } else if (strcmp(name, "strtof") == 0) {
        char *end = NULL;
        float value = 0.0f;
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            value = strtof(first, &end);
            if (r1 && end) {
                uint32_t guest_end = r0 + (uint32_t)(end - first);
                uc_mem_write(probe->uc, r1, &guest_end, sizeof(guest_end));
            }
        }
        result = float_bits(value);
    } else if (strcmp(name, "crc32") == 0) {
        unsigned char *payload = NULL;
        int owns_payload = 0;
        if (!r1) {
            result = (uint32_t)crc32((uLong)r0, Z_NULL, 0);
        } else if (!r2) {
            result = r0;
        } else if (r2 <= MAX_GUEST_ZLIB_BUFFER) {
            GuestMemoryRegion *region = guest_memory_region(probe, r1, r2);
            if (region) {
                payload = region->host + (size_t)(r1 - region->base);
                ++probe->direct_memory_reads;
            } else {
                payload = (unsigned char *)malloc(r2);
                owns_payload = payload != NULL;
                if (!payload || uc_mem_read(probe->uc, r1, payload, r2) != UC_ERR_OK)
                    payload = NULL;
            }
            if (payload)
                result = (uint32_t)crc32((uLong)r0, payload, (uInt)r2);
        }
        if (owns_payload) free(payload);
    } else if (strcmp(name, "uncompress") == 0) {
        uint32_t capacity = 0;
        GuestMemoryRegion *source_region = NULL;
        GuestMemoryRegion *destination_region = NULL;
        unsigned char *source = NULL;
        unsigned char *destination = NULL;
        int owns_source = 0;
        int owns_destination = 0;
        uLongf destination_size;
        int zresult = Z_STREAM_ERROR;
        if (r1 && uc_mem_read(probe->uc, r1, &capacity, sizeof(capacity)) ==
                      UC_ERR_OK &&
            capacity <= MAX_GUEST_ZLIB_BUFFER &&
            r3 <= MAX_GUEST_ZLIB_BUFFER) {
            if (r3) {
                if (!r2) goto uncompress_finished;
                source_region = guest_memory_region(probe, r2, r3);
                if (source_region) {
                    source = source_region->host +
                             (size_t)(r2 - source_region->base);
                    ++probe->direct_memory_reads;
                } else {
                    source = (unsigned char *)malloc(r3);
                    owns_source = source != NULL;
                    if (!source || uc_mem_read(probe->uc, r2, source, r3) !=
                                       UC_ERR_OK)
                        goto uncompress_finished;
                }
            }
            if (capacity) {
                if (!r0) goto uncompress_finished;
                destination_region = guest_memory_region(probe, r0, capacity);
                if (destination_region && destination_region->direct_write) {
                    destination = destination_region->host +
                                  (size_t)(r0 - destination_region->base);
                    ++probe->direct_memory_writes;
                } else {
                    destination = (unsigned char *)malloc(capacity);
                    owns_destination = destination != NULL;
                    if (!destination) goto uncompress_finished;
                }
            }
            destination_size = capacity;
            zresult = uncompress(destination, &destination_size, source, r3);
            if (zresult == Z_OK && owns_destination && destination_size &&
                uc_mem_write(probe->uc, r0, destination,
                             destination_size) != UC_ERR_OK)
                zresult = Z_STREAM_ERROR;
            capacity = (uint32_t)destination_size;
            uc_mem_write(probe->uc, r1, &capacity, sizeof(capacity));
        }
uncompress_finished:
        if (owns_source) free(source);
        if (owns_destination) free(destination);
        result = (uint32_t)zresult;
    } else if (strcmp(name, "inflateInit_") == 0) {
        result = (uint32_t)guest_inflate_init(probe, r0, 0, MAX_WBITS);
    } else if (strcmp(name, "inflateInit2_") == 0) {
        result = (uint32_t)guest_inflate_init(probe, r0, 1, (int32_t)r1);
    } else if (strcmp(name, "inflate") == 0) {
        result = (uint32_t)guest_zstream_process(
            probe, r0, GUEST_ZSTREAM_INFLATE, (int32_t)r1);
    } else if (strcmp(name, "inflateReset") == 0) {
        result = (uint32_t)guest_zstream_reset(
            probe, r0, GUEST_ZSTREAM_INFLATE);
    } else if (strcmp(name, "inflateEnd") == 0) {
        result = (uint32_t)guest_zstream_end(
            probe, r0, GUEST_ZSTREAM_INFLATE);
    } else if (strcmp(name, "inflateSync") == 0) {
        result = (uint32_t)guest_inflate_sync(probe, r0);
    } else if (strcmp(name, "inflateCopy") == 0) {
        result = (uint32_t)guest_inflate_copy(probe, r0, r1);
    } else if (strcmp(name, "deflateInit_") == 0) {
        result = (uint32_t)guest_deflate_init(
            probe, r0, (int32_t)r1, 0, Z_DEFLATED, MAX_WBITS,
            8, Z_DEFAULT_STRATEGY);
    } else if (strcmp(name, "deflateInit2_") == 0) {
        uint32_t extra[4] = {0, 0, 0, 0};
        if (uc_mem_read(probe->uc, sp, extra, sizeof(extra)) == UC_ERR_OK) {
            result = (uint32_t)guest_deflate_init(
                probe, r0, (int32_t)r1, 1, (int32_t)r2, (int32_t)r3,
                (int32_t)extra[0], (int32_t)extra[1]);
        } else {
            result = (uint32_t)Z_STREAM_ERROR;
        }
    } else if (strcmp(name, "deflate") == 0) {
        result = (uint32_t)guest_zstream_process(
            probe, r0, GUEST_ZSTREAM_DEFLATE, (int32_t)r1);
    } else if (strcmp(name, "deflateReset") == 0) {
        result = (uint32_t)guest_zstream_reset(
            probe, r0, GUEST_ZSTREAM_DEFLATE);
    } else if (strcmp(name, "deflateEnd") == 0) {
        result = (uint32_t)guest_zstream_end(
            probe, r0, GUEST_ZSTREAM_DEFLATE);
    } else if (strcmp(name, "deflateParams") == 0) {
        result = (uint32_t)guest_deflate_params(
            probe, r0, (int32_t)r1, (int32_t)r2);
    } else if (strcmp(name, "printf") == 0 || strcmp(name, "fprintf") == 0 ||
               strcmp(name, "vfprintf") == 0 || strcmp(name, "__android_log_print") == 0) {
        GuestArgCursor format_arguments;
        uint32_t format_address;
        char formatted[MAX_STRING];
        if (strcmp(name, "printf") == 0) {
            format_address = r0;
            cursor_setup(&format_arguments, probe, r0, r1, r2, r3, sp, 1u, 0);
        } else if (strcmp(name, "fprintf") == 0) {
            format_address = r1;
            cursor_setup(&format_arguments, probe, r0, r1, r2, r3, sp, 2u, 0);
        } else if (strcmp(name, "vfprintf") == 0) {
            format_address = r1;
            cursor_setup(&format_arguments, probe, r0, r1, r2, r3, sp, 0u, r2);
        } else {
            format_address = r2;
            cursor_setup(&format_arguments, probe, r0, r1, r2, r3, sp, 3u, 0);
        }
        if (guest_read_string(probe, format_address, first, sizeof(first))) {
            result = (uint32_t)guest_vformat(probe, formatted, sizeof(formatted),
                                             first, &format_arguments);
            probe_log("  guest stdio: %s", formatted);
        }
    } else if (strcmp(name, "fputs") == 0 || strcmp(name, "puts") == 0) {
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            probe_log("  guest stdio: %s", first);
            result = (uint32_t)strlen(first) + (strcmp(name, "puts") == 0 ? 1u : 0u);
        }
    } else if (strcmp(name, "snprintf") == 0 ||
               strcmp(name, "vsnprintf") == 0 ||
               strcmp(name, "sprintf") == 0 ||
               strcmp(name, "vsprintf") == 0) {
        GuestArgCursor format_arguments;
        uint32_t capacity;
        uint32_t format_address;
        if (strcmp(name, "snprintf") == 0) {
            capacity = r1;
            format_address = r2;
            cursor_setup(&format_arguments, probe, r0, r1, r2, r3, sp, 3u, 0);
        } else if (strcmp(name, "vsnprintf") == 0) {
            capacity = r1;
            format_address = r2;
            cursor_setup(&format_arguments, probe, r0, r1, r2, r3, sp, 0u, r3);
        } else if (strcmp(name, "sprintf") == 0) {
            capacity = MAX_GUEST_FORMAT;
            format_address = r1;
            cursor_setup(&format_arguments, probe, r0, r1, r2, r3, sp, 2u, 0);
        } else {
            capacity = MAX_GUEST_FORMAT;
            format_address = r1;
            cursor_setup(&format_arguments, probe, r0, r1, r2, r3, sp, 0u, r2);
        }
        result = guest_format_to_memory(probe, r0, capacity, format_address,
                                        &format_arguments);
    } else if (strcmp(name, "sscanf") == 0 ||
               strcmp(name, "vsscanf") == 0) {
        GuestArgCursor scan_arguments;
        int scan_result = 0;
        if (guest_read_string(probe, r0, first, sizeof(first)) &&
            guest_read_string(probe, r1, second, sizeof(second))) {
            cursor_setup(&scan_arguments, probe, r0, r1, r2, r3, sp,
                         2u, strcmp(name, "vsscanf") == 0 ? r2 : 0u);
            scan_result = guest_vscan(probe, first, second, &scan_arguments);
            result = (uint32_t)scan_result;
            if (import->calls <= 4u)
                probe_log("  ARM %s: assigned=%d format=%s", name,
                          scan_result, second);
        }
    } else if (strcmp(name, "fopen") == 0) {
        if (guest_read_string(probe, r0, first, sizeof(first)) &&
            guest_read_string(probe, r1, second, sizeof(second)))
            result = guest_open_file(probe, first, second, 0);
    } else if (strcmp(name, "fread") == 0) {
        GuestFile *file = guest_file(probe, r3);
        uint64_t requested = (uint64_t)r1 * r2;
        size_t actual = requested <= SIZE_MAX
                            ? guest_file_read(probe, file, r0, (size_t)requested) : 0;
        result = r1 ? (uint32_t)(actual / r1) : 0;
    } else if (strcmp(name, "fwrite") == 0) {
        GuestFile *file = guest_file(probe, r3);
        uint64_t requested = (uint64_t)r1 * r2;
        if (file) {
            size_t actual = requested <= SIZE_MAX
                                ? guest_file_write(probe, file, r0,
                                                   (size_t)requested) : 0;
            result = r1 ? (uint32_t)(actual / r1) : 0;
        } else if (requested && r0) {
            size_t captured = requested < sizeof(first)
                                  ? (size_t)requested : sizeof(first) - 1u;
            if (uc_mem_read(probe->uc, r0, first, captured) == UC_ERR_OK) {
                size_t character;
                first[captured] = 0;
                for (character = 0; character < captured; ++character) {
                    unsigned char value = (unsigned char)first[character];
                    if (!value) break;
                    if (value == '\r' || value == '\n' || value == '\t')
                        first[character] = ' ';
                    else if (value < 0x20u || value == 0x7fu)
                        first[character] = '.';
                }
                probe_log("  guest stdio fwrite stream=0x%08x: %s%s",
                          r3, first,
                          requested > captured ? " [truncated]" : "");
            }
            /* Bionic's __sF stdin/stdout/stderr objects are guest pointers,
               not handles returned by guest_open_file.  Treat writes to
               those streams as successfully consumed by the wrapper log. */
            result = r1 ? r2 : 0;
        }
    } else if (strcmp(name, "fseek") == 0) {
        result = (uint32_t)guest_file_seek(guest_file(probe, r0), (long)(int32_t)r1, (int)r2);
    } else if (strcmp(name, "ftell") == 0) {
        result = (uint32_t)guest_file_tell(guest_file(probe, r0));
    } else if (strcmp(name, "rewind") == 0) {
        guest_file_seek(guest_file(probe, r0), 0, SEEK_SET);
        result = 0;
    } else if (strcmp(name, "fclose") == 0) {
        result = (uint32_t)guest_file_close(probe, guest_file(probe, r0));
    } else if (strcmp(name, "feof") == 0) {
        GuestFile *file = guest_file(probe, r0);
        result = file ? (uint32_t)file->eof : 1;
    } else if (strcmp(name, "ferror") == 0) {
        GuestFile *file = guest_file(probe, r0);
        result = file && file->host ? (uint32_t)ferror(file->host) : 0;
    } else if (strcmp(name, "fflush") == 0) {
        GuestFile *file = guest_file(probe, r0);
        result = file && file->host ? (uint32_t)fflush(file->host) : 0;
    } else if (strcmp(name, "getc") == 0 || strcmp(name, "fgetc") == 0) {
        result = (uint32_t)guest_file_get_character(guest_file(probe, r0));
    } else if (strcmp(name, "open") == 0) {
        const char *mode = (r1 & 3u) == 0 ? "rb" : (r1 & 3u) == 1 ? "wb" : "r+b";
        if (guest_read_string(probe, r0, first, sizeof(first)))
            result = guest_open_file(probe, first, mode, 0);
    } else if (strcmp(name, "read") == 0) {
        result = (uint32_t)guest_file_read(probe, guest_file(probe, r0), r1, r2);
    } else if (strcmp(name, "write") == 0) {
        result = (uint32_t)guest_file_write(probe, guest_file(probe, r0), r1, r2);
    } else if (strcmp(name, "lseek") == 0) {
        GuestFile *file = guest_file(probe, r0);
        result = guest_file_seek(file, (long)(int32_t)r1, (int)r2) == 0
                     ? (uint32_t)guest_file_tell(file) : (uint32_t)-1;
    } else if (strcmp(name, "close") == 0) {
        result = (uint32_t)guest_file_close(probe, guest_file(probe, r0));
    } else if (strcmp(name, "access") == 0 || strcmp(name, "stat") == 0) {
        char translated[MAX_PATH * 4];
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            translate_guest_path(probe, first, translated, sizeof(translated));
            result = GetFileAttributesA(translated) == INVALID_FILE_ATTRIBUTES
                         ? (uint32_t)-1 : 0;
            if (result == 0 && strcmp(name, "stat") == 0 && r1) {
                unsigned char zero[128] = {0};
                uc_mem_write(probe->uc, r1, zero, sizeof(zero));
            }
        } else result = (uint32_t)-1;
    } else if (strcmp(name, "mkdir") == 0) {
        char translated[MAX_PATH * 4];
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            translate_guest_path(probe, first, translated, sizeof(translated));
            result = CreateDirectoryA(translated, NULL) || GetLastError() == ERROR_ALREADY_EXISTS
                         ? 0 : (uint32_t)-1;
        } else result = (uint32_t)-1;
    } else if (strcmp(name, "unlink") == 0 || strcmp(name, "remove") == 0) {
        char translated[MAX_PATH * 4];
        if (guest_read_string(probe, r0, first, sizeof(first))) {
            translate_guest_path(probe, first, translated, sizeof(translated));
            result = DeleteFileA(translated) ? 0 : (uint32_t)-1;
        } else result = (uint32_t)-1;
    } else if (strcmp(name, "rename") == 0) {
        char first_path[MAX_PATH * 4], second_path[MAX_PATH * 4];
        if (guest_read_string(probe, r0, first, sizeof(first)) &&
            guest_read_string(probe, r1, second, sizeof(second))) {
            translate_guest_path(probe, first, first_path, sizeof(first_path));
            translate_guest_path(probe, second, second_path, sizeof(second_path));
            result = MoveFileExA(first_path, second_path, MOVEFILE_REPLACE_EXISTING)
                         ? 0 : (uint32_t)-1;
        } else result = (uint32_t)-1;
    } else if (strcmp(name, "AAssetManager_fromJava") == 0) {
        result = 1;
    } else if (strcmp(name, "AAssetManager_open") == 0) {
        if (guest_read_string(probe, r1, first, sizeof(first)))
            result = guest_open_file(probe, first, "rb", 1);
    } else if (strcmp(name, "AAsset_read") == 0) {
        result = (uint32_t)guest_file_read(probe, guest_file(probe, r0), r1, r2);
    } else if (strcmp(name, "AAsset_seek") == 0) {
        GuestFile *file = guest_file(probe, r0);
        result = guest_file_seek(file, (long)(int32_t)r1, (int)r2) == 0
                     ? (uint32_t)guest_file_tell(file) : (uint32_t)-1;
    } else if (strcmp(name, "AAsset_getLength") == 0) {
        GuestFile *file = guest_file(probe, r0);
        if (file && file->host) { long old=ftell(file->host); fseek(file->host,0,SEEK_END); result=(uint32_t)ftell(file->host); fseek(file->host,old,SEEK_SET); }
        else result = file ? (uint32_t)file->size : 0;
    } else if (strcmp(name, "AAsset_getRemainingLength") == 0) {
        GuestFile *file = guest_file(probe, r0);
        long position = guest_file_tell(file);
        if (file && file->host) { long old=position; fseek(file->host,0,SEEK_END); result=(uint32_t)(ftell(file->host)-old); fseek(file->host,old,SEEK_SET); }
        else result = file && position >= 0 ? (uint32_t)(file->size-(size_t)position) : 0;
    } else if (strcmp(name, "AAsset_close") == 0) {
        guest_file_close(probe, guest_file(probe, r0)); result = 0;
    } else if (strcmp(name, "getcwd") == 0) {
        const char *value = "/";
        if (r0 && r1) { guest_write_string(probe, r0, value, r1); result = r0; }
    } else if (strcmp(name, "chdir") == 0) {
        result = 0;
    } else if (strcmp(name, "getenv") == 0 || strcmp(name, "dlerror") == 0 ||
               strcmp(name, "dlsym") == 0 || strcmp(name, "fdopen") == 0 ||
               strcmp(name, "gzopen") == 0) {
        result = 0;
    } else if (strcmp(name, "dlopen") == 0) {
        result = 1;
    } else if (strcmp(name, "pthread_once") == 0) {
        int once_result = guest_pthread_once_start(probe, r0, r1);
        if (once_result > 0) return;
        result = once_result < 0 ? 22u : 0u;
    } else if (strcmp(name, "pthread_setspecific") == 0) {
        if (r0 < MAX_PTHREAD_KEYS && r0 < probe->pthread_next_key) {
            probe->pthread_values[r0] = r1;
            result = 0;
        } else {
            result = 22u;
        }
    } else if (strcmp(name, "pthread_key_delete") == 0) {
        if (r0 < MAX_PTHREAD_KEYS && r0 < probe->pthread_next_key) {
            probe->pthread_values[r0] = 0;
            probe->pthread_destructors[r0] = 0;
            result = 0;
        } else {
            result = 22u;
        }
    } else if (strcmp(name, "pthread_mutex_init") == 0 ||
               strcmp(name, "pthread_mutex_destroy") == 0 ||
               strcmp(name, "pthread_mutex_lock") == 0 ||
               strcmp(name, "pthread_mutex_unlock") == 0 ||
               strcmp(name, "pthread_detach") == 0 ||
               strcmp(name, "pthread_join") == 0 ||
               strcmp(name, "pthread_cond_init") == 0 ||
               strcmp(name, "pthread_cond_destroy") == 0 ||
               strcmp(name, "pthread_cond_signal") == 0 ||
               strcmp(name, "pthread_cond_broadcast") == 0 ||
               strcmp(name, "sem_init") == 0 ||
               strcmp(name, "sem_destroy") == 0 ||
               strcmp(name, "sem_post") == 0 ||
               strcmp(name, "sem_wait") == 0 ||
               strcmp(name, "setjmp") == 0 ||
               strcmp(name, "sigsetjmp") == 0 ||
               strcmp(name, "sigaction") == 0 || strcmp(name, "raise") == 0) {
        result = 0;
    } else if (strcmp(name, "pthread_key_create") == 0) {
        uint32_t key;
        if (!probe->pthread_next_key) probe->pthread_next_key = 1u;
        key = probe->pthread_next_key;
        if (!r0 || key >= MAX_PTHREAD_KEYS ||
            uc_mem_write(probe->uc, r0, &key, sizeof(key)) != UC_ERR_OK) {
            result = 11u;
        } else {
            probe->pthread_destructors[key] = r1;
            probe->pthread_values[key] = 0;
            ++probe->pthread_next_key;
            result = 0;
        }
    } else if (strcmp(name, "pthread_getspecific") == 0) {
        result = r0 < MAX_PTHREAD_KEYS && r0 < probe->pthread_next_key
                     ? probe->pthread_values[r0] : 0;
    } else if (strcmp(name, "srand48") == 0) {
        probe->lrand48_state =
            (((uint64_t)r0 << 16u) | UINT64_C(0x330e)) &
            UINT64_C(0x0000ffffffffffff);
        result = 0;
    } else if (strcmp(name, "lrand48") == 0) {
        if (!probe->lrand48_state)
            probe->lrand48_state = UINT64_C(0x1234abcd330e);
        probe->lrand48_state =
            (UINT64_C(0x5deece66d) * probe->lrand48_state + UINT64_C(0xb)) &
            UINT64_C(0x0000ffffffffffff);
        result = (uint32_t)(probe->lrand48_state >> 17u);
    } else if (strcmp(name, "time") == 0 ||
               strcmp(name, "arc4random") == 0) {
        result = (uint32_t)time(NULL);
        if (r0 && strcmp(name, "time") == 0)
            uc_mem_write(probe->uc, r0, &result, sizeof(result));
    } else if (strcmp(name, "clock") == 0) {
        result = (uint32_t)host_monotonic_units(UINT64_C(1000000));
    } else if (strcmp(name, "gettimeofday") == 0) {
        uint64_t microseconds = host_unix_microseconds();
        uint32_t values[2] = {
            (uint32_t)(microseconds / UINT64_C(1000000)),
            (uint32_t)(microseconds % UINT64_C(1000000))
        };
        static const uint32_t timezone[2] = {0, 0};
        if (r0) uc_mem_write(probe->uc, r0, values, sizeof(values));
        if (r1) uc_mem_write(probe->uc, r1, timezone, sizeof(timezone));
        result = 0;
    } else if (strcmp(name, "clock_gettime") == 0) {
        uint64_t nanoseconds = r0 == 0
                                   ? host_unix_microseconds() * UINT64_C(1000)
                                   : host_monotonic_units(UINT64_C(1000000000));
        uint32_t values[2] = {
            (uint32_t)(nanoseconds / UINT64_C(1000000000)),
            (uint32_t)(nanoseconds % UINT64_C(1000000000))
        };
        if (r1) uc_mem_write(probe->uc, r1, values, sizeof(values));
        result = 0;
    } else if (strcmp(name, "ftime") == 0) {
        result = (uint32_t)guest_ftime(probe, r0);
    } else if (strcmp(name, "localtime") == 0 ||
               strcmp(name, "gmtime") == 0) {
        result = guest_time_to_tm(probe, r0, 0,
                                  strcmp(name, "localtime") == 0);
    } else if (strcmp(name, "localtime_r") == 0 ||
               strcmp(name, "gmtime_r") == 0) {
        result = r1 ? guest_time_to_tm(
                          probe, r0, r1,
                          strcmp(name, "localtime_r") == 0) : 0;
    } else if (strcmp(name, "strftime") == 0) {
        struct tm host_tm;
        size_t capacity = r1 < sizeof(second) ? (size_t)r1 : sizeof(second);
        if (r0 && capacity &&
            guest_read_string(probe, r2, first, sizeof(first)) &&
            guest_tm_read(probe, r3, &host_tm)) {
            second[0] = 0;
            result = (uint32_t)strftime(second, capacity, first, &host_tm);
            if (result || !first[0])
                uc_mem_write(probe->uc, r0, second, (size_t)result + 1u);
        }
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
        result = dispatch_gl(probe, import, r0, r1, r2, r3, sp);
    } else if (strcmp(name, "abort") == 0 || strcmp(name, "exit") == 0 ||
               strcmp(name, "__stack_chk_fail") == 0 ||
               strcmp(name, "longjmp") == 0 ||
               strcmp(name, "siglongjmp") == 0 ||
               strcmp(name, "pthread_exit") == 0) {
        uint32_t pc = 0, lr = 0;
        uint32_t stack_words[6] = {0, 0, 0, 0, 0, 0};
        int have_stack = sp &&
            uc_mem_read(probe->uc, sp, stack_words,
                        sizeof(stack_words)) == UC_ERR_OK;
        uc_reg_read(probe->uc, UC_ARM_REG_PC, &pc);
        uc_reg_read(probe->uc, UC_ARM_REG_LR, &lr);
        probe_log("ARM fatal import %s: pc=0x%08x lr=0x%08x "
                  "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x "
                  "sp=0x%08x stack=%08x,%08x,%08x,%08x,%08x,%08x%s",
                  name, pc, lr, r0, r1, r2, r3, sp,
                  stack_words[0], stack_words[1], stack_words[2],
                  stack_words[3], stack_words[4], stack_words[5],
                  have_stack ? "" : " [unreadable]");
        probe->failed = 1;
        snprintf(probe->failure, sizeof(probe->failure),
                 "guest called fatal import %s pc=0x%08x lr=0x%08x "
                 "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x "
                 "sp=0x%08x",
                 name, pc, lr, r0, r1, r2, r3, sp);
        uc_emu_stop(probe->uc);
        return;
    } else {
        /* The probe intentionally keeps unknown services deterministic.  The
           first-use line above makes every missing semantic visible. */
        result = 0;
    }
import_done:
    set_r0(probe->uc, result);
}

static unsigned import_register_count(const ArmImport *import) {
    unsigned arguments;
    if (!import) return 5u;
    switch (import->kind) {
    case IMPORT_MALLOC:
    case IMPORT_FREE:
    case IMPORT_STRLEN:
        return 1u;
    case IMPORT_CALLOC:
    case IMPORT_REALLOC:
    case IMPORT_STRCMP:
        return 2u;
    case IMPORT_MEMCPY:
    case IMPORT_MEMMOVE:
    case IMPORT_MEMSET:
    case IMPORT_MEMCMP:
    case IMPORT_STRNCMP:
        return 3u;
    case IMPORT_PTHREAD_NOOP:
    case IMPORT_LRAND48:
        return 0u;
    case IMPORT_FREAD:
        return 4u;
    case IMPORT_SIN:
    case IMPORT_COS:
    case IMPORT_ACOS:
        return 2u;
    case IMPORT_ATAN2:
    case IMPORT_POW:
    case IMPORT_FMOD:
        return 4u;
    case IMPORT_AEABI_UIDIV:
    case IMPORT_AEABI_IDIV:
    case IMPORT_AEABI_UIDIVMOD:
    case IMPORT_AEABI_IDIVMOD:
        return 2u;
    case IMPORT_OPENGL:
        arguments = import->gl_argument_count;
        if (arguments == 0xffu || arguments == 0xfeu)
            arguments = gl_argument_count(import->name);
        if (arguments == UINT32_MAX) return 5u;
        return arguments > 4u ? 5u : arguments;
    default:
        return 5u;
    }
}

static int read_import_registers(uc_engine *uc, const ArmImport *import,
                                 uint32_t *r0, uint32_t *r1,
                                 uint32_t *r2, uint32_t *r3,
                                 uint32_t *sp) {
    static const int registers[5] = {
        UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
        UC_ARM_REG_R3, UC_ARM_REG_SP
    };
    void *values[5] = {r0, r1, r2, r3, sp};
    unsigned count = import_register_count(import);
    return !count ||
        uc_reg_read_batch(uc, registers, values, (int)count) == UC_ERR_OK;
}

static void import_hook(uc_engine *uc, uint64_t address, uint32_t size,
                        void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0, sp = 0;
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
    {
        LARGE_INTEGER timing_start = {0};
        LARGE_INTEGER timing_end = {0};
        int timing = probe->profile_import_timing &&
                     QueryPerformanceCounter(&timing_start);
        if (!read_import_registers(uc, &probe->imports[index],
                                   &r0, &r1, &r2, &r3, &sp)) {
            probe->failed = 1;
            snprintf(probe->failure, sizeof(probe->failure),
                     "cannot read ARM import registers");
            uc_emu_stop(uc);
            return;
        }
        dispatch_import(probe, &probe->imports[index], r0, r1, r2, r3, sp);
        if (timing && QueryPerformanceCounter(&timing_end) &&
            timing_end.QuadPart >= timing_start.QuadPart)
            probe->profile_import_ticks[index] +=
                (uint64_t)(timing_end.QuadPart - timing_start.QuadPart);
    }
}

static int install_import_hooks(ArmProbe *probe) {
    uc_hook hook;
    unsigned index = 0;
    unsigned hook_spans = 0;
    while (index < probe->import_count) {
        unsigned first;
        unsigned last;
        while (index < probe->import_count &&
               probe->imports[index].kind == IMPORT_PTHREAD_NOOP)
            ++index;
        if (index >= probe->import_count) break;
        first = index;
        while (index < probe->import_count &&
               probe->imports[index].kind != IMPORT_PTHREAD_NOOP)
            ++index;
        last = index - 1u;
        if (uc_hook_add(probe->uc, &hook, UC_HOOK_CODE,
                        import_hook, probe,
                        GUEST_IMPORT_BASE + first * 4u,
                        GUEST_IMPORT_BASE + last * 4u + 3u) != UC_ERR_OK)
            return 0;
        ++hook_spans;
    }
    probe_log("ARM fast pthread stubs ready: hook-spans=%u", hook_spans);
    return 1;
}

static int zip_path_is_current_apk(const ArmProbe *probe,
                                   const char *zip_path) {
    char normalized[MAX_PATH * 2];
    if (!probe || !zip_path || !*zip_path) return 0;
    snprintf(normalized, sizeof(normalized), "%s", zip_path);
    path_slashes(normalized);
    return _stricmp(normalized, probe->input_path) == 0;
}

static void normalize_apk_member_name(const char *source, char *destination,
                                      size_t capacity) {
    size_t index;
    while (source && (*source == '/' || *source == '\\')) ++source;
    snprintf(destination, capacity, "%s", source ? source : "");
    for (index = 0; destination[index]; ++index)
        if (destination[index] == '\\') destination[index] = '/';
}

static const ApkIndexEntry *apk_index_find_variant(const ArmProbe *probe,
                                                    const char *member_name,
                                                    char *resolved,
                                                    size_t capacity) {
    const ApkIndexEntry *entry;
    normalize_apk_member_name(member_name, resolved, capacity);
    entry = apk_index_find(probe, resolved);
    if (!entry && !path_has_prefix(resolved, "assets/")) {
        char with_assets[MAX_PATH * 4];
        snprintf(with_assets, sizeof(with_assets), "assets/%s", resolved);
        entry = apk_index_find(probe, with_assets);
        if (entry) snprintf(resolved, capacity, "%s", with_assets);
    }
    return entry;
}

static int guest_write_host_bytes(ArmProbe *probe, uint32_t destination,
                                  const void *source, size_t size) {
    GuestMemoryRegion *region;
    if (!size) return 1;
    region = guest_memory_region(probe, destination, size);
    if (region && region->direct_write) {
        memcpy(region->host + (size_t)(destination - region->base),
               source, size);
        ++probe->direct_memory_writes;
        return 1;
    }
    return uc_mem_write(probe->uc, destination, source, size) == UC_ERR_OK;
}

static void return_from_guest_function(uc_engine *uc, uint32_t result) {
    uint32_t lr = 0;
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    uc_reg_write(uc, UC_ARM_REG_R0, &result);
    uc_reg_write(uc, UC_ARM_REG_PC, &lr);
}

static int overkill_prepare_patch(ArmProbe *probe, GuestNoopPatch *patch,
                                  const char *name, int return_zero) {
    uint32_t address = find_export(probe, name) & ~1u;
    if (!address ||
        uc_mem_read(probe->uc, address, patch->original,
                    sizeof(patch->original)) != UC_ERR_OK)
        return 0;
    patch->name = name;
    patch->address = address;
    patch->return_zero = return_zero;
    patch->ready = 1;
    return 1;
}

static int overkill_apply_patch(ArmProbe *probe, GuestNoopPatch *patch,
                                int enabled) {
    static const unsigned char thumb_void_return[4] = {0x70,0x47,0xc0,0x46};
    static const unsigned char thumb_zero_return[4] = {0x00,0x20,0x70,0x47};
    const unsigned char *bytes;
    uc_err error;
    if (!patch || !patch->ready || patch->active == enabled) return 1;
    bytes = enabled ? (patch->return_zero ? thumb_zero_return : thumb_void_return)
                    : patch->original;
    {
        int guard_was_enabled = probe->host_write_guard_enabled;
        /* These are deliberate diagnostic code patches. The immutable-image
           host guard is meant to catch accidental corruption, but in
           OverkillTest2 it also blocked every runtime F4/F5/F7 toggle. */
        probe->host_write_guard_enabled = 0;
        error = uc_mem_write(probe->uc, patch->address, bytes, 4u);
        probe->host_write_guard_enabled = guard_was_enabled;
    }
    if (error != UC_ERR_OK) {
        probe_log("ERROR: OVERKILL patch write failed: %s address=0x%08x "
                  "symbol=%s", uc_strerror(error), patch->address,
                  patch->name ? patch->name : "?");
        return 0;
    }
    /* uc_ctl_remove_cache is variadic and consumes uint64_t arguments.
       Explicit casts are mandatory for the 32-bit Windows host build; passing
       uint32_t here makes va_arg combine adjacent stack arguments and causes
       UC_ERR_ARG before the game can start. */
    error = uc_ctl_remove_cache(probe->uc, (uint64_t)patch->address,
                                (uint64_t)patch->address + 4u);
    if (error != UC_ERR_OK) {
        probe_log("ERROR: OVERKILL patch cache invalidation failed: %s "
                  "address=0x%08x symbol=%s", uc_strerror(error),
                  patch->address, patch->name ? patch->name : "?");
        return 0;
    }
    patch->active = enabled;
    return 1;
}

static int overkill_set_particles(ArmProbe *probe, int disabled) {
    unsigned index;
    unsigned applied = 0;
    unsigned failed = 0;
    for (index = 0; index < probe->overkill_particle_patch_count; ++index) {
        if (overkill_apply_patch(probe,
                                 &probe->overkill_particle_patches[index],
                                 disabled))
            ++applied;
        else
            ++failed;
    }
    probe->disable_particles = disabled;
    ++probe->overkill_particle_toggles;
    probe_log("OVERKILL particles/cosmetics: %s (applied=%u failed=%u)",
              disabled ? "REMOVED" : "restored", applied, failed);
    if (failed)
        probe_log("WARNING: OVERKILL continued with %u unavailable cosmetic "
                  "patches", failed);
    return 1;
}

static int overkill_set_node_draws(ArmProbe *probe, int disabled) {
    unsigned index;
    unsigned applied = 0;
    unsigned failed = 0;
    for (index = 0; index < probe->overkill_node_draw_patch_count; ++index) {
        if (overkill_apply_patch(probe,
                                 &probe->overkill_node_draw_patches[index],
                                 disabled))
            ++applied;
        else
            ++failed;
    }
    probe->disable_node_draws = disabled;
    ++probe->overkill_node_draw_toggles;
    probe_log("OVERKILL ARM node draw functions: %s (applied=%u failed=%u)",
              disabled ? "REMOVED" : "restored", applied, failed);
    if (failed)
        probe_log("WARNING: OVERKILL continued with %u unavailable draw "
                  "patches", failed);
    return 1;
}

static int overkill_set_scene_visit(ArmProbe *probe, int disabled) {
    if (!probe->overkill_scene_visit_patch.ready) {
        probe_log("WARNING: OVERKILL scene traversal patch unavailable; "
                  "continuing");
        return 1;
    }
    if (!overkill_apply_patch(probe, &probe->overkill_scene_visit_patch,
                              disabled)) {
        probe_log("WARNING: OVERKILL scene traversal toggle failed; "
                  "continuing");
        return 1;
    }
    probe->skip_scene_visit = disabled;
    ++probe->overkill_scene_toggles;
    probe_log("OVERKILL scene traversal: %s",
              disabled ? "REMOVED (logic-only diagnostic)" : "restored");
    return 1;
}

static int overkill_prepare_patches(ArmProbe *probe) {
    struct PatchDescription { const char *name; int return_zero; };
    static const struct PatchDescription particle_patches[] = {
        {"_ZN7cocos2d16CCParticleSystem6updateEf", 0},
        {"_ZN7cocos2d16CCParticleSystem11addParticleEv", 1},
        {"_ZN7cocos2d16CCParticleSystem8isActiveEv", 1},
        {"_ZN7cocos2d16CCParticleSystem16getParticleCountEv", 1},
        {"_ZN7cocos2d16CCParticleSystem16updateWithNoTimeEv", 0},
        {"_ZN7cocos2d20CCParticleSystemQuad4drawEv", 0},
        {"_ZN7cocos2d20CCParticleSystemQuad8postStepEv", 0},
        {"_ZN7cocos2d19CCParticleBatchNode4drawEv", 0},
        {"_ZN7cocos2d19CCParticleBatchNode5visitEv", 0},
        {"_ZN7cocos2d14CCMotionStreak6updateEf", 0},
        {"_ZN7cocos2d14CCMotionStreak4drawEv", 0},
        {"_ZN16GhostTrailEffect13trailSnapshotEv", 0},
        {"_ZN16GhostTrailEffect4drawEv", 0},
        {"_ZN12PlayerObject14activateStreakEv", 0},
        {"_ZN12CCCircleWave4drawEv", 0},
        {"_ZN12CCLightStrip4drawEv", 0},
        {"_ZN13GJGroundLayer4drawEv", 0},
        {"_ZN18GameEffectsManager13scaleParticleEPN7cocos2d20CCParticleSystemQuadEf", 0}
    };
    unsigned index;
    for (index = 0; index < sizeof(particle_patches) /
                                  sizeof(particle_patches[0]); ++index) {
        GuestNoopPatch *patch;
        if (probe->overkill_particle_patch_count >= MAX_OVERKILL_PATCHES) break;
        patch = &probe->overkill_particle_patches[
            probe->overkill_particle_patch_count];
        if (overkill_prepare_patch(probe, patch, particle_patches[index].name,
                                   particle_patches[index].return_zero))
            ++probe->overkill_particle_patch_count;
    }
    {
        static const char *const draw_patches[] = {
            "_ZN7cocos2d8CCSprite4drawEv",
            "_ZN7cocos2d17CCSpriteBatchNode4drawEv",
            "_ZN7cocos2d11CCAtlasNode4drawEv",
            "_ZN7cocos2d12CCLayerColor4drawEv",
            "_ZN7cocos2d15CCProgressTimer4drawEv",
            "_ZN7cocos2d14CCTextFieldTTF4drawEv",
            "_ZN7cocos2d11CCLightning4drawEv"
        };
        for (index = 0; index < sizeof(draw_patches) / sizeof(draw_patches[0]);
             ++index) {
            GuestNoopPatch *patch;
            if (probe->overkill_node_draw_patch_count >= MAX_OVERKILL_PATCHES)
                break;
            patch = &probe->overkill_node_draw_patches[
                probe->overkill_node_draw_patch_count];
            if (overkill_prepare_patch(probe, patch, draw_patches[index], 0))
                ++probe->overkill_node_draw_patch_count;
        }
    }
    if (!overkill_prepare_patch(probe, &probe->overkill_scene_visit_patch,
                                "_ZN7cocos2d6CCNode5visitEv", 0))
        probe_log("WARNING: OVERKILL CCNode::visit patch unavailable");
    probe_log("OVERKILL guest patch table ready: cosmetics=%u draws=%u scene=%s",
              probe->overkill_particle_patch_count,
              probe->overkill_node_draw_patch_count,
              probe->overkill_scene_visit_patch.ready ? "ready" : "missing");
    if (probe->disable_particles && !overkill_set_particles(probe, 1)) return 0;
    if (probe->disable_node_draws && !overkill_set_node_draws(probe, 1)) return 0;
    if (probe->skip_scene_visit && !overkill_set_scene_visit(probe, 1)) return 0;
    return 1;
}

static void overkill_log_state(ArmProbe *probe) {
    probe_log("OVERKILL state: audio=%s particles=%s arm-draw=%s host-draw=%s "
              "scene=%s native-render=%s headless-gl=%s textures=%s vsync=%s",
              probe->disable_audio ? "REMOVED" : "on",
              probe->disable_particles ? "REMOVED" : "on",
              probe->disable_node_draws ? "REMOVED" : "on",
              probe->skip_draw_calls ? "REMOVED" : "on",
              probe->skip_scene_visit ? "REMOVED" : "on",
              probe->skip_native_render ? "REMOVED" : "on",
              probe->headless_gl ? "REMOVED" : "on",
              probe->skip_texture_uploads ? "REMOVED" :
                  (probe->solid_textures ? "1x1 solid" : "on"),
              probe->disable_vsync ? "off" : "on");
}

static const unsigned char g_overkill_white_png[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
    0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
    0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,
    0x0d,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0xf8,0xff,0xff,0xff,
    0x7f,0x00,0x09,0xfb,0x03,0xfd,0xf5,0xd8,0xf1,0x9a,0x00,0x00,
    0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};

static int overkill_is_png_member(const char *name) {
    size_t length = name ? strlen(name) : 0;
    return length >= 4u && _stricmp(name + length - 4u, ".png") == 0;
}

static void zip_get_file_data_hook(uc_engine *uc, uint64_t address,
                                   uint32_t size, void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t zip_address = 0, member_address = 0, size_address = 0;
    char zip_path[MAX_PATH * 2];
    char member_name[MAX_PATH * 4];
    char resolved[MAX_PATH * 4];
    const ApkIndexEntry *entry;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    int owned = 0;
    uint32_t guest_payload = 0;
    uint32_t guest_size = 0;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R1, &zip_address);
    uc_reg_read(uc, UC_ARM_REG_R2, &member_address);
    uc_reg_read(uc, UC_ARM_REG_R3, &size_address);
    if (!size_address ||
        !guest_read_string(probe, zip_address, zip_path, sizeof(zip_path)) ||
        !zip_path_is_current_apk(probe, zip_path) ||
        !guest_read_string(probe, member_address, member_name,
                           sizeof(member_name))) {
        ++probe->zip_accel_fallbacks;
        return;
    }
    entry = apk_index_find_variant(probe, member_name, resolved,
                                   sizeof(resolved));
    if (!entry) {
        guest_size = 0;
        if (uc_mem_write(uc, size_address, &guest_size,
                         sizeof(guest_size)) == UC_ERR_OK) {
            ++probe->zip_accel_misses;
            return_from_guest_function(uc, 0);
        } else {
            ++probe->zip_accel_fallbacks;
        }
        return;
    }
    if ((probe->solid_textures || probe->skip_texture_uploads ||
         probe->headless_gl) && overkill_is_png_member(resolved)) {
        payload = (unsigned char *)g_overkill_white_png;
        payload_size = sizeof(g_overkill_white_png);
        owned = 0;
        ++probe->overkill_texture_skips;
        if (probe->overkill_texture_skips == 1u)
            probe_log("OVERKILL original PNG assets replaced by 1x1 white PNG");
    } else if (!apk_extract_member_cached(probe, resolved, &payload,
                                          &payload_size, &owned) ||
               !payload || !payload_size || payload_size > UINT32_MAX) {
        if (owned) free(payload);
        ++probe->zip_accel_fallbacks;
        return;
    }
    guest_payload = guest_alloc(probe, (uint32_t)payload_size);
    guest_size = (uint32_t)payload_size;
    if (!guest_payload ||
        !guest_write_host_bytes(probe, guest_payload, payload, payload_size) ||
        uc_mem_write(uc, size_address, &guest_size,
                     sizeof(guest_size)) != UC_ERR_OK) {
        if (guest_payload) guest_free(probe, guest_payload);
        if (owned) free(payload);
        ++probe->zip_accel_fallbacks;
        return;
    }
    if (owned) free(payload);
    ++probe->zip_accel_get_hits;
    if (probe->zip_accel_get_hits == 1u)
        probe_log("ARM indexed ZIP accelerator active: %s (%u bytes)",
                  resolved, guest_size);
    return_from_guest_function(uc, guest_payload);
}

static void zip_exist_file_data_hook(uc_engine *uc, uint64_t address,
                                     uint32_t size, void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t zip_address = 0, member_address = 0;
    char zip_path[MAX_PATH * 2];
    char member_name[MAX_PATH * 4];
    char resolved[MAX_PATH * 4];
    const ApkIndexEntry *entry;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R1, &zip_address);
    uc_reg_read(uc, UC_ARM_REG_R2, &member_address);
    if (!guest_read_string(probe, zip_address, zip_path, sizeof(zip_path)) ||
        !zip_path_is_current_apk(probe, zip_path) ||
        !guest_read_string(probe, member_address, member_name,
                           sizeof(member_name))) {
        ++probe->zip_accel_fallbacks;
        return;
    }
    entry = apk_index_find_variant(probe, member_name, resolved,
                                   sizeof(resolved));
    if (entry) ++probe->zip_accel_exist_hits;
    else ++probe->zip_accel_misses;
    return_from_guest_function(uc, entry ? 1u : 0u);
}

static unsigned char png_paeth_predictor(unsigned char left,
                                         unsigned char above,
                                         unsigned char upper_left) {
    int prediction = (int)left + (int)above - (int)upper_left;
    int left_distance = abs(prediction - (int)left);
    int above_distance = abs(prediction - (int)above);
    int upper_left_distance = abs(prediction - (int)upper_left);
    if (left_distance <= above_distance &&
        left_distance <= upper_left_distance)
        return left;
    if (above_distance <= upper_left_distance) return above;
    return upper_left;
}

static int png_filter_guest_row(ArmProbe *probe, uint32_t row_info_address,
                                uint32_t row_address,
                                uint32_t previous_address,
                                uint32_t filter) {
    GuestPngRowInfo row_info;
    unsigned char *row = NULL;
    unsigned char *previous = NULL;
    uint32_t bytes_per_pixel;
    uint32_t index;
    if (!row_info_address ||
        uc_mem_read(probe->uc, row_info_address, &row_info,
                    sizeof(row_info)) != UC_ERR_OK ||
        row_info.rowbytes > MAX_GL_CLIENT_ARRAY_BYTES ||
        !row_info.pixel_depth || filter > 4u)
        return 0;
    if (!row_info.rowbytes || filter == 0u) return 1;
    if (!row_address) return 0;
    bytes_per_pixel = ((uint32_t)row_info.pixel_depth + 7u) >> 3u;
    if (!bytes_per_pixel) bytes_per_pixel = 1u;
    row = (unsigned char *)guest_buffer_copy(
        probe, row_address, row_info.rowbytes);
    if (!row) return 0;
    if (previous_address && filter >= 2u) {
        previous = (unsigned char *)guest_buffer_copy(
            probe, previous_address, row_info.rowbytes);
        if (!previous) goto failed;
    }

    if (filter == 1u) { /* PNG_FILTER_VALUE_SUB */
        for (index = bytes_per_pixel; index < row_info.rowbytes; ++index)
            row[index] = (unsigned char)(row[index] +
                                         row[index - bytes_per_pixel]);
    } else if (filter == 2u) { /* PNG_FILTER_VALUE_UP */
        if (previous)
            for (index = 0; index < row_info.rowbytes; ++index)
                row[index] = (unsigned char)(row[index] + previous[index]);
    } else if (filter == 3u) { /* PNG_FILTER_VALUE_AVG */
        for (index = 0; index < row_info.rowbytes; ++index) {
            unsigned left = index >= bytes_per_pixel
                                ? row[index - bytes_per_pixel] : 0u;
            unsigned above = previous ? previous[index] : 0u;
            row[index] = (unsigned char)(row[index] +
                                         ((left + above) >> 1u));
        }
    } else { /* PNG_FILTER_VALUE_PAETH */
        for (index = 0; index < row_info.rowbytes; ++index) {
            unsigned char left = index >= bytes_per_pixel
                                     ? row[index - bytes_per_pixel] : 0;
            unsigned char above = previous ? previous[index] : 0;
            unsigned char upper_left =
                previous && index >= bytes_per_pixel
                    ? previous[index - bytes_per_pixel] : 0;
            row[index] = (unsigned char)(
                row[index] + png_paeth_predictor(left, above, upper_left));
        }
    }
    if (uc_mem_write(probe->uc, row_address, row,
                     row_info.rowbytes) != UC_ERR_OK)
        goto failed;
    ++probe->png_filter_rows;
    if (probe->png_filter_rows == 1u)
        probe_log("ARM libpng row-filter acceleration active: "
                  "rowbytes=%u bpp=%u filter=%u",
                  row_info.rowbytes, bytes_per_pixel, filter);
    else if ((probe->png_filter_rows & 2047u) == 0u)
        probe_log("ARM libpng accelerated rows: %u",
                  probe->png_filter_rows);
    free(previous);
    free(row);
    return 1;

failed:
    free(previous);
    free(row);
    return 0;
}

static void png_filter_hook(uc_engine *uc, uint64_t address, uint32_t size,
                            void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t row_info = 0, row = 0, previous = 0, sp = 0, filter = 0;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R1, &row_info);
    uc_reg_read(uc, UC_ARM_REG_R2, &row);
    uc_reg_read(uc, UC_ARM_REG_R3, &previous);
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    if (!sp || uc_mem_read(uc, sp, &filter, sizeof(filter)) != UC_ERR_OK ||
        !png_filter_guest_row(probe, row_info, row, previous, filter)) {
        probe->failed = 1;
        snprintf(probe->failure, sizeof(probe->failure),
                 "libpng row-filter bridge failed row_info=0x%08x "
                 "row=0x%08x previous=0x%08x filter=%u",
                 row_info, row, previous, filter);
        uc_emu_stop(uc);
    }
}

static void png_error_hook(uc_engine *uc, uint64_t address, uint32_t size,
                           void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t png = 0, message_address = 0, lr = 0;
    char message[2048];
    unsigned active = 0;
    unsigned index;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R0, &png);
    uc_reg_read(uc, UC_ARM_REG_R1, &message_address);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    for (index = 0; index < probe->allocation_count; ++index)
        if ((probe->allocations[index].size & GUEST_ALLOCATION_FREE) == 0u)
            ++active;
    if (!guest_read_string(probe, message_address, message, sizeof(message)))
        snprintf(message, sizeof(message), "<unreadable at 0x%08x>",
                 message_address);
    probe_log("ARM libpng error: png=0x%08x caller=0x%08x message=%s "
              "heap_used=%u active_allocations=%u records=%u failures=%u",
              png, lr, message, probe->heap_next - GUEST_HEAP_BASE,
              active, probe->allocation_count, probe->allocation_failures);
}

static void ds_step_entry_hook(uc_engine *uc, uint64_t address, uint32_t size,
                               void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t key_address = 0, lr = 0;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R1, &key_address);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    ++probe->ds_step_calls;
    probe->ds_seen_count = 0;
    memset(probe->ds_seen_nodes, 0, sizeof(probe->ds_seen_nodes));
    if (!guest_read_string(probe, key_address, probe->ds_step_key,
                           sizeof(probe->ds_step_key)))
        snprintf(probe->ds_step_key, sizeof(probe->ds_step_key),
                 "<unreadable at 0x%08x>", key_address);
    if (probe->ds_step_calls <= 12u)
        probe_log("ARM DS_Dictionary step: call=%llu key=%s caller=0x%08x",
                  (unsigned long long)probe->ds_step_calls,
                  probe->ds_step_key, lr);
}

static void ds_step_loop_hook(uc_engine *uc, uint64_t address, uint32_t size,
                              void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t sp = 0, node = 0;
    uint32_t slot;
    unsigned probes;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    ++probe->ds_step_iterations;
    ++probe->ds_seen_count;
    if (!sp || uc_mem_read(uc, sp + 0x1cu, &node, sizeof(node)) != UC_ERR_OK ||
        !node)
        return;
    slot = (node >> 4u) & (MAX_DS_SEEN_NODES - 1u);
    for (probes = 0; probes < MAX_DS_SEEN_NODES; ++probes) {
        uint32_t *seen = &probe->ds_seen_nodes[slot];
        if (!*seen) {
            *seen = node;
            return;
        }
        if (*seen == node) {
            uint32_t zero = 0;
            ++probe->ds_step_cycles;
            probe_log("ARM DS_Dictionary XML cycle stopped: key=%s "
                      "node=0x%08x iteration=%u cycles=%llu",
                      probe->ds_step_key, node, probe->ds_seen_count,
                      (unsigned long long)probe->ds_step_cycles);
            uc_mem_write(uc, sp + 0x1cu, &zero, sizeof(zero));
            return;
        }
        slot = (slot + 1u) & (MAX_DS_SEEN_NODES - 1u);
    }
}

static int compatibility_particle_retain(ArmProbe *probe, uint32_t particle) {
    uint32_t retain_count = 0;
    unsigned index;
    if (!particle ||
        probe->compatibility_particle_hold_count >=
            MAX_COMPAT_PARTICLE_HOLDS ||
        uc_mem_read(probe->uc, particle + 0x0cu, &retain_count,
                    sizeof(retain_count)) != UC_ERR_OK ||
        !retain_count || retain_count == UINT32_MAX)
        return 0;
    for (index = 0; index < probe->compatibility_particle_hold_count;
         ++index)
        if (probe->compatibility_particle_holds[index] == particle)
            return 0;
    ++retain_count;
    if (uc_mem_write(probe->uc, particle + 0x0cu, &retain_count,
                     sizeof(retain_count)) != UC_ERR_OK)
        return 0;
    probe->compatibility_particle_holds[
        probe->compatibility_particle_hold_count++] = particle;
    return 1;
}

static int compatibility_particle_release(ArmProbe *probe,
                                          uint32_t particle) {
    uint32_t retain_count = 0;
    unsigned index;
    for (index = 0; index < probe->compatibility_particle_hold_count;
         ++index) {
        if (probe->compatibility_particle_holds[index] != particle) continue;
        if (uc_mem_read(probe->uc, particle + 0x0cu, &retain_count,
                        sizeof(retain_count)) != UC_ERR_OK ||
            retain_count <= 1u)
            return 0;
        --retain_count;
        if (uc_mem_write(probe->uc, particle + 0x0cu, &retain_count,
                         sizeof(retain_count)) != UC_ERR_OK)
            return 0;
        --probe->compatibility_particle_hold_count;
        probe->compatibility_particle_holds[index] =
            probe->compatibility_particle_holds[
                probe->compatibility_particle_hold_count];
        probe->compatibility_particle_holds[
            probe->compatibility_particle_hold_count] = 0;
        return 1;
    }
    return 0;
}

static void claim_particle_guard_hook(uc_engine *uc, uint64_t address,
                                      uint32_t size, void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t destination_array = 0;
    uint32_t source_array = 0;
    uint32_t particle = 0;
    uint32_t resume;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R5, &destination_array);
    if (destination_array) return;
    uc_reg_read(uc, UC_ARM_REG_R4, &source_array);
    uc_reg_read(uc, UC_ARM_REG_R6, &particle);
    resume = (GUEST_IMAGE_BASE +
              (particle && compatibility_particle_retain(probe, particle)
                   ? CLAIM_PARTICLE_REMOVE_SOURCE_OFFSET
                   : CLAIM_PARTICLE_NULL_RETURN_OFFSET)) | 1u;
    uc_reg_write(uc, UC_ARM_REG_PC, &resume);
    ++probe->particle_claim_guards;
    if (probe->particle_claim_guards <= 8u)
        probe_log("ARM PlayLayer particle claim compatibility: missing "
                  "destination pool, particle=%s source=0x%08x "
                  "particle=0x%08x holds=%u records=%u/%u free=%u",
                  resume == ((GUEST_IMAGE_BASE +
                              CLAIM_PARTICLE_REMOVE_SOURCE_OFFSET) | 1u)
                      ? "retained and removed from free pool"
                      : "suppressed because ownership could not be retained",
                  source_array, particle,
                  probe->compatibility_particle_hold_count,
                  probe->allocation_count, MAX_ALLOCS,
                  probe->free_allocation_count);
}

static void unclaim_particle_guard_hook(uc_engine *uc, uint64_t address,
                                        uint32_t size, void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t destination_array = 0;
    uint32_t particle = 0;
    int released;
    uint32_t resume = (GUEST_IMAGE_BASE +
                       UNCLAIM_PARTICLE_DEACTIVATE_OFFSET) | 1u;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R6, &destination_array);
    if (destination_array) return;
    uc_reg_read(uc, UC_ARM_REG_R4, &particle);
    released = compatibility_particle_release(probe, particle);
    uc_reg_write(uc, UC_ARM_REG_PC, &resume);
    ++probe->particle_unclaim_guards;
    if (probe->particle_unclaim_guards <= 8u)
        probe_log("ARM PlayLayer particle unclaim compatibility: restored "
                  "particle to source, released_hold=%u, skipped missing "
                  "destination particle=0x%08x holds=%u count=%u",
                  released != 0, particle,
                  probe->compatibility_particle_hold_count,
                  probe->particle_unclaim_guards);
}

static void label_bmfont_set_string_hook(uc_engine *uc, uint64_t address,
                                         uint32_t size, void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    static const unsigned char attempt_one_literal[12] = {
        'A', 't', 't', 'e', 'm', 'p', 't', ' ', '1', 0, 0, 0
    };
    uint32_t string_address = 0;
    uint32_t lr = 0;
    char text[2048];
    char *attempt;
    char *type_name;
    size_t length;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R1, &string_address);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    if (string_address == GUEST_IMAGE_BASE + ATTEMPT_ONE_LITERAL_OFFSET) {
        unsigned char actual[sizeof(attempt_one_literal)];
        if (uc_mem_read(probe->uc, string_address, actual, sizeof(actual)) ==
                UC_ERR_OK &&
            memcmp(actual, attempt_one_literal, sizeof(actual)) != 0 &&
            uc_mem_write(probe->uc, string_address, attempt_one_literal,
                         sizeof(attempt_one_literal)) == UC_ERR_OK) {
            ++probe->attempt_label_repairs;
            if (probe->attempt_label_repairs <= 8u)
                probe_log("ARM CCLabelBMFont restored immutable 1.4 "
                          "Attempt literal: address=0x%08x caller=0x%08x "
                          "count=%u",
                          string_address, lr,
                          probe->attempt_label_repairs);
        }
    }
    if (!guest_read_string(probe, string_address, text, sizeof(text))) return;
    length = strlen(text);
    type_name = strstr(text, "N7cocos2d");
    attempt = strstr(text, "Attempt ");
    if (type_name && attempt && attempt < type_name) {
        uint32_t repaired = string_address + (uint32_t)(attempt - text);
        if (guest_write_string(probe, repaired, "Attempt 1", 10u)) {
            uc_reg_write(uc, UC_ARM_REG_R1, &repaired);
            ++probe->attempt_label_repairs;
            if (probe->attempt_label_repairs <= 8u)
                probe_log("ARM CCLabelBMFont repaired corrupted 1.4 "
                          "Attempt label: input=0x%08x repaired=0x%08x "
                          "caller=0x%08x count=%u",
                          string_address, repaired, lr,
                          probe->attempt_label_repairs);
            return;
        }
    }
    if (length + 1u == sizeof(text) || type_name) {
        ++probe->suspicious_label_inputs;
        if (probe->suspicious_label_inputs <= 12u)
            probe_log("ARM CCLabelBMFont suspicious input: address=0x%08x "
                      "caller=0x%08x length>=%u type_name=%u sample=%.160s",
                      string_address, lr, (unsigned)length,
                      type_name != NULL, text);
    }
}

static void label_bmfont_guard_hook(uc_engine *uc, uint64_t address,
                                    uint32_t size, void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t font_definition = 0;
    uint32_t character = 0;
    uint32_t label = 0;
    uint32_t utf16 = 0;
    uint32_t sp = 0;
    uint32_t resume = (GUEST_IMAGE_BASE +
                       LABEL_BMFONT_SKIP_CHARACTER_OFFSET) | 1u;
    (void)address;
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R4, &font_definition);
    if (font_definition) return;
    uc_reg_read(uc, UC_ARM_REG_R5, &label);
    if (label)
        uc_mem_read(uc, label + 0x14cu, &utf16, sizeof(utf16));
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    if (sp)
        uc_mem_read(uc, sp + 0x1ecu, &character, sizeof(character));
    uc_reg_write(uc, UC_ARM_REG_PC, &resume);
    ++probe->unsupported_font_characters;
    if (probe->unsupported_font_characters <= 32u)
        probe_log("ARM CCLabelBMFont skipped unsupported character: "
                  "U+%04X (%u), label=0x%08x utf16=0x%08x count=%u",
                  character, character, label, utf16,
                  probe->unsupported_font_characters);
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

static void readonly_write_hook(uc_engine *uc, uc_mem_type type,
                                uint64_t address, int size, int64_t value,
                                void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t pc = 0, lr = 0;
    uint32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0;
    uint64_t original = 0;
    uint32_t offset;
    size_t original_size;
    (void)type;
    ++probe->readonly_writes;
    if (probe->readonly_writes > 64u) return;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    uc_reg_read(uc, UC_ARM_REG_R2, &r2);
    uc_reg_read(uc, UC_ARM_REG_R3, &r3);
    offset = address >= GUEST_IMAGE_BASE &&
                     address - GUEST_IMAGE_BASE <= UINT32_MAX
                 ? (uint32_t)(address - GUEST_IMAGE_BASE) : UINT32_MAX;
    original_size = size > 0 && size < 8 ? (size_t)size : sizeof(original);
    if (offset != UINT32_MAX && offset <= probe->image_size &&
        original_size <= probe->image_size - offset)
        memcpy(&original, probe->image_data + offset, original_size);
    probe_log("ARM immutable-image write: count=%u address=0x%08llx "
              "size=%d value=0x%016llx original=0x%016llx pc=0x%08x "
              "lr=0x%08x r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x",
              probe->readonly_writes, (unsigned long long)address, size,
              (unsigned long long)(uint64_t)value,
              (unsigned long long)original, pc, lr, r0, r1, r2, r3);
}

static void level_string_trace(ArmProbe *probe, const char *stage,
                               uint32_t owner, uint32_t string_object,
                               uint32_t caller) {
    uint32_t data = 0;
    uint32_t metadata[3] = {0, 0, 0};
    unsigned char block[4096];
    char sample[97];
    uint32_t offset = 0;
    uint32_t semicolons = 0;
    uint32_t commas = 0;
    uLong checksum = crc32(0L, Z_NULL, 0);
    unsigned sample_length;
    unsigned index;
    ++probe->level_trace_calls;
    if (probe->level_trace_calls > 128u) return;
    if (!string_object ||
        uc_mem_read(probe->uc, string_object, &data, sizeof(data)) !=
            UC_ERR_OK ||
        data < 12u ||
        uc_mem_read(probe->uc, data - 12u, metadata, sizeof(metadata)) !=
            UC_ERR_OK ||
        metadata[0] > MAX_GUEST_CSTRING || metadata[1] < metadata[0] ||
        metadata[1] > MAX_GUEST_CSTRING) {
        probe_log("ARM level string trace: call=%u stage=%s owner=0x%08x "
                  "object=0x%08x data=0x%08x invalid-layout "
                  "length=%u capacity=%u refs=%d caller=0x%08x",
                  probe->level_trace_calls, stage, owner, string_object, data,
                  metadata[0], metadata[1], (int32_t)metadata[2], caller);
        return;
    }
    while (offset < metadata[0]) {
        uint32_t part = metadata[0] - offset;
        if (part > sizeof(block)) part = sizeof(block);
        if (offset > UINT32_MAX - data ||
            uc_mem_read(probe->uc, data + offset, block, part) != UC_ERR_OK) {
            probe_log("ARM level string trace: call=%u stage=%s "
                      "data=0x%08x unreadable-at=%u length=%u",
                      probe->level_trace_calls, stage, data, offset,
                      metadata[0]);
            return;
        }
        checksum = crc32(checksum, block, (uInt)part);
        for (index = 0; index < part; ++index) {
            semicolons += block[index] == ';';
            commas += block[index] == ',';
        }
        offset += part;
    }
    sample_length = metadata[0] < sizeof(sample) - 1u
                        ? (unsigned)metadata[0]
                        : (unsigned)sizeof(sample) - 1u;
    memset(sample, 0, sizeof(sample));
    if (sample_length &&
        uc_mem_read(probe->uc, data, sample, sample_length) != UC_ERR_OK)
        sample_length = 0;
    for (index = 0; index < sample_length; ++index) {
        unsigned char value = (unsigned char)sample[index];
        if (value < 0x20u || value > 0x7eu) sample[index] = '.';
    }
    sample[sample_length] = 0;
    probe_log("ARM level string trace: call=%u stage=%s owner=0x%08x "
              "object=0x%08x data=0x%08x length=%u capacity=%u refs=%d "
              "crc=%08lx semicolons=%u commas=%u caller=0x%08x sample=%.96s",
              probe->level_trace_calls, stage, owner, string_object, data,
              metadata[0], metadata[1], (int32_t)metadata[2],
              (unsigned long)checksum, semicolons, commas, caller, sample);
}

static void level_string_trace_hook(uc_engine *uc, uint64_t address,
                                    uint32_t size, void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t r0 = 0, r1 = 0, lr = 0;
    uint32_t owner = 0;
    uint32_t string_object = 0;
    const char *stage = "unknown";
    (void)size;
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    if ((uint32_t)address == probe->level_trace_play_init) {
        stage = "play-init";
        owner = r1;
        string_object = r1 ? r1 + 0x134u : 0;
    } else if ((uint32_t)address == probe->level_trace_play_create) {
        stage = "play-create";
        owner = r0;
        string_object = r1;
    } else if ((uint32_t)address == probe->level_trace_editor_init) {
        stage = "editor-init";
        owner = r1;
        string_object = r1 ? r1 + 0x134u : 0;
    } else if ((uint32_t)address == probe->level_trace_editor_create) {
        stage = "editor-create";
        owner = r0;
        string_object = r1;
    } else if ((uint32_t)address == probe->level_trace_set_string) {
        stage = "set-level-string";
        owner = r0;
        string_object = r1;
    } else if ((uint32_t)address == probe->level_trace_encode) {
        stage = "encode-level";
        owner = r0;
        string_object = r0 ? r0 + 0x134u : 0;
    } else if ((uint32_t)address == probe->level_trace_save) {
        stage = "save-level";
        owner = r1;
        string_object = r1 ? r1 + 0x134u : 0;
    }
    level_string_trace(probe, stage, owner, string_object, lr);
}

static void arm_block_profile_hook(uc_engine *uc, uint64_t address,
                                   uint32_t size, void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t guest_address = (uint32_t)address;
    uint32_t index;
    uint32_t attempt;
    (void)uc;
    if (!probe || !probe->block_profile_entries ||
        address < GUEST_IMAGE_BASE ||
        address >= (uint64_t)GUEST_IMAGE_BASE + probe->image_size)
        return;
    index = (guest_address * UINT32_C(2654435761)) &
            (MAX_BLOCK_PROFILE_ENTRIES - 1u);
    for (attempt = 0; attempt < MAX_BLOCK_PROFILE_ENTRIES; ++attempt) {
        ArmBlockProfileEntry *entry =
            &probe->block_profile_entries[index];
        if (!entry->address || entry->address == guest_address) {
            if (!entry->address) {
                entry->address = guest_address;
                entry->size = size;
            } else if (size > entry->size) {
                entry->size = size;
            }
            ++entry->calls;
            return;
        }
        index = (index + 1u) & (MAX_BLOCK_PROFILE_ENTRIES - 1u);
    }
    ++probe->profile_block_dropped;
}

static bool invalid_memory_hook(uc_engine *uc, uc_mem_type type,
                                uint64_t address, int size, int64_t value,
                                void *user_data) {
    ArmProbe *probe = (ArmProbe *)user_data;
    uint32_t pc = 0;
    uint32_t lr = 0;
    uint32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0, sp = 0;
    (void)value;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    uc_reg_read(uc, UC_ARM_REG_R2, &r2);
    uc_reg_read(uc, UC_ARM_REG_R3, &r3);
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    probe->failed = 1;
    snprintf(probe->failure, sizeof(probe->failure),
             "invalid guest memory type=%d address=0x%08llx size=%d "
             "pc=0x%08x lr=0x%08x r0=0x%08x r1=0x%08x "
             "r2=0x%08x r3=0x%08x sp=0x%08x",
             (int)type, (unsigned long long)address, size, pc, lr,
             r0, r1, r2, r3, sp);
    return false;
}

static int initialize_unicorn(ArmProbe *probe) {
    uc_err error;
    uc_hook return_trace;
    uc_hook callback_return_trace;
    uc_hook invalid_trace;
    uc_hook readonly_write_trace;
    uc_hook kuser_trace;
    uc_hook jni_trace;
    uc_hook vm_trace;
    uc_hook png_filter_trace;
    uc_hook png_error_trace;
    uc_hook zip_get_trace;
    uc_hook zip_exist_trace;
    uc_hook ds_step_entry_trace;
    uc_hook ds_step_loop_trace;
    uc_hook claim_particle_trace;
    uc_hook unclaim_particle_trace;
    uc_hook label_bmfont_set_string_trace;
    uc_hook label_bmfont_trace;
    uc_hook level_string_traces[7];
    unsigned char *stubs;
    unsigned index;
    uint32_t object_value;
    probe->allocations =
        (GuestAllocation *)calloc(MAX_ALLOCS, sizeof(*probe->allocations));
    probe->free_allocation_next =
        (uint32_t *)calloc(MAX_ALLOCS, sizeof(*probe->free_allocation_next));
    if (!probe->allocations || !probe->free_allocation_next) {
        probe_log("ERROR: cannot allocate ARM guest allocation metadata");
        return 0;
    }
    error = uc_open(UC_ARCH_ARM, UC_MODE_THUMB, &probe->uc);
    if (error != UC_ERR_OK) {
        probe_log("ERROR: uc_open: %s", uc_strerror(error));
        return 0;
    }
    error = uc_ctl_set_tcg_buffer_size(probe->uc, GUEST_TCG_BUFFER_SIZE);
    if (error != UC_ERR_OK) {
        probe_log("ERROR: cannot set Unicorn TCG cache: %s",
                  uc_strerror(error));
        return 0;
    }
    {
        uint32_t actual_tcg_size = GUEST_TCG_BUFFER_SIZE;
        if (uc_ctl_get_tcg_buffer_size(probe->uc, &actual_tcg_size) !=
            UC_ERR_OK)
            actual_tcg_size = GUEST_TCG_BUFFER_SIZE;
        /* Keep Unicorn's normal CPU TLB. UC_TLB_VIRTUAL was measured with
           this exact ARM workload pattern and made repeated guest memory
           accesses several times slower because it bypasses the normal TLB
           fast path. */
        probe_log("ARM Unicorn translation cache ready: TCG cache=%u MiB, "
                  "CPU TLB=default",
                  actual_tcg_size / (1024u * 1024u));
    }
    if (!apk_build_index(probe))
        probe_log("WARNING: APK central-directory index unavailable; "
                  "using original lookup paths");
    if (!map_guest_memory(probe, GUEST_IMAGE_BASE, probe->image_size,
                          UC_PROT_ALL, probe->image_data, 0) ||
        !map_guest_memory(probe, GUEST_RETURN_BASE, 0x1000u,
                          UC_PROT_ALL, NULL, 0) ||
        !map_guest_memory(probe, GUEST_IMPORT_BASE, GUEST_IMPORT_SIZE,
                          UC_PROT_ALL, NULL, 0) ||
        !map_guest_memory(probe, GUEST_OBJECT_BASE, GUEST_OBJECT_SIZE,
                          UC_PROT_ALL, NULL, 1) ||
        !map_guest_memory(probe, GUEST_HEAP_BASE, GUEST_HEAP_SIZE,
                          UC_PROT_ALL, NULL, 1) ||
        !map_guest_memory(probe, GUEST_JNI_BASE, GUEST_JNI_SIZE,
                          UC_PROT_ALL, NULL, 1) ||
        !map_guest_memory(probe, GUEST_FILE_BASE, GUEST_FILE_SIZE,
                          UC_PROT_ALL, NULL, 1) ||
        !map_guest_memory(probe, GUEST_STACK_BASE, GUEST_STACK_SIZE,
                          UC_PROT_ALL, NULL, 1) ||
        !map_guest_memory(probe, GUEST_KUSER_BASE, GUEST_KUSER_SIZE,
                          UC_PROT_ALL, NULL, 0)) {
        probe_log("ERROR: cannot map ARM guest address space");
        return 0;
    }
    if (!guest_memory_region(probe, GUEST_IMAGE_BASE, probe->image_size) &&
        uc_mem_write(probe->uc, GUEST_IMAGE_BASE, probe->image_data,
                     probe->image_size) != UC_ERR_OK)
        return 0;
    probe_log("ARM direct guest memory ready: regions=%u heap=%.0f MiB",
              probe->memory_region_count,
              (double)GUEST_HEAP_SIZE / (1024.0 * 1024.0));
    stubs = (unsigned char *)calloc(1, probe->import_count * 4u);
    if (!stubs) return 0;
    for (index = 0; index < probe->import_count; ++index) {
        if (probe->imports[index].kind == IMPORT_PTHREAD_NOOP) {
            /* movs r0, #0; bx lr. These guest-side stubs deliberately have no
               Unicorn code hook, removing hundreds of thousands of pointless
               ARM-to-host transitions in the single-threaded renderer. */
            stubs[index * 4u] = 0x00;
            stubs[index * 4u + 1u] = 0x20;
            stubs[index * 4u + 2u] = 0x70;
            stubs[index * 4u + 3u] = 0x47;
        } else {
            stubs[index * 4u] = 0x70;     /* bx lr */
            stubs[index * 4u + 1u] = 0x47;
            stubs[index * 4u + 2u] = 0xc0; /* nop */
            stubs[index * 4u + 3u] = 0x46;
        }
    }
    error = uc_mem_write(probe->uc, GUEST_IMPORT_BASE, stubs,
                         probe->import_count * 4u);
    free(stubs);
    if (error != UC_ERR_OK) return 0;
    {
        static const unsigned char callback_return[] = {
            0x70, 0x47, 0xc0, 0x46 /* bx lr; nop */
        };
        if (uc_mem_write(probe->uc, GUEST_CALLBACK_RETURN,
                         callback_return, sizeof(callback_return)) !=
            UC_ERR_OK)
            return 0;
    }
    {
        static const unsigned char bx_lr[] = {0x1e, 0xff, 0x2f, 0xe1};
        static const uint32_t helpers[] = {
            0xffff0f60u, 0xffff0fa0u, 0xffff0fc0u, 0xffff0fe0u
        };
        for (index = 0; index < sizeof(helpers) / sizeof(helpers[0]); ++index)
            uc_mem_write(probe->uc, helpers[index], bx_lr, sizeof(bx_lr));
    }
    for (index = 0; index < probe->object_count; ++index) {
        if (!initialize_object(probe, probe->objects[index].name,
                               probe->objects[index].address)) {
            probe_log("ERROR: cannot initialize ARM imported object: %s",
                      probe->objects[index].name);
            return 0;
        }
    }
    if (probe->profile_arm_blocks) {
        /* Preserve the command-line startup profiler while sharing the same
           runtime implementation used by F11. */
        probe->profile_arm_blocks = 0;
        if (!arm_block_profiler_set(probe, 1, 0))
            return 0;
    }
    if (probe->deep_diagnostics) {
        uint32_t ds_step_address =
            find_export(probe,
                        "_ZN13DS_Dictionary22stepIntoSubDictWithKeyEPKc") &
            ~1u;
        if (ds_step_address) {
            if (uc_hook_add(probe->uc, &ds_step_entry_trace, UC_HOOK_CODE,
                            ds_step_entry_hook, probe, ds_step_address,
                            ds_step_address) != UC_ERR_OK ||
                uc_hook_add(probe->uc, &ds_step_loop_trace, UC_HOOK_CODE,
                            ds_step_loop_hook, probe, ds_step_address + 0x40u,
                            ds_step_address + 0x40u) != UC_ERR_OK) {
                probe_log("ERROR: cannot install ARM DS_Dictionary hooks");
                return 0;
            }
            probe_log("ARM DS_Dictionary diagnostics ready: guest 0x%08x",
                      ds_step_address | 1u);
        }
    }
    if (probe->deep_diagnostics) {
        struct LevelTraceExport {
            const char *name;
            uint32_t *address;
        } traces[] = {
            {"_ZN9PlayLayer4initEP11GJGameLevel",
             &probe->level_trace_play_init},
            {"_ZN9PlayLayer22createObjectsFromSetupESs",
             &probe->level_trace_play_create},
            {"_ZN16LevelEditorLayer4initEP11GJGameLevel",
             &probe->level_trace_editor_init},
            {"_ZN16LevelEditorLayer22createObjectsFromSetupESs",
             &probe->level_trace_editor_create},
            {"_ZN11GJGameLevel14setLevelStringESs",
             &probe->level_trace_set_string},
            {"_ZN11GJGameLevel15encodeWithCoderEP13DS_Dictionary",
             &probe->level_trace_encode},
            {"_ZN16GameLevelManager9saveLevelEP11GJGameLevel",
             &probe->level_trace_save}
        };
        unsigned installed = 0;
        for (index = 0; index < sizeof(traces) / sizeof(traces[0]); ++index) {
            *traces[index].address = find_export(probe, traces[index].name) &
                                     ~1u;
            if (!*traces[index].address) continue;
            if (uc_hook_add(probe->uc, &level_string_traces[index],
                            UC_HOOK_CODE, level_string_trace_hook, probe,
                            *traces[index].address,
                            *traces[index].address) != UC_ERR_OK) {
                probe_log("ERROR: cannot install ARM level-string trace %s",
                          traces[index].name);
                return 0;
            }
            ++installed;
        }
        probe_log("ARM level-string diagnostics ready: hooks=%u", installed);
    } else {
        probe_log("ARM deep parser diagnostics disabled (use --deep-diagnostics)");
    }
    probe->errno_address = GUEST_OBJECT_BASE + GUEST_OBJECT_SIZE - 0x1000u;
    object_value = 0;
    uc_mem_write(probe->uc, probe->errno_address, &object_value,
                 sizeof(object_value));
    if (!initialize_guest_jni(probe)) {
        probe_log("ERROR: cannot initialize guest JNI tables");
        return 0;
    }
    if (!install_import_hooks(probe) ||
        uc_hook_add(probe->uc, &return_trace, UC_HOOK_CODE,
                    import_hook, probe, GUEST_RETURN_BASE,
                    GUEST_RETURN_BASE) != UC_ERR_OK ||
        uc_hook_add(probe->uc, &callback_return_trace, UC_HOOK_CODE,
                    guest_callback_return_hook, probe,
                    GUEST_CALLBACK_RETURN,
                    GUEST_CALLBACK_RETURN) != UC_ERR_OK ||
        uc_hook_add(probe->uc, &kuser_trace, UC_HOOK_CODE,
                    kuser_hook, probe, 0xffff0f60u,
                    0xffff0fe0u) != UC_ERR_OK ||
        uc_hook_add(probe->uc, &jni_trace, UC_HOOK_CODE,
                    jni_hook, probe, GUEST_JNI_TRAPS,
                    GUEST_JNI_TRAPS + JNI_TABLE_SIZE * 4u - 1u) != UC_ERR_OK ||
        uc_hook_add(probe->uc, &vm_trace, UC_HOOK_CODE,
                    vm_hook, probe, GUEST_VM_TRAPS,
                    GUEST_VM_TRAPS + 8u * 4u - 1u) != UC_ERR_OK ||
        uc_hook_add(probe->uc, &invalid_trace, UC_HOOK_MEM_INVALID,
                    invalid_memory_hook, probe, 1, 0) != UC_ERR_OK) {
        probe_log("ERROR: cannot install ARM guest hooks");
        return 0;
    }
    if (!overkill_prepare_patches(probe)) {
        probe_log("ERROR: cannot prepare OVERKILL guest patches");
        return 0;
    }
    for (index = 0; index < probe->header->e_phnum; ++index) {
        const Elf32_Phdr *segment = &probe->program_headers[index];
        uint32_t start;
        uint32_t end;
        if (segment->p_type != PT_LOAD || !segment->p_memsz ||
            (segment->p_flags & 2u) != 0u)
            continue;
        start = GUEST_IMAGE_BASE + segment->p_vaddr;
        end = start + segment->p_memsz - 1u;
        probe->immutable_start = start;
        probe->immutable_end = end;
        if (uc_hook_add(probe->uc, &readonly_write_trace,
                        UC_HOOK_MEM_WRITE, readonly_write_hook, probe,
                        start, end) != UC_ERR_OK) {
            probe_log("ERROR: cannot install immutable-image write watch");
            return 0;
        }
        probe_log("ARM immutable-image write watch ready: guest "
                  "0x%08x-0x%08x", start, end);
        break;
    }
    if (!probe->disable_particle_guards) {
        static const unsigned char claim_particle_signature[] = {
            0x28, 0x1c, 0x31, 0x1c, 0x6a, 0xf0, 0x0d, 0xf8
        };
        static const unsigned char claim_remove_source_signature[] = {
            0x20, 0x1c, 0x01, 0x21, 0x69, 0xf0, 0x87, 0xff
        };
        unsigned char actual[sizeof(claim_particle_signature)];
        unsigned char actual_remove[sizeof(claim_remove_source_signature)];
        uint32_t guard_address = GUEST_IMAGE_BASE +
                                 CLAIM_PARTICLE_GUARD_OFFSET;
        uint32_t remove_address = GUEST_IMAGE_BASE +
                                  CLAIM_PARTICLE_REMOVE_SOURCE_OFFSET;
        if (uc_mem_read(probe->uc, guard_address, actual, sizeof(actual)) ==
                UC_ERR_OK &&
            uc_mem_read(probe->uc, remove_address, actual_remove,
                        sizeof(actual_remove)) == UC_ERR_OK &&
            memcmp(actual, claim_particle_signature, sizeof(actual)) == 0 &&
            memcmp(actual_remove, claim_remove_source_signature,
                   sizeof(actual_remove)) == 0) {
            if (uc_hook_add(probe->uc, &claim_particle_trace, UC_HOOK_CODE,
                            claim_particle_guard_hook, probe, guard_address,
                            guard_address) != UC_ERR_OK) {
                probe_log("ERROR: cannot install ARM PlayLayer particle guard");
                return 0;
            }
            probe_log("ARM PlayLayer particle guard ready: guest 0x%08x",
                      guard_address | 1u);
        } else {
            probe_log("ARM PlayLayer particle guard inactive: "
                      "guest signature differs");
        }
    }
    if (!probe->disable_particle_guards) {
        static const unsigned char unclaim_guard_signature[] = {
            0x30, 0x1c, 0x21, 0x1c, 0x01, 0x22, 0x6a, 0xf0,
            0x87, 0xf8
        };
        static const unsigned char unclaim_deactivate_signature[] = {
            0x23, 0x68, 0x20, 0x1c, 0x00, 0x21, 0x5b, 0x6f
        };
        unsigned char actual_guard[sizeof(unclaim_guard_signature)];
        unsigned char actual_deactivate[sizeof(unclaim_deactivate_signature)];
        uint32_t guard_address = GUEST_IMAGE_BASE +
                                 UNCLAIM_PARTICLE_GUARD_OFFSET;
        uint32_t deactivate_address = GUEST_IMAGE_BASE +
                                      UNCLAIM_PARTICLE_DEACTIVATE_OFFSET;
        if (uc_mem_read(probe->uc, guard_address, actual_guard,
                        sizeof(actual_guard)) == UC_ERR_OK &&
            uc_mem_read(probe->uc, deactivate_address, actual_deactivate,
                        sizeof(actual_deactivate)) == UC_ERR_OK &&
            memcmp(actual_guard, unclaim_guard_signature,
                   sizeof(actual_guard)) == 0 &&
            memcmp(actual_deactivate, unclaim_deactivate_signature,
                   sizeof(actual_deactivate)) == 0) {
            if (uc_hook_add(probe->uc, &unclaim_particle_trace, UC_HOOK_CODE,
                            unclaim_particle_guard_hook, probe, guard_address,
                            guard_address) != UC_ERR_OK) {
                probe_log("ERROR: cannot install ARM PlayLayer particle "
                          "unclaim guard");
                return 0;
            }
            probe_log("ARM PlayLayer particle unclaim guard ready: "
                      "guest 0x%08x", guard_address | 1u);
        } else {
            probe_log("ARM PlayLayer particle unclaim guard inactive: "
                      "guest signature differs");
        }
    }
    if (probe->disable_particle_guards)
        probe_log("ARM particle compatibility hooks disabled for performance test");
    {
        static const unsigned char set_string_signature[] = {
            0xf0, 0xb5, 0x5f, 0x46, 0x56, 0x46, 0x4d, 0x46,
            0x44, 0x46, 0xf0, 0xb4
        };
        unsigned char actual[sizeof(set_string_signature)];
        uint32_t set_string_address = GUEST_IMAGE_BASE +
                                      LABEL_BMFONT_SET_STRING_OFFSET;
        if (uc_mem_read(probe->uc, set_string_address, actual,
                        sizeof(actual)) == UC_ERR_OK &&
            memcmp(actual, set_string_signature, sizeof(actual)) == 0) {
            if (uc_hook_add(probe->uc, &label_bmfont_set_string_trace,
                            UC_HOOK_CODE, label_bmfont_set_string_hook, probe,
                            set_string_address,
                            set_string_address) != UC_ERR_OK) {
                probe_log("ERROR: cannot install ARM CCLabelBMFont string "
                          "compatibility hook");
                return 0;
            }
            probe_log("ARM CCLabelBMFont string compatibility ready: "
                      "guest 0x%08x", set_string_address | 1u);
        } else {
            probe_log("ARM CCLabelBMFont string compatibility inactive: "
                      "guest signature differs");
        }
    }
    {
        static const unsigned char label_guard_signature[] = {
            0x63, 0x68, 0x21, 0x1c, 0x08, 0x31, 0x16, 0x93
        };
        static const unsigned char label_skip_signature[] = {
            0x0c, 0x9a, 0x01, 0x21, 0x8b, 0x44, 0x5a, 0x45
        };
        unsigned char actual_guard[sizeof(label_guard_signature)];
        unsigned char actual_skip[sizeof(label_skip_signature)];
        uint32_t guard_address = GUEST_IMAGE_BASE +
                                 LABEL_BMFONT_GUARD_OFFSET;
        uint32_t skip_address = GUEST_IMAGE_BASE +
                                LABEL_BMFONT_SKIP_CHARACTER_OFFSET;
        if (uc_mem_read(probe->uc, guard_address, actual_guard,
                        sizeof(actual_guard)) == UC_ERR_OK &&
            uc_mem_read(probe->uc, skip_address, actual_skip,
                        sizeof(actual_skip)) == UC_ERR_OK &&
            memcmp(actual_guard, label_guard_signature,
                   sizeof(actual_guard)) == 0 &&
            memcmp(actual_skip, label_skip_signature,
                   sizeof(actual_skip)) == 0) {
            if (uc_hook_add(probe->uc, &label_bmfont_trace, UC_HOOK_CODE,
                            label_bmfont_guard_hook, probe, guard_address,
                            guard_address) != UC_ERR_OK) {
                probe_log("ERROR: cannot install ARM CCLabelBMFont guard");
                return 0;
            }
            probe_log("ARM CCLabelBMFont compatibility guard ready: "
                      "guest 0x%08x", guard_address | 1u);
        } else {
            probe_log("ARM CCLabelBMFont compatibility guard inactive: "
                      "guest signature differs");
        }
    }
    {
        uint32_t zip_get_address = find_export(
            probe, "_ZN7cocos2d11CCFileUtils18getFileDataFromZipEPKcS2_Pm") &
            ~1u;
        uint32_t zip_exist_address = find_export(
            probe, "_ZN7cocos2d11CCFileUtils20existFileDataFromZipEPKcS2_") &
            ~1u;
        if (probe->apk_index && zip_get_address && zip_exist_address) {
            if (uc_hook_add(probe->uc, &zip_get_trace, UC_HOOK_CODE,
                            zip_get_file_data_hook, probe, zip_get_address,
                            zip_get_address) != UC_ERR_OK ||
                uc_hook_add(probe->uc, &zip_exist_trace, UC_HOOK_CODE,
                            zip_exist_file_data_hook, probe, zip_exist_address,
                            zip_exist_address) != UC_ERR_OK) {
                probe_log("ERROR: cannot install indexed ZIP accelerator");
                return 0;
            }
            probe_log("ARM indexed ZIP accelerator ready: get=0x%08x "
                      "exist=0x%08x",
                      zip_get_address | 1u, zip_exist_address | 1u);
        } else {
            probe_log("ARM indexed ZIP accelerator inactive");
        }
    }
    {
        static const unsigned char thumb_return[] = {
            0x70, 0x47, 0xc0, 0x46 /* bx lr; nop */
        };
        uint32_t png_error_address = find_export(probe, "png_error") & ~1u;
        uint32_t png_filter_address =
            find_export(probe, "png_read_filter_row") & ~1u;
        if (png_error_address) {
            if (uc_hook_add(probe->uc, &png_error_trace, UC_HOOK_CODE,
                            png_error_hook, probe, png_error_address,
                            png_error_address) != UC_ERR_OK) {
                probe_log("ERROR: cannot install ARM libpng error hook");
                return 0;
            }
            probe_log("ARM libpng error diagnostics ready: guest 0x%08x",
                      png_error_address | 1u);
        }
        if (png_filter_address) {
            if (uc_mem_write(probe->uc, png_filter_address,
                             thumb_return, sizeof(thumb_return)) != UC_ERR_OK ||
                uc_hook_add(probe->uc, &png_filter_trace, UC_HOOK_CODE,
                            png_filter_hook, probe, png_filter_address,
                            png_filter_address) != UC_ERR_OK) {
                probe_log("ERROR: cannot install ARM libpng row-filter hook");
                return 0;
            }
            probe_log("ARM libpng row-filter accelerator ready: guest 0x%08x",
                      png_filter_address | 1u);
        }
    }
    probe->host_write_guard_enabled = probe->immutable_start != 0u;
    if (probe->host_write_guard_enabled)
        probe_log("ARM host immutable-write guard armed: guest "
                  "0x%08x-0x%08x", probe->immutable_start,
                  probe->immutable_end);
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


static const char *find_nearest_export(const ArmProbe *probe,
                                       uint32_t guest_address,
                                       uint32_t *symbol_address) {
    uint32_t image_offset;
    uint32_t best_value = 0;
    const char *best_name = NULL;
    uint16_t section_index;
    if (symbol_address) *symbol_address = 0;
    if (!probe || guest_address < GUEST_IMAGE_BASE) return NULL;
    image_offset = guest_address - GUEST_IMAGE_BASE;
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
            symbols_section->sh_entsize != sizeof(Elf32_Sym))
            continue;
        strings_section = section_at(probe, symbols_section->sh_link);
        if (!strings_section) continue;
        symbols = (const Elf32_Sym *)(probe->file_data +
                                      symbols_section->sh_offset);
        strings = (const char *)(probe->file_data +
                                  strings_section->sh_offset);
        count = symbols_section->sh_size / sizeof(Elf32_Sym);
        for (index = 0; index < count; ++index) {
            const Elf32_Sym *symbol = &symbols[index];
            if (symbol->st_shndx == SHN_UNDEF || !symbol->st_value ||
                symbol->st_value > image_offset ||
                symbol->st_name >= strings_section->sh_size)
                continue;
            if (!best_name || symbol->st_value > best_value) {
                const char *name = strings + symbol->st_name;
                if (!*name) continue;
                best_name = name;
                best_value = symbol->st_value;
            }
        }
    }
    if (best_name && symbol_address)
        *symbol_address = GUEST_IMAGE_BASE + best_value;
    return best_name;
}

static void arm_block_profile_clear(ArmProbe *probe) {
    if (!probe || !probe->block_profile_entries) return;
    memset(probe->block_profile_entries, 0,
           MAX_BLOCK_PROFILE_ENTRIES * sizeof(*probe->block_profile_entries));
    probe->profile_block_dropped = 0;
}

static void arm_block_profile_report(ArmProbe *probe, const char *reason) {
    enum { TOP_BLOCKS = 16 };
    unsigned top_indices[TOP_BLOCKS];
    uint64_t top_scores[TOP_BLOCKS] = {0};
    char block_text[3072];
    size_t block_used = 0;
    unsigned index;
    unsigned place;
    if (!probe || !probe->block_profile_entries) return;
    memset(top_indices, 0xff, sizeof(top_indices));
    for (index = 0; index < MAX_BLOCK_PROFILE_ENTRIES; ++index) {
        ArmBlockProfileEntry *entry = &probe->block_profile_entries[index];
        uint64_t score = entry->calls * (entry->size ? entry->size : 2u);
        if (!score) continue;
        for (place = 0; place < TOP_BLOCKS; ++place) {
            if (score > top_scores[place]) {
                unsigned move;
                for (move = TOP_BLOCKS - 1u; move > place; --move) {
                    top_scores[move] = top_scores[move - 1u];
                    top_indices[move] = top_indices[move - 1u];
                }
                top_scores[place] = score;
                top_indices[place] = index;
                break;
            }
        }
    }
    block_text[0] = 0;
    for (place = 0; place < TOP_BLOCKS && top_scores[place]; ++place) {
        ArmBlockProfileEntry *entry =
            &probe->block_profile_entries[top_indices[place]];
        uint32_t symbol_address = 0;
        const char *symbol =
            find_nearest_export(probe, entry->address, &symbol_address);
        int written;
        if (symbol) {
            written = snprintf(
                block_text + block_used, sizeof(block_text) - block_used,
                "%s0x%08x:%llux%u=%s+0x%x",
                place ? "," : "", entry->address,
                (unsigned long long)entry->calls, entry->size, symbol,
                entry->address - symbol_address);
        } else {
            written = snprintf(
                block_text + block_used, sizeof(block_text) - block_used,
                "%s0x%08x(+0x%06x):%llux%u",
                place ? "," : "", entry->address,
                entry->address - GUEST_IMAGE_BASE,
                (unsigned long long)entry->calls, entry->size);
        }
        if (written < 0 || (size_t)written >=
                             sizeof(block_text) - block_used)
            break;
        block_used += (size_t)written;
    }
    probe_log("ARM hot block profile (%s): %s dropped=%llu",
              reason ? reason : "sample",
              block_text[0] ? block_text : "none",
              (unsigned long long)probe->profile_block_dropped);
}

static int arm_block_profiler_set(ArmProbe *probe, int enabled,
                                  int timed_sample) {
    uc_err error;
    if (!probe || !probe->uc) return 0;
    if (enabled) {
        if (probe->profile_arm_blocks) return 1;
        if (!probe->block_profile_entries) {
            probe->block_profile_entries = (ArmBlockProfileEntry *)calloc(
                MAX_BLOCK_PROFILE_ENTRIES,
                sizeof(*probe->block_profile_entries));
            if (!probe->block_profile_entries) {
                probe_log("ERROR: cannot allocate ARM block profiler table");
                return 0;
            }
        }
        arm_block_profile_clear(probe);
        error = uc_hook_add(probe->uc, &probe->arm_block_trace, UC_HOOK_BLOCK,
                            arm_block_profile_hook, probe, GUEST_IMAGE_BASE,
                            GUEST_IMAGE_BASE + probe->image_size - 1u);
        if (error != UC_ERR_OK) {
            probe_log("ERROR: cannot enable ARM block profiler: %s",
                      uc_strerror(error));
            probe->arm_block_trace = 0;
            return 0;
        }
        error = uc_ctl_remove_cache(
            probe->uc, (uint64_t)GUEST_IMAGE_BASE,
            (uint64_t)GUEST_IMAGE_BASE + probe->image_size);
        if (error != UC_ERR_OK) {
            uc_hook_del(probe->uc, probe->arm_block_trace);
            probe->arm_block_trace = 0;
            probe_log("ERROR: cannot activate ARM block profiler cache: %s",
                      uc_strerror(error));
            return 0;
        }
        probe->profile_arm_blocks = 1;
        probe->block_profile_deadline =
            timed_sample ? GetTickCount() + 5000u : 0u;
        probe_log("ARM block profiler ENABLED%s: translated blocks will run "
                  "slower during this diagnostic sample",
                  timed_sample ? " for 5 seconds" : "");
        return 1;
    }
    if (!probe->profile_arm_blocks) return 1;
    if (probe->arm_block_trace) {
        error = uc_hook_del(probe->uc, probe->arm_block_trace);
        if (error != UC_ERR_OK)
            probe_log("WARNING: cannot remove ARM block profiler hook: %s",
                      uc_strerror(error));
        probe->arm_block_trace = 0;
    }
    error = uc_ctl_remove_cache(
        probe->uc, (uint64_t)GUEST_IMAGE_BASE,
        (uint64_t)GUEST_IMAGE_BASE + probe->image_size);
    if (error != UC_ERR_OK)
        probe_log("WARNING: cannot restore normal ARM translation cache: %s",
                  uc_strerror(error));
    arm_block_profile_report(probe, "completed 5-second sample");
    probe->profile_arm_blocks = 0;
    probe->block_profile_deadline = 0;
    probe_log("ARM block profiler DISABLED; normal-speed translation restored");
    return 1;
}

static int run_guest(ArmProbe *probe, uint32_t entry,
                     const uint32_t *arguments, unsigned argument_count,
                     size_t instruction_limit, uint32_t *return_value) {
    uint32_t registers[4] = {0, 0, 0, 0};
    uint32_t stack_words = argument_count > 4u ? argument_count - 4u : 0u;
    uint32_t stack = (GUEST_STACK_TOP - 0x1000u - stack_words * 4u) & ~7u;
    uint32_t link = GUEST_RETURN_BASE | 1u;
    uc_err error;
    unsigned index;
    for (index = 0; index < argument_count && index < 4; ++index)
        registers[index] = arguments[index];
    if (stack_words && arguments &&
        uc_mem_write(probe->uc, stack, arguments + 4u, stack_words * 4u) != UC_ERR_OK) {
        probe_log("ERROR: cannot write ARM call stack arguments");
        return 0;
    }
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
    error = uc_emu_start(probe->uc, entry, UINT64_MAX, 0,
                         instruction_limit);
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
        uint32_t pc = 0, lr = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, sp = 0;
        uint32_t cpsr = 0;
        uint32_t stack_dump[12] = {0};
        int have_stack;
        uc_reg_read(probe->uc, UC_ARM_REG_PC, &pc);
        uc_reg_read(probe->uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(probe->uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(probe->uc, UC_ARM_REG_R1, &r1);
        uc_reg_read(probe->uc, UC_ARM_REG_R2, &r2);
        uc_reg_read(probe->uc, UC_ARM_REG_R3, &r3);
        uc_reg_read(probe->uc, UC_ARM_REG_SP, &sp);
        uc_reg_read(probe->uc, UC_ARM_REG_CPSR, &cpsr);
        have_stack = sp &&
            uc_mem_read(probe->uc, sp, stack_dump,
                        sizeof(stack_dump)) == UC_ERR_OK;
        if (instruction_limit) {
            probe_log("ERROR: ARM call hit instruction limit=%llu without "
                      "returning entry=0x%08x pc=0x%08x lr=0x%08x "
                      "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x sp=0x%08x "
                      "cpsr=0x%08x mode=%s",
                      (unsigned long long)instruction_limit, entry, pc, lr,
                      r0, r1, r2, r3, sp, cpsr,
                      (cpsr & (1u << 5u)) ? "Thumb" : "ARM");
        } else {
            probe_log("ERROR: ARM call stopped without returning "
                      "entry=0x%08x pc=0x%08x lr=0x%08x "
                      "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x sp=0x%08x "
                      "cpsr=0x%08x mode=%s",
                      entry, pc, lr, r0, r1, r2, r3, sp, cpsr,
                      (cpsr & (1u << 5u)) ? "Thumb" : "ARM");
        }
        probe_log("ARM stopped-call stack: "
                  "%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,"
                  "%08x,%08x,%08x,%08x%s",
                  stack_dump[0], stack_dump[1], stack_dump[2],
                  stack_dump[3], stack_dump[4], stack_dump[5],
                  stack_dump[6], stack_dump[7], stack_dump[8],
                  stack_dump[9], stack_dump[10], stack_dump[11],
                  have_stack ? "" : " [unreadable]");
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
            if (index < 8u || ((index + 1u) % 32u) == 0u ||
                index + 1u == count)
                probe_log("constructor %u/%u: guest 0x%08x (ELF+0x%08x)",
                          index + 1u, count, entry,
                          entry - GUEST_IMAGE_BASE);
            if (!run_guest(probe, entry, NULL, 0,
                           DEFAULT_GUEST_INSTRUCTION_LIMIT, NULL)) {
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
    if (!probe) return;
    for (index = 0; index < probe->zstream_count; ++index)
        guest_zstream_release(&probe->zstreams[index]);
    for (index = 0; index < probe->file_count; ++index) {
        if (probe->files[index].handle)
            guest_file_close(probe, &probe->files[index]);
    }
    for (index = 0; index < probe->ref_count; ++index) {
        free(probe->refs[index].class_name);
        free(probe->refs[index].name);
        free(probe->refs[index].signature);
    }
    for (index = 0; index < probe->native_count; ++index) {
        free(probe->natives[index].class_name);
        free(probe->natives[index].name);
        free(probe->natives[index].signature);
    }
    if (probe->uc) uc_close(probe->uc);
    for (index = 0; index < probe->memory_region_count; ++index) {
        GuestMemoryRegion *region = &probe->memory_regions[index];
        if (region->owned && region->host)
            VirtualFree(region->host, 0, MEM_RELEASE);
    }
    for (index = 0; index < probe->import_count; ++index)
        free(probe->imports[index].name);
    for (index = 0; index < probe->object_count; ++index)
        free(probe->objects[index].name);
    for (index = 0; index < MAX_GL_VERTEX_ATTRIBS; ++index)
        free(probe->gl_client_buffers[index]);
    free(probe->gl_index_buffer);
    free(probe->io_buffer);
    for (index = 0; index < probe->apk_member_cache_count; ++index) {
        free(probe->apk_member_cache[index].name);
        free(probe->apk_member_cache[index].payload);
    }
    for (index = 0; index < probe->apk_index_count; ++index)
        free(probe->apk_index[index].name);
    free(probe->apk_index);
    free(probe->apk_data);
    if (probe->image_data_virtual && probe->image_data)
        VirtualFree(probe->image_data, 0, MEM_RELEASE);
    else
        free(probe->image_data);
    free(probe->file_data);
    free(probe->allocations);
    free(probe->free_allocation_next);
    free(probe->block_profile_entries);
}

static uint32_t find_registered_native(const ArmProbe *probe,
                                       const char *class_name,
                                       const char *method_name) {
    unsigned index;
    for (index = 0; index < probe->native_count; ++index) {
        const RegisteredNative *native = &probe->natives[index];
        if (native->name && strcmp(native->name, method_name) == 0 &&
            (!class_name || (native->class_name &&
             strcmp(native->class_name, class_name) == 0)))
            return native->function;
    }
    return 0;
}

static uint32_t resolve_native(ArmProbe *probe, const char *export_name,
                               const char *class_name,
                               const char *method_name, int required) {
    uint32_t address = find_export(probe, export_name);
    if (!address) address = find_registered_native(probe, class_name, method_name);
    if (address) {
        probe_log("ARM native bridge: %s -> 0x%08x", method_name, address);
    } else if (required) {
        probe_log("ERROR: required ARM native method is missing: %s", method_name);
    }
    return address;
}

static int host_call(ArmProbe *probe, uint32_t address,
                     const uint32_t *arguments, unsigned count,
                     const char *label) {
    int render_call = label && strcmp(label, "nativeRender") == 0;
    uint64_t started_microseconds;
    uint64_t elapsed_microseconds;
    uint64_t apk_opens_before = probe->profile_apk_opens;
    uint64_t file_bytes_before = probe->profile_file_read_bytes;
    uint64_t zlib_before = probe->profile_zlib_calls;
    uint64_t allocs_before = probe->profile_alloc_calls;
    uint64_t draws_before = probe->profile_gl_draw_calls;
    uint64_t ds_calls_before = probe->ds_step_calls;
    uint64_t ds_iterations_before = probe->ds_step_iterations;
    uint64_t ds_cycles_before = probe->ds_step_cycles;
    int call_ok;
    size_t instruction_limit = label && strcmp(label, "nativeInit") == 0
                                   ? NATIVE_INIT_INSTRUCTION_LIMIT
                                   : probe->host.native_ready
                                         ? NATIVE_RUNTIME_INSTRUCTION_LIMIT
                                         : DEFAULT_GUEST_INSTRUCTION_LIMIT;
    if (!address) return 0;
    started_microseconds = host_monotonic_units(UINT64_C(1000000));
    if (!instruction_limit) {
        if (!probe->runtime_fast_path_logged) {
            probe_log("ARM runtime fast path: per-instruction counting "
                      "disabled (first call: %s)", label ? label : "?");
            probe->runtime_fast_path_logged = 1;
        }
    } else if (instruction_limit != DEFAULT_GUEST_INSTRUCTION_LIMIT &&
               (!render_call || probe->native_render_calls == 0)) {
        probe_log("ARM call instruction budget: %s -> %llu",
                  label, (unsigned long long)instruction_limit);
    }
    if (render_call) ++probe->native_render_calls;
    call_ok = run_guest(probe, address, arguments, count, instruction_limit,
                        NULL);
    elapsed_microseconds =
        host_monotonic_units(UINT64_C(1000000)) - started_microseconds;
    if (render_call) {
        probe->profile_last_render_microseconds = elapsed_microseconds;
        probe->profile_render_microseconds += elapsed_microseconds;
        if (elapsed_microseconds > probe->profile_render_max_microseconds)
            probe->profile_render_max_microseconds = elapsed_microseconds;
    }
    if (probe->host.native_ready && elapsed_microseconds >= UINT64_C(100000) &&
        probe->profile_slow_native_logs < 32u &&
        (!render_call || elapsed_microseconds >= UINT64_C(250000))) {
        ++probe->profile_slow_native_logs;
        probe_log("ARM slow native call: %s %.1f ms apk-opens=%llu "
                  "file-read=%.2f MiB zlib=%llu allocs=%llu draws=%llu",
                  label ? label : "?",
                  (double)elapsed_microseconds / 1000.0,
                  (unsigned long long)(probe->profile_apk_opens -
                                            apk_opens_before),
                  (double)(probe->profile_file_read_bytes -
                               file_bytes_before) / (1024.0 * 1024.0),
                  (unsigned long long)(probe->profile_zlib_calls - zlib_before),
                  (unsigned long long)(probe->profile_alloc_calls -
                                            allocs_before),
                  (unsigned long long)(probe->profile_gl_draw_calls -
                                            draws_before));
    }
    if (render_call &&
        (probe->native_render_calls <= 24u ||
         probe->ds_step_cycles != ds_cycles_before))
        probe_log("ARM render dictionary work: frame=%u calls=%llu "
                  "iterations=%llu cycles=%llu",
                  probe->native_render_calls,
                  (unsigned long long)(probe->ds_step_calls - ds_calls_before),
                  (unsigned long long)(probe->ds_step_iterations -
                                       ds_iterations_before),
                  (unsigned long long)(probe->ds_step_cycles - ds_cycles_before));
    if (!call_ok) {
        probe_log("ERROR: ARM native call failed: %s", label ? label : "?");
        return 0;
    }
    return 1;
}

static int executable_directory(char *destination, size_t capacity) {
    char *slash;
    DWORD length = GetModuleFileNameA(NULL, destination, (DWORD)capacity);
    if (!length || length >= capacity) return 0;
    slash = strrchr(destination, '\\');
    if (slash) *slash = 0;
    return SetCurrentDirectoryA(destination) != 0;
}

static void host_client_to_native(ArmProbe *probe, HWND window,
                                  float *x, float *y) {
    RECT area;
    if (!probe || !x || !y || !GetClientRect(window, &area) ||
        area.right <= area.left || area.bottom <= area.top ||
        probe->host.native_width <= 0 || probe->host.native_height <= 0)
        return;
    *x = *x * (float)probe->host.native_width /
         (float)(area.right - area.left);
    *y = *y * (float)probe->host.native_height /
         (float)(area.bottom - area.top);
}

static void host_touch(ArmProbe *probe, uint32_t function,
                       float x, float y, const char *label) {
    uint32_t arguments[5] = {
        GUEST_ENV_OBJECT, 0, 0, float_bits(x), float_bits(y)
    };
    if (probe && probe->host.native_ready && function)
        host_call(probe, function, arguments, 5, label);
}

static void host_touch_move(ArmProbe *probe, float x, float y) {
    int32_t identifier = 0;
    uint32_t arguments[5];
    GuestRef *ids, *xs, *ys;
    if (!probe || !probe->host.native_ready || !probe->host.touch_move) return;
    if (!probe->host.touch_ids) {
        probe->host.touch_ids = guest_new_array_ref(probe, GREF_INT_ARRAY, 1, 4);
        probe->host.touch_xs = guest_new_array_ref(probe, GREF_FLOAT_ARRAY, 1, 4);
        probe->host.touch_ys = guest_new_array_ref(probe, GREF_FLOAT_ARRAY, 1, 4);
    }
    ids = guest_ref(probe, probe->host.touch_ids);
    xs = guest_ref(probe, probe->host.touch_xs);
    ys = guest_ref(probe, probe->host.touch_ys);
    if (!ids || !xs || !ys) return;
    uc_mem_write(probe->uc, ids->data_address, &identifier, 4);
    uc_mem_write(probe->uc, xs->data_address, &x, 4);
    uc_mem_write(probe->uc, ys->data_address, &y, 4);
    arguments[0] = GUEST_ENV_OBJECT;
    arguments[1] = 0;
    arguments[2] = probe->host.touch_ids;
    arguments[3] = probe->host.touch_xs;
    arguments[4] = probe->host.touch_ys;
    host_call(probe, probe->host.touch_move, arguments, 5, "nativeTouchesMove");
}

static void host_send_text(ArmProbe *probe, WPARAM character) {
    WCHAR utf16[3] = {0, 0, 0};
    char utf8[12];
    int utf16_length = 1;
    int utf8_length;
    uint32_t text;
    uint32_t arguments[3];
    if (!probe || !probe->host.native_ready || !probe->host.insert_text) return;
    if (character == '\r') character = '\n';
    if (character < 0x20 && character != '\n' && character != '\t') return;
    if (character <= 0xffff) {
        utf16[0] = (WCHAR)character;
    } else if (character <= 0x10ffff) {
        character -= 0x10000;
        utf16[0] = (WCHAR)(0xd800 + (character >> 10));
        utf16[1] = (WCHAR)(0xdc00 + (character & 0x3ff));
        utf16_length = 2;
    } else return;
    utf8_length = WideCharToMultiByte(CP_UTF8, 0, utf16, utf16_length,
                                      utf8, sizeof(utf8) - 1, NULL, NULL);
    if (utf8_length <= 0) return;
    utf8[utf8_length] = 0;
    text = guest_new_string_ref(probe, utf8);
    arguments[0] = GUEST_ENV_OBJECT;
    arguments[1] = 0;
    arguments[2] = text;
    host_call(probe, probe->host.insert_text, arguments, 3, "nativeInsertText");
}

static void host_pause(ArmProbe *probe, const char *reason) {
    uint32_t arguments[2] = {GUEST_ENV_OBJECT, 0};
    if (!probe || !probe->host.native_ready || probe->host.native_paused ||
        !probe->host.pause) return;
    probe_log("Android ARM lifecycle: nativeOnPause (%s)",
              reason ? reason : "unspecified");
    if (host_call(probe, probe->host.pause, arguments, 2, "nativeOnPause"))
        probe->host.native_paused = 1;
}

static void host_resume(ArmProbe *probe, const char *reason) {
    uint32_t arguments[2] = {GUEST_ENV_OBJECT, 0};
    if (!probe || !probe->host.native_ready || !probe->host.native_paused ||
        !probe->host.resume || probe->host.closing) return;
    probe_log("Android ARM lifecycle: nativeOnResume (%s)",
              reason ? reason : "unspecified");
    if (host_call(probe, probe->host.resume, arguments, 2, "nativeOnResume"))
        probe->host.native_paused = 0;
}

static LRESULT CALLBACK arm_window_procedure(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam) {
    ArmProbe *probe = g_active_probe;
    float x = (float)GET_X_LPARAM(lparam);
    float y = (float)GET_Y_LPARAM(lparam);
    if (!probe) return DefWindowProcA(window, message, wparam, lparam);
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_MOUSEMOVE) {
        host_client_to_native(probe, window, &x, &y);
        probe->host.last_touch_x = x;
        probe->host.last_touch_y = y;
    }
    switch (message) {
    case WM_CLOSE:
        probe->host.closing = 1;
        host_pause(probe, "window close");
        DestroyWindow(window);
        return 0;
    case WM_QUERYENDSESSION:
        host_pause(probe, "Windows session ending");
        return TRUE;
    case WM_ENDSESSION:
        if (wparam) {
            probe->host.closing = 1;
            host_pause(probe, "Windows session ended");
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ACTIVATEAPP:
        probe->host.window_active = wparam != 0;
        if (wparam) host_resume(probe, "window activated");
        else host_pause(probe, "window deactivated");
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CHAR:
        if (wparam == '\b') {
            uint32_t arguments[2] = {GUEST_ENV_OBJECT, 0};
            if (probe->host.native_ready && probe->host.delete_backward)
                host_call(probe, probe->host.delete_backward, arguments, 2,
                          "nativeDeleteBackward");
        } else host_send_text(probe, wparam);
        return 0;
    case WM_UNICHAR:
        if (wparam == UNICODE_NOCHAR) return TRUE;
        host_send_text(probe, wparam);
        return 0;
    case WM_LBUTTONDOWN:
        probe->host.mouse_down = 1;
        SetFocus(window);
        SetCapture(window);
        host_touch(probe, probe->host.touch_begin, x, y, "nativeTouchesBegin");
        return 0;
    case WM_MOUSEMOVE:
        if (probe->host.mouse_down) host_touch_move(probe, x, y);
        return 0;
    case WM_LBUTTONUP:
        if (probe->host.mouse_down) {
            probe->host.mouse_down = 0;
            ReleaseCapture();
            host_touch(probe, probe->host.touch_end, x, y, "nativeTouchesEnd");
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (probe->host.mouse_down) {
            probe->host.mouse_down = 0;
            host_touch(probe, probe->host.touch_end,
                       probe->host.last_touch_x, probe->host.last_touch_y,
                       "nativeTouchesEnd");
        }
        return 0;
    case WM_KEYDOWN:
        /* Windows auto-repeat generated multiple toggles for a single held
           function key in OverkillTest1 (for example F3 was removed and then
           restored immediately). Bit 30 is set when the key was already down. */
        if (wparam >= VK_F3 && wparam <= VK_F11 &&
            (lparam & ((LPARAM)1 << 30)) != 0)
            return 0;
        if (wparam == VK_F3) {
            probe->skip_native_render = !probe->skip_native_render;
            probe_log("OVERKILL nativeRender: %s",
                      probe->skip_native_render ? "REMOVED" : "restored");
            overkill_log_state(probe);
            return 0;
        }
        if (wparam == VK_F4) {
            overkill_set_node_draws(probe, !probe->disable_node_draws);
            overkill_log_state(probe);
            return 0;
        }
        if (wparam == VK_F5) {
            overkill_set_particles(probe, !probe->disable_particles);
            overkill_log_state(probe);
            return 0;
        }
        if (wparam == VK_F6) {
            probe->skip_draw_calls = !probe->skip_draw_calls;
            probe_log("OVERKILL host draw calls: %s",
                      probe->skip_draw_calls ? "REMOVED" : "restored");
            overkill_log_state(probe);
            return 0;
        }
        if (wparam == VK_F7) {
            overkill_set_scene_visit(probe, !probe->skip_scene_visit);
            overkill_log_state(probe);
            return 0;
        }
        if (wparam == VK_F8) {
            probe->headless_gl = !probe->headless_gl;
            probe_log("OVERKILL OpenGL bridge: %s",
                      probe->headless_gl ? "HEADLESS" : "restored");
            overkill_log_state(probe);
            return 0;
        }
        if (wparam == VK_F9) {
            probe->skip_texture_uploads = !probe->skip_texture_uploads;
            probe_log("OVERKILL future texture uploads: %s",
                      probe->skip_texture_uploads ? "REMOVED" : "restored");
            overkill_log_state(probe);
            return 0;
        }
        if (wparam == VK_F10) {
            overkill_log_state(probe);
            return 0;
        }
        if (wparam == VK_F11) {
            arm_block_profiler_set(probe, !probe->profile_arm_blocks, 1);
            return 0;
        }
        if (wparam == VK_ESCAPE && probe->host.native_ready &&
            probe->host.key_down) {
            uint32_t arguments[3] = {GUEST_ENV_OBJECT, 0, 4};
            host_call(probe, probe->host.key_down, arguments, 3, "nativeKeyDown");
            return 0;
        }
        if ((wparam == VK_SPACE || wparam == VK_UP) &&
            !probe->host.keyboard_down && !probe->text_input_active) {
            probe->host.keyboard_down = 1;
            host_touch(probe, probe->host.touch_begin,
                       (float)probe->host.native_width * 0.5f,
                       (float)probe->host.native_height * 0.5f,
                       "nativeTouchesBegin");
            return 0;
        }
        break;
    case WM_KEYUP:
        if ((wparam == VK_SPACE || wparam == VK_UP) &&
            probe->host.keyboard_down) {
            probe->host.keyboard_down = 0;
            host_touch(probe, probe->host.touch_end,
                       (float)probe->host.native_width * 0.5f,
                       (float)probe->host.native_height * 0.5f,
                       "nativeTouchesEnd");
            return 0;
        }
        break;
    default: break;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static int create_arm_opengl_window(ArmProbe *probe) {
    WNDCLASSA window_class;
    RECT rectangle;
    PIXELFORMATDESCRIPTOR descriptor;
    int pixel_format;
    typedef BOOL (WINAPI *SwapIntervalFunction)(int);
    SwapIntervalFunction swap_interval;
    memset(&window_class, 0, sizeof(window_class));
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = arm_window_procedure;
    window_class.hInstance = GetModuleHandleA(NULL);
    window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
    window_class.lpszClassName = "GDArmNativeWrapperWindow";
    if (!RegisterClassA(&window_class) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        probe_log("ERROR: RegisterClass failed: %lu",
                  (unsigned long)GetLastError());
        return 0;
    }
    rectangle.left = rectangle.top = 0;
    rectangle.right = probe->host.native_width;
    rectangle.bottom = probe->host.native_height;
    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    probe->host.window = CreateWindowExA(
        0, window_class.lpszClassName,
        "Geometry Dash ARM wrapper 0.9.4-arm-overkilltest2",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        NULL, NULL, window_class.hInstance, NULL);
    if (!probe->host.window) {
        probe_log("ERROR: CreateWindow failed: %lu",
                  (unsigned long)GetLastError());
        return 0;
    }
    probe->host.device = GetDC(probe->host.window);
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
                         PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;
    descriptor.iLayerType = PFD_MAIN_PLANE;
    pixel_format = ChoosePixelFormat(probe->host.device, &descriptor);
    if (!pixel_format ||
        !SetPixelFormat(probe->host.device, pixel_format, &descriptor)) {
        probe_log("ERROR: OpenGL pixel format setup failed: %lu",
                  (unsigned long)GetLastError());
        return 0;
    }
    probe->host.context = wglCreateContext(probe->host.device);
    if (!probe->host.context ||
        !wglMakeCurrent(probe->host.device, probe->host.context)) {
        probe_log("ERROR: wglCreateContext/wglMakeCurrent failed: %lu",
                  (unsigned long)GetLastError());
        return 0;
    }
    swap_interval = (SwapIntervalFunction)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap_interval && swap_interval(probe->disable_vsync ? 0 : 1))
        probe->host.vsync_enabled = !probe->disable_vsync;
    probe_log("OpenGL vertical sync: %s",
              probe->host.vsync_enabled ? "enabled" : "unavailable");
    probe_log("OpenGL vendor: %s", glGetString(GL_VENDOR));
    probe_log("OpenGL renderer: %s", glGetString(GL_RENDERER));
    probe_log("OpenGL version: %s", glGetString(GL_VERSION));
    ShowWindow(probe->host.window, SW_SHOW);
    UpdateWindow(probe->host.window);
    return 1;
}

static void destroy_arm_opengl_window(ArmProbe *probe) {
    if (!probe) return;
    if (probe->host.context) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(probe->host.context);
        probe->host.context = NULL;
    }
    if (probe->host.device && probe->host.window) {
        ReleaseDC(probe->host.window, probe->host.device);
        probe->host.device = NULL;
    }
    if (probe->host.window && IsWindow(probe->host.window))
        DestroyWindow(probe->host.window);
    probe->host.window = NULL;
}

static void arm_frame_profile_reset(ArmProbe *probe) {
    unsigned index;
    if (!probe) return;
    probe->profile_render_microseconds = 0;
    probe->profile_render_max_microseconds = 0;
    probe->profile_last_render_microseconds = 0;
    probe->profile_swap_microseconds = 0;
    probe->profile_swap_max_microseconds = 0;
    probe->profile_gl_draw_calls = 0;
    probe->profile_gl_vertices = 0;
    probe->profile_gl_client_copies = 0;
    probe->profile_gl_direct_arrays = 0;
    probe->profile_gl_client_bytes = 0;
    probe->profile_gl_proc_cache_hits = 0;
    probe->profile_gl_proc_cache_misses = 0;
    probe->profile_gl_redundant_binds = 0;
    probe->profile_gl_direct_uploads = 0;
    probe->profile_alloc_calls = 0;
    probe->profile_alloc_bytes = 0;
    probe->profile_free_calls = 0;
    probe->profile_file_opens = 0;
    probe->profile_apk_opens = 0;
    probe->profile_file_read_bytes = 0;
    probe->profile_zlib_calls = 0;
    probe->profile_frames_over_20ms = 0;
    probe->profile_frames_over_33ms = 0;
    probe->profile_frames_over_50ms = 0;
    probe->profile_direct_read_baseline = probe->direct_memory_reads;
    probe->profile_direct_write_baseline = probe->direct_memory_writes;
    probe->profile_apk_cache_hit_baseline = probe->apk_member_cache_hits;
    probe->profile_apk_cache_miss_baseline = probe->apk_member_cache_misses;
    for (index = 0; index < probe->import_count; ++index) {
        probe->profile_import_baseline[index] = probe->imports[index].calls;
        probe->profile_import_ticks[index] = 0;
    }
}

static void arm_frame_profile_log(ArmProbe *probe, unsigned frames,
                                  DWORD elapsed_milliseconds) {
    enum { HOT_IMPORTS = 6 };
    unsigned hot_indices[HOT_IMPORTS];
    uint64_t hot_calls[HOT_IMPORTS] = {0};
    uint64_t total_imports = 0;
    char hot_text[768];
    size_t hot_used = 0;
    unsigned index;
    double elapsed_seconds = (double)elapsed_milliseconds / 1000.0;
    memset(hot_indices, 0xff, sizeof(hot_indices));
    for (index = 0; index < probe->import_count; ++index) {
        uint64_t delta = (unsigned)(probe->imports[index].calls -
                                    probe->profile_import_baseline[index]);
        unsigned place;
        total_imports += delta;
        if (!delta) continue;
        for (place = 0; place < HOT_IMPORTS; ++place) {
            if (delta > hot_calls[place]) {
                unsigned move;
                for (move = HOT_IMPORTS - 1u; move > place; --move) {
                    hot_calls[move] = hot_calls[move - 1u];
                    hot_indices[move] = hot_indices[move - 1u];
                }
                hot_calls[place] = delta;
                hot_indices[place] = index;
                break;
            }
        }
    }
    hot_text[0] = 0;
    for (index = 0; index < HOT_IMPORTS && hot_calls[index]; ++index) {
        int written = snprintf(hot_text + hot_used,
                               sizeof(hot_text) - hot_used,
                               "%s%s:%llu", index ? "," : "",
                               probe->imports[hot_indices[index]].name,
                               (unsigned long long)hot_calls[index]);
        if (written < 0 || (size_t)written >= sizeof(hot_text) - hot_used) {
            hot_text[sizeof(hot_text) - 1u] = 0;
            break;
        }
        hot_used += (size_t)written;
    }
    if (!hot_text[0]) snprintf(hot_text, sizeof(hot_text), "none");
    probe_log("ARM frame profile: render avg=%.2f max=%.2f ms "
              "swap avg=%.2f max=%.2f ms slow>20/33/50=%llu/%llu/%llu "
              "draws/frame=%.1f vertices/frame=%.0f",
              frames ? (double)probe->profile_render_microseconds /
                           (1000.0 * frames) : 0.0,
              (double)probe->profile_render_max_microseconds / 1000.0,
              frames ? (double)probe->profile_swap_microseconds /
                           (1000.0 * frames) : 0.0,
              (double)probe->profile_swap_max_microseconds / 1000.0,
              (unsigned long long)probe->profile_frames_over_20ms,
              (unsigned long long)probe->profile_frames_over_33ms,
              (unsigned long long)probe->profile_frames_over_50ms,
              frames ? (double)probe->profile_gl_draw_calls / frames : 0.0,
              frames ? (double)probe->profile_gl_vertices / frames : 0.0);
    probe_log("ARM bridge profile: imports/frame=%.0f client-copies/frame=%.1f "
              "direct-arrays/frame=%.1f client-copy=%.2f MiB/s "
              "direct-r/w=%llu/%llu "
              "alloc/free=%llu/%llu alloc-bytes=%.2f MiB "
              "apk-opens=%llu file-opens=%llu file-read=%.2f MiB "
              "zlib=%llu apk-cache-h/m=%llu/%llu hot=%s",
              frames ? (double)total_imports / frames : 0.0,
              frames ? (double)probe->profile_gl_client_copies / frames : 0.0,
              frames ? (double)probe->profile_gl_direct_arrays / frames : 0.0,
              elapsed_seconds > 0.0
                  ? (double)probe->profile_gl_client_bytes /
                        (1024.0 * 1024.0 * elapsed_seconds) : 0.0,
              (unsigned long long)(probe->direct_memory_reads -
                                   probe->profile_direct_read_baseline),
              (unsigned long long)(probe->direct_memory_writes -
                                   probe->profile_direct_write_baseline),
              (unsigned long long)probe->profile_alloc_calls,
              (unsigned long long)probe->profile_free_calls,
              (double)probe->profile_alloc_bytes / (1024.0 * 1024.0),
              (unsigned long long)probe->profile_apk_opens,
              (unsigned long long)probe->profile_file_opens,
              (double)probe->profile_file_read_bytes / (1024.0 * 1024.0),
              (unsigned long long)probe->profile_zlib_calls,
              (unsigned long long)(probe->apk_member_cache_hits -
                                   probe->profile_apk_cache_hit_baseline),
              (unsigned long long)(probe->apk_member_cache_misses -
                                   probe->profile_apk_cache_miss_baseline),
              hot_text);
    if (probe->profile_import_timing &&
        probe->profile_import_frequency.QuadPart > 0) {
        unsigned rank;
        char timing_text[768];
        size_t used = 0;
        timing_text[0] = 0;
        for (rank = 0; rank < 8u; ++rank) {
            unsigned best = UINT32_MAX;
            uint64_t best_ticks = 0;
            for (index = 0; index < probe->import_count; ++index) {
                if (probe->profile_import_ticks[index] > best_ticks) {
                    best = index;
                    best_ticks = probe->profile_import_ticks[index];
                }
            }
            if (best == UINT32_MAX || !best_ticks) break;
            {
                double milliseconds =
                    (double)best_ticks * 1000.0 /
                    (double)probe->profile_import_frequency.QuadPart;
                int written = snprintf(
                    timing_text + used, sizeof(timing_text) - used,
                    "%s%s:%.2fms", used ? "," : "",
                    probe->imports[best].name, milliseconds);
                if (written < 0 || (size_t)written >=
                                     sizeof(timing_text) - used)
                    break;
                used += (size_t)written;
                probe->profile_import_ticks[best] = 0;
            }
        }
        probe_log("ARM timed import profile: %s",
                  timing_text[0] ? timing_text : "no callbacks");
    }
    if (probe->profile_arm_blocks && probe->block_profile_entries &&
        !probe->block_profile_deadline) {
        arm_block_profile_report(probe, "current profile window");
        arm_block_profile_clear(probe);
    }
    probe_log("ARM overkill-test profile: gl-proc-cache=%llu/%llu "
              "redundant-binds=%llu direct-uploads=%llu "
              "zip-get/exist/miss/fallback=%llu/%llu/%llu/%llu "
              "particle-guards=%u/%u audio-skips=%llu skipped-draws=%llu "
              "headless-gl=%llu texture-skips=%llu native-render-skips=%llu "
              "flags=P%d,A%d,D%d,S%d,N%d,H%d,T%d",
              (unsigned long long)probe->profile_gl_proc_cache_hits,
              (unsigned long long)probe->profile_gl_proc_cache_misses,
              (unsigned long long)probe->profile_gl_redundant_binds,
              (unsigned long long)probe->profile_gl_direct_uploads,
              (unsigned long long)probe->zip_accel_get_hits,
              (unsigned long long)probe->zip_accel_exist_hits,
              (unsigned long long)probe->zip_accel_misses,
              (unsigned long long)probe->zip_accel_fallbacks,
              probe->particle_claim_guards,
              probe->particle_unclaim_guards,
              (unsigned long long)probe->overkill_audio_skips,
              (unsigned long long)probe->overkill_skipped_draws,
              (unsigned long long)probe->overkill_headless_gl_calls,
              (unsigned long long)probe->overkill_texture_skips,
              (unsigned long long)probe->overkill_native_render_skips,
              probe->disable_particles, probe->disable_node_draws,
              probe->skip_draw_calls, probe->skip_scene_visit,
              probe->skip_native_render, probe->headless_gl,
              probe->skip_texture_uploads);
    arm_frame_profile_reset(probe);
}

static int run_arm_message_loop(ArmProbe *probe) {
    MSG message;
    uint32_t arguments[2] = {GUEST_ENV_OBJECT, 0};
    DWORD performance_started = GetTickCount();
    unsigned performance_frames = 0;
    int performance_active = 0;
    arm_frame_profile_reset(probe);
    probe_log("RESULT: ARM_RENDER_LOOP_ENTERED");
    while (probe->host.window && IsWindow(probe->host.window)) {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return 0;
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        if (probe->host.render && probe->host.window_active &&
            !probe->host.native_paused) {
            uint64_t swap_started;
            uint64_t swap_elapsed;
            uint64_t frame_elapsed;
            if (!performance_active) {
                performance_started = GetTickCount();
                performance_frames = 0;
                arm_frame_profile_reset(probe);
                performance_active = 1;
            }
            if (probe->skip_native_render) {
                probe->profile_last_render_microseconds = 0;
                ++probe->overkill_native_render_skips;
            } else if (!host_call(probe, probe->host.render, arguments, 2,
                                  "nativeRender")) {
                return 0;
            }
            swap_started = host_monotonic_units(UINT64_C(1000000));
            SwapBuffers(probe->host.device);
            swap_elapsed = host_monotonic_units(UINT64_C(1000000)) -
                           swap_started;
            probe->profile_swap_microseconds += swap_elapsed;
            if (swap_elapsed > probe->profile_swap_max_microseconds)
                probe->profile_swap_max_microseconds = swap_elapsed;
            frame_elapsed = probe->profile_last_render_microseconds +
                            swap_elapsed;
            if (frame_elapsed > UINT64_C(20000))
                ++probe->profile_frames_over_20ms;
            if (frame_elapsed > UINT64_C(33333))
                ++probe->profile_frames_over_33ms;
            if (frame_elapsed > UINT64_C(50000))
                ++probe->profile_frames_over_50ms;
            ++performance_frames;
            {
                DWORD now = GetTickCount();
                DWORD elapsed = now - performance_started;
                if (elapsed >= 5000u) {
                    probe_log("ARM render performance: %.1f FPS "
                              "(%u frames / %lu ms) heap=%u MiB "
                              "records=%u/%u free=%u",
                              (double)performance_frames * 1000.0 /
                                  (double)elapsed,
                              performance_frames, (unsigned long)elapsed,
                              (probe->heap_next - GUEST_HEAP_BASE) /
                                  (1024u * 1024u),
                              probe->allocation_count, MAX_ALLOCS,
                              probe->free_allocation_count);
                    arm_frame_profile_log(probe, performance_frames, elapsed);
                    performance_started = now;
                    performance_frames = 0;
                }
                if (probe->profile_arm_blocks &&
                    probe->block_profile_deadline &&
                    (LONG)(now - probe->block_profile_deadline) >= 0)
                    arm_block_profiler_set(probe, 0, 0);
            }
            if (!probe->host.vsync_enabled && !probe->uncapped) Sleep(1);
        } else {
            if (performance_active) {
                performance_started = GetTickCount();
                performance_frames = 0;
                arm_frame_profile_reset(probe);
                performance_active = 0;
            }
            Sleep(16);
        }
    }
    return 0;
}

static int configure_native_exports(ArmProbe *probe) {
    const char *renderer = "org/cocos2dx/lib/Cocos2dxRenderer";
    probe->host.set_apk_path = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath",
        "org/cocos2dx/lib/Cocos2dxHelper", "nativeSetApkPath", 0);
    if (!probe->host.set_apk_path)
        probe->host.set_apk_path = resolve_native(
            probe, "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths",
            "org/cocos2dx/lib/Cocos2dxActivity", "nativeSetPaths", 1);
    probe->host.native_init = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit",
        renderer, "nativeInit", 1);
    probe->host.render = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender",
        renderer, "nativeRender", 1);
    probe->host.touch_begin = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin",
        renderer, "nativeTouchesBegin", 1);
    probe->host.touch_end = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd",
        renderer, "nativeTouchesEnd", 1);
    probe->host.touch_move = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove",
        renderer, "nativeTouchesMove", 1);
    probe->host.key_down = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown",
        renderer, "nativeKeyDown", 1);
    probe->host.insert_text = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInsertText",
        renderer, "nativeInsertText", 1);
    probe->host.delete_backward = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeDeleteBackward",
        renderer, "nativeDeleteBackward", 1);
    probe->host.pause = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause",
        renderer, "nativeOnPause", 0);
    probe->host.resume = resolve_native(
        probe, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnResume",
        renderer, "nativeOnResume", 0);
    return probe->host.set_apk_path && probe->host.native_init &&
           probe->host.render && probe->host.touch_begin &&
           probe->host.touch_end && probe->host.touch_move &&
           probe->host.key_down && probe->host.insert_text &&
           probe->host.delete_backward;
}

int main(int argc, char **argv) {
    ArmProbe probe;
    const char *apk_path = "game.apk";
    const char *library_path = NULL;
    const char *input_path;
    int mode = 2; /* 0 relocation, 1 constructors/JNI, 2 graphical */
    uint32_t jni_on_load;
    uint32_t jni_arguments[2] = {GUEST_VM_OBJECT, 0};
    uint32_t result = 0;
    uint32_t path_string;
    uint32_t path_arguments[3];
    uint32_t init_arguments[4];
    char absolute_apk[MAX_PATH * 4];
    char log_path[MAX_PATH * 4];
    int exit_code = 1;
    int index;

    memset(&probe, 0, sizeof(probe));
    probe.host.native_width = 1280;
    probe.host.native_height = 720;
    probe.host.window_active = 1;
    probe.frame_interval = 1.0 / 60.0;
    if (!executable_directory(probe.executable_directory,
                              sizeof(probe.executable_directory)))
        snprintf(probe.executable_directory,
                 sizeof(probe.executable_directory), ".");
    snprintf(log_path, sizeof(log_path), "%s\\gd-arm-wrapper.log",
             probe.executable_directory);
    g_log_stream = fopen(log_path, "w");
    g_active_probe = &probe;
    SetUnhandledExceptionFilter(log_unhandled_exception);
    probe_log("Geometry Dash ARM native compatibility wrapper "
              "0.9.4-arm-overkilltest2");

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--relocate-only") == 0) mode = 0;
        else if (strcmp(argv[index], "--probe") == 0) mode = 1;
        else if (strncmp(argv[index], "--apk=", 6) == 0) apk_path = argv[index] + 6;
        else if (strncmp(argv[index], "--library=", 10) == 0)
            library_path = argv[index] + 10;
        else if (strncmp(argv[index], "--width=", 8) == 0)
            probe.host.native_width = atoi(argv[index] + 8);
        else if (strncmp(argv[index], "--height=", 9) == 0)
            probe.host.native_height = atoi(argv[index] + 9);
        else if (strcmp(argv[index], "--deep-diagnostics") == 0)
            probe.deep_diagnostics = 1;
        else if (strcmp(argv[index], "--no-particle-guards") == 0)
            probe.disable_particle_guards = 1;
        else if (strcmp(argv[index], "--no-audio") == 0)
            probe.disable_audio = 1;
        else if (strcmp(argv[index], "--no-particles") == 0 ||
                 strcmp(argv[index], "--no-cosmetics") == 0)
            probe.disable_particles = 1;
        else if (strcmp(argv[index], "--no-node-draws") == 0)
            probe.disable_node_draws = 1;
        else if (strcmp(argv[index], "--skip-native-render") == 0)
            probe.skip_native_render = 1;
        else if (strcmp(argv[index], "--skip-draws") == 0)
            probe.skip_draw_calls = 1;
        else if (strcmp(argv[index], "--skip-scene-visit") == 0)
            probe.skip_scene_visit = 1;
        else if (strcmp(argv[index], "--headless-gl") == 0)
            probe.headless_gl = 1;
        else if (strcmp(argv[index], "--no-textures") == 0)
            probe.skip_texture_uploads = 1;
        else if (strcmp(argv[index], "--solid-textures") == 0)
            probe.solid_textures = 1;
        else if (strcmp(argv[index], "--no-vsync") == 0)
            probe.disable_vsync = 1;
        else if (strcmp(argv[index], "--uncapped") == 0)
            probe.uncapped = 1;
        else if (strcmp(argv[index], "--overkill") == 0) {
            probe.disable_audio = 1;
            probe.disable_particles = 1;
            probe.solid_textures = 1;
        } else if (strcmp(argv[index], "--profile-import-time") == 0)
            probe.profile_import_timing = 1;
        else if (strcmp(argv[index], "--profile-arm-blocks") == 0)
            probe.profile_arm_blocks = 1;
        else if (argv[index][0] != '-') apk_path = argv[index];
    }
    if (probe.profile_import_timing) {
        if (!QueryPerformanceFrequency(&probe.profile_import_frequency) ||
            probe.profile_import_frequency.QuadPart <= 0) {
            probe_log("WARNING: import timing profiler unavailable");
            probe.profile_import_timing = 0;
        } else {
            probe_log("ARM import timing profiler enabled");
        }
    }
    if (probe.host.native_width < 320) probe.host.native_width = 320;
    if (probe.host.native_height < 240) probe.host.native_height = 240;
    probe_log("Mode: %s", mode == 0 ? "ARM relocation only" :
              mode == 1 ? "ARM constructors + JNI_OnLoad" :
                          "ARM graphical native boot");

    if (!GetFullPathNameA(apk_path, sizeof(absolute_apk), absolute_apk, NULL))
        snprintf(absolute_apk, sizeof(absolute_apk), "%s", apk_path);
    input_path = library_path ? library_path : absolute_apk;
    snprintf(probe.input_path, sizeof(probe.input_path), "%s", absolute_apk);
    snprintf(probe.writable_path, sizeof(probe.writable_path), "%s\\save",
             probe.executable_directory);
    CreateDirectoryA(probe.writable_path, NULL);
    storage_initialize(probe.writable_path);
    if (!probe.disable_audio) {
        audio_initialize(probe.executable_directory);
        audio_set_apk_path(absolute_apk);
    } else {
        probe_log("OVERKILL audio subsystem REMOVED before initialization");
    }

    overkill_log_state(&probe);
    probe_log("OVERKILL hotkeys: F3 nativeRender, F4 ARM node draws, "
              "F5 particles, F6 host draws, F7 scene visit, "
              "F8 headless GL, F9 future textures, F10 state, "
              "F11 5-second ARM hot-block profile");
    probe_log("Input: %s", input_path);
    if (!load_arm_input(input_path, &probe.file_data, &probe.file_size,
                        &probe.apk_data, &probe.apk_size)) {
        probe_log("RESULT: ARM_LOAD_FAILED");
        exit_code = 2;
        goto finished;
    }
    if (mode == 2 && !probe.apk_data &&
        !load_file(absolute_apk, &probe.apk_data, &probe.apk_size)) {
        probe_log("RESULT: ARM_APK_CACHE_FAILED");
        exit_code = 2;
        goto finished;
    }
    if (probe.apk_data)
        probe_log("ARM shared APK cache ready: %.2f MiB",
                  (double)probe.apk_size / (1024.0 * 1024.0));
    if (!validate_elf(&probe) || !build_image(&probe) ||
        !apply_relocations(&probe) || !initialize_unicorn(&probe)) {
        probe_log("RESULT: ARM_LOAD_FAILED");
        exit_code = 2;
        goto finished;
    }
    probe_log("RESULT: ARM_RELOCATION_OK");
    if (mode == 0) { exit_code = 0; goto finished; }
    if (!run_constructors(&probe)) {
        probe_log("RESULT: ARM_CONSTRUCTORS_FAILED");
        exit_code = 3;
        goto finished;
    }
    jni_on_load = find_export(&probe, "JNI_OnLoad");
    if (!jni_on_load) {
        probe_log("ERROR: JNI_OnLoad export is missing");
        exit_code = 4;
        goto finished;
    }
    probe_log("Calling authentic ARM JNI_OnLoad at guest 0x%08x", jni_on_load);
    if (!run_guest(&probe, jni_on_load, jni_arguments, 2,
                   DEFAULT_GUEST_INSTRUCTION_LIMIT, &result)) {
        probe_log("RESULT: ARM_JNI_ONLOAD_FAILED");
        exit_code = 5;
        goto finished;
    }
    probe_log("ARM JNI_OnLoad returned 0x%08x", result);
    if (result != JNI_VERSION_1_4) {
        probe_log("RESULT: ARM_JNI_ONLOAD_UNEXPECTED");
        exit_code = 6;
        goto finished;
    }
    probe_log("RESULT: ARM_NATIVE_PROBE_OK");
    if (mode == 1) { exit_code = 0; goto finished; }

    if (GetFileAttributesA(absolute_apk) == INVALID_FILE_ATTRIBUTES) {
        probe_log("ERROR: game APK not found: %s", absolute_apk);
        exit_code = 7;
        goto finished;
    }
    if (!configure_native_exports(&probe)) {
        probe_log("RESULT: ARM_NATIVE_EXPORTS_MISSING");
        exit_code = 8;
        goto finished;
    }
    path_string = guest_new_string_ref(&probe, absolute_apk);
    path_arguments[0] = GUEST_ENV_OBJECT;
    path_arguments[1] = 0;
    path_arguments[2] = path_string;
    probe_log("Setting APK path: %s", absolute_apk);
    if (!host_call(&probe, probe.host.set_apk_path, path_arguments, 3,
                   "nativeSetApkPath/nativeSetPaths")) {
        exit_code = 9;
        goto finished;
    }
    probe_log("RESULT: ARM_APK_PATH_SET");

    if (!create_arm_opengl_window(&probe)) {
        probe_log("RESULT: ARM_OPENGL_HOST_FAILED");
        exit_code = 10;
        goto finished;
    }
    probe_log("RESULT: ARM_OPENGL_HOST_OK");
    init_arguments[0] = GUEST_ENV_OBJECT;
    init_arguments[1] = 0;
    init_arguments[2] = (uint32_t)probe.host.native_width;
    init_arguments[3] = (uint32_t)probe.host.native_height;
    probe_log("Calling authentic ARM nativeInit(%d, %d)",
              probe.host.native_width, probe.host.native_height);
    if (!host_call(&probe, probe.host.native_init, init_arguments, 4,
                   "nativeInit")) {
        probe_log("RESULT: ARM_NATIVE_INIT_FAILED");
        exit_code = 11;
        goto finished;
    }
    probe.host.native_ready = 1;
    probe_log("RESULT: ARM_NATIVE_INIT_RETURNED");
    run_arm_message_loop(&probe);
    exit_code = 0;

finished:
    host_pause(&probe, "wrapper shutdown");
    probe.host.native_ready = 0;
    destroy_arm_opengl_window(&probe);
    if (!probe.disable_audio) audio_shutdown();
    storage_shutdown();
    if (probe.host.opengl) FreeLibrary(probe.host.opengl);
    probe.host.opengl = NULL;
    destroy_probe(&probe);
    g_active_probe = NULL;
    if (g_log_stream) { fclose(g_log_stream); g_log_stream = NULL; }
    return exit_code;
}
