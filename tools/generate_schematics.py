#!/usr/bin/env python3
"""
Generate schematic diagrams for all 64korppu alternatives.
Uses matplotlib for detailed circuit block diagrams with pinouts.
"""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, FancyArrowPatch
import os

DPI = 150
BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def draw_ic(ax, x, y, w, h, name, pins_left, pins_right,
            color='#E8F0FE', edgecolor='#4A86C8', fontsize=9,
            subtitle=None, pin_fontsize=6):
    """Draw an IC package with labeled pins on left and right sides."""
    box = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.05",
                          facecolor=color, edgecolor=edgecolor, linewidth=2)
    ax.add_patch(box)
    text_y = y + h/2 + (0.12 if subtitle else 0)
    ax.text(x + w/2, text_y, name, ha='center', va='center',
            fontsize=fontsize, fontweight='bold')
    if subtitle:
        ax.text(x + w/2, y + h/2 - 0.15, subtitle, ha='center', va='center',
                fontsize=pin_fontsize, color='#555')

    # Draw pins on left
    n_left = len(pins_left)
    for i, (pnum, pname) in enumerate(pins_left):
        py = y + h - (i + 1) * h / (n_left + 1)
        # pin dot
        ax.plot(x, py, 'o', color=edgecolor, markersize=3)
        # pin number
        ax.text(x + 0.08, py, f'{pnum}', fontsize=pin_fontsize - 1,
                va='center', ha='left', color='#888')
        # pin name
        ax.text(x - 0.08, py, pname, fontsize=pin_fontsize,
                va='center', ha='right', color='#333', family='monospace')

    # Draw pins on right
    n_right = len(pins_right)
    for i, (pnum, pname) in enumerate(pins_right):
        py = y + h - (i + 1) * h / (n_right + 1)
        ax.plot(x + w, py, 'o', color=edgecolor, markersize=3)
        ax.text(x + w - 0.08, py, f'{pnum}', fontsize=pin_fontsize - 1,
                va='center', ha='right', color='#888')
        ax.text(x + w + 0.08, py, pname, fontsize=pin_fontsize,
                va='center', ha='left', color='#333', family='monospace')


def draw_block(ax, x, y, w, h, label, color='#E8F0FE', edgecolor='#4A86C8',
               fontsize=9, sublabel=None, bold=True):
    """Draw a simple labeled block."""
    box = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.08",
                          facecolor=color, edgecolor=edgecolor, linewidth=2)
    ax.add_patch(box)
    weight = 'bold' if bold else 'normal'
    text_y = y + h/2 + (0.15 if sublabel else 0)
    ax.text(x + w/2, text_y, label, ha='center', va='center',
            fontsize=fontsize, fontweight=weight)
    if sublabel:
        ax.text(x + w/2, y + h/2 - 0.18, sublabel, ha='center', va='center',
                fontsize=6.5, color='#555')


def draw_wire(ax, points, color='#333', lw=1.2, ls='-'):
    """Draw a wire through a list of (x,y) points."""
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    ax.plot(xs, ys, color=color, lw=lw, ls=ls, solid_capstyle='round')


def draw_arrow(ax, x1, y1, x2, y2, color='#333', lw=1.5):
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle='->', color=color, lw=lw))


def draw_label(ax, x, y, text, fontsize=6, color='#333', ha='center', **kw):
    ax.text(x, y, text, fontsize=fontsize, color=color, ha=ha, va='center', **kw)


def draw_resistor_label(ax, x, y, value, orient='h', fontsize=5.5):
    """Draw a small resistor symbol with value."""
    if orient == 'h':
        ax.plot([x-0.15, x+0.15], [y, y], color='#666', lw=2)
        ax.text(x, y+0.08, value, fontsize=fontsize, ha='center', va='bottom', color='#666')
    else:
        ax.plot([x, x], [y-0.12, y+0.12], color='#666', lw=2)
        ax.text(x+0.1, y, value, fontsize=fontsize, ha='left', va='center', color='#666')


def draw_cap_label(ax, x, y, value, fontsize=5.5):
    ax.plot([x-0.06, x-0.06], [y-0.06, y+0.06], color='#666', lw=2)
    ax.plot([x+0.06, x+0.06], [y-0.06, y+0.06], color='#666', lw=2)
    ax.text(x, y+0.1, value, fontsize=fontsize, ha='center', va='bottom', color='#666')


