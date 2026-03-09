# KiCad PCB — Vaihtoehto E Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Generate a complete KiCad 8 project for vaihtoehto E (Arduino Nano + 23LC256 + 74HC595) PCB, ready for Freerouting autorouting and Gerber export.

**Architecture:** Three KiCad files generated as S-expressions/JSON: `.kicad_pro` (project config), `.kicad_sch` (schematic with all symbols, wires, and nets), `.kicad_pcb` (board outline, footprint placement, net assignments, GND zone). A custom DIN-6 footprint is created since KiCad's standard library lacks one. Tracks are left to Freerouting.

**Tech Stack:** KiCad 8 file formats (S-expression for .sch/.pcb, JSON for .pro), standard KiCad libraries, custom footprint for DIN-6.

**Reference:** `docs/plans/2026-03-08-kicad-pcb-design.md` and `docs/E-IEC-Nano-SRAM/piirikaavio.md`

---

### Task 1: Create project directory and .kicad_pro

**Files:**
- Create: `hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pro`

**Step 1: Create directory**

```bash
mkdir -p hardware/E-IEC-Nano-SRAM
```

**Step 2: Write .kicad_pro file**

JSON project file with default KiCad 8 settings. Key sections: board design_settings (trace width 0.25mm, clearance 0.2mm, via size 0.6mm/0.3mm drill), 2-layer stackup, net classes.

**Step 3: Verify valid JSON**

```bash
python3 -c "import json; json.load(open('hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pro'))"
```
Expected: No error.

**Step 4: Commit**

```bash
git add hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pro
git commit -m "feat(hw): add KiCad project file for option E"
```

---

### Task 2: Create custom DIN-6 footprint

**Files:**
- Create: `hardware/E-IEC-Nano-SRAM/64korppu-E.pretty/DIN-6_IEC_Vertical.kicad_mod`

KiCad standard library has no DIN-6 connector footprint.
Create one based on standard DIN-6 pin circular layout:
- 6 through-hole pins on 7mm diameter circle (262° arc, 45° apart)
- Pin numbering per DIN standard: 1(bottom-left), 2(bottom-right), 3(top-right), 4(top-left), 5(center-right), 6(center-left)
- Shield/mounting pins at ±6mm
- Courtyard and silkscreen outline

**Step 1: Create footprint library directory**

```bash
mkdir -p hardware/E-IEC-Nano-SRAM/64korppu-E.pretty
```

**Step 2: Write .kicad_mod footprint file**

S-expression footprint with:
- 6 THT pads (1.5mm drill, 2.0mm pad) at DIN-6 standard positions
- 2 mounting/shield pads at ±6mm
- Silkscreen circle and pin 1 marker
- Fab layer outline
- Courtyard (15mm × 15mm)

**Step 3: Verify S-expression syntax**

```bash
python3 -c "
with open('hardware/E-IEC-Nano-SRAM/64korppu-E.pretty/DIN-6_IEC_Vertical.kicad_mod') as f:
    content = f.read()
    assert content.startswith('(footprint'), 'Not a valid footprint file'
    assert content.count('(pad') == 8, 'Expected 8 pads (6 signal + 2 shield)'
print('OK')
"
```

**Step 4: Commit**

```bash
git add hardware/E-IEC-Nano-SRAM/64korppu-E.pretty/
git commit -m "feat(hw): add custom DIN-6 IEC footprint"
```

---

### Task 3: Create schematic — lib_symbols section

**Files:**
- Create: `hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_sch`

The schematic requires embedded copies of all library symbols used.
This task creates the file with header and all lib_symbols.

**Symbols needed:**

| Ref | Symbol | Pins |
|-----|--------|------|
| U1 | Arduino Nano module | 30 pins (2×15 headers: D0-D13, A0-A7, VIN, GND, RST, 5V, 3V3, AREF) |
| U2 | 23LC256 (generic 8-pin SPI SRAM) | 8 pins: /CS, SO, NC, VSS, SI, SCK, /HOLD, VCC |
| U3 | 74HC595 | 16 pins: QB,QC,QD,QE,QF,QG,QH,GND,QH',SRCLK,RCLK,/OE,SER,QA,/CLR,VCC |
| J1 | DIN-6 connector | 6 pins |
| J2 | 2×17 IDC header | 34 pins |
| J3 | Barrel jack | 3 pins (tip, ring, sleeve) |
| R1-R12 | Resistor | 2 pins |
| C1-C3 | Capacitor | 2 pins |
| C4 | Capacitor polarized | 2 pins |
| D1 | LED | 2 pins (A, K) |

