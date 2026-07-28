#!/usr/bin/env bash
# Package an installed cctv-dl binary as a portable macOS archive.
# Usage:
#   scripts/package-macos-cli.sh <install_dir> <output_tar_gz>
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <install_dir> <output_tar_gz>" >&2
  exit 2
fi

INSTALL_DIR="$1"
OUTPUT_ARCHIVE="$2"
PACKAGE_ROOT="cctv-dl"

if [[ ! -x "$INSTALL_DIR/bin/cctv-dl" ]]; then
  echo "CLI executable not found: $INSTALL_DIR/bin/cctv-dl" >&2
  exit 1
fi
if [[ -z "${QT_ROOT_DIR:-}" || ! -x "$QT_ROOT_DIR/bin/qtpaths" ]]; then
  echo "Qt tools not found under QT_ROOT_DIR" >&2
  exit 1
fi

FRAMEWORKS_DIR="$INSTALL_DIR/lib"
PLUGINS_DIR="$INSTALL_DIR/plugins"
mkdir -p "$FRAMEWORKS_DIR" "$PLUGINS_DIR/tls"
for framework in QtCore QtNetwork; do
  source="$QT_ROOT_DIR/lib/${framework}.framework"
  target="$FRAMEWORKS_DIR/${framework}.framework"
  if [[ ! -d "$source" ]]; then
    echo "required Qt framework not found: $source" >&2
    exit 1
  fi
  source="$(cd "$source" && pwd -P)"
  rm -rf "$target"
  cp -R "$source" "$target"
  rm -rf "$target/Versions/A/Headers" "$target/Headers"
  rm -f "$target/Versions/A/Resources/${framework}.prl"
  install_name_tool -id "@rpath/${framework}.framework/Versions/A/${framework}" \
    "$target/Versions/A/$framework"
done

PLUGIN_ROOT="$("$QT_ROOT_DIR/bin/qtpaths" --plugin-dir)"
TLS_PLUGIN="$PLUGIN_ROOT/tls/libqsecuretransportbackend.dylib"
if [[ ! -f "$TLS_PLUGIN" ]]; then
  echo "Qt Secure Transport plugin not found: $TLS_PLUGIN" >&2
  exit 1
fi
cp "$TLS_PLUGIN" "$PLUGINS_DIR/tls/"
install_name_tool \
  -change "@rpath/QtCore.framework/Versions/A/QtCore" \
          "@loader_path/../../lib/QtCore.framework/Versions/A/QtCore" \
  -change "@rpath/QtNetwork.framework/Versions/A/QtNetwork" \
          "@loader_path/../../lib/QtNetwork.framework/Versions/A/QtNetwork" \
  "$PLUGINS_DIR/tls/libqsecuretransportbackend.dylib"
cat > "$INSTALL_DIR/bin/qt.conf" <<'EOF'
[Paths]
Plugins = ../plugins
EOF

if ! otool -l "$INSTALL_DIR/bin/cctv-dl" | grep -A2 LC_RPATH | grep -Fx '         path @loader_path/../lib (offset 12)' >/dev/null; then
  install_name_tool -add_rpath "@loader_path/../lib" "$INSTALL_DIR/bin/cctv-dl"
fi
for framework in QtCore QtNetwork; do
  installed_path="$(otool -L "$INSTALL_DIR/bin/cctv-dl" | awk -v framework="$framework" '$1 ~ ("/" framework "\\.framework/Versions/A/" framework "$") { print $1; exit }')"
  if [[ -z "$installed_path" ]]; then
    echo "cctv-dl does not link required framework: $framework" >&2
    exit 1
  fi
  install_name_tool -change \
    "$installed_path" \
    "@rpath/${framework}.framework/Versions/A/${framework}" \
    "$INSTALL_DIR/bin/cctv-dl"
done

if otool -L "$INSTALL_DIR/bin/cctv-dl" | grep -F "$QT_ROOT_DIR"; then
  echo "cctv-dl still references the build-time Qt installation" >&2
  exit 1
fi

codesign --force --sign - "$INSTALL_DIR/lib/QtCore.framework/Versions/A/QtCore"
codesign --force --sign - "$INSTALL_DIR/lib/QtNetwork.framework/Versions/A/QtNetwork"
codesign --force --sign - "$INSTALL_DIR/plugins/tls/libqsecuretransportbackend.dylib"
codesign --force --sign - "$INSTALL_DIR/bin/cctv-dl"
codesign --verify --strict --verbose=2 "$INSTALL_DIR/bin/cctv-dl"
codesign --verify --strict --verbose=2 "$INSTALL_DIR/plugins/tls/libqsecuretransportbackend.dylib"
env -u DYLD_FRAMEWORK_PATH -u DYLD_LIBRARY_PATH -u QT_PLUGIN_PATH \
  "$INSTALL_DIR/bin/cctv-dl" --version

ARCHIVE_STAGE="$(mktemp -d "${TMPDIR:-/tmp}/cctv-dl-macos.XXXXXX")"
cleanup() {
  rm -rf "$ARCHIVE_STAGE"
}
trap cleanup EXIT
mkdir -p "$(dirname "$OUTPUT_ARCHIVE")" "$ARCHIVE_STAGE/$PACKAGE_ROOT"
cp -R "$INSTALL_DIR"/. "$ARCHIVE_STAGE/$PACKAGE_ROOT/"
tar -C "$ARCHIVE_STAGE" -czf "$OUTPUT_ARCHIVE" "$PACKAGE_ROOT"
echo "created $OUTPUT_ARCHIVE"
