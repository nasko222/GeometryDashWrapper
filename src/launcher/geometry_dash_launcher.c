#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>

static char gd_config_buffer[32768];
static wchar_t gd_command_buffer[32768];


static size_t wide_length(const wchar_t *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

static int ascii_ci_equal_range(const char *begin, const char *end,
                                const char *expected) {
    while (begin < end && *expected) {
        char left = *begin++;
        char right = *expected++;
        if (left >= 'A' && left <= 'Z') left = (char)(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z') right = (char)(right + ('a' - 'A'));
        if (left != right) return 0;
    }
    return begin == end && *expected == 0;
}

static void trim_ascii(const char **begin, const char **end) {
    while (*begin < *end && (**begin == ' ' || **begin == '\t')) ++*begin;
    while (*end > *begin &&
           ((*end)[-1] == ' ' || (*end)[-1] == '\t' ||
            (*end)[-1] == '\r' || (*end)[-1] == '\n')) --*end;
}

static int read_console_setting(const wchar_t *cfg_path) {
    HANDLE file;
    DWORD bytes_read = 0;
    char *buffer = gd_config_buffer;
    const char *cursor;
    const char *limit;
    int disabled = 1;

    file = CreateFileW(cfg_path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return disabled;
    if (!ReadFile(file, buffer, (DWORD)(sizeof(gd_config_buffer) - 1u),
                  &bytes_read, NULL)) {
        CloseHandle(file);
        return disabled;
    }
    CloseHandle(file);
    buffer[bytes_read] = 0;
    cursor = buffer;
    limit = buffer + bytes_read;

    while (cursor < limit) {
        const char *line_begin = cursor;
        const char *line_end;
        const char *equals;
        const char *key_begin;
        const char *key_end;
        const char *value_begin;
        const char *value_end;
        while (cursor < limit && *cursor != '\n') ++cursor;
        line_end = cursor;
        if (cursor < limit) ++cursor;
        key_begin = line_begin;
        key_end = line_end;
        trim_ascii(&key_begin, &key_end);
        if (key_begin == key_end || *key_begin == '#' || *key_begin == ';' ||
            *key_begin == '[') continue;
        equals = key_begin;
        while (equals < key_end && *equals != '=') ++equals;
        if (equals == key_end) continue;
        value_begin = equals + 1;
        value_end = key_end;
        key_end = equals;
        trim_ascii(&key_begin, &key_end);
        trim_ascii(&value_begin, &value_end);
        if (!ascii_ci_equal_range(key_begin, key_end,
                                  "disable_windows_console")) continue;
        disabled = ascii_ci_equal_range(value_begin, value_end, "true") ||
                   ascii_ci_equal_range(value_begin, value_end, "yes") ||
                   ascii_ci_equal_range(value_begin, value_end, "on") ||
                   ascii_ci_equal_range(value_begin, value_end, "1");
    }
    return disabled;
}

static int wide_copy(wchar_t *out, size_t capacity, const wchar_t *text) {
    size_t index = 0;
    while (text[index]) {
        if (index + 1u >= capacity) return 0;
        out[index] = text[index];
        ++index;
    }
    if (!capacity) return 0;
    out[index] = 0;
    return 1;
}

static int path_join(wchar_t *out, size_t capacity,
                     const wchar_t *left, const wchar_t *right) {
    size_t left_length = wide_length(left);
    size_t right_length = wide_length(right);
    size_t index;
    int separator = left_length != 0u && left[left_length - 1u] != L'\\' &&
                    left[left_length - 1u] != L'/';
    if (left_length + (size_t)separator + right_length + 1u > capacity)
        return 0;
    for (index = 0; index < left_length; ++index) out[index] = left[index];
    if (separator) out[index++] = L'\\';
    for (size_t right_index = 0; right_index < right_length; ++right_index)
        out[index++] = right[right_index];
    out[index] = 0;
    return 1;
}

static int append_wide(wchar_t *out, size_t capacity, size_t *used,
                       const wchar_t *text) {
    while (*text) {
        if (*used + 1u >= capacity) return 0;
        out[(*used)++] = *text++;
    }
    out[*used] = 0;
    return 1;
}

static int append_quoted(wchar_t *out, size_t capacity, size_t *used,
                         const wchar_t *argument) {
    size_t slash_count = 0;
    if (!append_wide(out, capacity, used, L"\"")) return 0;
    while (*argument) {
        if (*argument == L'\\') {
            ++slash_count;
            ++argument;
            continue;
        }
        if (*argument == L'\"') {
            while (slash_count) {
                if (!append_wide(out, capacity, used, L"\\\\")) return 0;
                --slash_count;
            }
            if (!append_wide(out, capacity, used, L"\\\"")) return 0;
            ++argument;
            continue;
        }
        while (slash_count) {
            if (!append_wide(out, capacity, used, L"\\")) return 0;
            --slash_count;
        }
        if (*used + 1u >= capacity) return 0;
        out[(*used)++] = *argument++;
        out[*used] = 0;
    }
    while (slash_count) {
        if (!append_wide(out, capacity, used, L"\\\\")) return 0;
        --slash_count;
    }
    return append_wide(out, capacity, used, L"\"");
}

static void zero_bytes(void *memory, size_t size) {
    unsigned char *bytes = (unsigned char *)memory;
    while (size--) *bytes++ = 0;
}

static int launch_python(const wchar_t *python, int use_py_switch,
                         const wchar_t *script, const wchar_t *cfg,
                         const wchar_t *directory, int hide_console,
                         DWORD *exit_code) {
    wchar_t *command = gd_command_buffer;
    size_t used = 0;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD flags = hide_console ? CREATE_NO_WINDOW : 0;
    command[0] = 0;
    zero_bytes(&startup, sizeof(startup));
    zero_bytes(&process, sizeof(process));
    startup.cb = sizeof(startup);

    if (!append_quoted(command, sizeof(gd_command_buffer) / sizeof(gd_command_buffer[0]),
                       &used, python)) return 0;
    if (use_py_switch &&
        !append_wide(command, sizeof(gd_command_buffer) / sizeof(gd_command_buffer[0]),
                     &used, L" -3")) return 0;
    if (!append_wide(command, sizeof(gd_command_buffer) / sizeof(gd_command_buffer[0]),
                     &used, L" ") ||
        !append_quoted(command, sizeof(gd_command_buffer) / sizeof(gd_command_buffer[0]),
                       &used, script) ||
        !append_wide(command, sizeof(gd_command_buffer) / sizeof(gd_command_buffer[0]),
                     &used, L" --cfg ") ||
        !append_quoted(command, sizeof(gd_command_buffer) / sizeof(gd_command_buffer[0]),
                       &used, cfg)) return 0;

    if (!CreateProcessW(python, command, NULL, NULL, FALSE, flags,
                        NULL, directory, &startup, &process)) return 0;
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 1;
}

static int launcher_main(void) {
    wchar_t executable[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t script[MAX_PATH];
    wchar_t cfg[MAX_PATH];
    wchar_t python[MAX_PATH];
    DWORD path_length;
    DWORD exit_code = 1;
    int hide_console;
    size_t length;

    path_length = GetModuleFileNameW(NULL, executable,
                                    (DWORD)(sizeof(executable) /
                                            sizeof(executable[0])));
    if (!path_length || path_length >= sizeof(executable) / sizeof(executable[0]))
        return 1;
    if (!wide_copy(directory, sizeof(directory) / sizeof(directory[0]),
                   executable)) return 1;
    length = wide_length(directory);
    while (length && directory[length - 1u] != L'\\' &&
           directory[length - 1u] != L'/') --length;
    if (!length) return 1;
    directory[length - 1u] = 0;

    if (!path_join(script, sizeof(script) / sizeof(script[0]),
                   directory, L"run_auto.py") ||
        !path_join(cfg, sizeof(cfg) / sizeof(cfg[0]),
                   directory, L"GeometryDash.cfg")) return 1;

    hide_console = read_console_setting(cfg);
    if (!hide_console) {
        AllocConsole();
        SetConsoleTitleW(L"Geometry Dash Wrapper");
    }

    if (SearchPathW(NULL, L"py.exe", NULL,
                    (DWORD)(sizeof(python) / sizeof(python[0])),
                    python, NULL) &&
        launch_python(python, 1, script, cfg, directory,
                      hide_console, &exit_code)) return (int)exit_code;
    if (SearchPathW(NULL, L"python.exe", NULL,
                    (DWORD)(sizeof(python) / sizeof(python[0])),
                    python, NULL) &&
        launch_python(python, 0, script, cfg, directory,
                      hide_console, &exit_code)) return (int)exit_code;

    MessageBoxW(NULL,
        L"Python 3 was not found. Install Python 3 or add python.exe/py.exe to PATH.",
        L"Geometry Dash", MB_OK | MB_ICONERROR);
    return 2;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show_command) {
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;
    return launcher_main();
}

#ifdef GD_LAUNCHER_NO_CRT
void WINAPI wWinMainCRTStartup(void) {
    ExitProcess((UINT)launcher_main());
}
#endif
