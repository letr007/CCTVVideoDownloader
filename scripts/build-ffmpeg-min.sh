#!/usr/bin/env bash
# Build a minimal static FFmpeg for TS(H264/AAC) -> MP4 copy-remux.
# Supports macOS and Linux. Output: third_party/ffmpeg-min/{include,lib}
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFVER="${FFVER:-7.1.1}"
SRC_DIR="$ROOT/third_party/ffmpeg-${FFVER}"
TARBALL="$ROOT/third_party/ffmpeg-${FFVER}.tar.xz"
PREFIX="$ROOT/third_party/ffmpeg-min"
BUILD="$ROOT/third_party/ffmpeg-min-build"
URL="https://ffmpeg.org/releases/ffmpeg-${FFVER}.tar.xz"
JOBS="${JOBS:-$(command -v nproc >/dev/null && nproc || sysctl -n hw.ncpu)}"

UNAME="$(uname -s)"
case "$UNAME" in
  Darwin) CC_BIN="${CC:-clang}" ;;
  Linux)  CC_BIN="${CC:-gcc}" ;;
  *)
    echo "Unsupported OS: $UNAME (use build-ffmpeg-min-windows.ps1 on Windows)" >&2
    exit 1
    ;;
esac

mkdir -p "$ROOT/third_party"
if [[ ! -d "$SRC_DIR" ]]; then
  [[ -f "$TARBALL" ]] || curl -L --fail -o "$TARBALL" "$URL"
  tar -C "$ROOT/third_party" -xf "$TARBALL"
fi

rm -rf "$BUILD" "$PREFIX"
mkdir -p "$BUILD" "$PREFIX"
cd "$BUILD"

EXTRA_CFG=()
if [[ "$UNAME" == "Darwin" ]]; then
  EXTRA_CFG+=(
    --disable-videotoolbox
    --disable-audiotoolbox
    --disable-appkit
    --disable-coreimage
    --disable-metal
  )
fi

"$SRC_DIR/configure" \
  --prefix="$PREFIX" \
  --cc="$CC_BIN" \
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
  --extra-cflags="-Os" \
  "${EXTRA_CFG[@]}"

make -j"$JOBS"
make install

# H264 SEI references aom film-grain helpers; bsf table may reference vp9_superframe.
patch_obj() {
  local src_name="$1"
  local obj_name="$2"
  if ar t "$PREFIX/lib/libavcodec.a" | grep -q "${obj_name}"; then
    return 0
  fi
  if [[ ! -f "$SRC_DIR/libavcodec/${src_name}.c" ]]; then
    echo "warning: missing $SRC_DIR/libavcodec/${src_name}.c" >&2
    return 0
  fi
  "$CC_BIN" -I. -I"$SRC_DIR" -DHAVE_AV_CONFIG_H -Os -std=c17 \
    -c "$SRC_DIR/libavcodec/${src_name}.c" -o "libavcodec/${obj_name}.o"
  ar r "$PREFIX/lib/libavcodec.a" "libavcodec/${obj_name}.o"
  echo "Patched libavcodec.a += ${obj_name}.o"
}

patch_obj aom_film_grain aom_film_grain
if ! ar t "$PREFIX/lib/libavcodec.a" | grep -Eq 'vp9_superframe'; then
  patch_obj vp9_superframe_bsf vp9_superframe_bsf
fi

echo "Installed minimal FFmpeg to: $PREFIX"
ls -lh "$PREFIX/lib"/libav*.a "$PREFIX/lib"/libswresample.a 2>/dev/null || ls -lh "$PREFIX/lib"
