#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "storage_win.h"

#define PREF_MAX_COUNT 100000u
#define PREF_MAX_KEY_SIZE 4096u
#define PREF_MAX_VALUE_SIZE (64u * 1024u * 1024u)

enum PreferenceType {
    PREF_STRING = 1,
    PREF_BOOL = 2,
    PREF_INTEGER = 3,
    PREF_FLOAT = 4,
    PREF_DOUBLE = 5
};

typedef struct PreferenceEntry {
    char *key;
    unsigned char type;
    unsigned char *value;
    uint32_t value_size;
    struct PreferenceEntry *next;
} PreferenceEntry;

static const unsigned char g_preferences_magic[8] = {
    'G', 'D', 'W', 'P', 'R', 'E', 'F', '1'
};
static char g_writable_directory[MAX_PATH * 2];
static char g_preferences_path[MAX_PATH * 2];
static PreferenceEntry *g_preferences;
static CRITICAL_SECTION g_preferences_lock;
static int g_storage_initialized;

int storage_is_game_file_name(const char *name) {
    static const char *const families[] = {
        "CCGameManager", "CCLocalLevels", "CCGameStore", "CCData",
        "CCGameSave", "CCGameStatistics"
    };
    size_t index;
    if (!name || !name[0]) return 0;
    for (index = 0; index < sizeof(families) / sizeof(families[0]); ++index) {
        const char *suffix;
        size_t family_length = strlen(families[index]);
        if (strncmp(name, families[index], family_length) != 0) continue;
        suffix = name + family_length;
        while (*suffix >= '0' && *suffix <= '9') ++suffix;
        if (strcmp(suffix, ".dat") == 0 ||
            strcmp(suffix, ".dat.bak") == 0) {
            return 1;
        }
    }
    return 0;
}

static char *copy_string(const char *value) {
    size_t length;
    char *result;
    if (!value) value = "";
    length = strlen(value) + 1;
    result = (char *)malloc(length);
    if (result) memcpy(result, value, length);
    return result;
}

static const char *path_file_name(const char *path) {
    const char *slash;
    const char *backslash;
    if (!path) return "";
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (slash && (!backslash || slash > backslash)) return slash + 1;
    return backslash ? backslash + 1 : path;
}

static void resolve_storage_path(const char *path, char *destination,
                                 size_t capacity) {
    if (!path) path = "";
    if (strcmp(path, "/save") == 0 || strcmp(path, "/save/") == 0) {
        snprintf(destination, capacity, "%s", g_writable_directory);
    } else if (strncmp(path, "/save/", 6) == 0) {
        snprintf(destination, capacity, "%s%s", g_writable_directory, path + 6);
    } else if ((path[0] && path[1] == ':') || path[0] == '/' ||
               path[0] == '\\') {
        snprintf(destination, capacity, "%s", path);
    } else {
        snprintf(destination, capacity, "%s%s", g_writable_directory, path);
    }
}

static void migrate_legacy_root_file(const char *root, const char *name) {
    char source[MAX_PATH * 2];
    char destination[MAX_PATH * 2];
    DWORD source_attributes;
    DWORD destination_attributes;
    snprintf(source, sizeof(source), "%s/%s", root, name);
    snprintf(destination, sizeof(destination), "%s%s", g_writable_directory,
             name);
    source_attributes = GetFileAttributesA(source);
    if (source_attributes == INVALID_FILE_ATTRIBUTES ||
        (source_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return;
    }
    destination_attributes = GetFileAttributesA(destination);
    if (destination_attributes != INVALID_FILE_ATTRIBUTES) {
        runtime_log("Save migration: kept existing save/%s; legacy root file remains",
                    name);
        return;
    }
    if (MoveFileExA(source, destination, MOVEFILE_WRITE_THROUGH)) {
        runtime_log("Save migration: moved legacy root %s into save/", name);
    } else {
        runtime_log("Save migration: could not move legacy root %s (%lu)", name,
                    (unsigned long)GetLastError());
    }
}

static void migrate_legacy_root_saves(void) {
    char root[MAX_PATH * 2];
    char pattern[MAX_PATH * 2];
    char *separator;
    size_t length;
    WIN32_FIND_DATAA entry;
    HANDLE search;
    snprintf(root, sizeof(root), "%s", g_writable_directory);
    length = strlen(root);
    while (length && (root[length - 1] == '/' || root[length - 1] == '\\')) {
        root[--length] = 0;
    }
    separator = strrchr(root, '/');
    if (!separator) separator = strrchr(root, '\\');
    if (!separator || strcmp(separator + 1, "save") != 0) return;
    *separator = 0;
    snprintf(pattern, sizeof(pattern), "%s\\CC*.dat*", root);
    search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            storage_is_game_file_name(entry.cFileName)) {
            migrate_legacy_root_file(root, entry.cFileName);
        }
    } while (FindNextFileA(search, &entry));
    FindClose(search);
}

