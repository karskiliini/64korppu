#!/usr/bin/env python3
"""
Render 3D-style visualization of PCB variants with components.
Shows top view and isometric perspective with component bodies.

Renders both 2-layer and 4-layer variants.
"""
import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.patches import FancyBboxPatch
import numpy as np


# ── Board configs ──────────────────────────────────────────────────────

BOARDS = {
    '4-layer': {
        'title': '64korppu-E — 4-Layer PCB',
        'output': 'hardware/E-IEC-Nano-SRAM/4-layer/64korppu-E-4layer-3d.png',
        'board': (100, 100, 170, 160),  # x1, y1, x2, y2
        'layers': '4-layer: F.Cu / GND / +5V / B.Cu',
        'components': [
            # Mounting holes
            ('H1', 101, 101, 105, 105, '', '#888888', 0),
            ('H2', 165, 101, 169, 105, '', '#888888', 0),
            ('H3', 101, 155, 105, 159, '', '#888888', 0),
            ('H4', 165, 155, 169, 159, '', '#888888', 0),
            # Power
            ('J3', 106, 100.5, 120, 113, 'DC 5V', '#2a2a2a', 11),
            ('C4', 124, 101.5, 129, 106.5, '10µF', '#CD853F', 8),
            ('D1', 130, 102, 134, 106, 'LED', '#00CC00', 5),
            ('R8', 135.5, 102.5, 148.5, 105.5, '330Ω', '#DEB887', 2.5),
            # IEC series resistors
            ('R1', 106.5, 114.5, 119.5, 117.5, '1kΩ', '#DEB887', 2.5),
            ('R2', 106.5, 118.5, 119.5, 121.5, '1kΩ', '#DEB887', 2.5),
            ('R3', 106.5, 122.5, 119.5, 125.5, '1kΩ', '#DEB887', 2.5),
            ('R4', 106.5, 126.5, 119.5, 129.5, '1kΩ', '#DEB887', 2.5),
            # IEC pull-up resistors
            ('R5', 106.5, 130.5, 119.5, 133.5, '4.7kΩ', '#DEB887', 2.5),
            ('R6', 106.5, 134.5, 119.5, 137.5, '4.7kΩ', '#DEB887', 2.5),
            ('R7', 106.5, 138.5, 119.5, 141.5, '4.7kΩ', '#DEB887', 2.5),
            # DIN-6 IEC connector
            ('J1', 106, 143, 122, 159, 'IEC\nDIN-6', '#333333', 15),
            # Arduino Nano
            ('U1', 133, 109.5, 140, 147.5, 'Arduino\nNano', '#0066CC', 10),
            # Floppy pull-up resistors
            ('R9',  146.5, 106.5, 159.5, 109.5, '10kΩ', '#DEB887', 2.5),
            ('R10', 146.5, 111.5, 159.5, 114.5, '10kΩ', '#DEB887', 2.5),
            ('R11', 146.5, 116.5, 159.5, 119.5, '10kΩ', '#DEB887', 2.5),
            ('R12', 146.5, 121.5, 159.5, 124.5, '10kΩ', '#DEB887', 2.5),
            # Floppy IDC connector
            ('J2', 160.5, 108, 169.5, 152, 'Floppy\nIDC\n2x17', '#444444', 10),
            # SRAM DIP-8
            ('U2', 123.5, 148.5, 134, 159, 'SRAM\n23LC1024', '#333333', 5),
            # 74HC595 DIP-16
            ('U3', 146.5, 130.5, 157, 152, '74HC595', '#333333', 5),
            # Bypass caps
            ('C1', 135, 148.5, 142, 153.5, '100nF', '#CD853F', 3),
            ('C2', 135, 154.5, 142, 159.5, '100nF', '#CD853F', 3),
            ('C3', 147, 153.5, 154, 158.5, '100nF', '#CD853F', 3),
        ],
        'test_points': [
            ('TP1',  103, 108.0, 'GND'),
            ('TP2',  103, 110.5, '+5V'),
            ('TP3',  103, 113.0, 'ATN'),
            ('TP4',  103, 115.5, 'CLK'),
            ('TP5',  103, 118.0, 'DATA'),
            ('TP6',  103, 120.5, 'MOTOR'),
            ('TP7',  103, 123.0, 'STEP'),
            ('TP8',  103, 125.5, 'RDATA'),
            ('TP9',  103, 128.0, 'WDATA'),
            ('TP10', 103, 130.5, 'SIDE1'),
            ('TP11', 103, 133.0, 'SCK'),
            ('TP12', 103, 135.5, 'MOSI'),
            ('TP13', 103, 138.0, 'MISO'),
            ('TP14', 103, 140.5, 'CS'),
        ],
    },
    '2-layer': {
        'title': '64korppu-E — 2-Layer PCB',
        'output': 'hardware/E-IEC-Nano-SRAM/2-layer/64korppu-E-2layer-3d.png',
        'board': (100, 100, 240, 220),  # 140x120mm
        'layers': '2-layer: F.Cu / B.Cu',
        'components': [
            # Mounting holes
            ('H1', 101, 101, 105, 105, '', '#888888', 0),
            ('H2', 235, 101, 239, 105, '', '#888888', 0),
            ('H3', 101, 215, 105, 219, '', '#888888', 0),
            ('H4', 235, 215, 239, 219, '', '#888888', 0),
            # Power
            ('J3', 157, 103.5, 171, 116, 'DC 5V', '#2a2a2a', 11),
            ('C4', 179, 105.5, 184, 110.5, '10µF', '#CD853F', 8),
            # IEC series resistors
            ('R1', 120.5, 120.5, 133.5, 123.5, '1kΩ', '#DEB887', 2.5),
            ('R2', 120.5, 127.5, 133.5, 130.5, '1kΩ', '#DEB887', 2.5),
            ('R3', 120.5, 134.5, 133.5, 137.5, '1kΩ', '#DEB887', 2.5),
            ('R4', 120.5, 141.5, 133.5, 144.5, '1kΩ', '#DEB887', 2.5),
            # IEC pull-up resistors
            ('R5', 138.5, 120.5, 151.5, 123.5, '4.7kΩ', '#DEB887', 2.5),
            ('R6', 138.5, 127.5, 151.5, 130.5, '4.7kΩ', '#DEB887', 2.5),
            ('R7', 138.5, 134.5, 151.5, 137.5, '4.7kΩ', '#DEB887', 2.5),
            # DIN-6 IEC connector
            ('J1', 110, 150, 126, 166, 'IEC\nDIN-6', '#333333', 15),
            # Arduino Nano
            ('U1', 163, 123.5, 170, 161.5, 'Arduino\nNano', '#0066CC', 10),
            # Floppy pull-up resistors
            ('R9',  198.5, 126.5, 211.5, 129.5, '10kΩ', '#DEB887', 2.5),
            ('R10', 198.5, 134.5, 211.5, 137.5, '10kΩ', '#DEB887', 2.5),
            ('R11', 198.5, 142.5, 211.5, 145.5, '10kΩ', '#DEB887', 2.5),
            ('R12', 198.5, 150.5, 211.5, 153.5, '10kΩ', '#DEB887', 2.5),
            # Floppy IDC connector
            ('J2', 220.5, 126, 229.5, 170, 'Floppy\nIDC\n2x17', '#444444', 10),
            # SRAM DIP-8
            ('U2', 126.5, 170.5, 137, 181, 'SRAM\n23LC1024', '#333333', 5),
            # 74HC595 DIP-16
            ('U3', 193.5, 170.5, 204, 192, '74HC595', '#333333', 5),
            # LED + R8
            ('D1', 113, 196, 117, 200, 'LED', '#00CC00', 5),
            ('R8', 124.5, 196.5, 137.5, 199.5, '330Ω', '#DEB887', 2.5),
            # Bypass caps
            ('C1', 154, 195.5, 161, 200.5, '100nF', '#CD853F', 3),
            ('C2', 174, 195.5, 181, 200.5, '100nF', '#CD853F', 3),
            ('C3', 194, 195.5, 201, 200.5, '100nF', '#CD853F', 3),
        ],
        'test_points': [],
    },
}


