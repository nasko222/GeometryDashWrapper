# 0.9.4-arm-dynarmictest1

- Starts the Windows x64 migration.
- Adds a pinned public Dynarmic GitLab checkout with vendored dependencies.
- Disables Git Credential Manager and interactive authentication for the public checkout.
- Removes incomplete leftovers created by the retired GitHub repository URL.
- Adds an ARMv5TE Thumb execution smoke test.
- Adds sparse 32-bit guest-memory callbacks on a 64-bit host.
- Adds an internal APK central-directory reader and deflate extractor.
- Loads and maps the authentic Geometry Dash ARM `libgame.so`.
- Verifies 238 constructors and the JNI/native renderer exports.
- Reports ELF segments, symbols, imports, and relocation counts.
- Does not yet execute the authentic constructors or launch the game.

## Builder hotfix 3

- Added an automatic compatibility patch for fmt 10.1 with Zig 0.14.1 libc++.
- Avoids the removed private `<__std_stream>` header while preserving ostream support.
- Bumped the builder cache revision so the failed Dynarmic build is reconfigured automatically.
