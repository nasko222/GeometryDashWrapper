# 0.9.4-arm-dynarmictest12

- Replaced the `WSAPoll`-based guest `poll()` bridge with a Windows `select()` implementation using Linux-compatible event bits.
- Added bounded readability wait and retry for nonblocking `recv()`.
- Added bounded writability wait and retry for nonblocking `send()`.
- Added diagnostics for poll readiness, receive waits, EOF, invalid guest buffers, and genuine socket errors.
- Preserved Test11's working browser hook, correct `SO_ERROR` translation, APK cache, audio, save stability, input fixes, clean shutdown, and GPU preference.
