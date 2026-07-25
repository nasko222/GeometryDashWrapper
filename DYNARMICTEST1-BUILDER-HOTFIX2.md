# DynarmicTest1 builder hotfix 2

This hotfix fixes the CMake configure failure:

```text
Could NOT find Boost (missing: Boost_INCLUDE_DIR)
```

The x64 builder now downloads the official Boost 1.84.0 Windows source archive from `archives.boost.io`, verifies SHA-256
`cc77eb8ed25da4d596b25e77e4dbb6c5afaac9cddd00dc9ca947b6b268cc76a4`, and extracts only the `boost/` header tree plus `LICENSE_1_0.txt`.

No Boost installation, Visual Studio, vcpkg, administrator access, or system environment changes are required. The cached headers are stored under:

```text
.build-tools\boost-1.84.0\boost_1_84_0
```

The builder passes the local header path directly to CMake and forces the legacy `FindBoost` policy required by pinned Dynarmic 6.7.0. Its builder revision changed, so the incomplete CMake configure directory from the failed build is removed automatically on the next run.

Apply this hotfix over DynarmicTest1 and run:

```text
BUILD_DYNARMIC_X64.cmd
```
