# Geometry Dash ARM Wrapper — Milestone 1

Version and branch snapshot: `0.9.4-milestone1`

This freezes the exact working v2.2 beta wrapper state after the APK server override was restored and all tested online features began working.

## Included working state

- Dynarmic ARMv7-A, Thumb-2, VFPv3 and NEON execution.
- Local save redirection.
- Editor and platformer compatibility fixes from the selected v2.2 beta branch.
- Concurrent native `CCHttpClient::send` bridge using independent WinHTTP request threads.
- Generic HTTPS-first handling that preserves the hostname supplied by the APK, including custom GDPS hosts.
- Safe HTTP fallback for retryable read/download requests.
- Redirect support, empty User-Agent compatibility and HTML error-page rejection for API calls.
- Working level/server requests, song metadata and song downloads with the corrected APK.

## APK

No APK is included. Place the known-working v2.2 beta APK beside the source. The APK must have the unwanted GDPS override reverted to the intended server configuration.

## Build

```bat
BUILD_V22BETA_X64.cmd game-v22beta-selected.apk
```

Output directory:

```text
dist-arm-wrapper-0.9.4-milestone1
```

Runtime diagnostics:

```text
gd-milestone1.log
gd-milestone1-imports.txt
gd-milestone1-profile.csv
gd-milestone1-profile-summary.txt
```
