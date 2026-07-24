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

$MsvcCl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $MsvcCl) {
    $MsvcCl = Get-Command cl -ErrorAction SilentlyContinue
}
if (-not $MsvcCl) {
    throw "cl.exe not on PATH. Run from an MSVC x64 developer environment (e.g. ilammy/msvc-dev-cmd)."
}
$MsvcBin = Split-Path -Parent $MsvcCl.Source

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
Write-Host "Using MSVC cl: $($MsvcCl.Source)"

# Keep the MSVC bin directory ahead of MSYS2 so configure --toolchain=msvc
# resolves cl/link/lib from Visual Studio, while make and POSIX helpers come
# from the one authoritative MSYS2 installation. Invoke the builder file
# directly rather than passing a nested `bash -c` program through PowerShell.
$PreviousPath = $env:Path
$BuilderExitCode = $null
try {
    $env:Path = "$MsvcBin;$Msys2Root\usr\bin;$PreviousPath"
    Push-Location $Root.Path
    try {
        & $Msys2Bash --noprofile --norc -- "scripts/build-ffmpeg-min.sh"
        $BuilderExitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:Path = $PreviousPath
}

if ($BuilderExitCode -ne 0) {
    throw "build-ffmpeg-min.sh failed with exit code $BuilderExitCode"
}

$hdr = Join-Path $Root "third_party\ffmpeg-min\include\libavformat\avformat.h"
if (-not (Test-Path $hdr)) {
    throw "FFmpeg min install incomplete: missing $hdr"
}

Write-Host "FFmpeg min ready under third_party/ffmpeg-min"
Get-ChildItem (Join-Path $Root "third_party\ffmpeg-min\lib") | Format-Table Name, Length
