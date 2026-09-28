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
LINUXDEPLOY_APPIMAGE="${LINUXDEPLOY_APPIMAGE:-}"

if [[ ! -d "$INSTALL_DIR" ]]; then
  echo "install directory not found: $INSTALL_DIR" >&2
  exit 1
fi
INSTALL_DIR="$(cd "$INSTALL_DIR" && pwd)"
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

mkdir -p "$APPDIR/usr/lib" "$APPDIR/lib"
# Stage Qt libraries in lib/ (where Qt's RUNPATH points) and under usr/lib for linuxdeploy.
if [[ -d "$INSTALL_DIR/lib" ]]; then
  cp -a "$INSTALL_DIR/lib"/. "$APPDIR/lib/"
  cp -a "$INSTALL_DIR/lib"/. "$APPDIR/usr/lib/"
fi

curl -L --fail --retry 3 -o "$TOOLS_DIR/linuxdeploy.AppImage" "$LINUXDEPLOY_URL"
chmod +x "$TOOLS_DIR/linuxdeploy.AppImage"

# Extract tools so they can run in CI without FUSE.
(
  cd "$TOOLS_DIR"
  ./linuxdeploy.AppImage --appimage-extract >/dev/null
  mv squashfs-root linuxdeploy-root
)

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-root/AppRun"

# Pass the main executable and every bundled library and plugin (.so) to linuxdeploy
# so it recursively collects all third-party dependencies (such as libxkbcommon-x11,
# libxcb-cursor, libxcb-*, libzstd, libglib, etc.).
EXTRA_LIB_ARGS=()
while IFS= read -r -d '' sofile; do
  EXTRA_LIB_ARGS+=(--library "$sofile")
done < <(find "$APPDIR/plugins" "$APPDIR/lib" -name '*.so*' -type f -print0 2>/dev/null)

# Create a self-contained AppRun entrypoint that sets up search paths and launches the GUI.
cat > "$WORK_DIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "${0}")")"
export APPDIR="${HERE}"
export PATH="${HERE}/bin:${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/lib:${HERE}/usr/lib:${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/plugins:${HERE}/usr/plugins"
export QML2_IMPORT_PATH="${HERE}/qml:${HERE}/usr/qml:${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
exec "${HERE}/bin/CCTVVideoDownloader" "$@"
EOF
chmod +x "$WORK_DIR/AppRun"

# First pass: collect and deploy dependencies into AppDir.
(
  cd "$WORK_DIR"
  "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/cctvvideodownloader.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/cctvvideodownloader.png" \
    --executable "$APPDIR/usr/bin/$APP_NAME" \
    --custom-apprun "$WORK_DIR/AppRun" \
    "${EXTRA_LIB_ARGS[@]}"
)

# Merge newly collected libraries from usr/lib into lib. Files already present keep
# the staged copy; the real version is preferred over linuxdeploy's dedup copy.
find "$APPDIR/usr/lib" -maxdepth 1 -name '*.so*' -print0 | while IFS= read -r -d '' f; do
  base="$(basename "$f")"
  if [[ ! -e "$APPDIR/lib/$base" ]]; then
    cp -L "$f" "$APPDIR/lib/$base"
  fi
done

# Ensure critical transitive libraries (which linuxdeploy may blacklist or skip)
# are staged into both lib and usr/lib.
for extra in libgpg-error.so.0 libcom_err.so.2 libxkbcommon-x11.so.0 libxcb-cursor.so.0; do
  for dest in "$APPDIR/lib" "$APPDIR/usr/lib"; do
    if ! find "$dest" -name "$extra*" -print -quit | grep -q .; then
      src="$(ldconfig -p 2>/dev/null | awk -v l="$extra" '$1==l {print $NF; exit}')"
      if [[ -n "$src" && -f "$src" ]]; then
        cp -L "$src" "$dest/$extra"
      fi
    fi
  done
done

cp -f "$WORK_DIR/AppRun" "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"

# Sanity: every non-core dependency of the binary, plugins, and Qt libs must resolve.
MISSING=""
for f in "$APPDIR"/bin/* "$APPDIR"/lib/libQt6*.so.6 $(find "$APPDIR/plugins" -name '*.so*' 2>/dev/null); do
  [[ -f "$f" ]] || continue
  while read -r lib; do
    [[ -z "$lib" ]] && continue
    case "$lib" in
      ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread*|librt.so*|libresolv.so*|libgcc_s.so*|libstdc++.so*|libz.so*|linux-vdso*|\
      libGL.so*|libGLX.so*|libOpenGL.so*|libEGL.so*|libGLdispatch.so*|libdrm.so*|libgbm.so*|\
      libX11.so*|libX11-xcb.so*|libxcb.so.1*|\
      libfontconfig.so*|libfreetype.so*|\
      libasound.so*|libudev.so*)
        continue ;; # core runtime and system graphics/display server drivers
    esac
    if [[ ! -e "$APPDIR/lib/$lib" && ! -e "$APPDIR/usr/lib/$lib" ]]; then
      MISSING="$MISSING $lib"
    fi
  done < <(readelf -d "$f" 2>/dev/null | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p')
done
if [[ -n "$MISSING" ]]; then
  echo "error: unresolved library dependencies in AppDir:$MISSING" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT_APPIMAGE")"
OUTPUT_DIR="$(cd "$(dirname "$OUTPUT_APPIMAGE")" && pwd)"
OUTPUT_NAME="$(basename "$OUTPUT_APPIMAGE")"
OUTPUT_PATH="$OUTPUT_DIR/$OUTPUT_NAME"
rm -f "$OUTPUT_PATH"

# Second pass: package the completed AppDir into the AppImage.
(
  cd "$WORK_DIR"
  "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/cctvvideodownloader.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/cctvvideodownloader.png" \
    --custom-apprun "$WORK_DIR/AppRun" \
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
echo "created $OUTPUT_PATH"
