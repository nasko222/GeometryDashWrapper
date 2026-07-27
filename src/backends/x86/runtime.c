#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "runtime.h"
#include "build_info.h"
#include "net_compat_win.h"
#include "fmod_win.h"
#include "storage_win.h"
#include "zlib.h"

#define MAX_IMPORTS 1024
#define THUNK_SIZE 16
#define CALLCONV_THUNK_SIZE 80
#define MAX_ATEXIT 1024

typedef struct {
    void (*function)(void *);
    void *argument;
    void *dso;
} AtExitEntry;

typedef struct {
    uint32_t dlpi_addr;
    const char *dlpi_name;
    const Elf32_Phdr *dlpi_phdr;
    uint16_t dlpi_phnum;
    uint16_t padding;
} DlPhdrInfo32;

static FILE *g_log;
static char *g_import_names[MAX_IMPORTS];
static void *g_import_targets[MAX_IMPORTS];
static unsigned char *g_thunks;
static unsigned char *g_callconv_thunks;
static HMODULE g_msvcrt;
static HMODULE g_ws2;
static HMODULE g_opengl;
static void *g_elf_base;
static size_t g_elf_size;
static const char *g_elf_path;
static const Elf32_Phdr *g_elf_phdr;
static uint16_t g_elf_phnum;
static AtExitEntry g_atexit[MAX_ATEXIT];
static unsigned g_atexit_count;
static int g_errno_value;
static uintptr_t g_stack_guard = 0x6d5a56a9u;
static unsigned char g_sF[512];
static unsigned char g_ctype[384];
static int16_t g_tolower[384];
static int16_t g_toupper[384];
static const unsigned char *g_ctype_pointer;
static const int16_t *g_tolower_pointer;
static const int16_t *g_toupper_pointer;
static char *g_optarg;
static int g_optind = 1;
static uint32_t g_lcg_state = 1;

/* OpenSSL in the Android game reads /dev/urandom before starting HTTPS.
 * Keep this descriptor odd so it cannot collide with a Winsock HANDLE, which
 * is aligned, and below the Android getdtablesize() result. */
#define ANDROID_RANDOM_FD 2047
static INIT_ONCE g_random_once = INIT_ONCE_STATIC_INIT;
static BOOLEAN (WINAPI *g_system_random)(PVOID, ULONG);
static volatile LONG g_random_open_count;
typedef unsigned (WINAPI *IfNameToIndexFunction)(const char *);
static INIT_ONCE g_if_name_index_once = INIT_ONCE_STATIC_INIT;
static IfNameToIndexFunction g_if_name_to_index;

static BOOL CALLBACK initialize_system_random(PINIT_ONCE once, PVOID parameter,
                                               PVOID *context) {
    HMODULE module;
    (void)once;
    (void)parameter;
    (void)context;
    module = LoadLibraryA("advapi32.dll");
    if (module) {
        g_system_random = (BOOLEAN (WINAPI *)(PVOID, ULONG))
            GetProcAddress(module, "SystemFunction036");
    }
    if (g_system_random) {
        runtime_log("Secure random: Windows /dev/urandom bridge initialized");
    } else {
        runtime_log("Secure random: Windows provider is unavailable");
    }
    return TRUE;
}

static BOOL CALLBACK initialize_if_name_index(PINIT_ONCE once, PVOID parameter,
                                              PVOID *context) {
    HMODULE module;
    (void)once;
    (void)parameter;
    (void)context;
    module = LoadLibraryA("iphlpapi.dll");
    if (module) {
        g_if_name_to_index = (IfNameToIndexFunction)GetProcAddress(
            module, "if_nametoindex");
    }
    return TRUE;
}

static int fill_system_random(void *buffer, size_t length) {
    unsigned char *cursor = (unsigned char *)buffer;
    InitOnceExecuteOnce(&g_random_once, initialize_system_random, NULL, NULL);
    if (!g_system_random || (!buffer && length)) return 0;
    while (length) {
        ULONG chunk = length > ULONG_MAX ? ULONG_MAX : (ULONG)length;
        if (!g_system_random(cursor, chunk)) return 0;
        cursor += chunk;
        length -= chunk;
    }
    return 1;
}

/*
 * The Android/i386 signal ABI used by these APKs is a compact 16-byte
 * sigaction. Windows sockets never raise SIGPIPE, but libcurl/OpenSSL still
 * save, replace, and restore these records. Keeping an in-process copy gives
 * those libraries the state semantics they expect without installing Unix
 * handlers into the Windows process.
 */
typedef struct {
    uint32_t words[4];
} AndroidSigaction;

#define ANDROID_SIGNAL_COUNT 65
static SRWLOCK g_signal_lock = SRWLOCK_INIT;
static AndroidSigaction g_signal_actions[ANDROID_SIGNAL_COUNT];
static uint32_t g_signal_mask;

extern int shim_bionic_setjmp(uint32_t *environment);
extern int shim_bionic_sigsetjmp(uint32_t *environment, int save_mask);
extern void shim_bionic_longjmp(uint32_t *environment, int value);

#if defined(__i386__)
static void log_crash_stack(uintptr_t stack_pointer) {
    MEMORY_BASIC_INFORMATION memory;
    uintptr_t region_end;
    size_t available;
    size_t count;
    size_t index;
    const uint32_t *stack;

    if (!stack_pointer ||
        !VirtualQuery((const void *)stack_pointer, &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        runtime_log("Stack: unavailable at %p", (void *)stack_pointer);
        return;
    }
    region_end = (uintptr_t)memory.BaseAddress + memory.RegionSize;
    if (region_end <= stack_pointer) {
        runtime_log("Stack: invalid memory region at %p", (void *)stack_pointer);
        return;
    }
    available = (size_t)(region_end - stack_pointer);
    count = available / sizeof(uint32_t);
    if (count > 96) count = 96;
    stack = (const uint32_t *)stack_pointer;
    runtime_log("Stack: first %lu DWORDs from ESP (ELF values are candidate return addresses)",
                (unsigned long)count);
    for (index = 0; index < count; ++index) {
        uintptr_t value = stack[index];
        if (g_elf_base && value >= (uintptr_t)g_elf_base &&
            value < (uintptr_t)g_elf_base + g_elf_size) {
            runtime_log("  ESP+0x%02lx = %08lx  ELF+0x%08lx",
                        (unsigned long)(index * sizeof(uint32_t)),
                        (unsigned long)value,
                        (unsigned long)(value - (uintptr_t)g_elf_base));
        } else {
            runtime_log("  ESP+0x%02lx = %08lx",
                        (unsigned long)(index * sizeof(uint32_t)),
                        (unsigned long)value);
        }
    }
}
#endif

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *info) {
    uintptr_t address = (uintptr_t)info->ExceptionRecord->ExceptionAddress;
    runtime_log("FATAL: Windows exception 0x%08lx at %p",
                (unsigned long)info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress);
    if (g_elf_base && address >= (uintptr_t)g_elf_base &&
        address < (uintptr_t)g_elf_base + g_elf_size) {
        runtime_log("FATAL: exception address is ELF+0x%08lx",
                    (unsigned long)(address - (uintptr_t)g_elf_base));
    }
#if defined(__i386__)
    if (info->ContextRecord) {
        runtime_log("Registers: EAX=%08lx EBX=%08lx ECX=%08lx EDX=%08lx",
                    (unsigned long)info->ContextRecord->Eax,
                    (unsigned long)info->ContextRecord->Ebx,
                    (unsigned long)info->ContextRecord->Ecx,
                    (unsigned long)info->ContextRecord->Edx);
        runtime_log("           ESI=%08lx EDI=%08lx EBP=%08lx ESP=%08lx",
                    (unsigned long)info->ContextRecord->Esi,
                    (unsigned long)info->ContextRecord->Edi,
                    (unsigned long)info->ContextRecord->Ebp,
                    (unsigned long)info->ContextRecord->Esp);
        log_crash_stack((uintptr_t)info->ContextRecord->Esp);
    }
#endif
    runtime_log("The wrapper stopped. Send gd-wrapper.log with this address.");
    return EXCEPTION_EXECUTE_HANDLER;
}

void runtime_log(const char *format, ...) {
    va_list args;
    va_list copy;
    va_start(args, format);
    va_copy(copy, args);
    vfprintf(stdout, format, args);
    fputc('\n', stdout);
    fflush(stdout);
    if (g_log) {
        vfprintf(g_log, format, copy);
        fputc('\n', g_log);
        fflush(g_log);
    }
    va_end(copy);
    va_end(args);
}

void runtime_initialize(const char *log_path) {
    WSADATA winsock;
    unsigned i;
    g_log = fopen(log_path, "wb");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crash_filter);
    g_msvcrt = LoadLibraryA("msvcrt.dll");
    g_ws2 = LoadLibraryA("ws2_32.dll");
    g_opengl = LoadLibraryA("opengl32.dll");
    WSAStartup(MAKEWORD(2, 2), &winsock);
    g_thunks = (unsigned char *)VirtualAlloc(
        NULL, MAX_IMPORTS * THUNK_SIZE, MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE);
    g_callconv_thunks = (unsigned char *)VirtualAlloc(
        NULL, MAX_IMPORTS * CALLCONV_THUNK_SIZE, MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE);
    memset(g_ctype, 0, sizeof(g_ctype));
    memset(g_tolower, 0, sizeof(g_tolower));
    memset(g_toupper, 0, sizeof(g_toupper));
    g_tolower[0] = -1;
    g_toupper[0] = -1;
    for (i = 0; i <= 255; ++i) {
        int value = (int)i;
        unsigned char flags = 0;
        unsigned char c = (unsigned char)value;
        if (isupper(c)) flags |= 0x01;
        if (islower(c)) flags |= 0x02;
        if (isdigit(c)) flags |= 0x04;
        if (isspace(c)) flags |= 0x08;
        if (ispunct(c)) flags |= 0x10;
        if (iscntrl(c)) flags |= 0x20;
        if (isxdigit(c)) flags |= 0x40;
        if (c == ' ') flags |= 0x80;
        /* Bionic indexes all three exported tables as (table + 1)[c]. */
        g_ctype[i + 1] = flags;
        g_tolower[i + 1] = (int16_t)tolower(c);
        g_toupper[i + 1] = (int16_t)toupper(c);
    }
    g_ctype_pointer = g_ctype;
    g_tolower_pointer = g_tolower;
    g_toupper_pointer = g_toupper;
    runtime_log("Geometry Dash Wrapper %s backend=%s", GD_WRAPPER_VERSION, GD_X86_BACKEND_NAME);
    runtime_log("Bionic ABI tables: ctype/tolower/toupper use table+1 indexing");
    runtime_log("Bionic stdio bridge: __sF sentinels translated; fopen streams stay on MSVCRT");
    runtime_log("System DLLs: msvcrt=%s ws2_32=%s opengl32=%s",
                g_msvcrt ? "yes" : "no", g_ws2 ? "yes" : "no",
                g_opengl ? "yes" : "no");
    runtime_log("Compatibility services: zlib, pthread, Winsock/POSIX, Cocos/FMOD audio, and JNI bridges");
}

void runtime_shutdown(void) {
    if (g_thunks) {
        VirtualFree(g_thunks, 0, MEM_RELEASE);
        g_thunks = NULL;
    }
    if (g_callconv_thunks) {
        VirtualFree(g_callconv_thunks, 0, MEM_RELEASE);
        g_callconv_thunks = NULL;
    }
    WSACleanup();
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}

void runtime_set_elf_info(void *base, size_t size, const char *path,
                          const Elf32_Phdr *phdr, uint16_t phnum) {
    g_elf_base = base;
    g_elf_size = size;
    g_elf_path = path;
    g_elf_phdr = phdr;
    g_elf_phnum = phnum;
}

void runtime_set_import_name(uint32_t id, const char *name) {
    size_t length;
    if (id >= MAX_IMPORTS) {
        return;
    }
    length = strlen(name) + 1;
    g_import_names[id] = (char *)malloc(length);
    if (g_import_names[id]) {
        memcpy(g_import_names[id], name, length);
    }
}

static int shim_android_log_print(int priority, const char *tag,
                                  const char *format, ...) {
    va_list args;
    (void)priority;
    if (!tag) {
        tag = "android";
    }
    fprintf(stdout, "[android:%s] ", tag);
    if (g_log) {
        fprintf(g_log, "[android:%s] ", tag);
    }
    va_start(args, format);
    vfprintf(stdout, format ? format : "", args);
    va_end(args);
    fputc('\n', stdout);
    fflush(stdout);
    va_start(args, format);
    if (g_log) {
        vfprintf(g_log, format ? format : "", args);
        fputc('\n', g_log);
        fflush(g_log);
    }
    va_end(args);
    return 0;
}

static void shim_assert2(const char *file, int line, const char *function,
                         const char *expression) {
    runtime_log("FATAL: assertion failed: %s (%s:%d in %s)",
                expression ? expression : "?", file ? file : "?", line,
                function ? function : "?");
    ExitProcess(120);
}

static int *shim_errno(void) {
    return &g_errno_value;
}

static void shim_stack_chk_fail(void) {
    runtime_log("FATAL: Android stack protector failure");
    ExitProcess(121);
}

static int shim_cxa_atexit(void (*function)(void *), void *argument, void *dso) {
    if (g_atexit_count >= MAX_ATEXIT) {
        return -1;
    }
    g_atexit[g_atexit_count].function = function;
    g_atexit[g_atexit_count].argument = argument;
    g_atexit[g_atexit_count].dso = dso;
    ++g_atexit_count;
    return 0;
}

static void shim_cxa_finalize(void *dso) {
    unsigned index = g_atexit_count;
    while (index) {
        AtExitEntry *entry = &g_atexit[--index];
        if (entry->function && (!dso || entry->dso == dso)) {
            void (*function)(void *) = entry->function;
            entry->function = NULL;
            function(entry->argument);
        }
    }
}

static void *shim_dlopen(const char *path, int flags) {
    (void)flags;
    if (!path) {
        return GetModuleHandleA(NULL);
    }
    return LoadLibraryA(path);
}

static void *shim_dlsym(void *module, const char *name) {
    return module && name ? (void *)GetProcAddress((HMODULE)module, name) : NULL;
}

static int shim_dlclose(void *module) {
    return module ? (FreeLibrary((HMODULE)module) ? 0 : -1) : 0;
}

static char *shim_dlerror(void) {
    return "Windows loader error";
}

