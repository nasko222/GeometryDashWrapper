# 0.9.4-arm-v22beta-bringup18-runtime-hooks

- Recovers the observed end-screen null `CCString::floatValue()` fault without
  hiding unrelated invalid guest memory accesses.
- Adds companion `libgame.so` constructor execution and named safe/all
  `ApplyHooks` profiles.
- Uses the wrapper's host `HookManager::do_hook` bridge; `libhooking.so` and
  `libdobby.so` remain unexecuted.
- Adds external/donor `libgame.so` support for stock SubZero late-layout APKs.
- Adds a second build argument accepting a donor APK/ZIP or raw `libgame.so`.
- Adds safe and all-hooks launchers with separate logs and profiles.
- Keeps portable save redirect/migration and platformer input fixes from
  Bringup17.
