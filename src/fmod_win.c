#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_win.h"
#include "fmod_win.h"
#include "runtime.h"

#define FMOD_OK 0
#define FMOD_ERR_INVALID_PARAM 31
#define FMOD_VERSION_1_05_04 0x00010504u
#define FMOD_LOOP_NORMAL 0x00000002u
#define FMOD_TIMEUNIT_RAWBYTES 0x00000008u
#define FMOD_SPEAKERMODE_STEREO 3

#define SYSTEM_MAGIC 0x53595346u
#define SOUND_MAGIC 0x444e5346u
#define CHANNEL_MAGIC 0x4e484346u
#define DSP_MAGIC 0x50534446u

#define EFFECT_SOUND_SLOTS 64
#define EFFECT_CHANNEL_SLOTS 64
#define AUDIO_PATH_CAPACITY 1024

typedef int (*FmodChannelCallback)(void *channel_control,
                                   int channel_control_type,
                                   int callback_type,
                                   void *command_data_1,
                                   void *command_data_2);

typedef struct {
    uint32_t magic;
    int initialized;
    int suspended;
    unsigned stream_buffer_size;
    unsigned stream_buffer_time_unit;
    int output_type;
    int software_rate;
    int software_speaker_mode;
    int software_raw_speakers;
} FakeFmodSystem;

typedef struct {
    uint32_t magic;
    unsigned mode;
    int background;
    char path[AUDIO_PATH_CAPACITY];
} FakeFmodSound;

typedef struct {
    uint32_t magic;
    unsigned mode;
    unsigned effect_identifier;
    unsigned position_ms;
    int background;
    int loop;
    int paused;
    int stopped;
    int started;
    int mixer_paused;
    int observed_playing;
    int callback_sent;
    DWORD last_status_poll;
    float volume;
    FmodChannelCallback callback;
    char path[AUDIO_PATH_CAPACITY];
} FakeFmodChannel;

typedef struct {
    uint32_t magic;
    int input_metering_enabled;
    int output_metering_enabled;
} FakeFmodDsp;

typedef struct {
    int numsamples;
    float peaklevel[32];
    float rmslevel[32];
    int16_t numchannels;
    int16_t padding;
} FakeFmodMeteringInfo;

static FakeFmodSystem g_system;
static FakeFmodDsp g_dsp;
static FakeFmodSound g_background_sound;
static FakeFmodSound g_effect_sounds[EFFECT_SOUND_SLOTS];
static FakeFmodChannel g_background_channel;
static FakeFmodChannel g_effect_channels[EFFECT_CHANNEL_SLOTS];
static unsigned g_next_effect_sound;
static unsigned g_next_effect_channel;

static void copy_path(char *destination, size_t capacity, const char *source) {
    if (!capacity) return;
    if (!source) source = "";
    snprintf(destination, capacity, "%s", source);
}

static int ascii_equal_ignore_case(char left, char right) {
    if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
    if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
    return left == right;
}

static int path_has_extension(const char *path, const char *extension) {
    size_t path_length;
    size_t extension_length;
    size_t index;
    if (!path || !extension) return 0;
    path_length = strlen(path);
    extension_length = strlen(extension);
    if (path_length < extension_length) return 0;
    path += path_length - extension_length;
    for (index = 0; index < extension_length; ++index) {
        if (!ascii_equal_ignore_case(path[index], extension[index])) return 0;
    }
    return 1;
}

static int sound_is_background(const char *path, int stream) {
    return stream || path_has_extension(path, ".mp3");
}

static FakeFmodSound *allocate_sound(int background) {
    FakeFmodSound *sound;
    unsigned index;
    if (background) {
        sound = &g_background_sound;
    } else {
        sound = NULL;
        for (index = 0; index < EFFECT_SOUND_SLOTS; ++index) {
            if (g_effect_sounds[index].magic != SOUND_MAGIC) {
                sound = &g_effect_sounds[index];
                break;
            }
        }
        if (!sound) {
            sound = &g_effect_sounds[
                g_next_effect_sound++ % EFFECT_SOUND_SLOTS];
        }
    }
    memset(sound, 0, sizeof(*sound));
    sound->magic = SOUND_MAGIC;
    sound->background = background;
    return sound;
}

