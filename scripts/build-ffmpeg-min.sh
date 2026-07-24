#!/usr/bin/env bash
# Build a minimal *static* FFmpeg for in-process TS(H264/AAC) → MP4 remux.
#
# Output layout (consumed by cmake/FindFFmpegMin.cmake):
#   third_party/ffmpeg-min/include/libav{format,codec,util}/...
#   third_party/ffmpeg-min/lib/libav*.a   (Unix)
#   third_party/ffmpeg-min/lib/*.lib      (MSVC, name depends on FFmpeg install)
#
# Supported hosts:
#   - macOS (clang)
#   - Linux (gcc/clang)
#   - Windows via MSYS2 bash + MSVC on PATH (cl/link from VsDevCmd / msvc-dev-cmd)
#
# Usage:
#   ./scripts/build-ffmpeg-min.sh
#   FFVER=7.1.1 ./scripts/build-ffmpeg-min.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFVER="${FFVER:-7.1.1}"
SRC_DIR="$ROOT/third_party/ffmpeg-${FFVER}"
TARBALL="$ROOT/third_party/ffmpeg-${FFVER}.tar.xz"
PREFIX="$ROOT/third_party/ffmpeg-min"
BUILD="$ROOT/third_party/ffmpeg-min-build"
URL="https://ffmpeg.org/releases/ffmpeg-${FFVER}.tar.xz"

if command -v nproc >/dev/null 2>&1; then
  JOBS="${JOBS:-$(nproc)}"
elif command -v sysctl >/dev/null 2>&1; then
  JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"
else
  JOBS="${JOBS:-4}"
fi

# Detect host. Prefer uname; fall back to Windows_NT for MSYS/Cygwin-less envs.
UNAME="$(uname -s 2>/dev/null || echo unknown)"
IS_MSVC=0
case "$UNAME" in
  Darwin)
    CC_BIN="${CC:-clang}"
    EXTRA_CFG=(
      --disable-videotoolbox
      --disable-audiotoolbox
      --disable-appkit
      --disable-coreimage
      --disable-metal
    )
    ;;
  Linux)
    CC_BIN="${CC:-gcc}"
    EXTRA_CFG=()
    ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT|unknown)
    # Windows: require MSVC tools already on PATH (ci uses ilammy/msvc-dev-cmd).
    if ! command -v cl >/dev/null 2>&1 && ! command -v cl.exe >/dev/null 2>&1; then
      echo "error: on Windows, cl.exe must be on PATH (run from VS x64 env / msvc-dev-cmd)." >&2
      exit 1
    fi
    IS_MSVC=1
    CC_BIN="${CC:-cl}"
    EXTRA_CFG=(
      --toolchain=msvc
    )
    ;;
  *)
    echo "error: unsupported host '$UNAME'" >&2
    exit 1
    ;;
esac

mkdir -p "$ROOT/third_party"
if [[ ! -d "$SRC_DIR" ]]; then
  if [[ ! -f "$TARBALL" ]]; then
    echo "Downloading $URL ..."
    curl -L --fail -o "$TARBALL" "$URL"
  fi
  echo "Extracting $TARBALL ..."
  tar -C "$ROOT/third_party" -xf "$TARBALL"
fi

rm -rf "$BUILD" "$PREFIX"
mkdir -p "$BUILD" "$PREFIX"
cd "$BUILD"

# Absolute paths avoid MSYS vs Windows path confusion during install.
if command -v cygpath >/dev/null 2>&1; then
  SRC_DIR_CFG="$(cygpath -m "$SRC_DIR")"
  PREFIX_CFG="$(cygpath -m "$PREFIX")"
else
  SRC_DIR_CFG="$SRC_DIR"
  PREFIX_CFG="$PREFIX"
fi

