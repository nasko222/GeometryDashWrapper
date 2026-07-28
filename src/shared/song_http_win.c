#include "song_http_win.h"
#include "runtime_settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

static int ascii_mem_contains_ci(const char *text, size_t text_size,
                                 const char *needle) {
    size_t needle_size;
    size_t offset;
    if (!text || !needle || !*needle) return 0;
    needle_size = strlen(needle);
    if (needle_size > text_size) return 0;
    for (offset = 0; offset + needle_size <= text_size; ++offset) {
        size_t index;
        for (index = 0; index < needle_size; ++index) {
            if (tolower((unsigned char)text[offset + index]) !=
                tolower((unsigned char)needle[index])) {
                break;
            }
        }
        if (index == needle_size) return 1;
    }
    return 0;
}

#ifdef _WIN32
static int ascii_starts_ci(const char *text, const char *prefix) {
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (!*text ||
            tolower((unsigned char)*text) !=
                tolower((unsigned char)*prefix)) {
            return 0;
        }
        ++text;
        ++prefix;
    }
    return 1;
}

static int ascii_equal_ci(const char *left, const char *right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (tolower((unsigned char)*left) !=
            tolower((unsigned char)*right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}
#endif

static const char *find_header_end(const char *bytes, size_t size) {
    size_t index;
    for (index = 0; index + 3 < size; ++index) {
        if (bytes[index] == '\r' && bytes[index + 1] == '\n' &&
            bytes[index + 2] == '\r' && bytes[index + 3] == '\n') {
            return bytes + index + 4;
        }
    }
    return NULL;
}

static int parse_song_request(const char *bytes, size_t size,
                              char method[8], const char **body,
                              size_t *body_size) {
    const char *headers_end;
    const char *space;
    size_t method_size;
    if (!bytes || !size || !method || !body || !body_size) return 0;
    headers_end = find_header_end(bytes, size);
    if (!headers_end) return 0;
    space = memchr(bytes, ' ', (size_t)(headers_end - bytes));
    if (!space) return 0;
    method_size = (size_t)(space - bytes);
    if (!method_size || method_size >= 8) return 0;
    if (!ascii_mem_contains_ci(space + 1,
                               (size_t)(headers_end - (space + 1)),
                               "getGJSongInfo.php")) {
        return 0;
    }
    memcpy(method, bytes, method_size);
    method[method_size] = 0;
    *body = headers_end;
    *body_size = size - (size_t)(headers_end - bytes);
    return 1;
}

#ifdef _WIN32

typedef struct {
    DWORD status;
    unsigned char *payload;
    size_t payload_size;
} SongHttpAttempt;

static wchar_t *wide_from_utf8(const char *text) {
    int count;
    wchar_t *wide;
    if (!text) return NULL;
    count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (count <= 0) return NULL;
    wide = (wchar_t *)calloc((size_t)count, sizeof(*wide));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count)) {
        free(wide);
        return NULL;
    }
    return wide;
}

static const char *status_text(int code) {
    switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "Response";
    }
}

static int song_payload_is_invalid(const unsigned char *payload, size_t size) {
    size_t begin = 0;
    size_t end = size;
    if (!payload || !size) return 1;
    while (begin < end && isspace((unsigned char)payload[begin])) ++begin;
    while (end > begin && isspace((unsigned char)payload[end - 1])) --end;
    if (end == begin) return 1;
    if (end - begin == 2 && payload[begin] == '-' && payload[begin + 1] == '1')
        return 1;
    if (payload[begin] == '<') return 1;
    return ascii_mem_contains_ci((const char *)payload + begin, end - begin,
                                 "file_get_contents") ||
           ascii_mem_contains_ci((const char *)payload + begin, end - begin,
                                 "warning") ||
           ascii_mem_contains_ci((const char *)payload + begin, end - begin,
                                 "403 forbidden") ||
           ascii_mem_contains_ci((const char *)payload + begin, end - begin,
                                 "<!doctype html") ||
           ascii_mem_contains_ci((const char *)payload + begin, end - begin,
                                 "<html");
}

