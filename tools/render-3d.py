#!/usr/bin/env python3
"""
Render 3D-style visualization of the 4-layer PCB with components.
Shows top view and isometric perspective with component bodies.
"""
import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.patches import FancyBboxPatch
import numpy as np

# Board dimensions (mm)
BOARD_X, BOARD_Y = 100, 100
BOARD_W, BOARD_H = 70, 60

# Component positions and sizes: (ref, x, y, w, h, label, color, height_mm)
COMPONENTS = [
    # Mounting holes
    ('H1', 103, 103, 6, 6, '', '#888888', 0),
    ('H2', 167, 103, 6, 6, '', '#888888', 0),
    ('H3', 103, 157, 6, 6, '', '#888888', 0),
    ('H4', 167, 157, 6, 6, '', '#888888', 0),
    # Power
    ('J3', 111, 105, 14, 8, 'DC 5V', '#2a2a2a', 11),
    ('C4', 125, 104, 5, 5, '10µF', '#CD853F', 8),
    # LED + resistor
    ('D1', 132, 104, 4, 4, 'LED', '#00CC00', 5),
    ('R8', 137, 104, 10, 3, '330Ω', '#DEB887', 2.5),
    # IEC resistors (left column)
    ('R1', 108, 116, 10, 3, '1kΩ', '#DEB887', 2.5),
    ('R2', 108, 120, 10, 3, '1kΩ', '#DEB887', 2.5),
    ('R3', 108, 124, 10, 3, '1kΩ', '#DEB887', 2.5),
    ('R4', 108, 128, 10, 3, '1kΩ', '#DEB887', 2.5),
    ('R5', 108, 132, 10, 3, '4.7kΩ', '#DEB887', 2.5),
    ('R6', 108, 136, 10, 3, '4.7kΩ', '#DEB887', 2.5),
    ('R7', 108, 140, 10, 3, '4.7kΩ', '#DEB887', 2.5),
    # DIN-6 IEC connector
    ('J1', 114, 151, 16, 16, 'IEC\nDIN-6', '#333333', 15),
    # Arduino Nano (center)
    ('U1', 135, 111, 7.6, 36.5, 'Arduino\nNano', '#0066CC', 10),
    # Floppy pull-ups
    ('R9',  148, 108, 10, 3, '10kΩ', '#DEB887', 2.5),
    ('R10', 148, 113, 10, 3, '10kΩ', '#DEB887', 2.5),
    ('R11', 148, 118, 10, 3, '10kΩ', '#DEB887', 2.5),
    ('R12', 148, 123, 10, 3, '10kΩ', '#DEB887', 2.5),
    # Floppy IDC connector
    ('J2', 165, 110, 9, 42, 'Floppy\nIDC\n2x17', '#444444', 10),
    # ICs
    ('U2', 125, 150, 9, 9, 'SRAM\n23LC1024', '#333333', 5),
    ('U3', 148, 132, 9, 20, '74HC595', '#333333', 5),
    # Bypass caps
    ('C1', 136, 151, 5, 5, '100nF', '#CD853F', 3),
    ('C2', 136, 157, 5, 5, '100nF', '#CD853F', 3),
    ('C3', 148, 156, 5, 5, '100nF', '#CD853F', 3),
]

# Test points (on back side, shown as small circles)
TEST_POINTS = [
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
]


