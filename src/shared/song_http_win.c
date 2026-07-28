#include "song_http_win.h"

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
static wchar_t *wide_from_ascii(const char *text) {
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
        HINTERNET connection = NULL;
        HINTERNET handle = NULL;
        wchar_t *wide_method = NULL;
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        unsigned char *payload = NULL;
        size_t payload_size = 0;
        size_t payload_capacity = 0;
        unsigned char *raw = NULL;
        char header[256];
        int header_size;
        int result = -1;

        wide_method = wide_from_ascii(method);
        if (!wide_method) goto cleanup;
        session = WinHttpOpen(L"", WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) goto cleanup;
        WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);
        connection = WinHttpConnect(session, L"www.boomlings.com",
                                    INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connection) goto cleanup;
        handle = WinHttpOpenRequest(
            connection, wide_method, L"/database/getGJSongInfo.php",
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
        if (!handle) goto cleanup;
        if (!WinHttpAddRequestHeaders(
                handle,
                L"Content-Type: application/x-www-form-urlencoded\r\n"
                L"Connection: close\r\n",
                (DWORD)-1L,
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
            goto cleanup;
        }
        if (!WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                body_size ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
                                (DWORD)(body_size > 0xffffffffu ? 0xffffffffu : body_size),
                                (DWORD)(body_size > 0xffffffffu ? 0xffffffffu : body_size),
                                0)) {
            goto cleanup;
        }
        if (!WinHttpReceiveResponse(handle, NULL)) goto cleanup;
        if (!WinHttpQueryHeaders(handle,
                                 WINHTTP_QUERY_STATUS_CODE |
                                     WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                 &status, &status_size,
                                 WINHTTP_NO_HEADER_INDEX)) {
            goto cleanup;
        }
        for (;;) {
            DWORD available = 0;
            DWORD read = 0;
            if (!WinHttpQueryDataAvailable(handle, &available)) goto cleanup;
            if (!available) break;
            if (payload_size + available > payload_capacity) {
                size_t next = payload_capacity ? payload_capacity : 4096u;
                while (next < payload_size + available) {
                    if (next > ((size_t)-1) / 2u) goto cleanup;
                    next *= 2u;
                }
                {
                    unsigned char *grown =
                        (unsigned char *)realloc(payload, next);
                    if (!grown) goto cleanup;
                    payload = grown;
                    payload_capacity = next;
                }
            }
            if (!WinHttpReadData(handle, payload + payload_size,
                                 available, &read)) {
                goto cleanup;
            }
            payload_size += read;
            if (!read) break;
        }
        header_size = snprintf(
            header, sizeof(header),
            "HTTP/1.1 %lu %s\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n\r\n",
            (unsigned long)status, status_text((int)status),
            (unsigned long)payload_size);
        if (header_size < 0 || (size_t)header_size >= sizeof(header))
            goto cleanup;
        raw = (unsigned char *)malloc((size_t)header_size + payload_size);
        if (!raw) goto cleanup;
        memcpy(raw, header, (size_t)header_size);
        if (payload_size) memcpy(raw + header_size, payload, payload_size);
        *response = raw;
        *response_size = (size_t)header_size + payload_size;
        if (response_code) *response_code = (int)status;
        raw = NULL;
        result = 1;
cleanup:
        free(raw);
        free(payload);
        free(wide_method);
        if (handle) WinHttpCloseHandle(handle);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
        return result;
    }
#endif
}