# ============================================================
# VAIHTOEHTO A: IEC + Raspberry Pi Pico — DETAILED
# ============================================================
def generate_a():
    fig, ax = plt.subplots(1, 1, figsize=(18, 12))
    ax.set_xlim(-2, 18)
    ax.set_ylim(-2, 12)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')
    ax.set_title('Vaihtoehto A: IEC-väylä + Raspberry Pi Pico — Piirikaavio',
                 fontsize=15, fontweight='bold', pad=20)

    # ---- 6-pin DIN (IEC) ----
    draw_ic(ax, 0, 5, 1.8, 3.5, 'IEC-väylä\n6-pin DIN', [
        ('1', 'SRQ (n/c)'),
        ('3', 'ATN'),
        ('4', 'CLK'),
        ('5', 'DATA'),
        ('6', 'RESET'),
        ('2', 'GND'),
    ], [], color='#FFF3E0', edgecolor='#E65100', fontsize=8, pin_fontsize=6.5)
    draw_label(ax, 0.9, 4.7, 'C64:stä', fontsize=6, color='#E65100')

    # ---- BSS138 #1 (ATN) ----
    bss_x, bss_y = 4.0, 7.8
    draw_ic(ax, bss_x, bss_y, 1.6, 1.2, 'BSS138', [
        ('G', 'Gate'),
    ], [
        ('D', 'Drain'),
    ], color='#F3E5F5', edgecolor='#7B1FA2', fontsize=7,
    subtitle='N-ch MOSFET', pin_fontsize=5.5)
    # Source pin (bottom)
    ax.plot(bss_x + 0.8, bss_y, 'o', color='#7B1FA2', markersize=3)
    draw_label(ax, bss_x + 0.8, bss_y - 0.12, 'S→GND', fontsize=5, color='#7B1FA2')
    # pull-ups
    draw_resistor_label(ax, bss_x - 0.5, bss_y + 0.9, '10kΩ→5V', fontsize=5)
    draw_resistor_label(ax, bss_x + 2.3, bss_y + 0.9, '10kΩ→3V3', fontsize=5)
    # Label
    draw_label(ax, bss_x + 0.8, bss_y + 1.5, 'ATN', fontsize=7, fontweight='bold', color='#7B1FA2')

    # ---- BSS138 #2 (CLK) ----
    bss2_y = 6.2
    draw_ic(ax, bss_x, bss2_y, 1.6, 1.2, 'BSS138', [
        ('G', 'Gate'),
    ], [
        ('D', 'Drain'),
    ], color='#F3E5F5', edgecolor='#7B1FA2', fontsize=7,
    subtitle='N-ch MOSFET', pin_fontsize=5.5)
    ax.plot(bss_x + 0.8, bss2_y, 'o', color='#7B1FA2', markersize=3)
    draw_label(ax, bss_x + 0.8, bss2_y - 0.12, 'S→GND', fontsize=5, color='#7B1FA2')
    draw_resistor_label(ax, bss_x - 0.5, bss2_y + 0.9, '10kΩ→5V', fontsize=5)
    draw_resistor_label(ax, bss_x + 2.3, bss2_y + 0.9, '10kΩ→3V3', fontsize=5)
    draw_label(ax, bss_x + 0.8, bss2_y + 1.5, 'CLK', fontsize=7, fontweight='bold', color='#7B1FA2')

    # ---- BSS138 #3 (DATA) ----
    bss3_y = 4.6
    draw_ic(ax, bss_x, bss3_y, 1.6, 1.2, 'BSS138', [
        ('G', 'Gate'),
    ], [
        ('D', 'Drain'),
    ], color='#F3E5F5', edgecolor='#7B1FA2', fontsize=7,
    subtitle='N-ch MOSFET', pin_fontsize=5.5)
    ax.plot(bss_x + 0.8, bss3_y, 'o', color='#7B1FA2', markersize=3)
    draw_label(ax, bss_x + 0.8, bss3_y - 0.12, 'S→GND', fontsize=5, color='#7B1FA2')
    draw_resistor_label(ax, bss_x - 0.5, bss3_y + 0.9, '10kΩ→5V', fontsize=5)
    draw_resistor_label(ax, bss_x + 2.3, bss3_y + 0.9, '10kΩ→3V3', fontsize=5)
    draw_label(ax, bss_x + 0.8, bss3_y + 1.5, 'DATA', fontsize=7, fontweight='bold', color='#7B1FA2')

    # ---- BSS138 #4 (RESET) ----
    bss4_y = 3.0
    draw_ic(ax, bss_x, bss4_y, 1.6, 1.2, 'BSS138', [
        ('G', 'Gate'),
    ], [
        ('D', 'Drain'),
    ], color='#F3E5F5', edgecolor='#7B1FA2', fontsize=7,
    subtitle='N-ch MOSFET', pin_fontsize=5.5)
    ax.plot(bss_x + 0.8, bss4_y, 'o', color='#7B1FA2', markersize=3)
    draw_label(ax, bss_x + 0.8, bss4_y - 0.12, 'S→GND', fontsize=5, color='#7B1FA2')
    draw_resistor_label(ax, bss_x - 0.5, bss4_y + 0.9, '10kΩ→5V', fontsize=5)
    draw_resistor_label(ax, bss_x + 2.3, bss4_y + 0.9, '10kΩ→3V3', fontsize=5)
    draw_label(ax, bss_x + 0.8, bss4_y + 1.5, 'RESET', fontsize=7, fontweight='bold', color='#7B1FA2')

    # Wires: IEC DIN → BSS138 gates (5V side)
    for iec_py, bss_by in [(7.92, bss_y+0.6), (7.08, bss2_y+0.6),
                            (6.25, bss3_y+0.6), (5.42, bss4_y+0.6)]:
        draw_wire(ax, [(1.8, iec_py), (3.0, iec_py), (3.0, bss_by), (bss_x, bss_by)],
                  color='#E65100', lw=1.2)

    # ---- Raspberry Pi Pico ----
    pico_x, pico_y = 7.5, 1.5
    pico_w, pico_h = 3.0, 9.0
    draw_ic(ax, pico_x, pico_y, pico_w, pico_h, 'Raspberry Pi\nPico', [
        ('GP2', 'ATN'),
        ('GP3', 'CLK'),
        ('GP4', 'DATA'),
        ('GP5', 'RESET'),
        ('GP6', '/DENSITY'),
        ('GP7', '/MOTEA'),
        ('GP8', '/DRVSEL'),
        ('GP9', '/MOTOR'),
        ('GP10', '/DIR'),
        ('GP11', '/STEP'),
        ('GP12', '/WDATA'),
        ('GP13', '/WGATE'),
    ], [
        ('VBUS', '+5V in'),
        ('3V3', '3.3V out'),
        ('GND', 'GND'),
        ('GP14', '/TRK00'),
        ('GP15', '/WPT'),
        ('GP16', '/RDATA'),
        ('GP17', '/SIDE1'),
        ('GP18', '/DSKCHG'),
        ('', ''),
        ('', 'Core 0: IEC+FAT12'),
        ('', 'Core 1: Floppy+MFM'),
        ('', '264KB RAM, 2×PIO'),
    ], color='#E8F5E9', edgecolor='#2E7D32', fontsize=11, pin_fontsize=6)

    # Wires: BSS138 drains → Pico GPIOs (3.3V side)
    for bss_by, label_idx in [(bss_y+0.6, 0), (bss2_y+0.6, 1),
                               (bss3_y+0.6, 2), (bss4_y+0.6, 3)]:
        pico_py = pico_y + pico_h - (label_idx + 1) * pico_h / 13
        draw_wire(ax, [(bss_x+1.6, bss_by), (6.8, bss_by), (6.8, pico_py), (pico_x, pico_py)],
                  color='#2E7D32', lw=1.2)

    # ---- 34-pin IDC Floppy ----
    floppy_x = 13.0
    draw_ic(ax, floppy_x, 2.0, 2.2, 8.0, '34-pin IDC\nFloppy', [
        ('2', '/DENSITY'),
        ('10', '/MOTEA'),
        ('12', '/DRVSB'),
        ('16', '/MOTOR'),
        ('18', '/DIR'),
        ('20', '/STEP'),
        ('22', '/WDATA'),
        ('24', '/WGATE'),
        ('26', '/TRK00'),
        ('28', '/WPT'),
        ('30', '/RDATA'),
        ('32', '/SIDE1'),
        ('34', '/DSKCHG'),
    ], [
        ('odd', 'GND (all odd)'),
        ('', ''),
        ('', 'PC 3.5" HD'),
        ('', '1.44 MB'),
        ('', 'FAT12'),
        ('', ''),
        ('', '500 kbps'),
        ('', 'MFM encoded'),
        ('', ''),
        ('', '80 tracks'),
        ('', '2 sides'),
        ('', '18 sectors'),
        ('', '512 B/sector'),
    ], color='#E3F2FD', edgecolor='#1565C0', fontsize=9, pin_fontsize=5.5)

    # Wires: Pico output GPIOs → Floppy IDC (left side pins)
    # GP6-GP13 (outputs) → floppy pins
    for pico_idx, floppy_idx in [(4,0),(5,1),(6,2),(7,3),(8,4),(9,5),(10,6),(11,7)]:
        ppy = pico_y + pico_h - (pico_idx + 1) * pico_h / 13
        fpy = 2.0 + 8.0 - (floppy_idx + 1) * 8.0 / 14
        mid_x = 12.0 + floppy_idx * 0.06
        draw_wire(ax, [(pico_x + pico_w, ppy), (mid_x, ppy), (mid_x, fpy), (floppy_x, fpy)],
                  color='#1565C0', lw=0.8)

    # Pico right-side GPIOs → Floppy inputs
    for pico_ri, floppy_idx in [(3,8),(4,9),(5,10),(6,11),(7,12)]:
        ppy = pico_y + pico_h - (pico_ri + 1) * pico_h / 13
        fpy = 2.0 + 8.0 - (floppy_idx + 1) * 8.0 / 14
        mid_x = 12.3 + pico_ri * 0.06
        draw_wire(ax, [(pico_x + pico_w, ppy), (mid_x, ppy), (mid_x, fpy), (floppy_x, fpy)],
                  color='#0D47A1', lw=0.8, ls='--')

    # ---- PSU ----
    draw_block(ax, 3.5, 0, 3, 1.2, '5V / 2A PSU', '#FBE9E7', '#BF360C',
               fontsize=9, sublabel='Ulkoinen virtalähde')
    # Power wires
    draw_wire(ax, [(6.5, 1.2), (6.5, 1.5), (pico_x+pico_w, 9.9)],
              color='#BF360C', lw=1.5, ls='--')
    draw_wire(ax, [(5.0, 0.6), (floppy_x+1.1, 0.6), (floppy_x+1.1, 2.0)],
              color='#BF360C', lw=1.5, ls='--')
    draw_label(ax, 9, 0.4, '+5V', fontsize=6, color='#BF360C')

    # ---- BSS138 pinout legend ----
    draw_block(ax, -1.5, 0, 4.5, 2.5, '', '#FAFAFA', '#BDBDBD', fontsize=7)
    ax.text(-1.3, 2.3, 'BSS138 MOSFET — Pinout (SOT-23)', fontsize=7,
            fontweight='bold', va='top')
    ax.text(-1.3, 1.9, '┌─────────┐\n│  1 G  Gate   │  ← IEC-signaali (5V puoli)\n│  2 S  Source │  → GND\n│  3 D  Drain  │  → Pico GPIO (3.3V puoli)\n└─────────┘', fontsize=5.5,
            va='top', family='monospace', color='#555')
    ax.text(-1.3, 0.4, 'Bidirektionaalinen tasonmuunnin:\n5V HIGH ↔ 3.3V HIGH\n5V LOW  ↔ 3.3V LOW', fontsize=5.5,
            va='top', color='#7B1FA2')

    # ---- Info box ----
    box = FancyBboxPatch((-1.5, 10.5), 17, 0.6, boxstyle="round,pad=0.1",
                          facecolor='#E8F5E9', edgecolor='#2E7D32', linewidth=2)
    ax.add_patch(box)
    ax.text(7, 10.8, 'Hinta: ~12€  |  IEC 1-3 KB/s  |  Ei C64-modifiointia  |  4× BSS138 level shifter + 8× 10kΩ',
            fontsize=9, ha='center', fontweight='bold', color='#1B5E20')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/A-IEC-Pico/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VAIHTOEHTO B: Expansion Port + FDC — DETAILED
