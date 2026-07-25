param(
    [Parameter(Position = 0)]
    [string]$Apk = "",
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ToolsRoot = Join-Path $Root ".build-tools"
$Downloads = Join-Path $ToolsRoot "downloads"
$BuildRoot = Join-Path $Root "build-cache-windows"
$UnicornSource = Join-Path $Root "third_party\unicorn-2.1.4\unicorn-2.1.4"
$UnicornBuild = Join-Path $BuildRoot "unicorn-win32-arm"
$WrapperCache = Join-Path $BuildRoot "wrapper"
$Output = Join-Path $Root "dist-arm-wrapper-overkilltest2"

$ZigVersion = "0.14.1"
$CMakeVersion = "3.31.10"
$NinjaVersion = "1.13.2"
$CMakeSha256 = "13D1A463D7130DF5339BAEDD63D8AE990AAF385062B2F42F372796143AE94086"
$NinjaSha256 = "07FC8261B42B20E71D1720B39068C2E14FFCEE6396B76FB7A795FB460B78DC65"
$BuilderRevision = "bootstrap15-windows-builder6"

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    Write-Host "`n> $FilePath $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath"
    }
}

function Get-CheckedFile {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$Sha256
    )
    $Expected = $Sha256.ToUpperInvariant()
    if (Test-Path $Destination) {
        $Existing = (Get-FileHash -Algorithm SHA256 $Destination).Hash.ToUpperInvariant()
        if ($Existing -eq $Expected) {
            Write-Host "Using cached $([IO.Path]::GetFileName($Destination))"
            return
        }
        Remove-Item -Force $Destination
    }
    Write-Host "Downloading $Uri"
    Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile $Destination
    $Actual = (Get-FileHash -Algorithm SHA256 $Destination).Hash.ToUpperInvariant()
    if ($Actual -ne $Expected) {
        Remove-Item -Force $Destination
        throw "SHA-256 mismatch for $Uri`nExpected: $Expected`nActual:   $Actual"
    }
}

function Expand-ZipOnce {
    param(
        [Parameter(Mandatory = $true)][string]$Archive,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$ExpectedExecutable
    )
    if (Test-Path $ExpectedExecutable) { return }
    if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Write-Host "Extracting $([IO.Path]::GetFileName($Archive))"
    Expand-Archive -LiteralPath $Archive -DestinationPath $Destination -Force
    if (-not (Test-Path $ExpectedExecutable)) {
        throw "Archive extracted, but expected executable was not found: $ExpectedExecutable"
    }
}

function Find-SingleFile {
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $Matches = @(Get-ChildItem -LiteralPath $Directory -Filter $Name -File -Recurse)
    if ($Matches.Count -lt 1) { throw "Could not find $Name below $Directory" }
    return $Matches[0].FullName
}

function Write-AsciiFile {
    param([string]$Path, [string]$Content)
    [IO.File]::WriteAllText($Path, $Content, [Text.Encoding]::ASCII)
}

if ([Environment]::Is64BitOperatingSystem -ne $true) {
    throw "The automatic builder requires 64-bit Windows. The produced wrapper is still a 32-bit EXE."
}

New-Item -ItemType Directory -Force -Path $ToolsRoot, $Downloads, $BuildRoot, $WrapperCache | Out-Null

if ($Clean) {
    Write-Host "Cleaning previous build output"
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $UnicornBuild, $WrapperCache, $Output
    New-Item -ItemType Directory -Force -Path $WrapperCache | Out-Null
}

if (-not (Test-Path (Join-Path $UnicornSource "include\unicorn\unicorn.h"))) {
    throw "The patched Unicorn source is missing. Use the bootstrap15 full-source archive; expected: $UnicornSource"
}

