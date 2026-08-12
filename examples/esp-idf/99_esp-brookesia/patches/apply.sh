#!/bin/bash
# Apply local patches to managed components after idf.py fullclean
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BSP_FILE="$SCRIPT_DIR/../managed_components/waveshare__esp32_s3_touch_amoled_1_75c/esp32_s3_touch_amoled_1_75c.c"

if [ -f "$BSP_FILE" ]; then
    patch -p1 -d "$SCRIPT_DIR/../managed_components/waveshare__esp32_s3_touch_amoled_1_75c" < "$SCRIPT_DIR/0001-fix-dma-underflow-use-internal-ram-for-display-buffers.patch"
    echo "Patches applied."
else
    echo "BSP file not found. Run 'idf.py build' first to fetch managed components."
    exit 1
fi