static void free_song_attempt(SongHttpAttempt *attempt) {
    if (!attempt) return;
    free(attempt->payload);
    memset(attempt, 0, sizeof(*attempt));
}

static int perform_song_attempt(HINTERNET session, const char *url,
                                const char *method, const char *body,
                                size_t body_size, SongHttpAttempt *output) {
    wchar_t *wide_url = NULL;
    wchar_t *wide_method = NULL;
    URL_COMPONENTS components;
    wchar_t host[512];
    wchar_t path[2048];
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    DWORD status_size = sizeof(output->status);
    size_t capacity = 0;
    int result = 0;

    if (!session || !url || !method || !output || body_size > 0xffffffffu)
        return 0;
    memset(output, 0, sizeof(*output));
    wide_url = wide_from_utf8(url);
    wide_method = wide_from_utf8(method);
    if (!wide_url || !wide_method) goto cleanup;

    memset(&components, 0, sizeof(components));
    components.dwStructSize = sizeof(components);
    components.lpszHostName = host;
    components.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));
    if (!WinHttpCrackUrl(wide_url, 0, 0, &components)) goto cleanup;
    if (components.dwHostNameLength >= sizeof(host) / sizeof(host[0]) ||
        components.dwUrlPathLength >= sizeof(path) / sizeof(path[0])) {
        goto cleanup;
    }
    host[components.dwHostNameLength] = 0;
    path[components.dwUrlPathLength] = 0;

    connection = WinHttpConnect(session, host, components.nPort, 0);
    if (!connection) goto cleanup;
    request = WinHttpOpenRequest(
        connection, wide_method, path, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        (components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) |
            WINHTTP_FLAG_REFRESH);
    if (!request) goto cleanup;
#if defined(WINHTTP_OPTION_REDIRECT_POLICY) && \
    defined(WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS)
    {
        DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirect_policy, sizeof(redirect_policy));
    }
#endif
    if (!WinHttpAddRequestHeaders(
            request,
            L"Content-Type: application/x-www-form-urlencoded\r\n"
            L"Connection: close\r\n",
            (DWORD)-1L,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        goto cleanup;
    }
    if (!WinHttpSendRequest(
            request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            body_size ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
            (DWORD)body_size, (DWORD)body_size, 0)) {
        goto cleanup;
    }
    if (!WinHttpReceiveResponse(request, NULL)) goto cleanup;
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &output->status, &status_size,
                             WINHTTP_NO_HEADER_INDEX)) {
        goto cleanup;
    }
    for (;;) {
        DWORD available = 0;
        DWORD read = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) goto cleanup;
        if (!available) break;
        if (output->payload_size + available > 16u * 1024u * 1024u)
            goto cleanup;
        if (output->payload_size + available > capacity) {
            size_t next = capacity ? capacity : 4096u;
            while (next < output->payload_size + available) {
                if (next > ((size_t)-1) / 2u) goto cleanup;
                next *= 2u;
            }
            {
                unsigned char *grown =
                    (unsigned char *)realloc(output->payload, next);
                if (!grown) goto cleanup;
                output->payload = grown;
                capacity = next;
            }
        }
        if (!WinHttpReadData(request, output->payload + output->payload_size,
                             available, &read)) {
            goto cleanup;
        }
        output->payload_size += read;
        if (!read) break;
    }
    result = 1;
cleanup:
    if (!result) free_song_attempt(output);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    free(wide_method);
    free(wide_url);
    return result;
}

static int append_attempt(char attempts[][2048], size_t *count,
                          const char *url) {
    size_t index;
    if (!attempts || !count || !url || !*url || *count >= 4u ||
        strlen(url) >= 2048u) {
        return 0;
    }
    for (index = 0; index < *count; ++index) {
        if (ascii_equal_ci(attempts[index], url)) return 1;
    }
    strcpy(attempts[*count], url);
    ++*count;
    return 1;
}

