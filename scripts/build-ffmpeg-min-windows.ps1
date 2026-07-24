# Thin Windows entrypoint for the unified FFmpeg min builder.
#
# Responsibility:
#   - Verify MSVC tools are on PATH (caller should run msvc-dev-cmd / VsDevCmd first)
#   - Run the caller-provided MSYS2 bash, never an arbitrary bash found on PATH
#   - Invoke scripts/build-ffmpeg-min.sh
#
# This script intentionally does NOT hardcode C:\msys64 or re-bootstrap VS.
# CI owns the environment and supplies the MSYS2 installation root.
param(
    [string]$FfVer = $(if ($env:FFVER) { $env:FFVER } else { "7.1.1" }),
    [string]$Msys2Root = $env:MSYS2_ROOT
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

if ([string]::IsNullOrWhiteSpace($Msys2Root)) {
    throw "MSYS2_ROOT is required. On GitHub Actions, set it from steps.msys2.outputs.msys2-location; locally pass -Msys2Root <MSYS2 root>."
}

$Msys2Bash = Join-Path $Msys2Root "usr\bin\bash.exe"
$Msys2Make = Join-Path $Msys2Root "usr\bin\make.exe"
if (-not (Test-Path $Msys2Bash)) {
    throw "MSYS2 bash not found at $Msys2Bash"
}
if (-not (Test-Path $Msys2Make)) {
    throw "MSYS2 make not found at $Msys2Make"
}

$env:FFVER = $FfVer
Write-Host "Using MSYS2 bash: $Msys2Bash"
Write-Host "Using MSVC cl: $((Get-Command cl -ErrorAction SilentlyContinue).Source)"

# Use the known MSYS2 shell and put its tools first. This keeps configure,
# cygpath, and make in one POSIX runtime instead of mixing Git Bash with MSYS2.
# Pass the Windows path through the environment, then convert it in MSYS2 rather
# than relying on Git Bash-style drive-path parsing or fragile inline escaping.
$env:FFMPEG_MIN_BUILDER_ROOT = $Root.Path
& $Msys2Bash -lc 'set -e; export PATH=/usr/bin:/bin:$PATH; cd "$(cygpath -u "$FFMPEG_MIN_BUILDER_ROOT")"; ./scripts/build-ffmpeg-min.sh'
if ($LASTEXITCODE -ne 0) {
    throw "build-ffmpeg-min.sh failed with exit code $LASTEXITCODE"
}

$hdr = Join-Path $Root "third_party\ffmpeg-min\include\libavformat\avformat.h"
if (-not (Test-Path $hdr)) {
    throw "FFmpeg min install incomplete: missing $hdr"
}

Write-Host "FFmpeg min ready under third_party/ffmpeg-min"
Get-ChildItem (Join-Path $Root "third_party\ffmpeg-min\lib") | Format-Table Name, Length
