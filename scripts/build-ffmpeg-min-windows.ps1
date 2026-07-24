# Thin Windows entrypoint for the unified FFmpeg min builder.
#
# Responsibility:
#   - Verify MSVC tools are on PATH (caller should run msvc-dev-cmd / VsDevCmd first)
#   - Locate bash from PATH (caller should put MSYS2 usr\bin on PATH)
#   - Invoke scripts/build-ffmpeg-min.sh
#
# This script intentionally does NOT hardcode C:\msys64 or re-bootstrap VS.
# CI owns the environment; this file is only a bridge.
param(
    [string]$FfVer = $(if ($env:FFVER) { $env:FFVER } else { "7.1.1" })
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Builder = Join-Path $Root "scripts\build-ffmpeg-min.sh"

if (-not (Test-Path $Builder)) {
    throw "Missing $Builder"
}

function Test-Cmd([string]$Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

if (-not (Test-Cmd "cl") -and -not (Test-Cmd "cl.exe")) {
    throw "cl.exe not on PATH. Run from an MSVC x64 developer environment (e.g. ilammy/msvc-dev-cmd)."
}

$bash = Get-Command bash -ErrorAction SilentlyContinue
if (-not $bash) {
    throw "bash not on PATH. On CI, use msys2/setup-msys2 and ensure MSYS2 usr/bin is prepended to PATH before this script."
}

$env:FFVER = $FfVer
Write-Host "Using bash: $($bash.Source)"
Write-Host "Using cl: $((Get-Command cl -ErrorAction SilentlyContinue).Source)"

# Run the unified builder under bash. MSVC tools remain on PATH for configure --toolchain=msvc.
& $bash.Source -lc "set -e; cd `"$($Root -replace '\\','/')`"; ./scripts/build-ffmpeg-min.sh"
if ($LASTEXITCODE -ne 0) {
    throw "build-ffmpeg-min.sh failed with exit code $LASTEXITCODE"
}

$hdr = Join-Path $Root "third_party\ffmpeg-min\include\libavformat\avformat.h"
if (-not (Test-Path $hdr)) {
    throw "FFmpeg min install incomplete: missing $hdr"
}

Write-Host "FFmpeg min ready under third_party/ffmpeg-min"
Get-ChildItem (Join-Path $Root "third_party\ffmpeg-min\lib") | Format-Table Name, Length
