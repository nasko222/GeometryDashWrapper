# EnduranceTest9

EnduranceTest9 is a narrow 2.2 beta render/lifecycle correction based on the
user's `logs(10).zip`. BPM guidelines are confirmed improved in EnduranceTest8,
so their working setup path is retained unchanged.

## Editor background position anchor

EnduranceTest8 suppressed the one audited `updateCameraBGArt()` call inside
`LevelEditorLayer::updateEditor`, yet the black wrapped region continued moving.
The same update function runs gradient/extra-layer work before that call. The
new hook captures the actual background nodes at editor fields `0x498`, `0x49C`
and `0x4A0`, then restores their CCNode positions at the exact background-art
callsite. If a texture selection replaces a node pointer, the new node is
recaptured automatically. Gameplay callsites are not patched.

## Ground/background missing-texture safety

The failing log enters the host hook with a non-null batch node. The nested guest
`CCSpriteBatchNode::updateBlendFunc()` later loses its saved object register and
faults while reading texture offset `0x54`. Exact disassembly shows the helper is
only four field operations:

- texture atlas at batch-node `+0x108`;
- texture at atlas `+0x48`;
- premultiplied-alpha byte at texture `+0x54`;
- blend source/destination at batch-node `+0x10C/+0x110`.

EnduranceTest9 reproduces those accesses host-side. Missing community texture
entries use the class's normal non-premultiplied-alpha defaults and return
safely instead of entering the fragile nested helper. Unsupported art may still
appear blank/default because the APK does not contain the asset.

## Legacy audio

The EnduranceTest8 first-play volume-1000 experiment had no effect. Its state,
setter, startup call and playback branch are fully removed. No new volume guess
is added in this focused build.

## Retained unchanged

- working BPM `levelSettingsUpdated()` session setup;
- song guide, selection rectangle and client-array/VBO correction;
- editor camera culling/object lifecycle;
- Z/X Practice Mode controls;
- x86 backend and frame pacing;
- networking, backups and version-isolated saves.

The background anchor and missing-texture safety still require a Windows/APK
runtime test before they can be called confirmed fixes.
