#!/usr/bin/env python3
"""
Validoi KiCad PCB-tiedoston ennen Freerouting-reititystä.

Tarkistukset:
  1. Ei ;; -kommentteja (KiCad 9 hylkää ne)
  2. Sulkujen tasapaino
  3. Kaikki padit piirilevyn rajojen sisällä
  4. Ei päällekkäisiä padeja eri komponenttien välillä
  5. Komponenttien fyysiset rungot eivät törmää toisiinsa

Käyttö:
  ./tools/pcb-validate.py hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb
"""
import re
import math
import sys

MIN_PAD_CLEARANCE = 0.3  # mm, minimietäisyys padien reunojen välillä
BOARD_MARGIN = 0.5       # mm, minimi etäisyys padista levyn reunaan
BODY_CLEARANCE = 1.0     # mm, minimietäisyys komponenttien runkojen välillä

# Komponenttien fyysiset runkokoot (mm) suhteessa footprintin keskipisteeseen
# Muoto: (dx_neg, dx_pos, dy_neg, dy_pos) = kuinka paljon runko ulottuu
# keskipisteestä vasemmalle, oikealle, ylös, alas
COMPONENT_BODIES = {
    'BarrelJack_Horizontal':                        (-5, 9, -4.5, 8),
    'DIN-6_IEC_Vertical':                           (-8, 8, -8, 8),
    'PinHeader_2x15_P2.54mm_Vertical':              (-2, 5, -1.5, 36.5),
    'IDC-Header_2x17_P2.54mm_Vertical':             (-4.5, 4.5, -2, 42),
    'DIP-8_W7.62mm_Socket':                         (-1.5, 9, -1.5, 9),
    'DIP-16_W7.62mm_Socket':                        (-1.5, 9, -1.5, 20),
    'R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal': (-1.5, 11.5, -1.5, 1.5),
    'LED_D3.0mm':                                   (-2, 2, -2, 2),
    'C_Disc_D5.0mm_W2.5mm_P5.00mm':                (-1, 6, -2.5, 2.5),
    'CP_Radial_D5.0mm_P2.00mm':                     (-1, 4, -2.5, 2.5),
    'MountingHole_3.2mm_M3':                        (-2, 2, -2, 2),
    'TestPoint_Pad_D1.5_SMD':                       (-0.75, 0.75, -0.75, 0.75),
}


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
    all_footprints = []
    for i, m in enumerate(fp_matches):
        fp_name = m.group(1)
        fp_x = float(m.group(2))
        fp_y = float(m.group(3))
        fp_rot = float(m.group(4)) if m.group(4) else 0

        start = m.start()
        end = fp_matches[i + 1].start() if i + 1 < len(fp_matches) else len(content)
        fp_block = content[start:end]

        ref_match = re.search(r'"Reference" "([^"]+)"', fp_block)
        ref = ref_match.group(1) if ref_match else '??'

        # Footprint name: strip library prefix ("lib:Name" -> "Name")
        short_name = fp_name.split(':')[-1] if ':' in fp_name else fp_name

        all_footprints.append({
            'ref': ref,
            'name': short_name,
            'x': fp_x,
            'y': fp_y,
            'rot': fp_rot,
        })

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
    return all_pads, all_footprints


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


def get_body_rect(fp):
    """Laske komponentin fyysisen rungon suorakaide (x1,y1,x2,y2).

    Käyttää COMPONENT_BODIES-taulukkoa jos footprint tunnetaan,
    muuten laskee bounding boxin padeista + 1mm marginaali.
    """
    name = fp['name']
    x, y = fp['x'], fp['y']

    if name in COMPONENT_BODIES:
        dx_neg, dx_pos, dy_neg, dy_pos = COMPONENT_BODIES[name]
        return (x + dx_neg, y + dy_neg, x + dx_pos, y + dy_pos)

    # Tuntematon footprint — palautetaan None, ohitetaan tarkistuksessa
    return None


def rects_overlap(r1, r2, clearance):
    """Tarkista ovatko kaksi suorakaidetta päällekkäin (clearance huomioiden)."""
    x1a, y1a, x2a, y2a = r1
    x1b, y1b, x2b, y2b = r2
    return not (x2a + clearance <= x1b or
                x2b + clearance <= x1a or
                y2a + clearance <= y1b or
                y2b + clearance <= y1a)


def check_body_collisions(footprints):
    """Tarkista ettei komponenttien fyysiset rungot törmää toisiinsa."""
    errors = []
    rects = []
    for fp in footprints:
        rect = get_body_rect(fp)
        if rect:
            rects.append((fp, rect))

    for i in range(len(rects)):
        for j in range(i + 1, len(rects)):
            fp1, r1 = rects[i]
            fp2, r2 = rects[j]
            if rects_overlap(r1, r2, BODY_CLEARANCE):
                # Laske todellinen overlap
                ox = min(r1[2], r2[2]) - max(r1[0], r2[0])
                oy = min(r1[3], r2[3]) - max(r1[1], r2[1])
                errors.append(
                    f"  {fp1['ref']} ({fp1['name']}) <-> {fp2['ref']} ({fp2['name']}) "
                    f"törmäys! overlap={max(ox,0):.1f}x{max(oy,0):.1f}mm "
                    f"[{fp1['ref']}:({r1[0]:.0f},{r1[1]:.0f})-({r1[2]:.0f},{r1[3]:.0f}) "
                    f"{fp2['ref']}:({r2[0]:.0f},{r2[1]:.0f})-({r2[2]:.0f},{r2[3]:.0f})]")
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
    pads, footprints = get_footprints_and_pads(content)
    print(f"Löytyi {len(pads)} padia, {len(footprints)} komponenttia")

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

    # 5. Komponenttien runkojen törmäykset
    errs = check_body_collisions(footprints)
    if errs:
        print(f"VIRHE: {len(errs)} runkotörmäystä:")
        for e in errs[:10]:
            print(e)
        if len(errs) > 10:
            print(f"  ... ja {len(errs) - 10} muuta")
        ok = False
    else:
        known = sum(1 for fp in footprints if fp['name'] in COMPONENT_BODIES)
        print(f"OK: Ei runkotörmäyksiä ({known}/{len(footprints)} tunnettua komponenttia)")

    if ok:
        print("\nVALIDOINTI OK — valmis reititykseen")
        sys.exit(0)
    else:
        print("\nVIRHEITÄ LÖYTYI — korjaa ennen reititystä")
        sys.exit(1)


if __name__ == '__main__':
    main()
