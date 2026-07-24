# Build a minimal static FFmpeg for MSVC (TS H264/AAC -> MP4 copy-remux).
# Requires: Visual Studio Build Tools (cl/link), MSYS2 (make, bash), nasm (optional), curl.
# Output: third_party/ffmpeg-min/{include,lib}
param(
    [string]$FfVer = $(if ($env:FFVER) { $env:FFVER } else { "7.1.1" }),
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Third = Join-Path $Root "third_party"
$SrcDir = Join-Path $Third "ffmpeg-$FfVer"
$Tarball = Join-Path $Third "ffmpeg-$FfVer.tar.xz"
$Prefix = Join-Path $Third "ffmpeg-min"
$Build = Join-Path $Third "ffmpeg-min-build"
$Url = "https://ffmpeg.org/releases/ffmpeg-$FfVer.tar.xz"

New-Item -ItemType Directory -Force -Path $Third | Out-Null

function Find-VsDevCmd {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) { return $null }
    $devCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (Test-Path $devCmd) { return $devCmd }
    return $null
}

$vsDevCmd = Find-VsDevCmd
if (-not $vsDevCmd) {
    throw "Visual Studio C++ toolchain not found (need VsDevCmd.bat)."
}

# Prefer MSYS2 bash if available; otherwise Git bash.
$bash = $null
foreach ($candidate in @(
        "C:\msys64\usr\bin\bash.exe",
        "C:\Program Files\Git\bin\bash.exe"
    )) {
    if (Test-Path $candidate) { $bash = $candidate; break }
}
if (-not $bash) {
    throw "bash not found. Install MSYS2 (recommended) or Git for Windows."
}

if (-not (Test-Path $SrcDir)) {
    if (-not (Test-Path $Tarball)) {
        Write-Host "Downloading $Url ..."
        Invoke-WebRequest -Uri $Url -OutFile $Tarball
    }
    Write-Host "Extracting $Tarball ..."
    # tar is available on modern Windows
    tar -C $Third -xf $Tarball
}

if (Test-Path $Build) { Remove-Item $Build -Recurse -Force }
if (Test-Path $Prefix) { Remove-Item $Prefix -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Build | Out-Null
New-Item -ItemType Directory -Force -Path $Prefix | Out-Null

$prefixUnix = ($Prefix -replace '\\', '/')
$srcUnix = ($SrcDir -replace '\\', '/')
$buildUnix = ($Build -replace '\\', '/')

# Convert Windows path to MSYS path if using MSYS2 bash
function To-BashPath([string]$winPath) {
    $p = $winPath -replace '\\', '/'
    if ($bash -match 'msys64') {
        if ($p -match '^([A-Za-z]):/(.*)$') {
            return "/$($Matches[1].ToLower())/$($Matches[2])"
        }
    }
    return $p
}

$srcBash = To-BashPath $srcUnix
$buildBash = To-BashPath $buildUnix
$prefixBash = To-BashPath $prefixUnix

$jobs = [Environment]::ProcessorCount
$configureScript = @"
set -euo pipefail
cd '$buildBash'
export PATH="/usr/bin:/bin:`$PATH"
# Ensure nasm/yasm if present
command -v nasm >/dev/null 2>&1 || true

'$srcBash/configure' \
  --prefix='$prefixBash' \
  --toolchain=msvc \
  --enable-gpl --enable-version3 \
  --disable-debug --disable-doc --disable-programs \
  --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
  --disable-network \
  --disable-asm \
  --disable-everything \
  --enable-avutil --enable-avcodec --enable-avformat --enable-swresample \
  --enable-demuxer=mpegts \
  --enable-muxer=mp4 --enable-muxer=mov \
  --enable-parser=h264 --enable-parser=aac \
  --enable-decoder=h264 --enable-decoder=aac \
  --enable-bsf=aac_adtstoasc --enable-bsf=extract_extradata \
  --enable-protocol=file \
  --enable-static --disable-shared \
  --extra-cflags='-O2'

make -j$jobs
make install
"@

$scriptPath = Join-Path $env:TEMP "build-ffmpeg-min-msvc.sh"
Set-Content -Path $scriptPath -Value $configureScript -Encoding UTF8
$scriptBash = To-BashPath ($scriptPath -replace '\\', '/')

# Run under VS x64 environment so cl/link are on PATH, then invoke bash.
$cmd = @"
call "$vsDevCmd" -arch=x64 -host_arch=x64 >nul
"$bash" -lc "$scriptBash"
"@
$cmdPath = Join-Path $env:TEMP "build-ffmpeg-min-msvc.cmd"
Set-Content -Path $cmdPath -Value $cmd -Encoding ASCII

Write-Host "Building minimal FFmpeg $FfVer with MSVC toolchain..."
& cmd.exe /c $cmdPath
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg minimal build failed with exit code $LASTEXITCODE"
}

# Patch optional objects if missing (best-effort; may already be present).
$libavcodec = Get-ChildItem -Path (Join-Path $Prefix "lib") -Filter "libavcodec*" -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $libavcodec) {
    throw "libavcodec static library not found under $Prefix/lib"
}

Write-Host "Installed minimal FFmpeg to: $Prefix"
Get-ChildItem (Join-Path $Prefix "lib") | Format-Table Name, Length
