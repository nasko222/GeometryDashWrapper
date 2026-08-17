/*
 * Geometry Dash Wrapper native launcher
 * =====================================
 *
 * This file replaces run_auto.py at runtime.  It is deliberately written as
 * straightforward Win32 C with many comments for developers coming from C# or
 * Java.  Think of LauncherContext as a small C# record that is filled in once
 * and then passed to helper methods.
 *
 * Responsibilities:
 *   1. Read APK packages as ZIP files and preserve the existing Android path.
 *   2. Extract package/version metadata from AndroidManifest.xml.
 *   3. Select the x86, legacy ARM, or ARMv7 backend for APKs.
 *   4. PublicTest2: inspect IPA files, then launch the ARMv7 iOS bootstrap backend.
 *   5. Create one dated log folder per Android launch.
 *   6. Set the shared save/title/icon/server environment variables.
 *   7. Start the Android backend and wait for it to exit.
 *
 * No Python installation is required on the user's PC.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "zlib.h"

#define LAUNCHER_VERSION "0.9.6-publictest20"
#define ARRAY_COUNT(value) (sizeof(value) / sizeof((value)[0]))
#define MAX_UTF8_TEXT 512
#define MAX_COMMAND_LINE 32768
#define MAX_ZIP_NAME 1024

/* ZIP signatures from the PKZIP file format. */
#define ZIP_LOCAL_FILE_HEADER 0x04034b50u
#define ZIP_CENTRAL_FILE_HEADER 0x02014b50u
#define ZIP_END_OF_CENTRAL_DIRECTORY 0x06054b50u

/* Android binary XML chunk identifiers. */
#define AXML_XML_CHUNK 0x0003u
#define AXML_STRING_POOL_CHUNK 0x0001u
#define AXML_START_ELEMENT_CHUNK 0x0102u
#define AXML_UTF8_FLAG 0x00000100u
#define AXML_NO_INDEX 0xffffffffu

/*
 * C# analogy: ApkArchive is similar to a disposable FileStream + MemoryMappedFile.
 * ApkArchiveClose is the manual equivalent of using/Dispose.
 */
typedef struct ApkArchive {
    HANDLE file;
    HANDLE mapping;
    const unsigned char *bytes;
    size_t size;
    const unsigned char *central_directory;
    uint16_t entry_count;
} ApkArchive;

typedef struct ZipEntry {
    const unsigned char *name;
    uint16_t name_length;
    uint16_t compression_method;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
} ZipEntry;

typedef struct ApkFeatures {
    int has_x86;
    int has_legacy_arm;
    int has_armv7;
    ZipEntry manifest_entry;
    int has_manifest;
} ApkFeatures;

typedef struct ManifestMetadata {
    char package_name[MAX_UTF8_TEXT];
    char version_name[MAX_UTF8_TEXT];
    char version_code[MAX_UTF8_TEXT];
} ManifestMetadata;

typedef enum BackendKind {
    BACKEND_X86,
    BACKEND_LEGACY_ARM,
    BACKEND_ARMV7
} BackendKind;

typedef struct GameIdentity {
    const wchar_t *window_title;
    const wchar_t *icon_file;
} GameIdentity;

typedef struct LauncherContext {
    wchar_t base_directory[MAX_PATH * 4];
    wchar_t apk_path[MAX_PATH * 4];
    wchar_t backend_path[MAX_PATH * 4];
    wchar_t save_directory[MAX_PATH * 4];
    wchar_t run_directory[MAX_PATH * 4];
    wchar_t log_path[MAX_PATH * 4];
    wchar_t profile_path[MAX_PATH * 4];
    wchar_t profile_summary_path[MAX_PATH * 4];
    wchar_t imports_path[MAX_PATH * 4];
    wchar_t icon_path[MAX_PATH * 4];
    BackendKind backend;
    ManifestMetadata metadata;
    GameIdentity identity;
    ULONGLONG apk_size;
    SYSTEMTIME local_started;
    SYSTEMTIME utc_started;
} LauncherContext;

/* ----------------------------- Safe byte access ----------------------------- */

static uint16_t ReadU16(const unsigned char *data) {
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t ReadU32(const unsigned char *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int RangeFits(size_t total, size_t offset, size_t length) {
    return offset <= total && length <= total - offset;
}

/* ----------------------------- Text/path helpers ---------------------------- */

static void PrintWindowsError(const wchar_t *operation) {
    DWORD code = GetLastError();
    wchar_t message[1024];
    DWORD length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code, 0, message, (DWORD)ARRAY_COUNT(message), NULL);
    if (!length) wcscpy_s(message, ARRAY_COUNT(message), L"Unknown Windows error");
    fwprintf(stderr, L"ERROR: %ls failed (Windows error %lu): %ls\n",
             operation, (unsigned long)code, message);
}

static int Utf8ToWide(const char *source, wchar_t *destination, size_t capacity) {
    int required;
    if (!source || !destination || capacity == 0) return 0;
    required = MultiByteToWideChar(CP_UTF8, 0, source, -1, NULL, 0);
    if (required <= 0 || (size_t)required > capacity) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, source, -1,
                               destination, (int)capacity) > 0;
}

static int PathJoin(wchar_t *destination, size_t capacity,
                    const wchar_t *left, const wchar_t *right) {
    size_t left_length;
    if (!destination || !left || !right || capacity == 0) return 0;
    left_length = wcslen(left);
    if (left_length + 1 + wcslen(right) + 1 > capacity) return 0;
    wcscpy_s(destination, capacity, left);
    if (left_length && destination[left_length - 1] != L'\\' &&
        destination[left_length - 1] != L'/') {
        wcscat_s(destination, capacity, L"\\");
    }
    wcscat_s(destination, capacity, right);
    return 1;
}

static int GetExecutableDirectory(wchar_t *destination, size_t capacity) {
    DWORD length = GetModuleFileNameW(NULL, destination, (DWORD)capacity);
    wchar_t *slash;
    if (!length || length >= capacity) return 0;
    slash = wcsrchr(destination, L'\\');
    if (!slash) slash = wcsrchr(destination, L'/');
    if (slash) *slash = L'\0';
    return 1;
}

