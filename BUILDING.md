# Building EnduranceTest5 on Windows

Use the included native build scripts from a normal Windows command prompt.
Python is not required.

## Complete build

```bat
BUILD_ALL.cmd
```

## Individual targets

```bat
BUILD_LAUNCHER.cmd
BUILD_X86.cmd
BUILD_DYNARMIC.cmd
```

The Dynarmic builder revision is pinned to
`dynarmic-x64-builder51-0.9.5-endurancetest5` so an older cached builder is not
silently reused.

## Source policy

The archive intentionally contains no APK, extracted `.so`, built `.exe` or
`.dll`, Python file, `.git` directory, or `.gitignore`.