echo "Configuring FFmpeg ${FFVER} (cc=$CC_BIN, jobs=$JOBS) ..."
"$SRC_DIR/configure" \
  --prefix="$PREFIX_CFG" \
  --cc="$CC_BIN" \
  --enable-gpl \
  --enable-version3 \
  --disable-debug \
  --disable-doc \
  --disable-programs \
  --disable-avdevice \
  --disable-avfilter \
  --disable-swscale \
  --disable-postproc \
  --disable-network \
  --disable-asm \
  --disable-everything \
  --enable-avutil \
  --enable-avcodec \
  --enable-avformat \
  --enable-swresample \
  --enable-demuxer=mpegts \
  --enable-muxer=mp4 \
  --enable-muxer=mov \
  --enable-parser=h264 \
  --enable-parser=aac \
  --enable-decoder=h264 \
  --enable-decoder=aac \
  --enable-bsf=aac_adtstoasc \
  --enable-bsf=extract_extradata \
  --enable-protocol=file \
  --enable-static \
  --disable-shared \
  "${EXTRA_CFG[@]}"

echo "Building ..."
make -j"$JOBS"
echo "Installing to $PREFIX ..."
make install

# FFmpeg gap: h264 SEI code references aom film-grain helpers that
# --disable-everything does not always pull into libavcodec.a.
# Same for a vp9_superframe bsf symbol sometimes left in the bsf table.
patch_missing_avcodec_obj() {
  local src_name="$1"
  local obj_stem="$2"
  local libavcodec=""
  local src_c="$SRC_DIR/libavcodec/${src_name}.c"

  if [[ ! -f "$src_c" ]]; then
    echo "warning: missing $src_c (skip patch $obj_stem)" >&2
    return 0
  fi

  # Locate installed static libavcodec.
  if [[ -f "$PREFIX/lib/libavcodec.a" ]]; then
    libavcodec="$PREFIX/lib/libavcodec.a"
  else
    # MSVC install names vary (libavcodec.a / avcodec.lib)
    libavcodec="$(find "$PREFIX/lib" -maxdepth 1 \( -name 'libavcodec.a' -o -name 'avcodec.lib' -o -name 'libavcodec.lib' \) | head -n1 || true)"
  fi
  if [[ -z "$libavcodec" || ! -f "$libavcodec" ]]; then
    echo "warning: libavcodec static library not found under $PREFIX/lib" >&2
    return 0
  fi

  if command -v ar >/dev/null 2>&1 && ar t "$libavcodec" 2>/dev/null | grep -Eq "${obj_stem}"; then
    return 0
  fi

  # Only auto-patch Unix .a archives (MSVC .lib needs lib.exe; rare on CI if configure is complete).
  if [[ "$libavcodec" != *.a ]]; then
    echo "warning: cannot patch non-.a archive $libavcodec for $obj_stem" >&2
    return 0
  fi

  mkdir -p libavcodec
  if [[ "$IS_MSVC" -eq 1 ]]; then
    # cl needs different flags; skip object patch on MSVC and rely on full decoder set.
    echo "warning: skip object patch for $obj_stem on MSVC" >&2
    return 0
  fi

  "$CC_BIN" -I. -I"$SRC_DIR" -DHAVE_AV_CONFIG_H -Os -std=c17 \
    -c "$src_c" -o "libavcodec/${obj_stem}.o"
  ar r "$libavcodec" "libavcodec/${obj_stem}.o"
  echo "Patched $(basename "$libavcodec") += ${obj_stem}.o"
}

patch_missing_avcodec_obj aom_film_grain aom_film_grain
patch_missing_avcodec_obj vp9_superframe_bsf vp9_superframe

# Hard requirements for the app build.
for hdr in \
  "$PREFIX/include/libavformat/avformat.h" \
  "$PREFIX/include/libavcodec/avcodec.h" \
  "$PREFIX/include/libavutil/avutil.h"
do
  if [[ ! -f "$hdr" ]]; then
    echo "error: expected header missing after install: $hdr" >&2
    exit 1
  fi
done

if ! find "$PREFIX/lib" -maxdepth 1 \( -name 'libavformat.*' -o -name 'avformat.lib' \) | grep -q .; then
  echo "error: libavformat static library missing under $PREFIX/lib" >&2
  ls -la "$PREFIX/lib" >&2 || true
  exit 1
fi

echo "Installed minimal FFmpeg to: $PREFIX"
echo "Headers:"
ls -la "$PREFIX/include/libavformat" "$PREFIX/include/libavcodec" | sed -n '1,20p'
echo "Libs:"
ls -la "$PREFIX/lib"
