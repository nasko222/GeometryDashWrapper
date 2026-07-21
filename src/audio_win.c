#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_win.h"
#include "runtime.h"

#define MAX_EFFECT_SLOTS 24

typedef struct {
    unsigned identifier;
    int open;
    int paused;
    char alias[32];
} EffectSlot;

static char g_audio_directory[MAX_PATH * 2];
static char g_music_path[MAX_PATH * 2];
static EffectSlot g_effects[MAX_EFFECT_SLOTS];
static unsigned g_next_effect_identifier = 1;
static unsigned g_next_effect_slot;
static float g_music_volume = 1.0f;
static float g_effects_volume = 1.0f;
static int g_music_open;
static int g_music_paused;
static int g_music_loop;

static float clamp_volume(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
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
    for (index = 0; index < MAX_EFFECT_SLOTS; ++index) {
        snprintf(g_effects[index].alias, sizeof(g_effects[index].alias),
                 "gd18_fx_%u", index);
    }
    runtime_log("Windows MCI audio bridge initialized: %s", g_audio_directory);
}

void audio_shutdown(void) {
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
    set_alias_volume(slot->alias, g_effects_volume);
    snprintf(command, sizeof(command), "play %s from 0%s", slot->alias,
             loop ? " repeat" : "");
    if (!mci_command(command, NULL, 0, 1)) {
        close_effect_slot(slot);
        return 0;
    }
    return identifier;
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
            set_alias_volume(g_effects[index].alias, g_effects_volume);
        }
    }
}
