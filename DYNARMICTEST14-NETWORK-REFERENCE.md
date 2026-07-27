# DynarmicTest14 network reference

The important working behavior copied from DynarmicTest14 is not a wholesale
replacement of the v22 runtime. It is the ordering of the worker wake:

1. increment the semaphore;
2. set the foreground import result;
3. advance the foreground PC past the import stub;
4. immediately run/resume the registered guest HTTP worker;
5. restore the foreground guest state after the worker yields.

NetworkTest2 and NetworkTest3 changed step 4 into a next-frame wake. NetworkTest4
restores immediate wake ordering but uses the newer bounded worker slices,
nonblocking WinSock implementation, asynchronous DNS, and 4 ms hard watchdog.
All editor, save, audio, platformer, APK cache, lifecycle, and companion-library
fixes remain from the v22 baseline.
