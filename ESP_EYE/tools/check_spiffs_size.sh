#!/bin/bash
# Check total size of files under spiffs/ and optionally fail if over limit
# Usage: ./tools/check_spiffs_size.sh [max_bytes]

set -euo pipefail
MAX_BYTES=${1:-10240} # default 10 KB
DIR="spiffs"

if [ ! -d "$DIR" ]; then
  echo "spiffs/ directory not found"
  exit 1
fi

# Sum file sizes in bytes
TOTAL=0
while IFS= read -r -d '' f; do
  s=$(stat -c%s "$f")
  TOTAL=$((TOTAL + s))
done < <(find "$DIR" -type f -print0)

echo "spiffs total size: ${TOTAL} bytes"
if [ "$TOTAL" -gt "$MAX_BYTES" ]; then
  echo "ERROR: spiffs contents exceed ${MAX_BYTES} bytes"
  exit 2
fi

# If spiffs image exists, report its size
BUILD_IMG="$(find build -maxdepth 1 -type f -name "*storage*.bin" -o -name "*spiffs*.bin" 2>/dev/null | head -n1 || true)"
if [ -n "$BUILD_IMG" ]; then
  img_size=$(stat -c%s "$BUILD_IMG")
  echo "SPIFFS image: $BUILD_IMG (${img_size} bytes)"
fi

echo "OK: within ${MAX_BYTES} bytes"
exit 0