static FakeFmodChannel *allocate_channel(int background) {
    FakeFmodChannel *channel;
    if (background) {
        channel = &g_background_channel;
        if (channel->magic == CHANNEL_MAGIC && !channel->stopped) {
            audio_stop_background();
        }
    } else {
        channel = &g_effect_channels[
            g_next_effect_channel++ % EFFECT_CHANNEL_SLOTS];
        if (channel->magic == CHANNEL_MAGIC && !channel->stopped &&
            channel->effect_identifier) {
            audio_stop_effect(channel->effect_identifier);
        }
    }
    memset(channel, 0, sizeof(*channel));
    channel->magic = CHANNEL_MAGIC;
    channel->volume = 1.0f;
    channel->background = background;
    return channel;
}

static FakeFmodSound *checked_sound(void *opaque) {
    FakeFmodSound *sound = (FakeFmodSound *)opaque;
    return sound && sound->magic == SOUND_MAGIC ? sound : NULL;
}

static FakeFmodChannel *checked_channel(void *opaque) {
    FakeFmodChannel *channel = (FakeFmodChannel *)opaque;
    return channel && channel->magic == CHANNEL_MAGIC ? channel : NULL;
}

static int channel_backend_playing(FakeFmodChannel *channel) {
    if (!channel || channel->stopped) return 0;
    if (!channel->started) return channel->paused != 0;
    if (channel->paused) return 1;
    if (channel->background) return audio_is_background_playing();
    return channel->effect_identifier &&
           audio_is_effect_playing(channel->effect_identifier);
}

static void start_background_channel(FakeFmodChannel *channel) {
    if (!channel || channel->stopped || channel->started) return;
    audio_play_background(channel->path, channel->loop);
    channel->started = 1;
    if (channel->position_ms) {
        audio_set_background_time((float)channel->position_ms / 1000.0f);
    }
    runtime_log("FMOD bridge: released deferred music channel at %u ms",
                channel->position_ms);
}

static void set_channel_paused(FakeFmodChannel *channel, int paused) {
    if (!channel || channel->stopped) return;
    if (channel->background && !channel->started) {
        channel->paused = paused;
        if (!paused) start_background_channel(channel);
        return;
    }
    if (channel->paused == paused) return;
    if (channel->background) {
        if (paused) {
            float seconds = audio_get_background_time();
            if (seconds >= 0.0f) {
                channel->position_ms =
                    (unsigned)(seconds * 1000.0f + 0.5f);
            }
            audio_pause_background();
        } else {
            /* MCI's resume command is unreliable after an FMOD-style pause or
               seek. Re-seek and issue a fresh play from the exact pause point. */
            audio_resume_background_from(
                (float)channel->position_ms / 1000.0f);
        }
    } else if (channel->effect_identifier) {
        if (paused) audio_pause_effect(channel->effect_identifier);
        else audio_resume_effect(channel->effect_identifier);
    }
    channel->paused = paused;
}

static int fake_system_create(void **output) {
    if (!output) return FMOD_ERR_INVALID_PARAM;
    memset(&g_system, 0, sizeof(g_system));
    memset(&g_dsp, 0, sizeof(g_dsp));
    memset(&g_background_sound, 0, sizeof(g_background_sound));
    memset(g_effect_sounds, 0, sizeof(g_effect_sounds));
    memset(&g_background_channel, 0, sizeof(g_background_channel));
    memset(g_effect_channels, 0, sizeof(g_effect_channels));
    g_next_effect_sound = 0;
    g_next_effect_channel = 0;
    g_system.magic = SYSTEM_MAGIC;
    g_system.stream_buffer_size = 16384u;
    g_system.stream_buffer_time_unit = FMOD_TIMEUNIT_RAWBYTES;
    g_system.software_rate = 44100;
    g_system.software_speaker_mode = FMOD_SPEAKERMODE_STEREO;
    g_dsp.magic = DSP_MAGIC;
    *output = &g_system;
    runtime_log("FMOD 1.05.04 compatibility bridge initialized");
    return FMOD_OK;
}

