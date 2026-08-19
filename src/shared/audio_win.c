#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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
#include "runtime_settings.h"
#include "zlib.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MAX_EFFECT_SLOTS 48
#define MAX_EFFECT_ASSET_CACHE 128
#define GD_WAVE_FORMAT_IEEE_FLOAT 0x0003u
#define GD_WAVE_FORMAT_EXTENSIBLE 0xfffeu

typedef struct {
    unsigned identifier;
    int open;
    int paused;
    int loop;
    float volume;
    HWAVEOUT output;
    WAVEHDR header;
    unsigned char *wave_file;
    size_t wave_file_size;
    char path[MAX_PATH * 2];
} EffectSlot;

typedef struct {
    char name[MAX_PATH];
    char path[MAX_PATH * 2];
} EffectAssetCache;

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
static EffectAssetCache g_effect_asset_cache[MAX_EFFECT_ASSET_CACHE];
static unsigned g_effect_asset_cache_count;
static volatile LONG g_next_effect_identifier;
static unsigned g_next_effect_slot;
static float g_music_volume = 1.0f;
static float g_effects_volume = 1.0f;
static volatile LONG g_effect_log_count;
static int g_music_open;
static int g_music_paused;
static int g_music_loop;
static int g_legacy_first_play_replay;
static int g_music_needs_first_play_replay;
static HANDLE g_output_meter_thread;
static HANDLE g_output_meter_stop;
static volatile LONG g_output_peak_bits;
static volatile LONG g_output_peak_logged;
static float g_output_peak_smoothed;
static float g_output_peak_max = 0.30f;
static volatile LONG g_short_path_logged;
static volatile LONG g_effect_cache_hit_logged;
static volatile LONG g_waveout_backend_logged;
static SRWLOCK g_mci_lock = SRWLOCK_INIT;

static SRWLOCK g_effect_state_lock = SRWLOCK_INIT;

#define EFFECT_COMMAND_QUEUE_CAPACITY 256

typedef enum {
    EFFECT_COMMAND_PRELOAD = 1,
    EFFECT_COMMAND_PLAY,
    EFFECT_COMMAND_SET_VOLUME,
    EFFECT_COMMAND_PAUSE,
    EFFECT_COMMAND_RESUME,
    EFFECT_COMMAND_STOP,
    EFFECT_COMMAND_PAUSE_ALL,
    EFFECT_COMMAND_RESUME_ALL,
    EFFECT_COMMAND_STOP_ALL,
    EFFECT_COMMAND_UNLOAD,
    EFFECT_COMMAND_APPLY_MASTER_VOLUME
} EffectCommandType;

typedef struct {
    EffectCommandType type;
    unsigned identifier;
    int loop;
    float pitch;
    float pan;
    float volume;
    char path[MAX_PATH * 2];
} EffectCommand;

static EffectCommand g_effect_commands[EFFECT_COMMAND_QUEUE_CAPACITY];
static unsigned g_effect_command_read;
static unsigned g_effect_command_write;
static unsigned g_effect_command_count;
static CRITICAL_SECTION g_effect_command_lock;
static int g_effect_command_lock_initialized;
static HANDLE g_effect_command_event;
static HANDLE g_effect_worker_stop;
static HANDLE g_effect_worker_thread;
static volatile LONG g_effect_worker_ready;
static volatile LONG g_effect_queue_overflow_logged;

static void initialize_effect_worker(void);
static void shutdown_effect_worker(void);

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
        {
            const float raw = clamp_volume(peak_bits.floating);
            const float noise_floor = 0.02f;
            const float full_scale = 0.75f;
            float normalized = 0.0f;
            float mapped;
            float smoothing;
            if (raw > noise_floor)
                normalized = (raw - noise_floor) / (full_scale - noise_floor);
            normalized = clamp_volume(normalized);
            mapped = normalized * g_output_peak_max;
            /* Fast enough to react to a beat, slow enough to prevent one
             * endpoint spike from making 2.2 music pulses fill the screen. */
            smoothing = mapped > g_output_peak_smoothed ? 0.35f : 0.12f;
            g_output_peak_smoothed +=
                (mapped - g_output_peak_smoothed) * smoothing;
            peak_bits.floating = clamp_volume(g_output_peak_smoothed);
        }
        InterlockedExchange(&g_output_peak_bits, peak_bits.integer);
        if (peak_bits.floating > 0.001f &&
            InterlockedCompareExchange(&g_output_peak_logged, 1, 0) == 0) {
            runtime_log("WASAPI FMOD metering: first mapped peak %.3f cap=%.3f",
                        peak_bits.floating, g_output_peak_max);
        }
    }
    InterlockedExchange(&g_output_peak_bits, 0);
    meter->lpVtbl->Release(meter);
    if (release_com) CoUninitialize();
    return 0;
}

static void initialize_output_meter(void) {
    if (g_output_meter_thread) return;
    g_output_peak_max = gd_settings_music_pulse_max();
    g_output_peak_smoothed = 0.0f;
    runtime_log("Music pulse meter mapping: floor=0.020 full-scale=0.750 cap=%.3f",
                g_output_peak_max);
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
    MCIERROR error;
    char message[256] = "unknown MCI error";
    AcquireSRWLockExclusive(&g_mci_lock);
    error = mciSendStringA(command, result, capacity, NULL);
    if (error && report_error) {
        mciGetErrorStringA(error, message, sizeof(message));
    }
    ReleaseSRWLockExclusive(&g_mci_lock);
    if (error && report_error) {
        runtime_log("Audio MCI error %lu: %s | %s", (unsigned long)error,
                    message, command);
    }
    return error == 0;
}

