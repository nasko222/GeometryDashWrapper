# Geometry Dash ARM Wrapper 0.9.4-arm-bootstrap13

- Expanded the ARM guest heap from 256 MiB to 512 MiB and the allocation
  metadata table from 131,072 to 1,048,576 records.
- Changed free-block selection to prefer the matching size class while bump
  space remains, preventing tiny C++ allocations from consuming large image
  and level-parser buffers.
- Added free-record recycling at the metadata ceiling and detailed free-space
  diagnostics for any remaining allocation failure.
- Made ARM stdio game-save writes transactional. Failed guest sessions preserve
  the previous save instead of committing an incomplete 22-byte replacement.
- Added startup recovery for the exact 22-byte incomplete saves produced by
  bootstrap12 allocator failures.
- Fixed `atoi` and `atol` so they no longer treat the unrelated ARM `r1`
  register as a `strtol` end-pointer and write into immutable Cocos strings.
- Retained bootstrap12's untruncated level strings, linear tokenization,
  pthread initialization, level tracing, decoded-effect cache, and the prior
  `qsort`, particle, label, OpenGL, and lifecycle compatibility work.
