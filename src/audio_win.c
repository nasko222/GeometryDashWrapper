#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <objbase.h>

#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_win.h"
#include "embedded_effects.h"
#include "loader.h"
#include "runtime.h"
#include "../third_party/zlib/zlib.h"

#define MAX_EFFECT_SLOTS 24

typedef struct {
    unsigned identifier;
    int open;
    int paused;
    float volume;
    char alias[32];
} EffectSlot;

/* MinGW's endpointvolume.h only forward-declares this interface in C. */
typedef struct OutputMeter OutputMeter;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(OutputMeter *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(OutputMeter *);
    ULONG (STDMETHODCALLTYPE *Release)(OutputMeter *);
    HRESULT (STDMETHODCALLTYPE *GetPeakValue)(OutputMeter *, float *);
    HRESULT (STDMETHODCALLTYPE *GetMeteringChannelCount)(OutputMeter *, UINT *);
    HRESULT (STDMETHODCALLTYPE *GetChannelsPeakValues)(OutputMeter *, UINT,
                                                        float *);
    HRESULT (STDMETHODCALLTYPE *QueryHardwareSupport)(OutputMeter *, DWORD *);
} OutputMeterVtbl;
struct OutputMeter {
    const OutputMeterVtbl *lpVtbl;
};

static char g_audio_directory[MAX_PATH * 2];
static char g_audio_cache_directory[MAX_PATH * 2];
static char g_save_directory[MAX_PATH * 2];
static char g_apk_path[MAX_PATH * 2];
static char g_music_path[MAX_PATH * 2];
static EffectSlot g_effects[MAX_EFFECT_SLOTS];
static unsigned g_next_effect_identifier = 1;
static unsigned g_next_effect_slot;
static float g_music_volume = 1.0f;
static float g_effects_volume = 1.0f;
static volatile LONG g_effect_log_count;
static int g_music_open;
static int g_music_paused;
static int g_music_loop;
static HANDLE g_output_meter_thread;
static HANDLE g_output_meter_stop;
static volatile LONG g_output_peak_bits;
static volatile LONG g_output_peak_logged;

/* Keep the Core Audio GUIDs local so the wrapper does not need uuid.lib. */
static const GUID g_clsid_mmdevice_enumerator = {
    0xbcde0395, 0xe52f, 0x467c,
    {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}
};
static const GUID g_iid_mmdevice_enumerator = {
    0xa95664d2, 0x9614, 0x4f35,
    {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}
};
static const GUID g_iid_audio_meter_information = {
    0xc02216f6, 0x8c67, 0x4b5b,
    {0x9d, 0x00, 0xd0, 0x08, 0xe7, 0x3e, 0x00, 0x64}
};

static float clamp_volume(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static DWORD WINAPI output_meter_thread(void *unused) {
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    OutputMeter *meter = NULL;
    HRESULT result;
    int release_com = 0;
    (void)unused;
    result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(result)) {
        release_com = 1;
    } else {
        runtime_log("WASAPI metering: COM initialization failed (0x%08lx)",
                    (unsigned long)result);
        return 1;
    }
    result = CoCreateInstance(
        &g_clsid_mmdevice_enumerator, NULL, CLSCTX_INPROC_SERVER,
        &g_iid_mmdevice_enumerator, (void **)&enumerator);
    if (SUCCEEDED(result)) {
        result = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
            enumerator, eRender, eMultimedia, &device);
    }
    if (SUCCEEDED(result)) {
        result = IMMDevice_Activate(
            device, &g_iid_audio_meter_information, CLSCTX_INPROC_SERVER,
            NULL, (void **)&meter);
    }
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    if (SUCCEEDED(result) && meter) {
        runtime_log("WASAPI output metering initialized for FMOD DSP peaks");
    } else {
        runtime_log("WASAPI output metering unavailable (0x%08lx)",
                    (unsigned long)result);
        if (release_com) CoUninitialize();
        return 2;
    }
    while (WaitForSingleObject(g_output_meter_stop, 10) == WAIT_TIMEOUT) {
        union {
            float floating;
            LONG integer;
        } peak_bits;
        peak_bits.floating = 0.0f;
        if (FAILED(meter->lpVtbl->GetPeakValue(meter,
                                               &peak_bits.floating))) {
            peak_bits.floating = 0.0f;
        }
        peak_bits.floating = clamp_volume(peak_bits.floating);
        InterlockedExchange(&g_output_peak_bits, peak_bits.integer);
        if (peak_bits.floating > 0.001f &&
            InterlockedCompareExchange(&g_output_peak_logged, 1, 0) == 0) {
            runtime_log("WASAPI FMOD metering: first nonzero peak %.3f",
                        peak_bits.floating);
        }
    }
    InterlockedExchange(&g_output_peak_bits, 0);
    meter->lpVtbl->Release(meter);
    if (release_com) CoUninitialize();
    return 0;
}

