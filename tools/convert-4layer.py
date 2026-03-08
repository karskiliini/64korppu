#!/usr/bin/env python3
"""
Convert 64korppu-E PCB from 2-layer 140x120mm to 4-layer 70x60mm.
Run with KiCad Python.
"""
import pcbnew

PCB_PATH = 'hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb'
board = pcbnew.LoadBoard(PCB_PATH)

# 1. Set 4 copper layers
board.SetCopperLayerCount(4)
print(f"Copper layers: {board.GetCopperLayerCount()}")

# 2. Remove all tracks (traces + vias)
tracks = list(board.GetTracks())
for t in tracks:
    board.Remove(t)
print(f"Removed {len(tracks)} tracks")

# 3. Remove all zones (will recreate)
zones = list(board.Zones())
for z in zones:
    board.Remove(z)
print(f"Removed {len(zones)} zones")

# 4. Component new positions (mm) — compact 70x60mm layout
positions = {
    # Mounting holes (3mm inset from corners)
    'H1': (103, 103, 0),
    'H2': (167, 103, 0),
    'H3': (103, 157, 0),
    'H4': (167, 157, 0),
    # Power input (top)
    'J3': (112, 103, 0),
    'C4': (125, 103, 0),
    # IEC series resistors (left column, 5mm spacing)
    'R1': (103, 110, 0),
    'R2': (103, 115, 0),
    'R3': (103, 120, 0),
    'R4': (103, 125, 0),
    # IEC pull-up resistors (left column, continued)
    'R5': (103, 130, 0),
    'R6': (103, 135, 0),
    'R7': (103, 140, 0),
    # DIN-6 IEC connector (left, below resistors)
    'J1': (110, 150, 0),
    # Arduino Nano (center)
    'U1': (135, 112, 0),
    # Floppy pull-up resistors (between Nano and IDC)
    'R9':  (148, 115, 0),
    'R10': (148, 120, 0),
    'R11': (148, 125, 0),
    'R12': (148, 130, 0),
    # Floppy IDC connector (right edge)
    'J2': (164, 108, 0),
    # ICs (bottom center area)
    'U2': (122, 147, 0),
    'U3': (148, 137, 0),
    # LED + resistor
    'D1': (104, 152, 0),
    'R8': (109, 152, 0),
    # Bypass caps near ICs
    'C1': (131, 150, 0),
    'C2': (122, 157, 0),
    'C3': (148, 157, 0),
}

moved = 0
for fp in board.GetFootprints():
    ref = fp.GetReference()
    if ref in positions:
        x, y, rot = positions[ref]
        fp.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
        fp.SetOrientation(pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T))
        moved += 1
print(f"Moved {moved} footprints")

# 5. Update board outline — remove old Edge.Cuts drawings
removed_edge = 0
for d in list(board.GetDrawings()):
    if d.GetLayer() == pcbnew.Edge_Cuts:
        board.Remove(d)
        removed_edge += 1
print(f"Removed {removed_edge} Edge.Cuts drawings")

# Draw new 70x60mm outline as gr_rect
rect = pcbnew.PCB_SHAPE(board)
rect.SetShape(pcbnew.SHAPE_T_RECT)
rect.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(100), pcbnew.FromMM(100)))
rect.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(170), pcbnew.FromMM(160)))
rect.SetLayer(pcbnew.Edge_Cuts)
rect.SetWidth(pcbnew.FromMM(0.05))
board.Add(rect)
print("Added 70x60mm board outline")

# 6. Add zones
def add_zone(board, net_code, layer, x1, y1, x2, y2):
    zone = pcbnew.ZONE(board)
    net_info = board.GetNetInfo()
    net = net_info.GetNetItem(net_code)
    zone.SetNet(net)
    zone.SetLayer(layer)
    outline = zone.Outline()
    outline.NewOutline()
    outline.Append(pcbnew.FromMM(x1), pcbnew.FromMM(y1))
    outline.Append(pcbnew.FromMM(x2), pcbnew.FromMM(y1))
    outline.Append(pcbnew.FromMM(x2), pcbnew.FromMM(y2))
    outline.Append(pcbnew.FromMM(x1), pcbnew.FromMM(y2))
    zone.SetMinThickness(pcbnew.FromMM(0.25))
    zone.SetThermalReliefGap(pcbnew.FromMM(0.5))
    zone.SetThermalReliefSpokeWidth(pcbnew.FromMM(0.5))
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    board.Add(zone)
    return zone

# GND plane on In1.Cu
add_zone(board, 1, pcbnew.In1_Cu, 100, 100, 170, 160)
print("Added GND zone on In1.Cu")

# +5V plane on In2.Cu
add_zone(board, 2, pcbnew.In2_Cu, 100, 100, 170, 160)
print("Added +5V zone on In2.Cu")

# GND pour on B.Cu (fills empty areas)
add_zone(board, 1, pcbnew.B_Cu, 100, 100, 170, 160)
print("Added GND zone on B.Cu")

# 7. Save
board.Save(PCB_PATH)
print(f"\nSaved 4-layer 70x60mm PCB: {PCB_PATH}")