Use standard KiCad lib symbols for all except Arduino Nano (custom symbol with 2×15 pin headers) and 23LC256 (custom 8-pin IC).

**Step 1: Write schematic header and lib_symbols**

Create file with:
- `(version 20231120)` header
- `(paper "A3")` — larger paper for readable schematic
- All symbol definitions embedded in `(lib_symbols ...)`
- No placed components yet (those come in Task 4)

**Step 2: Verify S-expression syntax**

```bash
python3 -c "
with open('hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_sch') as f:
    content = f.read()
    assert '(kicad_sch' in content
    assert '(lib_symbols' in content
print('Symbols:', content.count('(symbol \"'))
"
```

**Step 3: Commit**

```bash
git add hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_sch
git commit -m "feat(hw): add schematic with library symbols"
```

---

### Task 4: Place components and wire schematic

**Files:**
- Modify: `hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_sch`

Place all component instances and draw wires.
Use the netlist from `docs/plans/2026-03-08-kicad-pcb-design.md`.

**Layout on schematic sheet (A3, coordinates in mm):**

```
  Left side (x=30):    Center (x=130):     Right side (x=230):
  J1 DIN-6 (IEC)       U1 Arduino Nano     J2 34-pin IDC (floppy)
  R1-R4 (1k series)    U2 23LC256          R9-R12 (10k pull-ups)
  R5-R7 (4.7k pullup)  U3 74HC595

  Top center (x=130, y=30):
  J3 DC jack
  C4 bulk cap

  Bottom (y=200):
  C1-C3 bypass caps
  D1 LED + R8
```

**Step 1: Add all symbol instances with positions**

Each symbol block includes:
- `(lib_id ...)`, `(at x y angle)`, `(uuid ...)`
- Properties: Reference, Value, Footprint
- Pin UUIDs for wire connections

**Step 2: Add power symbols**

Place `+5V` and `GND` power symbols at all power connections.

**Step 3: Draw all wires**

Connect pins per netlist using `(wire (pts (xy x1 y1) (xy x2 y2)))`.
Use `(junction ...)` where wires cross/join.

Add `(net_label ...)` for named nets: SPI_MOSI, SPI_MISO, SPI_SCK,
IEC_ATN, IEC_CLK, IEC_DATA, IEC_RESET, LATCH_595, etc.

**Step 4: Add sheet_instances footer**

```
(sheet_instances (path "/" (page "1")))
```

**Step 5: Verify**

```bash
python3 -c "
with open('hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_sch') as f:
    content = f.read()
    # Count placed symbols (not lib_symbols definitions)
    import re
    placed = len(re.findall(r'\(symbol \(lib_id', content))
    print(f'Placed components: {placed}')
    assert placed >= 20, f'Expected >=20 placed components, got {placed}'
    wires = content.count('(wire')
    print(f'Wires: {wires}')
    assert wires >= 30, f'Expected >=30 wires, got {wires}'
print('OK')
"
```

**Step 6: Commit**

```bash
git add hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_sch
git commit -m "feat(hw): complete schematic with all components and wiring"
```

---

### Task 5: Create PCB — board outline and footprints

**Files:**
- Create: `hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb`

**Step 1: Write PCB header**

- `(version 20240108)`, 2-layer setup (F.Cu + B.Cu)
- Board thickness 1.6mm
- All standard layers

**Step 2: Define nets**

All nets from schematic:
```
(net 0 "")
(net 1 "GND")
(net 2 "+5V")
(net 3 "SPI_MOSI")
(net 4 "SPI_MISO")
(net 5 "SPI_SCK")
(net 6 "SPI_CS_SRAM")
(net 7 "LATCH_595")
(net 8 "IEC_ATN")
(net 9 "IEC_CLK")
(net 10 "IEC_DATA")
(net 11 "IEC_RESET")
(net 12 "FLOPPY_WDATA")
(net 13 "FLOPPY_RDATA")
(net 14 "FLOPPY_TRK00")
(net 15 "FLOPPY_WPT")
(net 16 "FLOPPY_DSKCHG")
(net 17 "FLOPPY_DENSITY")
(net 18 "FLOPPY_MOTEA")
(net 19 "FLOPPY_DRVSEL")
(net 20 "FLOPPY_MOTOR")
(net 21 "FLOPPY_DIR")
(net 22 "FLOPPY_STEP")
(net 23 "FLOPPY_WGATE")
(net 24 "FLOPPY_SIDE1")
(net 25 "LED_ANODE")
... (resistor intermediate nets)
```

**Step 3: Draw board outline**