static void initialize_output_meter(void) {
    if (g_output_meter_thread) return;
    g_output_meter_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!g_output_meter_stop) {
        runtime_log("WASAPI metering: failed to create stop event");
        return;
    }
    g_output_meter_thread = CreateThread(
        NULL, 0, output_meter_thread, NULL, 0, NULL);
    if (!g_output_meter_thread) {
        runtime_log("WASAPI metering: failed to create worker thread");
        CloseHandle(g_output_meter_stop);
        g_output_meter_stop = NULL;
    }
}

static int mci_command(const char *command, char *result, unsigned capacity,
                       int report_error) {
    MCIERROR error = mciSendStringA(command, result, capacity, NULL);
    if (error && report_error) {
        char message[256] = "unknown MCI error";
        mciGetErrorStringA(error, message, sizeof(message));
        runtime_log("Audio MCI error %lu: %s | %s", (unsigned long)error,
                    message, command);
    }
    return error == 0;
}

static const char *file_name_part(const char *path) {
    const char *slash;
    const char *backslash;
    if (!path) return "";
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (slash && (!backslash || slash > backslash)) return slash + 1;
    if (backslash) return backslash + 1;
    return path;
}

static int file_is_regular(const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int write_cached_audio(const char *destination, const void *data,
                              size_t size) {
    char temporary[MAX_PATH * 2 + 32];
    FILE *stream;
    int ok;
    snprintf(temporary, sizeof(temporary), "%s.wrapper.tmp", destination);
    stream = fopen(temporary, "wb");
    if (!stream) return 0;
    ok = (!size || fwrite(data, 1, size, stream) == size) &&
         fflush(stream) == 0 && _commit(_fileno(stream)) == 0;
    if (fclose(stream) != 0) ok = 0;
    if (ok && MoveFileExA(temporary, destination,
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return 1;
    }
    DeleteFileA(temporary);
    return 0;
}

static int materialize_embedded_effect(const char *name,
                                       const char *destination) {
    const EmbeddedEffect *effect = embedded_effect_find(name);
    unsigned char *decoded;
    uLongf decoded_size;
    int result;
    if (!effect || !effect->compressed_data || !effect->uncompressed_size) {
        return 0;
    }
    decoded = (unsigned char *)malloc(effect->uncompressed_size);
    if (!decoded) return 0;
    decoded_size = (uLongf)effect->uncompressed_size;
    result = uncompress(decoded, &decoded_size, effect->compressed_data,
                        (uLong)effect->compressed_size) == Z_OK &&
             decoded_size == effect->uncompressed_size &&
             write_cached_audio(destination, decoded, decoded_size);
    free(decoded);
    if (result) {
        runtime_log("Audio cache: materialized %s from embedded APK conversion",
                    name);
    }
    return result;
}

static int materialize_apk_audio(const char *name, const char *destination) {
    char member[MAX_PATH * 2];
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    int result;
    snprintf(member, sizeof(member), "assets/%s", name);
    if (!apk_extract_member(g_apk_path, member, &payload, &payload_size)) {
        return 0;
    }
    result = write_cached_audio(destination, payload, payload_size);
    free(payload);
    if (result) {
        runtime_log("Audio cache: extracted %s from game.apk", name);
    }
    return result;
}

static int audio_asset_path(const char *requested, int effect,
                            char *destination, size_t capacity) {
    const char *name = file_name_part(requested);
    char converted[MAX_PATH];
    char *extension;
    DWORD attributes;
    if (!name[0]) return 0;
    snprintf(converted, sizeof(converted), "%s", name);
    extension = strrchr(converted, '.');
    if (effect && extension && _stricmp(extension, ".ogg") == 0) {
        strcpy(extension, ".wav");
    }
    snprintf(destination, capacity, "%s\\%s", g_audio_directory, converted);
    attributes = GetFileAttributesA(destination);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return 1;
    }
    if (requested && (strchr(requested, '/') || strchr(requested, '\\'))) {
        attributes = GetFileAttributesA(requested);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            snprintf(destination, capacity, "%s", requested);
            return 1;
        }
    }
    if (!effect) {
        /* Downloaded custom songs live directly in Cocos's writable folder.
           The game refers to them through an Android path such as
           /save/590577.mp3, which is not a valid absolute path on Windows. */
        snprintf(destination, capacity, "%s\\%s", g_save_directory, name);
        if (file_is_regular(destination)) {
            runtime_log("Audio custom song: using writable save file %s", name);
            return 1;
        }
    }
    snprintf(destination, capacity, "%s\\%s", g_audio_cache_directory,
             converted);
    if (file_is_regular(destination)) return 1;
    if ((effect && materialize_embedded_effect(converted, destination)) ||
        (!effect && materialize_apk_audio(name, destination))) {
        return 1;
    }
    runtime_log("Audio asset is missing: %s", destination);
    return 0;
}

