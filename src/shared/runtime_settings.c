#include "runtime_settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GD_DEFAULT_SERVER "www.boomlings.com/database"
#define GD_SETTINGS_TEXT_MAX 2048

static int ascii_equal_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int ascii_contains_ci(const char *text, const char *needle) {
    size_t needle_length;
    const char *cursor;
    if (!text || !needle || !*needle) return 0;
    needle_length = strlen(needle);
    for (cursor = text; *cursor; ++cursor) {
        size_t index;
        for (index = 0; index < needle_length; ++index) {
            if (!cursor[index] ||
                tolower((unsigned char)cursor[index]) !=
                    tolower((unsigned char)needle[index])) {
                break;
            }
        }
        if (index == needle_length) return 1;
    }
    return 0;
}

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

static const char *ascii_mem_find_ci(const char *text, size_t text_size,
                                     const char *needle) {
    size_t needle_size;
    size_t offset;
    if (!text || !needle || !*needle) return NULL;
    needle_size = strlen(needle);
    if (needle_size > text_size) return NULL;
    for (offset = 0; offset + needle_size <= text_size; ++offset) {
        size_t index;
        for (index = 0; index < needle_size; ++index) {
            if (tolower((unsigned char)text[offset + index]) !=
                tolower((unsigned char)needle[index])) {
                break;
            }
        }
        if (index == needle_size) return text + offset;
    }
    return NULL;
}

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

int gd_setting_bool(const char *name, int default_value) {
    const char *value = name ? getenv(name) : NULL;
    if (!value || !*value) return default_value != 0;
    if (ascii_equal_ci(value, "true") || ascii_equal_ci(value, "yes") ||
        ascii_equal_ci(value, "on") || strcmp(value, "1") == 0) {
        return 1;
    }
    if (ascii_equal_ci(value, "false") || ascii_equal_ci(value, "no") ||
        ascii_equal_ci(value, "off") || strcmp(value, "0") == 0) {
        return 0;
    }
    return default_value != 0;
}

float gd_setting_float(const char *name, float default_value,
                       float minimum, float maximum) {
    const char *value = name ? getenv(name) : NULL;
    char *end = NULL;
    float parsed;
    if (minimum > maximum) {
        const float temporary = minimum;
        minimum = maximum;
        maximum = temporary;
    }
    if (!value || !*value) parsed = default_value;
    else {
        parsed = strtof(value, &end);
        if (end == value || *end != 0 || parsed != parsed)
            parsed = default_value;
    }
    if (parsed < minimum) parsed = minimum;
    if (parsed > maximum) parsed = maximum;
    return parsed;
}

int gd_settings_hack_icons(void) {
    return gd_setting_bool("HACK_ICONS", 0);
}

int gd_settings_full_bypass(void) {
    return gd_setting_bool("FULL_BYPASS", 1);
}

int gd_settings_force_highest_graphics(void) {
    return gd_setting_bool("FORCE_HIGHEST_GRAPHICS", 1);
}

int gd_settings_i_lost_the_game(void) {
    return gd_setting_bool("I_LOST_THE_GAME", 0);
}

int gd_settings_editor_controls(void) {
    return gd_setting_bool("EDITOR_CONTROLLS", 0);
}

int gd_settings_extras_menu(void) {
    /* Extras remains temporarily removed in gdpstweaks15. */
    return 0;
}

int gd_settings_v22_exact_editor_visibility(void) {
    return gd_setting_bool("V22_EXACT_EDITOR_VISIBILITY", 0);
}

float gd_settings_music_pulse_max(void) {
    return gd_setting_float("MUSIC_PULSE_MAX", 0.30f, 0.0f, 1.0f);
}

const char *gd_settings_server(void) {
    const char *value = getenv("GDPS_SERVER");
    return value && *value ? value : GD_DEFAULT_SERVER;
}

const char *gd_settings_official_song_url(void) {
    return "https://www.boomlings.com/database/getGJSongInfo.php";
}

typedef struct {
    char scheme[16];
    char host[512];
    char base_path[1024];
    char host_header[560];
} GdServerParts;

