#!/usr/bin/env bash
# macOS alias for the unified builder.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec "$ROOT/scripts/build-ffmpeg-min.sh" "$@"