static void mci_compatible_path(const char *path, char *destination,
                                size_t capacity) {
    char full[MAX_PATH * 2];
    char current[MAX_PATH * 2];
    DWORD full_length;
    DWORD current_length;
    DWORD short_length;
    if (!path || !destination || !capacity) return;
    full_length = GetFullPathNameA(path, (DWORD)sizeof(full), full, NULL);
    if (!full_length || full_length >= sizeof(full)) {
        snprintf(full, sizeof(full), "%s", path);
    }
    /* main.c pins the working directory to the folder containing the EXE.
       Prefer a short relative path for anything inside that folder. Unlike
       DOS 8.3 names, this remains available when short-name generation is
       disabled on the C: volume. */
    current_length = GetCurrentDirectoryA((DWORD)sizeof(current), current);
    if (current_length && current_length < sizeof(current)) {
        while (current_length > 0 &&
               (current[current_length - 1] == '\\' ||
                current[current_length - 1] == '/')) {
            current[--current_length] = 0;
        }
        if (current_length &&
            _strnicmp(full, current, current_length) == 0 &&
            (full[current_length] == '\\' || full[current_length] == '/')) {
            snprintf(destination, capacity, ".\\%s", full + current_length + 1);
            if (InterlockedCompareExchange(&g_short_path_logged, 1, 0) == 0) {
                runtime_log("Audio MCI: using compact executable-relative paths");
            }
            return;
        }
    }
    short_length = GetShortPathNameA(full, destination, (DWORD)capacity);
    if (short_length && short_length < capacity) {
        if (_stricmp(destination, full) != 0 &&
            InterlockedCompareExchange(&g_short_path_logged, 1, 0) == 0) {
            runtime_log("Audio MCI: using a short Windows path to avoid path-length limits");
        }
        return;
    }
    snprintf(destination, capacity, "%s", full);
}

static int mci_open_path(const char *path, const char *type,
                         const char *alias, int report_error) {
    char compatible[MAX_PATH * 2];
    char command[MAX_PATH * 2 + 96];
    int different;
    mci_compatible_path(path, compatible, sizeof(compatible));
    different = _stricmp(compatible, path) != 0;
    if (type && type[0]) {
        snprintf(command, sizeof(command), "open \"%s\" type %s alias %s",
                 compatible, type, alias);
    } else {
        snprintf(command, sizeof(command), "open \"%s\" alias %s",
                 compatible, alias);
    }
    if (mci_command(command, NULL, 0, different ? 0 : report_error)) return 1;
    if (!different) return 0;
    if (type && type[0]) {
        snprintf(command, sizeof(command), "open \"%s\" type %s alias %s",
                 path, type, alias);
    } else {
        snprintf(command, sizeof(command), "open \"%s\" alias %s", path,
                 alias);
    }
    return mci_command(command, NULL, 0, report_error);
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

static int effect_cache_lookup(const char *name, char *destination,
                               size_t capacity) {
    unsigned index;
    for (index = 0; index < g_effect_asset_cache_count; ++index) {
        EffectAssetCache *entry = &g_effect_asset_cache[index];
        if (_stricmp(entry->name, name) != 0) continue;
        if (!file_is_regular(entry->path)) {
            entry->name[0] = 0;
            entry->path[0] = 0;
            return 0;
        }
        snprintf(destination, capacity, "%s", entry->path);
        if (InterlockedCompareExchange(&g_effect_cache_hit_logged, 1, 0) == 0)
            runtime_log("Audio effect cache: reusing decoded files without "
                        "reopening game.apk");
        return 1;
    }
    return 0;
}

static void effect_cache_remember(const char *name, const char *path) {
    unsigned index;
    EffectAssetCache *entry = NULL;
    if (!name || !name[0] || !path || !path[0]) return;
    for (index = 0; index < g_effect_asset_cache_count; ++index) {
        if (_stricmp(g_effect_asset_cache[index].name, name) == 0) {
            entry = &g_effect_asset_cache[index];
            break;
        }
    }
    if (!entry && g_effect_asset_cache_count < MAX_EFFECT_ASSET_CACHE)
        entry = &g_effect_asset_cache[g_effect_asset_cache_count++];
    if (!entry) return;
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->path, sizeof(entry->path), "%s", path);
}