static int fake_system_get_version(void *opaque, unsigned *version) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    if (!system || system->magic != SYSTEM_MAGIC || !version) {
        return FMOD_ERR_INVALID_PARAM;
    }
    *version = FMOD_VERSION_1_05_04;
    return FMOD_OK;
}

static int fake_system_get_stream_buffer_size(void *opaque, unsigned *size,
                                               unsigned *time_unit) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    if (!system || system->magic != SYSTEM_MAGIC || !size || !time_unit) {
        return FMOD_ERR_INVALID_PARAM;
    }
    *size = system->stream_buffer_size;
    *time_unit = system->stream_buffer_time_unit;
    runtime_log("FMOD bridge: stream buffer queried (%u, unit=0x%08x)",
                *size, *time_unit);
    return FMOD_OK;
}

static int fake_system_set_stream_buffer_size(void *opaque, unsigned size,
                                               unsigned time_unit) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    if (!system || system->magic != SYSTEM_MAGIC || !size) {
        return FMOD_ERR_INVALID_PARAM;
    }
    system->stream_buffer_size = size;
    system->stream_buffer_time_unit = time_unit;
    runtime_log("FMOD bridge: stream buffer configured (%u, unit=0x%08x)",
                size, time_unit);
    return FMOD_OK;
}

static int fake_system_set_output(void *opaque, int output_type) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    if (!system || system->magic != SYSTEM_MAGIC) {
        return FMOD_ERR_INVALID_PARAM;
    }
    system->output_type = output_type;
    runtime_log("FMOD bridge: accepted output type %d for Windows backend",
                output_type);
    return FMOD_OK;
}

static int fake_system_get_software_format(void *opaque, int *sample_rate,
                                           int *speaker_mode,
                                           int *raw_speakers) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    if (!system || system->magic != SYSTEM_MAGIC || !sample_rate ||
        !speaker_mode || !raw_speakers) {
        return FMOD_ERR_INVALID_PARAM;
    }
    *sample_rate = system->software_rate;
    *speaker_mode = system->software_speaker_mode;
    *raw_speakers = system->software_raw_speakers;
    runtime_log("FMOD bridge: software format queried (%d Hz, mode=%d, raw=%d)",
                *sample_rate, *speaker_mode, *raw_speakers);
    return FMOD_OK;
}

static int fake_system_set_software_format(void *opaque, int sample_rate,
                                           int speaker_mode,
                                           int raw_speakers) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    if (!system || system->magic != SYSTEM_MAGIC || sample_rate <= 0) {
        return FMOD_ERR_INVALID_PARAM;
    }
    system->software_rate = sample_rate;
    system->software_speaker_mode = speaker_mode;
    system->software_raw_speakers = raw_speakers;
    runtime_log("FMOD bridge: software format configured (%d Hz, mode=%d, raw=%d)",
                sample_rate, speaker_mode, raw_speakers);
    return FMOD_OK;
}

static int fake_system_init(void *opaque, int maximum_channels,
                            unsigned flags, void *driver_data) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    (void)maximum_channels;
    (void)flags;
    (void)driver_data;
    if (!system || system->magic != SYSTEM_MAGIC) {
        return FMOD_ERR_INVALID_PARAM;
    }
    system->initialized = 1;
    return FMOD_OK;
}