# Zig publishes the archive hash in its official JSON index. Reading it here
# avoids trusting an unverified download while keeping the script self-contained.
Write-Host "Reading the official Zig $ZigVersion download metadata"
$ZigIndex = Invoke-RestMethod -Uri "https://ziglang.org/download/index.json"
$ZigRelease = $ZigIndex.PSObject.Properties[$ZigVersion].Value
$ZigInfo = $ZigRelease.PSObject.Properties["x86_64-windows"].Value
if (-not $ZigInfo) { throw "Zig $ZigVersion x86_64-windows metadata was not found" }
$ZigUri = [string]$ZigInfo.tarball
$ZigSha256 = [string]$ZigInfo.shasum
$ZigArchive = Join-Path $Downloads "zig-x86_64-windows-$ZigVersion.zip"
$ZigDirectory = Join-Path $ToolsRoot "zig-$ZigVersion"
Get-CheckedFile -Uri $ZigUri -Destination $ZigArchive -Sha256 $ZigSha256
if (-not (Test-Path $ZigDirectory)) {
    New-Item -ItemType Directory -Force -Path $ZigDirectory | Out-Null
    Expand-Archive -LiteralPath $ZigArchive -DestinationPath $ZigDirectory -Force
}
$ZigExe = Find-SingleFile -Directory $ZigDirectory -Name "zig.exe"

$CMakeArchive = Join-Path $Downloads "cmake-$CMakeVersion-windows-x86_64.zip"
$CMakeDirectory = Join-Path $ToolsRoot "cmake-$CMakeVersion"
$CMakeExpected = Join-Path $CMakeDirectory "cmake-$CMakeVersion-windows-x86_64\bin\cmake.exe"
Get-CheckedFile `
    -Uri "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-x86_64.zip" `
    -Destination $CMakeArchive -Sha256 $CMakeSha256
Expand-ZipOnce -Archive $CMakeArchive -Destination $CMakeDirectory -ExpectedExecutable $CMakeExpected
$CMakeExe = $CMakeExpected

$NinjaArchive = Join-Path $Downloads "ninja-win-$NinjaVersion.zip"
$NinjaDirectory = Join-Path $ToolsRoot "ninja-$NinjaVersion"
$NinjaExpected = Join-Path $NinjaDirectory "ninja.exe"
Get-CheckedFile `
    -Uri "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/ninja-win.zip" `
    -Destination $NinjaArchive -Sha256 $NinjaSha256
Expand-ZipOnce -Archive $NinjaArchive -Destination $NinjaDirectory -ExpectedExecutable $NinjaExpected
$NinjaExe = $NinjaExpected

Write-Host "`nPortable tools ready:" -ForegroundColor Cyan
Write-Host "  Zig:   $ZigExe"
Write-Host "  CMake: $CMakeExe"
Write-Host "  Ninja: $NinjaExe"

