# OverkillTest2 findings from logs 39-43

## What the previous overkill test actually proved

The visual baseline did help, but it did not remove the whole cost. With audio,
particles and original PNGs removed, the late Clutterfunk windows improved from
roughly 22-29 FPS in the normal wrapper to about 40-48 FPS. That means cosmetic
and texture work matters, but a large CPU-side cost remains.

## Why the buttons appeared to do nothing

Two diagnostic bugs invalidated the strongest tests:

1. F4 and F7 attempted to patch verified ARM functions after the immutable-image
   corruption guard had been armed. The guard rejected every write with
   `UC_ERR_WRITE_PROT`; F4 reported `applied=0 failed=7`, and F7 never disabled
   `CCNode::visit`.
2. Windows key auto-repeat toggled held function keys more than once. F3 removed
   `nativeRender` and restored it almost immediately, while F9 changed state
   repeatedly.

OverkillTest2 fixes both issues.

## Why extracting the APK cannot fix steady gameplay

The slow steady gameplay windows already report:

- `apk-opens=0`
- `file-opens=0`
- `file-read=0.00 MiB`
- `zlib=0`

Extracting the APK can remove level-entry and first-use stalls, but it cannot
change a frame in which the wrapper performs no archive or decompression work.

## The next decisive measurement

F11 now installs a translated ARM basic-block profiler for exactly five seconds
at the current gameplay position. It then removes the hook, regenerates normal
translation blocks and logs the 16 hottest guest blocks with nearest exported
function names.

Those functions are the candidates for native x86 replacement or direct TCG
helpers. This is the first test that measures the core ARM workload rather than
removing another external subsystem.