static int find_persistent_effect_cache(const char *name, char *destination,
                                        size_t capacity) {
    WIN32_FILE_ATTRIBUTE_DATA apk_attributes;
    WIN32_FIND_DATAA candidate;
    HANDLE search;
    char stem[MAX_PATH];
    char pattern[MAX_PATH * 2];
    char *extension;
    size_t stem_length;
    if (!name || strchr(name, '*') || strchr(name, '?') ||
        !GetFileAttributesExA(g_apk_path, GetFileExInfoStandard,
                              &apk_attributes))
        return 0;
    snprintf(stem, sizeof(stem), "%s", name);
    extension = strrchr(stem, '.');
    if (!extension || (_stricmp(extension, ".ogg") != 0 &&
                       _stricmp(extension, ".wav") != 0))
        return 0;
    *extension = 0;
    stem_length = strlen(stem);
    snprintf(pattern, sizeof(pattern), "%s\\%s-????????.wav",
             g_audio_cache_directory, stem);
    search = FindFirstFileA(pattern, &candidate);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        const char *suffix = candidate.cFileName + stem_length;
        if ((candidate.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            _strnicmp(candidate.cFileName, stem, stem_length) != 0 ||
            strlen(suffix) != 13u || suffix[0] != '-' ||
            _stricmp(suffix + 9, ".wav") != 0 ||
            CompareFileTime(&candidate.ftLastWriteTime,
                            &apk_attributes.ftLastWriteTime) < 0)
            continue;
        snprintf(destination, capacity, "%s\\%s",
                 g_audio_cache_directory, candidate.cFileName);
        FindClose(search);
        if (file_is_regular(destination)) {
            runtime_log("Audio cache: reused %s from an existing decoded "
                        "APK effect", name);
            return 1;
        }
        return 0;
    } while (FindNextFileA(search, &candidate));
    FindClose(search);
    return 0;
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

static void write_little_u16(unsigned char *destination, unsigned value) {
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void write_little_u32(unsigned char *destination, uint32_t value) {
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static int write_pcm_wave(const char *destination, const short *samples,
                          int samples_per_channel, int channels,
                          int sample_rate) {
    uint64_t sample_count;
    uint64_t pcm_size_64;
    size_t wave_size;
    uint32_t pcm_size;
    uint32_t byte_rate;
    unsigned char *wave;
    int result;
    if (!samples || samples_per_channel <= 0 || channels <= 0 ||
        channels > 8 || sample_rate <= 0) {
        return 0;
    }
    sample_count = (uint64_t)(unsigned)samples_per_channel *
                   (uint64_t)(unsigned)channels;
    pcm_size_64 = sample_count * sizeof(short);
    if (pcm_size_64 > UINT32_MAX || pcm_size_64 > SIZE_MAX - 44) return 0;
    pcm_size = (uint32_t)pcm_size_64;
    wave_size = (size_t)pcm_size + 44;
    byte_rate = (uint32_t)sample_rate * (uint32_t)channels * 2u;
    wave = (unsigned char *)malloc(wave_size);
    if (!wave) return 0;
    memcpy(wave, "RIFF", 4);
    write_little_u32(wave + 4, 36u + pcm_size);
    memcpy(wave + 8, "WAVEfmt ", 8);
    write_little_u32(wave + 16, 16);
    write_little_u16(wave + 20, 1); /* PCM */
    write_little_u16(wave + 22, (unsigned)channels);
    write_little_u32(wave + 24, (uint32_t)sample_rate);
    write_little_u32(wave + 28, byte_rate);
    write_little_u16(wave + 32, (unsigned)channels * 2u);
    write_little_u16(wave + 34, 16);
    memcpy(wave + 36, "data", 4);
    write_little_u32(wave + 40, pcm_size);
    memcpy(wave + 44, samples, pcm_size);
    result = write_cached_audio(destination, wave, wave_size);
    free(wave);
    return result;
}

static int materialize_apk_effect(const char *name, char *destination,
                                  size_t capacity) {
    char member[MAX_PATH * 2];
    char stem[MAX_PATH];
    char *extension;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    uLong checksum;
    short *samples = NULL;
    int channels = 0;
    int sample_rate = 0;
    int sample_count;
    int result = 0;
    if (!name || !name[0]) return 0;
    extension = strrchr(name, '.');
    if (!extension || (_stricmp(extension, ".ogg") != 0 &&
                       _stricmp(extension, ".wav") != 0)) {
        return 0;
    }
    snprintf(member, sizeof(member), "assets/%s", name);
    if (!apk_extract_member(g_apk_path, member, &payload, &payload_size)) {
        return 0;
    }
    checksum = crc32(0L, Z_NULL, 0);
    checksum = crc32(checksum, payload, (uInt)payload_size);
    snprintf(stem, sizeof(stem), "%s", name);
    extension = strrchr(stem, '.');
    if (extension) *extension = 0;
    if (snprintf(destination, capacity, "%s\\%s-%08lx.wav",
                 g_audio_cache_directory, stem,
                 (unsigned long)checksum) < 0 ||
        strlen(destination) + 1 >= capacity) {
        free(payload);
        return 0;
    }
    if (file_is_regular(destination)) {
        free(payload);
        return 1;
    }
    if (_stricmp(strrchr(name, '.'), ".wav") == 0) {
        result = write_cached_audio(destination, payload, payload_size);
    } else if (payload_size <= INT_MAX) {
        sample_count = stb_vorbis_decode_memory(
            payload, (int)payload_size, &channels, &sample_rate, &samples);
        if (sample_count > 0) {
            result = write_pcm_wave(destination, samples, sample_count,
                                    channels, sample_rate);
        }
    }
    free(samples);
    free(payload);
    if (result) {
        runtime_log("Audio cache: decoded %s from current game.apk", name);
    } else {
        runtime_log("Audio cache: could not decode APK effect %s", name);
    }
    return result;
}

static uint64_t file_size_from_attributes(const WIN32_FILE_ATTRIBUTE_DATA *data) {
    return ((uint64_t)data->nFileSizeHigh << 32) | data->nFileSizeLow;
}

static int prepare_id3_stripped_mp3(const char *source, char *destination,
                                    size_t capacity,
                                    uint32_t minimum_tag_size) {
    WIN32_FILE_ATTRIBUTE_DATA source_data;
    WIN32_FILE_ATTRIBUTE_DATA destination_data;
    unsigned char header[10];
    unsigned char buffer[64 * 1024];
    char temporary[MAX_PATH * 2 + 32];
    const char *name = file_name_part(source);
    const char *extension = strrchr(name, '.');
    uint64_t source_size;
    uint64_t expected_size;
    uint32_t tag_size;
    size_t skip;
    FILE *input;
    FILE *output;
    int ok = 1;
    if (!extension || _stricmp(extension, ".mp3") != 0 ||
        !GetFileAttributesExA(source, GetFileExInfoStandard, &source_data)) {
        return 0;
    }
    input = fopen(source, "rb");
    if (!input) return 0;
    if (fread(header, 1, sizeof(header), input) != sizeof(header) ||
        memcmp(header, "ID3", 3) != 0 ||
        (header[6] | header[7] | header[8] | header[9]) & 0x80) {
        fclose(input);
        return 0;
    }
    tag_size = ((uint32_t)header[6] << 21) |
               ((uint32_t)header[7] << 14) |
               ((uint32_t)header[8] << 7) | header[9];
    if (tag_size < minimum_tag_size) {
        fclose(input);
        return 0;
    }
    skip = 10u + tag_size;
    if ((header[5] & 0x10) != 0) skip += 10u; /* ID3v2.4 footer */
    source_size = file_size_from_attributes(&source_data);
    if (skip >= source_size || skip > LONG_MAX) {
        fclose(input);
        return 0;
    }
    expected_size = source_size - skip;
    if (snprintf(destination, capacity, "%s\\mci-%s",
                 g_audio_cache_directory, name) < 0 ||
        strlen(destination) + 1 >= capacity) {
        fclose(input);
        return 0;
    }
    if (GetFileAttributesExA(destination, GetFileExInfoStandard,
                             &destination_data) &&
        file_size_from_attributes(&destination_data) == expected_size &&
        CompareFileTime(&destination_data.ftLastWriteTime,
                        &source_data.ftLastWriteTime) >= 0) {
        fclose(input);
        return 1;
    }
    if (fseek(input, (long)skip, SEEK_SET) != 0) {
        fclose(input);
        return 0;
    }
    snprintf(temporary, sizeof(temporary), "%s.wrapper.tmp", destination);
    output = fopen(temporary, "wb");
    if (!output) {
        fclose(input);
        return 0;
    }
    for (;;) {
        size_t count = fread(buffer, 1, sizeof(buffer), input);
        if (count && fwrite(buffer, 1, count, output) != count) {
            ok = 0;
            break;
        }
        if (count < sizeof(buffer)) {
            if (ferror(input)) ok = 0;
            break;
        }
    }
    if (fclose(input) != 0) ok = 0;
    if (ok && (fflush(output) != 0 || _commit(_fileno(output)) != 0)) ok = 0;
    if (fclose(output) != 0) ok = 0;
    if (!ok || !MoveFileExA(temporary, destination,
                            MOVEFILE_REPLACE_EXISTING |
                            MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temporary);
        return 0;
    }
    runtime_log("Audio MCI: stripped ID3 metadata from %s", name);
    return 1;
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
    if (effect) {
        if (effect_cache_lookup(name, destination, capacity)) return 1;
        if (find_persistent_effect_cache(name, destination, capacity)) {
            effect_cache_remember(name, destination);
            return 1;
        }
        if (materialize_apk_effect(name, destination, capacity)) {
            effect_cache_remember(name, destination);
            return 1;
        }
    }
    snprintf(destination, capacity, "%s\\%s", g_audio_cache_directory,
             converted);
    if (file_is_regular(destination)) return 1;
    if ((effect && materialize_embedded_effect(converted, destination)) ||
        (!effect && materialize_apk_audio(name, destination))) {
        if (effect) effect_cache_remember(name, destination);
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
    g_music_needs_first_play_replay = 0;
    g_music_path[0] = 0;
}

static int open_music(const char *requested) {
    char path[MAX_PATH * 2];
    char sanitized[MAX_PATH * 2];
    int have_sanitized;
    int opened;
    if (!audio_asset_path(requested, 0, path, sizeof(path))) return 0;
    if (g_music_open && _stricmp(path, g_music_path) == 0) return 1;
    close_music();
    /* Large ID3 tags normally contain album art. Some Windows MCI versions
       accept the file at open time but fail only when playback starts, so use
       the metadata-free copy proactively instead of waiting for open to fail. */
    have_sanitized = prepare_id3_stripped_mp3(
        path, sanitized, sizeof(sanitized), 4096);
    if (have_sanitized) {
        opened = mci_open_path(sanitized, "mpegvideo", "gd18_music", 0) ||
                 mci_open_path(sanitized, NULL, "gd18_music", 0);
        if (!opened) {
            mci_command("close gd18_music", NULL, 0, 0);
            opened = mci_open_path(path, "mpegvideo", "gd18_music", 0) ||
                     mci_open_path(path, NULL, "gd18_music", 0);
        }
    } else {
        opened = mci_open_path(path, "mpegvideo", "gd18_music", 0) ||
                 mci_open_path(path, NULL, "gd18_music", 0);
    }
    if (!opened && !have_sanitized &&
        prepare_id3_stripped_mp3(path, sanitized, sizeof(sanitized), 0)) {
        mci_command("close gd18_music", NULL, 0, 0);
        opened = mci_open_path(sanitized, "mpegvideo", "gd18_music", 0) ||
                 mci_open_path(sanitized, NULL, "gd18_music", 1);
    }
    if (!opened) {
        /* Repeat once with diagnostics enabled so the log contains the actual
           Windows codec/path error, not merely a silent preload failure. */
        mci_command("close gd18_music", NULL, 0, 0);
        if (!mci_open_path(path, NULL, "gd18_music", 1)) return 0;
    }
    snprintf(g_music_path, sizeof(g_music_path), "%s", path);
    g_music_open = 1;
    g_music_needs_first_play_replay = 1;
    return 1;
}

static void close_effect_slot(EffectSlot *slot) {
    if (!slot) return;
    if (slot->output) {
        waveOutReset(slot->output);
        if ((slot->header.dwFlags & WHDR_PREPARED) != 0)
            waveOutUnprepareHeader(
                slot->output, &slot->header, sizeof(slot->header));
        waveOutClose(slot->output);
    }
    free(slot->wave_file);
    memset(&slot->header, 0, sizeof(slot->header));
    slot->output = NULL;
    slot->wave_file = NULL;
    slot->wave_file_size = 0;
    slot->open = 0;
    slot->paused = 0;
    slot->loop = 0;
    slot->identifier = 0;
    slot->volume = 1.0f;
    slot->path[0] = 0;
}

static void park_effect_slot(EffectSlot *slot) {
    if (!slot || !slot->open) return;
    if (slot->output) waveOutReset(slot->output);
    slot->paused = 0;
    slot->loop = 0;
    slot->identifier = 0;
    slot->volume = 1.0f;
}

static int effect_slot_playing(EffectSlot *slot) {
    if (!slot || !slot->open || !slot->identifier) return 0;
    return slot->paused || (slot->header.dwFlags & WHDR_DONE) == 0;
}

static EffectSlot *reusable_effect_slot(const char *path) {
    unsigned index;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        EffectSlot *slot = &g_effects[index];
        if (!slot->open || _stricmp(slot->path, path) != 0) continue;
        if (!slot->identifier || !effect_slot_playing(slot)) {
            slot->identifier = 0;
            slot->paused = 0;
            return slot;
        }
    }
    return NULL;
}

static unsigned read_little_u16(const unsigned char *source) {
    return (unsigned)source[0] | ((unsigned)source[1] << 8);
}

static uint32_t read_little_u32(const unsigned char *source) {
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static int load_wave_file(const char *path, unsigned char **file_data,
                          size_t *file_size, WAVEFORMATEX **format,
                          unsigned char **pcm_data, DWORD *pcm_size,
                          unsigned char *format_storage,
                          size_t format_storage_size) {
    FILE *stream;
    long length;
    unsigned char *bytes;
    size_t cursor;
    const unsigned char *format_chunk = NULL;
    size_t format_size = 0;
    unsigned char *data_chunk = NULL;
    size_t data_size = 0;
    if (!path || !file_data || !file_size || !format || !pcm_data ||
        !pcm_size || !format_storage ||
        format_storage_size < sizeof(WAVEFORMATEX))
        return 0;
    stream = fopen(path, "rb");
    if (!stream) return 0;
    if (fseek(stream, 0, SEEK_END) != 0 ||
        (length = ftell(stream)) < 44 || length > 64L * 1024L * 1024L ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return 0;
    }
    bytes = (unsigned char *)malloc((size_t)length);
    if (!bytes) {
        fclose(stream);
        return 0;
    }
    if (fread(bytes, 1, (size_t)length, stream) != (size_t)length) {
        fclose(stream);
        free(bytes);
        return 0;
    }
    fclose(stream);
    if (memcmp(bytes, "RIFF", 4) != 0 ||
        memcmp(bytes + 8, "WAVE", 4) != 0) {
        free(bytes);
        return 0;
    }
    cursor = 12;
    while (cursor + 8 <= (size_t)length) {
        const uint32_t chunk_size = read_little_u32(bytes + cursor + 4);
        const size_t chunk_data = cursor + 8;
        const size_t padded_size = (size_t)chunk_size + (chunk_size & 1u);
        if (chunk_data > (size_t)length ||
            padded_size > (size_t)length - chunk_data)
            break;
        if (memcmp(bytes + cursor, "fmt ", 4) == 0 && !format_chunk) {
            format_chunk = bytes + chunk_data;
            format_size = chunk_size;
        } else if (memcmp(bytes + cursor, "data", 4) == 0 && !data_chunk) {
            data_chunk = bytes + chunk_data;
            data_size = chunk_size;
        }
        cursor = chunk_data + padded_size;
    }
    if (!format_chunk || format_size < 16 || format_size > 64 ||
        !data_chunk || !data_size || data_size > MAXDWORD) {
        free(bytes);
        return 0;
    }
    memset(format_storage, 0, format_storage_size);
    memcpy(format_storage, format_chunk,
           format_size < format_storage_size
               ? format_size
               : format_storage_size);
    *format = (WAVEFORMATEX *)format_storage;
    if (format_size == 16) {
        (*format)->cbSize = 0;
    } else if (format_size < 18 ||
               (size_t)(*format)->cbSize + 18u > format_size) {
        free(bytes);
        return 0;
    }
    if ((*format)->nChannels == 0 || (*format)->nSamplesPerSec == 0 ||
        (*format)->nBlockAlign == 0 ||
        (read_little_u16(format_chunk) != WAVE_FORMAT_PCM &&
         read_little_u16(format_chunk) != GD_WAVE_FORMAT_IEEE_FLOAT &&
         read_little_u16(format_chunk) != GD_WAVE_FORMAT_EXTENSIBLE)) {
        free(bytes);
        return 0;
    }
    *file_data = bytes;
    *file_size = (size_t)length;
    *pcm_data = data_chunk;
    *pcm_size = (DWORD)data_size;
    return 1;
}

static void set_waveout_volume(EffectSlot *slot, float volume) {
    const DWORD channel = (DWORD)(clamp_volume(volume) * 65535.0f + 0.5f);
    if (slot && slot->output)
        waveOutSetVolume(slot->output, channel | (channel << 16));
}

static EffectSlot *open_effect_slot(const char *path, int report_error) {
    EffectSlot *slot = reusable_effect_slot(path);
    unsigned char format_storage[64];
    WAVEFORMATEX *format = NULL;
    unsigned char *pcm_data = NULL;
    DWORD pcm_size = 0;
    MMRESULT status;
    char error_text[256] = "unknown waveOut error";
    if (slot) return slot;
    slot = &g_effects[g_next_effect_slot++ % MAX_EFFECT_SLOTS];
    close_effect_slot(slot);
    if (!load_wave_file(path, &slot->wave_file, &slot->wave_file_size,
                        &format, &pcm_data, &pcm_size, format_storage,
                        sizeof(format_storage))) {
        if (report_error)
            runtime_log("Audio waveOut: invalid or unsupported WAV file %s",
                        path);
        return NULL;
    }
    status = waveOutOpen(&slot->output, WAVE_MAPPER, format, 0, 0,
                         CALLBACK_NULL);
    if (status != MMSYSERR_NOERROR) {
        if (report_error) {
            waveOutGetErrorTextA(status, error_text, sizeof(error_text));
            runtime_log("Audio waveOut open error %u: %s | %s",
                        (unsigned)status, error_text, path);
        }
        close_effect_slot(slot);
        return NULL;
    }
    memset(&slot->header, 0, sizeof(slot->header));
    slot->header.lpData = (LPSTR)pcm_data;
    slot->header.dwBufferLength = pcm_size;
    status = waveOutPrepareHeader(
        slot->output, &slot->header, sizeof(slot->header));
    if (status != MMSYSERR_NOERROR) {
        if (report_error) {
            waveOutGetErrorTextA(status, error_text, sizeof(error_text));
            runtime_log("Audio waveOut prepare error %u: %s | %s",
                        (unsigned)status, error_text, path);
        }
        close_effect_slot(slot);
        return NULL;
    }
    slot->open = 1;
    slot->identifier = 0;
    slot->paused = 0;
    slot->loop = 0;
    slot->volume = 1.0f;
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    if (InterlockedCompareExchange(&g_waveout_backend_logged, 1, 0) == 0)
        runtime_log(
            "RESULT: DYNARMIC_WAVEOUT_EFFECT_BACKEND_ACTIVE overlapping-slots=%u",
            MAX_EFFECT_SLOTS);
    return slot;
}

static EffectSlot *find_effect(unsigned identifier) {
    unsigned index;
    if (!identifier) return NULL;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        if (g_effects[index].open &&
            g_effects[index].identifier == identifier) {
            return &g_effects[index];
        }
    }
    return NULL;
}

void audio_initialize(const char *executable_directory) {
    const char *configured_save = getenv("GD_SAVE_DIR");
    const char *base = executable_directory ? executable_directory : ".";

    snprintf(g_audio_directory, sizeof(g_audio_directory), "%s\\audio", base);

    /*
     * The native launcher selects a package/version/backend save profile.
     * Honour it immediately instead of briefly creating x86\save or an ARM
     * backend-local cache before audio_set_writable_directory() runs.
     */
    if (configured_save && configured_save[0]) {
        snprintf(g_save_directory, sizeof(g_save_directory), "%s",
                 configured_save);
        snprintf(g_audio_cache_directory, sizeof(g_audio_cache_directory),
                 "%s\\audio-cache", configured_save);
    } else {
        snprintf(g_save_directory, sizeof(g_save_directory), "%s\\save", base);
        snprintf(g_audio_cache_directory, sizeof(g_audio_cache_directory),
                 "%s\\save\\audio-cache", base);
    }
    snprintf(g_apk_path, sizeof(g_apk_path), "%s\\game.apk", base);
    CreateDirectoryA(g_audio_cache_directory, NULL);
    InterlockedExchange(&g_effect_log_count, 0);
    InterlockedExchange(&g_next_effect_identifier, 0);
    InterlockedExchange(&g_effect_queue_overflow_logged, 0);
    InterlockedExchange(&g_short_path_logged, 0);
    InterlockedExchange(&g_effect_cache_hit_logged, 0);
    InterlockedExchange(&g_waveout_backend_logged, 0);
    memset(g_effect_asset_cache, 0, sizeof(g_effect_asset_cache));
    g_effect_asset_cache_count = 0;
    memset(g_effects, 0, sizeof(g_effects));
    initialize_output_meter();
    initialize_effect_worker();
    runtime_log(
        "Windows audio bridge initialized; music=MCI effects=waveOut APK cache: %s",
        g_audio_cache_directory);
}

void audio_set_writable_directory(const char *writable_directory) {
    if (!writable_directory || !writable_directory[0]) return;
    snprintf(g_save_directory, sizeof(g_save_directory), "%s",
             writable_directory);
    snprintf(g_audio_cache_directory, sizeof(g_audio_cache_directory),
             "%s\\audio-cache", writable_directory);
    CreateDirectoryA(g_save_directory, NULL);
    CreateDirectoryA(g_audio_cache_directory, NULL);
    runtime_log("Audio writable cache directory: %s",
                g_audio_cache_directory);
}

void audio_set_apk_path(const char *apk_path) {
    if (apk_path && apk_path[0]) {
        if (_stricmp(g_apk_path, apk_path) != 0) {
            unsigned index;
            AcquireSRWLockExclusive(&g_effect_state_lock);
            for (index = 0; index < MAX_EFFECT_SLOTS; ++index)
                close_effect_slot(&g_effects[index]);
            memset(g_effect_asset_cache, 0, sizeof(g_effect_asset_cache));
            g_effect_asset_cache_count = 0;
            InterlockedExchange(&g_effect_cache_hit_logged, 0);
            ReleaseSRWLockExclusive(&g_effect_state_lock);
        }
        snprintf(g_apk_path, sizeof(g_apk_path), "%s", apk_path);
    }
}

void audio_set_legacy_first_play_replay(int enabled) {
    g_legacy_first_play_replay = enabled != 0;
    if (!g_legacy_first_play_replay)
        g_music_needs_first_play_replay = 0;
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
    shutdown_effect_worker();
    AcquireSRWLockExclusive(&g_effect_state_lock);
    {
        unsigned index;
        for (index = 0; index < MAX_EFFECT_SLOTS; ++index)
            close_effect_slot(&g_effects[index]);
    }
    ReleaseSRWLockExclusive(&g_effect_state_lock);
    close_music();
}

void audio_preload_background(const char *path) {
    (void)open_music(path);
}

static int seek_music_milliseconds(unsigned long milliseconds, int resume_after) {
    char command[128];
    if (!g_music_open) return 0;

    /* Wine/Proton (and some native MCI drivers) reject seek while an MPEG alias
       is actively playing. Always stop the transport before moving the position;
       callers decide whether playback should resume afterwards. */
    (void)mci_command("stop gd18_music", NULL, 0, 0);
    (void)mci_command("set gd18_music time format milliseconds", NULL, 0, 0);
    snprintf(command, sizeof(command), "seek gd18_music to %lu", milliseconds);
    if (!mci_command(command, NULL, 0, 1)) return 0;

    if (resume_after) {
        snprintf(command, sizeof(command), "play gd18_music from %lu%s",
                 milliseconds, g_music_loop ? " repeat" : "");
        if (!mci_command(command, NULL, 0, 1)) return 0;
        g_music_paused = 0;
    }
    runtime_log("Audio music seek: target_ms=%lu stop-before-seek=yes resume=%s",
                milliseconds, resume_after ? "yes" : "no");
    return 1;
}

void audio_play_background(const char *path, int loop) {
    char command[96];
    int replay_first_start;
    if (!open_music(path)) return;
    replay_first_start = !loop && g_legacy_first_play_replay &&
                         g_music_needs_first_play_replay;
    g_music_loop = loop != 0;
    if (!seek_music_milliseconds(0u, 0)) return;
    snprintf(command, sizeof(command), "play gd18_music%s",
             loop ? " repeat" : "");
    if (mci_command(command, NULL, 0, 1)) {
        if (replay_first_start) {
            const int replay_ok =
                mci_command("play gd18_music from 0", NULL, 0, 1);
            runtime_log(
                "Audio legacy first-play MCI replay: second-start=%s",
                replay_ok ? "ok" : "failed");
            g_music_needs_first_play_replay = 0;
        }
        g_music_paused = 0;
        runtime_log("Audio music playing: %s (loop=%s)", file_name_part(path),
                    loop ? "yes" : "no");
    }
}

void audio_stop_background(void) {
    if (g_music_open) (void)seek_music_milliseconds(0u, 0);
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
    unsigned long milliseconds;
    if (!g_music_open) return;
    if (seconds < 0.0f) seconds = 0.0f;
    milliseconds = (unsigned long)(seconds * 1000.0f + 0.5f);
    if (seek_music_milliseconds(milliseconds, 1))
        runtime_log("Audio music resumed from %lu ms", milliseconds);
}

void audio_rewind_background(void) {
    int playing;
    int paused;
    if (!g_music_open) return;
    playing = audio_is_background_playing();
    paused = g_music_paused;
    if (seek_music_milliseconds(0u, playing)) {
        if (!playing) g_music_paused = paused;
    }
}

void audio_set_background_time(float seconds) {
    int playing;
    int paused;
    unsigned long milliseconds;
    if (!g_music_open) return;
    if (seconds < 0.0f) seconds = 0.0f;
    milliseconds = (unsigned long)(seconds * 1000.0f + 0.5f);
    playing = audio_is_background_playing();
    paused = g_music_paused;
    if (seek_music_milliseconds(milliseconds, playing)) {
        if (!playing) g_music_paused = paused;
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
    if (g_music_open)
        set_alias_volume("gd18_music", g_music_volume);
}

float audio_get_output_peak(void) {
    union {
        float floating;
        LONG integer;
    } peak_bits;
    peak_bits.integer = InterlockedCompareExchange(&g_output_peak_bits, 0, 0);
    return clamp_volume(peak_bits.floating);
}


static unsigned next_effect_identifier(void) {
    unsigned identifier = (unsigned)InterlockedIncrement(
        &g_next_effect_identifier);
    if (!identifier) {
        identifier = (unsigned)InterlockedIncrement(
            &g_next_effect_identifier);
    }
    return identifier;
}

static int effect_queue_push(const EffectCommand *command) {
    int queued = 0;
    if (!command || !g_effect_command_lock_initialized ||
        !g_effect_command_event || !g_effect_worker_thread) {
        return 0;
    }
    EnterCriticalSection(&g_effect_command_lock);
    if (g_effect_command_count < EFFECT_COMMAND_QUEUE_CAPACITY) {
        g_effect_commands[g_effect_command_write] = *command;
        g_effect_command_write =
            (g_effect_command_write + 1u) % EFFECT_COMMAND_QUEUE_CAPACITY;
        ++g_effect_command_count;
        queued = 1;
    }
    LeaveCriticalSection(&g_effect_command_lock);
    if (queued) {
        SetEvent(g_effect_command_event);
    } else if (InterlockedCompareExchange(
                   &g_effect_queue_overflow_logged, 1, 0) == 0) {
        runtime_log("Audio effect command queue overflow; falling back to synchronous playback");
    }
    return queued;
}

static int effect_queue_pop(EffectCommand *command) {
    int available = 0;
    if (!command || !g_effect_command_lock_initialized) return 0;
    EnterCriticalSection(&g_effect_command_lock);
    if (g_effect_command_count) {
        *command = g_effect_commands[g_effect_command_read];
        g_effect_command_read =
            (g_effect_command_read + 1u) % EFFECT_COMMAND_QUEUE_CAPACITY;
        --g_effect_command_count;
        available = 1;
    }
    LeaveCriticalSection(&g_effect_command_lock);
    return available;
}

static void preload_effect_now(const char *path) {
    char resolved[MAX_PATH * 2];
    if (!audio_asset_path(path, 1, resolved, sizeof(resolved))) return;
    runtime_log("Audio effect prepared in decoded cache: %s",
                file_name_part(path));
}

static unsigned play_effect_now(const char *path, int loop,
                                float pitch, float pan, float gain,
                                unsigned identifier) {
    char resolved[MAX_PATH * 2];
    EffectSlot *slot;
    MMRESULT status;
    char error_text[256] = "unknown waveOut error";
    (void)pitch;
    (void)pan;
    if (!identifier) identifier = next_effect_identifier();
    if (!audio_asset_path(path, 1, resolved, sizeof(resolved))) return 0;
    slot = open_effect_slot(resolved, 1);
    if (!slot) return 0;
    slot->identifier = identifier;
    slot->paused = 0;
    slot->loop = loop != 0;
    slot->volume = clamp_volume(gain);
    slot->header.dwFlags &= WHDR_PREPARED;
    slot->header.dwLoops = loop ? MAXDWORD : 0;
    if (loop)
        slot->header.dwFlags |= WHDR_BEGINLOOP | WHDR_ENDLOOP;
    set_waveout_volume(slot, g_effects_volume * slot->volume);
    status = waveOutWrite(slot->output, &slot->header, sizeof(slot->header));
    if (status != MMSYSERR_NOERROR) {
        waveOutGetErrorTextA(status, error_text, sizeof(error_text));
        runtime_log("Audio waveOut write error %u: %s | %s",
                    (unsigned)status, error_text, resolved);
        park_effect_slot(slot);
        return 0;
    }
    if (InterlockedIncrement(&g_effect_log_count) <= 64) {
        runtime_log("Audio effect playing via waveOut: %s (id=%u, loop=%s)",
                    file_name_part(path), identifier, loop ? "yes" : "no");
    }
    return identifier;
}

static void set_effect_volume_now(unsigned identifier, float volume) {
    EffectSlot *slot = find_effect(identifier);
    if (!slot) return;
    slot->volume = clamp_volume(volume);
    set_waveout_volume(slot, g_effects_volume * slot->volume);
}

static void pause_effect_now(unsigned identifier) {
    EffectSlot *slot = find_effect(identifier);
    if (!slot) return;
    if (waveOutPause(slot->output) == MMSYSERR_NOERROR) slot->paused = 1;
}

static void resume_effect_now(unsigned identifier) {
    EffectSlot *slot = find_effect(identifier);
    if (!slot || !slot->paused) return;
    if (waveOutRestart(slot->output) == MMSYSERR_NOERROR) slot->paused = 0;
}

static void stop_effect_now(unsigned identifier) {
    park_effect_slot(find_effect(identifier));
}

static void pause_all_effects_now(void) {
    unsigned index;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        EffectSlot *slot = &g_effects[index];
        if (!slot->open || !slot->identifier) continue;
        if (waveOutPause(slot->output) == MMSYSERR_NOERROR)
            slot->paused = 1;
    }
}

static void resume_all_effects_now(void) {
    unsigned index;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        EffectSlot *slot = &g_effects[index];
        if (!slot->open || !slot->identifier || !slot->paused) continue;
        if (waveOutRestart(slot->output) == MMSYSERR_NOERROR)
            slot->paused = 0;
    }
}

static void stop_all_effects_now(void) {
    unsigned index;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        if (g_effects[index].open) park_effect_slot(&g_effects[index]);
    }
}

static void unload_effect_now(const char *path) {
    char resolved[MAX_PATH * 2];
    unsigned index;
    if (!audio_asset_path(path, 1, resolved, sizeof(resolved))) return;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        if (g_effects[index].open &&
            _stricmp(g_effects[index].path, resolved) == 0) {
            close_effect_slot(&g_effects[index]);
        }
    }
}