static int shim_dl_iterate_phdr(int (*callback)(DlPhdrInfo32 *, size_t, void *),
                                void *data) {
    DlPhdrInfo32 info;
    if (!callback || !g_elf_base) {
        return 0;
    }
    memset(&info, 0, sizeof(info));
    info.dlpi_addr = (uint32_t)(uintptr_t)g_elf_base;
    info.dlpi_name = g_elf_path;
    info.dlpi_phdr = g_elf_phdr;
    info.dlpi_phnum = g_elf_phnum;
    return callback(&info, sizeof(info), data);
}

static void *shim_mmap(void *address, uint32_t length, int protection, int flags,
                       int descriptor, int32_t offset) {
    (void)address;
    (void)protection;
    (void)flags;
    (void)descriptor;
    (void)offset;
    return VirtualAlloc(NULL, length, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
}

static int shim_munmap(void *address, uint32_t length) {
    (void)length;
    return VirtualFree(address, 0, MEM_RELEASE) ? 0 : -1;
}

typedef struct {
    int32_t tv_sec;
    int32_t tv_nsec;
} AndroidTimespec;

typedef struct {
    int32_t tv_sec;
    int32_t tv_usec;
} AndroidTimeval;

typedef struct {
    int32_t time;
    uint16_t millitm;
    int16_t timezone;
    int16_t dstflag;
} AndroidTimeb;

typedef struct {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    int32_t tm_gmtoff;
    const char *tm_zone;
} AndroidTm;

static SRWLOCK g_time_conversion_lock = SRWLOCK_INIT;

static uint64_t windows_unix_100ns(void) {
    FILETIME file_time;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&file_time);
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return value.QuadPart - 116444736000000000ULL;
}

static int shim_clock_gettime(int clock_id, AndroidTimespec *value) {
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!value) {
        return -1;
    }
    if (clock_id == 1 || clock_id == 4) {
        if (!frequency.QuadPart) {
            QueryPerformanceFrequency(&frequency);
        }
        QueryPerformanceCounter(&counter);
        value->tv_sec = (int32_t)(counter.QuadPart / frequency.QuadPart);
        value->tv_nsec = (int32_t)((counter.QuadPart % frequency.QuadPart) *
                                   1000000000LL / frequency.QuadPart);
    } else {
        uint64_t ticks = windows_unix_100ns();
        value->tv_sec = (int32_t)(ticks / 10000000ULL);
        value->tv_nsec = (int32_t)((ticks % 10000000ULL) * 100ULL);
    }
    return 0;
}

static int shim_gettimeofday(AndroidTimeval *value, void *timezone) {
    uint64_t ticks;
    (void)timezone;
    if (!value) {
        return -1;
    }
    ticks = windows_unix_100ns();
    value->tv_sec = (int32_t)(ticks / 10000000ULL);
    value->tv_usec = (int32_t)((ticks % 10000000ULL) / 10ULL);
    return 0;
}

static int shim_ftime(AndroidTimeb *value) {
    TIME_ZONE_INFORMATION zone;
    DWORD zone_state;
    LONG bias;
    uint64_t ticks;
    if (!value) {
        g_errno_value = 14;
        return -1;
    }
    ticks = windows_unix_100ns();
    zone_state = GetTimeZoneInformation(&zone);
    bias = zone.Bias;
    if (zone_state == TIME_ZONE_ID_DAYLIGHT) {
        bias += zone.DaylightBias;
    } else if (zone_state == TIME_ZONE_ID_STANDARD) {
        bias += zone.StandardBias;
    }
    value->time = (int32_t)(ticks / 10000000ULL);
    value->millitm = (uint16_t)((ticks % 10000000ULL) / 10000ULL);
    value->timezone = (int16_t)bias;
    value->dstflag = (int16_t)(zone_state == TIME_ZONE_ID_DAYLIGHT);
    return 0;
}

static AndroidTm *shim_gmtime_r(const int32_t *timer, AndroidTm *result) {
    __time32_t windows_time;
    struct tm *windows_result;
    if (!timer || !result) {
        g_errno_value = 22;
        return NULL;
    }
    windows_time = (__time32_t)*timer;
    AcquireSRWLockExclusive(&g_time_conversion_lock);
    windows_result = _gmtime32(&windows_time);
    if (windows_result) {
        memset(result, 0, sizeof(*result));
        result->tm_sec = windows_result->tm_sec;
        result->tm_min = windows_result->tm_min;
        result->tm_hour = windows_result->tm_hour;
        result->tm_mday = windows_result->tm_mday;
        result->tm_mon = windows_result->tm_mon;
        result->tm_year = windows_result->tm_year;
        result->tm_wday = windows_result->tm_wday;
        result->tm_yday = windows_result->tm_yday;
        result->tm_isdst = windows_result->tm_isdst;
        result->tm_zone = "UTC";
    }
    ReleaseSRWLockExclusive(&g_time_conversion_lock);
    return windows_result ? result : NULL;
}

static int32_t shim_clock(void) {
    FILETIME creation;
    FILETIME exit_time;
    FILETIME kernel;
    FILETIME user;
    ULARGE_INTEGER kernel_ticks;
    ULARGE_INTEGER user_ticks;
    uint64_t microseconds;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel,
                         &user)) {
        return -1;
    }
    kernel_ticks.LowPart = kernel.dwLowDateTime;
    kernel_ticks.HighPart = kernel.dwHighDateTime;
    user_ticks.LowPart = user.dwLowDateTime;
    user_ticks.HighPart = user.dwHighDateTime;
    microseconds = (kernel_ticks.QuadPart + user_ticks.QuadPart) / 10u;
    return (int32_t)(uint32_t)microseconds;
}

static int shim_usleep(uint32_t microseconds) {
    Sleep((microseconds + 999u) / 1000u);
    return 0;
}

static size_t shim_strlcat(char *destination, const char *source, size_t size) {
    size_t d = 0;
    size_t s = strlen(source);
    while (d < size && destination[d]) {
        ++d;
    }
    if (d < size) {
        size_t copy = s < size - d - 1 ? s : size - d - 1;
        memcpy(destination + d, source, copy);
        destination[d + copy] = 0;
    }
    return d + s;
}

static void *shim_memrchr(const void *memory, int value, size_t size) {
    const unsigned char *bytes = (const unsigned char *)memory;
    while (size) {
        if (bytes[--size] == (unsigned char)value) {
            return (void *)(bytes + size);
        }
    }
    return NULL;
}

static char *shim_basename(char *path) {
    char *slash;
    char *backslash;
    if (!path || !*path) {
        return path;
    }
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
    return slash ? slash + 1 : path;
}

static char *shim_strtok_r(char *text, const char *delimiters,
                           char **save_pointer) {
    char *token;
    char *end;
    if (!delimiters || !save_pointer) {
        g_errno_value = 22;
        return NULL;
    }
    token = text ? text : *save_pointer;
    if (!token) return NULL;
    token += strspn(token, delimiters);
    if (!*token) {
        *save_pointer = token;
        return NULL;
    }
    end = token + strcspn(token, delimiters);
    if (*end) {
        *end++ = 0;
    }
    *save_pointer = end;
    return token;
}

static long long shim_strtoll(const char *text, char **end, int base) {
    static LONG logged;
    long long result;
    int conversion_error;
    errno = 0;
    result = strtoll(text, end, base);
    conversion_error = errno;
    if (conversion_error) g_errno_value = conversion_error;
    if (InterlockedCompareExchange(&logged, 1, 0) == 0) {
        runtime_log("Network HTTP parser: 64-bit signed conversion bridge active");
    }
    return result;
}

static unsigned long long shim_strtoull(const char *text, char **end,
                                        int base) {
    unsigned long long result;
    int conversion_error;
    errno = 0;
    result = strtoull(text, end, base);
    conversion_error = errno;
    if (conversion_error) g_errno_value = conversion_error;
    return result;
}

static const char *android_error_string(int error) {
    switch (error) {
    case 0: return "Success";
    case 1: return "Operation not permitted";
    case 2: return "No such file or directory";
    case 4: return "Interrupted system call";
    case 5: return "Input/output error";
    case 9: return "Bad file descriptor";
    case 11: return "Resource temporarily unavailable";
    case 12: return "Out of memory";
    case 13: return "Permission denied";
    case 14: return "Bad address";
    case 22: return "Invalid argument";
    case 24: return "Too many open files";
    case 32: return "Broken pipe";
    case 34: return "Result too large";
    case 88: return "Socket operation on non-socket";
    case 89: return "Destination address required";
    case 90: return "Message too long";
    case 91: return "Protocol wrong type for socket";
    case 92: return "Protocol option not available";
    case 93: return "Protocol not supported";
    case 94: return "Socket type not supported";
    case 95: return "Operation not supported";
    case 96: return "Protocol family not supported";
    case 97: return "Address family not supported";
    case 98: return "Address already in use";
    case 99: return "Cannot assign requested address";
    case 100: return "Network is down";
    case 101: return "Network is unreachable";
    case 102: return "Network dropped connection on reset";
    case 103: return "Software caused connection abort";
    case 104: return "Connection reset by peer";
    case 105: return "No buffer space available";
    case 106: return "Socket is already connected";
    case 107: return "Socket is not connected";
    case 108: return "Cannot send after socket shutdown";
    case 109: return "Too many references";
    case 110: return "Connection timed out";
    case 111: return "Connection refused";
    case 112: return "Host is down";
    case 113: return "No route to host";
    case 114: return "Operation already in progress";
    case 115: return "Operation now in progress";
    default: return "Unknown error";
    }
}

static char *shim_strerror(int error) {
    return (char *)android_error_string(error);
}

/*
 * The bundled fmt code was compiled for GNU strerror_r (char * return), while
 * the bundled curl code only needs a populated buffer and tolerates a nonzero
 * return. Returning the supplied buffer therefore satisfies both call sites;
 * returning POSIX's integer zero would make fmt store a null string pointer and
 * is the cause of the observed song-download exception after a failed connect.
 */
static char *shim_strerror_r(int error, char *buffer, size_t size) {
    static LONG logged;
    const char *message = android_error_string(error);
    size_t length = strlen(message);
    if (InterlockedCompareExchange(&logged, 1, 0) == 0) {
        runtime_log("Network error text: GNU/POSIX-compatible strerror_r buffer bridge active");
    }
    if (!buffer || !size) {
        return (char *)message;
    }
    if (length >= size) {
        length = size - 1;
        g_errno_value = 34;
    }
    memcpy(buffer, message, length);
    buffer[length] = 0;
    return buffer;
}

static const char *shim_gai_strerror(int error) {
    if (error < 0 && error >= -14) error = -error;
    switch (error) {
    case WSAHOST_NOT_FOUND: return "Name or service not known";
    case WSATRY_AGAIN: return "Temporary failure in name resolution";
    case WSANO_RECOVERY: return "Non-recoverable name resolution failure";
    case WSANO_DATA: return "No address associated with host";
    case WSAEAFNOSUPPORT: return "Address family not supported";
    case WSAESOCKTNOSUPPORT: return "Socket type not supported";
    case 1: return "Address family for hostname not supported";
    case 2: return "Temporary failure in name resolution";
    case 3: return "Invalid value for ai_flags";
    case 4: return "Non-recoverable name resolution failure";
    case 5: return "Address family not supported";
    case 6: return "Out of memory";
    case 7: return "No address associated with hostname";
    case 8: return "Name or service not known";
    case 9: return "Service not supported for socket type";
    case 10: return "Socket type not supported";
    default: return "Unknown address resolution error";
    }
}

static int shim_sigaction(int signal_number, const void *action,
                          void *old_action) {
    static LONG logged;
    if (signal_number <= 0 || signal_number >= ANDROID_SIGNAL_COUNT) {
        g_errno_value = 22;
        return -1;
    }
    AcquireSRWLockExclusive(&g_signal_lock);
    if (old_action) {
        memcpy(old_action, &g_signal_actions[signal_number],
               sizeof(AndroidSigaction));
    }
    if (action) {
        memcpy(&g_signal_actions[signal_number], action,
               sizeof(AndroidSigaction));
    }
    ReleaseSRWLockExclusive(&g_signal_lock);
    if (InterlockedCompareExchange(&logged, 1, 0) == 0) {
        runtime_log("POSIX signals: preserving Android 16-byte sigaction state (SIGPIPE is virtual)");
    }
    return 0;
}

static void *shim_bsd_signal(int signal_number, void *handler) {
    AndroidSigaction action;
    AndroidSigaction old_action;
    memset(&action, 0, sizeof(action));
    action.words[0] = (uint32_t)(uintptr_t)handler;
    if (shim_sigaction(signal_number, &action, &old_action) != 0) {
        return (void *)(intptr_t)-1;
    }
    return (void *)(uintptr_t)old_action.words[0];
}

static int shim_sigprocmask(int how, const uint32_t *set, uint32_t *old_set) {
    uint32_t previous;
    AcquireSRWLockExclusive(&g_signal_lock);
    previous = g_signal_mask;
    if (old_set) *old_set = previous;
    if (set) {
        if (how == 0) {
            g_signal_mask |= *set;
        } else if (how == 1) {
            g_signal_mask &= ~*set;
        } else if (how == 2) {
            g_signal_mask = *set;
        } else {
            ReleaseSRWLockExclusive(&g_signal_lock);
            g_errno_value = 22;
            return -1;
        }
    }
    ReleaseSRWLockExclusive(&g_signal_lock);
    return 0;
}

static const char *translate_game_path(const char *path, char *translated,
                                       size_t capacity) {
    const char *slash;
    const char *backslash;
    const char *name;
    if (!path) return path;
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    name = slash && (!backslash || slash > backslash) ? slash + 1
                                                       : backslash ? backslash + 1
                                                                   : path;
    if (storage_is_game_file_name(name)) {
        snprintf(translated, capacity, "save/%s", name);
        return translated;
    }
    if (strncmp(path, "/save/", 6) == 0 || strcmp(path, "/save") == 0) {
        return path + 1;
    }
    if (path[0] == '/' && isalpha((unsigned char)path[1]) && path[2] == ':') {
        return path + 1;
    }
    return path;
}

static void *shim_fopen(const char *path, const char *mode) {
    typedef void *(__cdecl *CrtFopenFunction)(const char *, const char *);
    static CrtFopenFunction function;
    char translated[MAX_PATH * 2];
    const char *resolved = translate_game_path(path, translated,
                                               sizeof(translated));
    if (!function && g_msvcrt) {
        function = (CrtFopenFunction)GetProcAddress(g_msvcrt, "fopen");
    }
    if (resolved == translated) {
        runtime_log("Game data: fopen %s (%s)", resolved,
                    mode ? mode : "<null mode>");
    }
    return function ? function(resolved, mode) : NULL;
}

