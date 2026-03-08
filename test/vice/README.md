# VICE IEC Bridge

Host-native build of 64korppu firmware for integration testing.

## Architecture

```
┌─────────────┐     TCP/6464     ┌──────────────────────┐
│ Python test  │◄───────────────►│  iec_bridge process   │
│ vice_test.py │                 │                       │
│ (simulates   │                 │  cbm_dos.c  (real)    │
│  C64 side)   │                 │  d64.c      (real)    │
│              │                 │  mock_fat12 (host fs) │
└─────────────┘                  │  mock_iec   (stubs)   │
                                 └──────────────────────┘
```

The bridge runs the **real** CBM-DOS and D64 code on the host machine.
FAT12 is mocked — D64 files are read from the host filesystem.
IEC protocol is replaced with a simple TCP binary protocol.

## Quick start

```bash
# Build
cd test
make vice/iec_bridge

# Start the bridge (scans current dir for .D64 files)
./vice/iec_bridge 6464 /path/to/d64/files/

# In another terminal, run the test client
python3 vice/vice_test.py
```

## TCP Protocol

Binary protocol, all integers are single bytes:

| Client → Server | Format |
|---|---|
| OPEN | `0x01` `sa` `len` `filename[len]` |
| CLOSE | `0x02` `sa` |
| READ | `0x03` `sa` |
| WRITE | `0x04` `sa` `byte` |
| EXEC | `0x05` `len` `command[len]` |
| QUIT | `0xFF` |

| Server → Client | Format |
|---|---|
| OK | `0x00` |
| BYTE | `0x01` `byte` `eoi` |
| NO DATA | `0x02` |
| ERROR | `0xFF` `code` `len` `msg[len]` |

## Using with VICE

For visual testing, you can use the Python client as a library
in a script that also controls VICE via its monitor interface:

```python
from vice_test import IECClient

c = IECClient()
c.mount_d64("GAME.D64")
data = c.load_file("INTRO")
# Write data to a .prg file, load in VICE...
```

Or generate .prg test files that exercise specific scenarios
and load them directly in VICE.
