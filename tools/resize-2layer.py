#!/usr/bin/env python3
"""
Resize 2-layer PCB to target dimensions. Repositions all components,
removes old traces/vias/zones, updates board outline, adds GND pour.

Run with KiCad Python:
  KICAD_PYTHON tools/resize-2layer.py <width> <height>

Example:
  /Applications/KiCad/KiCad.app/.../python3 tools/resize-2layer.py 120 100
"""
import pcbnew
import sys

PCB_PATH = 'hardware/E-IEC-Nano-SRAM/2-layer/64korppu-E.kicad_pcb'

# Component body sizes from pcb-validate.py (dx_neg, dx_pos, dy_neg, dy_pos)
# Used for reference in designing layouts
BODIES = {
    'J3':  (-5, 9, -4.5, 8),      # BarrelJack
    'J1':  (-8, 8, -8, 8),         # DIN-6
    'U1':  (-2, 17.5, -1.5, 36.5), # Nano 2x15 (wider — 2 pin rows at 15.24mm)
    'U2':  (-1.5, 9, -1.5, 9),     # SRAM DIP-8
    'U3':  (-1.5, 9, -1.5, 20),    # 74HC595 DIP-16
    'J2':  (-4.5, 4.5, -2, 42),    # IDC 2x17
    'R':   (-1.5, 11.5, -1.5, 1.5),# Axial resistor
    'D1':  (-2, 2, -2, 2),         # LED
    'C_d': (-1, 6, -2.5, 2.5),     # Disc capacitor
    'C4':  (-1, 4, -2.5, 2.5),     # Radial electrolytic
    'H':   (-2, 2, -2, 2),         # Mounting hole
}