/*
 * Older Bionic exposes stdin/stdout/stderr as three 84-byte __sFILE objects
 * in __sF. Android code performs the pointer arithmetic itself, so the ELF
 * cannot be pointed at the smaller MSVCRT FILE array. Map those three sentinel
 * addresses at every imported stdio boundary while passing ordinary streams
 * returned by shim_fopen through unchanged.
 */
static int is_bionic_standard_stream(const void *stream) {
    const unsigned char *value = (const unsigned char *)stream;
    return value == g_sF || value == g_sF + 84 || value == g_sF + 168;
}

static FILE *wrapper_standard_stream(void *stream) {
    unsigned char *value = (unsigned char *)stream;
    if (!stream) return NULL;
    if (value == g_sF) return stdin;
    if (value == g_sF + 84) return stdout;
    if (value == g_sF + 168) return stderr;
    return NULL;
}

static int shim_fclose(void *stream) {
    typedef int (__cdecl *Function)(void *);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        FILE *resolved = wrapper_standard_stream(stream);
        return resolved ? fflush(resolved) : EOF;
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "fclose");
    }
    return function && stream ? function(stream) : EOF;
}

static int shim_fflush(void *stream) {
    typedef int (__cdecl *Function)(void *);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return fflush(wrapper_standard_stream(stream));
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "fflush");
    }
    return function ? function(stream) : EOF;
}

static char *shim_fgets(char *buffer, int count, void *stream) {
    typedef char *(__cdecl *Function)(char *, int, void *);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return fgets(buffer, count, wrapper_standard_stream(stream));
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "fgets");
    }
    return function ? function(buffer, count, stream) : NULL;
}

static int shim_fprintf(void *stream, const char *format, ...) {
    typedef int (__cdecl *Function)(void *, const char *, va_list);
    static Function function;
    int result;
    va_list arguments;
    va_start(arguments, format);
    if (is_bionic_standard_stream(stream)) {
        result = vfprintf(wrapper_standard_stream(stream), format, arguments);
    } else {
        if (!function && g_msvcrt) {
            function = (Function)GetProcAddress(g_msvcrt, "vfprintf");
        }
        result = function ? function(stream, format, arguments) : -1;
    }
    va_end(arguments);
    return result;
}

static int shim_fputc(int character, void *stream) {
    typedef int (__cdecl *Function)(int, void *);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return fputc(character, wrapper_standard_stream(stream));
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "fputc");
    }
    return function ? function(character, stream) : EOF;
}

static int shim_fputs(const char *text, void *stream) {
    typedef int (__cdecl *Function)(const char *, void *);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return fputs(text, wrapper_standard_stream(stream));
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "fputs");
    }
    return function ? function(text, stream) : EOF;
}

static size_t shim_fread(void *buffer, size_t size, size_t count,
                         void *stream) {
    typedef size_t (__cdecl *Function)(void *, size_t, size_t, void *);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return fread(buffer, size, count, wrapper_standard_stream(stream));
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "fread");
    }
    return function ? function(buffer, size, count, stream) : 0;
}

static int shim_fseek(void *stream, long offset, int origin) {
    typedef int (__cdecl *Function)(void *, long, int);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return fseek(wrapper_standard_stream(stream), offset, origin);
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "fseek");
    }
    return function ? function(stream, offset, origin) : -1;
}

static long shim_ftell(void *stream) {
    typedef long (__cdecl *Function)(void *);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return ftell(wrapper_standard_stream(stream));
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "ftell");
    }
    return function ? function(stream) : -1;
}

static size_t shim_fwrite(const void *buffer, size_t size, size_t count,
                          void *stream) {
    typedef size_t (__cdecl *Function)(const void *, size_t, size_t, void *);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return fwrite(buffer, size, count, wrapper_standard_stream(stream));
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "fwrite");
    }
    return function ? function(buffer, size, count, stream) : 0;
}

static int shim_setvbuf(void *stream, char *buffer, int mode, size_t size) {
    typedef int (__cdecl *Function)(void *, char *, int, size_t);
    static Function function;
    int windows_mode;
    switch (mode) {
    case 0: windows_mode = _IOFBF; break;
    case 1: windows_mode = _IOLBF; break;
    case 2: windows_mode = _IONBF; break;
    default: return -1;
    }
    if (is_bionic_standard_stream(stream)) {
        return setvbuf(wrapper_standard_stream(stream), buffer, windows_mode,
                       size);
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "setvbuf");
    }
    return function ? function(stream, buffer, windows_mode, size) : -1;
}

static int shim_ungetc(int character, void *stream) {
    typedef int (__cdecl *Function)(int, void *);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return ungetc(character, wrapper_standard_stream(stream));
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "ungetc");
    }
    return function ? function(character, stream) : EOF;
}

static int shim_vfprintf(void *stream, const char *format, va_list arguments) {
    typedef int (__cdecl *Function)(void *, const char *, va_list);
    static Function function;
    if (is_bionic_standard_stream(stream)) {
        return vfprintf(wrapper_standard_stream(stream), format, arguments);
    }
    if (!function && g_msvcrt) {
        function = (Function)GetProcAddress(g_msvcrt, "vfprintf");
    }
    return function ? function(stream, format, arguments) : -1;
}

static int shim_rename(const char *old_path, const char *new_path) {
    typedef int (__cdecl *CrtRenameFunction)(const char *, const char *);
    static CrtRenameFunction function;
    char old_translated[MAX_PATH * 2];
    char new_translated[MAX_PATH * 2];
    const char *old_resolved = translate_game_path(old_path, old_translated,
                                                   sizeof(old_translated));
    const char *new_resolved = translate_game_path(new_path, new_translated,
                                                   sizeof(new_translated));
    if (!function && g_msvcrt) {
        function = (CrtRenameFunction)GetProcAddress(g_msvcrt, "rename");
    }
    if (old_resolved == old_translated || new_resolved == new_translated) {
        runtime_log("Game data: rename %s -> %s", old_resolved, new_resolved);
    }
    return function ? function(old_resolved, new_resolved) : -1;
}

static int shim_access(const char *path, int mode) {
    char translated[MAX_PATH * 2];
    return _access(translate_game_path(path, translated, sizeof(translated)),
                   mode);
}

static int shim_chmod(const char *path, int mode) {
    char translated[MAX_PATH * 2];
    return _chmod(translate_game_path(path, translated, sizeof(translated)),
                  mode);
}

static int shim_remove(const char *path) {
    typedef int (__cdecl *CrtPathFunction)(const char *);
    static CrtPathFunction function;
    char translated[MAX_PATH * 2];
    if (!function && g_msvcrt) {
        function = (CrtPathFunction)GetProcAddress(g_msvcrt, "remove");
    }
    return function ? function(translate_game_path(path, translated,
                                                   sizeof(translated)))
                    : -1;
}

static int shim_unlink(const char *path) {
    typedef int (__cdecl *CrtPathFunction)(const char *);
    static CrtPathFunction function;
    char translated[MAX_PATH * 2];
    if (!function && g_msvcrt) {
        function = (CrtPathFunction)GetProcAddress(g_msvcrt, "_unlink");
        if (!function) {
            function = (CrtPathFunction)GetProcAddress(g_msvcrt, "unlink");
        }
    }
    return function ? function(translate_game_path(path, translated,
                                                   sizeof(translated)))
                    : -1;
}

static void shim_srand48(long seed) {
    g_lcg_state = (uint32_t)seed;
}

static long shim_lrand48(void) {
    g_lcg_state = g_lcg_state * 1103515245u + 12345u;
    return (long)((g_lcg_state >> 1) & 0x7fffffffu);
}

static uint32_t shim_arc4random(void) {
    /* Game-side random selection does not require cryptographic entropy. */
    g_lcg_state ^= g_lcg_state << 13;
    g_lcg_state ^= g_lcg_state >> 17;
    g_lcg_state ^= g_lcg_state << 5;
    return g_lcg_state;
}

enum {
    WCTYPE_ALNUM = 1,
    WCTYPE_ALPHA,
    WCTYPE_BLANK,
    WCTYPE_CNTRL,
    WCTYPE_DIGIT,
    WCTYPE_GRAPH,
    WCTYPE_LOWER,
    WCTYPE_PRINT,
    WCTYPE_PUNCT,
    WCTYPE_SPACE,
    WCTYPE_UPPER,
    WCTYPE_XDIGIT
};

static uint32_t shim_wctype(const char *name) {
    static const char *const names[] = {
        "", "alnum", "alpha", "blank", "cntrl", "digit", "graph",
        "lower", "print", "punct", "space", "upper", "xdigit"
    };
    uint32_t index;
    for (index = 1; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (name && strcmp(name, names[index]) == 0) {
            return index;
        }
    }
    return 0;
}

static int shim_iswctype(uint32_t character, uint32_t type) {
    unsigned char value;
    if (character > 0x7f) {
        return 0;
    }
    value = (unsigned char)character;
    switch (type) {
    case WCTYPE_ALNUM: return isalnum(value) != 0;
    case WCTYPE_ALPHA: return isalpha(value) != 0;
    case WCTYPE_BLANK: return value == ' ' || value == '\t';
    case WCTYPE_CNTRL: return iscntrl(value) != 0;
    case WCTYPE_DIGIT: return isdigit(value) != 0;
    case WCTYPE_GRAPH: return isgraph(value) != 0;
    case WCTYPE_LOWER: return islower(value) != 0;
    case WCTYPE_PRINT: return isprint(value) != 0;
    case WCTYPE_PUNCT: return ispunct(value) != 0;
    case WCTYPE_SPACE: return isspace(value) != 0;
    case WCTYPE_UPPER: return isupper(value) != 0;
    case WCTYPE_XDIGIT: return isxdigit(value) != 0;
    default: return 0;
    }
}

static uint32_t shim_towlower(uint32_t character) {
    return character <= 0x7f ? (uint32_t)tolower((unsigned char)character) : character;
}

static uint32_t shim_towupper(uint32_t character) {
    return character <= 0x7f ? (uint32_t)toupper((unsigned char)character) : character;
}

static int shim_wctob(uint32_t character) {
    return character <= 0xff ? (int)character : -1;
}

static uint32_t shim_btowc(int character) {
    return character == -1 ? UINT32_MAX : (uint32_t)(unsigned char)character;
}

static size_t shim_wcslen(const uint32_t *string) {
    const uint32_t *cursor = string;
    while (*cursor) {
        ++cursor;
    }
    return (size_t)(cursor - string);
}

static uint32_t *shim_wmemchr(const uint32_t *memory, uint32_t value, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        if (memory[index] == value) {
            return (uint32_t *)(memory + index);
        }
    }
    return NULL;
}

static int shim_wmemcmp(const uint32_t *first, const uint32_t *second, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        if (first[index] != second[index]) {
            return first[index] < second[index] ? -1 : 1;
        }
    }
    return 0;
}

static uint32_t *shim_wmemcpy(uint32_t *destination, const uint32_t *source,
                              size_t count) {
    return (uint32_t *)memcpy(destination, source, count * sizeof(uint32_t));
}

static uint32_t *shim_wmemmove(uint32_t *destination, const uint32_t *source,
                               size_t count) {
    return (uint32_t *)memmove(destination, source, count * sizeof(uint32_t));
}

static uint32_t *shim_wmemset(uint32_t *destination, uint32_t value, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        destination[index] = value;
    }
    return destination;
}

static int shim_getdtablesize(void) {
    return 2048;
}

/* Android applications normally run under an app-specific, non-root UID.
 * OpenSSL mixes these values into its entropy state after reading urandom.
 * A stable synthetic app identity is sufficient on Windows and avoids
 * routing these real call sites through the generic zero-return stub. */
static unsigned shim_get_app_id(void) {
    return 10000u;
}

static int is_android_random_path(const char *path) {
    return path && (_stricmp(path, "/dev/urandom") == 0 ||
                    _stricmp(path, "/dev/random") == 0 ||
                    _stricmp(path, "/dev/srandom") == 0);
}

static int android_open_flags_to_windows(int flags) {
    int result = flags & 3; /* O_RDONLY/O_WRONLY/O_RDWR are ABI-compatible. */
    if (flags & 0x0040) result |= _O_CREAT;
    if (flags & 0x0080) result |= _O_EXCL;
    if (flags & 0x0200) result |= _O_TRUNC;
    if (flags & 0x0400) result |= _O_APPEND;
    result |= _O_BINARY;
    return result;
}

static int shim_open(const char *path, int flags, ...) {
    char translated[MAX_PATH * 2];
    const char *resolved;
    int mode = 0;
    if (is_android_random_path(path)) {
        InitOnceExecuteOnce(&g_random_once, initialize_system_random, NULL,
                            NULL);
        if (!g_system_random) {
            g_errno_value = 5;
            return -1;
        }
        InterlockedIncrement(&g_random_open_count);
        return ANDROID_RANDOM_FD;
    }
    resolved = translate_game_path(path, translated, sizeof(translated));
    if (flags & 0x0040) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
        return _open(resolved, android_open_flags_to_windows(flags), mode);
    }
    return _open(resolved, android_open_flags_to_windows(flags));
}

static int shim_read(int descriptor, void *buffer, size_t length) {
    if (descriptor == ANDROID_RANDOM_FD &&
        InterlockedCompareExchange(&g_random_open_count, 0, 0) > 0) {
        if (length > INT_MAX) length = INT_MAX;
        if (!fill_system_random(buffer, length)) {
            g_errno_value = 5;
            return -1;
        }
        return (int)length;
    }
    if (length > INT_MAX) length = INT_MAX;
    return _read(descriptor, buffer, (unsigned)length);
}

static int shim_fstat(int descriptor, void *status) {
    if (descriptor == ANDROID_RANDOM_FD &&
        InterlockedCompareExchange(&g_random_open_count, 0, 0) > 0) {
        if (!status) {
            g_errno_value = 14;
            return -1;
        }
        /* Legacy Bionic/i386 struct stat is 96 bytes. RAND_poll only compares
           the device/inode fields for its second and third candidate; the
           first secure-random device can safely use a zeroed record. */
        memset(status, 0, 96);
        return 0;
    }
    return _fstat32(descriptor, (struct _stat32 *)status);
}