# ── Drawing functions ──────────────────────────────────────────────────

def draw_top_view(ax, board_cfg):
    """Draw top-down view of the PCB with components."""
    bx1, by1, bx2, by2 = board_cfg['board']
    bw, bh = bx2 - bx1, by2 - by1

    # PCB board (green)
    board = FancyBboxPatch(
        (bx1, by1), bw, bh,
        boxstyle="round,pad=0.5",
        facecolor='#1a5c1a', edgecolor='#0d3d0d', linewidth=2)
    ax.add_patch(board)

    # Copper traces (subtle)
    for x in range(int(bx1) + 2, int(bx2) - 1, 4):
        ax.plot([x, x], [by1 + 1, by2 - 1], color='#2a7a2a', linewidth=0.3, alpha=0.5)

    # Components
    for ref, x1, y1, x2, y2, label, color, height in board_cfg['components']:
        w = x2 - x1
        h = y2 - y1
        cx = (x1 + x2) / 2
        cy = (y1 + y2) / 2

        if ref.startswith('H'):
            circle = plt.Circle((cx, cy), 3, facecolor='#666666',
                                edgecolor='#444444', linewidth=1.5)
            ax.add_patch(circle)
            inner = plt.Circle((cx, cy), 1.6, facecolor='#1a5c1a',
                               edgecolor='#444444', linewidth=0.5)
            ax.add_patch(inner)
            continue

        rect = FancyBboxPatch(
            (x1, y1), w, h,
            boxstyle="round,pad=0.3",
            facecolor=color, edgecolor='#111111', linewidth=0.8,
            alpha=0.9)
        ax.add_patch(rect)

        if label:
            fontsize = 5 if len(label) > 6 else 6
            ax.text(cx, cy, label,
                    ha='center', va='center', fontsize=fontsize,
                    color='white' if color in ('#333333', '#444444', '#2a2a2a', '#0066CC') else '#333333',
                    fontweight='bold')

        ax.text(cx, y1 - 1.5, ref,
                ha='center', va='top', fontsize=4,
                color='#FFD700', fontweight='bold')

    # Test points
    for ref, x, y, sig in board_cfg.get('test_points', []):
        circle = plt.Circle((x, y), 0.7, facecolor='#FFD700',
                            edgecolor='#B8860B', linewidth=0.5)
        ax.add_patch(circle)
        ax.text(x - 1.5, y, sig, ha='right', va='center',
                fontsize=3.5, color='#FFD700', fontweight='bold')

    if board_cfg.get('test_points'):
        tp_y = (board_cfg['test_points'][0][2] + board_cfg['test_points'][-1][2]) / 2
        ax.text(bx1 - 7, tp_y, 'TEST\nPOINTS\n(B.Cu)', ha='center', va='center',
                fontsize=4, color='#FFD700', fontstyle='italic', rotation=90)

    margin = 7
    ax.set_xlim(bx1 - margin, bx2 + margin)
    ax.set_ylim(by1 - 4, by2 + 5)
    ax.set_aspect('equal')
    ax.set_title(f"{board_cfg['title']} (ylhäältä)", fontsize=12, fontweight='bold', pad=10)
    ax.set_facecolor('#0a0a0a')
    ax.axis('off')


