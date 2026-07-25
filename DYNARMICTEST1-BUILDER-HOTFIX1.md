# DynarmicTest1 builder hotfix 1

The original builder referenced the retired `MerryMage/dynarmic` GitHub URL. Git treated the inaccessible repository as potentially private and opened Git Credential Manager.

This hotfix:

- uses the public GitLab mirror: `https://gitlab.com/suyu-emu/dynarmic.git`;
- pins commit `a41c380246d3d9f9874f0f792d234dc0cc17c180`;
- disables Git Credential Manager and all interactive Git authentication;
- uses the mirror's vendored dependency trees instead of recursive submodule cloning;
- automatically removes an incomplete checkout left by the old builder;
- stores the new checkout separately at `.build-tools\dynarmic-gitlab-a41c380246d3-src`.

After replacing `build-dynarmic-x64.ps1`, close any GitHub sign-in prompt and rerun:

```powershell
.\BUILD_DYNARMIC_X64.cmd
```

A public checkout failure now stops with an ordinary terminal error and cannot open an authorization window.