/*
 * Bionic/i386 calls imported functions with cdecl. Winsock uses stdcall on
 * 32-bit Windows, and several constants/layout details differ as well. These
 * wrappers are deliberately normal C functions: the compiler emits the
 * stdcall calls to Winsock while presenting a cdecl surface to the ELF.
 */
typedef struct AndroidAddrInfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    uint32_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct AndroidAddrInfo *ai_next;
} AndroidAddrInfo;

typedef struct {
    int descriptor;
    short events;
    short returned_events;
} AndroidPollFd;

typedef struct {
    void *base;
    uint32_t length;
} AndroidIovec;

enum {
    ANDROID_POLLIN = 0x0001,
    ANDROID_POLLPRI = 0x0002,
    ANDROID_POLLOUT = 0x0004,
    ANDROID_POLLERR = 0x0008,
    ANDROID_POLLHUP = 0x0010,
    ANDROID_POLLNVAL = 0x0020,
    ANDROID_POLLRDNORM = 0x0040,
    ANDROID_POLLRDBAND = 0x0080,
    ANDROID_POLLWRNORM = 0x0100,
    ANDROID_POLLWRBAND = 0x0200
};

static int set_socket_error(void) {
    g_errno_value = gd_net_wsa_error_to_android(WSAGetLastError());
    return -1;
}

static void free_android_addrinfo(AndroidAddrInfo *entry) {
    while (entry) {
        AndroidAddrInfo *next = entry->ai_next;
        free(entry->ai_canonname);
        free(entry->ai_addr);
        free(entry);
        entry = next;
    }
}

static int shim_getaddrinfo(const char *node, const char *service,
                            const AndroidAddrInfo *hints,
                            AndroidAddrInfo **result) {
    static LONG trace_count;
    ADDRINFOA windows_hints;
    ADDRINFOA *windows_result = NULL;
    ADDRINFOA *cursor;
    AndroidAddrInfo *head = NULL;
    AndroidAddrInfo **tail = &head;
    unsigned ipv4_count = 0;
    unsigned ipv6_count = 0;
    int first_family = 0;
    int status;
    if (!result) {
        return EAI_FAIL;
    }
    *result = NULL;
    memset(&windows_hints, 0, sizeof(windows_hints));
    if (hints) {
        windows_hints.ai_flags = gd_net_android_ai_flags_to_host(hints->ai_flags);
        windows_hints.ai_family = gd_net_android_family_to_host(hints->ai_family);
        windows_hints.ai_socktype = hints->ai_socktype;
        windows_hints.ai_protocol = hints->ai_protocol;
    }
    runtime_log("Network DNS: %s", node ? node : "<local>");
    status = getaddrinfo(node, service, hints ? &windows_hints : NULL,
                         &windows_result);
    if (status != 0) {
        runtime_log("Network DNS failed: %d", status);
        return status;
    }
    for (cursor = windows_result; cursor; cursor = cursor->ai_next) {
        AndroidAddrInfo *entry = (AndroidAddrInfo *)calloc(1, sizeof(*entry));
        if (!entry) {
            free_android_addrinfo(head);
            freeaddrinfo(windows_result);
            return EAI_MEMORY;
        }
        entry->ai_flags = cursor->ai_flags;
        entry->ai_family = gd_net_host_family_to_android(cursor->ai_family);
        if (!first_family) first_family = entry->ai_family;
        if (entry->ai_family == AF_INET) ++ipv4_count;
        if (entry->ai_family == GD_ANDROID_AF_INET6) ++ipv6_count;
        entry->ai_socktype = cursor->ai_socktype;
        entry->ai_protocol = cursor->ai_protocol;
        entry->ai_addrlen = (uint32_t)cursor->ai_addrlen;
        if (cursor->ai_addr && cursor->ai_addrlen) {
            entry->ai_addr = (struct sockaddr *)malloc(cursor->ai_addrlen);
            if (!entry->ai_addr) {
                free(entry);
                free_android_addrinfo(head);
                freeaddrinfo(windows_result);
                return EAI_MEMORY;
            }
            memcpy(entry->ai_addr, cursor->ai_addr, cursor->ai_addrlen);
            entry->ai_addr->sa_family =
                (ADDRESS_FAMILY)entry->ai_family;
        }
        if (cursor->ai_canonname) {
            size_t length = strlen(cursor->ai_canonname) + 1;
            entry->ai_canonname = (char *)malloc(length);
            if (!entry->ai_canonname) {
                free(entry->ai_addr);
                free(entry);
                free_android_addrinfo(head);
                freeaddrinfo(windows_result);
                return EAI_MEMORY;
            }
            memcpy(entry->ai_canonname, cursor->ai_canonname, length);
        }
        *tail = entry;
        tail = &entry->ai_next;
    }
    freeaddrinfo(windows_result);
    *result = head;
    {
        LONG trace = InterlockedIncrement(&trace_count);
        if (trace <= 64) {
            runtime_log("Network DNS result #%ld: first=%s IPv4=%u IPv6=%u",
                        (long)trace,
                        first_family == GD_ANDROID_AF_INET6 ? "IPv6" :
                        first_family == AF_INET ? "IPv4" : "other",
                        ipv4_count, ipv6_count);
        }
    }
    return 0;
}

static void shim_freeaddrinfo(AndroidAddrInfo *result) {
    free_android_addrinfo(result);
}

static void socket_trace_reset(int descriptor);

static int shim_socket(int family, int type, int protocol) {
    static LONG trace_count;
    int windows_type = type & 0x0f;
    SOCKET descriptor = socket(gd_net_android_family_to_host(family), windows_type,
                               protocol);
    if (descriptor == INVALID_SOCKET) {
        return set_socket_error();
    }
    if (type & GD_ANDROID_SOCK_NONBLOCK) {
        if (gd_net_set_nonblocking(descriptor, 1) != 0) {
            int error = WSAGetLastError();
            closesocket(descriptor);
            WSASetLastError(error);
            return set_socket_error();
        }
    }
    /* Windows handles are non-inheritable by default; SOCK_CLOEXEC needs no
       additional operation. */
    (void)GD_ANDROID_SOCK_CLOEXEC;
    socket_trace_reset((int)(uintptr_t)descriptor);
    {
        LONG trace = InterlockedIncrement(&trace_count);
        if (trace <= 64) {
            runtime_log("Network socket #%ld: fd=%d family=%d type=0x%x protocol=%d",
                        (long)trace, (int)(uintptr_t)descriptor, family, type,
                        protocol);
        }
    }
    return (int)(uintptr_t)descriptor;
}

static int shim_bind(int descriptor, const struct sockaddr *address, int length) {
    struct sockaddr_storage storage;
    int windows_length = length;
    const struct sockaddr *windows_address =
        gd_net_sockaddr_to_host(address, length, &storage, &windows_length);
    if (bind((SOCKET)(uintptr_t)(uint32_t)descriptor, windows_address,
             windows_length) == SOCKET_ERROR) {
        return set_socket_error();
    }
    return 0;
}

static int shim_connect(int descriptor, const struct sockaddr *address, int length) {
    static LONG trace_count;
    struct sockaddr_storage storage;
    int windows_length = length;
    LONG trace = InterlockedIncrement(&trace_count);
    int raw_family = address ? (int)address->sa_family : -1;
    const char *family = address &&
                                 (address->sa_family == GD_ANDROID_AF_INET6 ||
                                  address->sa_family == AF_INET6)
                             ? "IPv6"
                             : address && address->sa_family == AF_INET
                                   ? "IPv4"
                                   : "other";
    const struct sockaddr *windows_address =
        gd_net_sockaddr_to_host(address, length, &storage, &windows_length);
    if (connect((SOCKET)(uintptr_t)(uint32_t)descriptor, windows_address,
                windows_length) == SOCKET_ERROR) {
        int windows_error = WSAGetLastError();
        int android_error = gd_net_wsa_error_to_android(windows_error);
        if (trace <= 64) {
            if (windows_error == WSAEWOULDBLOCK ||
                windows_error == WSAEINPROGRESS) {
                runtime_log("Network connect #%ld: fd=%d %s pending "
                            "(family=%d length=%d)",
                            (long)trace, descriptor, family, raw_family,
                            length);
            } else {
                runtime_log("Network connect #%ld: fd=%d %s failed "
                            "(family=%d length=%d Winsock=%d Android errno=%d)",
                            (long)trace, descriptor, family, raw_family,
                            length, windows_error, android_error);
            }
        }
        g_errno_value = android_error;
        WSASetLastError(windows_error);
        return -1;
    }
    if (trace <= 64) {
        runtime_log("Network connect #%ld: fd=%d %s connected immediately "
                    "(family=%d length=%d)",
                    (long)trace, descriptor, family, raw_family, length);
    }
    return 0;
}

static int shim_listen(int descriptor, int backlog) {
    if (listen((SOCKET)(uintptr_t)(uint32_t)descriptor, backlog) == SOCKET_ERROR) {
        return set_socket_error();
    }
    return 0;
}

static int shim_accept(int descriptor, struct sockaddr *address, int *length) {
    struct sockaddr_storage storage;
    int windows_length = sizeof(storage);
    SOCKET accepted = accept((SOCKET)(uintptr_t)(uint32_t)descriptor,
                             address ? (struct sockaddr *)&storage : NULL,
                             address ? &windows_length : NULL);
    if (accepted == INVALID_SOCKET) {
        return set_socket_error();
    }
    if (address && length) {
        gd_net_sockaddr_from_host(address, length, (struct sockaddr *)&storage,
                             windows_length);
    }
    return (int)(uintptr_t)accepted;
}

static int shim_get_socket_name(int descriptor, struct sockaddr *address,
                                int *length, int peer) {
    struct sockaddr_storage storage;
    int windows_length = sizeof(storage);
    int status;
    if (!address || !length) {
        g_errno_value = 22;
        return -1;
    }
    status = peer
                 ? getpeername((SOCKET)(uintptr_t)(uint32_t)descriptor,
                               (struct sockaddr *)&storage, &windows_length)
                 : getsockname((SOCKET)(uintptr_t)(uint32_t)descriptor,
                               (struct sockaddr *)&storage, &windows_length);
    if (status == SOCKET_ERROR) {
        return set_socket_error();
    }
    gd_net_sockaddr_from_host(address, length, (struct sockaddr *)&storage,
                         windows_length);
    return 0;
}

static int shim_getpeername(int descriptor, struct sockaddr *address, int *length) {
    return shim_get_socket_name(descriptor, address, length, 1);
}

static int shim_getsockname(int descriptor, struct sockaddr *address, int *length) {
    return shim_get_socket_name(descriptor, address, length, 0);
}

static int socket_option_to_windows(int level, int option) {
    if (level != GD_ANDROID_SOL_SOCKET) {
        return option;
    }
    switch (option) {
    case 1: return SO_DEBUG;
    case 2: return SO_REUSEADDR;
    case 3: return SO_TYPE;
    case 4: return SO_ERROR;
    case 5: return SO_DONTROUTE;
    case 6: return SO_BROADCAST;
    case 7: return SO_SNDBUF;
    case 8: return SO_RCVBUF;
    case 9: return SO_KEEPALIVE;
    case 10: return SO_OOBINLINE;
    case 13: return SO_LINGER;
    case 18: return SO_RCVLOWAT;
    case 19: return SO_SNDLOWAT;
    case 20: return SO_RCVTIMEO;
    case 21: return SO_SNDTIMEO;
    default: return -1;
    }
}

static int shim_getsockopt(int descriptor, int level, int option,
                           void *value, int *length) {
    int windows_level = level == GD_ANDROID_SOL_SOCKET ? SOL_SOCKET : level;
    int windows_option = socket_option_to_windows(level, option);
    int status;
    if (windows_option < 0 || !value || !length) {
        g_errno_value = 92;
        return -1;
    }
    if (level == GD_ANDROID_SOL_SOCKET && (option == 20 || option == 21)) {
        DWORD milliseconds = 0;
        int windows_length = sizeof(milliseconds);
        AndroidTimeval timeout;
        status = getsockopt((SOCKET)(uintptr_t)(uint32_t)descriptor,
                            windows_level, windows_option,
                            (char *)&milliseconds, &windows_length);
        if (status == SOCKET_ERROR) return set_socket_error();
        timeout.tv_sec = (int32_t)(milliseconds / 1000u);
        timeout.tv_usec = (int32_t)((milliseconds % 1000u) * 1000u);
        if (*length > (int)sizeof(timeout)) *length = sizeof(timeout);
        memcpy(value, &timeout, (size_t)*length);
        return 0;
    }
    status = getsockopt((SOCKET)(uintptr_t)(uint32_t)descriptor, windows_level,
                        windows_option, (char *)value, length);
    if (status == SOCKET_ERROR) {
        return set_socket_error();
    }
    if (level == GD_ANDROID_SOL_SOCKET && option == 4 &&
        *length >= (int)sizeof(int)) {
        *(int *)value = gd_net_wsa_error_to_android(*(int *)value);
    }
    return 0;
}

static int shim_setsockopt(int descriptor, int level, int option,
                           const void *value, int length) {
    int windows_level = level == GD_ANDROID_SOL_SOCKET ? SOL_SOCKET : level;
    int windows_option = socket_option_to_windows(level, option);
    const char *windows_value = (const char *)value;
    int windows_length = length;
    DWORD milliseconds;
    LINGER windows_linger;
    if (level == GD_ANDROID_SOL_SOCKET && option == 15) {
        return 0; /* SO_REUSEPORT has no exact Winsock counterpart. */
    }
    if (windows_option < 0 || !value) {
        g_errno_value = 92;
        return -1;
    }
    if (level == GD_ANDROID_SOL_SOCKET && (option == 20 || option == 21) &&
        length >= (int)sizeof(AndroidTimeval)) {
        const AndroidTimeval *timeout = (const AndroidTimeval *)value;
        uint64_t total = (uint64_t)(timeout->tv_sec > 0 ? timeout->tv_sec : 0) *
                             1000u +
                         (uint64_t)(timeout->tv_usec > 0 ? timeout->tv_usec : 0) /
                             1000u;
        milliseconds = total > UINT32_MAX ? UINT32_MAX : (DWORD)total;
        windows_value = (const char *)&milliseconds;
        windows_length = sizeof(milliseconds);
    } else if (level == GD_ANDROID_SOL_SOCKET && option == 13 &&
               length >= 2 * (int)sizeof(int)) {
        const int *android_linger = (const int *)value;
        windows_linger.l_onoff = (u_short)(android_linger[0] != 0);
        windows_linger.l_linger = (u_short)android_linger[1];
        windows_value = (const char *)&windows_linger;
        windows_length = sizeof(windows_linger);
    }
    if (setsockopt((SOCKET)(uintptr_t)(uint32_t)descriptor, windows_level,
                   windows_option, windows_value, windows_length) == SOCKET_ERROR) {
        return set_socket_error();
    }
    return 0;
}

