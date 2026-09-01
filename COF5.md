# Geometry Dash Wrapper 0.9.7-cof5

COF5 removes the abandoned Geometry Dash 1.02 comments-hotkey experiment and
returns to the clean COF2 gameplay/editor baseline while retaining the FPS
control added afterward.

## Comments rollback

The 1.02 comments experiment is completely removed from the active wrapper:

- no C-key interception;
- no 1.02 comment build/capability detector;
- no InfoLayer / LevelInfoLayer comment symbol mapping;
- no native comment-open event or scene path;
- no GD_GAME_VERSION_CODE environment export added for that feature.

Geometry Dash 1.02 therefore behaves like the normal COF2 wrapper again.

## FPS control

`FPS=VSYNC` is the default on x86, legacy ARM, and ARMv7. It enables swap
interval 1 when supported. A numeric value such as `144`, `240`, or `9999`
disables VSync and enables the shared QPC host-side frame cap at that value.
Invalid values fall back to VSYNC behavior.

## Lost-game message

When `I_LOST_THE_GAME` is false, every launcher/backend message now says exactly:

`You lost the game. Launch through launch.cmd instead`

## Preserved behavior

All COF2 fixes remain in place, including the EnduranceTest10/companion 2023
editor path, platformer keyboard repair, gameplay Edit callback repair, and the
late-2023 ground/background OOB clamps. The stock 2019/2022/2023 wrapper-built
editor reconstruction remains removed.
