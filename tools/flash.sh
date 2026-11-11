#!/bin/sh
# Build and flash LOVELACE firmware + web UI in one step.
#
#   sh tools/flash.sh            build, then flash (keeps Wi-Fi/Spotify settings)
#   sh tools/flash.sh --erase    erase the whole chip first (factory reset)
#   sh tools/flash.sh --image    only build work/flash/lovelace-<ver>-full.bin
#                                for the browser flasher (offset 0x0)
#   PORT=/dev/cu.xxx sh tools/flash.sh   pick the serial port yourself
#
# The CH340N has no DTR/RTS wiring, so the chip cannot be reset by software:
# hold the button while plugging USB in, then plug in again after flashing.
set -eu

cd "$(dirname "$0")/.."
ROOT=$PWD
export PLATFORMIO_CORE_DIR="$ROOT/work/platformio"
PIO="$ROOT/.venv/bin/pio"
PY="$ROOT/.venv/bin/python"
ESPTOOL="$PLATFORMIO_CORE_DIR/packages/tool-esptoolpy/esptool.py"
ENV=lovelace-as-004
BUILD="$ROOT/firmware/.pio/build/$ENV"
VERSION=$(cat firmware/version.txt)
OUT="$ROOT/work/flash"
BAUD=${BAUD:-460800}

mode=flash
case "${1:-}" in
  "") ;;
  --erase) mode=erase ;;
  --image) mode=image ;;
  *) sed -n '2,11p' "$0"; exit 2 ;;
esac

[ -x "$PIO" ] || { echo "No .venv: see README (python3.11 -m venv .venv)"; exit 1; }

echo "== Building firmware $VERSION and web UI"
# PlatformIO re-runs CMake (which reads version.txt) only when a CMakeLists
# changes, so a bare version bump would ship the old version string.
if [ firmware/version.txt -nt "$BUILD/CMakeCache.txt" ]; then
  touch firmware/CMakeLists.txt
fi
# A half-written build folder (e.g. two builds at once) fails with missing
# generated headers: clean it and build once more before giving up.
if ! "$PIO" run -s -d firmware -e "$ENV"; then
  echo "== Build failed; cleaning the build folder and retrying once"
  rm -rf "$BUILD"
  "$PIO" run -s -d firmware -e "$ENV"
fi
"$PIO" run -s -d firmware -e "$ENV" -t buildfs

# Offsets follow components/boards/partitions.csv.
set -- 0x1000 "$BUILD/bootloader.bin" \
       0x8000 "$BUILD/partitions.bin" \
       0x19000 "$BUILD/ota_data_initial.bin" \
       0x20000 "$BUILD/firmware.bin" \
       0x620000 "$BUILD/spiffs.bin"

mkdir -p "$OUT"
IMAGE="$OUT/lovelace-$VERSION-full.bin"
"$PY" "$ESPTOOL" --chip esp32 merge_bin -o "$IMAGE" "$@" >/dev/null
cp "$BUILD/firmware.bin" "$OUT/lovelace-$VERSION-ota.bin"
echo "   $IMAGE  (browser flasher, offset 0x0, erases settings)"
echo "   $OUT/lovelace-$VERSION-ota.bin  (web UI OTA update)"
[ "$mode" = image ] && exit 0

find_port() {
  for p in /dev/cu.wchusbserial* /dev/cu.usbserial* /dev/ttyUSB*; do
    [ -e "$p" ] && { echo "$p"; return; }
  done
}

PORT=${PORT:-}
if [ -z "$PORT" ]; then
  echo
  echo "== Hold the board button, plug USB in, then release the button."
  i=0
  while [ -z "$(find_port)" ]; do
    i=$((i + 1))
    [ $i -gt 60 ] && { echo "No CH340 port after 60 s (check the cable carries data)"; exit 1; }
    sleep 1
  done
  PORT=$(find_port)
fi
echo "   port $PORT"

run() { "$PY" "$ESPTOOL" --chip esp32 --port "$PORT" --baud "$BAUD" \
          --before no_reset --after no_reset "$@"; }

if [ "$mode" = erase ]; then
  echo "== Erasing the whole flash (Wi-Fi, Spotify login and pairings are lost)"
  run erase_flash
fi
echo "== Writing"
run write_flash --flash_mode keep --flash_freq keep --flash_size keep "$@"

echo
echo "Done. Unplug USB and plug it in again WITHOUT the button to start."
echo "Log: sh tools/monitor.sh"