85mm × 55mm rectangle on Edge.Cuts, origin at (100, 100):
```
(gr_rect (start 100 100) (end 185 155)
  (stroke (width 0.05) (type solid))
  (fill none) (layer "Edge.Cuts"))
```

Add 4 mounting holes: M3 (3.2mm drill) at corners, 3mm inset.

**Step 4: Place footprints**

Coordinates based on design doc layout (origin 100,100):

| Ref | Footprint | Position (mm) | Notes |
|-----|-----------|---------------|-------|
| J3 | BarrelJack_Horizontal | (130, 102) | Top center |
| J1 | DIN-6_IEC_Vertical (custom) | (105, 125) | Left side |
| J2 | IDC-Header_2x17_P2.54mm_Vertical | (175, 125) | Right side |
| U1 | PinHeader_2x15_P2.54mm_Vertical | (142, 125) | Center |
| U2 | DIP-8_W7.62mm_Socket | (118, 135) | Left of Nano |
| U3 | DIP-16_W7.62mm_Socket | (142, 145) | Below Nano |
| R1-R4 | R_Axial P10.16mm | (110, 115) stacked | Near J1 |
| R5-R7 | R_Axial P10.16mm | (110, 120) stacked | Near J1 |
| R8 | R_Axial P10.16mm | (115, 150) | Near LED |
| R9-R12 | R_Axial P10.16mm | (168, 135) stacked | Near J2 |
| C1-C3 | C_Disc_D5.0mm | near each IC | Bypass |
| C4 | CP_Radial_D5.0mm | (138, 103) | Near J3 |
| D1 | LED_D3.0mm | (112, 150) | Bottom left |
| H1-H4 | MountingHole_3.2mm | corners | 3mm inset |

Each footprint includes all pads with net assignments.

**Step 5: Add GND zone on B.Cu**

```
(zone (net 1) (net_name "GND") (layer "B.Cu")
  (hatch edge 0.5)
  (connect_pads (clearance 0.3))
  (min_thickness 0.25)
  (fill (thermal_gap 0.5) (thermal_bridge_width 0.5))
  (polygon (pts
    (xy 100 100) (xy 185 100) (xy 185 155) (xy 100 155)
  ))
)
```

**Step 6: Verify**

```bash
python3 -c "
with open('hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb') as f:
    content = f.read()
    assert '(kicad_pcb' in content
    footprints = content.count('(footprint ')
    nets = content.count('(net ')
    print(f'Footprints: {footprints}, Nets: {nets}')
    assert footprints >= 20
    assert 'Edge.Cuts' in content
print('OK')
"
```

**Step 7: Commit**

```bash
git add hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb
git commit -m "feat(hw): add PCB with footprint placement and GND zone"
```

---

### Task 6: Add fp-lib-table for custom footprint library

**Files:**
- Create: `hardware/E-IEC-Nano-SRAM/fp-lib-table`

KiCad needs to know where to find the custom DIN-6 footprint.

**Step 1: Write fp-lib-table**

```
(fp_lib_table
  (version 7)
  (lib (name "64korppu-E")(type "KiCad")(uri "${KIPRJMOD}/64korppu-E.pretty")(options "")(descr "64korppu project footprints"))
)
```

**Step 2: Commit**

```bash
git add hardware/E-IEC-Nano-SRAM/fp-lib-table
git commit -m "feat(hw): add footprint library table for custom DIN-6"
```

---

### Task 7: Add hardware README and Freerouting instructions

**Files:**
- Create: `hardware/README.md`

**Step 1: Write README**

Document:
- Directory structure
- How to open in KiCad 8
- How to export .dsn for Freerouting
- How to import .ses back
- How to export Gerbers for JLCPCB/PCBWay

**Step 2: Commit**

```bash
git add hardware/README.md
git commit -m "docs: add hardware README with Freerouting and Gerber instructions"
```

---

### Task 8: Final verification and commit

**Step 1: Verify all files present**

```bash
ls -la hardware/E-IEC-Nano-SRAM/
# Expected:
#   64korppu-E.kicad_pro
#   64korppu-E.kicad_sch
#   64korppu-E.kicad_pcb
#   64korppu-E.pretty/DIN-6_IEC_Vertical.kicad_mod
#   fp-lib-table
```

**Step 2: Try KiCad CLI validation (if installed)**

```bash
kicad-cli sch erc --severity-all --exit-code-violations \
  hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_sch || echo "ERC issues found (expected for generated schematic)"

kicad-cli pcb drc --severity-all --exit-code-violations \
  hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb || echo "DRC issues found (expected, no tracks yet)"
```

**Step 3: Final commit if any fixes**

```bash
git add hardware/
git commit -m "feat(hw): complete KiCad project for option E PCB"
```
