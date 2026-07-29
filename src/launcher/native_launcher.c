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
 *   1. Read the APK as a ZIP file.
 *   2. Extract package/version metadata from AndroidManifest.xml.
 *   3. Select the x86, legacy ARM, or ARMv7 backend.
 *   4. Create one dated log folder per launch.
 *   5. Set the shared save/title/icon/server environment variables.
 *   6. Start the backend and wait for it to exit.
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

#define LAUNCHER_VERSION "0.9.5-endurancetest2"
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
    wchar_t disable_pause[64];
    wchar_t hide_cursor[64];
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
    fwprintf(file, L"disable_pause_button=%ls\n",
             GetSetting(L"DISABLE_PAUSE_BUTTON", L"true", disable_pause,
                        ARRAY_COUNT(disable_pause)));
    fwprintf(file, L"hide_cursor_during_play=%ls\n",
             GetSetting(L"HIDE_CURSOR_DURING_PLAY", L"true", hide_cursor,
                        ARRAY_COUNT(hide_cursor)));
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
