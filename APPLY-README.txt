Bringup20 source update
========================

Base: Bringup19 commit 9d457c2
Target: Bringup20 commit 63e723c

Preferred: apply GeometryDashWrapper-0.9.4-arm-v22beta-bringup20-dyn14-network.patch with `git am`.
Alternative: apply the .diff with `git apply`.

For a manual overlay, copy all ordinary files into the Bringup19 source tree,
then remove every path listed in DELETED-FILES.txt.

No APK is included. The builder no longer rejects APKs by exact size/hash.