# ============================================================
def generate_b():
    fig, ax = plt.subplots(1, 1, figsize=(18, 12))
    ax.set_xlim(-2, 18)
    ax.set_ylim(-2, 12)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')
    ax.set_title('Vaihtoehto B: Expansion Port + FDC-piiri — Piirikaavio',
                 fontsize=15, fontweight='bold', pad=20)

    # ---- C64 Expansion Port ----
    draw_ic(ax, -1, 2, 2.5, 8, 'C64\nExpansion\nPort', [
    ], [
        ('D0-D7', 'Data bus'),
        ('A0-A15', 'Address bus'),
        ('R/W', 'Read/Write'),
        ('Φ2', 'Clock 1MHz'),
        ('/IO1', '$DE00-$DEFF'),
        ('/ROML', '$8000-$9FFF'),
        ('/EXROM', '→ GND'),
        ('/IRQ', 'Interrupt'),
        ('+5V', 'Power'),
        ('GND', 'Ground'),
    ], color='#FFF3E0', edgecolor='#E65100', fontsize=10, pin_fontsize=6)

    # ---- 74LS138 Address Decoder ----
    draw_ic(ax, 4, 7, 2.2, 3.5, '74LS138', [
        ('1', 'A  ← A3'),
        ('2', 'B  ← A4'),
        ('3', 'C  ← A5'),
        ('4', '/G2A ← /IO1'),
        ('5', '/G2B ← A7'),
        ('6', 'G1 ← /A6'),
    ], [
        ('15', 'Y0 → /FDC_CS'),
        ('14', 'Y1 (n/c)'),
        ('13', 'Y2 (n/c)'),
    ], color='#FCE4EC', edgecolor='#C62828', fontsize=8,
    subtitle='3-to-8 decoder\n$DE00-$DE07', pin_fontsize=5.5)

    # ---- 74LS04 Inverter ----
    draw_ic(ax, 4, 5.5, 2.2, 1.2, '74LS04', [
        ('1', 'A6 in'),
    ], [
        ('2', '/A6 out → G1'),
    ], color='#FCE4EC', edgecolor='#C62828', fontsize=8,
    subtitle='Hex inverter', pin_fontsize=5.5)

    # ---- 74LS245 Bus Buffer ----
    draw_ic(ax, 4, 2.5, 2.2, 2.5, '74LS245', [
        ('A1-A8', 'D0-D7 (C64)'),
        ('DIR', '← /R/W (inv)'),
        ('/OE', '← /FDC_CS'),
    ], [
        ('B1-B8', 'DB0-DB7 (FDC)'),
    ], color='#E8EAF6', edgecolor='#283593', fontsize=8,
    subtitle='Bidir. bus buffer', pin_fontsize=5.5)

    # ---- 82077AA FDC ----
    draw_ic(ax, 8, 1.5, 2.8, 8, '82077AA\nFDC', [
        ('DB0-7', 'Data bus'),
        ('A0', 'Address 0'),
        ('A1', 'Address 1'),
        ('A2', 'Address 2'),
        ('/CS', 'Chip select'),
        ('/RD', 'Read'),
        ('/WR', 'Write'),
        ('CLK', 'Φ2 (1 MHz)'),
        ('X1', '24 MHz xtal'),
        ('/IRQ', 'Interrupt'),
    ], [
        ('/DENSEL', 'pin 2'),
        ('/MOTEA', 'pin 10'),
        ('/DRVSA', 'pin 12'),
        ('/DIR', 'pin 18'),
        ('/STEP', 'pin 20'),
        ('/WDATA', 'pin 22'),
        ('/WGATE', 'pin 24'),
        ('/RDATA', 'pin 30'),
        ('/TRK00', 'pin 26'),
        ('/SIDE1', 'pin 32'),
    ], color='#FFF9C4', edgecolor='#F57F17', fontsize=10,
    subtitle='Floppy Disk Controller', pin_fontsize=5.5)

    # ---- 34-pin IDC Floppy ----
    draw_ic(ax, 13.5, 2.5, 2.2, 6.5, '34-pin IDC\nFloppy', [
        ('2', '/DENSEL'),
        ('10', '/MOTEA'),
        ('12', '/DRVSA'),
        ('18', '/DIR'),
        ('20', '/STEP'),
        ('22', '/WDATA'),
        ('24', '/WGATE'),
        ('26', '/TRK00'),
        ('30', '/RDATA'),
        ('32', '/SIDE1'),
    ], [
        ('odd', 'GND'),
        ('', ''),
        ('', 'PC 3.5" HD'),
        ('', '1.44 MB'),
        ('', 'FAT12'),
    ], color='#E3F2FD', edgecolor='#1565C0', fontsize=9, pin_fontsize=5.5)

    # ---- 28C64 EEPROM ----
    draw_ic(ax, 4, -0.5, 2.2, 2.5, '28C64\nEEPROM', [
        ('A0-12', 'Address (C64)'),
        ('/CE', '← /ROML'),
        ('/OE', '← Φ2'),
    ], [
        ('D0-D7', 'Data (C64)'),
        ('/WE', '→ +5V'),
    ], color='#E0F2F1', edgecolor='#00695C', fontsize=8,
    subtitle='8KB ROM @ $8000', pin_fontsize=5.5)

    # ---- 24 MHz Crystal ----
    draw_block(ax, 8.5, -0.5, 1.5, 1, '24 MHz\nCrystal', '#F5F5F5', '#616161',
               fontsize=7, sublabel='+ 2× 22pF')
    draw_wire(ax, [(9.25, 0.5), (9.25, 1.5)], color='#616161', lw=1)

    # Wires: 74LS245 → FDC
    draw_wire(ax, [(6.2, 4.4), (7.5, 4.4), (7.5, 8.7), (8.0, 8.7)],
              color='#283593', lw=1.5)
    draw_label(ax, 7.3, 6, 'DB0-DB7', fontsize=6, color='#283593', rotation=90)

    # Wire: 74LS138 Y0 → FDC /CS
    draw_wire(ax, [(6.2, 9.5), (7.2, 9.5), (7.2, 5.7), (8.0, 5.7)],
              color='#C62828', lw=1.2)
    draw_label(ax, 7.0, 7.5, '/FDC_CS', fontsize=6, color='#C62828', rotation=90)

    # Wire: FDC → Floppy (right side outputs)
    for i in range(10):
        fdc_py = 1.5 + 8.0 - (i + 1) * 8.0 / 11
        fl_py = 2.5 + 6.5 - (i + 1) * 6.5 / 11
        draw_wire(ax, [(10.8, fdc_py), (12.5, fdc_py), (12.5, fl_py), (13.5, fl_py)],
                  color='#1565C0', lw=0.8)

    # Info box
    box = FancyBboxPatch((-1.5, 10.5), 17, 0.6, boxstyle="round,pad=0.1",
                          facecolor='#FFF9C4', edgecolor='#F57F17', linewidth=2)
    ax.add_patch(box)
    ax.text(7, 10.8, 'Hinta: ~20-30€  |  Nopein (~50 KB/s)  |  FDC hoitaa MFM  |  Vaatii custom ROM + vie expansion portin',
            fontsize=9, ha='center', fontweight='bold', color='#E65100')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/B-Expansion-FDC/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VAIHTOEHTO C: User Port + Pico — DETAILED
