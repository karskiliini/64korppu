#!/usr/bin/env python3
"""
Validoi KiCad PCB-tiedoston ennen Freerouting-reititystä.

Tarkistukset:
  1. Kaikki padit piirilevyn rajojen sisällä
  2. Ei päällekkäisiä padeja eri komponenttien välillä
  3. Ei ;; -kommentteja (KiCad 9 hylkää ne)
  4. Sulkujen tasapaino

Käyttö:
  ./tools/pcb-validate.py hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb
"""
import re
import math
import sys

MIN_PAD_CLEARANCE = 0.3  # mm, minimietäisyys padien reunojen välillä
BOARD_MARGIN = 0.5       # mm, minimi etäisyys padista levyn reunaan


def parse_pcb(filepath):
    with open(filepath) as f:
        content = f.read()
    return content


def check_comments(content):
    """Tarkista ettei ;; -kommentteja ole."""
    errors = []
    for i, line in enumerate(content.split('\n'), 1):
        if line.strip().startswith(';;'):
            errors.append(f"  Rivi {i}: ;; -kommentti (KiCad 9 hylkää)")
    return errors


def check_parens(content):
    """Tarkista sulkujen tasapaino."""
    opens = content.count('(')
    closes = content.count(')')
    if opens != closes:
        return [f"  Sulkuvirhe: ( = {opens}, ) = {closes}, ero = {opens - closes}"]
    return []


def get_board_boundary(content):
    """Lue Edge.Cuts -reunat."""
    m = re.search(
        r'\(gr_rect\s*\n\s*\(start ([\d.]+) ([\d.]+)\)\s*\n\s*\(end ([\d.]+) ([\d.]+)\)',
        content)
    if m:
        return (float(m.group(1)), float(m.group(2)),
                float(m.group(3)), float(m.group(4)))
    return None


def get_footprints_and_pads(content):
    """Parsii kaikki footprintit ja niiden padit."""
    fp_pattern = re.compile(
        r'\(footprint "([^"]+)"\s*\n\s*\(layer [^)]+\)\s*\n\s*'
        r'\(uuid [^)]+\)\s*\n\s*\(at ([\d.]+) ([\d.]+)(?:\s+([\d.]+))?')
    fp_matches = list(fp_pattern.finditer(content))

    all_pads = []
    for i, m in enumerate(fp_matches):
        fp_x = float(m.group(2))
        fp_y = float(m.group(3))
        fp_rot = float(m.group(4)) if m.group(4) else 0

        start = m.start()
        end = fp_matches[i + 1].start() if i + 1 < len(fp_matches) else len(content)
        fp_block = content[start:end]

        ref_match = re.search(r'"Reference" "([^"]+)"', fp_block)
        ref = ref_match.group(1) if ref_match else '??'

        for sec in re.split(r'(?=\(pad )', fp_block):
            if not sec.startswith('(pad'):
                continue
            pn_m = re.search(r'\(pad "([^"]+)"', sec)
            at_m = re.search(r'\(at ([-\d.]+) ([-\d.]+)', sec)
            sz_m = re.search(r'\(size ([\d.]+) ([\d.]+)\)', sec)
            net_m = re.search(r'\(net (\d+) "([^"]*)"\)', sec)
            if pn_m and at_m:
                pad_rx = float(at_m.group(1))
                pad_ry = float(at_m.group(2))
                if fp_rot != 0:
                    rad = math.radians(fp_rot)
                    rx = pad_rx * math.cos(rad) - pad_ry * math.sin(rad)
                    ry = pad_rx * math.sin(rad) + pad_ry * math.cos(rad)
                else:
                    rx, ry = pad_rx, pad_ry
                all_pads.append({
                    'ref': ref,
                    'pad': pn_m.group(1),
                    'abs_x': fp_x + rx,
                    'abs_y': fp_y + ry,
                    'size': float(sz_m.group(1)) if sz_m else 1.6,
                    'net': net_m.group(2) if net_m else '',
                })
    return all_pads


