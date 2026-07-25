# DynarmicTest1 builder hotfix 3

This hotfix resolves the Zig 0.14.1/libc++ build failure:

```text
fatal error: '__std_stream' file not found
```

Dynarmic 6.7.0 vendors fmt 10.1.0. Its Windows/libc++ ostream adapter tries
to include the old private libc++ header `<__std_stream>`. Zig 0.14.1 ships a
newer libc++ where that private header is no longer available in the public
include tree.

The builder now applies an idempotent source compatibility patch to the cached
Dynarmic checkout before CMake runs. The patch uses `__has_include` and disables
only fmt's optional direct Windows console stream-buffer access when the private
header is absent. Standard ostream formatting and Dynarmic functionality are not
disabled.

The builder revision is bumped so the failed Ninja/CMake cache is automatically
removed and configured again. No manual deletion is required.
