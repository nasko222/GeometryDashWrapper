param(
    [switch]$Clean,
    [switch]$RefreshTools,
    [switch]$RefreshDynarmic
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ToolsRoot = Join-Path $Root ".build-tools"
$Downloads = Join-Path $ToolsRoot "downloads"
$BuildRoot = Join-Path $Root "build-cache-windows"
$BuildDir = Join-Path $BuildRoot "dynarmic-x64-unified"
$Output = Join-Path $Root "dist-unified"
$DynarmicVersion = "6.7.0"
$DynarmicRevision = "a41c380246d3d9f9874f0f792d234dc0cc17c180"
$DynarmicRevisionShort = $DynarmicRevision.Substring(0, 12)
$DynarmicSource = Join-Path $ToolsRoot "dynarmic-gitlab-$DynarmicRevisionShort-src"
$LegacyDynarmicSource = Join-Path $ToolsRoot "dynarmic-$DynarmicVersion-src"
$DynarmicRepo = "https://gitlab.com/suyu-emu/dynarmic.git"

$ZigVersion = "0.14.1"
$CMakeVersion = "3.31.10"
$NinjaVersion = "1.13.2"
$BoostVersion = "1.84.0"
$BoostArchiveName = "boost_1_84_0.zip"
$BoostArchiveSha256 = "CC77EB8ED25DA4D596B25E77E4DBB6C5AFAAC9CDDD00DC9CA947B6B268CC76A4"
$BoostArchive = Join-Path $Downloads $BoostArchiveName
$BoostDirectory = Join-Path $ToolsRoot "boost-$BoostVersion"
$BoostSource = Join-Path $BoostDirectory "boost_1_84_0"
$CMakeSha256 = "13D1A463D7130DF5339BAEDD63D8AE990AAF385062B2F42F372796143AE94086"
$NinjaSha256 = "07FC8261B42B20E71D1720B39068C2E14FFCEE6396B76FB7A795FB460B78DC65"
$BuilderRevision = "dynarmic-x64-builder111-0.9.7-newera3-fix1"
$CompatibleBuilderRevisions = @($BuilderRevision)

function Invoke-External {
    param([string]$FilePath, [string[]]$Arguments)
    Write-Host "`n> $FilePath $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath"
    }
}


function Find-GitExecutable {
    $Command = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }

    $Candidates = @(
        (Join-Path $env:ProgramFiles "Git\cmd\git.exe"),
        (Join-Path $env:ProgramFiles "Git\bin\git.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Git\cmd\git.exe")
    )
    if (${env:ProgramFiles(x86)}) {
        $Candidates += Join-Path ${env:ProgramFiles(x86)} "Git\cmd\git.exe"
    }
    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path -LiteralPath $Candidate)) { return $Candidate }
    }
    throw "Git was not found. This unified Dynarmic build uses Git only for a non-interactive public checkout from GitLab. Install Git for Windows or put git.exe on PATH, then rerun BUILD_DYNARMIC.cmd."
}

function Get-PublicGitArguments {
    param([string[]]$Arguments)
    return @(
        "-c", "credential.helper=",
        "-c", "credential.interactive=never"
    ) + $Arguments
}

function Invoke-PublicGit {
    param([string]$GitExe, [string[]]$Arguments)
    $GitArguments = Get-PublicGitArguments -Arguments $Arguments
    Write-Host "`n> $GitExe $($GitArguments -join ' ')" -ForegroundColor DarkGray
    & $GitExe @GitArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Public Git command failed with exit code $LASTEXITCODE`: $GitExe"
    }
}

function Invoke-PublicGitCapture {
    param([string]$GitExe, [string[]]$Arguments)
    $GitArguments = Get-PublicGitArguments -Arguments $Arguments
    Write-Host "`n> $GitExe $($GitArguments -join ' ')" -ForegroundColor DarkGray
    $Output = @(& $GitExe @GitArguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        $Text = $Output -join "`n"
        throw "Public Git command failed with exit code $LASTEXITCODE`n$Text"
    }
    return $Output
}

