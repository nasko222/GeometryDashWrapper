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

/*
 * Handles complete plaintext Geometry Dash PHP API requests through WinHTTP.
 * This lets legacy ARM games reach current HTTPS Boomlings and configured
 * GDPS endpoints even though their bundled libcurl opens a plaintext socket.
 * Returns 1 with a malloc-owned raw HTTP response, 0 for unrelated traffic,
 * 2 when a recognized request is incomplete, and -1 on transport failure.
 */
int gd_api_http_handle_raw_request(const void *request, size_t request_size,
                                   unsigned char **response,
                                   size_t *response_size,
                                   int *response_code);

/*
 * Parses/classifies the same legacy PHP request without performing network I/O.
 * Returns 1 for a complete recognized request, 2 for a recognized request that
 * is still incomplete, and 0 for unrelated traffic.  The ARM legacy backend
 * uses this under Wine so slow WinHTTP compatibility never blocks a Cocos
 * touch callback; the complete request then follows the original guest socket.
 */
int gd_api_http_classify_raw_request(const void *request, size_t request_size);

#ifdef __cplusplus
}
#endif

#endif
