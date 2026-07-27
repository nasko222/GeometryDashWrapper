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

$Python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $Python) { $Python = Get-Command python -ErrorAction SilentlyContinue }
if (-not $Python) { throw "Python 3 was not found on PATH." }

$Arguments = @(
    (Join-Path $Root "build_x86.py"),
    "--zig", $ZigExe,
    "--out", $Output
)
if (-not [string]::IsNullOrWhiteSpace($Apk)) {
    $ResolvedApk = (Resolve-Path -LiteralPath $Apk).Path
    $Arguments += @("--apk", $ResolvedApk)
}

Write-Host "`n> $($Python.Source) $($Arguments -join ' ')" -ForegroundColor DarkGray
& $Python.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "x86 build failed with exit code $LASTEXITCODE" }
Write-Host "`nx86 backend ready: $Output" -ForegroundColor Green
