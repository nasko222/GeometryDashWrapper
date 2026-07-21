#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "runtime.h"
#include "../third_party/zlib/zlib.h"

#define MAX_IMPORTS 1024
#define THUNK_SIZE 16
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
static char *g_optarg;
static int g_optind = 1;
static uint32_t g_lcg_state = 1;

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
    }
#endif
    runtime_log("The wrapper stopped. Send gd18-wrapper.log with this address.");
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
    for (i = 0; i < 384; ++i) {
        int value = (int)i - 128;
        unsigned char c = (unsigned char)value;
        g_ctype[i] = (unsigned char)((isalpha(c) ? 1 : 0) |
                                     (isdigit(c) ? 4 : 0) |
                                     (isspace(c) ? 8 : 0));
        g_tolower[i] = (int16_t)tolower(c);
        g_toupper[i] = (int16_t)toupper(c);
    }
    runtime_log("Geometry Dash 1.8 native wrapper probe");
    runtime_log("System DLLs: msvcrt=%s ws2_32=%s opengl32=%s",
                g_msvcrt ? "yes" : "no", g_ws2 ? "yes" : "no",
                g_opengl ? "yes" : "no");
    runtime_log("Compatibility services: built-in zlib and Win32 pthread bridge");
}

void runtime_shutdown(void) {
    if (g_thunks) {
        VirtualFree(g_thunks, 0, MEM_RELEASE);
        g_thunks = NULL;
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

static void shim_srand48(long seed) {
    g_lcg_state = (uint32_t)seed;
}

static long shim_lrand48(void) {
    g_lcg_state = g_lcg_state * 1103515245u + 12345u;
    return (long)((g_lcg_state >> 1) & 0x7fffffffu);
}

static int shim_getdtablesize(void) {
    return 2048;
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

static void shim_glClearDepthf(float depth) {
    typedef void (APIENTRY *Function)(double);
    Function function = (Function)GetProcAddress(g_opengl, "glClearDepth");
    if (function) {
        function((double)depth);
    }
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
    CUSTOM("gettimeofday", shim_gettimeofday);
    CUSTOM("usleep", shim_usleep);
    CUSTOM("strlcat", shim_strlcat);
    CUSTOM("memrchr", shim_memrchr);
    CUSTOM("basename", shim_basename);
    CUSTOM("srand48", shim_srand48);
    CUSTOM("lrand48", shim_lrand48);
    CUSTOM("getdtablesize", shim_getdtablesize);
    CUSTOM("alarm", shim_alarm);
    CUSTOM("fork", shim_process_unsupported);
    CUSTOM("pause", shim_process_unsupported);
    CUSTOM("setsid", shim_process_unsupported);
    CUSTOM("waitpid", shim_process_unsupported);
    CUSTOM("syscall", shim_syscall);
    CUSTOM("glClearDepthf", shim_glClearDepthf);
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
    address = module_symbol(MOD_GL, name);
    if (address) {
        return address;
    }
    if (g_opengl) {
        typedef PROC (WINAPI *WglGetProcAddressFunction)(LPCSTR);
        WglGetProcAddressFunction get_proc =
            (WglGetProcAddressFunction)GetProcAddress(g_opengl, "wglGetProcAddress");
        if (get_proc) {
            PROC proc = get_proc(name);
            if (proc && proc != (PROC)1 && proc != (PROC)2 &&
                proc != (PROC)3 && proc != (PROC)-1) {
                return (void *)proc;
            }
        }
    }
    return NULL;
}

void *runtime_resolve_function(const char *name, uint32_t id) {
    void *target;
    if (id < MAX_IMPORTS && g_import_targets[id]) {
        return g_import_targets[id];
    }
    target = lookup_function(name);
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
    if (strcmp(name, "_ctype_") == 0) return g_ctype + 128;
    if (strcmp(name, "_tolower_tab_") == 0) return g_tolower + 128;
    if (strcmp(name, "_toupper_tab_") == 0) return g_toupper + 128;
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
