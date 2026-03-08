#!/usr/bin/env python3
"""
Generate schematic diagrams for all 64korppu alternatives.
Uses schemdraw + matplotlib for circuit-style block diagrams.
"""

import schemdraw
import schemdraw.elements as elm
import schemdraw.logic as logic
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch
import os

DPI = 150
BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def draw_block(ax, x, y, w, h, label, color='#E8F0FE', edgecolor='#4A86C8',
               fontsize=9, sublabel=None, bold=True):
    """Draw a labeled block/box on a matplotlib axis."""
    box = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.1",
                          facecolor=color, edgecolor=edgecolor, linewidth=2)
    ax.add_patch(box)
    weight = 'bold' if bold else 'normal'
    ax.text(x + w/2, y + h/2 + (0.15 if sublabel else 0), label,
            ha='center', va='center', fontsize=fontsize, fontweight=weight)
    if sublabel:
        ax.text(x + w/2, y + h/2 - 0.2, sublabel,
                ha='center', va='center', fontsize=7, color='#555555')


def draw_arrow(ax, x1, y1, x2, y2, label='', color='#333333', style='->', lw=2):
    """Draw an arrow between two points."""
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle=style, color=color, lw=lw))
    if label:
        mx, my = (x1+x2)/2, (y1+y2)/2
        ax.text(mx, my + 0.15, label, ha='center', va='bottom',
                fontsize=7, color=color)


def draw_line(ax, x1, y1, x2, y2, color='#333333', lw=1.5, ls='-'):
    ax.plot([x1, x2], [y1, y2], color=color, lw=lw, ls=ls)


def draw_pin_table(ax, x, y, pins, title, fontsize=6):
    """Draw a pin assignment table."""
    ax.text(x, y, title, fontsize=7, fontweight='bold', va='top')
    for i, (pin, sig) in enumerate(pins):
        ax.text(x, y - 0.2 - i*0.15, f'{pin}: {sig}',
                fontsize=fontsize, va='top', family='monospace')