# ============================================================
def generate_c():
    fig, ax = plt.subplots(1, 1, figsize=(18, 12))
    ax.set_xlim(-2, 18)
    ax.set_ylim(-2, 12)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')
    ax.set_title('Vaihtoehto C: User Port + Raspberry Pi Pico — Piirikaavio',
                 fontsize=15, fontweight='bold', pad=20)

    # ---- C64 User Port ----
    draw_ic(ax, -1, 3, 2.2, 7, 'C64\nUser Port\n24-pin', [
    ], [
        ('C', 'PB0'),
        ('D', 'PB1'),
        ('E', 'PB2'),
        ('F', 'PB3'),
        ('H', 'PB4'),
        ('J', 'PB5'),
        ('K', 'PB6'),
        ('L', 'PB7'),
        ('M', 'PA2 (strobe)'),
        ('B', '/FLAG2 (IRQ)'),
        ('+5V', 'pin 2'),
        ('GND', 'pin 1,12'),
    ], color='#FFF3E0', edgecolor='#E65100', fontsize=9, pin_fontsize=5.5)

    # ---- 74LVC245 ----
    draw_ic(ax, 3.5, 4.5, 2.5, 5, '74LVC245', [
        ('A1', 'PB0 (5V)'),
        ('A2', 'PB1'),
        ('A3', 'PB2'),
        ('A4', 'PB3'),
        ('A5', 'PB4'),
        ('A6', 'PB5'),
        ('A7', 'PB6'),
        ('A8', 'PB7'),
    ], [
        ('B1', 'GP0 (3.3V)'),
        ('B2', 'GP1'),
        ('B3', 'GP2'),
        ('B4', 'GP3'),
        ('B5', 'GP4'),
        ('B6', 'GP5'),
        ('B7', 'GP6'),
        ('B8', 'GP7'),
    ], color='#E8EAF6', edgecolor='#283593', fontsize=9,
    subtitle='Bidir. level shift\nVCC_A=5V VCC_B=3V3', pin_fontsize=5.5)
    # DIR and OE
    draw_label(ax, 4.75, 4.2, 'DIR ← GP26  |  /OE ← GND', fontsize=5.5,
               color='#283593', fontweight='bold')

    # ---- BSS138 for PA2 ----
    draw_ic(ax, 3.5, 2.5, 2.0, 1.2, 'BSS138 #1', [
        ('G', 'PA2 (5V)'),
    ], [
        ('D', 'GP9 (3.3V)'),
    ], color='#F3E5F5', edgecolor='#7B1FA2', fontsize=7,
    subtitle='S→GND, +pull-ups', pin_fontsize=5.5)
    draw_label(ax, 4.5, 3.9, 'PA2 strobe (C64→Pico)', fontsize=5.5, color='#7B1FA2')

    # ---- BSS138 for /FLAG2 ----
    draw_ic(ax, 3.5, 1, 2.0, 1.2, 'BSS138 #2', [
        ('G', '/FLAG2 (5V)'),
    ], [
        ('D', 'GP8 (3.3V)'),
    ], color='#F3E5F5', edgecolor='#7B1FA2', fontsize=7,
    subtitle='S→GND, +pull-ups', pin_fontsize=5.5)
    draw_label(ax, 4.5, 2.4, '/FLAG2 strobe (Pico→C64, IRQ)', fontsize=5.5, color='#7B1FA2')

    # ---- Pico ----
    draw_ic(ax, 8, 1.5, 3.0, 9.0, 'Raspberry Pi\nPico', [
        ('GP0', 'PB0 (data)'),
        ('GP1', 'PB1'),
        ('GP2', 'PB2'),
        ('GP3', 'PB3'),
        ('GP4', 'PB4'),
        ('GP5', 'PB5'),
        ('GP6', 'PB6'),
        ('GP7', 'PB7'),
        ('GP8', '/FLAG2'),
        ('GP9', 'PA2'),
        ('GP26', '245 DIR'),
    ], [
        ('GP10', '/DENSITY'),
        ('GP11', '/MOTEA'),
        ('GP12', '/DRVSEL'),
        ('GP13', '/MOTOR'),
        ('GP14', '/DIR'),
        ('GP15', '/STEP'),
        ('GP16', '/WDATA'),
        ('GP17', '/WGATE'),
        ('GP18', '/TRK00'),
        ('GP19', '/WPT'),
        ('GP20', '/RDATA'),
        ('GP21', '/SIDE1'),
        ('GP22', '/DSKCHG'),
    ], color='#E8F5E9', edgecolor='#2E7D32', fontsize=11, pin_fontsize=5.5)

    # ---- Floppy ----
    draw_ic(ax, 14, 2.5, 2, 7, '34-pin\nFloppy', [
        ('2', '/DENSITY'),
        ('10', '/MOTEA'),
        ('12', '/DRVSEL'),
        ('16', '/MOTOR'),
        ('18', '/DIR'),
        ('20', '/STEP'),
        ('22', '/WDATA'),
        ('24', '/WGATE'),
        ('26', '/TRK00'),
        ('28', '/WPT'),
        ('30', '/RDATA'),
        ('32', '/SIDE1'),
        ('34', '/DSKCHG'),
    ], [], color='#E3F2FD', edgecolor='#1565C0', fontsize=9, pin_fontsize=5.5)

    # Wires: 74LVC245 B side → Pico GP0-GP7
    for i in range(8):
        src_y = 4.5 + 5.0 - (i + 1) * 5.0 / 9
        dst_y = 1.5 + 9.0 - (i + 1) * 9.0 / 12
        draw_wire(ax, [(6.0, src_y), (7.5, src_y), (7.5, dst_y), (8.0, dst_y)],
                  color='#283593', lw=0.8)

    # Wires: Pico right → Floppy left
    for i in range(13):
        src_y = 1.5 + 9.0 - (i + 1) * 9.0 / 14
        dst_y = 2.5 + 7.0 - (i + 1) * 7.0 / 14
        draw_wire(ax, [(11.0, src_y), (13.0, src_y), (13.0, dst_y), (14.0, dst_y)],
                  color='#1565C0', lw=0.7)

    # Info
    box = FancyBboxPatch((-1.5, 10.8), 17, 0.6, boxstyle="round,pad=0.1",
                          facecolor='#E8EAF6', edgecolor='#283593', linewidth=2)
    ax.add_patch(box)
    ax.text(7, 11.1, 'Hinta: ~12€  |  8-bit rinnakkaissiirto 10-20 KB/s  |  Expansion port vapaa  |  Vaatii C64-ajurin',
            fontsize=9, ha='center', fontweight='bold', color='#283593')

    # PSU
    draw_block(ax, 3.5, -1.5, 3, 1.0, '5V / 2A PSU', '#FBE9E7', '#BF360C', fontsize=8)

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/C-Userport-Pico/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VAIHTOEHTO D: IEC + Arduino Nano — DETAILED
# ============================================================
def generate_d():
    fig, ax = plt.subplots(1, 1, figsize=(18, 12))
    ax.set_xlim(-2, 18)
    ax.set_ylim(-2, 12)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')
    ax.set_title('Vaihtoehto D: IEC + Arduino Nano — Piirikaavio',
                 fontsize=15, fontweight='bold', pad=20)

    # ---- IEC DIN ----
    draw_ic(ax, -1, 5, 1.8, 3.5, 'IEC-väylä\n6-pin DIN', [
        ('1', 'SRQ (n/c)'),
        ('3', 'ATN'),
        ('4', 'CLK'),
        ('5', 'DATA'),
        ('6', 'RESET'),
        ('2', 'GND'),
    ], [], color='#FFF3E0', edgecolor='#E65100', fontsize=8, pin_fontsize=6.5)

    # ---- Resistors (direct 5V connection!) ----
    for i, (sig, y_off) in enumerate([('ATN', 7.5), ('CLK', 6.7),
                                        ('DATA', 5.8), ('RESET', 5.0)]):
        # 1kΩ series resistor
        draw_wire(ax, [(1.8, y_off), (2.5, y_off)], color='#E65100', lw=1.2)
        draw_resistor_label(ax, 2.8, y_off, '1kΩ')
        draw_wire(ax, [(3.1, y_off), (4.0, y_off)], color='#388E3C', lw=1.2)
        # 4.7kΩ optional pull-up
        if i < 3:
            draw_wire(ax, [(3.5, y_off), (3.5, y_off+0.3)], color='#888', lw=0.6, ls=':')
            draw_label(ax, 3.5, y_off+0.45, '4.7k→5V', fontsize=4.5, color='#888')

    draw_label(ax, 2.8, 4.5, 'SUORA 5V↔5V KYTKENTÄ\nEi tasonmuuntimia!',
               fontsize=8, color='#2E7D32', fontweight='bold')

    # ---- Arduino Nano ----
    draw_ic(ax, 5, 1, 3.0, 9.5, 'Arduino\nNano', [
        ('D2', 'ATN (INT0)'),
        ('D3', 'CLK'),
        ('D4', 'DATA'),
        ('D5', 'RESET'),
        ('D6', '/DENSITY'),
        ('D7', '/MOTEA'),
        ('D8', '/RDATA (ICP1!)'),
        ('D9', '/MOTOR'),
        ('D10', '/DIR'),
        ('D11', '/STEP'),
        ('D12', '/WDATA'),
        ('D13', '/WGATE'),
    ], [
        ('+5V', 'Power in'),
        ('GND', 'Ground'),
        ('A0', '/TRK00'),
        ('A1', '/WPT'),
        ('A2', '/DRVSEL'),
        ('A3', '/SIDE1'),
        ('A4', '/DSKCHG'),
        ('A5', 'Status LED'),
        ('', ''),
        ('', 'ATmega328P'),
        ('', '16 MHz, 5V'),
        ('', '2 KB RAM'),
    ], color='#E3F2FD', edgecolor='#1565C0', fontsize=12, pin_fontsize=6)

    # Wires: resistors → Nano D2-D5
    for nano_idx, sig_y in [(0, 7.5), (1, 6.7), (2, 5.8), (3, 5.0)]:
        nano_py = 1.0 + 9.5 - (nano_idx + 1) * 9.5 / 13
        draw_wire(ax, [(4.0, sig_y), (4.5, sig_y), (4.5, nano_py), (5.0, nano_py)],
                  color='#388E3C', lw=1.2)

    # ---- 34-pin IDC Floppy ----
    draw_ic(ax, 12, 1.5, 2.2, 8, '34-pin IDC\nFloppy', [
        ('2', '/DENSITY'),
        ('10', '/MOTEA'),
        ('12', '/DRVSEL'),
        ('16', '/MOTOR'),
        ('18', '/DIR'),
        ('20', '/STEP'),
        ('22', '/WDATA'),
        ('24', '/WGATE'),
        ('26', '/TRK00'),
        ('28', '/WPT'),
        ('30', '/RDATA'),
        ('32', '/SIDE1'),
        ('34', '/DSKCHG'),
    ], [
        ('odd', 'GND'),
        ('', ''),
        ('', 'PC 3.5" HD'),
        ('', '1.44 MB'),
        ('', 'FAT12'),
        ('', ''),
        ('', '5V logiikka'),
        ('', '(suora kytkentä!)'),
    ], color='#E3F2FD', edgecolor='#1565C0', fontsize=9, pin_fontsize=5.5)

    # Wires: Nano left outputs → Floppy
    floppy_sigs = ['/DENSITY','/MOTEA','','','/MOTOR','/DIR','/STEP','/WDATA','/WGATE']
    for nano_idx in [4,5,7,8,9,10,11]:  # D6,D7,D9-D13
        nano_py = 1.0 + 9.5 - (nano_idx + 1) * 9.5 / 13
        fl_map = {4:0, 5:1, 7:3, 8:4, 9:5, 10:6, 11:7}
        if nano_idx in fl_map:
            fl_idx = fl_map[nano_idx]
            fl_py = 1.5 + 8.0 - (fl_idx + 1) * 8.0 / 14
            mid_x = 10.5 + nano_idx * 0.08
            draw_wire(ax, [(8.0, nano_py), (mid_x, nano_py), (mid_x, fl_py), (12.0, fl_py)],
                      color='#1565C0', lw=0.8)

    # D8 (ICP1) → /RDATA with highlight
    nano_py_d8 = 1.0 + 9.5 - 7 * 9.5 / 13
    fl_rdata = 1.5 + 8.0 - 11 * 8.0 / 14
    draw_wire(ax, [(8.0, nano_py_d8), (11.2, nano_py_d8), (11.2, fl_rdata), (12.0, fl_rdata)],
              color='#D32F2F', lw=2)
    draw_label(ax, 11.5, nano_py_d8 + 0.2, 'ICP1!\nTimer1 Input\nCapture', fontsize=5.5,
               color='#D32F2F', fontweight='bold')

    # Nano right side → Floppy inputs
    for nano_ri, fl_idx in [(2,8),(3,9),(4,2),(5,11),(6,12)]:
        nano_py = 1.0 + 9.5 - (nano_ri + 1) * 9.5 / 13
        fl_py = 1.5 + 8.0 - (fl_idx + 1) * 8.0 / 14
        mid_x = 10.0 + nano_ri * 0.1
        draw_wire(ax, [(8.0, nano_py), (mid_x, nano_py), (mid_x, fl_py), (12.0, fl_py)],
                  color='#0D47A1', lw=0.8, ls='--')

    # Pull-ups for inputs
    for fl_idx in [8,9,10,12]:
        fl_py = 1.5 + 8.0 - (fl_idx + 1) * 8.0 / 14
        draw_label(ax, 11.7, fl_py + 0.15, '↑5V', fontsize=4.5, color='#888')

    # ---- PSU ----
    draw_block(ax, 4, -1.5, 3, 1.0, '5V / 2A PSU', '#FBE9E7', '#BF360C', fontsize=8)
    draw_wire(ax, [(7.0, -1.0), (14.0, -1.0), (14.0, 1.5)], color='#BF360C', lw=1.5, ls='--')
    draw_label(ax, 10, -1.3, '+5V (Nano + Floppy)', fontsize=6, color='#BF360C')

    # ---- Highlight ----
    box = FancyBboxPatch((-1.5, 10.5), 17, 0.6, boxstyle="round,pad=0.1",
                          facecolor='#C8E6C9', edgecolor='#2E7D32', linewidth=2)
    ax.add_patch(box)
    ax.text(7, 10.8, 'Hinta: ~8€  |  5V NATIIVI — 0 tasonmuunninta!  |  IEC 1-3 KB/s  |  Yksinkertaisin kytkentä',
            fontsize=9, ha='center', fontweight='bold', color='#1B5E20')

    # Warning
    draw_block(ax, 12.5, -1.5, 3.5, 1.0, 'HUOM: 2 KB RAM\nSektori kerrallaan!',
               '#FBE9E7', '#BF360C', fontsize=7, bold=False)

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/D-IEC-Nano/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VAIHTOEHTO E: IEC + Nano + SPI SRAM — DETAILED
# ============================================================
def generate_e():
    fig, ax = plt.subplots(1, 1, figsize=(20, 13))
    ax.set_xlim(-3, 19)
    ax.set_ylim(-3, 13)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')
    ax.set_title('Vaihtoehto E: IEC + Arduino Nano + 23LC1024 SPI SRAM — Piirikaavio',
                 fontsize=15, fontweight='bold', pad=20)

    # ---- IEC DIN ----
    draw_ic(ax, -2, 6, 1.8, 3, 'IEC\n6-pin DIN', [
        ('3', 'ATN'),
        ('4', 'CLK'),
        ('5', 'DATA'),
        ('6', 'RESET'),
        ('2', 'GND'),
    ], [], color='#FFF3E0', edgecolor='#E65100', fontsize=8, pin_fontsize=6)

    # Resistors
    for i, (sig, y_off) in enumerate([('ATN', 8.1), ('CLK', 7.35),
                                        ('DATA', 6.6), ('RESET', 5.85)]):
        draw_wire(ax, [(-0.2, y_off), (0.5, y_off)], color='#E65100', lw=1.2)
        draw_resistor_label(ax, 0.8, y_off, '1kΩ')
        draw_wire(ax, [(1.1, y_off), (2.0, y_off)], color='#388E3C', lw=1.2)

    # ---- Arduino Nano ----
    draw_ic(ax, 3, 1.5, 3.0, 9.5, 'Arduino\nNano', [
        ('D2', 'ATN (INT0)'),
        ('D3', 'CLK'),
        ('D4', 'DATA'),
        ('D5', 'RESET'),
        ('D6', '595 RCLK'),
        ('D7', '/WDATA'),
        ('D8', '/RDATA (ICP1)'),
        ('D9', '(vapaa)'),
        ('D10', 'SRAM /CS'),
        ('D11', 'SPI MOSI'),
        ('D12', 'SPI MISO'),
        ('D13', 'SPI SCK'),
    ], [
        ('+5V', 'Power'),
        ('GND', 'Ground'),
        ('A0', '/TRK00'),
        ('A1', '/WPT'),
        ('A2', '/DSKCHG'),
        ('A3', '(vapaa)'),
        ('A4', '(vapaa)'),
        ('A5', 'LED'),
        ('', ''),
        ('', 'ATmega328P'),
        ('', '16 MHz, 5V'),
        ('', '2 KB RAM'),
    ], color='#E3F2FD', edgecolor='#1565C0', fontsize=12, pin_fontsize=5.5)

    # Wires: IEC → Nano D2-D5
    for nano_idx, sig_y in [(0, 8.1), (1, 7.35), (2, 6.6), (3, 5.85)]:
        nano_py = 1.5 + 9.5 - (nano_idx + 1) * 9.5 / 13
        draw_wire(ax, [(2.0, sig_y), (2.5, sig_y), (2.5, nano_py), (3.0, nano_py)],
                  color='#388E3C', lw=1.2)

    # ---- 23LC1024 SPI SRAM ----
    draw_ic(ax, 3, -2, 3.0, 2.5, '23LC1024', [
        ('1', '/CS ← D10'),
        ('5', 'SI ← D11 (MOSI)'),
        ('6', 'SCK ← D13'),
        ('4', 'VSS → GND'),
    ], [
        ('8', 'VCC → +5V'),
        ('7', '/HOLD → +5V'),
        ('2', 'SO → D12 (MISO)'),
        ('3', 'NC'),
    ], color='#FFF9C4', edgecolor='#F57F17', fontsize=10,
    subtitle='128 KB SPI SRAM\n5V, 20 MHz', pin_fontsize=5.5)

    # Wires: Nano SPI → SRAM
    # D10 → /CS
    nano_d10_y = 1.5 + 9.5 - 9 * 9.5 / 13
    draw_wire(ax, [(3.0, nano_d10_y), (2.2, nano_d10_y), (2.2, 0.1), (3.0, 0.1)],
              color='#F57F17', lw=1.2)
    # D11 → SI
    nano_d11_y = 1.5 + 9.5 - 10 * 9.5 / 13
    draw_wire(ax, [(3.0, nano_d11_y), (1.8, nano_d11_y), (1.8, -0.35), (3.0, -0.35)],
              color='#F57F17', lw=1.2)
    # D13 → SCK
    nano_d13_y = 1.5 + 9.5 - 12 * 9.5 / 13
    draw_wire(ax, [(3.0, nano_d13_y), (1.4, nano_d13_y), (1.4, -0.8), (3.0, -0.8)],
              color='#F57F17', lw=1.2)
    # D12 ← SO
    nano_d12_y = 1.5 + 9.5 - 11 * 9.5 / 13
    draw_wire(ax, [(6.0, nano_d12_y), (7.0, nano_d12_y), (7.0, -0.7), (6.0, -0.7)],
              color='#F57F17', lw=1.2, ls='--')

    # ---- 74HC595 Shift Register ----
    draw_ic(ax, 9, -2, 3.0, 3.5, '74HC595', [
        ('14', 'SER ← MOSI'),
        ('11', 'SRCLK ← SCK'),
        ('12', 'RCLK ← D6'),
        ('10', '/CLR → +5V'),
        ('13', '/OE → GND'),
    ], [
        ('15', 'QA → /SIDE1'),
        ('1', 'QB → /DENSITY'),
        ('2', 'QC → /MOTEA'),
        ('3', 'QD → /DRVSEL'),
        ('4', 'QE → /MOTOR'),
        ('5', 'QF → /DIR'),
        ('6', 'QG → /STEP'),
        ('7', 'QH → /WGATE'),
    ], color='#F3E5F5', edgecolor='#7B1FA2', fontsize=9,
    subtitle='8-bit shift register\nSPI-ohjattu, 5V', pin_fontsize=5.5)

    # Wires: Nano D11 (MOSI) → 595 SER (shared SPI)
    draw_wire(ax, [(1.8, -0.35), (1.8, -1.3), (9.0, -1.3)], color='#7B1FA2', lw=1.2)
    draw_label(ax, 5.5, -1.1, 'MOSI (jaettu)', fontsize=5.5, color='#7B1FA2')

    # Nano D13 (SCK) → 595 SRCLK
    draw_wire(ax, [(1.4, -0.8), (1.4, -1.7), (9.0, -1.7)], color='#7B1FA2', lw=1.2, ls='--')
    draw_label(ax, 5.5, -1.5, 'SCK (jaettu)', fontsize=5.5, color='#7B1FA2')

    # D6 → 595 RCLK
    nano_d6_y = 1.5 + 9.5 - 5 * 9.5 / 13
    draw_wire(ax, [(3.0, nano_d6_y), (0.8, nano_d6_y), (0.8, -0.9), (9.0, -0.9)],
              color='#7B1FA2', lw=1.2)
    draw_label(ax, 5.5, -0.7, 'LATCH (D6)', fontsize=5.5, color='#7B1FA2')

    # ---- 34-pin Floppy ----
    draw_ic(ax, 15, 0, 2.2, 9, '34-pin\nFloppy', [
        ('2', '/DENSITY'),
        ('10', '/MOTEA'),
        ('12', '/DRVSEL'),
        ('16', '/MOTOR'),
        ('18', '/DIR'),
        ('20', '/STEP'),
        ('22', '/WDATA'),
        ('24', '/WGATE'),
        ('26', '/TRK00'),
        ('28', '/WPT'),
        ('30', '/RDATA'),
        ('32', '/SIDE1'),
        ('34', '/DSKCHG'),
    ], [
        ('odd', 'GND'),
        ('', ''),
        ('', 'PC 3.5" HD'),
        ('', '1.44 MB'),
        ('', 'FAT12'),
    ], color='#E3F2FD', edgecolor='#1565C0', fontsize=9, pin_fontsize=5.5)

    # Wires: 595 outputs → Floppy
    for q_idx, fl_idx in [(0,11),(1,0),(2,1),(3,2),(4,3),(5,4),(6,5),(7,7)]:
        q_py = -2.0 + 3.5 - (q_idx + 1) * 3.5 / 9
        fl_py = 0 + 9.0 - (fl_idx + 1) * 9.0 / 14
        mid_x = 13.5 + q_idx * 0.08
        draw_wire(ax, [(12.0, q_py), (mid_x, q_py), (mid_x, fl_py), (15.0, fl_py)],
                  color='#7B1FA2', lw=0.8)

    # Direct: D7 → /WDATA (pin 22)
    nano_d7_y = 1.5 + 9.5 - 6 * 9.5 / 13
    fl_wdata_y = 0 + 9.0 - 7 * 9.0 / 14
    draw_wire(ax, [(6.0, nano_d7_y), (14.5, nano_d7_y), (14.5, fl_wdata_y), (15.0, fl_wdata_y)],
              color='#D32F2F', lw=2)
    draw_label(ax, 10, nano_d7_y + 0.2, '/WDATA (suora GPIO, MFM-kirjoitus)', fontsize=5.5,
               color='#D32F2F', fontweight='bold')

    # Direct: D8 → /RDATA (pin 30) via ICP1
    nano_d8_y = 1.5 + 9.5 - 7 * 9.5 / 13
    fl_rdata_y = 0 + 9.0 - 11 * 9.0 / 14
    draw_wire(ax, [(6.0, nano_d8_y), (14.0, nano_d8_y), (14.0, fl_rdata_y), (15.0, fl_rdata_y)],
              color='#D32F2F', lw=2)
    draw_label(ax, 10, nano_d8_y + 0.2, '/RDATA (ICP1 — Timer1 Input Capture!)', fontsize=5.5,
               color='#D32F2F', fontweight='bold')

    # Nano right → Floppy inputs
    for nano_ri, fl_idx in [(2,8),(3,9),(4,12)]:
        nano_py = 1.5 + 9.5 - (nano_ri + 1) * 9.5 / 13
        fl_py = 0 + 9.0 - (fl_idx + 1) * 9.0 / 14
        draw_wire(ax, [(6.0, nano_py), (13.2, nano_py), (13.2, fl_py), (15.0, fl_py)],
                  color='#0D47A1', lw=0.8, ls='--')

    # ---- PSU ----
    draw_block(ax, 9, -2.5, 3, 0.8, '5V / 2A PSU', '#FBE9E7', '#BF360C', fontsize=8)

    # ---- Memory map ----
    draw_block(ax, -2.5, 0, 4, 3.5, '', '#FFF9C4', '#F57F17', fontsize=7)
    ax.text(-2.3, 3.3, 'SRAM muistikartta (128 KB)', fontsize=7,
            fontweight='bold', va='top', color='#F57F17')
    for i, (addr, use, sz) in enumerate([
        ('0x00000', 'MFM-raitapuskuri', '12.5 KB'),
        ('0x030D0', 'FAT-cache', '4.5 KB'),
        ('0x042D0', 'Sektoripuskuri ×2', '1 KB'),
        ('0x046D0', 'Hakemistopuskuri', '4 KB'),
        ('0x056D0', 'IEC-puskuri', '512 B'),
        ('0x058D0', 'Vapaa', '~105 KB'),
    ]):
        ax.text(-2.3, 2.8 - i*0.4, f'{addr}  {use}', fontsize=5.5,
                va='top', family='monospace')
        ax.text(1.2, 2.8 - i*0.4, sz, fontsize=5.5, va='top',
                family='monospace', color='#888')

    # ---- Highlight ----
    box = FancyBboxPatch((-2.5, 11.5), 19, 0.6, boxstyle="round,pad=0.1",
                          facecolor='#C8E6C9', edgecolor='#2E7D32', linewidth=2)
    ax.add_patch(box)
    ax.text(7, 11.8, 'Hinta: ~10-13€  |  5V NATIIVI — 0 tasonmuunninta!  |  128 KB SRAM  |  SPI jaettu: SRAM + 74HC595',
            fontsize=9, ha='center', fontweight='bold', color='#1B5E20')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/E-IEC-Nano-SRAM/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VERTAILUKAAVIO (sama kuin ennen)