static void create_parent_directories(const char *path) {
    char copy[MAX_PATH * 2];
    size_t index;
    snprintf(copy, sizeof(copy), "%s", path ? path : "");
    for (index = 0; copy[index]; ++index) {
        if ((copy[index] == '/' || copy[index] == '\\') &&
            !(index == 2 && copy[1] == ':')) {
            char saved = copy[index];
            copy[index] = 0;
            if (copy[0]) CreateDirectoryA(copy, NULL);
            copy[index] = saved;
        }
    }
}

static int write_u32(FILE *stream, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
    return fwrite(bytes, 1, sizeof(bytes), stream) == sizeof(bytes);
}

static int read_u32(FILE *stream, uint32_t *value) {
    unsigned char bytes[4];
    if (fread(bytes, 1, sizeof(bytes), stream) != sizeof(bytes)) return 0;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return 1;
}

static PreferenceEntry *find_preference(const char *key) {
    PreferenceEntry *entry;
    for (entry = g_preferences; entry; entry = entry->next) {
        if (strcmp(entry->key, key) == 0) return entry;
    }
    return NULL;
}

static void free_preferences(void) {
    PreferenceEntry *entry = g_preferences;
    while (entry) {
        PreferenceEntry *next = entry->next;
        free(entry->key);
        free(entry->value);
        free(entry);
        entry = next;
    }
    g_preferences = NULL;
}

static unsigned preference_count(void) {
    PreferenceEntry *entry;
    unsigned count = 0;
    for (entry = g_preferences; entry; entry = entry->next) ++count;
    return count;
}

