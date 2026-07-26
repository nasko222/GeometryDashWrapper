# DynarmicTest14 technical notes — direct asynchronous effects

The normal-PC Test13 log showed stable 60 FPS gameplay but repeated death frames of roughly 130-300 ms. Effects were already decoded and preloaded, proving that OGG extraction was not the remaining problem. The old call still entered `SimpleAudioEngine::playEffect`, built the JNI call, and executed synchronous MCI decoder commands before returning to the render loop.

Test14 patches the Thumb `SimpleAudioEngine::playEffect` prologue (`0xB500`) and `preloadEffect` prologue (`0xB510`). R0 is the disposable `this` argument. R1 contains the path, R2 the loop flag, R3 the soft-float pitch bits, and the caller stack contains pan and gain. The host returns an effect identifier immediately after safely copying the command.

A Windows worker drains effect commands in order. This preserves play-then-volume and play-then-stop ordering without making the render thread wait for MCI. The queue capacity is 256; a rare overflow logs once and falls back to synchronous execution rather than dropping the command.

The effect worker and main music path share the existing MCI lock. Effect-slot state has a separate SRW lock, and shutdown joins the worker before closing MCI aliases.

The first text-input delay in the same log occurred alongside first-time chat-font/loading asset work. Test14 preloads those native APK members. It does not fake or automatically open a text field, so game state remains unchanged.
