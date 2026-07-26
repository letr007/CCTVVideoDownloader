#!/usr/bin/env bash
# Build a guided macOS DMG from an existing .app bundle.
# Usage:
#   scripts/package-macos-dmg.sh <app_path> <output_dmg_path> [background_png]
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <app_path> <output_dmg_path> [background_png]" >&2
  exit 2
fi

APP_PATH="$1"
OUTPUT_DMG="$2"
BACKGROUND_SRC="${3:-packaging/macos/dmg-background.png}"
VOLNAME="${DMG_VOLNAME:-CCTV Installer}"

if [[ ! -d "$APP_PATH" ]]; then
  echo "app bundle not found: $APP_PATH" >&2
  exit 1
fi
if [[ ! -f "$BACKGROUND_SRC" ]]; then
  echo "background image not found: $BACKGROUND_SRC" >&2
  exit 1
fi

if ! command -v magick >/dev/null 2>&1; then
  echo "ImageMagick 'magick' is required to scale the DMG background" >&2
  exit 1
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cctv-dmg.XXXXXX")"
VERIFY_DEVICE=""
cleanup() {
  local mount
  if [[ -n "$VERIFY_DEVICE" ]]; then
    hdiutil detach "$VERIFY_DEVICE" >/dev/null 2>&1 || true
  fi
  for mount in "/Volumes/${VOLNAME}" /Volumes/"${VOLNAME}"*; do
    if [[ -e "$mount" ]]; then
      hdiutil detach "$mount" >/dev/null 2>&1 || true
    fi
  done
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

STAGE="$WORK_DIR/stage"
RW_DMG="$WORK_DIR/package.rw.dmg"
BG_USE="$WORK_DIR/background.png"

mkdir -p "$STAGE/.background"
ditto "$APP_PATH" "$STAGE/CCTVVideoDownloader.app"
codesign --verify --deep --strict --verbose=2 "$STAGE/CCTVVideoDownloader.app"
ln -s /Applications "$STAGE/Applications"

# Finder content area for bounds {180,120,1010,590} is about 830x470.
magick "$BACKGROUND_SRC" -resize '830x470^' -gravity center -extent 830x470 "$BG_USE"
cp "$BG_USE" "$STAGE/.background/background.png"

hdiutil create \
  -volname "$VOLNAME" \
  -srcfolder "$STAGE" \
  -format UDRW \
  -fs HFS+ \
  -size 140m \
  -ov \
  "$RW_DMG" >/dev/null

MOUNT="$(hdiutil attach -readwrite -noverify -noautoopen "$RW_DMG" | sed -n 's#.*\(/Volumes/.*\)#\1#p' | tail -n 1)"
if [[ -z "$MOUNT" || ! -d "$MOUNT/CCTVVideoDownloader.app" ]]; then
  echo "failed to mount writable DMG" >&2
  exit 1
fi

mkdir -p "$MOUNT/.background"
cp "$BG_USE" "$MOUNT/.background/background.png"
if command -v SetFile >/dev/null 2>&1; then
  SetFile -a V "$MOUNT/.background" || true
fi

osascript <<EOF
tell application "Finder"
  tell disk "$VOLNAME"
    open
    set current view of container window to icon view
    set toolbar visible of container window to false
    set statusbar visible of container window to false
    set bounds of container window to {180, 120, 1010, 590}
    set viewOptions to icon view options of container window
    set arrangement of viewOptions to not arranged
    set icon size of viewOptions to 104
    set text size of viewOptions to 13
    try
      set background picture of viewOptions to file ".background:background.png"
    end try
    set position of item "CCTVVideoDownloader.app" of container window to {210, 250}
    set position of item "Applications" of container window to {620, 250}
    update without registering applications
    delay 1
    close
    open
    delay 1
    close
  end tell
end tell
EOF

sync
hdiutil detach "$MOUNT" >/dev/null

mkdir -p "$(dirname "$OUTPUT_DMG")"
rm -f "$OUTPUT_DMG"
hdiutil convert "$RW_DMG" -format UDZO -imagekey zlib-level=9 -o "$OUTPUT_DMG" >/dev/null

# Verify the exact app stored in the final compressed image, not only the
# source bundle used to create it.
VERIFY_MOUNT="$WORK_DIR/verify"
mkdir -p "$VERIFY_MOUNT"
VERIFY_DEVICE="$(hdiutil attach -readonly -noverify -noautoopen \
  -mountpoint "$VERIFY_MOUNT" "$OUTPUT_DMG" | awk '/^\/dev\// { print $1; exit }')"
if [[ -z "$VERIFY_DEVICE" || ! -d "$VERIFY_MOUNT/CCTVVideoDownloader.app" ]]; then
  echo "failed to mount final DMG for verification" >&2
  exit 1
fi
codesign --verify --deep --strict --verbose=2 "$VERIFY_MOUNT/CCTVVideoDownloader.app"
python3 - "$VERIFY_MOUNT/CCTVVideoDownloader.app" <<'PY'
import subprocess
import sys
import time

process = subprocess.Popen([f"{sys.argv[1]}/Contents/MacOS/CCTVVideoDownloader"])
time.sleep(5)
if process.poll() is not None:
    raise SystemExit(f"DMG app exited with {process.returncode}")
process.terminate()
process.wait(timeout=5)
PY
hdiutil detach "$VERIFY_DEVICE" >/dev/null
VERIFY_DEVICE=""

echo "created $OUTPUT_DMG"