def compute_layout(W, H):
    """Compute component positions for given board dimensions.

    Board goes from (100, 100) to (100+W, 100+H).
    Returns dict of {ref: (x, y, rotation)}.
    """
    left = 100
    top = 100
    right = 100 + W
    bottom = 100 + H
    M = 3  # mounting hole margin from edge

    positions = {}

    # Mounting holes at corners
    positions['H1'] = (left + M, top + M, 0)
    positions['H2'] = (right - M, top + M, 0)
    positions['H3'] = (left + M, bottom - M, 0)
    positions['H4'] = (right - M, bottom - M, 0)

    # IDC (tallest: -2 to +42 = 44mm vertical) — right edge
    # Position so center is near right edge
    j2_x = right - 8
    j2_y = top + max(5, (H - 44) / 2)  # Centered vertically if possible
    # Ensure bottom fits: j2_y + 42 must be <= bottom - 3
    if j2_y + 42 > bottom - 3:
        j2_y = bottom - 45
    if j2_y < top + 3:
        return None  # Board too small for IDC
    positions['J2'] = (j2_x, j2_y, 0)

    # Barrel jack — top edge, left area
    # Body: -5 to +9 wide, -4.5 to +8 tall
    j3_x = left + 12
    j3_y = top + 5
    positions['J3'] = (j3_x, j3_y, 0)

    # Electrolytic cap near barrel jack
    c4_x = j3_x + 18
    c4_y = j3_y
    positions['C4'] = (c4_x, c4_y, 0)

    # Arduino Nano — center-left area
    # Body: -2 to +17.5 wide (15.24mm pin spacing + margins), -1.5 to +36.5 tall
    # Need 38mm vertical clearance
    nano_x = left + W * 0.35
    nano_y = top + max(12, (H - 38) / 2 - 2)
    # Ensure fits: nano_y + 36.5 must be <= bottom - 5
    if nano_y + 36.5 > bottom - 5:
        nano_y = bottom - 41.5
    positions['U1'] = (nano_x, nano_y, 0)

    # DIN-6 IEC — left edge, below Nano area
    # Body: -8 to +8 = 16mm both ways
    j1_x = left + 10
    j1_y = bottom - 14  # extra margin from H3 mounting hole at corner
    # If board is small, put DIN-6 further up
    if j1_y - 8 < nano_y + 36.5 + 2:
        j1_y = nano_y + 36.5 + 10
    if j1_y + 8 > bottom - M - 4:  # clearance from H3 mounting hole body
        j1_y = bottom - M - 12
    positions['J1'] = (j1_x, j1_y, 0)

    # IEC series resistors R1-R4 (between DIN-6 and Nano)
    # Vertical stack on left side
    r_base_x = left + 8
    r_base_y = nano_y + 2
    r_spacing = min(7, (j1_y - 10 - r_base_y) / 7) if j1_y > r_base_y + 50 else 5
    for i, r in enumerate(['R1', 'R2', 'R3', 'R4']):
        positions[r] = (r_base_x, r_base_y + i * r_spacing, 0)

    # IEC pull-up resistors R5-R7 (next to R1-R4 or below)
    # Place normally first, then compress if overlapping J1
    r5_y = r_base_y + 4 * r_spacing
    r6_y = r_base_y + 5 * r_spacing
    r7_y = r_base_y + 6 * r_spacing
    r7_max = j1_y - 8 - 3  # max y for R7 (clearance from J1 body top)
    if r7_y > r7_max:
        # Compress R5-R7 stack to fit above J1
        r567_spacing = min(r_spacing, 4.0)
        r7_y = r7_max
        r6_y = r7_max - r567_spacing
        r5_y = r7_max - 2 * r567_spacing
    positions['R5'] = (r_base_x, r5_y, 0)
    positions['R6'] = (r_base_x, r6_y, 0)
    positions['R7'] = (r_base_x, r7_y, 0)

    # SRAM DIP-8 — below Nano, left side
    # Body: -1.5 to +9 wide, -1.5 to +9 tall
    u2_x = nano_x - 2
    u2_y = nano_y + 40  # +40 to leave 2mm gap after Nano body end (38mm)
    if u2_y + 9 > bottom - 5:
        u2_y = bottom - 14
    positions['U2'] = (u2_x, u2_y, 0)

    # 74HC595 DIP-16 — below Nano, right side
    # Body: -1.5 to +9 wide, -1.5 to +20 tall
    u3_x = nano_x + 12
    u3_y = nano_y + 32
    if u3_y + 20 > bottom - 5:
        u3_y = bottom - 25
    positions['U3'] = (u3_x, u3_y, 0)

    # Floppy pull-up resistors R9-R12 (between Nano and IDC)
    r9_x = nano_x + 20
    r9_y = nano_y + 5
    r9_spacing = min(7, max(3.5, (j2_y + 35 - r9_y) / 4))
    for i, r in enumerate(['R9', 'R10', 'R11', 'R12']):
        positions[r] = (r9_x, r9_y + i * r9_spacing, 0)

    # LED + resistor — near barrel jack, top area
    # Position between barrel jack and electrolytic cap
    d1_x = j3_x + 12
    d1_y = top + 7
    r8_x = d1_x + 6
    r8_y = d1_y
    # If overlapping C4, shift right
    if abs(d1_x - c4_x) < 8:
        d1_x = c4_x + 8
        r8_x = d1_x + 6
    positions['D1'] = (d1_x, d1_y, 0)
    positions['R8'] = (r8_x, r8_y, 0)

    # Bypass caps near ICs
    # C1: SRAM bypass — try left of U2, fallback between R9-R12 and IDC
    j1_right = j1_x + 8 + 2  # J1 body right edge + clearance
    u2_left = u2_x - 1.5 - 1  # U2 body left edge - clearance
    c1_width = 7  # body width (-1 to +6)
    if u2_left - j1_right >= c1_width:
        # Fits between J1 and U2
        c1_x = j1_right + 1
        c1_y = u2_y + 2
    else:
        # Not enough space — put between R9-R12 and IDC, below R9-R12
        c1_x = j2_x - 12
        c1_y = r9_y + 3 * r9_spacing + 5  # below last resistor R12 + clearance
    positions['C1'] = (c1_x, c1_y, 0)

    # C2: 595 bypass — right of U3
    c2_x = u3_x + 12
    c2_y = u3_y + 2
    positions['C2'] = (c2_x, c2_y, 0)

    # C3: Nano bypass — right of Nano, above R9 area
    c3_x = nano_x + 20
    c3_y = nano_y - 3
    if c3_y < top + 5:
        c3_y = top + 5
    # Avoid R8 body (r8_x-1.5 to r8_x+11.5)
    if c3_x - 1 < r8_x + 11.5 + 1 and abs(c3_y - r8_y) < 5:
        c3_x = r8_x + 11.5 + 2  # move right of R8
    # Fallback: between barrel jack and Nano if right side is too far
    if c3_x + 6 > right - 15:
        c3_x = max(j3_x + 9 + 2, nano_x - 2 - 8)
    positions['C3'] = (c3_x, c3_y, 0)

    # Test points — left edge on B.Cu, vertical line
    tp_x = left + 3
    tp_y_start = top + 8
    tp_spacing = 2.7  # minimum to avoid body collisions (1.5mm pads)
    # Need 14 TPs * spacing to fit in board height
    max_tp_height = H - 16  # margins top+bottom
    if 14 * tp_spacing > max_tp_height:
        tp_spacing = max_tp_height / 14
    for i in range(14):
        tp_name = f'TP{i+1}'
        positions[tp_name] = (tp_x, tp_y_start + i * tp_spacing, 0)

    return positions


