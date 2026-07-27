# Building NetworkTest7

Run on the existing Windows x64 build workspace:

```bat
BUILD_V22BETA_X64.cmd game-v22beta-selected.apk
```

Output:

```text
dist-arm-wrapper-v22beta-networktest7-native-winhttp-bridge\
```

NetworkTest7 adds the Windows system library `winhttp` to the existing link.
No additional third-party networking dependency is required.