static int save_preferences_locked(void) {
    char temporary[MAX_PATH * 2 + 32];
    PreferenceEntry *entry;
    FILE *stream;
    int ok = 1;
    snprintf(temporary, sizeof(temporary), "%s.wrapper.tmp", g_preferences_path);
    stream = fopen(temporary, "wb");
    if (!stream) {
        runtime_log("Save storage: cannot create preference temporary file");
        return 0;
    }
    if (fwrite(g_preferences_magic, 1, sizeof(g_preferences_magic), stream) !=
            sizeof(g_preferences_magic) ||
        !write_u32(stream, preference_count())) {
        ok = 0;
    }
    for (entry = g_preferences; ok && entry; entry = entry->next) {
        size_t key_size = strlen(entry->key);
        if (key_size > UINT32_MAX || fputc(entry->type, stream) == EOF ||
            !write_u32(stream, (uint32_t)key_size) ||
            !write_u32(stream, entry->value_size) ||
            fwrite(entry->key, 1, key_size, stream) != key_size ||
            (entry->value_size &&
             fwrite(entry->value, 1, entry->value_size, stream) !=
                 entry->value_size)) {
            ok = 0;
        }
    }
    if (ok && (fflush(stream) != 0 || _commit(_fileno(stream)) != 0)) ok = 0;
    if (fclose(stream) != 0) ok = 0;
    if (ok && MoveFileExA(temporary, g_preferences_path,
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return 1;
    }
    DeleteFileA(temporary);
    runtime_log("Save storage: preference commit failed (%lu)",
                (unsigned long)GetLastError());
    return 0;
}

static void load_preferences(void) {
    FILE *stream = fopen(g_preferences_path, "rb");
    unsigned char magic[sizeof(g_preferences_magic)];
    uint32_t count;
    uint32_t index;
    if (!stream) {
        runtime_log("Save storage: starting with new preferences");
        return;
    }
    if (fread(magic, 1, sizeof(magic), stream) != sizeof(magic) ||
        memcmp(magic, g_preferences_magic, sizeof(magic)) != 0 ||
        !read_u32(stream, &count) || count > PREF_MAX_COUNT) {
        runtime_log("Save storage: ignoring malformed preferences file");
        fclose(stream);
        return;
    }
    for (index = 0; index < count; ++index) {
        int type = fgetc(stream);
        uint32_t key_size;
        uint32_t value_size;
        PreferenceEntry *entry;
        if (type < PREF_STRING || type > PREF_DOUBLE ||
            !read_u32(stream, &key_size) || !read_u32(stream, &value_size) ||
            !key_size || key_size > PREF_MAX_KEY_SIZE ||
            value_size > PREF_MAX_VALUE_SIZE) {
            break;
        }
        entry = (PreferenceEntry *)calloc(1, sizeof(*entry));
        if (!entry) break;
        entry->key = (char *)malloc((size_t)key_size + 1);
        entry->value = (unsigned char *)malloc((size_t)value_size + 1);
        if (!entry->key || !entry->value ||
            fread(entry->key, 1, key_size, stream) != key_size ||
            (value_size &&
             fread(entry->value, 1, value_size, stream) != value_size)) {
            free(entry->key);
            free(entry->value);
            free(entry);
            break;
        }
        entry->key[key_size] = 0;
        entry->value[value_size] = 0;
        entry->type = (unsigned char)type;
        entry->value_size = value_size;
        entry->next = g_preferences;
        g_preferences = entry;
    }
    fclose(stream);
    if (index != count) {
        runtime_log("Save storage: preferences ended early; loaded %lu/%lu entries",
                    (unsigned long)index, (unsigned long)count);
    } else {
        runtime_log("Save storage: loaded %lu preference entries",
                    (unsigned long)count);
    }
}

static void set_preference(const char *key, unsigned char type,
                           const void *value, uint32_t value_size) {
    PreferenceEntry *entry;
    unsigned char *copy;
    if (!g_storage_initialized || !key || !key[0] ||
        value_size > PREF_MAX_VALUE_SIZE || (value_size && !value)) {
        return;
    }
    copy = (unsigned char *)malloc((size_t)value_size + 1);
    if (!copy) return;
    if (value_size) memcpy(copy, value, value_size);
    copy[value_size] = 0;
    EnterCriticalSection(&g_preferences_lock);
    entry = find_preference(key);
    if (!entry) {
        entry = (PreferenceEntry *)calloc(1, sizeof(*entry));
        if (entry) {
            entry->key = copy_string(key);
            if (!entry->key) {
                free(entry);
                entry = NULL;
            } else {
                entry->next = g_preferences;
                g_preferences = entry;
            }
        }
    }
    if (entry) {
        free(entry->value);
        entry->value = copy;
        entry->value_size = value_size;
        entry->type = type;
        copy = NULL;
        save_preferences_locked();
    }
    LeaveCriticalSection(&g_preferences_lock);
    free(copy);
}

static int get_preference(const char *key, unsigned char type, void *value,
                          uint32_t value_size) {
    PreferenceEntry *entry;
    int found = 0;
    if (!g_storage_initialized || !key || !value) return 0;
    EnterCriticalSection(&g_preferences_lock);
    entry = find_preference(key);
    if (entry && entry->type == type && entry->value_size == value_size) {
        memcpy(value, entry->value, value_size);
        found = 1;
    }
    LeaveCriticalSection(&g_preferences_lock);
    return found;
}

void storage_initialize(const char *writable_directory) {
    if (g_storage_initialized) return;
    snprintf(g_writable_directory, sizeof(g_writable_directory), "%s",
             writable_directory ? writable_directory : "./save/");
    CreateDirectoryA(g_writable_directory, NULL);
    migrate_legacy_root_saves();
    snprintf(g_preferences_path, sizeof(g_preferences_path), "%spreferences.bin",
             g_writable_directory);
    InitializeCriticalSection(&g_preferences_lock);
    g_storage_initialized = 1;
    load_preferences();
}

void storage_shutdown(void) {
    if (!g_storage_initialized) return;
    EnterCriticalSection(&g_preferences_lock);
    save_preferences_locked();
    free_preferences();
    LeaveCriticalSection(&g_preferences_lock);
    DeleteCriticalSection(&g_preferences_lock);
    g_storage_initialized = 0;
}

char *storage_get_string_copy(const char *key, const char *default_value) {
    PreferenceEntry *entry;
    char *result;
    if (!g_storage_initialized || !key) return copy_string(default_value);
    EnterCriticalSection(&g_preferences_lock);
    entry = find_preference(key);
    result = entry && entry->type == PREF_STRING
                 ? copy_string((const char *)entry->value)
                 : copy_string(default_value);
    LeaveCriticalSection(&g_preferences_lock);
    return result;
}

int storage_get_bool(const char *key, int default_value) {
    int32_t value;
    return get_preference(key, PREF_BOOL, &value, sizeof(value))
               ? value != 0
               : default_value != 0;
}

int32_t storage_get_integer(const char *key, int32_t default_value) {
    int32_t value;
    return get_preference(key, PREF_INTEGER, &value, sizeof(value))
               ? value
               : default_value;
}

float storage_get_float(const char *key, float default_value) {
    float value;
    return get_preference(key, PREF_FLOAT, &value, sizeof(value))
               ? value
               : default_value;
}

double storage_get_double(const char *key, double default_value) {
    double value;
    return get_preference(key, PREF_DOUBLE, &value, sizeof(value))
               ? value
               : default_value;
}

void storage_set_string(const char *key, const char *value) {
    if (!value) value = "";
    set_preference(key, PREF_STRING, value, (uint32_t)strlen(value));
}

void storage_set_bool(const char *key, int value) {
    int32_t stored = value != 0;
    set_preference(key, PREF_BOOL, &stored, sizeof(stored));
}

void storage_set_integer(const char *key, int32_t value) {
    set_preference(key, PREF_INTEGER, &value, sizeof(value));
}

void storage_set_float(const char *key, float value) {
    set_preference(key, PREF_FLOAT, &value, sizeof(value));
}

void storage_set_double(const char *key, double value) {
    set_preference(key, PREF_DOUBLE, &value, sizeof(value));
}

int storage_write_game_file(const char *path, const void *data, size_t size) {
    char resolved[MAX_PATH * 2];
    char temporary[MAX_PATH * 2 + 32];
    FILE *stream;
    int ok;
    if (!path || (size && !data)) return 0;
    resolve_storage_path(path, resolved, sizeof(resolved));
    create_parent_directories(resolved);
    snprintf(temporary, sizeof(temporary), "%s.wrapper.tmp", resolved);
    stream = fopen(temporary, "wb");
    if (!stream) {
        runtime_log("Game save: cannot create %s", path_file_name(resolved));
        return 0;
    }
    ok = (!size || fwrite(data, 1, size, stream) == size) &&
         fflush(stream) == 0 && _commit(_fileno(stream)) == 0;
    if (fclose(stream) != 0) ok = 0;
    if (ok && MoveFileExA(temporary, resolved,
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        runtime_log("Game save: wrote %s (%lu bytes)", path_file_name(resolved),
                    (unsigned long)size);
        return 1;
    }
    DeleteFileA(temporary);
    runtime_log("Game save: commit failed for %s (%lu)", path_file_name(resolved),
                (unsigned long)GetLastError());
    return 0;
}

char *storage_read_game_file(const char *path, size_t *size) {
    char resolved[MAX_PATH * 2];
    FILE *stream;
    long length;
    char *data;
    if (size) *size = 0;
    if (!path) return copy_string("");
    resolve_storage_path(path, resolved, sizeof(resolved));
    stream = fopen(resolved, "rb");
    if (!stream) return copy_string("");
    if (fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) < 0 ||
        (unsigned long)length > PREF_MAX_VALUE_SIZE ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return copy_string("");
    }
    data = (char *)malloc((size_t)length + 1);
    if (!data || (length && fread(data, 1, (size_t)length, stream) !=
                               (size_t)length)) {
        free(data);
        fclose(stream);
        return copy_string("");
    }
    fclose(stream);
    data[length] = 0;
    if (size) *size = (size_t)length;
    runtime_log("Game save: loaded %s (%lu bytes)", path_file_name(resolved),
                (unsigned long)length);
    return data;
}

int storage_file_exists(const char *path) {
    char resolved[MAX_PATH * 2];
    DWORD attributes;
    if (!path) return 0;
    resolve_storage_path(path, resolved, sizeof(resolved));
    attributes = GetFileAttributesA(resolved);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