def clamp_texts_to_board(board, left, top, right, bottom, margin=2):
    """Ensure all texts (footprint refs/values + standalone) stay within board area.

    For footprint texts: flip offset to opposite side, then clamp.
    For standalone PCB_TEXT: clamp directly to nearest edge.
    """
    fixed = 0

    # Footprint texts (Reference, Value)
    for fp in board.GetFootprints():
        fp_pos = fp.GetPosition()
        fp_x = pcbnew.ToMM(fp_pos.x)
        fp_y = pcbnew.ToMM(fp_pos.y)

        for text_item in [fp.Reference(), fp.Value()]:
            pos = text_item.GetPosition()
            tx = pcbnew.ToMM(pos.x)
            ty = pcbnew.ToMM(pos.y)

            dx = tx - fp_x
            dy = ty - fp_y

            needs_fix = False
            if tx < left + margin:
                dx = abs(dx)
                needs_fix = True
            elif tx > right - margin:
                dx = -abs(dx)
                needs_fix = True
            if ty < top + margin:
                dy = abs(dy)
                needs_fix = True
            elif ty > bottom - margin:
                dy = -abs(dy)
                needs_fix = True

            if needs_fix:
                new_x = max(left + margin, min(right - margin, fp_x + dx))
                new_y = max(top + margin, min(bottom - margin, fp_y + dy))
                text_item.SetPosition(pcbnew.VECTOR2I(
                    pcbnew.FromMM(new_x), pcbnew.FromMM(new_y)))
                fixed += 1

    # Standalone PCB_TEXT items (board labels etc.)
    for d in board.GetDrawings():
        if not hasattr(d, 'GetText'):
            continue
        pos = d.GetPosition()
        tx = pcbnew.ToMM(pos.x)
        ty = pcbnew.ToMM(pos.y)
        if tx < left + margin or tx > right - margin or \
           ty < top + margin or ty > bottom - margin:
            new_x = max(left + margin, min(right - margin, tx))
            new_y = max(top + margin, min(bottom - margin, ty))
            d.SetPosition(pcbnew.VECTOR2I(
                pcbnew.FromMM(new_x), pcbnew.FromMM(new_y)))
            fixed += 1

    return fixed


def add_zone(board, net_code, layer, x1, y1, x2, y2):
    """Add a copper zone (pour)."""
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


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <width_mm> <height_mm>")
        sys.exit(1)

    W = int(sys.argv[1])
    H = int(sys.argv[2])

    print(f"Target board size: {W} x {H} mm")

    # Compute layout
    positions = compute_layout(W, H)
    if positions is None:
        print(f"ERROR: Board {W}x{H}mm is too small for IDC connector!")
        sys.exit(1)

    # Load PCB
    board = pcbnew.LoadBoard(PCB_PATH)

    # 1. Remove all tracks (traces + vias)
    tracks = list(board.GetTracks())
    for t in tracks:
        board.Remove(t)
    print(f"Removed {len(tracks)} tracks/vias")

    # 2. Remove all zones
    zones = list(board.Zones())
    for z in zones:
        board.Remove(z)
    print(f"Removed {len(zones)} zones")

    # 3. Move footprints
    moved = 0
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref in positions:
            x, y, rot = positions[ref]
            fp.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
            fp.SetOrientation(pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T))
            moved += 1
    print(f"Moved {moved} footprints")

    # 4. Update board outline
    removed_edge = 0
    for d in list(board.GetDrawings()):
        if d.GetLayer() == pcbnew.Edge_Cuts:
            board.Remove(d)
            removed_edge += 1
    print(f"Removed {removed_edge} Edge.Cuts drawings")

    rect = pcbnew.PCB_SHAPE(board)
    rect.SetShape(pcbnew.SHAPE_T_RECT)
    rect.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(100), pcbnew.FromMM(100)))
    rect.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(100 + W), pcbnew.FromMM(100 + H)))
    rect.SetLayer(pcbnew.Edge_Cuts)
    rect.SetWidth(pcbnew.FromMM(0.05))
    board.Add(rect)
    print(f"Set board outline: {W}x{H}mm")

    # 5. Move mounting hole refs to F.Fab to avoid silk_over_copper DRC
    #    Keep TP refs on B.SilkS (correct for B.Cu components)
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref.startswith('H'):
            fp.Reference().SetLayer(pcbnew.F_Fab)

    # 6. Add/update board label on F.SilkS
    BOARD_LABEL = "karski by\ndesign 2026\nrev 001"
    # Remove existing board label if present
    for d in list(board.GetDrawings()):
        if hasattr(d, 'GetText') and d.GetText() == BOARD_LABEL:
            board.Remove(d)
    label = pcbnew.PCB_TEXT(board)
    label.SetText(BOARD_LABEL)
    label.SetLayer(pcbnew.F_SilkS)
    label.SetPosition(pcbnew.VECTOR2I(
        pcbnew.FromMM(100 + 18), pcbnew.FromMM(100 + 13.5)))
    label.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(1.5), pcbnew.FromMM(1.5)))
    label.SetTextThickness(pcbnew.FromMM(0.3))
    label.SetBold(True)
    label.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    label.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_BOTTOM)
    board.Add(label)
    print(f"Added board label on F.Cu")

    # 7. Clamp all texts inside board area
    fixed = clamp_texts_to_board(board, 100, 100, 100 + W, 100 + H)
    if fixed:
        print(f"Fixed {fixed} texts outside board area")

    # 8. Add GND zone on B.Cu
    add_zone(board, 1, pcbnew.B_Cu, 100, 100, 100 + W, 100 + H)
    print("Added GND zone on B.Cu")

    # 9. Save
    board.Save(PCB_PATH)
    print(f"\nSaved {W}x{H}mm 2-layer PCB: {PCB_PATH}")


if __name__ == '__main__':
    main()