static void apply_master_effect_volume_now(void) {
    unsigned index;
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        if (g_effects[index].open) {
            set_waveout_volume(
                &g_effects[index],
                g_effects_volume * g_effects[index].volume);
        }
    }
}

static void process_effect_command(const EffectCommand *command) {
    if (!command) return;
    AcquireSRWLockExclusive(&g_effect_state_lock);
    switch (command->type) {
    case EFFECT_COMMAND_PRELOAD:
        preload_effect_now(command->path);
        break;
    case EFFECT_COMMAND_PLAY:
        play_effect_now(command->path, command->loop, command->pitch,
                        command->pan, command->volume,
                        command->identifier);
        break;
    case EFFECT_COMMAND_SET_VOLUME:
        set_effect_volume_now(command->identifier, command->volume);
        break;
    case EFFECT_COMMAND_PAUSE:
        pause_effect_now(command->identifier);
        break;
    case EFFECT_COMMAND_RESUME:
        resume_effect_now(command->identifier);
        break;
    case EFFECT_COMMAND_STOP:
        stop_effect_now(command->identifier);
        break;
    case EFFECT_COMMAND_PAUSE_ALL:
        pause_all_effects_now();
        break;
    case EFFECT_COMMAND_RESUME_ALL:
        resume_all_effects_now();
        break;
    case EFFECT_COMMAND_STOP_ALL:
        stop_all_effects_now();
        break;
    case EFFECT_COMMAND_UNLOAD:
        unload_effect_now(command->path);
        break;
    case EFFECT_COMMAND_APPLY_MASTER_VOLUME:
        apply_master_effect_volume_now();
        break;
    default:
        break;
    }
    ReleaseSRWLockExclusive(&g_effect_state_lock);
}