static void set_alias_volume(const char *alias, float volume) {
    char command[128];
    int level = (int)(clamp_volume(volume) * 1000.0f + 0.5f);
    snprintf(command, sizeof(command), "setaudio %s volume to %d", alias,
             level);
    mci_command(command, NULL, 0, 0);
}

static void close_music(void) {
    if (!g_music_open) return;
    mci_command("stop gd18_music", NULL, 0, 0);
    mci_command("close gd18_music", NULL, 0, 0);
    g_music_open = 0;
    g_music_paused = 0;
    g_music_path[0] = 0;
}

static int open_music(const char *requested) {
    char path[MAX_PATH * 2];
    char command[MAX_PATH * 2 + 96];
    if (!audio_asset_path(requested, 0, path, sizeof(path))) return 0;
    if (g_music_open && _stricmp(path, g_music_path) == 0) return 1;
    close_music();
    snprintf(command, sizeof(command),
             "open \"%s\" type mpegvideo alias gd18_music", path);
    if (!mci_command(command, NULL, 0, 1)) {
        snprintf(command, sizeof(command), "open \"%s\" alias gd18_music",
                 path);
        if (!mci_command(command, NULL, 0, 1)) return 0;
    }
    snprintf(g_music_path, sizeof(g_music_path), "%s", path);
    g_music_open = 1;
    set_alias_volume("gd18_music", g_music_volume);
    return 1;
}

static void close_effect_slot(EffectSlot *slot) {
    char command[80];
    if (!slot || !slot->open) return;
    snprintf(command, sizeof(command), "stop %s", slot->alias);
    mci_command(command, NULL, 0, 0);
    snprintf(command, sizeof(command), "close %s", slot->alias);
    mci_command(command, NULL, 0, 0);
    slot->open = 0;
    slot->paused = 0;
    slot->identifier = 0;
    slot->volume = 1.0f;
}

static EffectSlot *find_effect(unsigned identifier) {
    unsigned index;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        if (g_effects[index].open &&
            g_effects[index].identifier == identifier) {
            return &g_effects[index];
        }
    }
    return NULL;
}

void audio_initialize(const char *executable_directory) {
    unsigned index;
    snprintf(g_audio_directory, sizeof(g_audio_directory), "%s\\audio",
             executable_directory ? executable_directory : ".");
    snprintf(g_audio_cache_directory, sizeof(g_audio_cache_directory),
             "%s\\save\\audio-cache",
             executable_directory ? executable_directory : ".");
    snprintf(g_save_directory, sizeof(g_save_directory), "%s\\save",
             executable_directory ? executable_directory : ".");
    snprintf(g_apk_path, sizeof(g_apk_path), "%s\\game.apk",
             executable_directory ? executable_directory : ".");
    CreateDirectoryA(g_audio_cache_directory, NULL);
    InterlockedExchange(&g_effect_log_count, 0);
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        snprintf(g_effects[index].alias, sizeof(g_effects[index].alias),
                 "gd18_fx_%u", index);
    }
    initialize_output_meter();
    runtime_log("Windows MCI audio bridge initialized; APK cache: %s",
                g_audio_cache_directory);
}

void audio_set_apk_path(const char *apk_path) {
    if (apk_path && apk_path[0]) {
        snprintf(g_apk_path, sizeof(g_apk_path), "%s", apk_path);
    }
}

