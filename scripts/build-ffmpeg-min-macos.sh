#!/usr/bin/env bash
# macOS convenience wrapper (same as build-ffmpeg-min.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec "$ROOT/scripts/build-ffmpeg-min.sh" "$@"
