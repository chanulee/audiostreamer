#!/bin/sh
# Open the LOVELACE serial log (115200 bps). Ctrl+C to quit.
#
#   sh tools/monitor.sh
#   PORT=/dev/cu.xxx sh tools/monitor.sh
#
# PlatformIO lives in work/platformio; without PLATFORMIO_CORE_DIR it looks
# in ~/.platformio and reports "Unknown development platform".
set -eu

cd "$(dirname "$0")/.."
export PLATFORMIO_CORE_DIR="$PWD/work/platformio"

PORT=${PORT:-}
if [ -z "$PORT" ]; then
  for p in /dev/cu.wchusbserial* /dev/cu.usbserial* /dev/ttyUSB*; do
    [ -e "$p" ] && { PORT=$p; break; }
  done
fi
[ -n "$PORT" ] || { echo "No CH340 port: plug the board in (without the button)"; exit 1; }

echo "== $PORT  (unplug/replug the board to see the boot log from the start)"
exec .venv/bin/pio device monitor -d firmware -p "$PORT"
