# EnduranceTest2

This branch is a narrow stability correction over EnduranceTest1.

## Changes

- The exact companion editor-visibility redirect is disabled by default. The
  uploaded 2.2 runs spent multiple seconds inside one visibility/render pass,
  so that experiment is treated as unsafe. The faster Fix6 host visual path is
  restored.
- Synthetic x86 API sockets remain logically connected after an interim
  `HTTP/1.1 100 Continue` response and report `POLLOUT`. This allows old clients
  such as 1.93 to send the account-backup body. Pending headers are also joined
  when the body arrives through `writev`.
- x86 cursor visibility follows the actual Cocos pause/options scene graph. It
  stays visible while a pause overlay is present and hides again after Resume.
- The accepted x86 frame-pacing implementation is unchanged.

## Known editor status

The exact visibility experiment did not contain the DrawGrid time-marker update
that owns the song line and BPM guidelines, so it could not solve those overlays.
EnduranceTest2 intentionally removes the freeze first. The black moving region,
song line, and BPM guidelines remain open issues and are not claimed fixed.