def draw_top_view(ax):
    """Draw top-down view of the PCB with components."""
    # PCB board (green)
    board = FancyBboxPatch(
        (BOARD_X, BOARD_Y), BOARD_W, BOARD_H,
        boxstyle="round,pad=0.5",
        facecolor='#1a5c1a', edgecolor='#0d3d0d', linewidth=2)
    ax.add_patch(board)

    # Copper traces (subtle)
    for x in range(102, 169, 4):
        ax.plot([x, x], [101, 159], color='#2a7a2a', linewidth=0.3, alpha=0.5)

    # Components
    for ref, x, y, w, h, label, color, height in COMPONENTS:
        if ref.startswith('H'):
            # Mounting holes
            circle = plt.Circle((x, y), 3, facecolor='#666666',
                                edgecolor='#444444', linewidth=1.5)
            ax.add_patch(circle)
            inner = plt.Circle((x, y), 1.6, facecolor='#1a5c1a',
                               edgecolor='#444444', linewidth=0.5)
            ax.add_patch(inner)
            continue

        # Component body
        rect = FancyBboxPatch(
            (x - w/2, y - h/2 if h < 10 else y), w, h,
            boxstyle="round,pad=0.3",
            facecolor=color, edgecolor='#111111', linewidth=0.8,
            alpha=0.9)
        ax.add_patch(rect)

        # Label
        if label:
            cy = y if h < 10 else y + h/2
            fontsize = 5 if len(label) > 6 else 6
            ax.text(x + w/2 if ref.startswith('R') else x + w/2 - w/2,
                    cy, label,
                    ha='center', va='center', fontsize=fontsize,
                    color='white' if color in ('#333333', '#444444', '#2a2a2a', '#0066CC') else '#333333',
                    fontweight='bold')

        # Reference designator
        if not ref.startswith('H'):
            ref_y = y - h/2 - 1.5 if h < 10 else y - 1.5
            ax.text(x + w/2 if ref.startswith('R') else x,
                    ref_y, ref,
                    ha='center', va='top', fontsize=4,
                    color='#FFD700', fontweight='bold')

    # Test point indicators (small golden circles on left edge)
    for ref, x, y, sig in TEST_POINTS:
        circle = plt.Circle((x, y), 0.7, facecolor='#FFD700',
                            edgecolor='#B8860B', linewidth=0.5)
        ax.add_patch(circle)
        ax.text(x - 1.5, y, sig, ha='right', va='center',
                fontsize=3.5, color='#FFD700', fontweight='bold')

    # Test point label
    ax.text(98, 124, 'TEST\nPOINTS\n(B.Cu)', ha='center', va='center',
            fontsize=4, color='#FFD700', fontstyle='italic', rotation=90)

    ax.set_xlim(93, 175)
    ax.set_ylim(96, 165)
    ax.set_aspect('equal')
    ax.set_title('64korppu-E — 4-Layer PCB (ylhäältä)', fontsize=12, fontweight='bold', pad=10)
    ax.set_facecolor('#0a0a0a')
    ax.axis('off')


def draw_isometric(ax):
    """Draw isometric 3D perspective."""
    # Isometric transformation
    def iso(x, y, z=0):
        """Convert 3D coordinates to 2D isometric."""
        # Scale down from PCB coordinates
        x = (x - 135) * 0.8  # center around 135
        y = (y - 130) * 0.8
        ix = x - y * 0.5
        iy = -x * 0.3 - y * 0.5 + z * 0.8
        return ix, iy

    # Board outline
    corners_3d = [(100, 100), (170, 100), (170, 160), (100, 160)]
    board_top = [iso(x, y, 0) for x, y in corners_3d]
    board_bot = [iso(x, y, -2) for x, y in corners_3d]

    # Board side (front edge)
    side = plt.Polygon([board_top[0], board_top[1], board_bot[1], board_bot[0]],
                       facecolor='#0d3d0d', edgecolor='#0a2a0a', linewidth=1)
    ax.add_patch(side)
    # Board side (right edge)
    side2 = plt.Polygon([board_top[1], board_top[2], board_bot[2], board_bot[1]],
                        facecolor='#0e4e0e', edgecolor='#0a2a0a', linewidth=1)
    ax.add_patch(side2)
    # Board top
    board_poly = plt.Polygon(board_top, facecolor='#1a5c1a',
                             edgecolor='#0d3d0d', linewidth=2)
    ax.add_patch(board_poly)

    # Draw components as 3D blocks
    for ref, x, y, w, h, label, color, height in COMPONENTS:
        if ref.startswith('H') or height == 0:
            continue

        # Component corners (top of component)
        if h < 10:
            cx, cy = x, y
            hw, hh = w/2, h/2
        else:
            cx, cy = x + w/2, y + h/2
            hw, hh = w/2, h/2

        corners = [
            (cx - hw, cy - hh),
            (cx + hw, cy - hh),
            (cx + hw, cy + hh),
            (cx - hw, cy + hh),
        ]

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
    ax.set_title('64korppu-E — Isometrinen 3D-näkymä', fontsize=12, fontweight='bold', pad=10)
    ax.set_facecolor('#0a0a0a')
    ax.axis('off')


# Create figure
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(20, 10))
fig.patch.set_facecolor('#0a0a0a')

draw_top_view(ax1)
draw_isometric(ax2)

plt.tight_layout(pad=2)
plt.savefig('docs/E-IEC-Nano-SRAM/64korppu-E-4layer-3d.png',
            dpi=200, bbox_inches='tight',
            facecolor='#0a0a0a', edgecolor='none')
print('Saved: docs/E-IEC-Nano-SRAM/64korppu-E-4layer-3d.png')
plt.close()
