#ifndef GD_STORAGE_WIN_H
#define GD_STORAGE_WIN_H

#include <stddef.h>
#include <stdint.h>

void storage_initialize(const char *writable_directory);
void storage_shutdown(void);

/* Recognize RobTop's Cocos save-file families, including numbered variants
   such as CCGameManager2.dat and optional .bak files. */
int storage_is_game_file_name(const char *name);

char *storage_get_string_copy(const char *key, const char *default_value);
int storage_get_bool(const char *key, int default_value);
int32_t storage_get_integer(const char *key, int32_t default_value);
float storage_get_float(const char *key, float default_value);
double storage_get_double(const char *key, double default_value);

void storage_set_string(const char *key, const char *value);
void storage_set_bool(const char *key, int value);
void storage_set_integer(const char *key, int32_t value);
void storage_set_float(const char *key, float value);
void storage_set_double(const char *key, double value);

int storage_write_game_file(const char *path, const void *data, size_t size);
char *storage_read_game_file(const char *path, size_t *size);
int storage_file_exists(const char *path);

#endif