static int shim_shutdown(int descriptor, int how) {
    if (shutdown((SOCKET)(uintptr_t)(uint32_t)descriptor, how) == SOCKET_ERROR) {
        return set_socket_error();
    }
    return 0;
}

#define HTTP_TRACE_SLOT_COUNT 64
typedef struct {
    int used;
    int descriptor;
    int headers_seen;
    int body_logged;
    int chunked;
    int tls;
    uint64_t sent_bytes;
    uint64_t received_bytes;
    char path[192];
} HttpTraceState;

static SRWLOCK g_http_trace_lock = SRWLOCK_INIT;
static HttpTraceState g_http_trace_states[HTTP_TRACE_SLOT_COUNT];

static HttpTraceState *http_trace_state_locked(int descriptor, int create) {
    HttpTraceState *empty = NULL;
    unsigned index;
    for (index = 0; index < HTTP_TRACE_SLOT_COUNT; ++index) {
        HttpTraceState *state = &g_http_trace_states[index];
        if (state->used && state->descriptor == descriptor) return state;
        if (!state->used && !empty) empty = state;
    }
    if (!create) return NULL;
    if (!empty) {
        empty = &g_http_trace_states[(unsigned)descriptor %
                                     HTTP_TRACE_SLOT_COUNT];
    }
    memset(empty, 0, sizeof(*empty));
    empty->used = 1;
    empty->descriptor = descriptor;
    return empty;
}

static void socket_trace_reset(int descriptor) {
    HttpTraceState *state;
    AcquireSRWLockExclusive(&g_http_trace_lock);
    state = http_trace_state_locked(descriptor, 1);
    memset(state, 0, sizeof(*state));
    state->used = 1;
    state->descriptor = descriptor;
    ReleaseSRWLockExclusive(&g_http_trace_lock);
}

static void socket_trace_traffic(int descriptor, const void *buffer,
                                 size_t length, int outgoing) {
    const unsigned char *bytes = (const unsigned char *)buffer;
    HttpTraceState *state;
    if (!length) return;
    AcquireSRWLockExclusive(&g_http_trace_lock);
    state = http_trace_state_locked(descriptor, 1);
    if (outgoing) {
        if (UINT64_MAX - state->sent_bytes < length) {
            state->sent_bytes = UINT64_MAX;
        } else {
            state->sent_bytes += length;
        }
    } else if (UINT64_MAX - state->received_bytes < length) {
        state->received_bytes = UINT64_MAX;
    } else {
        state->received_bytes += length;
    }
    if (length >= 3 && bytes[0] >= 0x14 && bytes[0] <= 0x17 &&
        bytes[1] == 0x03) {
        state->tls = 1;
    }
    ReleaseSRWLockExclusive(&g_http_trace_lock);
}

static void socket_trace_close(int descriptor) {
    uint64_t sent = 0;
    uint64_t received = 0;
    int tls = 0;
    HttpTraceState *state;
    AcquireSRWLockExclusive(&g_http_trace_lock);
    state = http_trace_state_locked(descriptor, 0);
    if (state) {
        sent = state->sent_bytes;
        received = state->received_bytes;
        tls = state->tls;
        memset(state, 0, sizeof(*state));
    }
    ReleaseSRWLockExclusive(&g_http_trace_lock);
    if (tls) {
        runtime_log("Network TLS socket closed: fd=%d sent=%llu received=%llu",
                    descriptor, (unsigned long long)sent,
                    (unsigned long long)received);
    }
}

static int ascii_contains_case_insensitive(const unsigned char *bytes,
                                           size_t length,
                                           const char *needle) {
    size_t needle_length = strlen(needle);
    size_t offset;
    if (!needle_length || needle_length > length) return 0;
    for (offset = 0; offset + needle_length <= length; ++offset) {
        size_t index;
        for (index = 0; index < needle_length; ++index) {
            if (tolower(bytes[offset + index]) !=
                tolower((unsigned char)needle[index])) {
                break;
            }
        }
        if (index == needle_length) return 1;
    }
    return 0;
}

static const char *classify_http_body(const unsigned char *bytes,
                                      size_t length) {
    size_t offset = 0;
    while (offset < length &&
           (bytes[offset] == '\r' || bytes[offset] == '\n' ||
            bytes[offset] == ' ' || bytes[offset] == '\t')) {
        ++offset;
    }
    if (offset >= length) return "empty body chunk";
    if (bytes[offset] == '-' && offset + 1 < length &&
        isdigit(bytes[offset + 1])) {
        if (bytes[offset + 1] == '1' &&
            (offset + 2 >= length || !isdigit(bytes[offset + 2]))) {
            return "server result -1";
        }
        return "negative numeric result";
    }
    if (isdigit(bytes[offset])) return "numeric/text result";
    if (bytes[offset] == '<') return "HTML/text result";
    if (bytes[offset] >= 0x20 && bytes[offset] <= 0x7e) {
        return "text result";
    }
    return "binary result";
}

static void trace_http_request(int descriptor, const void *buffer, int length) {
    static LONG request_count;
    static const char *const methods[] = {
        "GET", "POST", "HEAD", "PUT", "DELETE", "OPTIONS", "PATCH"
    };
    const char *bytes = (const char *)buffer;
    const char *method = NULL;
    size_t method_length = 0;
    size_t method_index;
    char path[192];
    size_t path_length = 0;
    size_t offset;
    LONG trace;
    if (!bytes || length <= 0) return;
    for (method_index = 0;
         method_index < sizeof(methods) / sizeof(methods[0]); ++method_index) {
        size_t candidate_length = strlen(methods[method_index]);
        if (length > (int)candidate_length &&
            memcmp(bytes, methods[method_index], candidate_length) == 0 &&
            bytes[candidate_length] == ' ') {
            method = methods[method_index];
            method_length = candidate_length;
            break;
        }
    }
    if (!method) return;
    offset = method_length + 1;
    while (offset < (size_t)length && path_length + 1 < sizeof(path)) {
        unsigned char character = (unsigned char)bytes[offset++];
        if (character == ' ' || character == '?' || character == '\r' ||
            character == '\n' || character < 0x20 || character > 0x7e) {
            break;
        }
        path[path_length++] = (char)character;
    }
    path[path_length] = 0;
    AcquireSRWLockExclusive(&g_http_trace_lock);
    {
        HttpTraceState *state = http_trace_state_locked(descriptor, 1);
        state->headers_seen = 0;
        state->body_logged = 0;
        state->chunked = 0;
        snprintf(state->path, sizeof(state->path), "%s",
                 path_length ? path : "<path unavailable>");
    }
    ReleaseSRWLockExclusive(&g_http_trace_lock);
    trace = InterlockedIncrement(&request_count);
    if (trace <= 64) {
        runtime_log("Network HTTP request #%ld: fd=%d %s %s (%d bytes)",
                    (long)trace, descriptor, method,
                    path_length ? path : "<path unavailable>", length);
    }
}