# ============================================================
def generate_comparison():
    fig, ax = plt.subplots(1, 1, figsize=(14, 8))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis('off')
    fig.patch.set_facecolor('white')
    ax.set_title('64korppu — Vaihtoehtojen vertailu', fontsize=16, fontweight='bold', pad=20)

    headers = ['', 'A: IEC+Pico', 'B: Exp+FDC', 'C: User+Pico', 'D: IEC+Nano', 'E: Nano+SRAM']
    rows = [
        ['Hinta',       '~12€',    '~20-30€',   '~12€',      '~8€',       '~10-13€'],
        ['RAM',         '264 KB',  'N/A',       '264 KB',    '2 KB',      '2+128 KB'],
        ['Tasonmuunt.', '4×BSS138','Ei',        '245+2×BSS', 'EI',        'EI'],
        ['Nopeus',      '1-3 KB/s','~50 KB/s',  '10-20 KB/s','1-3 KB/s',  '1-3 KB/s'],
        ['C64-modi',    'Ei',      'ROM',       'Ajuri',     'Ei',        'Ei'],
        ['Komponent.',  '~12',     '~14',       '~12',       '~7',        '~9'],
        ['Luotettavuus','****',    '*****',     '****',      '**',        '***'],
    ]
    colors_header = ['#37474F'] + ['#E65100', '#C62828', '#7B1FA2', '#1565C0', '#F57F17']
    bg_header = ['#ECEFF1'] + ['#FFF3E0', '#FFEBEE', '#F3E5F5', '#E3F2FD', '#FFF9C4']

    y_start, row_h, col_w, x_start = 8.5, 0.7, 1.55, 0.5

    for j, h in enumerate(headers):
        x = x_start + j * col_w
        box = FancyBboxPatch((x, y_start), col_w-0.1, row_h, boxstyle="round,pad=0.05",
                              facecolor=bg_header[j], edgecolor=colors_header[j], linewidth=1.5)
        ax.add_patch(box)
        ax.text(x+col_w/2-0.05, y_start+row_h/2, h, ha='center', va='center',
                fontsize=8, fontweight='bold', color=colors_header[j])

    for i, row in enumerate(rows):
        y = y_start - (i+1) * row_h
        for j, cell in enumerate(row):
            x = x_start + j * col_w
            bg = '#FAFAFA' if i%2==0 else '#FFFFFF'
            if j == 0: bg = '#ECEFF1'
            box = FancyBboxPatch((x, y), col_w-0.1, row_h, boxstyle="round,pad=0.03",
                                  facecolor=bg, edgecolor='#BDBDBD', linewidth=0.5)
            ax.add_patch(box)
            weight = 'bold' if j == 0 else 'normal'
            color = '#333' if j > 0 else '#37474F'
            if cell == 'EI': color, weight = '#2E7D32', 'bold'
            ax.text(x+col_w/2-0.05, y+row_h/2, cell, ha='center', va='center',
                    fontsize=7.5, fontweight=weight, color=color)

    y_rec = y_start - (len(rows) + 1.5) * row_h
    box = FancyBboxPatch((1, y_rec), 8, 0.8, boxstyle="round,pad=0.1",
                          facecolor='#E8F5E9', edgecolor='#2E7D32', linewidth=2)
    ax.add_patch(box)
    ax.text(5, y_rec+0.4,
            'Suositus: A (IEC+Pico) — paras tasapaino luotettavuuden, nopeuden ja helppouden välillä',
            ha='center', va='center', fontsize=10, fontweight='bold', color='#1B5E20')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/vertailu.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


if __name__ == '__main__':
    print('Generating detailed schematics with pinouts...')
    generate_a()
    generate_b()
    generate_c()
    generate_d()
    generate_e()
    generate_comparison()
    print('Done!')
