# GD Wrapper 0.9.4-arm-bootstrap8

This is a native Windows compatibility wrapper for the original x86 Android
game code inside a supported APK. It is not BlueStacks/Nox, does not boot
Android, and does not borrow logic from a Windows Geometry Dash executable.
The visible title is version- and game-neutral; the established x86 package
keeps the familiar `GeometryDashWrapper` filename. This source release also
adds the separate `GeometryDashArmWrapper` executable for ARM-only Geometry
Dash 1.0 through 1.4 APKs.

Version `0.9.4-arm-bootstrap8` raises the guest allocation-record capacity from
32,768 to 131,072 and moves the metadata table off the Windows stack. This
addresses the exact record-table exhaustion that stopped Stereo Madness while
only 139 MiB of the 256 MiB heap was occupied. Closed APK-file and completed
zlib-stream slots are reused for longer sessions. A byte-signature-checked
Geometry Dash 1.4 guard also makes `PlayLayer::claimParticle` take its existing
null-return path when the destination particle pool is absent, instead of
calling `CCArray::addObject` through a null pointer at the shared gameplay crash
address from both bootstrap7 test logs.

Version 0.9.3-alpha3 fixes numbered saves such as `CCGameManager2.dat` and
`CCLocalLevels2.dat`: they are routed into `save/`, and root-level copies from
older builds are migrated automatically. The same routing now covers the
`CCGameSave` and `CCGameStatistics` families used by Boomlings.

Sound effects are no longer limited to the Ogg files compiled into a particular
wrapper build. Alpha3 extracts and decodes requested Ogg effects from the
currently installed `game.apk`, caches a payload-specific WAV, and retains the
embedded files only as a fallback. This is intended to cover Boomlings and
other RobTop APKs without a game-specific EXE. MP3 playback also retries through
a metadata-free cache copy when Windows MCI rejects a song with a large ID3
cover image.

The HTTPS secure-random bridge from alpha2 remains. Alpha3 adds byte-count-only
TLS diagnostics and a guarded GD 2.11 song-completion diagnostic showing the
HTTP result and curl error without logging request bodies, usernames, passwords,
or response contents. It also implements the legacy Bionic `ftime` call used by
older games. The wrapper retains the serialized, compact-relative-path MCI fix
for long C: installations and the strict MSVCRT stream bridge.

It includes:

- current-APK Ogg-to-WAV decoding with payload-specific caches, so effect sets
  follow the selected game instead of the EXE that happened to be built;
- ID3-stripped MP3 retry files for legacy Windows MCI when album art prevents a
  valid downloaded song from opening;
- routing and migration for numbered `CCGameManager`, `CCLocalLevels`,
  `CCGameStore`, and `CCData` saves plus Boomlings' `CCGameSave` and
  `CCGameStatistics` files;
- a Bionic/i386-compatible `ftime` bridge for older Cocos titles;
- credential-safe TLS transfer summaries and a version-guarded custom-song
  response diagnostic for distinguishing HTTP failure from audio failure;

- automatic discovery of both modern `lib/x86/libcocos2dcpp.so` and the
  Geometry Dash 1.6 name `lib/x86/libgame.so`;
- the legacy `Cocos2dxActivity.nativeSetPaths` APK-path bridge used by 1.6;
- Android-compatible `arc4random`, microsecond `clock`, `getcwd`, `round`, and
  semaphore bridges used by 1.6 but absent from the later 1.8 import set;

- deterministic implementations of FMOD stream-buffer, output-type, and
  software-format configuration so Meltdown does not consume uninitialized
  values during startup;
- an Android-compatible legacy x86 `setjmp`/`longjmp` bridge for Cocos image
  decoding instead of returning through a generic zero stub;
- a bounded 96-DWORD crash-time x86 stack dump with ELF-relative return addresses, so a
  failure inside stripped Android code can be traced to its real caller;
- a diagnostic detour around the authentic Cocos Android asset reader that
  records the requested filename, mode, result, and byte count without replacing
  the reader itself;
- correct legacy Bionic character-classification tables used by TinyXML and
  the C/C++ standard libraries;
- strict MSVCRT affinity for every ordinary `FILE *` returned by the imported
  `fopen`, including `fseek`, `fread`, `fclose`, and the other stdio calls;
- translation of Bionic `__sF` standard-stream pointers for `fprintf`,
  `fputc`, `vfprintf`, and the remaining imported stdio operations without
  exposing those Android objects to a Windows CRT;
- a targeted TinyXML diagnostic that reports the return value and document
  error code when Meltdown parses `objectDefinitions.plist`;
- live WASAPI peak metering for the FMOD DSP API used by newer music-reactive
  glow and pulse effects;
- bidirectional Android-to-Winsock `poll()` event translation for native HTTP
  clients, plus safe connection/readiness/send/receive milestone diagnostics;
- support for Android `SOCK_NONBLOCK` and `SOCK_CLOEXEC` flags without passing
  their Linux-only bits to Winsock's `socket()` type parameter;
- correct `strtoll`/`strtoull` 64-bit x86 return values for libcurl HTTP length
  and transfer-metadata parsing;
