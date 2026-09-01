param(
    [string]$Apk,
    [switch]$Clean,
    [switch]$RefreshTools
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ToolsRoot = Join-Path $Root ".build-tools"
$Downloads = Join-Path $ToolsRoot "downloads"
$ZigVersion = "0.14.1"
$ZigDirectory = Join-Path $ToolsRoot "zig-$ZigVersion"
$ZigArchive = Join-Path $Downloads "zig-x86_64-windows-$ZigVersion.zip"
$Output = Join-Path $Root "dist-unified\x86"

function Get-CheckedFile {
    param([string]$Uri, [string]$Destination, [string]$Sha256)
    $Expected = $Sha256.ToUpperInvariant()
    if (Test-Path -LiteralPath $Destination) {
        $Actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash.ToUpperInvariant()
        if ($Actual -eq $Expected) { return }
        Remove-Item -Force -LiteralPath $Destination
    }
    Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile $Destination
    $Actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash.ToUpperInvariant()
    if ($Actual -ne $Expected) {
        Remove-Item -Force -LiteralPath $Destination
        throw "SHA-256 mismatch for $Uri`nExpected: $Expected`nActual:   $Actual"
    }
}

function Find-SingleFile {
    param([string]$Directory, [string]$Name)
    $Matches = @(Get-ChildItem -LiteralPath $Directory -Filter $Name -File -Recurse)
    if ($Matches.Count -ne 1) {
        throw "Expected one $Name under $Directory, found $($Matches.Count)"
    }
    return $Matches[0].FullName
}

New-Item -ItemType Directory -Force -Path $ToolsRoot, $Downloads | Out-Null
if ($Clean) {
    $CleanPaths = @((Join-Path $Root "build-cache"), $Output)
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue -LiteralPath $CleanPaths
}
if ($RefreshTools) {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue -LiteralPath $ZigDirectory
    Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $ZigArchive
}

Write-Host "Reading official Zig $ZigVersion metadata"
$Index = Invoke-RestMethod -Uri "https://ziglang.org/download/index.json"
$Release = $Index.PSObject.Properties[$ZigVersion].Value
$Info = $Release.PSObject.Properties["x86_64-windows"].Value
if (-not $Info) { throw "Zig $ZigVersion x86_64-windows metadata was not found" }
Get-CheckedFile -Uri ([string]$Info.tarball) -Destination $ZigArchive -Sha256 ([string]$Info.shasum)
if (-not (Test-Path -LiteralPath $ZigDirectory)) {
    New-Item -ItemType Directory -Force -Path $ZigDirectory | Out-Null
    Expand-Archive -LiteralPath $ZigArchive -DestinationPath $ZigDirectory -Force
}
$ZigExe = Find-SingleFile -Directory $ZigDirectory -Name "zig.exe"

function Invoke-External {
    param([string]$FilePath, [string[]]$Arguments)
    Write-Host "`n> $FilePath $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath"
    }
}

$ResolvedApk = $null
if (-not [string]::IsNullOrWhiteSpace($Apk)) {
    $ResolvedApk = (Resolve-Path -LiteralPath $Apk).Path
}

# The previous builder delegated this source list to build_x86.py.  Keeping the
# list directly in PowerShell removes the Python build dependency without
# changing the compiler, target, optimization level, source order, or libraries.
$ZlibSources = @(Get-ChildItem -LiteralPath (Join-Path $Root "third_party\zlib") -Filter "*.c" -File | Sort-Object Name | ForEach-Object { $_.FullName })
$Sources = @(
    (Join-Path $Root "src\backends\x86\main.c"),
    (Join-Path $Root "src\backends\x86\loader.c"),
    (Join-Path $Root "src\backends\x86\runtime.c"),
    (Join-Path $Root "src\backends\x86\bionic_x86.S"),
    (Join-Path $Root "src\backends\x86\jni_shim.c"),
    (Join-Path $Root "src\shared\audio_win.c"),
    (Join-Path $Root "src\backends\x86\fmod_win.c"),
    (Join-Path $Root "src\shared\storage_win.c"),
    (Join-Path $Root "src\shared\net_compat_win.c"),
    (Join-Path $Root "src\shared\runtime_settings.c"),
    (Join-Path $Root "src\shared\frame_pacing_win.c"),
    (Join-Path $Root "src\shared\extras_menu_win.c"),
    (Join-Path $Root "src\shared\song_http_win.c"),
    (Join-Path $Root "src\shared\window_icon_win.c"),
    (Join-Path $Root "src\shared\embedded_effects_stub.c"),
    (Join-Path $Root "third_party\stb\stb_vorbis.c")
) + $ZlibSources

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Cache = Join-Path $Root "build-cache"
New-Item -ItemType Directory -Force -Path $Cache | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $Cache "global"
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $Cache "local"

$Arguments = @(
    "cc",
    "-target", "x86-windows-gnu",
    "-std=c11",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Wno-cast-function-type",
    "-Wno-deprecated-non-prototype",
    "-mstackrealign",
    "-I$Root\third_party\zlib",
    "-I$Root\third_party\stb",
    "-I$Root\src\shared",
    "-I$Root\src\backends\x86",
    "-o", (Join-Path $Output "GeometryDashWrapper.exe")
) + $Sources + @(
    "-lws2_32",
    "-lopengl32",
    "-lgdi32",
    "-luser32",
    "-lshell32",
    "-lwinmm",
    "-lole32",
    "-lwinhttp"
)
Invoke-External -FilePath $ZigExe -Arguments $Arguments

# Remove files from historical builders that no longer belong in the output.
@("GeometryDash18Wrapper.exe", "GeometryDash18Wrapper.pdb", "libcocos2dcpp.so", "libgame.so") |
    ForEach-Object { Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $Output $_) }
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Output "audio")
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $Output "game.apk")
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Output "save")

if ($ResolvedApk) {
    Copy-Item -Force -LiteralPath $ResolvedApk -Destination (Join-Path (Split-Path -Parent $Output) "game.apk")
}

Write-Host "`nx86 backend ready: $Output" -ForegroundColor Green