static int fake_system_update(void *opaque) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    FakeFmodChannel *channel = &g_background_channel;
    DWORD now;
    int playing;
    if (!system || system->magic != SYSTEM_MAGIC) {
        return FMOD_ERR_INVALID_PARAM;
    }
    if (channel->magic != CHANNEL_MAGIC || channel->stopped ||
        !channel->started ||
        channel->paused || channel->loop) {
        return FMOD_OK;
    }
    now = GetTickCount();
    if ((DWORD)(now - channel->last_status_poll) < 100u) return FMOD_OK;
    channel->last_status_poll = now;
    playing = audio_is_background_playing();
    if (playing) {
        channel->observed_playing = 1;
    } else if (channel->observed_playing) {
        channel->stopped = 1;
        if (channel->callback && !channel->callback_sent) {
            channel->callback_sent = 1;
            channel->callback(channel, 0, 0, NULL, NULL);
        }
    }
    return FMOD_OK;
}

static int fake_system_mixer_suspend(void *opaque) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    unsigned index;
    if (!system || system->magic != SYSTEM_MAGIC) {
        return FMOD_ERR_INVALID_PARAM;
    }
    if (system->suspended) return FMOD_OK;
    system->suspended = 1;
    if (g_background_channel.magic == CHANNEL_MAGIC &&
        !g_background_channel.stopped && !g_background_channel.paused) {
        set_channel_paused(&g_background_channel, 1);
        g_background_channel.mixer_paused = 1;
    }
    for (index = 0; index < EFFECT_CHANNEL_SLOTS; ++index) {
        FakeFmodChannel *channel = &g_effect_channels[index];
        if (channel->magic == CHANNEL_MAGIC && !channel->stopped &&
            !channel->paused) {
            set_channel_paused(channel, 1);
            channel->mixer_paused = 1;
        }
    }
    return FMOD_OK;
}

static int fake_system_mixer_resume(void *opaque) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    unsigned index;
    if (!system || system->magic != SYSTEM_MAGIC) {
        return FMOD_ERR_INVALID_PARAM;
    }
    system->suspended = 0;
    if (g_background_channel.magic == CHANNEL_MAGIC &&
        g_background_channel.mixer_paused) {
        g_background_channel.mixer_paused = 0;
        set_channel_paused(&g_background_channel, 0);
    }
    for (index = 0; index < EFFECT_CHANNEL_SLOTS; ++index) {
        FakeFmodChannel *channel = &g_effect_channels[index];
        if (channel->magic == CHANNEL_MAGIC && channel->mixer_paused) {
            channel->mixer_paused = 0;
            set_channel_paused(channel, 0);
        }
    }
    return FMOD_OK;
}

static int fake_system_close(void *opaque) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    if (!system || system->magic != SYSTEM_MAGIC) {
        return FMOD_ERR_INVALID_PARAM;
    }
    audio_stop_background();
    audio_stop_all_effects();
    system->initialized = 0;
    return FMOD_OK;
}

static int fake_system_release(void *opaque) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    if (!system || system->magic != SYSTEM_MAGIC) {
        return FMOD_ERR_INVALID_PARAM;
    }
    system->initialized = 0;
    return FMOD_OK;
}

static int create_sound_common(void *opaque, const char *path, unsigned mode,
                               void *create_info, void **output, int stream) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    FakeFmodSound *sound;
    int background;
    (void)create_info;
    if (output) *output = NULL;
    if (!system || system->magic != SYSTEM_MAGIC || !path || !path[0] ||
        !output) {
        return FMOD_ERR_INVALID_PARAM;
    }
    background = sound_is_background(path, stream);
    sound = allocate_sound(background);
    sound->mode = mode;
    copy_path(sound->path, sizeof(sound->path), path);
    if (background) audio_preload_background(path);
    else audio_preload_effect(path);
    *output = sound;
    return FMOD_OK;
}

static int fake_system_create_sound(void *opaque, const char *path,
                                    unsigned mode, void *create_info,
                                    void **output) {
    return create_sound_common(opaque, path, mode, create_info, output, 0);
}

static int fake_system_create_stream(void *opaque, const char *path,
                                     unsigned mode, void *create_info,
                                     void **output) {
    return create_sound_common(opaque, path, mode, create_info, output, 1);
}

