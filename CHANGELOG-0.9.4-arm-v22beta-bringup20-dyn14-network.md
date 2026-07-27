# 0.9.4-arm-v22beta-bringup20-dyn14-network

- Ports the proven DynarmicTest14 cooperative HTTP execution path to the newer
  APK's pthread-condition-variable wake-up model.
- Implements cooperative `pthread_cond_init`, `pthread_cond_destroy`,
  `pthread_cond_signal`, `pthread_cond_broadcast`, and `pthread_cond_wait`.
- Adds explicit worker wake/yield/network diagnostics.
- Removes the hard-coded APK size and SHA-256 build rejection.
- Keeps local-only saves and the desktop custom-song text-field position fix.
- Excludes the temporary bad 90/95 MB APK from current development scope.