void audio_shutdown(void) {
    if (g_output_meter_stop) SetEvent(g_output_meter_stop);
    if (g_output_meter_thread) {
        WaitForSingleObject(g_output_meter_thread, INFINITE);
        CloseHandle(g_output_meter_thread);
        g_output_meter_thread = NULL;
    }
    if (g_output_meter_stop) {
        CloseHandle(g_output_meter_stop);
        g_output_meter_stop = NULL;
    }
    InterlockedExchange(&g_output_peak_bits, 0);
    InterlockedExchange(&g_output_peak_logged, 0);
    audio_stop_all_effects();
    close_music();
}

void audio_preload_background(const char *path) {
    (void)open_music(path);
}

void audio_play_background(const char *path, int loop) {
    char command[96];
    if (!open_music(path)) return;
    mci_command("seek gd18_music to start", NULL, 0, 0);
    snprintf(command, sizeof(command), "play gd18_music%s",
             loop ? " repeat" : "");
    if (mci_command(command, NULL, 0, 1)) {
        g_music_loop = loop != 0;
        g_music_paused = 0;
        runtime_log("Audio music playing: %s (loop=%s)", file_name_part(path),
                    loop ? "yes" : "no");
    }
}

void audio_stop_background(void) {
    if (g_music_open) {
        mci_command("stop gd18_music", NULL, 0, 0);
        mci_command("seek gd18_music to start", NULL, 0, 0);
    }
    g_music_paused = 0;
}

void audio_pause_background(void) {
    if (g_music_open && mci_command("pause gd18_music", NULL, 0, 0)) {
        g_music_paused = 1;
    }
}

void audio_resume_background(void) {
    char command[96];
    if (!g_music_open || !g_music_paused) return;
    snprintf(command, sizeof(command), "resume gd18_music%s",
             g_music_loop ? " repeat" : "");
    if (!mci_command(command, NULL, 0, 0)) {
        snprintf(command, sizeof(command), "play gd18_music%s",
                 g_music_loop ? " repeat" : "");
        mci_command(command, NULL, 0, 1);
    }
    g_music_paused = 0;
}

void audio_resume_background_from(float seconds) {
    char command[128];
    unsigned long milliseconds;
    if (!g_music_open) return;
    if (seconds < 0.0f) seconds = 0.0f;
    milliseconds = (unsigned long)(seconds * 1000.0f + 0.5f);
    mci_command("set gd18_music time format milliseconds", NULL, 0, 0);
    snprintf(command, sizeof(command), "seek gd18_music to %lu", milliseconds);
    if (!mci_command(command, NULL, 0, 1)) return;
    snprintf(command, sizeof(command), "play gd18_music from %lu%s",
             milliseconds, g_music_loop ? " repeat" : "");
    if (mci_command(command, NULL, 0, 1)) {
        g_music_paused = 0;
        runtime_log("Audio music resumed from %lu ms", milliseconds);
    }
}

void audio_rewind_background(void) {
    int playing = audio_is_background_playing();
    if (!g_music_open) return;
    mci_command("seek gd18_music to start", NULL, 0, 0);
    if (playing) {
        char command[96];
        snprintf(command, sizeof(command), "play gd18_music%s",
                 g_music_loop ? " repeat" : "");
        mci_command(command, NULL, 0, 1);
    }
}

void audio_set_background_time(float seconds) {
    char command[128];
    int playing;
    unsigned long milliseconds;
    if (!g_music_open) return;
    if (seconds < 0.0f) seconds = 0.0f;
    milliseconds = (unsigned long)(seconds * 1000.0f + 0.5f);
    playing = audio_is_background_playing();
    mci_command("set gd18_music time format milliseconds", NULL, 0, 0);
    snprintf(command, sizeof(command), "seek gd18_music to %lu", milliseconds);
    if (!mci_command(command, NULL, 0, 1)) return;
    if (playing) {
        snprintf(command, sizeof(command), "play gd18_music from %lu%s",
                 milliseconds, g_music_loop ? " repeat" : "");
        mci_command(command, NULL, 0, 1);
    }
}

float audio_get_background_time(void) {
    char value[64] = {0};
    char *end;
    unsigned long milliseconds;
    if (!g_music_open) return -1.0f;
    mci_command("set gd18_music time format milliseconds", NULL, 0, 0);
    if (!mci_command("status gd18_music position", value, sizeof(value), 0)) {
        return -1.0f;
    }
    milliseconds = strtoul(value, &end, 10);
    if (end == value) return -1.0f;
    return (float)milliseconds / 1000.0f;
}

int audio_is_background_playing(void) {
    char mode[32] = {0};
    if (!g_music_open) return 0;
    if (!mci_command("status gd18_music mode", mode, sizeof(mode), 0)) return 0;
    return _stricmp(mode, "playing") == 0;
}