# ============================================================
# VAIHTOEHTO A: IEC + Raspberry Pi Pico
# ============================================================
def generate_a():
    fig, ax = plt.subplots(1, 1, figsize=(14, 8))
    ax.set_xlim(-1, 15)
    ax.set_ylim(-1, 9)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')

    ax.set_title('Vaihtoehto A: IEC-väylä + Raspberry Pi Pico',
                 fontsize=14, fontweight='bold', pad=20)

    # C64
    draw_block(ax, 0, 3, 2.5, 3, 'Commodore 64', '#FFF3E0', '#E65100', fontsize=11,
               sublabel='IEC-portti (6-pin DIN)')

    # Level shifter
    draw_block(ax, 3.8, 3.5, 2.2, 2, 'Level Shifter', '#F3E5F5', '#7B1FA2',
               sublabel='4× BSS138 MOSFET\n+ 8× 10kΩ pull-up')
    ax.text(5.9, 5.7, '5V ↔ 3.3V', fontsize=7, color='#7B1FA2', ha='center')

    # Pico
    draw_block(ax, 7.3, 2, 3, 5, 'Raspberry Pi\nPico', '#E8F5E9', '#2E7D32',
               fontsize=12, sublabel='RP2040\n264KB RAM\n2× PIO')

    # Floppy
    draw_block(ax, 11.8, 3, 2.8, 3, 'PC 3.5" HD\nFloppy', '#E3F2FD', '#1565C0',
               fontsize=11, sublabel='1.44 MB\nFAT12')

    # PSU
    draw_block(ax, 5, 0, 2.5, 1.2, '5V / 2A PSU', '#FBE9E7', '#BF360C',
               fontsize=9, sublabel='Ulkoinen virtalähde')

    # Arrows
    draw_arrow(ax, 2.5, 4.5, 3.8, 4.5, 'IEC-väylä\nATN, CLK, DATA', '#E65100')
    draw_arrow(ax, 6.0, 4.5, 7.3, 4.5, '3.3V signaalit', '#7B1FA2')
    draw_arrow(ax, 10.3, 4.5, 11.8, 4.5, '34-pin IDC\nShugart', '#1565C0')

    # Power lines
    draw_line(ax, 6.25, 1.2, 6.25, 2.0, '#BF360C', 1.5, '--')
    draw_line(ax, 6.25, 2.0, 8.8, 2.0, '#BF360C', 1.5, '--')
    draw_line(ax, 6.25, 1.2, 6.25, 1.5, '#BF360C', 1.5, '--')
    ax.text(7.5, 1.7, '+5V', fontsize=7, color='#BF360C')

    draw_line(ax, 7.5, 0.6, 13.2, 0.6, '#BF360C', 1.5, '--')
    draw_line(ax, 13.2, 0.6, 13.2, 3.0, '#BF360C', 1.5, '--')
    ax.text(10.5, 0.4, '+5V floppy', fontsize=7, color='#BF360C')

    # Pin tables
    draw_pin_table(ax, 0, 2.5, [
        ('Pin 3', 'ATN → GP2'),
        ('Pin 4', 'CLK → GP3'),
        ('Pin 5', 'DATA → GP4'),
        ('Pin 6', 'RESET → GP5'),
    ], 'IEC → Pico:', fontsize=6)

    draw_pin_table(ax, 11.8, 2.5, [
        ('GP6-GP13', 'Output signaalit'),
        ('GP14', '/TRK00 (input)'),
        ('GP15', '/WPT (input)'),
        ('GP16', '/RDATA (input)'),
        ('GP17', '/SIDE1 (output)'),
        ('GP18', '/DSKCHG (input)'),
    ], 'Pico → Floppy:', fontsize=6)

    # Firmware blocks inside Pico
    draw_block(ax, 7.5, 5.8, 1.3, 0.8, 'Core 0', '#C8E6C9', '#2E7D32',
               fontsize=7, sublabel='IEC + FAT12', bold=False)
    draw_block(ax, 9.0, 5.8, 1.3, 0.8, 'Core 1', '#C8E6C9', '#2E7D32',
               fontsize=7, sublabel='Floppy+MFM', bold=False)

    # Cost
    ax.text(7, 8.3, 'Hinta: ~12€  |  Nopeus: 1-3 KB/s (IEC)  |  Ei C64-modifiointia',
            fontsize=9, ha='center', style='italic', color='#555')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/A-IEC-Pico/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VAIHTOEHTO B: Expansion Port + FDC
