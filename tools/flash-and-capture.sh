#!/bin/bash
# Flash floppy_reader firmware, capture serial trace + scope data.
#
# Usage: ./tools/flash-and-capture.sh [name]
#   name: optional prefix for output files (default: "capture")
#
# Output files:
#   <name>_HHMMSS.png          Scope screenshot
#   <name>_HHMMSS_ch1.csv      Scope CH1 waveform
#   <name>_HHMMSS_ch2.csv      Scope CH2 waveform
#   <name>_HHMMSS_trace.log    Serial trace with timestamps

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FW_DIR="$(dirname "$SCRIPT_DIR")/firmware/E-IEC-Nano-SRAM"
PYTHON="$SCRIPT_DIR/scope-env/bin/python3"
SCOPE_PY="$PYTHON $SCRIPT_DIR/rigol-scope.py"
LOGGER_PY="$PYTHON $SCRIPT_DIR/serial-logger.py"
NAME="${1:-capture}"
TIMESTAMP=$(date +%H%M%S)
PREFIX="${NAME}_${TIMESTAMP}"

# Detect Nano serial port
PORT=$(ls /dev/tty.usbserial-* 2>/dev/null | head -1)
if [ -z "$PORT" ]; then
    PORT=$(ls /dev/tty.usbmodem* 2>/dev/null | head -1)
fi
if [ -z "$PORT" ]; then
    echo "ERROR: Arduino Nano not found. Check USB."
    exit 1
fi

echo "=== Flash & Capture: ${PREFIX} ==="
echo "Nano: ${PORT}"
echo ""

# Build
echo "--- Building floppy_reader ---"
make -C "$FW_DIR" clean 2>&1 | tail -1
make -C "$FW_DIR" reader
echo ""

# Flash
echo "--- Flashing ---"
make -C "$FW_DIR" reader-flash PORT="$PORT"
echo ""

# Setup scope before Nano boots
echo "--- Scope setup ---"
$SCOPE_PY ch 1 on
$SCOPE_PY ch 1 probe 10
$SCOPE_PY ch 1 scale 2.0
$SCOPE_PY ch 1 offset 2.0
$SCOPE_PY ch 1 coupling DC
$SCOPE_PY ch 2 on
$SCOPE_PY ch 2 probe 10
$SCOPE_PY ch 2 scale 2.0
$SCOPE_PY ch 2 offset 2.0
$SCOPE_PY ch 2 coupling DC
$SCOPE_PY timebase 0.000005
$SCOPE_PY trig 1 fall 2.5
$SCOPE_PY trig mode normal
echo ""

# Start serial logger in background (captures boot + read cycles)
echo "--- Starting serial logger ---"
TRACE_FILE="${PREFIX}_trace.log"
$LOGGER_PY "$PORT" "$TRACE_FILE" 30 &
LOGGER_PID=$!
echo "Logger PID: $LOGGER_PID"
echo ""

# Wait for Nano boot + motor spin-up + first deselect/reselect cycle
echo "Waiting for Nano boot + first read cycle..."
sleep 4

# Arm single trigger — next RDATA falling edge captures
echo "--- Arming scope trigger ---"
$SCOPE_PY single
echo ""

# Wait for trigger
echo "Waiting for trigger..."
for i in $(seq 1 30); do
    STATUS=$($SCOPE_PY trig status 2>&1 | grep -o 'status=[A-Z]*' | cut -d= -f2)
    if [ "$STATUS" = "STOP" ]; then
        echo "Triggered!"
        break
    fi
    sleep 0.5
done

if [ "$STATUS" != "STOP" ]; then
    echo "WARNING: Trigger timeout"
fi

# Download scope data
echo ""
echo "--- Downloading scope data ---"
$SCOPE_PY screenshot "${PREFIX}.png"
$SCOPE_PY waveform 1 "${PREFIX}_ch1.csv"
sleep 1
$SCOPE_PY waveform 2 "${PREFIX}_ch2.csv"

# Let logger run a few more seconds then stop
sleep 2
kill $LOGGER_PID 2>/dev/null
wait $LOGGER_PID 2>/dev/null

echo ""
echo "=== Done ==="
echo "Files:"
echo "  ${PREFIX}.png           Scope screenshot"
echo "  ${PREFIX}_ch1.csv       CH1 waveform (RDATA raw)"
echo "  ${PREFIX}_ch2.csv       CH2 waveform (RDATA after 74HC14)"
echo "  ${TRACE_FILE}           Serial trace"
