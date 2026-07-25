# 0.9.4-arm-dynarmictest2

- Applies all authentic ARM ELF relocations in the Dynarmic x64 memory backend.
- Creates 277 ARM import SVC traps and seven imported-object mappings.
- Adds Bionic character tables, stack guard, optind/optarg and minimal JavaVM/JNIEnv tables.
- Adds a guest heap, isolated nested-call stacks and ARM/Thumb return trampolines.
- Executes all 238 authentic `.init_array` constructors through Dynarmic.
- Implements constructor-critical host imports including memory/string calls, `pthread_once`, TLS keys, C++ atexit, locale helpers and common soft-float math.
- Executes the authentic `JNI_OnLoad` and verifies JNI 1.4 (`0x00010004`).
- Logs any permissive fallback import by exact symbol name for the next bridge increment.