# ============================================================
def generate_b():
    fig, ax = plt.subplots(1, 1, figsize=(14, 8))
    ax.set_xlim(-1, 15)
    ax.set_ylim(-1, 9)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')

    ax.set_title('Vaihtoehto B: Expansion Port + FDC-piiri',
                 fontsize=14, fontweight='bold', pad=20)

    # C64
    draw_block(ax, 0, 3, 2.5, 3, 'Commodore 64', '#FFF3E0', '#E65100', fontsize=11,
               sublabel='Expansion port\n(44-pin edge)')

    # Address decoder
    draw_block(ax, 3.5, 5, 2, 1.5, '74LS138\n+ 74LS04', '#FCE4EC', '#C62828',
               fontsize=8, sublabel='Osoitedekooderi')

    # Bus buffer
    draw_block(ax, 3.5, 3, 2, 1.5, '74LS245', '#E8EAF6', '#283593',
               fontsize=9, sublabel='Dataväylä buffer')

    # FDC
    draw_block(ax, 7, 2.5, 3, 3.5, '82077AA\nFDC', '#FFF9C4', '#F57F17',
               fontsize=12, sublabel='Floppy Disk\nController')

    # ROM
    draw_block(ax, 3.5, 0.5, 2, 1.5, '28C64\nEEPROM', '#E0F2F1', '#00695C',
               fontsize=8, sublabel='8KB custom ROM\n$8000-$9FFF')

    # Crystal
    draw_block(ax, 7.5, 0.5, 1.5, 1, '24 MHz', '#F5F5F5', '#616161',
               fontsize=8, sublabel='Kide')

    # Floppy
    draw_block(ax, 11.5, 3, 2.8, 3, 'PC 3.5" HD\nFloppy', '#E3F2FD', '#1565C0',
               fontsize=11, sublabel='1.44 MB\nFAT12')

    # Arrows
    draw_arrow(ax, 2.5, 5.5, 3.5, 5.5, 'A0-A7\n/IO1', '#E65100')
    draw_arrow(ax, 2.5, 3.8, 3.5, 3.8, 'D0-D7\nR/W', '#E65100')
    draw_arrow(ax, 5.5, 5.5, 7.0, 4.8, '/CS', '#C62828')
    draw_arrow(ax, 5.5, 3.8, 7.0, 3.8, 'DB0-DB7', '#283593')
    draw_arrow(ax, 10.0, 4.5, 11.5, 4.5, '34-pin IDC', '#1565C0')
    draw_arrow(ax, 2.5, 1.2, 3.5, 1.2, '/ROML\nA0-A12', '#E65100', lw=1.5)
    draw_line(ax, 8.25, 1.5, 8.25, 2.5, '#616161', 1.5, '--')
    ax.text(8.6, 2.0, 'CLK', fontsize=7, color='#616161')

    # Expansion port signals
    draw_pin_table(ax, 0, 2.5, [
        ('/IO1', '$DE00-$DEFF'),
        ('/ROML', '$8000-$9FFF'),
        ('D0-D7', 'Dataväylä'),
        ('A0-A15', 'Osoiteväylä'),
        ('R/W, Φ2', 'Ohjaussignaalit'),
        ('/EXROM', 'GND (cart mode)'),
    ], 'Expansion port:', fontsize=6)

    # FDC registers
    draw_pin_table(ax, 11.5, 2.5, [
        ('$DE04', 'MSR / DSR'),
        ('$DE05', 'Data (FIFO)'),
        ('$DE02', 'DOR'),
        ('$DE07', 'DIR / CCR'),
    ], 'FDC @ $DE00:', fontsize=6)

    ax.text(7, 8.3, 'Hinta: ~20-30€  |  Nopeus: kymmeniä KB/s  |  Vaatii custom ROM',
            fontsize=9, ha='center', style='italic', color='#555')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/B-Expansion-FDC/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VAIHTOEHTO C: User Port + Pico
