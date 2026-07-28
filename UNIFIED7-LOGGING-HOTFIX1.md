# Unified7 Fix2 — Logging Hotfix 1

This update deliberately changes no editor, renderer, shader, HTTP, save parser,
account, audio, input, or compatibility behavior.

## Launching

- Double-click `RUN_AUTO.cmd` to use `game.apk` beside it.
- Drag any `.apk` file onto `RUN_AUTO.cmd` to launch that APK directly.
- The dragged APK is used in place; it is not copied, renamed, or modified.

## Log layout

Every launch receives its own directory:

```text
logs\YYYY-MM-DD\HH-MM-SS__android.package__vVERSION__backend\
```

The directory contains `run-info.txt`, the backend log, and ARM profile/import
files where applicable. `run-info.txt` records the APK path, APK size, Android
package, manifest version name/code, selected backend, server and launch options.

No completed run folder is deleted or overwritten. Fixed-name files left in the
wrapper root by older builds are preserved under `logs\_old-root-files\`.
`logs\latest-run.txt` points to the most recent run directory.

The existing x86 executable still writes `gd-wrapper.log` temporarily in the
wrapper root. The launcher moves it into the run directory immediately when the
process exits, including after a normal Windows crash return.
