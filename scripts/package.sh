#!/usr/bin/env bash
# Packs a release from the last `pio run`:
#   firmware.bin  - application image, used by OTA (web upload and internet update)
#   firmware.sig  - ECDSA P-256 / SHA-256 signature of firmware.bin (if a key is given)
#   factory.bin   - bootloader + partitions + app merged, flash at 0x0 on a blank chip
#   SHA256SUMS
# Usage: scripts/package.sh <outdir> [signing-key.pem]
set -euo pipefail
PIO=${PIO:-pio}
B=.pio/build/esp32dev
OUT=${1:-dist}
KEY=${2:-}
mkdir -p "$OUT"
cp "$B/firmware.bin" "$OUT/firmware.bin"

BOOT_APP0="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
"$PIO" pkg exec -p tool-esptoolpy -- esptool.py --chip esp32 merge_bin -o "$OUT/factory.bin" \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin"

if [ -n "$KEY" ]; then
  openssl dgst -sha256 -sign "$KEY" -out "$OUT/firmware.sig" "$OUT/firmware.bin"
fi
(cd "$OUT" && sha256sum ./*.bin $( [ -f firmware.sig ] && echo firmware.sig ) | sed 's| \./| |' > SHA256SUMS)
ls -l "$OUT"