# ============================================================
def generate_c():
    fig, ax = plt.subplots(1, 1, figsize=(14, 8))
    ax.set_xlim(-1, 15)
    ax.set_ylim(-1, 9)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')

    ax.set_title('Vaihtoehto C: User Port + Raspberry Pi Pico',
                 fontsize=14, fontweight='bold', pad=20)

    # C64
    draw_block(ax, 0, 3, 2.5, 3, 'Commodore 64', '#FFF3E0', '#E65100', fontsize=11,
               sublabel='User port\n(24-pin edge)')

    # 74LVC245
    draw_block(ax, 3.5, 4, 2.2, 1.8, '74LVC245', '#E8EAF6', '#283593',
               fontsize=9, sublabel='8-bit data bus\n5V ↔ 3.3V')

    # BSS138 handshake
    draw_block(ax, 3.5, 2, 2.2, 1.5, '2× BSS138', '#F3E5F5', '#7B1FA2',
               fontsize=8, sublabel='PA2 + /FLAG2\nhandshake')

    # Pico
    draw_block(ax, 7.3, 2, 3, 5, 'Raspberry Pi\nPico', '#E8F5E9', '#2E7D32',
               fontsize=12, sublabel='RP2040\n264KB RAM\n2× PIO')

    # Floppy
    draw_block(ax, 11.8, 3, 2.8, 3, 'PC 3.5" HD\nFloppy', '#E3F2FD', '#1565C0',
               fontsize=11, sublabel='1.44 MB\nFAT12')

    # PSU
    draw_block(ax, 5, 0, 2.5, 1.2, '5V / 2A PSU', '#FBE9E7', '#BF360C',
               fontsize=9, sublabel='Ulkoinen virtalähde')

    # Arrows
    draw_arrow(ax, 2.5, 5, 3.5, 5, 'PB0-PB7\n(8-bit data)', '#E65100')
    draw_arrow(ax, 2.5, 2.7, 3.5, 2.7, 'PA2, /FLAG2\n(strobe)', '#E65100')
    draw_arrow(ax, 5.7, 5, 7.3, 5, 'GP0-GP7\n(3.3V)', '#283593')
    draw_arrow(ax, 5.7, 2.7, 7.3, 2.7, 'GP8-GP9', '#7B1FA2')
    draw_arrow(ax, 10.3, 4.5, 11.8, 4.5, '34-pin IDC', '#1565C0')

    # DIR control
    ax.text(4.6, 6.0, 'DIR ← GP26', fontsize=7, color='#283593',
            ha='center', style='italic')

    # Firmware blocks
    draw_block(ax, 7.5, 5.8, 1.3, 0.8, 'Core 0', '#C8E6C9', '#2E7D32',
               fontsize=7, sublabel='Userport\n+FAT12', bold=False)
    draw_block(ax, 9.0, 5.8, 1.3, 0.8, 'Core 1', '#C8E6C9', '#2E7D32',
               fontsize=7, sublabel='Floppy+MFM', bold=False)

    # Power
    draw_line(ax, 6.25, 1.2, 8.8, 2.0, '#BF360C', 1.5, '--')
    draw_line(ax, 7.5, 0.6, 13.2, 0.6, '#BF360C', 1.5, '--')
    draw_line(ax, 13.2, 0.6, 13.2, 3.0, '#BF360C', 1.5, '--')

    draw_pin_table(ax, 0, 2.5, [
        ('PB0-PB7', '8-bit rinnakkaisdata'),
        ('PA2', 'Strobe C64→Pico'),
        ('/FLAG2', 'Strobe Pico→C64 (IRQ)'),
    ], 'User port:', fontsize=6)

    ax.text(7, 8.3, 'Hinta: ~12€  |  Nopeus: 10-20 KB/s  |  Expansion port vapaa',
            fontsize=9, ha='center', style='italic', color='#555')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/C-Userport-Pico/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VAIHTOEHTO D: IEC + Arduino Nano
