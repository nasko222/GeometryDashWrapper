#ifndef GD_SONG_HTTP_WIN_H
#define GD_SONG_HTTP_WIN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Detects a complete plaintext HTTP getGJSongInfo.php request. A configured
 * GDPS song endpoint is tried first, then official HTTPS Boomlings is used when
 * the custom response is missing, -1, HTML, or a PHP proxy error. On success,
 * returns 1 and provides a raw
 * HTTP/1.1 response buffer allocated with malloc. Returns 0 for non-song
 * requests and -1 for a recognized song request that could not be completed.
 */
int gd_song_http_handle_raw_request(const void *request, size_t request_size,
                                    unsigned char **response,
                                    size_t *response_size,
                                    int *response_code);

#ifdef __cplusplus
}
#endif

#endif