function Get-CheckedFile {
    param([string]$Uri, [string]$Destination, [string]$Sha256)
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


function Expand-BoostHeaders {
    param([string]$Archive, [string]$Destination)

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Destination
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null

    $ArchiveHandle = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $Prefix = "boost_1_84_0/"
        foreach ($Entry in $ArchiveHandle.Entries) {
            if (-not $Entry.FullName.StartsWith($Prefix, [StringComparison]::Ordinal)) { continue }
            $Relative = $Entry.FullName.Substring($Prefix.Length)
            if ($Relative -ne "LICENSE_1_0.txt" -and
                -not $Relative.StartsWith("boost/", [StringComparison]::Ordinal)) {
                continue
            }

            $Target = Join-Path $Destination ($Relative.Replace('/', '\'))
            if ([string]::IsNullOrEmpty($Entry.Name)) {
                New-Item -ItemType Directory -Force -Path $Target | Out-Null
                continue
            }

            $Parent = Split-Path -Parent $Target
            if ($Parent) { New-Item -ItemType Directory -Force -Path $Parent | Out-Null }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($Entry, $Target, $true)
        }
    } finally {
        $ArchiveHandle.Dispose()
    }
}

function Find-SingleFile {
    param([string]$Directory, [string]$Name)
    $Matches = @(Get-ChildItem -LiteralPath $Directory -Filter $Name -File -Recurse)
    if ($Matches.Count -lt 1) { throw "Could not find $Name below $Directory" }
    return $Matches[0].FullName
}

function Patch-DynarmicFmtForZigLibCpp {
    param([string]$DynarmicRoot)

    $FmtOstream = Join-Path $DynarmicRoot "externals\fmt\include\fmt\ostream.h"
    if (-not (Test-Path -LiteralPath $FmtOstream)) {
        throw "Dynarmic fmt header was not found: $FmtOstream"
    }

    $Marker = "FMT_GD_LIBCPP_STD_STREAM_AVAILABLE"
    $Text = [IO.File]::ReadAllText($FmtOstream)
    if ($Text.Contains($Marker)) {
        Write-Host "Using cached Zig/libc++ fmt compatibility patch"
        return
    }

    $FirstWindowsBranch = "#if defined(_WIN32) && defined(__GLIBCXX__)"
    $FirstIndex = $Text.IndexOf($FirstWindowsBranch, [StringComparison]::Ordinal)
    if ($FirstIndex -lt 0) {
        throw "Could not locate the expected fmt Windows stream branch in $FmtOstream"
    }

    $FeatureProbe = @'
#if defined(_WIN32) && defined(_LIBCPP_VERSION) && __has_include(<__std_stream>)
#  define FMT_GD_LIBCPP_STD_STREAM_AVAILABLE 1
#else
#  define FMT_GD_LIBCPP_STD_STREAM_AVAILABLE 0
#endif

'@
    $Text = $Text.Insert($FirstIndex, $FeatureProbe)

    $OldBranch = "#elif defined(_WIN32) && defined(_LIBCPP_VERSION)"
    $OccurrenceCount = ([regex]::Matches($Text, [regex]::Escape($OldBranch))).Count
    if ($OccurrenceCount -ne 3) {
        throw "Unexpected fmt ostream.h layout: expected 3 libc++ private-stream branches, found $OccurrenceCount"
    }
    $Text = $Text.Replace($OldBranch, "#elif FMT_GD_LIBCPP_STD_STREAM_AVAILABLE")

    [IO.File]::WriteAllText($FmtOstream, $Text, [Text.UTF8Encoding]::new($false))
    Write-Host "Applied Zig/libc++ compatibility patch to vendored fmt 10.1"
}

if (-not [Environment]::Is64BitOperatingSystem) {
    throw "Dynarmic requires a 64-bit Windows host."
}

New-Item -ItemType Directory -Force -Path $ToolsRoot, $Downloads, $BuildRoot | Out-Null
if ($RefreshTools) {
    $ToolDirectories = @(
        (Join-Path $ToolsRoot "zig-$ZigVersion"),
        (Join-Path $ToolsRoot "cmake-$CMakeVersion"),
        (Join-Path $ToolsRoot "ninja-$NinjaVersion"),
        $BoostDirectory
    )
    $ToolArchives = @(
        (Join-Path $Downloads "zig-x86_64-windows-$ZigVersion.zip"),
        (Join-Path $Downloads "cmake-$CMakeVersion-windows-x86_64.zip"),
        (Join-Path $Downloads "ninja-win-$NinjaVersion.zip"),
        $BoostArchive
    )
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue -LiteralPath $ToolDirectories
    Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $ToolArchives
}
if ($Clean) {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue -LiteralPath $BuildDir
    $ArmOutputPaths = @(
        (Join-Path $Output "arm-legacy"),
        (Join-Path $Output "armv7")
    )
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue -LiteralPath $ArmOutputPaths
}
if ($RefreshDynarmic) {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $DynarmicSource
}

# A failed clone from the retired GitHub URL can leave this old directory behind.
# Remove it only when it is incomplete; valid old checkouts are left untouched.
if ((Test-Path $LegacyDynarmicSource) -and
    -not (Test-Path (Join-Path $LegacyDynarmicSource "src\dynarmic\interface\A32\a32.h"))) {
    Write-Host "Removing incomplete legacy Dynarmic checkout: $LegacyDynarmicSource"
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $LegacyDynarmicSource
}

Write-Host "Reading official Zig $ZigVersion metadata"
$ZigIndex = Invoke-RestMethod -Uri "https://ziglang.org/download/index.json"
$ZigRelease = $ZigIndex.PSObject.Properties[$ZigVersion].Value
$ZigInfo = $ZigRelease.PSObject.Properties["x86_64-windows"].Value
if (-not $ZigInfo) { throw "Zig $ZigVersion x86_64-windows metadata was not found" }
$ZigArchive = Join-Path $Downloads "zig-x86_64-windows-$ZigVersion.zip"
$ZigDirectory = Join-Path $ToolsRoot "zig-$ZigVersion"
Get-CheckedFile -Uri ([string]$ZigInfo.tarball) -Destination $ZigArchive -Sha256 ([string]$ZigInfo.shasum)
if (-not (Test-Path $ZigDirectory)) {
    New-Item -ItemType Directory -Force -Path $ZigDirectory | Out-Null
    Expand-Archive -LiteralPath $ZigArchive -DestinationPath $ZigDirectory -Force
}
$ZigExe = Find-SingleFile -Directory $ZigDirectory -Name "zig.exe"

$CMakeArchive = Join-Path $Downloads "cmake-$CMakeVersion-windows-x86_64.zip"
$CMakeDirectory = Join-Path $ToolsRoot "cmake-$CMakeVersion"
$CMakeExe = Join-Path $CMakeDirectory "cmake-$CMakeVersion-windows-x86_64\bin\cmake.exe"
Get-CheckedFile -Uri "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-x86_64.zip" -Destination $CMakeArchive -Sha256 $CMakeSha256
if (-not (Test-Path $CMakeExe)) {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $CMakeDirectory
    New-Item -ItemType Directory -Force -Path $CMakeDirectory | Out-Null
    Expand-Archive -LiteralPath $CMakeArchive -DestinationPath $CMakeDirectory -Force
}

$NinjaArchive = Join-Path $Downloads "ninja-win-$NinjaVersion.zip"
$NinjaDirectory = Join-Path $ToolsRoot "ninja-$NinjaVersion"
$NinjaExe = Join-Path $NinjaDirectory "ninja.exe"
Get-CheckedFile -Uri "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/ninja-win.zip" -Destination $NinjaArchive -Sha256 $NinjaSha256
if (-not (Test-Path $NinjaExe)) {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $NinjaDirectory
    New-Item -ItemType Directory -Force -Path $NinjaDirectory | Out-Null
    Expand-Archive -LiteralPath $NinjaArchive -DestinationPath $NinjaDirectory -Force
}


Write-Host "Preparing pinned Boost $BoostVersion headers"
Get-CheckedFile -Uri "https://archives.boost.io/release/$BoostVersion/source/$BoostArchiveName" -Destination $BoostArchive -Sha256 $BoostArchiveSha256
$BoostVersionHeader = Join-Path $BoostSource "boost\version.hpp"
if (-not (Test-Path $BoostVersionHeader)) {
    Write-Host "Extracting Boost header tree"
    Expand-BoostHeaders -Archive $BoostArchive -Destination $BoostSource
} else {
    Write-Host "Using cached Boost headers: $BoostSource"
}
if (-not (Test-Path $BoostVersionHeader)) {
    throw "Boost header extraction failed; missing: $BoostVersionHeader"
}

$GitExe = Find-GitExecutable

# This is a public GitLab checkout. Disable terminal prompts and Git Credential
# Manager interaction for this process so a missing public mirror fails clearly
# instead of asking for access to the user's GitHub account.
$env:GIT_TERMINAL_PROMPT = "0"
$env:GCM_INTERACTIVE = "Never"

$CheckoutReady = $false
if (Test-Path (Join-Path $DynarmicSource ".git")) {
    try {
        $CachedRemoteLines = @(Invoke-PublicGitCapture -GitExe $GitExe -Arguments @(
            "-C", $DynarmicSource, "remote", "get-url", "origin"
        ))
        $CachedCommitLines = @(Invoke-PublicGitCapture -GitExe $GitExe -Arguments @(
            "-C", $DynarmicSource, "rev-parse", "HEAD"
        ))
        $CachedRemote = $CachedRemoteLines[0].Trim()
        $CachedCommit = $CachedCommitLines[0].Trim()
        $CheckoutReady = ($CachedRemote -eq $DynarmicRepo -and
                          $CachedCommit -eq $DynarmicRevision -and
                          (Test-Path (Join-Path $DynarmicSource "src\dynarmic\interface\A32\a32.h")))
    } catch {
        $CheckoutReady = $false
    }

    if (-not $CheckoutReady) {
        Write-Host "Discarding stale or incomplete Dynarmic checkout: $DynarmicSource"
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $DynarmicSource
    }
}

if (-not $CheckoutReady) {
    Write-Host "Cloning public Dynarmic mirror from GitLab"
    Write-Host "Pinned revision: $DynarmicRevision"
    New-Item -ItemType Directory -Force -Path $DynarmicSource | Out-Null
    try {
        Invoke-PublicGit -GitExe $GitExe -Arguments @(
            "-C", $DynarmicSource, "init"
        )
        Invoke-PublicGit -GitExe $GitExe -Arguments @(
            "-C", $DynarmicSource, "remote", "add", "origin", $DynarmicRepo
        )
        Invoke-PublicGit -GitExe $GitExe -Arguments @(
            "-C", $DynarmicSource, "fetch", "--depth", "1", "origin", $DynarmicRevision
        )
        Invoke-PublicGit -GitExe $GitExe -Arguments @(
            "-C", $DynarmicSource, "checkout", "--detach", "FETCH_HEAD"
        )
    } catch {
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $DynarmicSource
        throw "The public Dynarmic GitLab checkout failed without requesting credentials. Check access to gitlab.com and rerun BUILD_DYNARMIC.cmd.`n$($_.Exception.Message)"
    }
} else {
    Write-Host "Using cached public Dynarmic checkout: $DynarmicSource"
}

$RequiredDynarmicPaths = @(
    "CMakeLists.txt",
    "LICENSE.txt",
    "src\dynarmic\interface\A32\a32.h",
    "externals\fmt",
    "externals\xbyak",
    "externals\zydis"
)
foreach ($RelativePath in $RequiredDynarmicPaths) {
    if (-not (Test-Path (Join-Path $DynarmicSource $RelativePath))) {
        throw "Dynarmic GitLab checkout is incomplete; missing: $RelativePath"
    }
}

$DynarmicCommitLines = @(Invoke-PublicGitCapture -GitExe $GitExe -Arguments @(
    "-C", $DynarmicSource, "rev-parse", "HEAD"
))
$DynarmicCommit = $DynarmicCommitLines[0].Trim()
if ($DynarmicCommit -ne $DynarmicRevision) {
    throw "Dynarmic revision mismatch.`nExpected: $DynarmicRevision`nActual:   $DynarmicCommit"
}
Write-Host "Dynarmic source commit: $DynarmicCommit"

# fmt 10.1 uses libc++'s old private <__std_stream> header for an optional
# Windows console fast path. Zig 0.14.1 ships a newer libc++ where that private
# header was moved out of the public include tree. Disable only that optional
# path when the header is unavailable; normal ostream formatting remains intact.
Patch-DynarmicFmtForZigLibCpp -DynarmicRoot $DynarmicSource

$env:ZIG = $ZigExe
$env:GD_ARM_ZIGAR = (Join-Path $Root "tools\zigar-x64.cmd").Replace('\', '/')
$env:GD_ARM_ZIGRANLIB = (Join-Path $Root "tools\zigranlib-x64.cmd").Replace('\', '/')
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $BuildRoot "zig-global-x64"
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $BuildRoot "zig-local-x64"
$env:PATH = "$NinjaDirectory;$env:PATH"

$Stamp = Join-Path $BuildDir ".gd-builder-revision"
if (Test-Path $BuildDir) {
    $Old = if (Test-Path $Stamp) { (Get-Content -LiteralPath $Stamp -Raw).Trim() } else { "" }
    if ($Old -notin $CompatibleBuilderRevisions) {
        Remove-Item -Recurse -Force $BuildDir
    }
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$Toolchain = Join-Path $Root "tools\dynarmic-x64-zig.cmake"
$ProjectSource = Join-Path $Root "cmake"
Invoke-External -FilePath $CMakeExe -Arguments @(
    "-S", $ProjectSource,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=$NinjaExe",
    "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$Toolchain",
    "-DDYNARMIC_SOURCE_DIR:PATH=$DynarmicSource",
    "-DBOOST_ROOT:PATH=$BoostSource",
    "-DBoost_ROOT:PATH=$BoostSource",
    "-DBoost_INCLUDE_DIR:PATH=$BoostSource",
    "-DBoost_NO_SYSTEM_PATHS=ON",
    "-DCMAKE_POLICY_DEFAULT_CMP0167=OLD",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DBUILD_TESTING=OFF",
    "-DDYNARMIC_TESTS=OFF",
    "-DDYNARMIC_WARNINGS_AS_ERRORS=OFF",
    "-DDYNARMIC_IGNORE_ASSERTS=ON",
    "-DDYNARMIC_FRONTENDS=A32",
    "-DDYNARMIC_USE_LLVM=OFF",
    "-DDYNARMIC_USE_PRECOMPILED_HEADERS=OFF",
    "-DCMAKE_BUILD_TYPE=Release"
)
[IO.File]::WriteAllText($Stamp, $BuilderRevision, [Text.Encoding]::ASCII)

# ZIP extraction can preserve source timestamps older than an existing Ninja object.
# In that case Ninja may incorrectly reuse a stale probe executable even though the
# source contents changed. Refresh only the two wrapper backend and shared sources before building;
# Dynarmic's 173-object static library remains cached.
Write-Host "Refreshing unified backend source timestamps to prevent stale Ninja output"
$ProbeSourcesToRefresh = @(
    (Join-Path $Root "src\backends\arm_legacy\dynarmic_legacy.cpp"),
    (Join-Path $Root "src\backends\armv7\dynarmic_armv7.cpp"),
    (Join-Path $Root "src\shared\storage_win.c"),
    (Join-Path $Root "src\shared\audio_win.c"),
    (Join-Path $Root "src\shared\apk_extract_audio.c"),
    (Join-Path $Root "src\shared\embedded_effects_stub.c"),
    (Join-Path $Root "src\shared\net_compat_win.c"),
    (Join-Path $Root "src\shared\build_info.h"),
    (Join-Path $Root "third_party\stb\stb_vorbis.c"),
    (Join-Path $Root "cmake\CMakeLists.txt")
)
$RefreshTime = Get-Date
foreach ($ProbeSource in $ProbeSourcesToRefresh) {
    if (Test-Path -LiteralPath $ProbeSource) {
        [IO.File]::SetLastWriteTime($ProbeSource, $RefreshTime)
    }
}

Invoke-External -FilePath $CMakeExe -Arguments @("--build", $BuildDir, "--parallel")


$LegacyMatches = @(Get-ChildItem -LiteralPath $BuildDir -Filter "GeometryDashArmLegacy.exe" -File -Recurse)
$ArmV7Matches = @(Get-ChildItem -LiteralPath $BuildDir -Filter "GeometryDashArmV7.exe" -File -Recurse)
if ($LegacyMatches.Count -lt 1) { throw "Build completed but GeometryDashArmLegacy.exe was not found" }
if ($ArmV7Matches.Count -lt 1) { throw "Build completed but GeometryDashArmV7.exe was not found" }
$LegacyExe = $LegacyMatches[0].FullName
$ArmV7Exe = $ArmV7Matches[0].FullName

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$LegacyOut = Join-Path $Output "arm-legacy"
$ArmV7Out = Join-Path $Output "armv7"
$ArmOutputPaths = @($LegacyOut, $ArmV7Out)
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue -LiteralPath $ArmOutputPaths
New-Item -ItemType Directory -Force -Path $ArmOutputPaths | Out-Null
Copy-Item -Force $LegacyExe (Join-Path $LegacyOut "GeometryDashArmLegacy.exe")
Copy-Item -Force $ArmV7Exe (Join-Path $ArmV7Out "GeometryDashArmV7.exe")
New-Item -ItemType Directory -Force -Path (Join-Path $Output "save") | Out-Null

$LegacyRun = @'
@echo off
cd /d "%~dp0.."
set "I_LOST_THE_GAME=true"
set "EDITOR_CONTROLLS=true"
set "RESOLUTION=1140x640"
set "TEXTURE_FILTERING=LINEAR"
set "OLD_VER_PLAYTEST=false"
if not exist game.apk (
  echo Put the ARM-only Geometry Dash 1.0-1.4 APK in dist-unified as game.apk
  pause
  exit /b 2
)
if not exist save mkdir save
for %%F in (gd-wrapper*.log gd-arm*.log gd-dynarmic*.log gd-networktest*.log gd-v22beta*.log gd-arm*-imports*.txt gd-v22beta-imports*.txt gd-arm*-profile*.csv gd-dynarmic-profile*.csv gd-networktest*-profile*.csv gd-arm*-profile-summary*.txt gd-dynarmic-profile-summary*.txt gd-networktest*-profile-summary*.txt gd-run-info.txt) do del /q "%%F" >nul 2>nul
"%~dp0GeometryDashArmLegacy.exe" game.apk --log=gd-arm-legacy.log
'@
$LegacyDebug = @'
@echo off
cd /d "%~dp0.."
set "I_LOST_THE_GAME=true"
set "EDITOR_CONTROLLS=true"
set "RESOLUTION=1140x640"
set "TEXTURE_FILTERING=LINEAR"
set "OLD_VER_PLAYTEST=false"
if not exist game.apk (
  echo Put the ARM-only Geometry Dash 1.0-1.4 APK in dist-unified as game.apk
  pause
  exit /b 2
)
if not exist save mkdir save
for %%F in (gd-wrapper*.log gd-arm*.log gd-dynarmic*.log gd-networktest*.log gd-v22beta*.log gd-arm*-imports*.txt gd-v22beta-imports*.txt gd-arm*-profile*.csv gd-dynarmic-profile*.csv gd-networktest*-profile*.csv gd-arm*-profile-summary*.txt gd-dynarmic-profile-summary*.txt gd-networktest*-profile-summary*.txt gd-run-info.txt) do del /q "%%F" >nul 2>nul
"%~dp0GeometryDashArmLegacy.exe" game.apk --debug-everything --dump-imports=gd-arm-legacy-imports.txt --log=gd-arm-legacy-debug.log --profile=gd-arm-legacy-profile.csv --profile-summary=gd-arm-legacy-profile-summary.txt
'@
$ArmV7Run = @'
@echo off
cd /d "%~dp0.."
set "I_LOST_THE_GAME=true"
set "EDITOR_CONTROLLS=true"
set "RESOLUTION=1140x640"
set "TEXTURE_FILTERING=LINEAR"
set "OLD_VER_PLAYTEST=false"
if not exist game.apk (
  echo Put the ARMv7 Geometry Dash 2.2 APK in dist-unified as game.apk
  pause
  exit /b 2
)
if not exist save mkdir save
for %%F in (gd-wrapper*.log gd-arm*.log gd-dynarmic*.log gd-networktest*.log gd-v22beta*.log gd-arm*-imports*.txt gd-v22beta-imports*.txt gd-arm*-profile*.csv gd-dynarmic-profile*.csv gd-networktest*-profile*.csv gd-arm*-profile-summary*.txt gd-dynarmic-profile-summary*.txt gd-networktest*-profile-summary*.txt gd-run-info.txt) do del /q "%%F" >nul 2>nul
"%~dp0GeometryDashArmV7.exe" game.apk --companion-hooks=off --log=gd-armv7.log
'@
$ArmV7Debug = @'
@echo off
cd /d "%~dp0.."
set "I_LOST_THE_GAME=true"
set "EDITOR_CONTROLLS=true"
set "RESOLUTION=1140x640"
set "TEXTURE_FILTERING=LINEAR"
set "OLD_VER_PLAYTEST=false"
if not exist game.apk (
  echo Put the ARMv7 Geometry Dash 2.2 APK in dist-unified as game.apk
  pause
  exit /b 2
)
if not exist save mkdir save
for %%F in (gd-wrapper*.log gd-arm*.log gd-dynarmic*.log gd-networktest*.log gd-v22beta*.log gd-arm*-imports*.txt gd-v22beta-imports*.txt gd-arm*-profile*.csv gd-dynarmic-profile*.csv gd-networktest*-profile*.csv gd-arm*-profile-summary*.txt gd-dynarmic-profile-summary*.txt gd-networktest*-profile-summary*.txt gd-run-info.txt) do del /q "%%F" >nul 2>nul
"%~dp0GeometryDashArmV7.exe" game.apk --companion-hooks=off --debug-everything --dump-imports=gd-armv7-imports.txt --log=gd-armv7-debug.log --profile=gd-armv7-profile.csv --profile-summary=gd-armv7-profile-summary.txt
'@
[IO.File]::WriteAllText((Join-Path $LegacyOut "RUN.cmd"), $LegacyRun, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $LegacyOut "RUN_DEBUG.cmd"), $LegacyDebug, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $ArmV7Out "RUN.cmd"), $ArmV7Run, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $ArmV7Out "RUN_DEBUG.cmd"), $ArmV7Debug, [Text.Encoding]::ASCII)

$License = Join-Path $DynarmicSource "LICENSE.txt"
if (Test-Path $License) {
    Copy-Item -Force $License (Join-Path $LegacyOut "DYNARMIC-LICENSE.txt")
    Copy-Item -Force $License (Join-Path $ArmV7Out "DYNARMIC-LICENSE.txt")
}
$BoostLicense = Join-Path $BoostSource "LICENSE_1_0.txt"
if (Test-Path $BoostLicense) {
    Copy-Item -Force $BoostLicense (Join-Path $LegacyOut "BOOST-LICENSE.txt")
    Copy-Item -Force $BoostLicense (Join-Path $ArmV7Out "BOOST-LICENSE.txt")
}
[IO.File]::WriteAllText((Join-Path $Output "DYNARMIC-VERSION.txt"), "api=$DynarmicVersion`r`ncommit=$DynarmicCommit`r`nsource=$DynarmicRepo`r`n", [Text.Encoding]::ASCII)
Write-Host "`nUnified Dynarmic backends ready:" -ForegroundColor Green
Write-Host "  $LegacyOut"
Write-Host "  $ArmV7Out"