static int parse_server_parts(GdServerParts *parts) {
    char text[GD_SETTINGS_TEXT_MAX];
    const char *setting = gd_settings_server();
    const char *cursor;
    char *slash;
    char *end;
    size_t length;
    if (!parts || !setting || !*setting) return 0;
    memset(parts, 0, sizeof(*parts));
    length = strlen(setting);
    if (length >= sizeof(text)) return 0;
    memcpy(text, setting, length + 1);

    cursor = text;
    if (ascii_starts_ci(cursor, "https://")) {
        strcpy(parts->scheme, "https");
        cursor += 8;
    } else if (ascii_starts_ci(cursor, "http://")) {
        strcpy(parts->scheme, "http");
        cursor += 7;
    }
    while (*cursor == '/') ++cursor;
    if (!*cursor) return 0;

    slash = strchr((char *)cursor, '/');
    if (slash) {
        *slash++ = 0;
        snprintf(parts->base_path, sizeof(parts->base_path), "/%s", slash);
    } else {
        strcpy(parts->base_path, "/database");
    }
    if (strlen(cursor) >= sizeof(parts->host_header) ||
        strlen(cursor) >= sizeof(parts->host))
        return 0;
    strcpy(parts->host_header, cursor);
    strcpy(parts->host, cursor);

    /* Remove userinfo, then strip a numeric port for DNS resolution. */
    {
        char *at = strrchr(parts->host, '@');
        if (at) memmove(parts->host, at + 1, strlen(at + 1) + 1);
    }
    if (parts->host[0] == '[') {
        char *close = strchr(parts->host, ']');
        if (close) {
            memmove(parts->host, parts->host + 1,
                    (size_t)(close - parts->host - 1));
            parts->host[close - parts->host - 1] = 0;
        }
    } else {
        char *colon = strrchr(parts->host, ':');
        if (colon) {
            char *digit = colon + 1;
            int numeric = *digit != 0;
            while (*digit) {
                if (!isdigit((unsigned char)*digit++)) numeric = 0;
            }
            if (numeric) *colon = 0;
        }
    }

    end = parts->base_path + strlen(parts->base_path);
    while (end > parts->base_path + 1 && end[-1] == '/') *--end = 0;
    if (!parts->host[0] || !parts->base_path[0]) return 0;
    return 1;
}

int gd_settings_server_is_official(void) {
    GdServerParts server;
    if (!parse_server_parts(&server)) return 0;
    return (ascii_equal_ci(server.host, "www.boomlings.com") ||
            ascii_equal_ci(server.host, "boomlings.com")) &&
           ascii_equal_ci(server.base_path, "/database");
}

static const char *find_ascii_ci(const char *text, const char *needle) {
    size_t needle_length;
    const char *cursor;
    if (!text || !needle || !*needle) return NULL;
    needle_length = strlen(needle);
    for (cursor = text; *cursor; ++cursor) {
        size_t index;
        for (index = 0; index < needle_length; ++index) {
            if (!cursor[index] ||
                tolower((unsigned char)cursor[index]) !=
                    tolower((unsigned char)needle[index])) {
                break;
            }
        }
        if (index == needle_length) return cursor;
    }
    return NULL;
}

static int parse_api_url(const char *input, char *scheme, size_t scheme_size,
                         char *host, size_t host_size, char *relative_path,
                         size_t relative_size, const char **query) {
    const char *cursor;
    const char *host_end;
    const char *path;
    const char *database_marker;
    const char *server_marker;
    const char *relative_start;
    const char *relative_end;
    size_t length;
    if (!input || !scheme || !host || !relative_path || !query) return 0;
    if (ascii_starts_ci(input, "https://")) {
        snprintf(scheme, scheme_size, "https");
        cursor = input + 8;
    } else if (ascii_starts_ci(input, "http://")) {
        snprintf(scheme, scheme_size, "http");
        cursor = input + 7;
    } else {
        return 0;
    }
    host_end = strchr(cursor, '/');
    if (!host_end) return 0;
    length = (size_t)(host_end - cursor);
    if (!length || length >= host_size) return 0;
    memcpy(host, cursor, length);
    host[length] = 0;
    path = host_end;
    *query = strchr(path, '?');
    database_marker = find_ascii_ci(path, "/database/");
    server_marker = find_ascii_ci(path, "/server/");
    if (database_marker && server_marker)
        relative_start = database_marker < server_marker
                             ? database_marker + strlen("/database/")
                             : server_marker + strlen("/server/");
    else if (database_marker)
        relative_start = database_marker + strlen("/database/");
    else if (server_marker)
        relative_start = server_marker + strlen("/server/");
    else
        return 0;
    relative_end = *query ? *query : path + strlen(path);
    if (relative_end <= relative_start) return 0;
    length = (size_t)(relative_end - relative_start);
    if (!length || length >= relative_size) return 0;
    memcpy(relative_path, relative_start, length);
    relative_path[length] = 0;
    {
        const char *filename = strrchr(relative_path, '/');
        filename = filename ? filename + 1 : relative_path;
        length = strlen(filename);
        if (length < 4 || !ascii_equal_ci(filename + length - 4, ".php"))
            return 0;
    }
    return 1;
}

