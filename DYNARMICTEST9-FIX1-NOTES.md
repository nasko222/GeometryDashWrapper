# DynarmicTest9 Fix1 notes

The original Test9 source must not be used. It installed this Thumb trampoline:

```text
ldr r3, [pc, #0]
bx  r3
```

For `CCFileUtils::getFileDataFromZip`, R3 is the output-size pointer. Clobbering it made the host hook write into the imported-code region and caused the `nativeInit` exception near cocos2d RTTI data.

Fix1 uses R0 as the branch scratch register. The host replacement does not need the `this` pointer, while R1, R2 and R3 remain the ZIP path, member path and output-size pointer.

Expected startup marker:

```text
RESULT: DYNARMIC_CCFILEUTILS_ZIP_HOOKS_READY count=2 scratch=r0 args=r1-r3-preserved
```

Then nativeInit should proceed to the normal Android/cocos2d loading logs and `RESULT: DYNARMIC_NATIVE_INIT_RETURNED`.