# ============================================================
def generate_d():
    fig, ax = plt.subplots(1, 1, figsize=(14, 8))
    ax.set_xlim(-1, 15)
    ax.set_ylim(-1, 9)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')

    ax.set_title('Vaihtoehto D: IEC-väylä + Arduino Nano',
                 fontsize=14, fontweight='bold', pad=20)

    # C64
    draw_block(ax, 0, 3, 2.5, 3, 'Commodore 64', '#FFF3E0', '#E65100', fontsize=11,
               sublabel='IEC-portti (6-pin DIN)')

    # Direct connection box (no level shifter!)
    draw_block(ax, 3.5, 3.8, 2.5, 1.5, 'SUORA\nKYTKENTÄ', '#E8F5E9', '#388E3C',
               fontsize=9, sublabel='5V ↔ 5V\nEi tasonmuuntimia!')

    # 1k resistors
    ax.text(4.75, 3.5, '4× 1kΩ suojavastus', fontsize=7, ha='center',
            color='#388E3C', style='italic')

    # Nano
    draw_block(ax, 7.3, 2, 3, 5, 'Arduino\nNano', '#E3F2FD', '#1565C0',
               fontsize=12, sublabel='ATmega328P\n16 MHz, 5V\n2 KB RAM\n32 KB Flash')

    # Floppy
    draw_block(ax, 11.8, 3, 2.8, 3, 'PC 3.5" HD\nFloppy', '#E3F2FD', '#1565C0',
               fontsize=11, sublabel='1.44 MB\nFAT12')

    # PSU
    draw_block(ax, 5, 0, 2.5, 1.2, '5V / 2A PSU', '#FBE9E7', '#BF360C',
               fontsize=9, sublabel='Ulkoinen virtalähde')

    # Arrows
    draw_arrow(ax, 2.5, 4.5, 3.5, 4.5, 'IEC-väylä\nATN, CLK, DATA', '#E65100')
    draw_arrow(ax, 6.0, 4.5, 7.3, 4.5, 'D2-D5\n(5V suora!)', '#388E3C')
    draw_arrow(ax, 10.3, 4.5, 11.8, 4.5, '34-pin IDC\n(5V suora!)', '#1565C0')

    # Power
    draw_line(ax, 6.25, 1.2, 8.8, 2.0, '#BF360C', 1.5, '--')
    draw_line(ax, 7.5, 0.6, 13.2, 0.6, '#BF360C', 1.5, '--')
    draw_line(ax, 13.2, 0.6, 13.2, 3.0, '#BF360C', 1.5, '--')

    # Highlight: no level shifters
    box = FancyBboxPatch((3.2, 7.2), 7.5, 0.7, boxstyle="round,pad=0.1",
                          facecolor='#C8E6C9', edgecolor='#2E7D32', linewidth=2)
    ax.add_patch(box)
    ax.text(6.95, 7.55, '5V natiivi — ei yhtään tasonmuunninta koko piirissä!',
            fontsize=10, ha='center', fontweight='bold', color='#1B5E20')

    # Pin tables
    draw_pin_table(ax, 0, 2.5, [
        ('D2 (INT0)', 'ATN'),
        ('D3', 'CLK'),
        ('D4', 'DATA'),
        ('D5', 'RESET'),
    ], 'IEC → Nano:', fontsize=6)

    draw_pin_table(ax, 11.8, 2.5, [
        ('D6-D13', 'Output-signaalit'),
        ('D8 (ICP1)', '/RDATA (MFM-luku!)'),
        ('A0-A2,A4', 'Input-signaalit'),
        ('A3', '/SIDE1'),
    ], 'Nano → Floppy:', fontsize=6)

    # Warning
    ax.text(8.8, 1.3, 'HUOM: vain 2 KB RAM!\nSektori kerrallaan -dekoodaus',
            fontsize=7, color='#BF360C', ha='center', style='italic',
            bbox=dict(boxstyle='round', facecolor='#FBE9E7', edgecolor='#BF360C'))

    ax.text(7, 8.3, 'Hinta: ~8€  |  Nopeus: 1-3 KB/s  |  Yksinkertaisin kytkentä',
            fontsize=9, ha='center', style='italic', color='#555')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/D-IEC-Nano/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VAIHTOEHTO E: IEC + Arduino Nano + SPI SRAM
