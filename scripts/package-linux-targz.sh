#!/usr/bin/env bash
# Build a portable Linux tar.gz from a cmake-installed Qt application tree.
#
# Uses linuxdeploy's recursive dependency collection so the archive carries the
# third-party shared libraries that Qt's deploy script skips (e.g. libzstd,
# libglib, libpcre, krb5). The CLI ships a bare $ORIGIN/../lib RPATH with no
# wrapper, so collected libraries are merged back into <prefix>/lib where the
# loader already looks for them.
#
# Usage:
#   scripts/package-linux-targz.sh <install_dir> <output_tar_gz>
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <install_dir> <output_tar_gz>" >&2
  exit 2
fi

INSTALL_DIR="$(cd "$1" && pwd)"
OUTPUT_ARCHIVE="$2"
# Match the AppImage packaging convention: honour an explicit arch override,
# otherwise derive it from the build host.
LINUXDEPLOY_ARCH="${LINUXDEPLOY_ARCH:-$(uname -m)}"
case "$LINUXDEPLOY_ARCH" in
  x86_64|aarch64) : ;;
  *) echo "unsupported architecture: $LINUXDEPLOY_ARCH" >&2; exit 1 ;;
esac
LINUXDEPLOY_URL="${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${LINUXDEPLOY_ARCH}.AppImage}"
# Allow a pre-staged linuxdeploy AppImage so CI/verify can skip the GitHub download.
LINUXDEPLOY_APPIMAGE="${LINUXDEPLOY_APPIMAGE:-}"

if [[ ! -x "$INSTALL_DIR/bin/cctv-dl" ]]; then
  echo "CLI executable not found: $INSTALL_DIR/bin/cctv-dl" >&2
  exit 1
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cctv-targz.XXXXXX")"
cleanup() {
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

APPDIR="$WORK_DIR/AppDir"
mkdir -p "$APPDIR"

# Seed the AppDir with the deployed install tree. Qt's deploy script defines
# that layout (bin/ with qt.conf, lib/, plugins/) and the CLI locates its
# plugins through the prefix declared in qt.conf, so the tree moves as a unit.
cp -a "$INSTALL_DIR"/. "$APPDIR"/
chmod +x "$APPDIR/bin/cctv-dl"

# usr/lib is linuxdeploy's working directory: it resolves the dependency
# closure from there, including libraries that only exist in this tree (Qt's
# bundled ICU, for one). The CLI's $ORIGIN/../lib RUNPATH keeps resolving from
# lib/, so the results are merged back below.
mkdir -p "$APPDIR/usr/lib" "$APPDIR/usr/bin"
cp -a "$INSTALL_DIR/lib"/. "$APPDIR/usr/lib/"
ln -sfn ../../bin/cctv-dl "$APPDIR/usr/bin/cctv-dl"

TOOLS_DIR="$WORK_DIR/tools"
mkdir -p "$TOOLS_DIR"
if [[ -n "$LINUXDEPLOY_APPIMAGE" && -f "$LINUXDEPLOY_APPIMAGE" ]]; then
  cp "$LINUXDEPLOY_APPIMAGE" "$TOOLS_DIR/linuxdeploy.AppImage"
else
  curl -L --fail --retry 3 -o "$TOOLS_DIR/linuxdeploy.AppImage" "$LINUXDEPLOY_URL"
fi
chmod +x "$TOOLS_DIR/linuxdeploy.AppImage"
(
  cd "$TOOLS_DIR"
  ./linuxdeploy.AppImage --appimage-extract >/dev/null
  mv squashfs-root linuxdeploy-root
)
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-root/AppRun"

# Collect the closure for the executable, the bundled Qt libraries and the
# deployed plugins, so dependencies of the plugins are staged as well.
EXTRA_LIB_ARGS=()
while IFS= read -r -d '' sofile; do
  EXTRA_LIB_ARGS+=(--library "$sofile")
done < <(find "$APPDIR/plugins" "$APPDIR/lib" -name '*.so*' -type f -print0 2>/dev/null)

"$LINUXDEPLOY" --appdir "$APPDIR" --executable "$APPDIR/usr/bin/cctv-dl" "${EXTRA_LIB_ARGS[@]}"

# Merge newly collected libraries into <prefix>/lib. Files already present keep
# the staged copy; the real version is preferred over linuxdeploy's dedup copy.
find "$APPDIR/usr/lib" -maxdepth 1 -name '*.so*' -print0 | while IFS= read -r -d '' f; do
  base="$(basename "$f")"
  if [[ ! -e "$APPDIR/lib/$base" ]]; then
    cp -L "$f" "$APPDIR/lib/$base"
  fi
done

# linuxdeploy blacklists libgpg-error and libcom_err even though they are
# transitive dependencies of libgcrypt/libkrb5 (used by Qt TLS). They are tiny
# and self-contained (only depend on libc), so copy them explicitly so the
# archive is fully self-contained beyond core C runtime libs.
for extra in libgpg-error.so.0 libcom_err.so.2; do
  if ! find "$APPDIR/lib" -name "$extra*" -print -quit | grep -q .; then
    src="$(ldconfig -p 2>/dev/null | awk -v l="$extra" '$1==l {print $NF; exit}')"
    if [[ -n "$src" && -f "$src" ]]; then
      cp -L "$src" "$APPDIR/lib/$extra"
    fi
  fi
done

# Drop the staging layout; the archive ships bin/ (including qt.conf), lib/
# and plugins/.
rm -rf "$APPDIR/usr" "$APPDIR/AppRun" "$APPDIR/.DirIcon"

# A missing qt.conf or TLS plugin turns every HTTPS request into
# "TLS initialization failed" at runtime, so the archive has to carry both.
if [[ ! -f "$APPDIR/bin/qt.conf" ]]; then
  echo "error: bin/qt.conf missing from the CLI archive" >&2
  exit 1
fi
if [[ -z "$(find "$APPDIR/plugins/tls" -maxdepth 1 -name '*.so' -print -quit 2>/dev/null)" ]]; then
  echo "error: Qt TLS plugin missing from the CLI archive" >&2
  exit 1
fi

# Sanity: every non-core dependency of the entry point, the bundled libraries
# and the plugins must resolve in lib/.
MISSING=""
while IFS= read -r -d '' f; do
  while read -r lib; do
    [[ -z "$lib" ]] && continue
    case "$lib" in
      ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread*|librt.so*|libresolv.so*|libgcc_s.so*|libstdc++.so*|libz.so*|linux-vdso*)
        continue ;; # core runtime, intentionally not bundled
    esac
    if [[ ! -e "$APPDIR/lib/$lib" ]]; then
      MISSING="$MISSING $lib"
    fi
  done < <(readelf -d "$f" 2>/dev/null | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p')
done < <(printf '%s\0' "$APPDIR/bin/cctv-dl"; find "$APPDIR" -type f -name '*.so*' -print0)
if [[ -n "$MISSING" ]]; then
  echo "error: unresolved library dependencies:$MISSING" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT_ARCHIVE")"
PACKAGE_ROOT="cctv-dl"
ARCHIVE_STAGE="$WORK_DIR/archive"
mkdir -p "$ARCHIVE_STAGE/$PACKAGE_ROOT"
cp -a "$APPDIR"/. "$ARCHIVE_STAGE/$PACKAGE_ROOT/"
tar -C "$ARCHIVE_STAGE" -czf "$OUTPUT_ARCHIVE" "$PACKAGE_ROOT"
echo "created $OUTPUT_ARCHIVE"