static int fake_system_play_sound(void *opaque, void *opaque_sound,
                                  void *channel_group, int paused,
                                  void **output) {
    FakeFmodSystem *system = (FakeFmodSystem *)opaque;
    FakeFmodSound *sound = checked_sound(opaque_sound);
    FakeFmodChannel *channel;
    (void)channel_group;
    if (output) *output = NULL;
    if (!system || system->magic != SYSTEM_MAGIC || !sound || !output) {
        return FMOD_ERR_INVALID_PARAM;
    }
    channel = allocate_channel(sound->background);
    channel->mode = sound->mode;
    channel->loop = (sound->mode & FMOD_LOOP_NORMAL) != 0;
    copy_path(channel->path, sizeof(channel->path), sound->path);
    if (channel->background) {
        channel->volume = audio_get_background_volume();
        if (paused) {
            channel->paused = 1;
            runtime_log("FMOD bridge: level music armed in paused state");
        } else {
            start_background_channel(channel);
        }
    } else {
        channel->volume = audio_get_effects_volume();
        channel->effect_identifier =
            audio_play_effect(channel->path, channel->loop);
        channel->started = channel->effect_identifier != 0;
        if (paused) set_channel_paused(channel, 1);
    }
    *output = channel;
    return FMOD_OK;
}

static int fake_sound_release(void *opaque) {
    FakeFmodSound *sound = checked_sound(opaque);
    if (!sound) return FMOD_OK;
    sound->magic = 0;
    return FMOD_OK;
}

static int fake_channel_stop(void *opaque) {
    FakeFmodChannel *channel = checked_channel(opaque);
    if (!channel) return FMOD_OK;
    if (channel->background && channel->started) audio_stop_background();
    else if (channel->effect_identifier) {
        audio_stop_effect(channel->effect_identifier);
    }
    channel->stopped = 1;
    channel->paused = 0;
    channel->mixer_paused = 0;
    return FMOD_OK;
}

static int fake_channel_set_paused(void *opaque, int paused) {
    FakeFmodChannel *channel = checked_channel(opaque);
    if (!channel) return FMOD_OK;
    set_channel_paused(channel, paused != 0);
    return FMOD_OK;
}

static int fake_channel_get_paused(void *opaque, unsigned char *paused) {
    FakeFmodChannel *channel = checked_channel(opaque);
    if (!paused) return FMOD_ERR_INVALID_PARAM;
    *paused = channel ? (unsigned char)(channel->paused != 0) : 0;
    return FMOD_OK;
}

static int fake_channel_set_volume(void *opaque, float volume) {
    FakeFmodChannel *channel = checked_channel(opaque);
    if (!channel) return FMOD_OK;
    channel->volume = volume;
    if (channel->background) audio_set_background_volume(volume);
    else audio_set_effects_volume(volume);
    return FMOD_OK;
}

static int fake_channel_get_volume(void *opaque, float *volume) {
    FakeFmodChannel *channel = checked_channel(opaque);
    if (!volume) return FMOD_ERR_INVALID_PARAM;
    *volume = channel ? channel->volume : 0.0f;
    return FMOD_OK;
}

static int fake_channel_is_playing(void *opaque, unsigned char *playing) {
    FakeFmodChannel *channel = checked_channel(opaque);
    if (!playing) return FMOD_ERR_INVALID_PARAM;
    *playing = channel ? (unsigned char)(channel_backend_playing(channel) != 0)
                       : 0;
    return FMOD_OK;
}

static int fake_channel_set_mode(void *opaque, unsigned mode) {
    FakeFmodChannel *channel = checked_channel(opaque);
    int old_loop;
    if (!channel) return FMOD_OK;
    old_loop = channel->loop;
    channel->mode = mode;
    channel->loop = (mode & FMOD_LOOP_NORMAL) != 0;
    if (channel->background && channel->started && !channel->stopped &&
        old_loop != channel->loop) {
        float position = audio_get_background_time();
        audio_play_background(channel->path, channel->loop);
        if (position > 0.0f) audio_set_background_time(position);
        if (channel->paused) audio_pause_background();
    }
    return FMOD_OK;
}