int gd_settings_rewrite_url(const char *input, char *output, size_t capacity) {
    GdServerParts server;
    char original_scheme[16];
    char original_host[512];
    char relative_path[1024];
    const char *query = NULL;
    const char *scheme;
    const char *filename;
    int written;
    if (!output || !capacity) return -1;
    output[0] = 0;
    if (!parse_server_parts(&server) ||
        !parse_api_url(input, original_scheme, sizeof(original_scheme),
                       original_host, sizeof(original_host), relative_path,
                       sizeof(relative_path), &query)) {
        return 0;
    }
    filename = strrchr(relative_path, '/');
    filename = filename ? filename + 1 : relative_path;
    /* A custom GDPS may provide its own song catalogue. Route song metadata to
     * that server first; the transport layer validates the response and falls
     * back to official HTTPS Boomlings when the custom endpoint is absent,
     * returns -1, or emits an HTML/PHP proxy error. */
    if (ascii_equal_ci(filename, "getGJSongInfo.php") &&
        gd_settings_server_is_official()) {
        written = snprintf(output, capacity, "%s%s",
                           gd_settings_official_song_url(),
                           query ? query : "");
    } else {
        scheme = server.scheme[0] ? server.scheme : original_scheme;
        written = snprintf(output, capacity, "%s://%s%s/%s%s", scheme,
                           server.host_header, server.base_path, relative_path,
                           query ? query : "");
    }
    if (written < 0 || (size_t)written >= capacity) {
        output[0] = 0;
        return -1;
    }
    return strcmp(input, output) != 0 ? 1 : 0;
}

static int is_known_game_api_host(const char *host) {
    if (!host || !*host) return 0;
    return ascii_equal_ci(host, "boomlings.com") ||
           ascii_equal_ci(host, "www.boomlings.com") ||
           ascii_contains_ci(host, "gdps");
}

