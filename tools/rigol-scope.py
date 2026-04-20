#!/usr/bin/env python3
"""
Rigol DS1054Z oscilloscope control via USB-TMC.

Usage:
  ./tools/scope-env/bin/python3 tools/rigol-scope.py <command> [args]

Commands:
  find                         Find and list connected instruments
  screenshot [filename.png]    Save screen capture
  waveform <ch> [filename.csv] Dump waveform data (ch: 1-4)

  ch <n> on|off                Enable/disable channel
  ch <n> scale <V/div>         Set vertical scale (e.g. 1.0, 0.5, 0.1)
  ch <n> offset <V>            Set vertical offset
  ch <n> probe <1|10|100>      Set probe attenuation
  ch <n> coupling <DC|AC|GND>  Set input coupling

  timebase <s/div>             Set horizontal timebase (e.g. 0.001, 0.0001)
  trig <ch> <edge> <level>     Set edge trigger (edge: rise|fall, level in V)
  trig mode <auto|normal|single> Set trigger mode
  trig status                  Show trigger status

  run                          Start acquisition
  stop                         Stop acquisition
  single                       Single trigger
  auto                         Auto-scale

  status                       Show current scope settings

Examples:
  rigol-scope.py ch 1 scale 2.0
  rigol-scope.py ch 2 on
  rigol-scope.py trig 1 rise 1.5
  rigol-scope.py timebase 0.0001
  rigol-scope.py screenshot capture.png
  rigol-scope.py waveform 1 ch1_data.csv
"""

import sys
import time
import struct
import pyvisa


def find_scope():
    rm = pyvisa.ResourceManager('@py')
    resources = rm.list_resources()
    for r in resources:
        if 'USB' in r or 'TCPIP' in r:
            try:
                inst = rm.open_resource(r)
                idn = inst.query('*IDN?').strip()
                if 'RIGOL' in idn.upper():
                    return inst, idn
                inst.close()
            except Exception:
                pass
    return None, None


def connect():
    scope, idn = find_scope()
    if scope is None:
        print("ERROR: Rigol scope not found. Check USB connection.")
        sys.exit(1)
    scope.timeout = 10000
    return scope, idn


def cmd_find():
    rm = pyvisa.ResourceManager('@py')
    resources = rm.list_resources()
    if not resources:
        print("No instruments found.")
        return
    for r in resources:
        try:
            inst = rm.open_resource(r)
            idn = inst.query('*IDN?').strip()
            print(f"  {r}")
            print(f"    {idn}")
            inst.close()
        except Exception as e:
            print(f"  {r} (error: {e})")


def cmd_screenshot(scope, filename):
    scope.write(':DISP:DATA? ON,OFF,PNG')
    time.sleep(0.5)
    raw = scope.read_raw()
    # TMC header: #NXXXXXXX where N is number of digits
    if raw[0:1] == b'#':
        n = int(chr(raw[1]))
        data_len = int(raw[2:2+n])
        img_data = raw[2+n:2+n+data_len]
    else:
        img_data = raw
    with open(filename, 'wb') as f:
        f.write(img_data)
    print(f"Screenshot saved: {filename} ({len(img_data)} bytes)")


def cmd_waveform(scope, channel, filename):
    ch = f'CHAN{channel}'
    scope.write(f':WAV:SOUR {ch}')
    scope.write(':WAV:MODE NORM')
    scope.write(':WAV:FORM BYTE')

    preamble = scope.query(':WAV:PRE?').strip().split(',')
    points = int(preamble[2])
    x_inc = float(preamble[4])
    x_orig = float(preamble[5])
    x_ref = int(float(preamble[6]))
    y_inc = float(preamble[7])
    y_orig = int(float(preamble[8]))
    y_ref = int(float(preamble[9]))

    scope.write(':WAV:DATA?')
    raw = scope.read_raw()
    # Parse TMC header
    if raw[0:1] == b'#':
        n = int(chr(raw[1]))
        data_start = 2 + n
        data = raw[data_start:]
    else:
        data = raw

    # Remove trailing newline if present
    if data and data[-1:] == b'\n':
        data = data[:-1]

    with open(filename, 'w') as f:
        f.write("time_s,voltage_V\n")
        for i, byte_val in enumerate(data):
            t = (i - x_ref) * x_inc + x_orig
            v = (byte_val - y_ref - y_orig) * y_inc
            f.write(f"{t:.9e},{v:.6f}\n")

    print(f"Waveform CH{channel}: {len(data)} points saved to {filename}")
    print(f"  Time/div: {x_inc * points / 12:.6g}s  Y scale: {y_inc * 25:.4g}V/div")


