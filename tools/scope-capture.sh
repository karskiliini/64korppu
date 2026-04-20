#!/bin/bash
# Scope capture script for floppy RDATA debugging.
# Sets up channels, triggers single capture, downloads data.
#
# Usage: ./tools/scope-capture.sh [name]
#   name: optional prefix for output files (default: "capture")

SCOPE_PY="$(dirname "$0")/scope-env/bin/python3 $(dirname "$0")/rigol-scope.py"
NAME="${1:-capture}"
TIMESTAMP=$(date +%H%M%S)
PREFIX="${NAME}_${TIMESTAMP}"

echo "=== Scope capture: ${PREFIX} ==="

# Channel setup
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

# Timebase: 5µs/div — shows several MFM bit cells
$SCOPE_PY timebase 0.000005

# Trigger: CH1 falling edge @ 2.5V, normal mode
# (first RDATA pulse after quiet gap)
$SCOPE_PY trig 1 fall 2.5
$SCOPE_PY trig mode normal

# Arm single trigger
$SCOPE_PY single
echo ""
echo "Waiting for trigger..."

# Poll trigger status until captured (STOP = triggered and done)
for i in $(seq 1 60); do
    STATUS=$($SCOPE_PY trig status 2>&1 | grep -o 'status=[A-Z]*' | cut -d= -f2)
    if [ "$STATUS" = "STOP" ]; then
        echo "Triggered!"
        break
    fi
    sleep 0.5
done

if [ "$STATUS" != "STOP" ]; then
    echo "Timeout — no trigger received."
    exit 1
fi

# Download data
echo "Downloading..."
$SCOPE_PY screenshot "${PREFIX}.png"
$SCOPE_PY waveform 1 "${PREFIX}_ch1.csv"
sleep 1
$SCOPE_PY waveform 2 "${PREFIX}_ch2.csv"

echo ""
echo "Files:"
echo "  ${PREFIX}.png"
echo "  ${PREFIX}_ch1.csv"
echo "  ${PREFIX}_ch2.csv"
