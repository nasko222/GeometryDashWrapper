#ifndef GD18_AUDIO_WIN_H
#define GD18_AUDIO_WIN_H

void audio_initialize(const char *executable_directory);
void audio_set_apk_path(const char *apk_path);
void audio_set_writable_directory(const char *writable_directory);
void audio_set_legacy_first_play_prime(int enabled);
void audio_shutdown(void);

void audio_preload_background(const char *path);
void audio_play_background(const char *path, int loop);
void audio_stop_background(void);
void audio_pause_background(void);
void audio_resume_background(void);
void audio_resume_background_from(float seconds);
void audio_rewind_background(void);
void audio_set_background_time(float seconds);
float audio_get_background_time(void);
int audio_is_background_playing(void);
float audio_get_background_volume(void);
void audio_set_background_volume(float volume);
float audio_get_output_peak(void);

void audio_preload_effect(const char *path);
unsigned audio_play_effect(const char *path, int loop);
unsigned audio_play_effect_ex(const char *path, int loop, float pitch,
                              float pan, float gain);
int audio_is_effect_playing(unsigned identifier);
void audio_set_effect_volume(unsigned identifier, float volume);
void audio_pause_effect(unsigned identifier);
void audio_resume_effect(unsigned identifier);
void audio_stop_effect(unsigned identifier);
void audio_pause_all_effects(void);
void audio_resume_all_effects(void);
void audio_stop_all_effects(void);
void audio_unload_effect(const char *path);
float audio_get_effects_volume(void);
void audio_set_effects_volume(float volume);

#endif