def check_boundary(pads, boundary):
    """Tarkista että kaikki padit ovat levyn sisällä."""
    if not boundary:
        return ["  Levyn reunoja (Edge.Cuts gr_rect) ei löydy!"]
    bx1, by1, bx2, by2 = boundary
    errors = []
    for p in pads:
        margin = BOARD_MARGIN
        if (p['abs_x'] < bx1 + margin or p['abs_x'] > bx2 - margin or
                p['abs_y'] < by1 + margin or p['abs_y'] > by2 - margin):
            errors.append(
                f"  {p['ref']}.{p['pad']} ({p['net']}) at "
                f"({p['abs_x']:.1f}, {p['abs_y']:.1f}) — levyn ulkopuolella "
                f"[{bx1},{by1}]-[{bx2},{by2}]")
    return errors


def check_overlaps(pads):
    """Tarkista ettei eri komponenttien padit ole päällekkäin."""
    errors = []
    for i in range(len(pads)):
        for j in range(i + 1, len(pads)):
            p1, p2 = pads[i], pads[j]
            if p1['ref'] == p2['ref']:
                continue
            dist = math.sqrt((p2['abs_x'] - p1['abs_x'])**2 +
                             (p2['abs_y'] - p1['abs_y'])**2)
            edge_dist = dist - (p1['size'] / 2 + p2['size'] / 2)
            if edge_dist < MIN_PAD_CLEARANCE:
                errors.append(
                    f"  {p1['ref']}.{p1['pad']} <-> {p2['ref']}.{p2['pad']} "
                    f"edge={edge_dist:.2f}mm (min {MIN_PAD_CLEARANCE}mm) "
                    f"[({p1['abs_x']:.1f},{p1['abs_y']:.1f}) <-> "
                    f"({p2['abs_x']:.1f},{p2['abs_y']:.1f})]")
    return errors


def main():
    if len(sys.argv) < 2:
        print(f"Käyttö: {sys.argv[0]} <pcb-tiedosto>")
        sys.exit(1)

    filepath = sys.argv[1]
    content = parse_pcb(filepath)
    ok = True

    # 1. Kommentit
    errs = check_comments(content)
    if errs:
        print("VIRHE: ;; -kommentteja löytyi:")
        for e in errs:
            print(e)
        ok = False
    else:
        print("OK: Ei ;; -kommentteja")

    # 2. Sulut
    errs = check_parens(content)
    if errs:
        print("VIRHE: Sulkuvirhe:")
        for e in errs:
            print(e)
        ok = False
    else:
        print("OK: Sulut tasapainossa")

    # 3. Piirilevyn rajat
    boundary = get_board_boundary(content)
    pads = get_footprints_and_pads(content)
    print(f"Löytyi {len(pads)} padia")

    if boundary:
        bx1, by1, bx2, by2 = boundary
        print(f"Levy: ({bx1},{by1}) - ({bx2},{by2}) = "
              f"{bx2 - bx1:.0f} x {by2 - by1:.0f} mm")

    errs = check_boundary(pads, boundary)
    if errs:
        print(f"VIRHE: {len(errs)} padia levyn ulkopuolella:")
        for e in errs[:10]:
            print(e)
        if len(errs) > 10:
            print(f"  ... ja {len(errs) - 10} muuta")
        ok = False
    else:
        print("OK: Kaikki padit levyn sisällä")

    # 4. Päällekkäisyydet
    errs = check_overlaps(pads)
    if errs:
        print(f"VIRHE: {len(errs)} pad-päällekkäisyyttä:")
        for e in errs[:10]:
            print(e)
        ok = False
    else:
        print("OK: Ei pad-päällekkäisyyksiä")

    if ok:
        print("\nVALIDOINTI OK — valmis reititykseen")
        sys.exit(0)
    else:
        print("\nVIRHEITÄ LÖYTYI — korjaa ennen reititystä")
        sys.exit(1)


if __name__ == '__main__':
    main()