static int build_song_attempts(char attempts[][2048], size_t *count) {
    const char *original =
        "http://www.boomlings.com/database/getGJSongInfo.php";
    const char *official = gd_settings_official_song_url();
    char configured[2048];
    int rewrite;
    *count = 0;
    configured[0] = 0;
    rewrite = gd_settings_rewrite_url(original, configured, sizeof(configured));
    if (rewrite < 0) return 0;
    if (rewrite == 0) strcpy(configured, official);
    if (ascii_starts_ci(configured, "http://")) {
        char secure[2048];
        if (snprintf(secure, sizeof(secure), "https://%s", configured + 7) < 0)
            return 0;
        if (!append_attempt(attempts, count, secure) ||
            !append_attempt(attempts, count, configured)) {
            return 0;
        }
    } else if (!append_attempt(attempts, count, configured)) {
        return 0;
    }
    return append_attempt(attempts, count, official);
}

static int make_raw_response(const SongHttpAttempt *attempt,
                             unsigned char **response, size_t *response_size,
                             int *response_code) {
    char header[256];
    int header_size;
    unsigned char *raw;
    if (!attempt || !response || !response_size) return 0;
    header_size = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %lu %s\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n\r\n",
        (unsigned long)attempt->status, status_text((int)attempt->status),
        (unsigned long)attempt->payload_size);
    if (header_size < 0 || (size_t)header_size >= sizeof(header)) return 0;
    raw = (unsigned char *)malloc((size_t)header_size + attempt->payload_size);
    if (!raw) return 0;
    memcpy(raw, header, (size_t)header_size);
    if (attempt->payload_size) {
        memcpy(raw + header_size, attempt->payload, attempt->payload_size);
    }
    *response = raw;
    *response_size = (size_t)header_size + attempt->payload_size;
    if (response_code) *response_code = (int)attempt->status;
    return 1;
}
#endif

int gd_song_http_handle_raw_request(const void *request, size_t request_size,
                                    unsigned char **response,
                                    size_t *response_size,
                                    int *response_code) {
    const char *body = NULL;
    size_t body_size = 0;
    char method[8];
    if (response) *response = NULL;
    if (response_size) *response_size = 0;
    if (response_code) *response_code = 0;
    if (!request || !response || !response_size ||
        !parse_song_request((const char *)request, request_size, method,
                            &body, &body_size)) {
        return 0;
    }
#ifndef _WIN32
    (void)body;
    (void)body_size;
    return -1;
#else
    {
        HINTERNET session = NULL;
        char attempts[4][2048];
        size_t attempt_count = 0;
        size_t index;
        SongHttpAttempt final_attempt;
        int have_response = 0;
        int result = -1;
        memset(&final_attempt, 0, sizeof(final_attempt));

        if (!build_song_attempts(attempts, &attempt_count)) goto cleanup;
        session = WinHttpOpen(L"", WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) goto cleanup;
        WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);

        for (index = 0; index < attempt_count; ++index) {
            SongHttpAttempt attempt;
            const int completed = perform_song_attempt(
                session, attempts[index], method, body, body_size, &attempt);
            if (!completed) continue;
            free_song_attempt(&final_attempt);
            final_attempt = attempt;
            have_response = 1;
            if (attempt.status >= 200u && attempt.status < 300u &&
                !song_payload_is_invalid(attempt.payload,
                                         attempt.payload_size)) {
                break;
            }
        }
        if (!have_response) goto cleanup;
        if (!make_raw_response(&final_attempt, response, response_size,
                               response_code)) {
            goto cleanup;
        }
        result = 1;
cleanup:
        free_song_attempt(&final_attempt);
        if (session) WinHttpCloseHandle(session);
        return result;
    }
#endif
}