float audio_get_background_volume(void) { return g_music_volume; }

void audio_set_background_volume(float volume) {
    g_music_volume = clamp_volume(volume);
    if (g_music_open) set_alias_volume("gd18_music", g_music_volume);
}

float audio_get_output_peak(void) {
    union {
        float floating;
        LONG integer;
    } peak_bits;
    peak_bits.integer = InterlockedCompareExchange(&g_output_peak_bits, 0, 0);
    return clamp_volume(peak_bits.floating);
}

void audio_preload_effect(const char *path) {
    char resolved[MAX_PATH * 2];
    (void)audio_asset_path(path, 1, resolved, sizeof(resolved));
}

unsigned audio_play_effect(const char *path, int loop) {
    char resolved[MAX_PATH * 2];
    char command[MAX_PATH * 2 + 96];
    EffectSlot *slot;
    unsigned identifier;
    if (!audio_asset_path(path, 1, resolved, sizeof(resolved))) return 0;
    slot = &g_effects[g_next_effect_slot++ % MAX_EFFECT_SLOTS];
    close_effect_slot(slot);
    identifier = g_next_effect_identifier++;
    if (!identifier) identifier = g_next_effect_identifier++;
    snprintf(command, sizeof(command),
             "open \"%s\" type waveaudio alias %s", resolved, slot->alias);
    if (!mci_command(command, NULL, 0, 1)) return 0;
    slot->open = 1;
    slot->identifier = identifier;
    slot->volume = 1.0f;
    set_alias_volume(slot->alias, g_effects_volume * slot->volume);
    snprintf(command, sizeof(command), "play %s from 0%s", slot->alias,
             loop ? " repeat" : "");
    if (!mci_command(command, NULL, 0, 1)) {
        close_effect_slot(slot);
        return 0;
    }
    if (InterlockedIncrement(&g_effect_log_count) <= 64) {
        runtime_log("Audio effect playing: %s (id=%u, loop=%s)",
                    file_name_part(path), identifier, loop ? "yes" : "no");
    }
    return identifier;
}

int audio_is_effect_playing(unsigned identifier) {
    EffectSlot *slot = find_effect(identifier);
    char command[80];
    char mode[32] = {0};
    if (!slot) return 0;
    snprintf(command, sizeof(command), "status %s mode", slot->alias);
    if (!mci_command(command, mode, sizeof(mode), 0)) return 0;
    return _stricmp(mode, "playing") == 0 ||
           _stricmp(mode, "paused") == 0;
}

void audio_set_effect_volume(unsigned identifier, float volume) {
    EffectSlot *slot = find_effect(identifier);
    if (!slot) return;
    slot->volume = clamp_volume(volume);
    set_alias_volume(slot->alias, g_effects_volume * slot->volume);
}

void audio_pause_effect(unsigned identifier) {
    EffectSlot *slot = find_effect(identifier);
    char command[80];
    if (!slot) return;
    snprintf(command, sizeof(command), "pause %s", slot->alias);
    if (mci_command(command, NULL, 0, 0)) slot->paused = 1;
}

void audio_resume_effect(unsigned identifier) {
    EffectSlot *slot = find_effect(identifier);
    char command[80];
    if (!slot || !slot->paused) return;
    snprintf(command, sizeof(command), "resume %s", slot->alias);
    if (mci_command(command, NULL, 0, 0)) slot->paused = 0;
}

void audio_stop_effect(unsigned identifier) {
    close_effect_slot(find_effect(identifier));
}

void audio_pause_all_effects(void) {
    unsigned index;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        if (g_effects[index].open) audio_pause_effect(g_effects[index].identifier);
    }
}

void audio_resume_all_effects(void) {
    unsigned index;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        if (g_effects[index].open) audio_resume_effect(g_effects[index].identifier);
    }
}

void audio_stop_all_effects(void) {
    unsigned index;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        close_effect_slot(&g_effects[index]);
    }
}

void audio_unload_effect(const char *path) {
    (void)path;
    /* Each play owns a short-lived MCI alias; there is no persistent decoder. */
}

float audio_get_effects_volume(void) { return g_effects_volume; }

void audio_set_effects_volume(float volume) {
    unsigned index;
    g_effects_volume = clamp_volume(volume);
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        if (g_effects[index].open) {
            set_alias_volume(g_effects[index].alias,
                             g_effects_volume * g_effects[index].volume);
        }
    }
}
