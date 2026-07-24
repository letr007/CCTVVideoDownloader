#!/usr/bin/env bash
# Build a minimal static FFmpeg for TS(H264/AAC) -> MP4 copy-remux on macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFVER="${FFVER:-7.1.1}"
SRC_DIR="$ROOT/third_party/ffmpeg-${FFVER}"
TARBALL="$ROOT/third_party/ffmpeg-${FFVER}.tar.xz"
PREFIX="$ROOT/third_party/ffmpeg-min"
BUILD="$ROOT/third_party/ffmpeg-min-build"
URL="https://ffmpeg.org/releases/ffmpeg-${FFVER}.tar.xz"

mkdir -p "$ROOT/third_party"
if [[ ! -d "$SRC_DIR" ]]; then
  [[ -f "$TARBALL" ]] || curl -L --fail -o "$TARBALL" "$URL"
  tar -C "$ROOT/third_party" -xf "$TARBALL"
fi

rm -rf "$BUILD" "$PREFIX"
mkdir -p "$BUILD" "$PREFIX"
cd "$BUILD"

"$SRC_DIR/configure" \
  --prefix="$PREFIX" \
  --cc=clang \
  --enable-gpl --enable-version3 \
  --disable-debug --disable-doc --disable-programs \
  --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
  --disable-network --disable-videotoolbox --disable-audiotoolbox \
  --disable-appkit --disable-coreimage --disable-metal \
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
  --extra-cflags="-Os"

make -j"$(sysctl -n hw.ncpu)"
make install

# H264 SEI references aom film-grain helpers; bsf table may reference vp9_superframe.
patch_obj() {
  local src_name="$1"
  local obj_name="$2"
  if ar t "$PREFIX/lib/libavcodec.a" | grep -q "${obj_name}"; then
    return 0
  fi
  clang -I. -I"$SRC_DIR" -DHAVE_AV_CONFIG_H -Os -std=c17 \
    -c "$SRC_DIR/libavcodec/${src_name}.c" -o "libavcodec/${obj_name}.o"
  ar r "$PREFIX/lib/libavcodec.a" "libavcodec/${obj_name}.o"
  echo "Patched libavcodec.a += ${obj_name}.o"
}

patch_obj aom_film_grain aom_film_grain
# Source file is vp9_superframe_bsf.c, object often named vp9_superframe.o / vp9_superframe_bsf.o
if ! ar t "$PREFIX/lib/libavcodec.a" | grep -Eq 'vp9_superframe'; then
  patch_obj vp9_superframe_bsf vp9_superframe_bsf
fi

echo "Installed minimal FFmpeg to: $PREFIX"
ls -lh "$PREFIX/lib"/*.a
cat <<EOF

Static link into an app (macOS needs force_load for codec tables):
  c++ ... -I$PREFIX/include \\
    -Wl,-force_load,$PREFIX/lib/libavformat.a \\
    -Wl,-force_load,$PREFIX/lib/libavcodec.a \\
    -Wl,-force_load,$PREFIX/lib/libswresample.a \\
    -Wl,-force_load,$PREFIX/lib/libavutil.a \\
    -lz -lbz2 -liconv

CMake will auto-detect $PREFIX via cmake/FindFFmpegMin.cmake.
EOF