- a dual-call-site `strerror_r` bridge that both fills and returns its buffer
  for the two Android libraries that use different historical conventions;
- stateful Android/i386 `sigaction`, `bsd_signal`, and `sigprocmask` shims so
  curl/OpenSSL can save and restore virtual SIGPIPE state safely on Windows;
- Android/Winsock error descriptions through `strerror`, `strerror_r`, and
  `gai_strerror`, including the translated Linux errno values used by curl;
- a stable locally generated 16-hex-digit Android-style ID retained in wrapper
  preferences for account and other identity-dependent requests;
- bounded, credential-safe HTTP diagnostics showing method/path, status line,
  response category, address family, and translated connection errors without
  logging POST data or server-provided account text;
- a Windows secure-random pseudo-device for Android OpenSSL, preventing HTTPS
  downloads from falling through to unsupported EGD Unix sockets;
- Bionic-compatible `strtok_r`, `gmtime_r`, `writev`, `getnameinfo`, legacy
  resolver flags, interface-name lookup, and synthetic Android app IDs;
- separate per-channel and master effect volumes, preventing one muted FMOD
  channel from silencing later level-enter/level-exit effects globally;
- bounded effect-play diagnostics for confirming `playSound_01` and
  `quitSound_01` requests without flooding the log;
- translation of downloaded custom-song names and Android writable paths to
  the real Windows `save\\<song-id>.mp3` file before APK-cache fallback;
- repeatable `--effects-apk` build inputs for producing one EXE with a union of
  compatible fallback effects from several game APKs;
- inactive-window render suspension to prevent old front/back frames flickering
  when tabbing away;
- serialized MCI commands and compact executable-relative audio paths, avoiding
  intermittent effects and long-path failures on C: installations;

- corrected FMOD start-paused behavior for 1.9 level music: the Windows audio
  stream is now armed during level loading and started only when the game
  releases the channel, fixing silence on the first attempt while preserving
  seeking and subsequent resets;
- reliable 1.9 pause/unpause playback: the stream is restarted from the exact
  paused position instead of leaving Windows MCI in a stopped state;

- a native FMOD 1.05.04 compatibility layer for the 25 FMOD calls used by
  Geometry Dash 1.9, routed into the existing Windows MCI audio backend;
- 1.9 background music, seeking, looping, pause/resume, fades, effects, and
  volume control without loading Android's `libfmod.so`;
- automatic APK extraction for the new `BlastProcessing.mp3` and
  `TheoryOfEverything2.mp3` songs, just like the older songs;
- runtime conversion of the selected APK's effects, so switching between 1.8,
  1.9, Meltdown, or another compatible game does not require a loose audio
  folder;

- one consistent MSVCRT file ABI for translated Cocos save reads, fixing the
  0.8.1 startup crash after it found `CCGameManager.dat` and
  `CCLocalLevels.dat`;
- a virtual Android `/save/` path translated to the Windows `save\\` directory,
  fixing the mismatch where 0.8 could write data but Cocos searched for it
  inside the APK when loading;
- automatic migration of recognized root-level `CC*.dat` files, including
  numbered variants and backups, into `save\\`;
- Android pause/resume lifecycle delivery on Windows focus changes and shutdown,
  which triggers the game's own progress and editor-level saves;
- Windows default-browser support for the game's external URL and app-page
  buttons;
- durable implementations of Cocos `get/set*ForKey` preferences;
- atomic implementations of RobTop's `saveAndEncryptStringToFile` and
  `loadAndDecryptFileToString` bridge used by `CCGameManager.dat` and
  `CCLocalLevels.dat`;
- automatic MP3 extraction from `game.apk` into a persistent cache;
- runtime extraction and decoding of Ogg effects from `game.apk`, with optional
  build-time embedded effects retained as a fallback;
- a positive Windows implementation of Android `isNetworkAvailable()`;
- the 0.6 POSIX/Winsock bridge and corrected editor drag/swipe JNI calls.

## Required files

Only these runtime files are required:

- `GeometryDashWrapper.exe`
- `game.apk`

There is no required loose `libcocos2dcpp.so` and no required `audio/` folder.
The PDB and command files are useful for diagnosis but are optional.

The wrapper creates `save/` beside the executable. It contains game progress,
local editor levels, preferences, and the automatic audio cache. **Do not
delete `save/`.** If you extract a future wrapper release into a different
folder, copy the old `save/` folder across to keep your progress.

### Upgrading from 0.8

Close the game, then copy the alpha3 EXE into the same folder where the older
wrapper was running. On first launch, alpha3 automatically moves recognized
root-level saves—including numbered variants and backups—into `save/` when a
same-named destination does not already exist. Existing destination files win;
the wrapper never overwrites them during migration.

## Running and testing

Extract the complete folder and double-click `RUN_NATIVE_BOOT.cmd`. Do not run
the executable from inside the archive.

Please test this build in this order:

1. Retain/copy your existing `save/` folder and launch once. Confirm that
   `CCGameManager2.dat` and `CCLocalLevels2.dat` were moved from the wrapper root
   into `save/` and that the game loads them.
