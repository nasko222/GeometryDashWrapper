param(
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
$Output = Join-Path $Root "dist-unified"

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

function Invoke-External {
    param([string]$FilePath, [string[]]$Arguments)
    Write-Host "`n> $FilePath $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath"
    }
}

New-Item -ItemType Directory -Force -Path $ToolsRoot, $Downloads, $Output | Out-Null
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

$ZlibSources = @(
    (Join-Path $Root "third_party\zlib\adler32.c"),
    (Join-Path $Root "third_party\zlib\crc32.c"),
    (Join-Path $Root "third_party\zlib\inflate.c"),
    (Join-Path $Root "third_party\zlib\inftrees.c"),
    (Join-Path $Root "third_party\zlib\inffast.c"),
    (Join-Path $Root "third_party\zlib\zutil.c")
)
$Sources = @((Join-Path $Root "src\launcher\native_launcher.c")) + $ZlibSources
$Cache = Join-Path $Root "build-cache-launcher"
New-Item -ItemType Directory -Force -Path $Cache | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $Cache "global"
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $Cache "local"

$Arguments = @(
    "cc",
    "-target", "x86_64-windows-gnu",
    "-std=c11",
    "-O2",
    "-Wall",
    "-Wextra",
    "-D_CRT_SECURE_NO_WARNINGS",
    "-municode",
    "-I$Root\third_party\zlib",
    "-I$Root\src\shared",
    "-o", (Join-Path $Output "GeometryDashLauncher.exe")
) + $Sources + @(
    "-lkernel32",
    "-luser32"
)

Invoke-External -FilePath $ZigExe -Arguments $Arguments
Write-Host "`nNative launcher ready: $(Join-Path $Output 'GeometryDashLauncher.exe')" -ForegroundColor Green