def cmd_channel(scope, args):
    if len(args) < 2:
        print("Usage: ch <1-4> on|off|scale|offset|probe|coupling <value>")
        return
    ch = int(args[0])
    sub = args[1].lower()
    chan = f':CHAN{ch}'

    if sub == 'on':
        scope.write(f'{chan}:DISP ON')
        print(f"CH{ch} enabled")
    elif sub == 'off':
        scope.write(f'{chan}:DISP OFF')
        print(f"CH{ch} disabled")
    elif sub == 'scale' and len(args) >= 3:
        scope.write(f'{chan}:SCAL {args[2]}')
        print(f"CH{ch} scale: {args[2]} V/div")
    elif sub == 'offset' and len(args) >= 3:
        scope.write(f'{chan}:OFFS {args[2]}')
        print(f"CH{ch} offset: {args[2]} V")
    elif sub == 'probe' and len(args) >= 3:
        scope.write(f'{chan}:PROB {args[2]}')
        print(f"CH{ch} probe: {args[2]}x")
    elif sub == 'coupling' and len(args) >= 3:
        scope.write(f'{chan}:COUP {args[2].upper()}')
        print(f"CH{ch} coupling: {args[2].upper()}")
    else:
        print(f"Unknown channel command: {sub}")


def cmd_timebase(scope, value):
    scope.write(f':TIM:MAIN:SCAL {value}')
    print(f"Timebase: {value} s/div")


def cmd_trigger(scope, args):
    if len(args) < 1:
        print("Usage: trig <ch> <rise|fall> <level> | trig mode <auto|normal|single> | trig status")
        return

    if args[0] == 'mode' and len(args) >= 2:
        mode = args[1].upper()
        if mode == 'SINGLE':
            scope.write(':SING')
            print("Trigger: single shot")
        else:
            scope.write(f':TRIG:SWE {mode}')
            print(f"Trigger mode: {mode}")
        return

    if args[0] == 'status':
        status = scope.query(':TRIG:STAT?').strip()
        src = scope.query(':TRIG:EDGE:SOUR?').strip()
        slope = scope.query(':TRIG:EDGE:SLOP?').strip()
        level = scope.query(':TRIG:EDGE:LEV?').strip()
        mode = scope.query(':TRIG:SWE?').strip()
        print(f"Trigger: {src} {slope} @ {level}V  mode={mode}  status={status}")
        return

    if len(args) >= 3:
        ch = f'CHAN{args[0]}'
        edge = 'POS' if args[1].lower() in ('rise', 'pos', 'rising') else 'NEG'
        level = args[2]
        scope.write(f':TRIG:EDGE:SOUR {ch}')
        scope.write(f':TRIG:EDGE:SLOP {edge}')
        scope.write(f':TRIG:EDGE:LEV {level}')
        print(f"Trigger: CH{args[0]} {edge} edge @ {level}V")
    else:
        print("Usage: trig <ch> <rise|fall> <level>")


def cmd_status(scope):
    print("=== Scope Status ===")
    tb = scope.query(':TIM:MAIN:SCAL?').strip()
    print(f"Timebase: {float(tb):.6g} s/div")

    for ch in range(1, 5):
        disp = scope.query(f':CHAN{ch}:DISP?').strip()
        if disp == '1' or disp == 'ON':
            scale = scope.query(f':CHAN{ch}:SCAL?').strip()
            offs = scope.query(f':CHAN{ch}:OFFS?').strip()
            coup = scope.query(f':CHAN{ch}:COUP?').strip()
            probe = scope.query(f':CHAN{ch}:PROB?').strip()
            print(f"CH{ch}: {float(scale):.4g}V/div  offset={float(offs):.4g}V  {coup}  probe={probe}x")
        else:
            print(f"CH{ch}: OFF")

    src = scope.query(':TRIG:EDGE:SOUR?').strip()
    slope = scope.query(':TRIG:EDGE:SLOP?').strip()
    level = scope.query(':TRIG:EDGE:LEV?').strip()
    mode = scope.query(':TRIG:SWE?').strip()
    status = scope.query(':TRIG:STAT?').strip()
    print(f"Trigger: {src} {slope} @ {float(level):.4g}V  mode={mode}  status={status}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return

    cmd = sys.argv[1].lower()

    if cmd == 'find':
        cmd_find()
        return

    scope, idn = connect()

    if cmd == 'screenshot':
        fn = sys.argv[2] if len(sys.argv) >= 3 else 'scope.png'
        cmd_screenshot(scope, fn)
    elif cmd == 'waveform':
        ch = sys.argv[2] if len(sys.argv) >= 3 else '1'
        fn = sys.argv[3] if len(sys.argv) >= 4 else f'ch{ch}_waveform.csv'
        cmd_waveform(scope, ch, fn)
    elif cmd == 'ch':
        cmd_channel(scope, sys.argv[2:])
    elif cmd == 'timebase':
        cmd_timebase(scope, sys.argv[2])
    elif cmd == 'trig':
        cmd_trigger(scope, sys.argv[2:])
    elif cmd == 'run':
        scope.write(':RUN')
        print("Acquisition running")
    elif cmd == 'stop':
        scope.write(':STOP')
        print("Acquisition stopped")
    elif cmd == 'single':
        scope.write(':SING')
        print("Single trigger armed")
    elif cmd == 'auto':
        scope.write(':AUT')
        print("Auto-scale")
    elif cmd == 'status':
        cmd_status(scope)
    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)

    scope.close()


if __name__ == '__main__':
    main()