2. Put the Boomlings APK you tested at `game.apk`. Its first requested effect
   should log `decoded ... from current game.apk`; confirm previously silent
   buttons now have sound and that its save files persist.
3. Put the GD 2.11 APK at `game.apk`. Test a cached custom song, your
   `661012.mp3` copied into `save/`, and a fresh HTTPS download. A failed fresh
   download should now produce `Song HTTP completion` and `Network TLS socket
   closed` lines instead of an unexplained result.
4. Recheck comments, menu/official music, entry/exit effects, pause/resume,
   reactive glow, editor input, and one older APK.

The first use of a song/effect may pause briefly while it is cached. Historical
servers may return an error or empty result, but the wrapper should not claim
the Windows host has no connection or remain stuck indefinitely. If something
fails, send `gd-wrapper.log` and describe the exact last action. Before sending,
you can search the log for your password; this build is intentionally designed
never to record request bodies.

Diagnostic commands:

- `RUN_PROBE.cmd` runs constructors and `JNI_OnLoad` without opening the game.
- `RUN_RELOCATION_ONLY.cmd` tests only APK extraction, ELF loading, and
  relocations.

## Controls

- Left mouse button: touch, hold, drag, and release.
- Space or Up Arrow: gameplay touch/hold/release.
- Escape: Android Back.
- Keyboard characters and Backspace: active Cocos text field.

## Version compatibility

The loader discovers the x86 library inside the APK, and the wrapper branding
is version-neutral. The user has confirmed 1.5, 1.6, original Meltdown,
Meltdown 1.01, Geometry Dash 2.0/2.1, and Geometry Dash World running with the
current core. Earlier wrapper tests also covered 1.7 through 1.9, but those
versions should be rechecked before a stable release. The x86 executable
cannot run Geometry Dash 1.0 through 1.4/1.41 because those APKs are ARM-only;
use the experimental ARM executable for them. Plain HTTP online actions and
some custom songs have
been confirmed on GD 2.11; CDN HTTPS downloads, other historical versions, and
private servers need retesting with 0.9.3-alpha3. JNI
methods, Cocos exports, audio assets, and OS APIs changed across releases, so
this is not yet a claim that every historical or later APK works.

## Experimental ARM-only graphical branch

`src/arm_wrapper.c` is the first graphical backend for the ARMv5/Thumb-1
libraries used by Geometry Dash 1.0 through 1.4. It uses statically linked
Unicorn 2.1.4 and executes the original Android ARM code; it does not cast ARM
addresses to x86 function pointers, use an Android emulator, or substitute a
Windows Geometry Dash executable.

The earlier `0.9.4-arm-probe1` milestone already loaded the ARM library directly
from an APK, mapped its guest address space, applied `R_ARM_*` relocations,
provided Android kuser atomics/TLS, ran every authentic ELF constructor, and
received JNI 1.4 from the authentic `JNI_OnLoad`. `0.9.4-arm-bootstrap8` retains
those probe modes and adds:

- a Win32 OpenGL window and message/render loop;
- a guest JavaVM/JNIEnv table with `RegisterNatives` capture;
- lookup and invocation of Cocos `nativeInit`, `nativeRender`, pause/resume,
  touch, key, and text-input callbacks;
- JNI object, string, array, preference, save-file, language, identity, URL,
  keyboard, and audio services;
- APK-backed `AAsset` and stdio/POSIX file access with a Windows `save/` path;
- ARM-to-Windows OpenGL ES dispatch for the rendering calls used by early
  Cocos builds;
- existing Windows MCI/Ogg audio and durable storage services.

The default mode now attempts a graphical boot. `--probe` stops after
constructors and `JNI_OnLoad`; `--relocate-only` stops after ELF relocation.
The graphical backend is intentionally labeled a bootstrap because the first
real APK run may identify additional imported functions, JNI methods, or GL
edge cases. Send `gd-arm-wrapper.log` after each test.

See `BUILDING-ARM.md` for the reproducible Win32 translator and wrapper build.
The runtime test folder needs `GeometryDashArmWrapper.exe` and `game.apk`; no
loose `.so` or translator DLL is required.

## Rebuilding

The complete buildable source is included in `source/`. Install Zig, then run
this for the established x86 wrapper:

```text
python build_wrapper.py --zig C:\path\to\zig.exe --apk Geometry_Dash.apk --out dist
```

The ARM wrapper additionally needs the patched Win32 Unicorn static library:

```text
python build_arm_wrapper.py --zig C:\path\to\zig.exe ^
  --unicorn-source C:\path\to\unicorn-2.1.4 ^
  --unicorn-lib C:\path\to\libunicorn.a ^
  --apk Geometry_Dash_1.000.apk --out dist-arm-wrapper
```

The builder verifies that the primary APK contains a little-endian ELF32/i386
game library. Runtime effect decoding needs no external codec. `--effects-apk`
is optional and repeatable; it adds fallback effects to the EXE and requires
FFmpeg only while building. Players never need FFmpeg.

For loader diagnostics, `GeometryDashWrapper.exe --library=path\to\libcocos2dcpp.so`
still permits an explicit loose library, while `game.apk` remains required for
resources during a graphical boot.
