Bootstrap15 Windows builder cumulative hotfix 5

Copy the CONTENTS of this folder into the bootstrap15 source root and allow all
files to be replaced. Then run:

  powershell -ExecutionPolicy Bypass -File .\build-windows.ps1

Expected message when upgrading the current builder5 failure:

  Updating Unicorn final-link rule for bootstrap15-windows-builder6 (preserving object cache)

The previous run already compiled all Unicorn objects. This hotfix removes the
final Windows symbolic-link command that required Developer Mode/admin rights.
It keeps libunicorn.a as the real archive used by the wrapper, so the next run
should normally rebuild only the final archive step before compiling the wrapper.

This ZIP is cumulative and also contains the prior Windows-native CMake/config
changes, so it may be applied to an older bootstrap15 source tree.
