#!/usr/bin/env python3
"""
Serial logger with timestamps for Arduino Nano trace capture.

Usage:
  serial-logger.py <port> <output_file> [duration_s]

Logs serial data with microsecond timestamps until duration expires
or Ctrl-C. Default duration: 30s.
"""

import sys
import time
import serial
import signal

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    port = sys.argv[1]
    outfile = sys.argv[2]
    duration = float(sys.argv[3]) if len(sys.argv) >= 4 else 30.0

    stop = False
    def handler(sig, frame):
        nonlocal stop
        stop = True
    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)

    ser = serial.Serial(port, 9600, timeout=0.1)
    ser.reset_input_buffer()

    t0 = time.time()
    lines = []

    while not stop and (time.time() - t0) < duration:
        line = ser.readline()
        if line:
            t = time.time() - t0
            text = line.decode('ascii', errors='replace').rstrip()
            entry = f"{t:.6f} {text}"
            lines.append(entry)
            print(entry)

    ser.close()

    with open(outfile, 'w') as f:
        f.write("time_s message\n")
        for l in lines:
            f.write(l + "\n")

    print(f"\n{len(lines)} lines saved to {outfile}")

if __name__ == '__main__':
    main()
