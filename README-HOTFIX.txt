PerformanceTest2 Windows build hotfix 1

Fixes:
  src/arm_wrapper.c: use of undeclared identifier 'PF_X'

Apply:
  Copy the contents of this folder into the PerformanceTest2 project root.
  Allow src/elf32.h and documentation files to be replaced.
  Run BUILD_WINDOWS.cmd again.

Unicorn is already built and should report "ninja: no work to do." Only the
wrapper executable needs to compile and link again.
