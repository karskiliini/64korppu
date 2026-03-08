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

# Component bounding boxes from COMPONENT_BODIES (pcb-validate.py)
# Format: (ref, x1, y1, x2, y2, label, color, height_mm)
# x1,y1 = top-left corner, x2,y2 = bottom-right corner
COMPONENTS = [
    # Mounting holes (special: drawn as circles)
    ('H1', 101, 101, 105, 105, '', '#888888', 0),
    ('H2', 165, 101, 169, 105, '', '#888888', 0),
    ('H3', 101, 155, 105, 159, '', '#888888', 0),
    ('H4', 165, 155, 169, 159, '', '#888888', 0),
    # Power: J3 barrel jack at (111,105), body (-5,9,-4.5,8)
    ('J3', 106, 100.5, 120, 113, 'DC 5V', '#2a2a2a', 11),
    # C4 electrolytic at (125,104), body (-1,4,-2.5,2.5)
    ('C4', 124, 101.5, 129, 106.5, '10µF', '#CD853F', 8),
    # LED at (132,104), body (-2,2,-2,2)
    ('D1', 130, 102, 134, 106, 'LED', '#00CC00', 5),
    # R8 axial at (137,104), body (-1.5,11.5,-1.5,1.5)
    ('R8', 135.5, 102.5, 148.5, 105.5, '330Ω', '#DEB887', 2.5),
    # IEC series resistors R1-R4 at x=108, body (-1.5,11.5,-1.5,1.5)
    ('R1', 106.5, 114.5, 119.5, 117.5, '1kΩ', '#DEB887', 2.5),
    ('R2', 106.5, 118.5, 119.5, 121.5, '1kΩ', '#DEB887', 2.5),
    ('R3', 106.5, 122.5, 119.5, 125.5, '1kΩ', '#DEB887', 2.5),
    ('R4', 106.5, 126.5, 119.5, 129.5, '1kΩ', '#DEB887', 2.5),
    # IEC pull-up resistors R5-R7
    ('R5', 106.5, 130.5, 119.5, 133.5, '4.7kΩ', '#DEB887', 2.5),
    ('R6', 106.5, 134.5, 119.5, 137.5, '4.7kΩ', '#DEB887', 2.5),
    ('R7', 106.5, 138.5, 119.5, 141.5, '4.7kΩ', '#DEB887', 2.5),
    # DIN-6 IEC connector at (114,151), body (-8,8,-8,8)
    ('J1', 106, 143, 122, 159, 'IEC\nDIN-6', '#333333', 15),
    # Arduino Nano at (135,111), PinHeader body (-2,5,-1.5,36.5)
    ('U1', 133, 109.5, 140, 147.5, 'Arduino\nNano', '#0066CC', 10),
    # Floppy pull-up resistors R9-R12 at x=148
    ('R9',  146.5, 106.5, 159.5, 109.5, '10kΩ', '#DEB887', 2.5),
    ('R10', 146.5, 111.5, 159.5, 114.5, '10kΩ', '#DEB887', 2.5),
    ('R11', 146.5, 116.5, 159.5, 119.5, '10kΩ', '#DEB887', 2.5),
    ('R12', 146.5, 121.5, 159.5, 124.5, '10kΩ', '#DEB887', 2.5),
    # Floppy IDC connector at (165,110), body (-4.5,4.5,-2,42)
    ('J2', 160.5, 108, 169.5, 152, 'Floppy\nIDC\n2x17', '#444444', 10),
    # SRAM DIP-8 at (125,150), body (-1.5,9,-1.5,9)
    ('U2', 123.5, 148.5, 134, 159, 'SRAM\n23LC1024', '#333333', 5),
    # 74HC595 DIP-16 at (148,132), body (-1.5,9,-1.5,20)
    ('U3', 146.5, 130.5, 157, 152, '74HC595', '#333333', 5),
    # Bypass caps: C_Disc body (-1,6,-2.5,2.5)
    ('C1', 135, 148.5, 142, 153.5, '100nF', '#CD853F', 3),
    ('C2', 135, 154.5, 142, 159.5, '100nF', '#CD853F', 3),
    ('C3', 147, 153.5, 154, 158.5, '100nF', '#CD853F', 3),
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
    for ref, x1, y1, x2, y2, label, color, height in COMPONENTS:
        w = x2 - x1
        h = y2 - y1
        cx = (x1 + x2) / 2
        cy = (y1 + y2) / 2

        if ref.startswith('H'):
            # Mounting holes
            circle = plt.Circle((cx, cy), 3, facecolor='#666666',
                                edgecolor='#444444', linewidth=1.5)
            ax.add_patch(circle)
            inner = plt.Circle((cx, cy), 1.6, facecolor='#1a5c1a',
                               edgecolor='#444444', linewidth=0.5)
            ax.add_patch(inner)
            continue

        # Component body
        rect = FancyBboxPatch(
            (x1, y1), w, h,
            boxstyle="round,pad=0.3",
            facecolor=color, edgecolor='#111111', linewidth=0.8,
            alpha=0.9)
        ax.add_patch(rect)

        # Label
        if label:
            fontsize = 5 if len(label) > 6 else 6
            ax.text(cx, cy, label,
                    ha='center', va='center', fontsize=fontsize,
                    color='white' if color in ('#333333', '#444444', '#2a2a2a', '#0066CC') else '#333333',
                    fontweight='bold')

        # Reference designator
        ax.text(cx, y1 - 1.5, ref,
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

    # Draw components as 3D blocks (back-to-front for correct overlap)
    for ref, x1, y1, x2, y2, label, color, height in sorted(COMPONENTS, key=lambda c: -(c[2] + c[4])/2):
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