static DWORD WINAPI effect_worker_thread(void *unused) {
    HANDLE waits[2];
    EffectCommand command;
    (void)unused;
    waits[0] = g_effect_worker_stop;
    waits[1] = g_effect_command_event;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    InterlockedExchange(&g_effect_worker_ready, 1);
    runtime_log("RESULT: DYNARMIC_ASYNC_EFFECT_WORKER_READY queue=%u",
                EFFECT_COMMAND_QUEUE_CAPACITY);
    for (;;) {
        DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_OBJECT_0 + 1) break;
        while (effect_queue_pop(&command)) process_effect_command(&command);
    }
    InterlockedExchange(&g_effect_worker_ready, 0);
    return 0;
}

static void initialize_effect_worker(void) {
    if (g_effect_worker_thread) return;
    InitializeCriticalSection(&g_effect_command_lock);
    g_effect_command_lock_initialized = 1;
    g_effect_command_read = 0;
    g_effect_command_write = 0;
    g_effect_command_count = 0;
    g_effect_command_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    g_effect_worker_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!g_effect_command_event || !g_effect_worker_stop) {
        runtime_log("Async effect worker: event creation failed");
        return;
    }
    g_effect_worker_thread = CreateThread(
        NULL, 0, effect_worker_thread, NULL, 0, NULL);
    if (!g_effect_worker_thread) {
        runtime_log("Async effect worker: thread creation failed");
    }
}

