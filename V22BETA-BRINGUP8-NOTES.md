# Geometry Dash 2.2 beta ARMv7 Bringup8

Bringup8 corrects two mistaken boundaries from Bringup7:

1. My Levels is not the level editor. The real wrench-and-hammer callback is `EditLevelLayer::onEdit`, which is a two-byte no-op in both beta engines. The callback pointer is now redirected to a bridge that creates and enters `LevelEditorLayer`.
2. The low-level inflater returns correct level bytes, but the beta still passes an empty string to `PlayLayer::prepareCreateObjectsFromSetup`. The single parser callsite now repairs only that empty handoff using the last verified `kS` payload and the beta's own COW string builder.

No APK is included. Build with an external APK.
