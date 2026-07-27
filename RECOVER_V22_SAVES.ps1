param(
    [string]$Destination = "",
    [switch]$ListOnly,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $Root "save-v22beta"
}
$Destination = [IO.Path]::GetFullPath($Destination)
$Names = @(
    "CCGameManager.dat",
    "CCLocalLevels.dat",
    "CCGameManager.dat.bak",
    "CCLocalLevels.dat.bak"
)

$AndroidRoots = @()
foreach ($Drive in Get-PSDrive -PSProvider FileSystem) {
    if (-not $Drive.Root) { continue }
    $Candidate = Join-Path $Drive.Root "data\data"
    if (Test-Path -LiteralPath $Candidate -PathType Container) {
        $AndroidRoots += $Candidate
    }
}

if ($AndroidRoots.Count -eq 0) {
    Write-Host "No misplaced drive-root Android save folders were found."
    Write-Host "Checked: X:\data\data on mounted file-system drives."
    exit 1
}

Write-Host "Destination: $Destination"
Write-Host "Drive-root Android folders:"
$AndroidRoots | ForEach-Object { Write-Host "  $_" }

$Copied = 0
$Found = 0
foreach ($Name in $Names) {
    $Candidates = @()
    foreach ($AndroidRoot in $AndroidRoots) {
        foreach ($Package in Get-ChildItem -LiteralPath $AndroidRoot -Directory -ErrorAction SilentlyContinue) {
            $Path = Join-Path $Package.FullName $Name
            if (Test-Path -LiteralPath $Path -PathType Leaf) {
                $Candidates += Get-Item -LiteralPath $Path
            }
        }
    }
    if ($Candidates.Count -eq 0) { continue }
    $Found += $Candidates.Count
    $Newest = $Candidates | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    Write-Host ""
    Write-Host "$Name candidates:"
    $Candidates | Sort-Object LastWriteTimeUtc -Descending | ForEach-Object {
        Write-Host ("  {0:u}  {1}" -f $_.LastWriteTimeUtc, $_.FullName)
    }
    Write-Host "Newest: $($Newest.FullName)"
    if ($ListOnly) { continue }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $Target = Join-Path $Destination $Name
    if ((Test-Path -LiteralPath $Target) -and -not $Force) {
        Write-Host "Skipped existing local file: $Target"
        continue
    }
    Copy-Item -LiteralPath $Newest.FullName -Destination $Target -Force:$Force
    Write-Host "Copied to: $Target"
    $Copied++
}

Write-Host ""
Write-Host "Found candidate files: $Found"
if ($ListOnly) {
    Write-Host "List-only mode; nothing was copied."
} else {
    Write-Host "Copied files: $Copied"
}
if ($Found -eq 0) { exit 1 }