static void shutdown_effect_worker(void) {
    if (g_effect_worker_stop) SetEvent(g_effect_worker_stop);
    if (g_effect_worker_thread) {
        WaitForSingleObject(g_effect_worker_thread, INFINITE);
        CloseHandle(g_effect_worker_thread);
        g_effect_worker_thread = NULL;
    }
    if (g_effect_command_event) {
        CloseHandle(g_effect_command_event);
        g_effect_command_event = NULL;
    }
    if (g_effect_worker_stop) {
        CloseHandle(g_effect_worker_stop);
        g_effect_worker_stop = NULL;
    }
    if (g_effect_command_lock_initialized) {
        DeleteCriticalSection(&g_effect_command_lock);
        g_effect_command_lock_initialized = 0;
    }
    g_effect_command_count = 0;
}

void audio_preload_effect(const char *path) {
    EffectCommand command;
    memset(&command, 0, sizeof(command));
    command.type = EFFECT_COMMAND_PRELOAD;
    snprintf(command.path, sizeof(command.path), "%s", path ? path : "");
    if (!effect_queue_push(&command)) {
        AcquireSRWLockExclusive(&g_effect_state_lock);
        preload_effect_now(command.path);
        ReleaseSRWLockExclusive(&g_effect_state_lock);
    }
}