# CMake writes the compiler path into generated .cmake source. Raw Windows
# backslashes there are parsed as escapes (for example, \G is invalid), so the
# Zig and archive-tool paths must use CMake-style forward slashes.
# The C compiler is invoked directly as `zig.exe cc`; a batch wrapper would
# corrupt arguments containing `=` on some Windows cmd.exe configurations.
$env:ZIG = $ZigExe
$env:GD_ARM_QEMU_ZIG = $ZigExe
$env:GD_ARM_ZIGAR = (Join-Path $Root "tools\zigar-win32.cmd").Replace('\', '/')
$env:GD_ARM_ZIGRANLIB = (Join-Path $Root "tools\zigranlib-win32.cmd").Replace('\', '/')
$env:GD_ARM_BUILD_CACHE = Join-Path $BuildRoot "zig-unicorn"
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $BuildRoot "zig-global"
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $BuildRoot "zig-local"
$env:PATH = "$NinjaDirectory;$env:PATH"

# The compiler command changed across builder revisions. Discard the old
# Unicorn CMake tree once so generated compiler rules cannot remain stale.
$BuilderStamp = Join-Path $UnicornBuild ".gd-builder-revision"
$ExistingBuilderRevision = ""
if (Test-Path $BuilderStamp) {
    $ExistingBuilderRevision = (Get-Content -LiteralPath $BuilderStamp -Raw).Trim()
}
if ((Test-Path $UnicornBuild) -and ($ExistingBuilderRevision -ne $BuilderRevision)) {
    if ($ExistingBuilderRevision -eq "bootstrap15-windows-builder5") {
        # Builder6 only removes a privileged Windows symlink post-build step.
        # Keep the already compiled Unicorn objects; CMake will regenerate the
        # final archive rule and normally rebuild only libunicorn.a.
        Write-Host "Updating Unicorn final-link rule for $BuilderRevision (preserving object cache)"
    } else {
        Write-Host "Refreshing Unicorn CMake cache for $BuilderRevision"
        Remove-Item -Recurse -Force $UnicornBuild
    }
}

$Toolchain = Join-Path $Root "tools\unicorn-win32-zig.cmake"
$ConfigureArguments = @(
    "-S", $UnicornSource,
    "-B", $UnicornBuild,
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=$NinjaExe",
    "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$Toolchain",
    "-DUNICORN_ARCH=arm",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DUNICORN_LEGACY_STATIC_ARCHIVE=ON",
    "-DUNICORN_BUILD_TESTS=OFF",
    "-DUNICORN_INSTALL=OFF",
    "-DUNICORN_USE_PREGENERATED_WIN32_CONFIG=ON",
    "-DCMAKE_BUILD_TYPE=Release"
)
Invoke-External -FilePath $CMakeExe -Arguments $ConfigureArguments
Write-AsciiFile -Path $BuilderStamp -Content $BuilderRevision
Invoke-External -FilePath $CMakeExe -Arguments @("--build", $UnicornBuild, "--parallel")

$UnicornLibraries = @(Get-ChildItem -LiteralPath $UnicornBuild -Filter "libunicorn.a" -File -Recurse)
if ($UnicornLibraries.Count -lt 1) {
    throw "Unicorn finished building, but libunicorn.a was not found below $UnicornBuild"
}
$UnicornLibrary = $UnicornLibraries[0].FullName
Write-Host "Using Unicorn library: $UnicornLibrary"

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$GeneratedDirectory = Join-Path $WrapperCache "generated"
New-Item -ItemType Directory -Force -Path $GeneratedDirectory | Out-Null
$EmbeddedEffects = Join-Path $GeneratedDirectory "embedded_effects.c"
Write-AsciiFile -Path $EmbeddedEffects -Content @'
#include "embedded_effects.h"
const EmbeddedEffect *embedded_effect_find(const char *name) { (void)name; return 0; }
'@

$Sources = @(
    (Join-Path $Root "src\arm_wrapper.c"),
    (Join-Path $Root "src\audio_win.c"),
    (Join-Path $Root "src\storage_win.c"),
    $EmbeddedEffects,
    (Join-Path $Root "third_party\stb\stb_vorbis.c")
)
$Sources += @(Get-ChildItem -LiteralPath (Join-Path $Root "third_party\zlib") -Filter "*.c" -File | Sort-Object Name | ForEach-Object { $_.FullName })

$ExePath = Join-Path $Output "GeometryDashArmWrapper.exe"
$CompileArguments = @(
    "cc", "-target", "x86-windows-gnu", "-std=c11", "-O3",
    "-Wall", "-Wextra", "-Wno-cast-function-type",
    "-Wno-deprecated-non-prototype", "-mstackrealign",
    "-Dcrc32=gd_z_crc32",
    "-I$(Join-Path $UnicornSource 'include')",
    "-I$(Join-Path $Root 'src')",
    "-I$(Join-Path $Root 'third_party\zlib')"
)
$CompileArguments += $Sources
$CompileArguments += @(
    $UnicornLibrary,
    "-o", $ExePath,
    "-lws2_32", "-lopengl32", "-lgdi32", "-luser32",
    "-lshell32", "-lwinmm", "-lole32"
)
Invoke-External -FilePath $ZigExe -Arguments $CompileArguments
if (-not (Test-Path $ExePath)) { throw "Wrapper link completed without producing $ExePath" }

$SelectedApk = $null
if ($Apk) {
    $SelectedApk = (Resolve-Path -LiteralPath $Apk).Path
} else {
    $BundledApk = Join-Path $Root "game.apk"
    if (Test-Path $BundledApk) { $SelectedApk = $BundledApk }
}
if ($SelectedApk) {
    Copy-Item -LiteralPath $SelectedApk -Destination (Join-Path $Output "game.apk") -Force
    Write-Host "Copied APK: $SelectedApk"
} else {
    Write-Warning "No APK was supplied. Place game.apk beside the built EXE before running it."
}

Write-AsciiFile -Path (Join-Path $Output "RUN_ARM_NATIVE_BOOT.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo OVERKILL visual mode: audio and cosmetics removed; textures become 1x1 white.
echo Hotkeys: F3 nativeRender, F4 ARM draws, F5 particles, F6 host draws, F7 scene, F8 GL, F9 textures, F10 state, F11 ARM profiler.
GeometryDashArmWrapper.exe --apk=game.apk --overkill
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_CONTROL_PERFORMANCETEST1.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo Control run: all normal performancetest1 subsystems enabled.
GeometryDashArmWrapper.exe --apk=game.apk
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_OVERKILL_BARE_MINIMUM.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo Bare minimum: audio, particles, ARM draw methods, textures and host OpenGL removed.
echo Game update/collision and scene traversal still execute. Window will be blank.
GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-node-draws --no-textures --headless-gl --no-vsync --uncapped
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_OVERKILL_ZERO_RENDER.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo Zero-render baseline: nativeRender is never called. Window will be blank.
GeometryDashArmWrapper.exe --apk=game.apk --no-audio --skip-native-render --no-vsync --uncapped
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_OVERKILL_NO_ARM_DRAWS.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo ARM sprite, batch, atlas, layer, label and shape draw methods removed.
GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-node-draws --no-textures --no-vsync --uncapped
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_OVERKILL_NO_TEXTURES.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo Audio, cosmetics and all texture uploads removed.
GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-textures
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_OVERKILL_NO_DRAWS.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo ARM still builds the scene, but every host draw call is discarded.
GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-textures --skip-draws --no-vsync --uncapped
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_OVERKILL_LOGIC_ONLY.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo CCNode scene traversal is removed. The window may be blank.
GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-node-draws --no-textures --skip-scene-visit --headless-gl --no-vsync --uncapped
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_OVERKILL_HEADLESS_GL.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo Every OpenGL call uses the fake headless backend. The window will be blank.
GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --headless-gl --no-vsync --uncapped
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_PROFILE_IMPORT_TIME.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo Diagnostic callback timing adds overhead. Test one heavy section, then close.
GeometryDashArmWrapper.exe --apk=game.apk --overkill --profile-import-time
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_PROFILE_ARM_BLOCKS.cmd") -Content @'
@echo off
cd /d "%~dp0"
echo Diagnostic ARM block profiling is slow. Test one heavy section, then close.
GeometryDashArmWrapper.exe --apk=game.apk --overkill --profile-arm-blocks
if errorlevel 1 pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_ARM_PROBE.cmd") -Content @'
@echo off
cd /d "%~dp0"
GeometryDashArmWrapper.exe --probe --apk=game.apk --no-audio
pause
'@
Write-AsciiFile -Path (Join-Path $Output "RUN_ARM_RELOCATION_ONLY.cmd") -Content @'
@echo off
cd /d "%~dp0"
GeometryDashArmWrapper.exe --relocate-only --apk=game.apk --no-audio
pause
'@
Write-AsciiFile -Path (Join-Path $Output "README-ARM-TEST.txt") -Content @'
Geometry Dash ARM Wrapper 0.9.4-arm-overkilltest2

RUN_ARM_NATIVE_BOOT.cmd starts the visual overkill mode: audio is never initialized,
particle/trail/cosmetic ARM functions are hot-patched out, and original PNG
assets are replaced before decoding by a 70-byte 1x1 white image. Enter a heavy level, then use:

  F3  toggle nativeRender itself (zero-render baseline)
  F4  toggle ARM sprite/label/shape draw methods
  F5  toggle particles/cosmetics
  F6  toggle actual OpenGL draw calls
  F7  toggle CCNode scene traversal (strongest logic-only test)
  F8  toggle the entire host OpenGL backend
  F9  toggle future texture uploads
  F10 print the current state to gd-arm-wrapper.log
  F11 profile hottest ARM blocks for five seconds

The other RUN_OVERKILL_*.cmd launchers start directly in stronger blank-screen
modes. Compare their ARM frame profile and ARM overkill-test profile lines.
'@

$BuildInfo = @"
Geometry Dash ARM Wrapper 0.9.4-arm-overkilltest2
Built: $([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss K'))
Zig: $ZigVersion
CMake: $CMakeVersion
Ninja: $NinjaVersion
Unicorn: 2.1.4, ARM-only static archive
Host build script: build-windows.ps1
"@
Write-AsciiFile -Path (Join-Path $Output "BUILD-INFO.txt") -Content $BuildInfo

Write-Host "`nSUCCESS" -ForegroundColor Green
Write-Host "Built wrapper: $ExePath"
Write-Host "Runtime folder: $Output"
Write-Host "The downloaded portable compilers remain only in $ToolsRoot and can be deleted after building."