# ============================================================
def generate_e():
    fig, ax = plt.subplots(1, 1, figsize=(16, 9))
    ax.set_xlim(-1, 17)
    ax.set_ylim(-1, 10)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.patch.set_facecolor('white')

    ax.set_title('Vaihtoehto E: IEC + Arduino Nano + 23LC1024 SPI SRAM',
                 fontsize=14, fontweight='bold', pad=20)

    # C64
    draw_block(ax, 0, 4, 2.5, 3, 'Commodore 64', '#FFF3E0', '#E65100', fontsize=11,
               sublabel='IEC-portti (6-pin DIN)')

    # Direct connection
    draw_block(ax, 3.5, 4.5, 2.2, 1.5, 'SUORA\nKYTKENTÄ', '#E8F5E9', '#388E3C',
               fontsize=9, sublabel='5V ↔ 5V')
    ax.text(4.6, 4.2, '4× 1kΩ', fontsize=7, ha='center', color='#388E3C')

    # Nano
    draw_block(ax, 7, 3, 3.2, 5, 'Arduino\nNano', '#E3F2FD', '#1565C0',
               fontsize=12, sublabel='ATmega328P\n16 MHz, 5V')

    # SPI SRAM
    draw_block(ax, 7.3, 0.5, 2.5, 1.8, '23LC1024', '#FFF9C4', '#F57F17',
               fontsize=10, sublabel='128 KB SPI SRAM\n5V, DIP-8')

    # 74HC595
    draw_block(ax, 11.5, 1, 2.5, 1.8, '74HC595', '#F3E5F5', '#7B1FA2',
               fontsize=10, sublabel='8-bit shift register\nfloppy outputs')

    # Floppy
    draw_block(ax, 13, 4, 2.8, 3, 'PC 3.5" HD\nFloppy', '#E3F2FD', '#1565C0',
               fontsize=11, sublabel='1.44 MB\nFAT12')

    # PSU
    draw_block(ax, 3.5, 0.5, 2.5, 1.2, '5V / 2A PSU', '#FBE9E7', '#BF360C',
               fontsize=9, sublabel='Ulkoinen virtalähde')

    # IEC arrows
    draw_arrow(ax, 2.5, 5.5, 3.5, 5.5, 'IEC-väylä', '#E65100')
    draw_arrow(ax, 5.7, 5.5, 7.0, 5.5, 'D2-D5', '#388E3C')

    # SPI bus
    draw_arrow(ax, 8.6, 3.0, 8.6, 2.3, 'SPI\nMOSI,MISO\nSCK,/CS', '#F57F17')

    # SPI to 595
    draw_line(ax, 9.5, 1.5, 11.5, 1.5, '#7B1FA2', 2)
    ax.text(10.5, 1.8, 'SPI (jaettu)\nMOSI + SCK', fontsize=7, ha='center',
            color='#7B1FA2')

    # 595 to floppy
    draw_arrow(ax, 14.0, 2.8, 14.0, 4.0, '8 output-\nsignaalia', '#7B1FA2')

    # Direct floppy signals
    draw_arrow(ax, 10.2, 5.5, 13.0, 5.5, 'D7: /WDATA\nD8: /RDATA (ICP1)', '#1565C0')

    # Floppy inputs
    draw_arrow(ax, 10.2, 4.2, 13.0, 4.2, 'A0-A2:\n/TRK00, /WPT, /DSKCHG', '#1565C0',
               lw=1.5)

    # Power
    draw_line(ax, 4.75, 1.7, 4.75, 3.0, '#BF360C', 1.5, '--')
    draw_line(ax, 4.75, 1.7, 7.0, 3.0, '#BF360C', 1.5, '--')

    # 5V highlight
    box = FancyBboxPatch((2.5, 8.5), 11, 0.7, boxstyle="round,pad=0.1",
                          facecolor='#C8E6C9', edgecolor='#2E7D32', linewidth=2)
    ax.add_patch(box)
    ax.text(8, 8.85, 'Kaikki 5V — Nano + 23LC1024 + 74HC595 + Floppy — ei tasonmuuntimia!',
            fontsize=10, ha='center', fontweight='bold', color='#1B5E20')

    # Memory map
    draw_block(ax, 0, 0.5, 2.5, 2.5, 'SRAM\nmuistikartta', '#FFF9C4', '#F57F17',
               fontsize=8, bold=False)
    mem_items = [
        'MFM-raita: 12.5 KB',
        'FAT-cache: 4.5 KB',
        'Sektorit: 1 KB',
        'IEC-puskuri: 512 B',
        'Vapaa: ~105 KB',
    ]
    for i, item in enumerate(mem_items):
        ax.text(0.15, 2.5 - i*0.17, item, fontsize=5.5, va='top', family='monospace')

    # SPI bus detail
    draw_block(ax, 7.5, 7, 4, 1.2, 'SPI-väylän jakaminen', '#ECEFF1', '#455A64',
               fontsize=8, bold=True)
    ax.text(9.5, 7.25, 'D10=/CS(SRAM)  D11=MOSI  D12=MISO  D13=SCK  D6=595 LATCH',
            fontsize=6, ha='center', family='monospace', color='#37474F')

    ax.text(8, 9.6, 'Hinta: ~10-13€  |  128KB SRAM  |  5V natiivi  |  IEC 1-3 KB/s',
            fontsize=9, ha='center', style='italic', color='#555')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/E-IEC-Nano-SRAM/img/piirikaavio.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