static void trace_http_response(int descriptor, const void *buffer, int length) {
    static LONG response_count;
    const unsigned char *bytes = (const unsigned char *)buffer;
    char status[128];
    char path[192] = "<request unavailable>";
    const unsigned char *body = NULL;
    size_t body_length = 0;
    size_t status_length = 0;
    size_t offset;
    int should_log_body = 0;
    LONG trace;
    int is_header = bytes && length >= 5 && memcmp(bytes, "HTTP/", 5) == 0;
    if (!bytes || length <= 0) return;
    if (is_header) {
        while (status_length < (size_t)length &&
               status_length + 1 < sizeof(status)) {
            unsigned char character = bytes[status_length];
            if (character == '\r' || character == '\n') break;
            status[status_length++] =
                character >= 0x20 && character <= 0x7e ? (char)character : '?';
        }
        status[status_length] = 0;
        trace = InterlockedIncrement(&response_count);
        if (trace <= 64) {
            runtime_log("Network HTTP response #%ld: fd=%d %s",
                        (long)trace, descriptor,
                        status_length ? status : "<status unavailable>");
        }
    }

    AcquireSRWLockExclusive(&g_http_trace_lock);
    {
        HttpTraceState *state = http_trace_state_locked(descriptor, 0);
        if (state) {
            snprintf(path, sizeof(path), "%s", state->path);
            if (is_header) {
                state->headers_seen = 1;
                state->chunked = ascii_contains_case_insensitive(
                    bytes, (size_t)length, "transfer-encoding: chunked");
                for (offset = 0; offset + 3 < (size_t)length; ++offset) {
                    if (bytes[offset] == '\r' && bytes[offset + 1] == '\n' &&
                        bytes[offset + 2] == '\r' &&
                        bytes[offset + 3] == '\n') {
                        body = bytes + offset + 4;
                        body_length = (size_t)length - offset - 4;
                        break;
                    }
                }
            } else if (state->headers_seen && !state->body_logged) {
                body = bytes;
                body_length = (size_t)length;
            }
            if (body && body_length && state->chunked) {
                for (offset = 0; offset + 1 < body_length; ++offset) {
                    if (body[offset] == '\r' && body[offset + 1] == '\n') {
                        body += offset + 2;
                        body_length -= offset + 2;
                        break;
                    }
                }
            }
            if (body && body_length && !state->body_logged) {
                state->body_logged = 1;
                should_log_body = 1;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_http_trace_lock);
    if (should_log_body) {
        runtime_log("Network HTTP body: fd=%d %s -> %s", descriptor, path,
                    classify_http_body(body, body_length));
    }
}

static int shim_recv(int descriptor, void *buffer, size_t length, int flags) {
    static LONG error_count;
    int result;
    if (length > INT_MAX) length = INT_MAX;
    result = recv((SOCKET)(uintptr_t)(uint32_t)descriptor, (char *)buffer,
                  (int)length, flags & ~GD_ANDROID_MSG_NOSIGNAL);
    if (result > 0) {
        socket_trace_traffic(descriptor, buffer, (size_t)result, 0);
        trace_http_response(descriptor, buffer, result);
        return result;
    }
    if (result == SOCKET_ERROR) {
        int windows_error = WSAGetLastError();
        int android_error = gd_net_wsa_error_to_android(windows_error);
        LONG trace = InterlockedIncrement(&error_count);
        if (trace <= 32 && windows_error != WSAEWOULDBLOCK) {
            runtime_log("Network recv error #%ld: fd=%d Winsock=%d Android errno=%d",
                        (long)trace, descriptor, windows_error, android_error);
        }
        g_errno_value = android_error;
        WSASetLastError(windows_error);
        return -1;
    }
    return result;
}

static int shim_send(int descriptor, const void *buffer, size_t length, int flags) {
    static LONG error_count;
    int result;
    if (length > INT_MAX) length = INT_MAX;
    result = send((SOCKET)(uintptr_t)(uint32_t)descriptor, (const char *)buffer,
                  (int)length, flags & ~GD_ANDROID_MSG_NOSIGNAL);
    if (result > 0) {
        socket_trace_traffic(descriptor, buffer, (size_t)result, 1);
        trace_http_request(descriptor, buffer, result);
        return result;
    }
    if (result == SOCKET_ERROR) {
        int windows_error = WSAGetLastError();
        int android_error = gd_net_wsa_error_to_android(windows_error);
        LONG trace = InterlockedIncrement(&error_count);
        if (trace <= 32 && windows_error != WSAEWOULDBLOCK) {
            runtime_log("Network send error #%ld: fd=%d Winsock=%d Android errno=%d",
                        (long)trace, descriptor, windows_error, android_error);
        }
        g_errno_value = android_error;
        WSASetLastError(windows_error);
        return -1;
    }
    return result;
}

static int shim_writev(int descriptor, const AndroidIovec *vectors,
                       int vector_count) {
    int socket_type;
    int socket_type_length = sizeof(socket_type);
    if (!vectors || vector_count < 0 || vector_count > 1024) {
        g_errno_value = 22;
        return -1;
    }
    if (getsockopt((SOCKET)(uintptr_t)(uint32_t)descriptor, SOL_SOCKET,
                   SO_TYPE, (char *)&socket_type,
                   &socket_type_length) == 0) {
        WSABUF *buffers;
        DWORD sent = 0;
        int index;
        int status;
        buffers = (WSABUF *)calloc((size_t)vector_count, sizeof(*buffers));
        if (!buffers && vector_count) {
            g_errno_value = 12;
            return -1;
        }
        for (index = 0; index < vector_count; ++index) {
            buffers[index].buf = (char *)vectors[index].base;
            buffers[index].len = vectors[index].length;
        }
        status = WSASend((SOCKET)(uintptr_t)(uint32_t)descriptor, buffers,
                         (DWORD)vector_count, &sent, 0, NULL, NULL);
        free(buffers);
        if (status == SOCKET_ERROR) return set_socket_error();
        if (sent) {
            size_t remaining = sent;
            for (index = 0; index < vector_count && remaining; ++index) {
                size_t count = vectors[index].length;
                if (count > remaining) count = remaining;
                socket_trace_traffic(descriptor, vectors[index].base, count,
                                     1);
                remaining -= count;
            }
        }
        return sent > INT_MAX ? INT_MAX : (int)sent;
    }
    {
        int total = 0;
        int index;
        for (index = 0; index < vector_count; ++index) {
            unsigned length = vectors[index].length > INT_MAX
                                  ? INT_MAX
                                  : (unsigned)vectors[index].length;
            int written = _write(descriptor, vectors[index].base, length);
            if (written < 0) return total ? total : -1;
            if (written > INT_MAX - total) return INT_MAX;
            total += written;
            if ((unsigned)written < length || total == INT_MAX) break;
        }
        return total;
    }
}

static int shim_recvfrom(int descriptor, void *buffer, size_t length, int flags,
                         struct sockaddr *address, int *address_length) {
    struct sockaddr_storage storage;
    int windows_length = sizeof(storage);
    int result;
    if (length > INT_MAX) length = INT_MAX;
    result = recvfrom((SOCKET)(uintptr_t)(uint32_t)descriptor, (char *)buffer,
                      (int)length, flags & ~GD_ANDROID_MSG_NOSIGNAL,
                      address ? (struct sockaddr *)&storage : NULL,
                      address ? &windows_length : NULL);
    if (result == SOCKET_ERROR) return set_socket_error();
    if (address && address_length) {
        gd_net_sockaddr_from_host(address, address_length,
                             (struct sockaddr *)&storage, windows_length);
    }
    return result;
}

static int shim_sendto(int descriptor, const void *buffer, size_t length, int flags,
                       const struct sockaddr *address, int address_length) {
    struct sockaddr_storage storage;
    int windows_length = address_length;
    const struct sockaddr *windows_address = gd_net_sockaddr_to_host(
        address, address_length, &storage, &windows_length);
    int result;
    if (length > INT_MAX) length = INT_MAX;
    result = sendto((SOCKET)(uintptr_t)(uint32_t)descriptor,
                    (const char *)buffer, (int)length,
                    flags & ~GD_ANDROID_MSG_NOSIGNAL, windows_address,
                    windows_length);
    return result == SOCKET_ERROR ? set_socket_error() : result;
}

static short android_poll_to_windows(short events) {
    short result = 0;
    if (events & ANDROID_POLLIN) result |= POLLRDNORM | POLLRDBAND;
    if (events & ANDROID_POLLPRI) result |= POLLPRI;
    if (events & ANDROID_POLLOUT) result |= POLLWRNORM;
    if (events & ANDROID_POLLRDNORM) result |= POLLRDNORM;
    if (events & ANDROID_POLLRDBAND) result |= POLLRDBAND;
    if (events & ANDROID_POLLWRNORM) result |= POLLWRNORM;
    if (events & ANDROID_POLLWRBAND) result |= POLLWRBAND;
    return result;
}

static short windows_poll_to_android(short events) {
    short result = 0;
    if (events & POLLRDNORM) result |= ANDROID_POLLIN | ANDROID_POLLRDNORM;
    if (events & POLLRDBAND) result |= ANDROID_POLLIN | ANDROID_POLLRDBAND;
    if (events & POLLPRI) result |= ANDROID_POLLPRI;
    if (events & POLLWRNORM) result |= ANDROID_POLLOUT | ANDROID_POLLWRNORM;
    if (events & POLLWRBAND) result |= ANDROID_POLLOUT | ANDROID_POLLWRBAND;
    if (events & POLLERR) result |= ANDROID_POLLERR;
    if (events & POLLHUP) result |= ANDROID_POLLHUP;
    if (events & POLLNVAL) result |= ANDROID_POLLNVAL;
    return result;
}

static int shim_poll(AndroidPollFd *descriptors, uint32_t count, int timeout) {
    static LONG logged_translation;
    static LONG logged_ready;
    WSAPOLLFD *windows_descriptors;
    uint32_t *windows_indices;
    uint32_t windows_count = 0;
    uint32_t index;
    int special_ready = 0;
    int result;
    if (!count) {
        Sleep(timeout < 0 ? INFINITE : (DWORD)timeout);
        return 0;
    }
    if (!descriptors || count > 4096u) {
        g_errno_value = 22;
        return -1;
    }
    windows_descriptors = (WSAPOLLFD *)calloc(
        count, sizeof(*windows_descriptors));
    windows_indices = (uint32_t *)calloc(count, sizeof(*windows_indices));
    if (!windows_descriptors || !windows_indices) {
        free(windows_descriptors);
        free(windows_indices);
        g_errno_value = 12;
        return -1;
    }
    for (index = 0; index < count; ++index) {
        descriptors[index].returned_events = 0;
        if (descriptors[index].descriptor == ANDROID_RANDOM_FD &&
            InterlockedCompareExchange(&g_random_open_count, 0, 0) > 0) {
            if (descriptors[index].events &
                (ANDROID_POLLIN | ANDROID_POLLRDNORM)) {
                descriptors[index].returned_events =
                    ANDROID_POLLIN | ANDROID_POLLRDNORM;
                ++special_ready;
            }
            continue;
        }
        windows_descriptors[windows_count].fd =
            (SOCKET)(uintptr_t)(uint32_t)descriptors[index].descriptor;
        windows_descriptors[windows_count].events =
            android_poll_to_windows(descriptors[index].events);
        windows_indices[windows_count++] = index;
    }
    if (InterlockedCompareExchange(&logged_translation, 1, 0) == 0) {
        runtime_log("Network poll ABI: translating Android readiness masks to Winsock");
    }
    result = windows_count
                 ? WSAPoll(windows_descriptors, windows_count,
                           special_ready ? 0 : timeout)
                 : 0;
    if (result != SOCKET_ERROR) {
        uint32_t windows_index;
        for (windows_index = 0; windows_index < windows_count;
             ++windows_index) {
            index = windows_indices[windows_index];
            descriptors[index].returned_events = windows_poll_to_android(
                windows_descriptors[windows_index].revents);
        }
        if ((result > 0 || special_ready > 0) &&
            InterlockedCompareExchange(&logged_ready, 1, 0) == 0) {
            runtime_log("Network poll: first translated readiness event delivered");
        }
    }
    free(windows_descriptors);
    free(windows_indices);
    return result == SOCKET_ERROR ? set_socket_error() : result + special_ready;
}

static int shim_fcntl(int descriptor, int command, ...) {
    va_list arguments;
    int flags = 0;
    u_long nonblocking;
    if (command == GD_ANDROID_F_GETFL) {
        return 0;
    }
    if (command == 1 || command == 2) { /* F_GETFD / F_SETFD */
        return 0;
    }
    if (command != GD_ANDROID_F_SETFL) {
        g_errno_value = 22;
        return -1;
    }
    va_start(arguments, command);
    flags = va_arg(arguments, int);
    va_end(arguments);
    nonblocking = (flags & GD_ANDROID_O_NONBLOCK) != 0;
    if (ioctlsocket((SOCKET)(uintptr_t)(uint32_t)descriptor, FIONBIO,
                    &nonblocking) == SOCKET_ERROR) {
        return set_socket_error();
    }
    return 0;
}

static int shim_ioctl(int descriptor, unsigned long request, ...) {
    va_list arguments;
    u_long *value;
    long windows_request;
    va_start(arguments, request);
    value = va_arg(arguments, u_long *);
    va_end(arguments);
    if (request == GD_ANDROID_FIONBIO) {
        windows_request = FIONBIO;
    } else if (request == GD_ANDROID_FIONREAD) {
        windows_request = FIONREAD;
    } else {
        g_errno_value = 22;
        return -1;
    }
    if (!value || ioctlsocket((SOCKET)(uintptr_t)(uint32_t)descriptor,
                              windows_request, value) == SOCKET_ERROR) {
        return set_socket_error();
    }
    return 0;
}

static int shim_close(int descriptor) {
    if (descriptor == ANDROID_RANDOM_FD) {
        LONG count = InterlockedCompareExchange(&g_random_open_count, 0, 0);
        while (count > 0) {
            LONG previous = InterlockedCompareExchange(
                &g_random_open_count, count - 1, count);
            if (previous == count) return 0;
            count = previous;
        }
        g_errno_value = 9;
        return -1;
    }
    if (closesocket((SOCKET)(uintptr_t)(uint32_t)descriptor) == 0) {
        socket_trace_close(descriptor);
        return 0;
    }
    if (WSAGetLastError() != WSAENOTSOCK) {
        return set_socket_error();
    }
    return _close(descriptor);
}

static int shim_socketpair(int family, int type, int protocol, int pair[2]) {
    SOCKET listener = INVALID_SOCKET;
    SOCKET client = INVALID_SOCKET;
    SOCKET server = INVALID_SOCKET;
    struct sockaddr_in address;
    int address_length = sizeof(address);
    (void)protocol;
    if (!pair || (family != 1 && family != AF_INET) || (type & 0xf) != SOCK_STREAM) {
        g_errno_value = 97;
        return -1;
    }
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) goto failure;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR ||
        getsockname(listener, (struct sockaddr *)&address, &address_length) ==
            SOCKET_ERROR) {
        goto failure;
    }
    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET ||
        connect(client, (struct sockaddr *)&address, sizeof(address)) ==
            SOCKET_ERROR) {
        goto failure;
    }
    server = accept(listener, NULL, NULL);
    if (server == INVALID_SOCKET) goto failure;
    closesocket(listener);
    pair[0] = (int)(uintptr_t)client;
    pair[1] = (int)(uintptr_t)server;
    return 0;
failure:
    if (listener != INVALID_SOCKET) closesocket(listener);
    if (client != INVALID_SOCKET) closesocket(client);
    if (server != INVALID_SOCKET) closesocket(server);
    return set_socket_error();
}

static struct hostent *shim_gethostbyname(const char *name) {
    struct hostent *result = gethostbyname(name);
    if (!result) set_socket_error();
    return result;
}

static struct hostent *shim_gethostbyaddr(const void *address, int length,
                                          int family) {
    struct hostent *result = gethostbyaddr((const char *)address, length,
                                           gd_net_android_family_to_host(family));
    if (!result) set_socket_error();
    return result;
}

static int shim_gethostname(char *name, size_t length) {
    int result = gethostname(name, length > INT_MAX ? INT_MAX : (int)length);
    return result == SOCKET_ERROR ? set_socket_error() : result;
}

static unsigned shim_if_nametoindex(const char *name) {
    InitOnceExecuteOnce(&g_if_name_index_once, initialize_if_name_index, NULL,
                        NULL);
    return g_if_name_to_index && name ? g_if_name_to_index(name) : 0u;
}

static int shim_getnameinfo(const struct sockaddr *address, int address_length,
                            char *host, size_t host_length, char *service,
                            size_t service_length, int flags) {
    struct sockaddr_storage storage;
    int windows_length = address_length;
    const struct sockaddr *windows_address = gd_net_sockaddr_to_host(
        address, address_length, &storage, &windows_length);
    int status = getnameinfo(
        windows_address, windows_length, host,
        host_length > UINT32_MAX ? UINT32_MAX : (DWORD)host_length, service,
        service_length > UINT32_MAX ? UINT32_MAX : (DWORD)service_length,
        flags);
    return status == 0 ? 0 : EAI_FAIL;
}

static struct servent *shim_getservbyname(const char *name, const char *protocol) {
    struct servent *result = getservbyname(name, protocol);
    if (!result) set_socket_error();
    return result;
}

static const char *shim_inet_ntop(int family, const void *source,
                                  char *destination, size_t size) {
    const char *result = InetNtopA(gd_net_android_family_to_host(family),
                                   (void *)source, destination,
                                   size > UINT32_MAX ? UINT32_MAX : (DWORD)size);
    if (!result) set_socket_error();
    return result;
}

static int shim_inet_pton(int family, const char *source, void *destination) {
    int result = InetPtonA(gd_net_android_family_to_host(family), source, destination);
    if (result < 0) set_socket_error();
    return result;
}

static CRITICAL_SECTION *pthread_mutex_object(void *storage, int create) {
    void *current = *(void *volatile *)storage;
    if ((uintptr_t)current < 0x10000u) {
        CRITICAL_SECTION *candidate;
        void *previous;
        if (!create) {
            return NULL;
        }
        candidate = (CRITICAL_SECTION *)malloc(sizeof(*candidate));
        if (!candidate) {
            return NULL;
        }
        InitializeCriticalSection(candidate);
        previous = InterlockedCompareExchangePointer((void *volatile *)storage,
                                                     candidate, current);
        if (previous != current) {
            DeleteCriticalSection(candidate);
            free(candidate);
            current = previous;
        } else {
            current = candidate;
        }
    }
    return (CRITICAL_SECTION *)current;
}

static int shim_pthread_mutex_init(void *mutex, const void *attributes) {
    (void)attributes;
    *(void **)mutex = NULL;
    return pthread_mutex_object(mutex, 1) ? 0 : 12;
}

static int shim_pthread_mutex_destroy(void *mutex) {
    CRITICAL_SECTION *object = (CRITICAL_SECTION *)
        InterlockedExchangePointer((void *volatile *)mutex, NULL);
    if ((uintptr_t)object >= 0x10000u) {
        DeleteCriticalSection(object);
        free(object);
    }
    return 0;
}

static int shim_pthread_mutex_lock(void *mutex) {
    CRITICAL_SECTION *object = pthread_mutex_object(mutex, 1);
    if (!object) {
        return 12;
    }
    EnterCriticalSection(object);
    return 0;
}

static int shim_pthread_mutex_unlock(void *mutex) {
    CRITICAL_SECTION *object = pthread_mutex_object(mutex, 0);
    if (!object) {
        return 22;
    }
    LeaveCriticalSection(object);
    return 0;
}

static int shim_pthread_cond_init(void *condition, const void *attributes) {
    (void)attributes;
    InitializeConditionVariable((CONDITION_VARIABLE *)condition);
    return 0;
}

static int shim_pthread_cond_destroy(void *condition) {
    (void)condition;
    return 0;
}

static int shim_pthread_cond_signal(void *condition) {
    WakeConditionVariable((CONDITION_VARIABLE *)condition);
    return 0;
}

static int shim_pthread_cond_wait(void *condition, void *mutex) {
    CRITICAL_SECTION *object = pthread_mutex_object(mutex, 0);
    if (!object) {
        return 22;
    }
    return SleepConditionVariableCS((CONDITION_VARIABLE *)condition, object,
                                    INFINITE) ? 0 : (int)GetLastError();
}

typedef struct {
    void *(*function)(void *);
    void *argument;
} PthreadStart;

static DWORD WINAPI pthread_start_routine(void *opaque) {
    PthreadStart *start = (PthreadStart *)opaque;
    void *(*function)(void *) = start->function;
    void *argument = start->argument;
    void *result;
    free(start);
    result = function(argument);
    return (DWORD)(uintptr_t)result;
}

static int shim_pthread_create(void *thread, const void *attributes,
                               void *(*function)(void *), void *argument) {
    PthreadStart *start;
    HANDLE handle;
    (void)attributes;
    start = (PthreadStart *)malloc(sizeof(*start));
    if (!start) {
        return 12;
    }
    start->function = function;
    start->argument = argument;
    handle = CreateThread(NULL, 0, pthread_start_routine, start, 0, NULL);
    if (!handle) {
        free(start);
        return 11;
    }
    *(HANDLE *)thread = handle;
    return 0;
}

static int shim_pthread_join(HANDLE thread, void **result) {
    DWORD code = 0;
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) {
        return 22;
    }
    GetExitCodeThread(thread, &code);
    CloseHandle(thread);
    if (result) {
        *result = (void *)(uintptr_t)code;
    }
    return 0;
}

