#ifndef GD_RUNTIME_SETTINGS_H
#define GD_RUNTIME_SETTINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boolean environment settings accept true/false, yes/no, on/off, and 1/0. */
int gd_setting_bool(const char *name, int default_value);
int gd_settings_hack_icons(void);
int gd_settings_full_bypass(void);

/* Returns the configured GDPS base, or the official default. */
const char *gd_settings_server(void);

/*
 * Rewrites a Geometry Dash PHP API URL onto GDPS_SERVER while preserving the
 * endpoint filename and query. Returns 1 when output differs, 0 when the URL
 * is not a game API URL, and -1 when the output buffer is too small/invalid.
 */
int gd_settings_rewrite_url(const char *input, char *output, size_t capacity);

/*
 * Returns the DNS host that should be resolved for a known Geometry Dash API
 * host. Returns 1 when overridden, 0 when the original host should be used.
 */
int gd_settings_override_dns_host(const char *input, char *output,
                                  size_t capacity);

/*
 * Rewrites a plaintext HTTP request line and Host header. The returned buffer
 * is allocated with malloc and must be freed by the caller. Returns 1 when a
 * replacement was made, 0 when no rewrite is needed, and -1 on failure.
 */
int gd_settings_rewrite_http_request(const void *input, size_t input_size,
                                     void **output, size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif
