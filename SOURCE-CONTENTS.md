# Source contents — 0.9.7-cof1

Included:

- unified native launcher and Windows build scripts;
- x86 backend carried unchanged from `gdpstweaks16`;
- legacy ARM backend carried forward with only COF1 version metadata changed;
- ARMv7 backend with the EnduranceTest10/old companion editor handler restored;
- shared Windows audio/network/storage/runtime bridges;
- GDPS/account/save/network and desktop support code retained from the current
  unified wrapper;
- source/licenses required by the project build.

Deliberately removed from the active ARMv7 source:

- stock 2019/2022/2023 editor-profile detection;
- wrapper-built `LevelEditorLayer` initialization;
- wrapper-owned editor CCArray/vector/object reconstruction;
- stock editor sprite-frame fallback/alias path;
- stock Preview Mode observer and transition repair;
- stock late-beta visibility replacement;
- stock-editor null-batch constructor guard introduced during the repair line;
- ARMv7 W/A/S/D/Q/E editor-command injection.

The retired implementation is distributed separately as the
`0.9.7-cof1-retired-2.2-editor-backup.zip` artifact. It is not compiled into the
COF1 source tree.

No APK, IPA, proprietary game `.so`, built EXE/DLL, or save data is included in
the source archive.