static int fake_channel_get_dsp(void *opaque, int index, void **output) {
    FakeFmodChannel *channel = checked_channel(opaque);
    (void)index;
    if (!output) return FMOD_ERR_INVALID_PARAM;
    *output = channel ? &g_dsp : NULL;
    return channel ? FMOD_OK : FMOD_ERR_INVALID_PARAM;
}

static int fake_channel_set_callback(void *opaque,
                                     FmodChannelCallback callback) {
    FakeFmodChannel *channel = checked_channel(opaque);
    if (!channel) return FMOD_OK;
    channel->callback = callback;
    return FMOD_OK;
}

static int fake_channel_get_position(void *opaque, unsigned *position,
                                     unsigned time_unit) {
    FakeFmodChannel *channel = checked_channel(opaque);
    float seconds;
    (void)time_unit;
    if (!position) return FMOD_ERR_INVALID_PARAM;
    *position = 0;
    if (!channel || !channel->background) return FMOD_OK;
    if (!channel->started) {
        *position = channel->position_ms;
        return FMOD_OK;
    }
    seconds = audio_get_background_time();
    if (seconds > 0.0f) {
        channel->position_ms = (unsigned)(seconds * 1000.0f + 0.5f);
    }
    *position = channel->position_ms;
    return FMOD_OK;
}

static int fake_channel_set_position(void *opaque, unsigned position,
                                     unsigned time_unit) {
    FakeFmodChannel *channel = checked_channel(opaque);
    (void)time_unit;
    if (channel && channel->background) {
        channel->position_ms = position;
        if (channel->started) {
            audio_set_background_time((float)position / 1000.0f);
        }
    }
    return FMOD_OK;
}

static int fake_dsp_set_metering_enabled(void *opaque, int input_enabled,
                                         int output_enabled) {
    FakeFmodDsp *dsp = (FakeFmodDsp *)opaque;
    if (!dsp || dsp->magic != DSP_MAGIC) return FMOD_ERR_INVALID_PARAM;
    dsp->input_metering_enabled = input_enabled != 0;
    dsp->output_metering_enabled = output_enabled != 0;
    return FMOD_OK;
}

static void fill_metering_info(FakeFmodMeteringInfo *metering, float peak) {
    if (!metering) return;
    memset(metering, 0, sizeof(*metering));
    metering->numsamples = peak > 0.0f ? 1 : 0;
    metering->peaklevel[0] = peak;
    metering->peaklevel[1] = peak;
    metering->rmslevel[0] = peak * 0.70710678f;
    metering->rmslevel[1] = peak * 0.70710678f;
    metering->numchannels = 2;
}

static int fake_dsp_get_metering_info(void *opaque, void *input_info,
                                      void *output_info) {
    FakeFmodDsp *dsp = (FakeFmodDsp *)opaque;
    FakeFmodMeteringInfo *input_metering =
        (FakeFmodMeteringInfo *)input_info;
    FakeFmodMeteringInfo *output_metering =
        (FakeFmodMeteringInfo *)output_info;
    float peak = 0.0f;
    if (!dsp || dsp->magic != DSP_MAGIC ||
        (!input_metering && !output_metering)) {
        return FMOD_ERR_INVALID_PARAM;
    }
    if (((input_metering && dsp->input_metering_enabled) ||
         (output_metering && dsp->output_metering_enabled)) &&
        g_background_channel.magic == CHANNEL_MAGIC &&
        g_background_channel.started && !g_background_channel.paused &&
        !g_background_channel.stopped) {
        peak = audio_get_output_peak();
    }
    fill_metering_info(input_metering,
                       dsp->input_metering_enabled ? peak : 0.0f);
    fill_metering_info(output_metering,
                       dsp->output_metering_enabled ? peak : 0.0f);
    return FMOD_OK;
}

