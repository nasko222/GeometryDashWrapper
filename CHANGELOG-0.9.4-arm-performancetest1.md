# Geometry Dash ARM Wrapper 0.9.4-arm-performancetest1

Experimental performance branch based on stable bootstrap15.

## Runtime changes

- Enables Unicorn's normal CPU TLB fast path; a local repeated-memory benchmark found `UC_TLB_VIRTUAL` several times slower for this workload, so that experiment was rejected.
- Requests a 256 MiB TCG translation cache instead of the 32 MiB default used by a 32-bit host.
- Caches resolved OpenGL entry points per imported function.
- Passes directly mapped guest memory to `glBufferData`, `glBufferSubData`, and uniform-vector uploads when safe.
- Suppresses redundant `glBindBuffer` host calls.
- Reads only the ARM registers required by classified imports.
- Fast-dispatches frequent double-precision `sin`, `cos`, `acos`, `atan2`, `pow`, and `fmod` imports.
- Fast-dispatches common ARM EABI integer division helpers.
- Builds the wrapper at `-O3`.

## Measurements and A/B modes

- Adds OpenGL resolver-cache, redundant-bind, and direct-upload counters.
- Adds `--profile-import-time` and `RUN_PROFILE_IMPORT_TIME.cmd`. This mode uses high-resolution timing around every imported callback and is diagnostic, not a normal benchmark.
- Adds `--profile-arm-blocks` and `RUN_PROFILE_ARM_BLOCKS.cmd`. This records the hottest guest ARM translation blocks so later builds can target actual game functions for native replacement.
- Adds `--no-particle-guards` and `RUN_PERFORMANCE_NO_PARTICLE_GUARDS.cmd` to measure the two PlayLayer particle compatibility hooks separately.

## Integrity policy

The normal launcher keeps all bootstrap15 save, immutable-memory, label, allocator, and particle compatibility protections enabled. The no-particle-guards launcher is an experimental comparison only.

## Indexed APK/ZIP path

- Builds and sorts the APK central-directory index once at startup.
- Safely accelerates the authentic ARM `CCFileUtils::getFileDataFromZip` and `existFileDataFromZip` instance methods using their verified r1/r2/r3 ABI.
- Does not overwrite either ARM function. Calls for another ZIP, unreadable arguments, allocation failures, or other uncertain cases continue through the original ARM implementation.
- Uses the wrapper guest allocator for returned file buffers, preserving the game's normal `delete[]`/`free` ownership path.
- Adds `zip-get/exist/miss/fallback` counters to the performance profile.
- Keeps OpenGL redundant-bind tracking coherent when a currently bound buffer is deleted.