unsigned audio_play_effect_ex(const char *path, int loop, float pitch,
                              float pan, float gain) {
    EffectCommand command;
    memset(&command, 0, sizeof(command));
    command.type = EFFECT_COMMAND_PLAY;
    command.identifier = next_effect_identifier();
    command.loop = loop != 0;
    command.pitch = pitch;
    command.pan = pan;
    command.volume = clamp_volume(gain);
    snprintf(command.path, sizeof(command.path), "%s", path ? path : "");
    if (!effect_queue_push(&command)) {
        unsigned result;
        AcquireSRWLockExclusive(&g_effect_state_lock);
        result = play_effect_now(command.path, command.loop, command.pitch,
                                 command.pan, command.volume,
                                 command.identifier);
        ReleaseSRWLockExclusive(&g_effect_state_lock);
        return result;
    }
    return command.identifier;
}

unsigned audio_play_effect(const char *path, int loop) {
    return audio_play_effect_ex(path, loop, 1.0f, 0.0f, 1.0f);
}

int audio_is_effect_playing(unsigned identifier) {
    EffectSlot *slot;
    int playing = 0;
    AcquireSRWLockExclusive(&g_effect_state_lock);
    slot = find_effect(identifier);
    if (slot) playing = effect_slot_playing(slot);
    ReleaseSRWLockExclusive(&g_effect_state_lock);
    return playing;
}