def draw_isometric(ax, board_cfg):
    """Draw isometric 3D perspective."""
    bx1, by1, bx2, by2 = board_cfg['board']
    bcx = (bx1 + bx2) / 2
    bcy = (by1 + by2) / 2
    bw = bx2 - bx1

    # Scale factor: larger boards need more scaling
    scale = 0.8 * (70 / bw)  # normalize to 4-layer board size

    def iso(x, y, z=0):
        x = (x - bcx) * scale
        y = (y - bcy) * scale
        ix = x - y * 0.5
        iy = -x * 0.3 - y * 0.5 + z * 0.8
        return ix, iy

    # Board outline
    corners_3d = [(bx1, by1), (bx2, by1), (bx2, by2), (bx1, by2)]
    board_top = [iso(x, y, 0) for x, y in corners_3d]
    board_bot = [iso(x, y, -2) for x, y in corners_3d]

    # Board sides
    side = plt.Polygon([board_top[0], board_top[1], board_bot[1], board_bot[0]],
                       facecolor='#0d3d0d', edgecolor='#0a2a0a', linewidth=1)
    ax.add_patch(side)
    side2 = plt.Polygon([board_top[1], board_top[2], board_bot[2], board_bot[1]],
                        facecolor='#0e4e0e', edgecolor='#0a2a0a', linewidth=1)
    ax.add_patch(side2)
    # Board top
    board_poly = plt.Polygon(board_top, facecolor='#1a5c1a',
                             edgecolor='#0d3d0d', linewidth=2)
    ax.add_patch(board_poly)

    # Draw components as 3D blocks (back-to-front)
    for ref, x1, y1, x2, y2, label, color, height in sorted(
            board_cfg['components'], key=lambda c: -(c[2] + c[4]) / 2):
        if ref.startswith('H') or height == 0:
            continue

        cx = (x1 + x2) / 2
        cy = (y1 + y2) / 2
        corners = [(x1, y1), (x2, y1), (x2, y2), (x1, y2)]

        z = height
        top = [iso(px, py, z) for px, py in corners]
        base = [iso(px, py, 0) for px, py in corners]

        # Front face
        front = plt.Polygon([base[0], base[1], top[1], top[0]],
                            facecolor=color, edgecolor='#111111',
                            linewidth=0.5, alpha=0.85)
        ax.add_patch(front)

        # Right face (darker)
        r, g, b = int(color[1:3], 16), int(color[3:5], 16), int(color[5:7], 16)
        darker = f'#{max(0,r-40):02x}{max(0,g-40):02x}{max(0,b-40):02x}'
        right = plt.Polygon([base[1], base[2], top[2], top[1]],
                            facecolor=darker, edgecolor='#111111',
                            linewidth=0.5, alpha=0.85)
        ax.add_patch(right)

        # Top face (lighter)
        lighter = f'#{min(255,r+30):02x}{min(255,g+30):02x}{min(255,b+30):02x}'
        top_face = plt.Polygon(top, facecolor=lighter,
                               edgecolor='#111111', linewidth=0.5, alpha=0.85)
        ax.add_patch(top_face)

        # Label on top
        if label:
            center = iso(cx, cy, z + 0.5)
            fontsize = 4 if len(label) > 6 else 5
            ax.text(center[0], center[1], label.split('\n')[0],
                    ha='center', va='center', fontsize=fontsize,
                    color='white' if color in ('#333333', '#444444', '#2a2a2a', '#0066CC') else '#333333',
                    fontweight='bold')

    ax.set_xlim(-45, 45)
    ax.set_ylim(-35, 25)
    ax.set_aspect('equal')
    ax.set_title(f"{board_cfg['title']} — isometrinen", fontsize=12, fontweight='bold', pad=10)
    ax.set_facecolor('#0a0a0a')
    ax.axis('off')


# ── Render both boards ─────────────────────────────────────────────────

for name, cfg in BOARDS.items():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(20, 10))
    fig.patch.set_facecolor('#0a0a0a')

    draw_top_view(ax1, cfg)
    draw_isometric(ax2, cfg)

    plt.tight_layout(pad=2)
    plt.savefig(cfg['output'], dpi=200, bbox_inches='tight',
                facecolor='#0a0a0a', edgecolor='none')
    print(f"Saved: {cfg['output']}")
    plt.close()