typedef struct {
    const char *name;
    void *function;
} FmodFunction;

static const FmodFunction functions[] = {
    {"FMOD_System_Create", (void *)fake_system_create},
    {"_ZN4FMOD14ChannelControl11setCallbackEPF11FMOD_RESULTP19FMOD_CHANNELCONTROL24FMOD_CHANNELCONTROL_TYPE33FMOD_CHANNELCONTROL_CALLBACK_TYPEPvS6_E", (void *)fake_channel_set_callback},
    {"_ZN4FMOD14ChannelControl4stopEv", (void *)fake_channel_stop},
    {"_ZN4FMOD14ChannelControl6getDSPEiPPNS_3DSPE", (void *)fake_channel_get_dsp},
    {"_ZN4FMOD14ChannelControl7setModeEj", (void *)fake_channel_set_mode},
    {"_ZN4FMOD14ChannelControl9getPausedEPb", (void *)fake_channel_get_paused},
    {"_ZN4FMOD14ChannelControl9getVolumeEPf", (void *)fake_channel_get_volume},
    {"_ZN4FMOD14ChannelControl9isPlayingEPb", (void *)fake_channel_is_playing},
    {"_ZN4FMOD14ChannelControl9setPausedEb", (void *)fake_channel_set_paused},
    {"_ZN4FMOD14ChannelControl9setVolumeEf", (void *)fake_channel_set_volume},
    {"_ZN4FMOD3DSP15getMeteringInfoEP22FMOD_DSP_METERING_INFOS2_", (void *)fake_dsp_get_metering_info},
    {"_ZN4FMOD3DSP18setMeteringEnabledEbb", (void *)fake_dsp_set_metering_enabled},
    {"_ZN4FMOD5Sound7releaseEv", (void *)fake_sound_release},
    {"_ZN4FMOD6System10getVersionEPj", (void *)fake_system_get_version},
    {"_ZN4FMOD6System17getSoftwareFormatEPiP16FMOD_SPEAKERMODES1_", (void *)fake_system_get_software_format},
    {"_ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi", (void *)fake_system_set_software_format},
    {"_ZN4FMOD6System19getStreamBufferSizeEPjS1_", (void *)fake_system_get_stream_buffer_size},
    {"_ZN4FMOD6System19setStreamBufferSizeEjj", (void *)fake_system_set_stream_buffer_size},
    {"_ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE", (void *)fake_system_create_sound},
    {"_ZN4FMOD6System11mixerResumeEv", (void *)fake_system_mixer_resume},
    {"_ZN4FMOD6System12createStreamEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE", (void *)fake_system_create_stream},
    {"_ZN4FMOD6System12mixerSuspendEv", (void *)fake_system_mixer_suspend},
    {"_ZN4FMOD6System4initEijPv", (void *)fake_system_init},
    {"_ZN4FMOD6System5closeEv", (void *)fake_system_close},
    {"_ZN4FMOD6System6updateEv", (void *)fake_system_update},
    {"_ZN4FMOD6System7releaseEv", (void *)fake_system_release},
    {"_ZN4FMOD6System9setOutputE15FMOD_OUTPUTTYPE", (void *)fake_system_set_output},
    {"_ZN4FMOD6System9playSoundEPNS_5SoundEPNS_12ChannelGroupEbPPNS_7ChannelE", (void *)fake_system_play_sound},
    {"_ZN4FMOD7Channel11getPositionEPjj", (void *)fake_channel_get_position},
    {"_ZN4FMOD7Channel11setPositionEjj", (void *)fake_channel_set_position},
};

void *fmod_win_resolve(const char *name) {
    size_t index;
    if (!name) return NULL;
    for (index = 0; index < sizeof(functions) / sizeof(functions[0]); ++index) {
        if (strcmp(name, functions[index].name) == 0) {
            return functions[index].function;
        }
    }
    return NULL;
}