# ============================================================
# VERTAILUKAAVIO
# ============================================================
def generate_comparison():
    fig, ax = plt.subplots(1, 1, figsize=(14, 8))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis('off')
    fig.patch.set_facecolor('white')

    ax.set_title('64korppu — Vaihtoehtojen vertailu',
                 fontsize=16, fontweight='bold', pad=20)

    # Table data
    headers = ['', 'A: IEC+Pico', 'B: Exp+FDC', 'C: User+Pico', 'D: IEC+Nano', 'E: Nano+SRAM']
    rows = [
        ['Hinta',       '~12€',    '~20-30€',   '~12€',      '~8€',       '~10-13€'],
        ['RAM',         '264 KB',  'N/A',       '264 KB',    '2 KB',      '2+128 KB'],
        ['Tasonmuunt.', '4×BSS138','Ei',        '245+2×BSS', 'EI',        'EI'],
        ['Nopeus',      '1-3 KB/s','~50 KB/s',  '10-20 KB/s','1-3 KB/s',  '1-3 KB/s'],
        ['C64-modi',    'Ei',      'ROM',       'Ajuri',     'Ei',        'Ei'],
        ['Komponent.',  '~12',     '~14',       '~12',       '~7',        '~9'],
        ['Luotettavuus','★★★★',    '★★★★★',     '★★★★',      '★★',        '★★★'],
    ]

    colors_header = ['#37474F'] + ['#E65100', '#C62828', '#7B1FA2', '#1565C0', '#F57F17']
    bg_header = ['#ECEFF1'] + ['#FFF3E0', '#FFEBEE', '#F3E5F5', '#E3F2FD', '#FFF9C4']

    y_start = 8.5
    row_h = 0.7
    col_w = 1.55
    x_start = 0.5

    # Headers
    for j, h in enumerate(headers):
        x = x_start + j * col_w
        box = FancyBboxPatch((x, y_start), col_w - 0.1, row_h,
                              boxstyle="round,pad=0.05",
                              facecolor=bg_header[j], edgecolor=colors_header[j],
                              linewidth=1.5)
        ax.add_patch(box)
        ax.text(x + col_w/2 - 0.05, y_start + row_h/2, h,
                ha='center', va='center', fontsize=8, fontweight='bold',
                color=colors_header[j])

    # Rows
    for i, row in enumerate(rows):
        y = y_start - (i + 1) * row_h
        for j, cell in enumerate(row):
            x = x_start + j * col_w
            bg = '#FAFAFA' if i % 2 == 0 else '#FFFFFF'
            if j == 0:
                bg = '#ECEFF1'
            box = FancyBboxPatch((x, y), col_w - 0.1, row_h,
                                  boxstyle="round,pad=0.03",
                                  facecolor=bg, edgecolor='#BDBDBD',
                                  linewidth=0.5)
            ax.add_patch(box)
            weight = 'bold' if j == 0 else 'normal'
            color = '#333' if j > 0 else '#37474F'
            # Highlight "EI" (no level shifters)
            if cell == 'EI':
                color = '#2E7D32'
                weight = 'bold'
            ax.text(x + col_w/2 - 0.05, y + row_h/2, cell,
                    ha='center', va='center', fontsize=7.5,
                    fontweight=weight, color=color)

    # Recommendation
    y_rec = y_start - (len(rows) + 1.5) * row_h
    box = FancyBboxPatch((1, y_rec), 8, 0.8, boxstyle="round,pad=0.1",
                          facecolor='#E8F5E9', edgecolor='#2E7D32', linewidth=2)
    ax.add_patch(box)
    ax.text(5, y_rec + 0.4,
            'Suositus: A (IEC+Pico) — paras tasapaino luotettavuuden, nopeuden ja helppouden välillä',
            ha='center', va='center', fontsize=10, fontweight='bold', color='#1B5E20')

    plt.tight_layout()
    path = os.path.join(BASE, 'docs/vertailu.png')
    fig.savefig(path, dpi=DPI, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f'  Saved: {path}')


if __name__ == '__main__':
    print('Generating schematics...')
    generate_a()
    generate_b()
    generate_c()
    generate_d()
    generate_e()
    generate_comparison()
    print('Done!')
