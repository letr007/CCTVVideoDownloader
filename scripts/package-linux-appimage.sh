#!/usr/bin/env bash
# Build a Linux AppImage from a cmake-installed Qt application tree.
# Usage:
#   scripts/package-linux-appimage.sh <install_dir> <output_appimage> [desktop_file] [icon_file]
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <install_dir> <output_appimage> [desktop_file] [icon_file]" >&2
  exit 2
fi

INSTALL_DIR="$1"
OUTPUT_APPIMAGE="$2"
DESKTOP_SRC="${3:-packaging/linux/cctvvideodownloader.desktop}"
ICON_SRC="${4:-packaging/linux/cctvvideodownloader.png}"
APP_NAME="CCTVVideoDownloader"
ARCH="${APPIMAGE_ARCH:-x86_64}"
LINUXDEPLOY_URL="${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage}"
LINUXDEPLOY_QT_URL="${LINUXDEPLOY_QT_URL:-https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage}"

if [[ ! -d "$INSTALL_DIR" ]]; then
  echo "install directory not found: $INSTALL_DIR" >&2
  exit 1
fi
if [[ ! -x "$INSTALL_DIR/bin/$APP_NAME" ]]; then
  echo "application binary not found: $INSTALL_DIR/bin/$APP_NAME" >&2
  exit 1
fi
if [[ ! -f "$DESKTOP_SRC" ]]; then
  echo "desktop file not found: $DESKTOP_SRC" >&2
  exit 1
fi
if [[ ! -f "$ICON_SRC" ]]; then
  echo "icon file not found: $ICON_SRC" >&2
  exit 1
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cctv-appimage.XXXXXX")"
cleanup() {
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

APPDIR="$WORK_DIR/AppDir"
TOOLS_DIR="$WORK_DIR/tools"
mkdir -p "$APPDIR" "$TOOLS_DIR"

# Seed AppDir from the already-deployed install tree.
cp -a "$INSTALL_DIR"/. "$APPDIR"/

mkdir -p \
  "$APPDIR/usr/bin" \
  "$APPDIR/usr/share/applications" \
  "$APPDIR/usr/share/icons/hicolor/256x256/apps"

if [[ -x "$APPDIR/bin/$APP_NAME" && ! -e "$APPDIR/usr/bin/$APP_NAME" ]]; then
  # Keep the original layout and also expose the binary under usr/bin for AppImage tools.
  ln -s "../../bin/$APP_NAME" "$APPDIR/usr/bin/$APP_NAME"
fi

cp "$DESKTOP_SRC" "$APPDIR/usr/share/applications/cctvvideodownloader.desktop"
cp "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/256x256/apps/cctvvideodownloader.png"
cp "$ICON_SRC" "$APPDIR/cctvvideodownloader.png"

curl -L --fail --retry 3 -o "$TOOLS_DIR/linuxdeploy.AppImage" "$LINUXDEPLOY_URL"
curl -L --fail --retry 3 -o "$TOOLS_DIR/linuxdeploy-plugin-qt.AppImage" "$LINUXDEPLOY_QT_URL"
chmod +x "$TOOLS_DIR/linuxdeploy.AppImage" "$TOOLS_DIR/linuxdeploy-plugin-qt.AppImage"

# Extract tools so they can run in CI without FUSE.
(
  cd "$TOOLS_DIR"
  ./linuxdeploy.AppImage --appimage-extract >/dev/null
  mv squashfs-root linuxdeploy-root
  ./linuxdeploy-plugin-qt.AppImage --appimage-extract >/dev/null
  mv squashfs-root linuxdeploy-plugin-qt-root
)

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-root/AppRun"
ln -sf "$TOOLS_DIR/linuxdeploy-plugin-qt-root/AppRun" "$TOOLS_DIR/linuxdeploy-plugin-qt"
export PATH="$TOOLS_DIR:$PATH"

if [[ -n "${QT_ROOT_DIR:-}" ]]; then
  export QMAKE="${QMAKE:-$QT_ROOT_DIR/bin/qmake}"
fi

OUTPUT_DIR="$(cd "$(dirname "$OUTPUT_APPIMAGE")" && pwd)"
OUTPUT_NAME="$(basename "$OUTPUT_APPIMAGE")"
OUTPUT_PATH="$OUTPUT_DIR/$OUTPUT_NAME"
rm -f "$OUTPUT_PATH"

(
  cd "$WORK_DIR"
  "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/cctvvideodownloader.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/cctvvideodownloader.png" \
    --executable "$APPDIR/usr/bin/$APP_NAME" \
    --output appimage
)

shopt -s nullglob
produced=( "$WORK_DIR"/*.AppImage )
if [[ ${#produced[@]} -eq 0 ]]; then
  echo "AppImage was not produced" >&2
  ls -la "$WORK_DIR" >&2 || true
  exit 1
fi

mv "${produced[0]}" "$OUTPUT_PATH"
chmod +x "$OUTPUT_PATH"
(
  cd "$WORK_DIR"
  "$OUTPUT_PATH" --appimage-extract >/dev/null
  test -x "squashfs-root/bin/cctv-dl"
)
echo "created $OUTPUT_PATH"