static int FileExists(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int DirectoryExists(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* Recursive directory creation, equivalent to Directory.CreateDirectory in C#. */
static int EnsureDirectory(const wchar_t *path) {
    wchar_t copy[MAX_PATH * 4];
    wchar_t *cursor;
    if (!path || !*path) return 0;
    if (DirectoryExists(path)) return 1;
    if (wcslen(path) >= ARRAY_COUNT(copy)) return 0;
    wcscpy_s(copy, ARRAY_COUNT(copy), path);
    cursor = copy;
    if (wcslen(copy) >= 3 && copy[1] == L':') cursor = copy + 3;
    for (; *cursor; ++cursor) {
        if (*cursor != L'\\' && *cursor != L'/') continue;
        {
            wchar_t saved = *cursor;
            *cursor = L'\0';
            if (*copy && !DirectoryExists(copy) && !CreateDirectoryW(copy, NULL) &&
                GetLastError() != ERROR_ALREADY_EXISTS) {
                return 0;
            }
            *cursor = saved;
        }
    }
    return DirectoryExists(copy) || CreateDirectoryW(copy, NULL) ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

static void MakeSafeComponent(const char *source, wchar_t *destination,
                              size_t capacity, const wchar_t *fallback) {
    wchar_t wide[MAX_UTF8_TEXT];
    size_t source_index;
    size_t destination_index = 0;
    if (!Utf8ToWide(source && *source ? source : "", wide, ARRAY_COUNT(wide))) {
        wcscpy_s(wide, ARRAY_COUNT(wide), fallback);
    }
    for (source_index = 0; wide[source_index] && destination_index + 1 < capacity;
         ++source_index) {
        wchar_t value = wide[source_index];
        int allowed = (value >= L'A' && value <= L'Z') ||
                      (value >= L'a' && value <= L'z') ||
                      (value >= L'0' && value <= L'9') ||
                      value == L'.' || value == L'_' || value == L'-';
        destination[destination_index++] = allowed ? value : L'-';
    }
    while (destination_index &&
           (destination[destination_index - 1] == L'.' ||
            destination[destination_index - 1] == L'-' ||
            destination[destination_index - 1] == L'_')) {
        --destination_index;
    }
    destination[destination_index] = L'\0';
    if (!destination_index) wcscpy_s(destination, capacity, fallback);
}

/* ------------------------------- APK ZIP reader ----------------------------- */

static void ApkArchiveClose(ApkArchive *archive) {
    if (!archive) return;
    if (archive->bytes) UnmapViewOfFile(archive->bytes);
    if (archive->mapping) CloseHandle(archive->mapping);
    if (archive->file != INVALID_HANDLE_VALUE && archive->file) CloseHandle(archive->file);
    memset(archive, 0, sizeof(*archive));
    archive->file = INVALID_HANDLE_VALUE;
}

static int ApkArchiveOpen(const wchar_t *path, ApkArchive *archive) {
    LARGE_INTEGER file_size;
    size_t search_start;
    size_t offset;
    const unsigned char *eocd = NULL;
    if (!archive) return 0;
    memset(archive, 0, sizeof(*archive));
    archive->file = INVALID_HANDLE_VALUE;
    archive->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (archive->file == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(archive->file, &file_size) || file_size.QuadPart <= 0 ||
        (ULONGLONG)file_size.QuadPart > (ULONGLONG)SIZE_MAX) {
        ApkArchiveClose(archive);
        return 0;
    }
    archive->size = (size_t)file_size.QuadPart;
    archive->mapping = CreateFileMappingW(archive->file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!archive->mapping) {
        ApkArchiveClose(archive);
        return 0;
    }
    archive->bytes = (const unsigned char *)MapViewOfFile(
        archive->mapping, FILE_MAP_READ, 0, 0, 0);
    if (!archive->bytes) {
        ApkArchiveClose(archive);
        return 0;
    }

    /* EOCD is within the final 65,557 bytes for a non-ZIP64 archive. */
    search_start = archive->size > 65557u ? archive->size - 65557u : 0u;
    if (archive->size >= 22u) {
        offset = archive->size - 22u;
        for (;;) {
            if (ReadU32(archive->bytes + offset) == ZIP_END_OF_CENTRAL_DIRECTORY) {
                eocd = archive->bytes + offset;
                break;
            }
            if (offset == search_start) break;
            --offset;
        }
    }
    if (!eocd || !RangeFits(archive->size, (size_t)(eocd - archive->bytes), 22u)) {
        ApkArchiveClose(archive);
        return 0;
    }
    archive->entry_count = ReadU16(eocd + 10);
    {
        uint32_t central_size = ReadU32(eocd + 12);
        uint32_t central_offset = ReadU32(eocd + 16);
        if (archive->entry_count == 0xffffu || central_size == 0xffffffffu ||
            central_offset == 0xffffffffu ||
            !RangeFits(archive->size, central_offset, central_size)) {
            ApkArchiveClose(archive);
            return 0;
        }
        archive->central_directory = archive->bytes + central_offset;
    }
    return 1;
}

static int ZipNameEquals(const ZipEntry *entry, const char *expected) {
    size_t expected_length = strlen(expected);
    return entry && entry->name_length == expected_length &&
           memcmp(entry->name, expected, expected_length) == 0;
}

static int ApkArchiveVisit(const ApkArchive *archive,
                           int (*visitor)(const ZipEntry *, void *),
                           void *context) {
    const unsigned char *cursor;
    size_t remaining;
    uint16_t index;
    if (!archive || !archive->central_directory || !visitor) return 0;
    cursor = archive->central_directory;
    remaining = archive->size - (size_t)(cursor - archive->bytes);
    for (index = 0; index < archive->entry_count; ++index) {
        ZipEntry entry;
        uint16_t name_length;
        uint16_t extra_length;
        uint16_t comment_length;
        size_t record_size;
        if (remaining < 46u || ReadU32(cursor) != ZIP_CENTRAL_FILE_HEADER) return 0;
        name_length = ReadU16(cursor + 28);
        extra_length = ReadU16(cursor + 30);
        comment_length = ReadU16(cursor + 32);
        record_size = 46u + name_length + extra_length + comment_length;
        if (record_size > remaining) return 0;
        memset(&entry, 0, sizeof(entry));
        entry.compression_method = ReadU16(cursor + 10);
        entry.compressed_size = ReadU32(cursor + 20);
        entry.uncompressed_size = ReadU32(cursor + 24);
        entry.name_length = name_length;
        entry.name = cursor + 46;
        entry.local_header_offset = ReadU32(cursor + 42);
        if (!visitor(&entry, context)) return 1;
        cursor += record_size;
        remaining -= record_size;
    }
    return 1;
}

static int ScanApkEntry(const ZipEntry *entry, void *opaque) {
    ApkFeatures *features = (ApkFeatures *)opaque;
    if (ZipNameEquals(entry, "lib/x86/libcocos2dcpp.so") ||
        ZipNameEquals(entry, "lib/x86/libgame.so")) {
        features->has_x86 = 1;
    } else if (ZipNameEquals(entry, "lib/armeabi/libgame.so")) {
        features->has_legacy_arm = 1;
    } else if (ZipNameEquals(entry, "lib/armeabi-v7a/libcocos2dcpp.so")) {
        features->has_armv7 = 1;
    } else if (ZipNameEquals(entry, "AndroidManifest.xml")) {
        features->manifest_entry = *entry;
        features->has_manifest = 1;
    }
    return 1;
}

static unsigned char *ExtractZipEntry(const ApkArchive *archive,
                                      const ZipEntry *entry,
                                      size_t *output_size) {
    size_t local_offset;
    uint16_t name_length;
    uint16_t extra_length;
    size_t data_offset;
    unsigned char *output;
    if (!archive || !entry || !output_size) return NULL;
    *output_size = 0;
    local_offset = entry->local_header_offset;
    if (!RangeFits(archive->size, local_offset, 30u) ||
        ReadU32(archive->bytes + local_offset) != ZIP_LOCAL_FILE_HEADER) return NULL;
    name_length = ReadU16(archive->bytes + local_offset + 26);
    extra_length = ReadU16(archive->bytes + local_offset + 28);
    data_offset = local_offset + 30u + name_length + extra_length;
    if (!RangeFits(archive->size, data_offset, entry->compressed_size)) return NULL;
    if (entry->uncompressed_size > 64u * 1024u * 1024u) return NULL;
    output = (unsigned char *)malloc((size_t)entry->uncompressed_size + 1u);
    if (!output) return NULL;
    if (entry->compression_method == 0u) {
        if (entry->compressed_size != entry->uncompressed_size) {
            free(output);
            return NULL;
        }
        memcpy(output, archive->bytes + data_offset, entry->uncompressed_size);
    } else if (entry->compression_method == 8u) {
        z_stream stream;
        int status;
        memset(&stream, 0, sizeof(stream));
        stream.next_in = (Bytef *)(archive->bytes + data_offset);
        stream.avail_in = entry->compressed_size;
        stream.next_out = output;
        stream.avail_out = entry->uncompressed_size;
        status = inflateInit2(&stream, -MAX_WBITS);
        if (status != Z_OK) {
            free(output);
            return NULL;
        }
        status = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (status != Z_STREAM_END || stream.total_out != entry->uncompressed_size) {
            free(output);
            return NULL;
        }
    } else {
        free(output);
        return NULL;
    }
    output[entry->uncompressed_size] = 0;
    *output_size = entry->uncompressed_size;
    return output;
}

/* ---------------------------- iOS IPA inspection --------------------------- */

#define MACH_MH_MAGIC 0xfeedfaceu
#define MACH_MH_CIGAM 0xcefaedfeu
#define MACH_MH_MAGIC_64 0xfeedfacfu
#define MACH_MH_CIGAM_64 0xcffaedfeu
#define MACH_FAT_MAGIC_BE 0xcafebabeu
#define MACH_FAT_MAGIC_64_BE 0xcafebabfu
#define MACH_LC_LOAD_DYLIB 0x0000000cu
#define MACH_LC_LOAD_WEAK_DYLIB 0x80000018u
#define MACH_LC_REEXPORT_DYLIB 0x8000001fu
#define MACH_LC_LAZY_LOAD_DYLIB 0x00000020u
#define MACH_LC_LOAD_UPWARD_DYLIB 0x80000023u
#define MACH_LC_MAIN 0x80000028u
#define MACH_LC_ENCRYPTION_INFO 0x00000021u
#define MACH_LC_ENCRYPTION_INFO_64 0x0000002cu
#define MACH_CPU_TYPE_X86 7u
#define MACH_CPU_TYPE_ARM 12u
#define MACH_CPU_ARCH_ABI64 0x01000000u
#define MACH_CPU_TYPE_X86_64 (MACH_CPU_TYPE_X86 | MACH_CPU_ARCH_ABI64)
#define MACH_CPU_TYPE_ARM64 (MACH_CPU_TYPE_ARM | MACH_CPU_ARCH_ABI64)

static uint32_t ReadBE32(const unsigned char *data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static uint64_t ReadBE64(const unsigned char *data) {
    uint64_t value = 0;
    unsigned index;
    for (index = 0; index < 8u; ++index) value = (value << 8) | data[index];
    return value;
}

static uint64_t ReadLE64(const unsigned char *data) {
    uint64_t value = 0;
    unsigned index;
    for (index = 0; index < 8u; ++index) value |= (uint64_t)data[index] << (index * 8u);
    return value;
}

static uint64_t ReadBigEndianInteger(const unsigned char *data, size_t bytes) {
    uint64_t value = 0;
    size_t index;
    if (!data || !bytes || bytes > 8u) return 0;
    for (index = 0; index < bytes; ++index) value = (value << 8) | data[index];
    return value;
}

static int PathHasExtension(const wchar_t *path, const wchar_t *extension) {
    const wchar_t *dot;
    if (!path || !extension) return 0;
    dot = wcsrchr(path, L'.');
    return dot && _wcsicmp(dot, extension) == 0;
}

static int ZipEntryNameCopy(const ZipEntry *entry, char *destination, size_t capacity) {
    size_t length;
    if (!entry || !destination || capacity == 0) return 0;
    length = entry->name_length;
    if (length + 1u > capacity) return 0;
    memcpy(destination, entry->name, length);
    destination[length] = '\0';
    return 1;
}

typedef struct IpaMetadata {
    char bundle_id[MAX_UTF8_TEXT];
    char version_name[MAX_UTF8_TEXT];
    char version_code[MAX_UTF8_TEXT];
    char executable[MAX_UTF8_TEXT];
    char display_name[MAX_UTF8_TEXT];
    char plist_format[32];
} IpaMetadata;

typedef struct BplistContext {
    const unsigned char *data;
    size_t size;
    uint8_t offset_size;
    uint8_t ref_size;
    uint64_t object_count;
    uint64_t top_object;
    uint64_t offset_table_offset;
} BplistContext;

static void IpaMetadataDefaults(IpaMetadata *metadata) {
    if (!metadata) return;
    memset(metadata, 0, sizeof(*metadata));
    strcpy_s(metadata->bundle_id, ARRAY_COUNT(metadata->bundle_id), "unknown");
    strcpy_s(metadata->version_name, ARRAY_COUNT(metadata->version_name), "unknown");
    strcpy_s(metadata->version_code, ARRAY_COUNT(metadata->version_code), "unknown");
    strcpy_s(metadata->executable, ARRAY_COUNT(metadata->executable), "unknown");
    strcpy_s(metadata->display_name, ARRAY_COUNT(metadata->display_name), "unknown");
    strcpy_s(metadata->plist_format, ARRAY_COUNT(metadata->plist_format), "unknown");
}

static int XmlPlistValue(const char *text, const char *key,
                         char *destination, size_t capacity) {
    char needle[256];
    const char *position;
    const char *start;
    const char *end;
    const char *tag_end;
    size_t length;
    if (!text || !key || !destination || capacity == 0) return 0;
    sprintf_s(needle, ARRAY_COUNT(needle), "<key>%s</key>", key);
    position = strstr(text, needle);
    if (!position) return 0;
    position += strlen(needle);
    while (*position && isspace((unsigned char)*position)) ++position;
    if (strncmp(position, "<string>", 8u) == 0) {
        start = position + 8u;
        end = strstr(start, "</string>");
    } else if (strncmp(position, "<integer>", 9u) == 0) {
        start = position + 9u;
        end = strstr(start, "</integer>");
    } else {
        tag_end = strchr(position, '>');
        if (!tag_end) return 0;
        start = tag_end + 1;
        end = strchr(start, '<');
    }
    if (!end || end < start) return 0;
    length = (size_t)(end - start);
    while (length && isspace((unsigned char)start[length - 1u])) --length;
    while (length && isspace((unsigned char)*start)) {
        ++start;
        --length;
    }
    if (length + 1u > capacity) length = capacity - 1u;
    memcpy(destination, start, length);
    destination[length] = '\0';
    return length != 0u;
}

static int BplistInit(const unsigned char *data, size_t size, BplistContext *context) {
    const unsigned char *trailer;
    if (!data || !context || size < 40u || memcmp(data, "bplist00", 8u) != 0) return 0;
    trailer = data + size - 32u;
    memset(context, 0, sizeof(*context));
    context->data = data;
    context->size = size;
    context->offset_size = trailer[6];
    context->ref_size = trailer[7];
    context->object_count = ReadBE64(trailer + 8);
    context->top_object = ReadBE64(trailer + 16);
    context->offset_table_offset = ReadBE64(trailer + 24);
    if (!context->offset_size || context->offset_size > 8u ||
        !context->ref_size || context->ref_size > 8u ||
        !context->object_count || context->top_object >= context->object_count ||
        context->object_count > 1000000u ||
        context->offset_table_offset >= size) return 0;
    if (context->object_count > (uint64_t)((size - (size_t)context->offset_table_offset) /
                                           context->offset_size)) return 0;
    return 1;
}

static int BplistObjectOffset(const BplistContext *context, uint64_t object,
                              size_t *offset) {
    uint64_t table_position;
    uint64_t value;
    if (!context || !offset || object >= context->object_count) return 0;
    table_position = context->offset_table_offset + object * context->offset_size;
    if (table_position > SIZE_MAX ||
        !RangeFits(context->size, (size_t)table_position, context->offset_size)) return 0;
    value = ReadBigEndianInteger(context->data + (size_t)table_position,
                                 context->offset_size);
    if (value >= context->size || value > SIZE_MAX) return 0;
    *offset = (size_t)value;
    return 1;
}

static int BplistReadCount(const BplistContext *context, size_t object_offset,
                           uint8_t marker, uint64_t *count, size_t *payload_offset) {
    uint8_t low;
    size_t cursor;
    if (!context || !count || !payload_offset || object_offset >= context->size) return 0;
    low = marker & 0x0fu;
    cursor = object_offset + 1u;
    if (low != 0x0fu) {
        *count = low;
        *payload_offset = cursor;
        return 1;
    }
    if (!RangeFits(context->size, cursor, 1u)) return 0;
    {
        uint8_t integer_marker = context->data[cursor++];
        size_t integer_bytes;
        if ((integer_marker >> 4) != 0x1u) return 0;
        if ((integer_marker & 0x0fu) > 3u) return 0;
        integer_bytes = (size_t)1u << (integer_marker & 0x0fu);
        if (!RangeFits(context->size, cursor, integer_bytes)) return 0;
        *count = ReadBigEndianInteger(context->data + cursor, integer_bytes);
        cursor += integer_bytes;
    }
    *payload_offset = cursor;
    return 1;
}

static int AppendUtf8Codepoint(char *destination, size_t capacity,
                               size_t *used, uint32_t codepoint) {
    if (!destination || !used || *used >= capacity) return 0;
    if (codepoint <= 0x7fu) {
        if (*used + 1u >= capacity) return 0;
        destination[(*used)++] = (char)codepoint;
    } else if (codepoint <= 0x7ffu) {
        if (*used + 2u >= capacity) return 0;
        destination[(*used)++] = (char)(0xc0u | (codepoint >> 6));
        destination[(*used)++] = (char)(0x80u | (codepoint & 0x3fu));
    } else {
        if (*used + 3u >= capacity) return 0;
        destination[(*used)++] = (char)(0xe0u | (codepoint >> 12));
        destination[(*used)++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        destination[(*used)++] = (char)(0x80u | (codepoint & 0x3fu));
    }
    destination[*used] = '\0';
    return 1;
}

static int BplistDecodeObjectText(const BplistContext *context, uint64_t object,
                                  char *destination, size_t capacity) {
    size_t offset;
    uint8_t marker;
    uint8_t type;
    uint64_t count;
    size_t payload;
    if (!context || !destination || capacity == 0 ||
        !BplistObjectOffset(context, object, &offset)) return 0;
    marker = context->data[offset];
    type = marker >> 4;
    destination[0] = '\0';
    if (type == 0x5u || type == 0x6u) {
        if (!BplistReadCount(context, offset, marker, &count, &payload) || count > SIZE_MAX)
            return 0;
        if (type == 0x5u) {
            size_t length = (size_t)count;
            if (!RangeFits(context->size, payload, length)) return 0;
            if (length + 1u > capacity) length = capacity - 1u;
            memcpy(destination, context->data + payload, length);
            destination[length] = '\0';
            return 1;
        } else {
            size_t index;
            size_t used = 0;
            if (count > SIZE_MAX / 2u ||
                !RangeFits(context->size, payload, (size_t)count * 2u)) return 0;
            for (index = 0; index < (size_t)count; ++index) {
                uint32_t codepoint = ((uint32_t)context->data[payload + index * 2u] << 8) |
                                     context->data[payload + index * 2u + 1u];
                if (!AppendUtf8Codepoint(destination, capacity, &used, codepoint)) break;
            }
            return used != 0u;
        }
    }
    if (type == 0x1u) {
        size_t bytes;
        uint64_t value;
        unsigned power = marker & 0x0fu;
        if (power > 3u) return 0;
        bytes = (size_t)1u << power;
        if (!RangeFits(context->size, offset + 1u, bytes)) return 0;
        value = ReadBigEndianInteger(context->data + offset + 1u, bytes);
        sprintf_s(destination, capacity, "%llu", (unsigned long long)value);
        return 1;
    }
    return 0;
}

static int BplistFindTopDictionaryValue(const BplistContext *context,
                                        const char *wanted_key,
                                        char *destination, size_t capacity) {
    size_t offset;
    uint8_t marker;
    uint64_t count;
    size_t payload;
    size_t refs_bytes;
    uint64_t index;
    if (!context || !wanted_key || !destination || capacity == 0 ||
        !BplistObjectOffset(context, context->top_object, &offset)) return 0;
    marker = context->data[offset];
    if ((marker >> 4) != 0xdu ||
        !BplistReadCount(context, offset, marker, &count, &payload) ||
        count > SIZE_MAX / context->ref_size) return 0;
    refs_bytes = (size_t)count * context->ref_size;
    if (!RangeFits(context->size, payload, refs_bytes) ||
        !RangeFits(context->size, payload + refs_bytes, refs_bytes)) return 0;
    for (index = 0; index < count; ++index) {
        uint64_t key_ref = ReadBigEndianInteger(
            context->data + payload + (size_t)index * context->ref_size,
            context->ref_size);
        char key[128];
        if (!BplistDecodeObjectText(context, key_ref, key, ARRAY_COUNT(key)) ||
            strcmp(key, wanted_key) != 0) continue;
        {
            uint64_t value_ref = ReadBigEndianInteger(
                context->data + payload + refs_bytes +
                    (size_t)index * context->ref_size,
                context->ref_size);
            return BplistDecodeObjectText(context, value_ref,
                                          destination, capacity);
        }
    }
    return 0;
}

static int ParseIpaInfoPlist(const unsigned char *data, size_t size,
                             IpaMetadata *metadata) {
    BplistContext binary;
    if (!data || !metadata || !size) return 0;
    IpaMetadataDefaults(metadata);
    if (size >= 8u && memcmp(data, "bplist00", 8u) == 0 &&
        BplistInit(data, size, &binary)) {
        strcpy_s(metadata->plist_format, ARRAY_COUNT(metadata->plist_format), "binary");
        BplistFindTopDictionaryValue(&binary, "CFBundleIdentifier",
                                    metadata->bundle_id, ARRAY_COUNT(metadata->bundle_id));
        BplistFindTopDictionaryValue(&binary, "CFBundleShortVersionString",
                                    metadata->version_name, ARRAY_COUNT(metadata->version_name));
        BplistFindTopDictionaryValue(&binary, "CFBundleVersion",
                                    metadata->version_code, ARRAY_COUNT(metadata->version_code));
        BplistFindTopDictionaryValue(&binary, "CFBundleExecutable",
                                    metadata->executable, ARRAY_COUNT(metadata->executable));
        if (!BplistFindTopDictionaryValue(&binary, "CFBundleDisplayName",
                                          metadata->display_name,
                                          ARRAY_COUNT(metadata->display_name))) {
            BplistFindTopDictionaryValue(&binary, "CFBundleName",
                                         metadata->display_name,
                                         ARRAY_COUNT(metadata->display_name));
        }
        return 1;
    }
    if (memchr(data, '<', size)) {
        const char *text = (const char *)data;
        strcpy_s(metadata->plist_format, ARRAY_COUNT(metadata->plist_format), "XML");
        XmlPlistValue(text, "CFBundleIdentifier", metadata->bundle_id,
                      ARRAY_COUNT(metadata->bundle_id));
        XmlPlistValue(text, "CFBundleShortVersionString", metadata->version_name,
                      ARRAY_COUNT(metadata->version_name));
        XmlPlistValue(text, "CFBundleVersion", metadata->version_code,
                      ARRAY_COUNT(metadata->version_code));
        XmlPlistValue(text, "CFBundleExecutable", metadata->executable,
                      ARRAY_COUNT(metadata->executable));
        if (!XmlPlistValue(text, "CFBundleDisplayName", metadata->display_name,
                           ARRAY_COUNT(metadata->display_name))) {
            XmlPlistValue(text, "CFBundleName", metadata->display_name,
                          ARRAY_COUNT(metadata->display_name));
        }
        return 1;
    }
    return 0;
}

typedef struct IpaRootScan {
    int has_info;
    ZipEntry info_entry;
    char app_prefix[MAX_ZIP_NAME];
} IpaRootScan;

static int ScanIpaRootEntry(const ZipEntry *entry, void *opaque) {
    IpaRootScan *scan = (IpaRootScan *)opaque;
    char name[MAX_ZIP_NAME];
    const char *component;
    const char *slash;
    const char *suffix;
    size_t prefix_length;
    if (!scan || scan->has_info || !ZipEntryNameCopy(entry, name, ARRAY_COUNT(name))) return 1;
    if (strncmp(name, "Payload/", 8u) != 0) return 1;
    component = name + 8u;
    slash = strchr(component, '/');
    if (!slash || (size_t)(slash - component) < 4u ||
        strncmp(slash - 4, ".app", 4u) != 0) return 1;
    suffix = slash + 1;
    if (strcmp(suffix, "Info.plist") != 0) return 1;
    prefix_length = (size_t)(slash + 1 - name);
    if (prefix_length + 1u > ARRAY_COUNT(scan->app_prefix)) return 1;
    memcpy(scan->app_prefix, name, prefix_length);
    scan->app_prefix[prefix_length] = '\0';
    scan->info_entry = *entry;
    scan->has_info = 1;
    return 1;
}

typedef struct ExactZipEntrySearch {
    const char *name;
    int found;
    ZipEntry entry;
} ExactZipEntrySearch;

static int FindExactZipEntryVisitor(const ZipEntry *entry, void *opaque) {
    ExactZipEntrySearch *search = (ExactZipEntrySearch *)opaque;
    if (search && !search->found && ZipNameEquals(entry, search->name)) {
        search->entry = *entry;
        search->found = 1;
    }
    return 1;
}

typedef struct IpaTopFileSearch {
    const char *prefix;
    int found;
    ZipEntry entry;
    uint32_t largest_size;
} IpaTopFileSearch;

static int FindIpaTopFileVisitor(const ZipEntry *entry, void *opaque) {
    IpaTopFileSearch *search = (IpaTopFileSearch *)opaque;
    char name[MAX_ZIP_NAME];
    const char *remainder;
    size_t prefix_length;
    if (!search || !search->prefix ||
        !ZipEntryNameCopy(entry, name, ARRAY_COUNT(name))) return 1;
    prefix_length = strlen(search->prefix);
    if (strncmp(name, search->prefix, prefix_length) != 0) return 1;
    remainder = name + prefix_length;
    if (!*remainder || strchr(remainder, '/') || strcmp(remainder, "Info.plist") == 0)
        return 1;
    if (entry->uncompressed_size >= 4u &&
        (!search->found || entry->uncompressed_size > search->largest_size)) {
        search->entry = *entry;
        search->largest_size = entry->uncompressed_size;
        search->found = 1;
    }
    return 1;
}

static const char *MachCpuName(uint32_t cpu_type, uint32_t cpu_subtype) {
    uint32_t subtype = cpu_subtype & 0x00ffffffu;
    if (cpu_type == MACH_CPU_TYPE_ARM) {
        if (subtype == 9u) return "armv7";
        if (subtype == 11u) return "armv7s";
        if (subtype == 12u) return "armv7k";
        if (subtype == 6u) return "armv6";
        return "arm32";
    }
    if (cpu_type == MACH_CPU_TYPE_ARM64) return "arm64";
    if (cpu_type == MACH_CPU_TYPE_X86) return "x86";
    if (cpu_type == MACH_CPU_TYPE_X86_64) return "x86_64";
    return "unknown";
}

static int IsMachDylibCommand(uint32_t command) {
    return command == MACH_LC_LOAD_DYLIB ||
           command == MACH_LC_LOAD_WEAK_DYLIB ||
           command == MACH_LC_REEXPORT_DYLIB ||
           command == MACH_LC_LAZY_LOAD_DYLIB ||
           command == MACH_LC_LOAD_UPWARD_DYLIB;
}

static int AnalyzeThinMachO(const unsigned char *data, size_t size,
                            size_t slice_offset, size_t slice_size,
                            const char **primary_arch, int *encrypted_out) {
    const unsigned char *slice;
    uint32_t magic;
    int swapped = 0;
    int is64 = 0;
    uint32_t cpu_type;
    uint32_t cpu_subtype;
    uint32_t file_type;
    uint32_t command_count;
    uint32_t command_bytes;
    size_t header_size;
    size_t command_offset;
    uint32_t index;
    uint64_t entry_offset = UINT64_MAX;
    int encrypted = 0;
    unsigned dylib_count = 0;
    if (!data || !RangeFits(size, slice_offset, slice_size) || slice_size < 28u) return 0;
    slice = data + slice_offset;
    magic = ReadU32(slice);
    if (magic == MACH_MH_MAGIC) {
        swapped = 0;
        is64 = 0;
    } else if (magic == MACH_MH_MAGIC_64) {
        swapped = 0;
        is64 = 1;
    } else if (magic == MACH_MH_CIGAM) {
        swapped = 1;
        is64 = 0;
    } else if (magic == MACH_MH_CIGAM_64) {
        swapped = 1;
        is64 = 1;
    } else {
        return 0;
    }
#define MACH_READ32(ptr) (swapped ? ReadBE32(ptr) : ReadU32(ptr))
    header_size = is64 ? 32u : 28u;
    if (slice_size < header_size) return 0;
    cpu_type = MACH_READ32(slice + 4);
    cpu_subtype = MACH_READ32(slice + 8);
    file_type = MACH_READ32(slice + 12);
    command_count = MACH_READ32(slice + 16);
    command_bytes = MACH_READ32(slice + 20);
    if (!RangeFits(slice_size, header_size, command_bytes)) return 0;
    if (primary_arch) *primary_arch = MachCpuName(cpu_type, cpu_subtype);
    printf("Mach-O slice: %s (%s)\n", MachCpuName(cpu_type, cpu_subtype),
           is64 ? "64-bit" : "32-bit");
    printf("Mach-O file type: 0x%08lx%s\n", (unsigned long)file_type,
           file_type == 2u ? " (executable)" : "");
    printf("Load commands: %lu\n", (unsigned long)command_count);
    command_offset = header_size;
    for (index = 0; index < command_count; ++index) {
        const unsigned char *command;
        uint32_t command_type;
        uint32_t command_size;
        if (!RangeFits(slice_size, command_offset, 8u)) return 0;
        command = slice + command_offset;
        command_type = MACH_READ32(command);
        command_size = MACH_READ32(command + 4);
        if (command_size < 8u || !RangeFits(slice_size, command_offset, command_size)) return 0;
        if (command_type == MACH_LC_MAIN && command_size >= 24u) {
            entry_offset = swapped ? ReadBE64(command + 8) : ReadLE64(command + 8);
        } else if ((command_type == MACH_LC_ENCRYPTION_INFO ||
                    command_type == MACH_LC_ENCRYPTION_INFO_64) && command_size >= 20u) {
            if (MACH_READ32(command + 16) != 0u) encrypted = 1;
        } else if (IsMachDylibCommand(command_type) && command_size >= 24u) {
            uint32_t name_offset = MACH_READ32(command + 8);
            if (name_offset < command_size) {
                const char *name = (const char *)(command + name_offset);
                size_t available = command_size - name_offset;
                if (memchr(name, '\0', available)) {
                    if (dylib_count < 24u) printf("  import: %s\n", name);
                    ++dylib_count;
                }
            }
        }
        command_offset += command_size;
    }
    if (entry_offset != UINT64_MAX)
        printf("Entry offset: 0x%llx (LC_MAIN)\n", (unsigned long long)entry_offset);
    else
        printf("Entry offset: legacy thread entry / no LC_MAIN\n");
    printf("Imported dylibs/frameworks: %u%s\n", dylib_count,
           dylib_count > 24u ? " (first 24 shown)" : "");
    printf("App Store encryption flag: %s\n", encrypted ? "ENCRYPTED" : "not encrypted");
    if (encrypted_out) *encrypted_out = encrypted;
#undef MACH_READ32
    return 1;
}

static int AnalyzeMachO(const unsigned char *data, size_t size,
                        const char **primary_arch, int *encrypted_out) {
    uint32_t magic_le;
    if (!data || size < 4u) return 0;
    magic_le = ReadU32(data);
    if (magic_le == MACH_MH_MAGIC || magic_le == MACH_MH_MAGIC_64 ||
        magic_le == MACH_MH_CIGAM || magic_le == MACH_MH_CIGAM_64) {
        return AnalyzeThinMachO(data, size, 0u, size, primary_arch, encrypted_out);
    }
    if (ReadBE32(data) == MACH_FAT_MAGIC_BE || ReadBE32(data) == MACH_FAT_MAGIC_64_BE) {
        int fat64 = ReadBE32(data) == MACH_FAT_MAGIC_64_BE;
        uint32_t count;
        size_t entry_size = fat64 ? 32u : 20u;
        size_t table_offset = 8u;
        uint32_t index;
        uint64_t chosen_offset = 0;
        uint64_t chosen_size = 0;
        int have_chosen = 0;
        if (size < 8u) return 0;
        count = ReadBE32(data + 4);
        if (count > 64u || !RangeFits(size, table_offset, (size_t)count * entry_size)) return 0;
        printf("Fat Mach-O architectures: %lu\n", (unsigned long)count);
        for (index = 0; index < count; ++index) {
            const unsigned char *arch = data + table_offset + (size_t)index * entry_size;
            uint32_t cpu_type = ReadBE32(arch);
            uint32_t cpu_subtype = ReadBE32(arch + 4);
            uint64_t offset = fat64 ? ReadBE64(arch + 8) : ReadBE32(arch + 8);
            uint64_t arch_size = fat64 ? ReadBE64(arch + 16) : ReadBE32(arch + 12);
            printf("  %s: offset=0x%llx size=%llu\n",
                   MachCpuName(cpu_type, cpu_subtype),
                   (unsigned long long)offset, (unsigned long long)arch_size);
            if (!have_chosen &&
                (cpu_type == MACH_CPU_TYPE_ARM || cpu_type == MACH_CPU_TYPE_ARM64) &&
                offset <= SIZE_MAX && arch_size <= SIZE_MAX &&
                RangeFits(size, (size_t)offset, (size_t)arch_size)) {
                chosen_offset = offset;
                chosen_size = arch_size;
                have_chosen = 1;
            }
        }
        if (!have_chosen) return 0;
        printf("Inspecting first ARM-family slice:\n");
        return AnalyzeThinMachO(data, size, (size_t)chosen_offset,
                                (size_t)chosen_size, primary_arch, encrypted_out);
    }
    return 0;
}

static int AnalyzeIpaInput(const wchar_t *input_path) {
    wchar_t ipa_path[MAX_PATH * 4];
    DWORD full_length;
    ApkArchive archive;
    IpaRootScan root;
    IpaMetadata metadata;
    unsigned char *plist = NULL;
    size_t plist_size = 0;
    ExactZipEntrySearch executable_search;
    IpaTopFileSearch fallback_search;
    ZipEntry executable_entry;
    int have_executable = 0;
    unsigned char *executable = NULL;
    size_t executable_size = 0;
    char executable_path[MAX_ZIP_NAME * 2];
    const char *primary_arch = "unknown";
    int encrypted = 0;
    if (!input_path || !*input_path) return 0;
    full_length = GetFullPathNameW(input_path, (DWORD)ARRAY_COUNT(ipa_path), ipa_path, NULL);
    if (!full_length || full_length >= ARRAY_COUNT(ipa_path)) {
        PrintWindowsError(L"GetFullPathName");
        return 0;
    }
    if (!FileExists(ipa_path)) {
        fwprintf(stderr, L"ERROR: IPA not found: %ls\n", ipa_path);
        return 0;
    }
    if (!ApkArchiveOpen(ipa_path, &archive)) {
        fwprintf(stderr, L"ERROR: Could not read IPA ZIP structure.\n");
        return 0;
    }
    memset(&root, 0, sizeof(root));
    if (!ApkArchiveVisit(&archive, ScanIpaRootEntry, &root) || !root.has_info) {
        ApkArchiveClose(&archive);
        fwprintf(stderr, L"ERROR: IPA has no Payload/<app>.app/Info.plist root bundle.\n");
        return 0;
    }
    plist = ExtractZipEntry(&archive, &root.info_entry, &plist_size);
    if (!plist) {
        ApkArchiveClose(&archive);
        fwprintf(stderr, L"ERROR: Could not extract Info.plist from IPA.\n");
        return 0;
    }
    ParseIpaInfoPlist(plist, plist_size, &metadata);
    free(plist);

    memset(&executable_search, 0, sizeof(executable_search));
    executable_path[0] = '\0';
    if (strcmp(metadata.executable, "unknown") != 0) {
        if (strlen(root.app_prefix) + strlen(metadata.executable) + 1u < ARRAY_COUNT(executable_path)) {
            strcpy_s(executable_path, ARRAY_COUNT(executable_path), root.app_prefix);
            strcat_s(executable_path, ARRAY_COUNT(executable_path), metadata.executable);
            executable_search.name = executable_path;
            ApkArchiveVisit(&archive, FindExactZipEntryVisitor, &executable_search);
            if (executable_search.found) {
                executable_entry = executable_search.entry;
                have_executable = 1;
            }
        }
    }
    if (!have_executable) {
        char app_name[MAX_UTF8_TEXT];
        const char *component = root.app_prefix + 8u;
        const char *app_suffix = strstr(component, ".app/");
        if (app_suffix && (size_t)(app_suffix - component) + 1u < ARRAY_COUNT(app_name)) {
            size_t name_length = (size_t)(app_suffix - component);
            memcpy(app_name, component, name_length);
            app_name[name_length] = '\0';
            if (strlen(root.app_prefix) + name_length + 1u < ARRAY_COUNT(executable_path)) {
                strcpy_s(executable_path, ARRAY_COUNT(executable_path), root.app_prefix);
                strcat_s(executable_path, ARRAY_COUNT(executable_path), app_name);
                memset(&executable_search, 0, sizeof(executable_search));
                executable_search.name = executable_path;
                ApkArchiveVisit(&archive, FindExactZipEntryVisitor, &executable_search);
                if (executable_search.found) {
                    executable_entry = executable_search.entry;
                    have_executable = 1;
                    strcpy_s(metadata.executable, ARRAY_COUNT(metadata.executable), app_name);
                }
            }
        }
    }
    if (!have_executable) {
        memset(&fallback_search, 0, sizeof(fallback_search));
        fallback_search.prefix = root.app_prefix;
        ApkArchiveVisit(&archive, FindIpaTopFileVisitor, &fallback_search);
        if (fallback_search.found) {
            executable_entry = fallback_search.entry;
            have_executable = 1;
            ZipEntryNameCopy(&executable_entry, executable_path, ARRAY_COUNT(executable_path));
        }
    }

    printf("Geometry Dash Wrapper %s - iOS IPA analyzer\n", LAUNCHER_VERSION);
    wprintf(L"IPA: %ls\n", ipa_path);
    printf("App bundle: %s\n", root.app_prefix);
    printf("Info.plist: %s\n", metadata.plist_format);
    printf("Name: %s\n", metadata.display_name);
    printf("Bundle ID: %s\n", metadata.bundle_id);
    printf("Version: %s (build %s)\n", metadata.version_name, metadata.version_code);
    printf("CFBundleExecutable: %s\n", metadata.executable);
    if (!have_executable) {
        ApkArchiveClose(&archive);
        printf("RESULT: IPA_ANALYZER_NO_EXECUTABLE\n");
        printf("Execution status: no executable was found, so the iOS bootstrap cannot start.\n");
        return 0;
    }
    executable = ExtractZipEntry(&archive, &executable_entry, &executable_size);
    if (!executable) {
        ApkArchiveClose(&archive);
        printf("RESULT: IPA_ANALYZER_EXECUTABLE_EXTRACT_FAILED\n");
        printf("Executable is too large, compressed with an unsupported ZIP method, or damaged.\n");
        return 0;
    }
    if (!*executable_path) ZipEntryNameCopy(&executable_entry, executable_path,
                                            ARRAY_COUNT(executable_path));
    printf("Executable member: %s (%lu bytes)\n", executable_path,
           (unsigned long)executable_size);
    if (!AnalyzeMachO(executable, executable_size, &primary_arch, &encrypted)) {
        free(executable);
        ApkArchiveClose(&archive);
        printf("RESULT: IPA_ANALYZER_NOT_MACHO\n");
        printf("Execution status: selected app executable was not recognized as Mach-O.\n");
        return 0;
    }
    printf("CPU assessment: ");
    if (strcmp(primary_arch, "armv7") == 0 || strcmp(primary_arch, "armv7s") == 0 ||
        strcmp(primary_arch, "armv6") == 0 || strcmp(primary_arch, "arm32") == 0) {
        printf("existing Dynarmic A32 core is reusable; Mach-O/iOS runtime still needed.\n");
    } else if (strcmp(primary_arch, "arm64") == 0) {
        printf("new AArch64 execution path plus Mach-O/iOS runtime is required.\n");
    } else {
        printf("architecture needs a dedicated iOS loader/runtime path.\n");
    }
    if (encrypted) {
        printf("Execution blocker: executable reports App Store encryption. PublicTest20 will not attempt to bypass it.\n");
    }
    printf("RESULT: IPA_ANALYZER_OK arch=%s encrypted=%d\n", primary_arch, encrypted);
    printf("Execution status: PublicTest20 will now attempt the separate ARMv7 iOS bootstrap; Android APK handling is unchanged.\n");
    free(executable);
    ApkArchiveClose(&archive);
    return 1;
}


/* ----------------------- Android binary manifest reader --------------------- */

typedef struct AxmlStringPool {
    const unsigned char *chunk;
    size_t chunk_size;
    uint16_t header_size;
    uint32_t string_count;
    uint32_t flags;
    uint32_t strings_start;
} AxmlStringPool;

static int DecodeLength8(const unsigned char *data, size_t size,
                         size_t *offset, uint32_t *value) {
    unsigned first;
    if (!data || !offset || !value || *offset >= size) return 0;
    first = data[(*offset)++];
    if (first & 0x80u) {
        if (*offset >= size) return 0;
        *value = ((first & 0x7fu) << 8) | data[(*offset)++];
    } else {
        *value = first;
    }
    return 1;
}

static int DecodeLength16(const unsigned char *data, size_t size,
                          size_t *offset, uint32_t *value) {
    uint16_t first;
    if (!data || !offset || !value || !RangeFits(size, *offset, 2u)) return 0;
    first = ReadU16(data + *offset);
    *offset += 2u;
    if (first & 0x8000u) {
        uint16_t second;
        if (!RangeFits(size, *offset, 2u)) return 0;
        second = ReadU16(data + *offset);
        *offset += 2u;
        *value = ((uint32_t)(first & 0x7fffu) << 16) | second;
    } else {
        *value = first;
    }
    return 1;
}

static int AxmlGetString(const AxmlStringPool *pool, uint32_t index,
                         char *destination, size_t capacity) {
    uint32_t relative;
    size_t offset;
    const unsigned char *data;
    size_t data_size;
    if (!pool || !destination || capacity == 0 || index >= pool->string_count) return 0;
    if (!RangeFits(pool->chunk_size, pool->header_size + index * 4u, 4u)) return 0;
    relative = ReadU32(pool->chunk + pool->header_size + index * 4u);
    if (pool->strings_start > pool->chunk_size ||
        relative > pool->chunk_size - pool->strings_start) return 0;
    data = pool->chunk + pool->strings_start;
    data_size = pool->chunk_size - pool->strings_start;
    offset = relative;
    if (pool->flags & AXML_UTF8_FLAG) {
        uint32_t utf16_length;
        uint32_t byte_length;
        if (!DecodeLength8(data, data_size, &offset, &utf16_length) ||
            !DecodeLength8(data, data_size, &offset, &byte_length) ||
            !RangeFits(data_size, offset, byte_length)) return 0;
        (void)utf16_length;
        if ((size_t)byte_length + 1u > capacity) byte_length = (uint32_t)(capacity - 1u);
        memcpy(destination, data + offset, byte_length);
        destination[byte_length] = '\0';
        return 1;
    } else {
        uint32_t char_length;
        int converted;
        if (!DecodeLength16(data, data_size, &offset, &char_length) ||
            !RangeFits(data_size, offset, (size_t)char_length * 2u)) return 0;
        converted = WideCharToMultiByte(
            CP_UTF8, 0, (const wchar_t *)(data + offset), (int)char_length,
            destination, (int)(capacity - 1u), NULL, NULL);
        if (converted < 0) return 0;
        destination[converted] = '\0';
        return 1;
    }
}

static void ManifestMetadataDefaults(ManifestMetadata *metadata) {
    strcpy_s(metadata->package_name, ARRAY_COUNT(metadata->package_name), "unknown.package");
    strcpy_s(metadata->version_name, ARRAY_COUNT(metadata->version_name), "unknown");
    strcpy_s(metadata->version_code, ARRAY_COUNT(metadata->version_code), "unknown");
}

static int ParseAndroidManifest(const unsigned char *data, size_t size,
                                ManifestMetadata *metadata) {
    size_t offset;
    size_t limit;
    AxmlStringPool pool;
    int have_pool = 0;
    if (!data || !metadata || size < 8u) return 0;
    ManifestMetadataDefaults(metadata);
    if (ReadU16(data) != AXML_XML_CHUNK || ReadU16(data + 2) < 8u) return 0;
    limit = ReadU32(data + 4);
    if (limit > size) limit = size;
    offset = ReadU16(data + 2);
    memset(&pool, 0, sizeof(pool));
    while (RangeFits(limit, offset, 8u)) {
        uint16_t chunk_type = ReadU16(data + offset);
        uint16_t header_size = ReadU16(data + offset + 2);
        uint32_t chunk_size = ReadU32(data + offset + 4);
        if (chunk_size < 8u || !RangeFits(limit, offset, chunk_size)) break;
        if (chunk_type == AXML_STRING_POOL_CHUNK && header_size >= 28u) {
            pool.chunk = data + offset;
            pool.chunk_size = chunk_size;
            pool.header_size = header_size;
            pool.string_count = ReadU32(data + offset + 8);
            pool.flags = ReadU32(data + offset + 16);
            pool.strings_start = ReadU32(data + offset + 20);
            have_pool = 1;
        } else if (chunk_type == AXML_START_ELEMENT_CHUNK && have_pool &&
                   chunk_size >= 36u) {
            uint32_t element_index = ReadU32(data + offset + 20);
            char element_name[64];
            if (AxmlGetString(&pool, element_index, element_name,
                              ARRAY_COUNT(element_name)) &&
                strcmp(element_name, "manifest") == 0) {
                uint16_t attribute_start = ReadU16(data + offset + 24);
                uint16_t attribute_size = ReadU16(data + offset + 26);
                uint16_t attribute_count = ReadU16(data + offset + 28);
                size_t attributes_base = offset + 16u + attribute_start;
                uint16_t index;
                if (attribute_size < 20u) return 0;
                for (index = 0; index < attribute_count; ++index) {
                    size_t item = attributes_base + (size_t)index * attribute_size;
                    uint32_t name_index;
                    uint32_t raw_index;
                    uint8_t value_type;
                    uint32_t value_data;
                    char attribute_name[96];
                    char value[MAX_UTF8_TEXT];
                    if (!RangeFits(offset + chunk_size, item, 20u)) break;
                    name_index = ReadU32(data + item + 4);
                    raw_index = ReadU32(data + item + 8);
                    value_type = data[item + 15];
                    value_data = ReadU32(data + item + 16);
                    if (!AxmlGetString(&pool, name_index, attribute_name,
                                      ARRAY_COUNT(attribute_name))) continue;
                    value[0] = '\0';
                    if (raw_index != AXML_NO_INDEX) {
                        AxmlGetString(&pool, raw_index, value, ARRAY_COUNT(value));
                    } else if (value_type == 0x03u) {
                        AxmlGetString(&pool, value_data, value, ARRAY_COUNT(value));
                    } else if (value_type == 0x10u || value_type == 0x11u) {
                        sprintf_s(value, ARRAY_COUNT(value), "%lu",
                                  (unsigned long)value_data);
                    } else if (value_type == 0x12u) {
                        strcpy_s(value, ARRAY_COUNT(value), value_data ? "true" : "false");
                    }
                    if (!*value) continue;
                    if (strcmp(attribute_name, "package") == 0) {
                        strcpy_s(metadata->package_name,
                                 ARRAY_COUNT(metadata->package_name), value);
                    } else if (strcmp(attribute_name, "versionName") == 0) {
                        strcpy_s(metadata->version_name,
                                 ARRAY_COUNT(metadata->version_name), value);
                    } else if (strcmp(attribute_name, "versionCode") == 0) {
                        strcpy_s(metadata->version_code,
                                 ARRAY_COUNT(metadata->version_code), value);
                    }
                }
                return 1;
            }
        }
        offset += chunk_size;
    }
    return strcmp(metadata->package_name, "unknown.package") != 0;
}

/* -------------------------- Backend/game identification --------------------- */

static BackendKind ChooseBackend(const ApkFeatures *features) {
    /* Prefer x86 whenever an APK provides it, matching the historical launcher. */
    if (features->has_x86) return BACKEND_X86;
    if (features->has_legacy_arm) return BACKEND_LEGACY_ARM;
    return BACKEND_ARMV7;
}

static const wchar_t *BackendName(BackendKind backend) {
    switch (backend) {
    case BACKEND_X86: return L"x86";
    case BACKEND_LEGACY_ARM: return L"arm-legacy";
    case BACKEND_ARMV7: return L"armv7";
    }
    return L"unknown";
}

/*
 * Geometry Dash 2.11 commonly reports versionName 2.111.  Match the 2.11
 * prefix so the launcher can choose the socket flow that this client expects.
 */
static int IsX86Version211(const LauncherContext *context) {
    const char *version;
    if (!context || context->backend != BACKEND_X86) return 0;
    version = context->metadata.version_name;
    return version && strncmp(version, "2.11", 4u) == 0;
}

static GameIdentity IdentifyGame(const char *package_name) {
    GameIdentity identity;
    identity.window_title = L"Geometry Dash";
    identity.icon_file = L"geometry-dash.ico";
    if (package_name && strstr(package_name, "subzero")) {
        identity.window_title = L"Geometry Dash SubZero";
        identity.icon_file = L"geometry-dash-subzero.ico";
    } else if (package_name && strstr(package_name, "meltdown")) {
        identity.window_title = L"Geometry Dash Meltdown";
        identity.icon_file = L"geometry-dash-meltdown.ico";
    } else if (package_name && strstr(package_name, "world")) {
        identity.window_title = L"Geometry Dash World";
        identity.icon_file = L"geometry-dash-world.ico";
    } else if (package_name && strstr(package_name, "lite")) {
        identity.window_title = L"Geometry Dash Lite";
        identity.icon_file = L"geometry-dash-lite.ico";
    }
    return identity;
}

/* ----------------------------- Per-run logging ------------------------------ */

static int CreateUniqueRunDirectory(LauncherContext *context) {
    wchar_t logs_directory[MAX_PATH * 4];
    wchar_t day_directory[MAX_PATH * 4];
    wchar_t package_component[MAX_UTF8_TEXT];
    wchar_t version_component[MAX_UTF8_TEXT];
    wchar_t folder_name[1024];
    unsigned suffix;
    if (!PathJoin(logs_directory, ARRAY_COUNT(logs_directory),
                  context->base_directory, L"logs")) return 0;
    swprintf_s(day_directory, ARRAY_COUNT(day_directory), L"%ls\\%04u-%02u-%02u",
               logs_directory,
               context->local_started.wYear,
               context->local_started.wMonth,
               context->local_started.wDay);
    if (!EnsureDirectory(day_directory)) return 0;
    MakeSafeComponent(context->metadata.package_name, package_component,
                      ARRAY_COUNT(package_component), L"unknown.package");
    MakeSafeComponent(context->metadata.version_name, version_component,
                      ARRAY_COUNT(version_component), L"unknown");
    swprintf_s(folder_name, ARRAY_COUNT(folder_name),
               L"%02u-%02u-%02u__%ls__v%ls__%ls",
               context->local_started.wHour,
               context->local_started.wMinute,
               context->local_started.wSecond,
               package_component,
               version_component,
               BackendName(context->backend));
    for (suffix = 1u; suffix < 1000u; ++suffix) {
        if (suffix == 1u) {
            swprintf_s(context->run_directory, ARRAY_COUNT(context->run_directory),
                       L"%ls\\%ls", day_directory, folder_name);
        } else {
            swprintf_s(context->run_directory, ARRAY_COUNT(context->run_directory),
                       L"%ls\\%ls__%02u", day_directory, folder_name, suffix);
        }
        if (CreateDirectoryW(context->run_directory, NULL)) return 1;
        if (GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    }
    return 0;
}

static const wchar_t *GetSetting(const wchar_t *name, const wchar_t *fallback,
                                 wchar_t *buffer, size_t capacity) {
    DWORD length = GetEnvironmentVariableW(name, buffer, (DWORD)capacity);
    if (!length || length >= capacity) {
        wcscpy_s(buffer, capacity, fallback);
    }
    return buffer;
}

/*
 * Environment switches use the same friendly values as .NET configuration:
 * true/false, yes/no, on/off, and 1/0. Unknown non-empty values keep the
 * supplied default instead of silently changing behaviour.
 */
static int GetBooleanSetting(const wchar_t *name, int fallback) {
    wchar_t value[64];
    DWORD length = GetEnvironmentVariableW(name, value, ARRAY_COUNT(value));
    if (!length || length >= ARRAY_COUNT(value)) return fallback;
    if (_wcsicmp(value, L"true") == 0 || _wcsicmp(value, L"yes") == 0 ||
        _wcsicmp(value, L"on") == 0 || wcscmp(value, L"1") == 0)
        return 1;
    if (_wcsicmp(value, L"false") == 0 || _wcsicmp(value, L"no") == 0 ||
        _wcsicmp(value, L"off") == 0 || wcscmp(value, L"0") == 0)
        return 0;
    return fallback;
}

static int WriteRunInfo(const LauncherContext *context, int finished,
                        DWORD exit_code, const wchar_t *launcher_error) {
    wchar_t path[MAX_PATH * 4];
    FILE *file = NULL;
    wchar_t server[1024];
    wchar_t hack_icons[64];
    wchar_t full_bypass[64];
    wchar_t highest[64];
    wchar_t pulse[64];
    wchar_t isolated_saves[64];
    wchar_t network_mode[64];
    if (!PathJoin(path, ARRAY_COUNT(path), context->run_directory,
                  L"run-info.txt")) return 0;
    if (_wfopen_s(&file, path, L"wb, ccs=UTF-8") != 0 || !file) return 0;
    fwprintf(file, L"launcher_version=%hs\n", LAUNCHER_VERSION);
    fwprintf(file, L"started_local=%04u-%02u-%02uT%02u:%02u:%02u\n",
             context->local_started.wYear, context->local_started.wMonth,
             context->local_started.wDay, context->local_started.wHour,
             context->local_started.wMinute, context->local_started.wSecond);
    fwprintf(file, L"started_utc=%04u-%02u-%02uT%02u:%02u:%02uZ\n",
             context->utc_started.wYear, context->utc_started.wMonth,
             context->utc_started.wDay, context->utc_started.wHour,
             context->utc_started.wMinute, context->utc_started.wSecond);
    fwprintf(file, L"backend=%ls\n", BackendName(context->backend));
    fwprintf(file, L"game_title=%ls\n", context->identity.window_title);
    fwprintf(file, L"package=%hs\n", context->metadata.package_name);
    fwprintf(file, L"version_name=%hs\n", context->metadata.version_name);
    fwprintf(file, L"version_code=%hs\n", context->metadata.version_code);
    fwprintf(file, L"apk=%ls\n", context->apk_path);
    fwprintf(file, L"apk_bytes=%llu\n", context->apk_size);
    fwprintf(file, L"log_folder=%ls\n", context->run_directory);
    fwprintf(file, L"save_folder=%ls\n", context->save_directory);
    fwprintf(file, L"gdps_server=%ls\n",
             GetSetting(L"GDPS_SERVER", L"www.boomlings.com/database",
                        server, ARRAY_COUNT(server)));
    fwprintf(file, L"hack_icons=%ls\n",
             GetSetting(L"HACK_ICONS", L"false", hack_icons, ARRAY_COUNT(hack_icons)));
    fwprintf(file, L"full_bypass=%ls\n",
             GetSetting(L"FULL_BYPASS", L"true", full_bypass, ARRAY_COUNT(full_bypass)));
    fwprintf(file, L"force_highest_graphics=%ls\n",
             GetSetting(L"FORCE_HIGHEST_GRAPHICS", L"true", highest, ARRAY_COUNT(highest)));
    fwprintf(file, L"music_pulse_max=%ls\n",
             GetSetting(L"MUSIC_PULSE_MAX", L"0.30", pulse, ARRAY_COUNT(pulse)));
    fwprintf(file, L"version_isolated_saves=%ls\n",
             GetSetting(L"VERSION_ISOLATED_SAVES", L"true", isolated_saves,
                        ARRAY_COUNT(isolated_saves)));
    fwprintf(file, L"x86_api_connect_mode=%ls\n",
             GetSetting(L"GD_X86_API_CONNECT_MODE",
                        IsX86Version211(context) ? L"real" : L"synthetic",
                        network_mode, ARRAY_COUNT(network_mode)));
    if (finished) {
        SYSTEMTIME finished_utc;
        GetSystemTime(&finished_utc);
        fwprintf(file, L"finished_utc=%04u-%02u-%02uT%02u:%02u:%02uZ\n",
                 finished_utc.wYear, finished_utc.wMonth, finished_utc.wDay,
                 finished_utc.wHour, finished_utc.wMinute, finished_utc.wSecond);
        fwprintf(file, L"exit_code=%lu\n", (unsigned long)exit_code);
    }
    if (launcher_error && *launcher_error)
        fwprintf(file, L"launcher_error=%ls\n", launcher_error);
    fclose(file);
    return 1;
}

static void WriteLatestRun(const LauncherContext *context) {
    wchar_t logs_directory[MAX_PATH * 4];
    wchar_t latest_path[MAX_PATH * 4];
    FILE *file = NULL;
    if (!PathJoin(logs_directory, ARRAY_COUNT(logs_directory),
                  context->base_directory, L"logs") ||
        !PathJoin(latest_path, ARRAY_COUNT(latest_path),
                  logs_directory, L"latest-run.txt")) return;
    if (_wfopen_s(&file, latest_path, L"wb, ccs=UTF-8") == 0 && file) {
        fwprintf(file, L"%ls\n", context->run_directory);
        fclose(file);
    }
}

/* ------------------------------ Process launch ------------------------------ */

static int AppendCommandArgument(wchar_t *command, size_t capacity,
                                 const wchar_t *argument) {
    size_t used = wcslen(command);
    size_t index;
    int needs_quotes = 0;
    if (used + 2u >= capacity) return 0;
    if (used) command[used++] = L' ';
    for (index = 0; argument[index]; ++index) {
        if (iswspace(argument[index]) || argument[index] == L'"') {
            needs_quotes = 1;
            break;
        }
    }
    if (!needs_quotes) {
        if (used + wcslen(argument) + 1u > capacity) return 0;
        wcscpy_s(command + used, capacity - used, argument);
        return 1;
    }
    command[used++] = L'"';
    for (index = 0; argument[index]; ++index) {
        size_t slash_count = 0;
        while (argument[index] == L'\\') {
            ++slash_count;
            ++index;
        }
        if (!argument[index]) {
            while (slash_count--) {
                if (used + 2u >= capacity) return 0;
                command[used++] = L'\\';
                command[used++] = L'\\';
            }
            break;
        }
        if (argument[index] == L'"') {
            while (slash_count--) {
                if (used + 2u >= capacity) return 0;
                command[used++] = L'\\';
                command[used++] = L'\\';
            }
            if (used + 2u >= capacity) return 0;
            command[used++] = L'\\';
            command[used++] = L'"';
        } else {
            while (slash_count--) {
                if (used + 1u >= capacity) return 0;
                command[used++] = L'\\';
            }
            if (used + 1u >= capacity) return 0;
            command[used++] = argument[index];
        }
    }
    if (used + 2u > capacity) return 0;
    command[used++] = L'"';
    command[used] = L'\0';
    return 1;
}

static int AppendOption(wchar_t *command, size_t capacity,
                        const wchar_t *prefix, const wchar_t *value) {
    wchar_t combined[MAX_PATH * 4 + 128];
    if (wcslen(prefix) + wcslen(value) + 1u > ARRAY_COUNT(combined)) return 0;
    wcscpy_s(combined, ARRAY_COUNT(combined), prefix);
    wcscat_s(combined, ARRAY_COUNT(combined), value);
    return AppendCommandArgument(command, capacity, combined);
}

static DWORD LaunchBackend(const LauncherContext *context,
                           wchar_t *error_text, size_t error_capacity) {
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    wchar_t command[MAX_COMMAND_LINE];
    DWORD exit_code = 1u;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    command[0] = L'\0';
    if (!AppendCommandArgument(command, ARRAY_COUNT(command), context->backend_path))
        goto command_error;
    if (context->backend == BACKEND_X86) {
        if (!AppendOption(command, ARRAY_COUNT(command), L"--apk=", context->apk_path) ||
            !AppendOption(command, ARRAY_COUNT(command), L"--log=", context->log_path))
            goto command_error;
    } else {
        if (!AppendCommandArgument(command, ARRAY_COUNT(command), context->apk_path) ||
            !AppendOption(command, ARRAY_COUNT(command), L"--log=", context->log_path) ||
            !AppendOption(command, ARRAY_COUNT(command), L"--profile=", context->profile_path) ||
            !AppendOption(command, ARRAY_COUNT(command), L"--profile-summary=",
                          context->profile_summary_path))
            goto command_error;
        if (context->backend == BACKEND_ARMV7) {
            /*
             * The ARMv7 backend now defaults to companion hooks OFF.  Omitting
             * the option avoids repeating the argument-parsing regression that
             * prevented every 2.2-beta launch in the supplied logs.
             */
            if (!AppendOption(command, ARRAY_COUNT(command), L"--dump-imports=",
                              context->imports_path))
                goto command_error;
        }
    }
    if (!CreateProcessW(context->backend_path, command, NULL, NULL, FALSE, 0,
                        NULL, context->base_directory, &startup, &process)) {
        swprintf_s(error_text, error_capacity, L"CreateProcess failed: %lu",
                   (unsigned long)GetLastError());
        return 1u;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) exit_code = 1u;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;

command_error:
    wcscpy_s(error_text, error_capacity, L"Backend command line exceeded the safe limit");
    return 1u;
}

static int CreateIpaRunDirectory(const wchar_t *base_directory,
                                 wchar_t *run_directory, size_t run_capacity,
                                 wchar_t *log_path, size_t log_capacity) {
    SYSTEMTIME now;
    wchar_t logs_directory[MAX_PATH * 4];
    wchar_t day_directory[MAX_PATH * 4];
    wchar_t folder_name[256];
    wchar_t latest_path[MAX_PATH * 4];
    unsigned suffix;
    FILE *latest = NULL;

    GetLocalTime(&now);
    if (!PathJoin(logs_directory, ARRAY_COUNT(logs_directory),
                  base_directory, L"logs")) return 0;
    swprintf_s(day_directory, ARRAY_COUNT(day_directory), L"%ls\\%04u-%02u-%02u",
               logs_directory, now.wYear, now.wMonth, now.wDay);
    if (!EnsureDirectory(day_directory)) return 0;

    swprintf_s(folder_name, ARRAY_COUNT(folder_name),
               L"%02u-%02u-%02u__ios-armv7__publictest20",
               now.wHour, now.wMinute, now.wSecond);
    for (suffix = 1u; suffix < 1000u; ++suffix) {
        if (suffix == 1u) {
            swprintf_s(run_directory, run_capacity, L"%ls\\%ls",
                       day_directory, folder_name);
        } else {
            swprintf_s(run_directory, run_capacity, L"%ls\\%ls__%02u",
                       day_directory, folder_name, suffix);
        }
        if (CreateDirectoryW(run_directory, NULL)) break;
        if (GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    }
    if (suffix >= 1000u) return 0;

    if (!PathJoin(log_path, log_capacity, run_directory, L"ios-armv7.log"))
        return 0;

    if (PathJoin(latest_path, ARRAY_COUNT(latest_path),
                 logs_directory, L"latest-run.txt") &&
        _wfopen_s(&latest, latest_path, L"wb, ccs=UTF-8") == 0 && latest) {
        fwprintf(latest, L"%ls\n", run_directory);
        fclose(latest);
    }
    return 1;
}

static DWORD LaunchIpaBootstrap(const wchar_t *input_path) {
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    wchar_t base_directory[MAX_PATH * 4];
    wchar_t backend_path[MAX_PATH * 4];
    wchar_t ipa_path[MAX_PATH * 4];
    wchar_t run_directory[MAX_PATH * 4];
    wchar_t log_path[MAX_PATH * 4];
    wchar_t command[MAX_COMMAND_LINE];
    DWORD full_length;
    DWORD exit_code = 1u;

    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);

    if (!GetExecutableDirectory(base_directory, ARRAY_COUNT(base_directory))) {
        PrintWindowsError(L"GetModuleFileName");
        return 1u;
    }
    full_length = GetFullPathNameW(input_path, ARRAY_COUNT(ipa_path), ipa_path, NULL);
    if (!full_length || full_length >= ARRAY_COUNT(ipa_path)) {
        PrintWindowsError(L"GetFullPathName");
        return 1u;
    }
    if (!PathJoin(backend_path, ARRAY_COUNT(backend_path),
                  base_directory, L"ios-armv7\\RobTopIOSArmV7.exe")) {
        return 1u;
    }
    if (!FileExists(backend_path)) {
        fwprintf(stderr,
                 L"ERROR: iOS ARMv7 backend is not built: %ls\n"
                 L"Run BUILD_ALL.cmd first.\n", backend_path);
        return 1u;
    }
    if (!CreateIpaRunDirectory(base_directory, run_directory,
                               ARRAY_COUNT(run_directory),
                               log_path, ARRAY_COUNT(log_path))) {
        fwprintf(stderr, L"ERROR: Could not create the iOS dated log folder.\n");
        return 1u;
    }

    SetEnvironmentVariableW(L"GD_LOG_PATH", log_path);
    command[0] = L'\0';
    if (!AppendCommandArgument(command, ARRAY_COUNT(command), backend_path) ||
        !AppendCommandArgument(command, ARRAY_COUNT(command), ipa_path) ||
        !AppendOption(command, ARRAY_COUNT(command), L"--log=", log_path)) {
        fwprintf(stderr, L"ERROR: iOS backend command line exceeded the safe limit.\n");
        return 1u;
    }

    wprintf(L"\nPublicTest20: starting real ARMv7 iOS bootstrap...\n");
    wprintf(L"iOS log: %ls\n", log_path);
    if (!CreateProcessW(backend_path, command, NULL, NULL, FALSE, 0,
                        NULL, base_directory, &startup, &process)) {
        PrintWindowsError(L"CreateProcess iOS backend");
        return 1u;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) exit_code = 1u;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;
}

/* ------------------------------- Main workflow ------------------------------ */

static int InitializeLauncherContext(int argc, wchar_t **argv, LauncherContext *context) {
    ApkArchive archive;
    ApkFeatures features;
    unsigned char *manifest = NULL;
    size_t manifest_size = 0;
    LARGE_INTEGER apk_size;
    HANDLE apk_file;
    wchar_t icon_directory[MAX_PATH * 4];
    memset(context, 0, sizeof(*context));
    memset(&features, 0, sizeof(features));
    GetLocalTime(&context->local_started);
    GetSystemTime(&context->utc_started);
    if (!GetExecutableDirectory(context->base_directory,
                                ARRAY_COUNT(context->base_directory))) {
        PrintWindowsError(L"GetModuleFileName");
        return 0;
    }
    if (argc >= 2 && argv[1] && *argv[1]) {
        DWORD length = GetFullPathNameW(argv[1],
                                       (DWORD)ARRAY_COUNT(context->apk_path),
                                       context->apk_path, NULL);
        if (!length || length >= ARRAY_COUNT(context->apk_path)) {
            PrintWindowsError(L"GetFullPathName");
            return 0;
        }
    } else if (!PathJoin(context->apk_path, ARRAY_COUNT(context->apk_path),
                         context->base_directory, L"game.apk")) {
        return 0;
    }
    if (!FileExists(context->apk_path)) {
        fwprintf(stderr, L"ERROR: APK not found: %ls\n", context->apk_path);
        return 0;
    }
    {
        const wchar_t *extension = wcsrchr(context->apk_path, L'.');
        if (!extension || _wcsicmp(extension, L".apk") != 0) {
            fwprintf(stderr, L"ERROR: Drag an Android .apk file, not: %ls\n",
                     context->apk_path);
            return 0;
        }
    }
    apk_file = CreateFileW(context->apk_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (apk_file == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(apk_file, &apk_size)) {
        CloseHandle(apk_file);
        return 0;
    }
    CloseHandle(apk_file);
    context->apk_size = (ULONGLONG)apk_size.QuadPart;

    if (!ApkArchiveOpen(context->apk_path, &archive)) {
        fwprintf(stderr, L"ERROR: Could not read APK ZIP structure.\n");
        return 0;
    }
    if (!ApkArchiveVisit(&archive, ScanApkEntry, &features)) {
        ApkArchiveClose(&archive);
        fwprintf(stderr, L"ERROR: APK central directory is damaged.\n");
        return 0;
    }
    ManifestMetadataDefaults(&context->metadata);
    if (features.has_manifest) {
        manifest = ExtractZipEntry(&archive, &features.manifest_entry, &manifest_size);
        if (manifest) {
            ParseAndroidManifest(manifest, manifest_size, &context->metadata);
            free(manifest);
        }
    }
    if (!features.has_x86 && !features.has_legacy_arm && !features.has_armv7) {
        ApkArchiveClose(&archive);
        fwprintf(stderr, L"ERROR: APK has no supported x86, armeabi, or armeabi-v7a game library.\n");
        return 0;
    }
    context->backend = ChooseBackend(&features);
    context->identity = IdentifyGame(context->metadata.package_name);
    ApkArchiveClose(&archive);

    {
        wchar_t save_root[MAX_PATH * 4];
        wchar_t package_component[MAX_UTF8_TEXT];
        wchar_t version_component[MAX_UTF8_TEXT];
        wchar_t profile_component[1024];
        MakeSafeComponent(context->metadata.package_name, package_component,
                          ARRAY_COUNT(package_component), L"unknown.package");
        MakeSafeComponent(context->metadata.version_name, version_component,
                          ARRAY_COUNT(version_component), L"unknown");
        swprintf_s(profile_component, ARRAY_COUNT(profile_component),
                   L"%ls__v%ls__%ls", package_component, version_component,
                   BackendName(context->backend));
        const int isolated_saves =
            GetBooleanSetting(L"VERSION_ISOLATED_SAVES", 1);
        if (!PathJoin(save_root, ARRAY_COUNT(save_root),
                      context->base_directory, L"save") ||
            !EnsureDirectory(save_root)) {
            fwprintf(stderr, L"ERROR: Could not create save directory.\n");
            return 0;
        }
        if (isolated_saves) {
            if (!PathJoin(context->save_directory,
                          ARRAY_COUNT(context->save_directory),
                          save_root, profile_component) ||
                !EnsureDirectory(context->save_directory)) {
                fwprintf(stderr,
                         L"ERROR: Could not create version-isolated save directory.\n");
                return 0;
            }
        } else {
            wcscpy_s(context->save_directory,
                     ARRAY_COUNT(context->save_directory), save_root);
        }
    }
    switch (context->backend) {
    case BACKEND_X86:
        if (!PathJoin(context->backend_path, ARRAY_COUNT(context->backend_path),
                      context->base_directory, L"x86\\GeometryDashWrapper.exe")) return 0;
        break;
    case BACKEND_LEGACY_ARM:
        if (!PathJoin(context->backend_path, ARRAY_COUNT(context->backend_path),
                      context->base_directory, L"arm-legacy\\GeometryDashArmLegacy.exe")) return 0;
        break;
    case BACKEND_ARMV7:
        if (!PathJoin(context->backend_path, ARRAY_COUNT(context->backend_path),
                      context->base_directory, L"armv7\\GeometryDashArmV7.exe")) return 0;
        break;
    }
    if (!FileExists(context->backend_path)) {
        fwprintf(stderr, L"ERROR: Selected backend is not built: %ls\n",
                 context->backend_path);
        return 0;
    }
    if (!CreateUniqueRunDirectory(context)) {
        fwprintf(stderr, L"ERROR: Could not create dated log folder.\n");
        return 0;
    }
    if (!PathJoin(context->log_path, ARRAY_COUNT(context->log_path),
                  context->run_directory,
                  context->backend == BACKEND_X86 ? L"gd-wrapper.log" :
                  context->backend == BACKEND_LEGACY_ARM ? L"gd-arm-legacy.log" :
                  L"gd-armv7.log") ||
        !PathJoin(context->profile_path, ARRAY_COUNT(context->profile_path),
                  context->run_directory, L"frame-profile.csv") ||
        !PathJoin(context->profile_summary_path,
                  ARRAY_COUNT(context->profile_summary_path),
                  context->run_directory, L"frame-profile-summary.txt") ||
        !PathJoin(context->imports_path, ARRAY_COUNT(context->imports_path),
                  context->run_directory, L"imports.txt")) return 0;
    if (!PathJoin(icon_directory, ARRAY_COUNT(icon_directory),
                  context->base_directory, L"assets\\icons") ||
        !PathJoin(context->icon_path, ARRAY_COUNT(context->icon_path),
                  icon_directory, context->identity.icon_file)) return 0;
    return 1;
}

int wmain(int argc, wchar_t **argv) {
    LauncherContext context;
    wchar_t launcher_error[1024];
    DWORD exit_code;
    launcher_error[0] = L'\0';

    /*
     * PublicTest2 keeps IPA execution entirely separate from the stable Android
     * launcher path. First print the analyzer report, then let the dedicated
     * ARMv7 Mach-O bootstrap make the real execution attempt.
     */
    if (argc >= 2 && argv[1] && *argv[1] && PathHasExtension(argv[1], L".ipa")) {
        if (!AnalyzeIpaInput(argv[1])) return 2;
        return (int)LaunchIpaBootstrap(argv[1]);
    }

    if (!InitializeLauncherContext(argc, argv, &context)) return 2;

    SetEnvironmentVariableW(L"GD_SAVE_DIR", context.save_directory);
    SetEnvironmentVariableW(L"GD_GAME_TITLE", context.identity.window_title);
    {
        wchar_t version_wide[MAX_UTF8_TEXT];
        if (Utf8ToWide(context.metadata.version_name, version_wide,
                       ARRAY_COUNT(version_wide))) {
            SetEnvironmentVariableW(L"GD_GAME_VERSION", version_wide);
        }
    }
    SetEnvironmentVariableW(L"GD_X86_API_CONNECT_MODE",
                            IsX86Version211(&context) ? L"real" : L"synthetic");
    SetEnvironmentVariableW(L"GD_LOG_PATH", context.log_path);
    if (FileExists(context.icon_path))
        SetEnvironmentVariableW(L"GD_WINDOW_ICON", context.icon_path);

    WriteRunInfo(&context, 0, 0, NULL);
    WriteLatestRun(&context);

    wprintf(L"Geometry Dash Wrapper %hs\n", LAUNCHER_VERSION);
    wprintf(L"Game: %ls\n", context.identity.window_title);
    wprintf(L"Package: %hs\n", context.metadata.package_name);
    wprintf(L"Version: %hs (code %hs)\n",
            context.metadata.version_name, context.metadata.version_code);
    wprintf(L"Backend: %ls\n", BackendName(context.backend));
    wprintf(L"APK: %ls\n", context.apk_path);
    wprintf(L"Saves: %ls\n", context.save_directory);
    wprintf(L"Logs: %ls\n", context.run_directory);

    exit_code = LaunchBackend(&context, launcher_error, ARRAY_COUNT(launcher_error));
    WriteRunInfo(&context, 1, exit_code, launcher_error);
    if (*launcher_error) fwprintf(stderr, L"ERROR: %ls\n", launcher_error);
    return (int)exit_code;
}