void audio_set_effect_volume(unsigned identifier, float volume) {
    EffectCommand command;
    memset(&command, 0, sizeof(command));
    command.type = EFFECT_COMMAND_SET_VOLUME;
    command.identifier = identifier;
    command.volume = clamp_volume(volume);
    if (!effect_queue_push(&command)) {
        AcquireSRWLockExclusive(&g_effect_state_lock);
        set_effect_volume_now(identifier, command.volume);
        ReleaseSRWLockExclusive(&g_effect_state_lock);
    }
}

static void enqueue_simple_effect_command(EffectCommandType type,
                                          unsigned identifier) {
    EffectCommand command;
    memset(&command, 0, sizeof(command));
    command.type = type;
    command.identifier = identifier;
    if (!effect_queue_push(&command)) process_effect_command(&command);
}

void audio_pause_effect(unsigned identifier) {
    enqueue_simple_effect_command(EFFECT_COMMAND_PAUSE, identifier);
}

void audio_resume_effect(unsigned identifier) {
    enqueue_simple_effect_command(EFFECT_COMMAND_RESUME, identifier);
}

void audio_stop_effect(unsigned identifier) {
    enqueue_simple_effect_command(EFFECT_COMMAND_STOP, identifier);
}

void audio_pause_all_effects(void) {
    enqueue_simple_effect_command(EFFECT_COMMAND_PAUSE_ALL, 0);
}

void audio_resume_all_effects(void) {
    enqueue_simple_effect_command(EFFECT_COMMAND_RESUME_ALL, 0);
}

void audio_stop_all_effects(void) {
    enqueue_simple_effect_command(EFFECT_COMMAND_STOP_ALL, 0);
}

void audio_unload_effect(const char *path) {
    EffectCommand command;
    memset(&command, 0, sizeof(command));
    command.type = EFFECT_COMMAND_UNLOAD;
    snprintf(command.path, sizeof(command.path), "%s", path ? path : "");
    if (!effect_queue_push(&command)) process_effect_command(&command);
}

float audio_get_effects_volume(void) {
    float volume;
    AcquireSRWLockShared(&g_effect_state_lock);
    volume = g_effects_volume;
    ReleaseSRWLockShared(&g_effect_state_lock);
    return volume;
}

void audio_set_effects_volume(float volume) {
    EffectCommand command;
    AcquireSRWLockExclusive(&g_effect_state_lock);
    g_effects_volume = clamp_volume(volume);
    ReleaseSRWLockExclusive(&g_effect_state_lock);
    memset(&command, 0, sizeof(command));
    command.type = EFFECT_COMMAND_APPLY_MASTER_VOLUME;
    if (!effect_queue_push(&command)) process_effect_command(&command);
}
