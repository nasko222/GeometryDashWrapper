# EnduranceTest12

Focused regression correction over EnduranceTest11.

- Preserves the confirmed 2.2 editor background/black-region fix, BPM guides,
  song line, swipe selection, object visibility and Practice Z/X paths.
- Removes the global missing-ground texture substitution that broke **Show
  Ground**. `CCSpriteBatchNode::initWithTexture` now rejects only an actually
  null texture before mutating the node; valid ground construction is untouched.
- Keeps the exact host `updateBlendFunc` implementation for valid batch nodes.
- Removes the failed muted 100 ms legacy decoder prime. Legacy ARM now performs
  one immediate second MCI start on the first non-looping play of each newly
  opened level alias, matching the state transition observed on attempt 2 while
  applying no volume override.
- The complete x86 backend is unchanged.

Runtime verification on Windows is still required for Show Ground and the
legacy attempt-1/attempt-2 loudness comparison.