static int shim_pthread_detach(HANDLE thread) {
    return CloseHandle(thread) ? 0 : 22;
}

static void shim_pthread_exit(void *result) {
    ExitThread((DWORD)(uintptr_t)result);
}

static int shim_pthread_attr_init(void *attributes) {
    *(uint32_t *)attributes = 0;
    return 0;
}

static int shim_pthread_key_create(uint32_t *key, void (*destructor)(void *)) {
    DWORD value = TlsAlloc();
    (void)destructor;
    if (value == TLS_OUT_OF_INDEXES) {
        return 11;
    }
    *key = value;
    return 0;
}

static int shim_pthread_key_delete(uint32_t key) {
    return TlsFree(key) ? 0 : 22;
}

static void *shim_pthread_getspecific(uint32_t key) {
    return TlsGetValue(key);
}

static int shim_pthread_setspecific(uint32_t key, const void *value) {
    return TlsSetValue(key, (void *)value) ? 0 : 22;
}

static int shim_pthread_once(volatile LONG *control, void (*function)(void)) {
    LONG previous = InterlockedCompareExchange(control, 1, 0);
    if (previous == 0) {
        function();
        InterlockedExchange(control, 2);
    } else {
        while (*control != 2) {
            Sleep(0);
        }
    }
    return 0;
}

static int shim_sem_init(void *semaphore, int process_shared,
                         unsigned initial_value) {
    HANDLE object;
    if (!semaphore || process_shared || initial_value > (unsigned)LONG_MAX) {
        g_errno_value = 22;
        return -1;
    }
    object = CreateSemaphoreA(NULL, (LONG)initial_value, LONG_MAX, NULL);
    if (!object) {
        g_errno_value = 12;
        return -1;
    }
    *(HANDLE *)semaphore = object;
    return 0;
}

static int shim_sem_destroy(void *semaphore) {
    HANDLE object;
    if (!semaphore) {
        g_errno_value = 22;
        return -1;
    }
    object = (HANDLE)InterlockedExchangePointer((void *volatile *)semaphore,
                                                NULL);
    if (object && CloseHandle(object)) return 0;
    g_errno_value = 22;
    return -1;
}

static int shim_sem_post(void *semaphore) {
    HANDLE object = semaphore ? *(HANDLE *)semaphore : NULL;
    if (object && ReleaseSemaphore(object, 1, NULL)) return 0;
    g_errno_value = 22;
    return -1;
}

static int shim_sem_wait(void *semaphore) {
    HANDLE object = semaphore ? *(HANDLE *)semaphore : NULL;
    if (object && WaitForSingleObject(object, INFINITE) == WAIT_OBJECT_0) {
        return 0;
    }
    g_errno_value = 22;
    return -1;
}

static unsigned shim_alarm(unsigned seconds) {
    (void)seconds;
    return 0;
}

static int shim_process_unsupported(void) {
    g_errno_value = 38;
    return -1;
}

static long shim_syscall(long number, ...) {
    (void)number;
    g_errno_value = 38;
    return -1;
}

/* Older msvcrt.dll builds do not export the C99 float entry points that the
 * Android library imports.  Keep the Android cdecl/float ABI and delegate to
 * the universally available double-precision functions. */
static float shim_acosf(float value) { return (float)acos((double)value); }
static float shim_asinf(float value) { return (float)asin((double)value); }
static float shim_atan2f(float y, float x) {
    return (float)atan2((double)y, (double)x);
}
static float shim_ceilf(float value) { return (float)ceil((double)value); }
static float shim_cosf(float value) { return (float)cos((double)value); }
static float shim_expf(float value) { return (float)exp((double)value); }
static float shim_floorf(float value) { return (float)floor((double)value); }
static float shim_fmodf(float x, float y) {
    return (float)fmod((double)x, (double)y);
}
static float shim_logf(float value) { return (float)log((double)value); }
static float shim_powf(float x, float y) {
    return (float)pow((double)x, (double)y);
}
static float shim_roundf(float value) {
    return value < 0.0f ? (float)ceil((double)value - 0.5)
                        : (float)floor((double)value + 0.5);
}
static double shim_round(double value) {
    return value < 0.0 ? ceil(value - 0.5) : floor(value + 0.5);
}
static float shim_sinf(float value) { return (float)sin((double)value); }
static float shim_sqrtf(float value) { return (float)sqrt((double)value); }
static float shim_tanf(float value) { return (float)tan((double)value); }

static void shim_glClearDepthf(float depth) {
    typedef void (APIENTRY *Function)(double);
    Function function = (Function)GetProcAddress(g_opengl, "glClearDepth");
    if (function) {
        function((double)depth);
    }
}

