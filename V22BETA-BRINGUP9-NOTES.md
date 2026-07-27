# Geometry Dash 2.2 beta ARMv7 Bringup9

Bringup8 established two real milestones: several official levels enter gameplay, and the wrench-and-hammer callback reaches a new editor scene. Bringup9 addresses the two remaining boundaries visible in those logs.

## Level boundary

Power Trip inflated 92,024 bytes to 889,245 bytes and entered gameplay. Knock Em Out inflated 124,252 bytes to 1,091,308 bytes, but `LevelSettingsObject::objectFromString` returned null before `PlayLayer` retained the settings object.

Bringup9 preserves native parsing first. A fallback activates only after a null result and strips the unsupported `kS38` color-channel block before retrying. A minimal default settings retry is the final guarded fallback. Level objects are not replaced.

## Editor boundary

Bringup8 reached `LevelEditorLayer::draw`, where member `this + 0x2BFC` was null. The companion `libgame.so` implementation of `LevelEditorLayerExt::initH` initializes that array and the wider editor state.

Bringup9 maps the optional companion module at `0x18000000`, resolves its main-game imports against `libcocos2dcpp.so`, and calls only `LevelEditorLayerExt::initH` on the created editor object. It does not run Dobby or the companion JNI hook manager.