int gd_settings_override_dns_host(const char *input, char *output,
                                  size_t capacity) {
    GdServerParts server;
    if (!input || !output || !capacity || !parse_server_parts(&server) ||
        !is_known_game_api_host(input) || ascii_equal_ci(input, server.host)) {
        return 0;
    }
    if (strlen(server.host) + 1 > capacity) return 0;
    strcpy(output, server.host);
    return 1;
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

static const char *find_line_end(const char *begin, const char *end) {
    const char *cursor;
    for (cursor = begin; cursor + 1 < end; ++cursor) {
        if (cursor[0] == '\r' && cursor[1] == '\n') return cursor;
    }
    return NULL;
}

int gd_settings_rewrite_http_request(const void *input, size_t input_size,
                                     void **output, size_t *output_size) {
    const char *bytes = (const char *)input;
    const char *header_end;
    const char *request_line_end;
    const char *first_space;
    const char *second_space;
    const char *query;
    const char *path_end;
    const char *relative_start;
    const char *filename_start;
    const char *host_line = NULL;
    const char *host_line_end = NULL;
    GdServerParts server;
    char new_path[1536];
    size_t method_size;
    size_t relative_size;
    size_t filename_size;
    size_t query_size;
    size_t new_path_size;
    size_t request_prefix_size;
    size_t request_suffix_size;
    size_t host_name_size;
    size_t total;
    char *replacement;
    char *write;
    const char *cursor;
    if (output) *output = NULL;
    if (output_size) *output_size = 0;
    if (!input || !input_size || !output || !output_size ||
        !parse_server_parts(&server)) {
        return 0;
    }
    header_end = find_header_end(bytes, input_size);
    if (!header_end) return 0;
    request_line_end = find_line_end(bytes, header_end);
    if (!request_line_end) return 0;
    first_space = memchr(bytes, ' ', (size_t)(request_line_end - bytes));
    if (!first_space) return 0;
    second_space = memchr(first_space + 1, ' ',
                          (size_t)(request_line_end - first_space - 1));
    if (!second_space) return 0;
    method_size = (size_t)(first_space - bytes);
    if (!((method_size == 3 && memcmp(bytes, "GET", 3) == 0) ||
          (method_size == 4 && memcmp(bytes, "POST", 4) == 0) ||
          (method_size == 4 && memcmp(bytes, "HEAD", 4) == 0) ||
          (method_size == 3 && memcmp(bytes, "PUT", 3) == 0) ||
          (method_size == 6 && memcmp(bytes, "DELETE", 6) == 0))) {
        return 0;
    }
    path_end = second_space;
    if (!ascii_mem_contains_ci(first_space + 1,
                               (size_t)(path_end - first_space - 1),
                               "/database/") &&
        !ascii_mem_contains_ci(first_space + 1,
                               (size_t)(path_end - first_space - 1),
                               "/server/")) {
        return 0;
    }
    query = memchr(first_space + 1, '?',
                   (size_t)(path_end - first_space - 1));
    {
        const char *request_path = first_space + 1;
        const size_t request_path_size =
            (size_t)((query ? query : path_end) - request_path);
        const char *database_marker = ascii_mem_find_ci(
            request_path, request_path_size, "/database/");
        const char *server_marker = ascii_mem_find_ci(
            request_path, request_path_size, "/server/");
        if (database_marker && server_marker)
            relative_start = database_marker < server_marker
                                 ? database_marker + strlen("/database/")
                                 : server_marker + strlen("/server/");
        else if (database_marker)
            relative_start = database_marker + strlen("/database/");
        else if (server_marker)
            relative_start = server_marker + strlen("/server/");
        else
            return 0;
    }
    relative_size = (size_t)((query ? query : path_end) - relative_start);
    filename_start = relative_start + relative_size;
    while (filename_start > relative_start && filename_start[-1] != '/')
        --filename_start;
    filename_size = (size_t)(relative_start + relative_size - filename_start);
    if (filename_size < 4 ||
        tolower((unsigned char)filename_start[filename_size - 4]) != '.' ||
        tolower((unsigned char)filename_start[filename_size - 3]) != 'p' ||
        tolower((unsigned char)filename_start[filename_size - 2]) != 'h' ||
        tolower((unsigned char)filename_start[filename_size - 1]) != 'p') {
        return 0;
    }
    if (filename_size == strlen("getGJSongInfo.php") &&
        ascii_mem_contains_ci(filename_start, filename_size,
                              "getGJSongInfo.php")) {
        return 0; /* handled by the custom-first song transport */
    }
    query_size = query ? (size_t)(path_end - query) : 0;
    if (snprintf(new_path, sizeof(new_path), "%s/%.*s%.*s",
                 server.base_path, (int)relative_size, relative_start,
                 (int)query_size, query ? query : "") < 0) {
        return -1;
    }
    new_path_size = strlen(new_path);

    cursor = request_line_end + 2;
    while (cursor < header_end - 2) {
        const char *line_end = find_line_end(cursor, header_end);
        if (!line_end) break;
        if ((size_t)(line_end - cursor) >= 5 &&
            tolower((unsigned char)cursor[0]) == 'h' &&
            tolower((unsigned char)cursor[1]) == 'o' &&
            tolower((unsigned char)cursor[2]) == 's' &&
            tolower((unsigned char)cursor[3]) == 't' && cursor[4] == ':') {
            host_line = cursor;
            host_line_end = line_end;
            break;
        }
        cursor = line_end + 2;
    }

    request_prefix_size = (size_t)(first_space + 1 - bytes);
    if (!host_line) {
        request_suffix_size = input_size - (size_t)(second_space - bytes);
        total = request_prefix_size + new_path_size + request_suffix_size;
        replacement = (char *)malloc(total);
        if (!replacement) return -1;
        write = replacement;
        memcpy(write, bytes, request_prefix_size);
        write += request_prefix_size;
        memcpy(write, new_path, new_path_size);
        write += new_path_size;
        memcpy(write, second_space, request_suffix_size);
    } else {
        size_t between_size = (size_t)(host_line - second_space);
        size_t after_host_size = input_size - (size_t)(host_line_end - bytes);
        host_name_size = strlen(server.host_header);
        total = request_prefix_size + new_path_size + between_size +
                6 + host_name_size + after_host_size;
        replacement = (char *)malloc(total);
        if (!replacement) return -1;
        write = replacement;
        memcpy(write, bytes, request_prefix_size);
        write += request_prefix_size;
        memcpy(write, new_path, new_path_size);
        write += new_path_size;
        memcpy(write, second_space, between_size);
        write += between_size;
        memcpy(write, "Host: ", 6);
        write += 6;
        memcpy(write, server.host_header, host_name_size);
        write += host_name_size;
        memcpy(write, host_line_end, after_host_size);
    }

    if (total == input_size && memcmp(replacement, input, input_size) == 0) {
        free(replacement);
        return 0;
    }
    *output = replacement;
    *output_size = total;
    return 1;
}