static int token_boundary(char value) {
    return !(value == '_' || (value >= '0' && value <= '9') ||
             (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'));
}

static void erase_shader_token(char *text, size_t length, const char *token) {
    size_t token_length = strlen(token);
    size_t index;
    if (length < token_length) {
        return;
    }
    for (index = 0; index + token_length <= length; ++index) {
        if ((index == 0 || token_boundary(text[index - 1])) &&
            (index + token_length == length || token_boundary(text[index + token_length])) &&
            memcmp(text + index, token, token_length) == 0) {
            memset(text + index, ' ', token_length);
        }
    }
}

static void erase_precision_statements(char *text, size_t length) {
    const char token[] = "precision";
    size_t index;
    for (index = 0; index + sizeof(token) - 1 <= length; ++index) {
        size_t end;
        if ((index != 0 && !token_boundary(text[index - 1])) ||
            memcmp(text + index, token, sizeof(token) - 1) != 0) {
            continue;
        }
        end = index;
        while (end < length && text[end] != ';' && text[end] != '\n') {
            ++end;
        }
        if (end < length && text[end] == ';') {
            ++end;
        }
        while (index < end) {
            if (text[index] != '\r' && text[index] != '\n') {
                text[index] = ' ';
            }
            ++index;
        }
    }
}

static void shim_glShaderSource(unsigned int shader, int count,
                                const char *const *strings, const int *lengths) {
    typedef void (APIENTRY *Function)(unsigned int, int, const char *const *,
                                      const int *);
    static Function function;
    char **copies;
    int *copy_lengths;
    int index;
    if (!function) {
        function = (Function)wglGetProcAddress("glShaderSource");
    }
    if (!function || count <= 0) {
        runtime_log("ERROR: desktop glShaderSource is unavailable");
        return;
    }
    copies = (char **)calloc((size_t)count, sizeof(*copies));
    copy_lengths = (int *)calloc((size_t)count, sizeof(*copy_lengths));
    if (!copies || !copy_lengths) {
        free(copies);
        free(copy_lengths);
        function(shader, count, strings, lengths);
        return;
    }
    for (index = 0; index < count; ++index) {
        size_t length = lengths && lengths[index] >= 0
                            ? (size_t)lengths[index]
                            : strlen(strings[index]);
        copies[index] = (char *)malloc(length + 1);
        if (!copies[index]) {
            continue;
        }
        memcpy(copies[index], strings[index], length);
        copies[index][length] = 0;
        copy_lengths[index] = (int)length;
        erase_precision_statements(copies[index], length);
        erase_shader_token(copies[index], length, "lowp");
        erase_shader_token(copies[index], length, "mediump");
        erase_shader_token(copies[index], length, "highp");
    }
    function(shader, count, (const char *const *)copies, copy_lengths);
    for (index = 0; index < count; ++index) {
        free(copies[index]);
    }
    free(copy_lengths);
    free(copies);
}

static uintptr_t shim_stub_zero(void) {
    return 0;
}

typedef struct {
    const char *android_name;
    const char *windows_name;
    int module;
} Alias;

enum { MOD_CRT, MOD_WS2, MOD_GL };

static const Alias aliases[] = {
    {"access", "_access", MOD_CRT}, {"chdir", "_chdir", MOD_CRT},
    {"chmod", "_chmod", MOD_CRT}, {"close", "_close", MOD_CRT},
    {"dup2", "_dup2", MOD_CRT}, {"fdopen", "_fdopen", MOD_CRT},
    {"fstat", "_fstat32", MOD_CRT}, {"getpid", "_getpid", MOD_CRT},
    {"lseek", "_lseek", MOD_CRT}, {"mkdir", "_mkdir", MOD_CRT},
    {"open", "_open", MOD_CRT}, {"read", "_read", MOD_CRT},
    {"snprintf", "_snprintf", MOD_CRT}, {"strcasecmp", "_stricmp", MOD_CRT},
    {"strdup", "_strdup", MOD_CRT}, {"strncasecmp", "_strnicmp", MOD_CRT},
    {"time", "_time32", MOD_CRT}, {"umask", "_umask", MOD_CRT},
    {"write", "_write", MOD_CRT}, {"gmtime", "_gmtime32", MOD_CRT},
    {"localtime", "_localtime32", MOD_CRT},
    {"getcwd", "_getcwd", MOD_CRT},
};

static void *module_symbol(int module, const char *name) {
    HMODULE handle = NULL;
    switch (module) {
    case MOD_CRT: handle = g_msvcrt; break;
    case MOD_WS2: handle = g_ws2; break;
    case MOD_GL: handle = g_opengl; break;
    }
    return handle ? (void *)GetProcAddress(handle, name) : NULL;
}

static void *custom_function(const char *name) {
    void *fmod_function = fmod_win_resolve(name);
    if (fmod_function) return fmod_function;
#define CUSTOM(symbol, function) if (strcmp(name, symbol) == 0) return (void *)(function)
    CUSTOM("__android_log_print", shim_android_log_print);
    CUSTOM("__assert2", shim_assert2);
    CUSTOM("__errno", shim_errno);
    CUSTOM("__stack_chk_fail", shim_stack_chk_fail);
    CUSTOM("__cxa_atexit", shim_cxa_atexit);
    CUSTOM("__cxa_finalize", shim_cxa_finalize);
    CUSTOM("dlopen", shim_dlopen);
    CUSTOM("dlsym", shim_dlsym);
    CUSTOM("dlclose", shim_dlclose);
    CUSTOM("dlerror", shim_dlerror);
    CUSTOM("dl_iterate_phdr", shim_dl_iterate_phdr);
    CUSTOM("mmap", shim_mmap);
    CUSTOM("munmap", shim_munmap);
    CUSTOM("clock_gettime", shim_clock_gettime);
    CUSTOM("clock", shim_clock);
    CUSTOM("gettimeofday", shim_gettimeofday);
    CUSTOM("ftime", shim_ftime);
    CUSTOM("gmtime_r", shim_gmtime_r);
    CUSTOM("usleep", shim_usleep);
    CUSTOM("strlcat", shim_strlcat);
    CUSTOM("memrchr", shim_memrchr);
    CUSTOM("basename", shim_basename);
    CUSTOM("strtok_r", shim_strtok_r);
    CUSTOM("strerror", shim_strerror);
    CUSTOM("strerror_r", shim_strerror_r);
    CUSTOM("gai_strerror", shim_gai_strerror);
    CUSTOM("strtoll", shim_strtoll);
    CUSTOM("strtoull", shim_strtoull);
    CUSTOM("fopen", shim_fopen);
    CUSTOM("fclose", shim_fclose);
    CUSTOM("fflush", shim_fflush);
    CUSTOM("fgets", shim_fgets);
    CUSTOM("fprintf", shim_fprintf);
    CUSTOM("fputc", shim_fputc);
    CUSTOM("fputs", shim_fputs);
    CUSTOM("fread", shim_fread);
    CUSTOM("fseek", shim_fseek);
    CUSTOM("ftell", shim_ftell);
    CUSTOM("fwrite", shim_fwrite);
    CUSTOM("setvbuf", shim_setvbuf);
    CUSTOM("ungetc", shim_ungetc);
    CUSTOM("vfprintf", shim_vfprintf);
    CUSTOM("access", shim_access);
    CUSTOM("chmod", shim_chmod);
    CUSTOM("rename", shim_rename);
    CUSTOM("remove", shim_remove);
    CUSTOM("unlink", shim_unlink);
    CUSTOM("srand48", shim_srand48);
    CUSTOM("lrand48", shim_lrand48);
    CUSTOM("arc4random", shim_arc4random);
    CUSTOM("setjmp", shim_bionic_setjmp);
    CUSTOM("_setjmp", shim_bionic_setjmp);
    CUSTOM("sigsetjmp", shim_bionic_sigsetjmp);
    CUSTOM("longjmp", shim_bionic_longjmp);
    CUSTOM("_longjmp", shim_bionic_longjmp);
    CUSTOM("siglongjmp", shim_bionic_longjmp);
    CUSTOM("sigaction", shim_sigaction);
    CUSTOM("bsd_signal", shim_bsd_signal);
    CUSTOM("sigprocmask", shim_sigprocmask);
    CUSTOM("wctype", shim_wctype);
    CUSTOM("iswctype", shim_iswctype);
    CUSTOM("towlower", shim_towlower);
    CUSTOM("towupper", shim_towupper);
    CUSTOM("wctob", shim_wctob);
    CUSTOM("btowc", shim_btowc);
    CUSTOM("wcslen", shim_wcslen);
    CUSTOM("wmemchr", shim_wmemchr);
    CUSTOM("wmemcmp", shim_wmemcmp);
    CUSTOM("wmemcpy", shim_wmemcpy);
    CUSTOM("wmemmove", shim_wmemmove);
    CUSTOM("wmemset", shim_wmemset);
    CUSTOM("getdtablesize", shim_getdtablesize);
    CUSTOM("getuid", shim_get_app_id);
    CUSTOM("geteuid", shim_get_app_id);
    CUSTOM("getgid", shim_get_app_id);
    CUSTOM("getegid", shim_get_app_id);
    CUSTOM("open", shim_open);
    CUSTOM("read", shim_read);
    CUSTOM("fstat", shim_fstat);
    CUSTOM("accept", shim_accept);
    CUSTOM("bind", shim_bind);
    CUSTOM("close", shim_close);
    CUSTOM("connect", shim_connect);
    CUSTOM("fcntl", shim_fcntl);
    CUSTOM("freeaddrinfo", shim_freeaddrinfo);
    CUSTOM("getaddrinfo", shim_getaddrinfo);
    CUSTOM("gethostbyaddr", shim_gethostbyaddr);
    CUSTOM("gethostbyname", shim_gethostbyname);
    CUSTOM("gethostname", shim_gethostname);
    CUSTOM("getnameinfo", shim_getnameinfo);
    CUSTOM("if_nametoindex", shim_if_nametoindex);
    CUSTOM("getpeername", shim_getpeername);
    CUSTOM("getservbyname", shim_getservbyname);
    CUSTOM("getsockname", shim_getsockname);
    CUSTOM("getsockopt", shim_getsockopt);
    CUSTOM("inet_ntop", shim_inet_ntop);
    CUSTOM("inet_pton", shim_inet_pton);
    CUSTOM("ioctl", shim_ioctl);
    CUSTOM("listen", shim_listen);
    CUSTOM("poll", shim_poll);
    CUSTOM("recv", shim_recv);
    CUSTOM("recvfrom", shim_recvfrom);
    CUSTOM("send", shim_send);
    CUSTOM("sendto", shim_sendto);
    CUSTOM("writev", shim_writev);
    CUSTOM("setsockopt", shim_setsockopt);
    CUSTOM("shutdown", shim_shutdown);
    CUSTOM("socket", shim_socket);
    CUSTOM("socketpair", shim_socketpair);
    CUSTOM("alarm", shim_alarm);
    CUSTOM("fork", shim_process_unsupported);
    CUSTOM("pause", shim_process_unsupported);
    CUSTOM("setsid", shim_process_unsupported);
    CUSTOM("waitpid", shim_process_unsupported);
    CUSTOM("syscall", shim_syscall);
    CUSTOM("acosf", shim_acosf);
    CUSTOM("asinf", shim_asinf);
    CUSTOM("atan2f", shim_atan2f);
    CUSTOM("ceilf", shim_ceilf);
    CUSTOM("cosf", shim_cosf);
    CUSTOM("expf", shim_expf);
    CUSTOM("floorf", shim_floorf);
    CUSTOM("fmodf", shim_fmodf);
    CUSTOM("logf", shim_logf);
    CUSTOM("powf", shim_powf);
    CUSTOM("roundf", shim_roundf);
    CUSTOM("round", shim_round);
    CUSTOM("sinf", shim_sinf);
    CUSTOM("sqrtf", shim_sqrtf);
    CUSTOM("tanf", shim_tanf);
    CUSTOM("glClearDepthf", shim_glClearDepthf);
    CUSTOM("glShaderSource", shim_glShaderSource);
    CUSTOM("pthread_attr_init", shim_pthread_attr_init);
    CUSTOM("pthread_cond_destroy", shim_pthread_cond_destroy);
    CUSTOM("pthread_cond_init", shim_pthread_cond_init);
    CUSTOM("pthread_cond_signal", shim_pthread_cond_signal);
    CUSTOM("pthread_cond_wait", shim_pthread_cond_wait);
    CUSTOM("pthread_create", shim_pthread_create);
    CUSTOM("pthread_detach", shim_pthread_detach);
    CUSTOM("pthread_exit", shim_pthread_exit);
    CUSTOM("pthread_getspecific", shim_pthread_getspecific);
    CUSTOM("pthread_join", shim_pthread_join);
    CUSTOM("pthread_key_create", shim_pthread_key_create);
    CUSTOM("pthread_key_delete", shim_pthread_key_delete);
    CUSTOM("pthread_mutex_destroy", shim_pthread_mutex_destroy);
    CUSTOM("pthread_mutex_init", shim_pthread_mutex_init);
    CUSTOM("pthread_mutex_lock", shim_pthread_mutex_lock);
    CUSTOM("pthread_mutex_unlock", shim_pthread_mutex_unlock);
    CUSTOM("pthread_once", shim_pthread_once);
    CUSTOM("pthread_setspecific", shim_pthread_setspecific);
    CUSTOM("sem_destroy", shim_sem_destroy);
    CUSTOM("sem_init", shim_sem_init);
    CUSTOM("sem_post", shim_sem_post);
    CUSTOM("sem_wait", shim_sem_wait);
    CUSTOM("crc32", crc32);
    CUSTOM("deflate", deflate);
    CUSTOM("deflateEnd", deflateEnd);
    CUSTOM("deflateInit2_", deflateInit2_);
    CUSTOM("deflateInit_", deflateInit_);
    CUSTOM("deflateParams", deflateParams);
    CUSTOM("deflateReset", deflateReset);
    CUSTOM("gzclose", gzclose);
    CUSTOM("gzopen", gzopen);
    CUSTOM("gzread", gzread);
    CUSTOM("inflate", inflate);
    CUSTOM("inflateCopy", inflateCopy);
    CUSTOM("inflateEnd", inflateEnd);
    CUSTOM("inflateInit2_", inflateInit2_);
    CUSTOM("inflateInit_", inflateInit_);
    CUSTOM("inflateReset", inflateReset);
    CUSTOM("inflateSync", inflateSync);
    CUSTOM("uncompress", uncompress);
    CUSTOM("zError", zError);
    CUSTOM("zlibVersion", zlibVersion);
#undef CUSTOM
    return NULL;
}

typedef struct {
    const char *name;
    unsigned char argument_dwords;
} CallConventionEntry;

/*
 * Android/i386 GLES entry points use cdecl.  The 32-bit Windows OpenGL ABI
 * uses stdcall, including functions returned by wglGetProcAddress.  Jumping
 * from an ELF import thunk straight to a Windows GL function therefore makes
 * the callee pop the Android caller's argument area and corrupts its stack.
 * These counts let us build a tiny cdecl wrapper around each stdcall target.
 */
static const CallConventionEntry gl_call_conventions[] = {
    {"glActiveTexture", 1}, {"glAttachShader", 2},
    {"glBindAttribLocation", 3}, {"glBindBuffer", 2},
    {"glBindFramebuffer", 2}, {"glBindRenderbuffer", 2},
    {"glBindTexture", 2}, {"glBlendEquation", 1},
    {"glBlendFunc", 2}, {"glBufferData", 4},
    {"glBufferSubData", 4}, {"glCheckFramebufferStatus", 1},
    {"glClear", 1}, {"glClearColor", 4}, {"glClearStencil", 1},
    {"glCompileShader", 1}, {"glCompressedTexImage2D", 8},
    {"glCreateProgram", 0}, {"glCreateShader", 1},
    {"glDeleteBuffers", 2}, {"glDeleteFramebuffers", 2},
    {"glDeleteProgram", 1}, {"glDeleteRenderbuffers", 2},
    {"glDeleteShader", 1}, {"glDeleteTextures", 2},
    {"glDepthFunc", 1}, {"glDepthMask", 1}, {"glDisable", 1},
    {"glDisableVertexAttribArray", 1}, {"glDrawArrays", 3},
    {"glDrawElements", 4}, {"glEnable", 1},
    {"glEnableVertexAttribArray", 1},
    {"glFramebufferRenderbuffer", 4}, {"glFramebufferTexture2D", 5},
    {"glGenBuffers", 2}, {"glGenFramebuffers", 2},
    {"glGenRenderbuffers", 2}, {"glGenTextures", 2},
    {"glGenerateMipmap", 1}, {"glGetBooleanv", 2},
    {"glGetError", 0}, {"glGetFloatv", 2}, {"glGetIntegerv", 2},
    {"glGetProgramInfoLog", 4}, {"glGetProgramiv", 3},
    {"glGetShaderInfoLog", 4}, {"glGetShaderSource", 4},
    {"glGetShaderiv", 3}, {"glGetString", 1},
    {"glGetUniformLocation", 2}, {"glIsEnabled", 1},
    {"glLineWidth", 1}, {"glLinkProgram", 1},
    {"glPixelStorei", 2}, {"glReadPixels", 7},
    {"glRenderbufferStorage", 4}, {"glScissor", 4},
    {"glStencilFunc", 3}, {"glStencilMask", 1}, {"glStencilOp", 3},
    {"glTexImage2D", 9}, {"glTexParameteri", 3},
    {"glUniform1f", 2}, {"glUniform1i", 2},
    {"glUniform2f", 3}, {"glUniform2fv", 3},
    {"glUniform2i", 3}, {"glUniform2iv", 3},
    {"glUniform3f", 4}, {"glUniform3fv", 3},
    {"glUniform3i", 4}, {"glUniform3iv", 3},
    {"glUniform4f", 5}, {"glUniform4fv", 3},
    {"glUniform4i", 5}, {"glUniform4iv", 3},
    {"glUniformMatrix4fv", 4}, {"glUseProgram", 1},
    {"glVertexAttribPointer", 6}, {"glViewport", 4},
};

static int gl_argument_dwords(const char *name) {
    size_t index;
    for (index = 0;
         index < sizeof(gl_call_conventions) / sizeof(gl_call_conventions[0]);
         ++index) {
        if (strcmp(name, gl_call_conventions[index].name) == 0) {
            return gl_call_conventions[index].argument_dwords;
        }
    }
    return -1;
}

static void *lookup_opengl_function(const char *name) {
    void *address = module_symbol(MOD_GL, name);
    if (!address && g_opengl) {
        typedef PROC (WINAPI *WglGetProcAddressFunction)(LPCSTR);
        WglGetProcAddressFunction get_proc =
            (WglGetProcAddressFunction)GetProcAddress(g_opengl,
                                                       "wglGetProcAddress");
        if (get_proc) {
            PROC proc = get_proc(name);
            if (proc && proc != (PROC)1 && proc != (PROC)2 &&
                proc != (PROC)3 && proc != (PROC)-1) {
                address = (void *)proc;
            }
        }
    }
    return address;
}

static void *make_cdecl_to_stdcall_thunk(uint32_t id, void *target,
                                         unsigned argument_dwords,
                                         const char *name) {
    unsigned char *code;
    size_t position = 0;
    int argument;
    if (!g_callconv_thunks || id >= MAX_IMPORTS || argument_dwords > 9) {
        return NULL;
    }
    code = g_callconv_thunks + id * CALLCONV_THUNK_SIZE;
    code[position++] = 0x55;             /* push ebp */
    code[position++] = 0x89;             /* mov ebp, esp */
    code[position++] = 0xe5;
    for (argument = (int)argument_dwords; argument >= 1; --argument) {
        code[position++] = 0xff;         /* push dword ptr [ebp+disp32] */
        code[position++] = 0xb5;
        *(uint32_t *)(code + position) = 4u + (uint32_t)argument * 4u;
        position += 4;
    }
    code[position++] = 0xb8;             /* mov eax, target */
    *(uint32_t *)(code + position) = (uint32_t)(uintptr_t)target;
    position += 4;
    code[position++] = 0xff;             /* call eax */
    code[position++] = 0xd0;
    code[position++] = 0xc9;             /* leave */
    code[position++] = 0xc3;             /* ret (cdecl: caller owns args) */
    if (position > CALLCONV_THUNK_SIZE) {
        runtime_log("FATAL: call-convention thunk overflow for %s", name);
        return NULL;
    }
    FlushInstructionCache(GetCurrentProcess(), code, position);
    runtime_log("OpenGL ABI bridge: %s (%u argument dwords)", name,
                argument_dwords);
    return code;
}

static void *lookup_function(const char *name) {
    void *address;
    size_t i;
    address = custom_function(name);
    if (address) {
        return address;
    }
    address = module_symbol(MOD_CRT, name);
    if (address) {
        return address;
    }
    for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
        if (strcmp(name, aliases[i].android_name) == 0) {
            address = module_symbol(aliases[i].module, aliases[i].windows_name);
            if (address) {
                return address;
            }
        }
    }
    address = module_symbol(MOD_WS2, name);
    if (address) {
        return address;
    }
    return NULL;
}

void *runtime_resolve_function(const char *name, uint32_t id) {
    void *target;
    int gl_arguments;
    if (id < MAX_IMPORTS && g_import_targets[id]) {
        return g_import_targets[id];
    }
    target = custom_function(name);
    gl_arguments = name && name[0] == 'g' && name[1] == 'l'
                       ? gl_argument_dwords(name)
                       : -1;
    if (!target && gl_arguments >= 0) {
        void *raw_target = lookup_opengl_function(name);
        if (raw_target) {
            target = make_cdecl_to_stdcall_thunk(
                id, raw_target, (unsigned)gl_arguments, name);
        }
    } else if (!target && name && name[0] == 'g' && name[1] == 'l') {
        runtime_log("UNSUPPORTED OPENGL IMPORT: %s", name);
    }
    if (!target && gl_arguments < 0 &&
        !(name && name[0] == 'g' && name[1] == 'l')) {
        target = lookup_function(name);
    }
    if (!target) {
        runtime_log("UNIMPLEMENTED IMPORT CALLED: %s", name ? name : "<invalid>");
        target = (void *)shim_stub_zero;
    }
    if (id < MAX_IMPORTS) {
        g_import_targets[id] = target;
    }
    return target;
}

void *runtime_resolve_object(const char *name) {
    if (strcmp(name, "__stack_chk_guard") == 0) return &g_stack_guard;
    if (strcmp(name, "__sF") == 0) return g_sF;
    /* Bionic exports these as pointer objects, not as the tables themselves. */
    if (strcmp(name, "_ctype_") == 0) return &g_ctype_pointer;
    if (strcmp(name, "_tolower_tab_") == 0) return &g_tolower_pointer;
    if (strcmp(name, "_toupper_tab_") == 0) return &g_toupper_pointer;
    if (strcmp(name, "optarg") == 0) return &g_optarg;
    if (strcmp(name, "optind") == 0) return &g_optind;
    return NULL;
}

static void *runtime_resolve_by_id(uint32_t id) {
    if (id >= MAX_IMPORTS || !g_import_names[id]) {
        runtime_log("FATAL: invalid lazy import id %lu", (unsigned long)id);
        return (void *)shim_stub_zero;
    }
    return runtime_resolve_function(g_import_names[id], id);
}

void *runtime_make_lazy_thunk(uint32_t id) {
    unsigned char *code;
    intptr_t displacement;
    if (!g_thunks || id >= MAX_IMPORTS) {
        return NULL;
    }
    code = g_thunks + id * THUNK_SIZE;
    code[0] = 0x68;
    *(uint32_t *)(code + 1) = id;
    code[5] = 0xe8;
    displacement = (unsigned char *)(void *)runtime_resolve_by_id - (code + 10);
    *(int32_t *)(code + 6) = (int32_t)displacement;
    code[10] = 0x83;
    code[11] = 0xc4;
    code[12] = 0x04;
    code[13] = 0xff;
    code[14] = 0xe0;
    code[15] = 0xcc;
    FlushInstructionCache(GetCurrentProcess(), code, THUNK_SIZE);
    return code;
}
