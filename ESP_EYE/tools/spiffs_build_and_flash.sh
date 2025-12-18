#!/bin/bash
# Build and flash SPIFFS image from spiffs/ folder
# Usage: TOOLS_PORT=/dev/ttyUSB0 ./tools/spiffs_build_and_flash.sh

set -euo pipefail
PORT=${TOOLS_PORT:-/dev/ttyUSB0}

echo "Building project and SPIFFS image..."
idf.py build

echo "Creating SPIFFS image (spiffs-image)..."
idf.py spiffs-image

echo "Flashing SPIFFS image to ${PORT}..."
idf.py -p "${PORT}" spiffs-flash

echo "Done. SPIFFS flashed to ${PORT}."
exit 0
