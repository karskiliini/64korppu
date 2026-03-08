#!/usr/bin/env python3
"""
Add 14 diagnostic test points to the 4-layer PCB.

Sijoittaa test pointit vasempaan reunaan pystyrivinä (x=101.5)
B.Cu-puolelle (taustapuoli). Through-hole padit 1.5mm, drill 0.8mm.

Käyttö:
  python3 tools/add-testpoints.py
"""
import uuid

PCB_PATH = 'hardware/E-IEC-Nano-SRAM/4-layer/64korppu-E.kicad_pcb'

# (ref, x, y, net_code, net_name)
TEST_POINTS = [
    ('TP1',  103.0, 108.0,  1, 'GND'),
    ('TP2',  103.0, 110.5,  2, '+5V'),
    ('TP3',  103.0, 113.0,  8, 'IEC_ATN'),
    ('TP4',  103.0, 115.5,  9, 'IEC_CLK'),
    ('TP5',  103.0, 118.0, 10, 'IEC_DATA'),
    ('TP6',  103.0, 120.5, 24, 'FLOPPY_MOTOR'),
    ('TP7',  103.0, 123.0, 26, 'FLOPPY_STEP'),
    ('TP8',  103.0, 125.5, 17, 'FLOPPY_RDATA'),
    ('TP9',  103.0, 128.0, 16, 'FLOPPY_WDATA'),
    ('TP10', 103.0, 130.5, 28, 'FLOPPY_SIDE1'),
    ('TP11', 103.0, 133.0,  5, 'SPI_SCK'),
    ('TP12', 103.0, 135.5,  3, 'SPI_MOSI'),
    ('TP13', 103.0, 138.0,  4, 'SPI_MISO'),
    ('TP14', 103.0, 140.5,  6, 'SPI_CS_SRAM'),
]


def make_uuid():
    return str(uuid.uuid4())


def make_testpoint(ref, x, y, net_code, net_name):
    """Generate KiCad footprint S-expression for a test point."""
    return f"""\t(footprint "TestPoint:TestPoint_Pad_D1.5_SMD"
\t\t(layer "B.Cu")
\t\t(uuid "{make_uuid()}")
\t\t(at {x} {y})
\t\t(property "Reference" "{ref}"
\t\t\t(at 2.5 0 0)
\t\t\t(layer "B.SilkS")
\t\t\t(uuid "{make_uuid()}")
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 0.8 0.8)
\t\t\t\t\t(thickness 0.15)
\t\t\t\t)
\t\t\t\t(justify mirror)
\t\t\t)
\t\t)
\t\t(property "Value" "{net_name}"
\t\t\t(at 0 2 0)
\t\t\t(layer "B.Fab")
\t\t\t(uuid "{make_uuid()}")
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 0.8 0.8)
\t\t\t\t\t(thickness 0.15)
\t\t\t\t)
\t\t\t\t(justify mirror)
\t\t\t)
\t\t)
\t\t(property "Datasheet" ""
\t\t\t(at 0 0 0)
\t\t\t(layer "B.Fab")
\t\t\t(hide yes)
\t\t\t(uuid "{make_uuid()}")
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t(thickness 0.15)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Description" "Test Point"
\t\t\t(at 0 0 0)
\t\t\t(layer "B.Fab")
\t\t\t(hide yes)
\t\t\t(uuid "{make_uuid()}")
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t(thickness 0.15)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(pad "1" smd circle
\t\t\t(at 0 0)
\t\t\t(size 1.5 1.5)
\t\t\t(layers "B.Cu" "B.Mask")
\t\t\t(net {net_code} "{net_name}")
\t\t\t(uuid "{make_uuid()}")
\t\t)
\t\t(embedded_fonts no)
\t)"""


def main():
    with open(PCB_PATH) as f:
        content = f.read()

    # Generate all test point footprints
    tp_text = '\n'.join(make_testpoint(*tp) for tp in TEST_POINTS)

    # Insert before gr_rect (board outline) which comes after footprints
    marker = '\t(gr_rect'
    if marker not in content:
        print("ERROR: gr_rect not found in PCB file")
        return

    content = content.replace(marker, tp_text + '\n' + marker, 1)

    with open(PCB_PATH, 'w') as f:
        f.write(content)

    print(f"Added {len(TEST_POINTS)} test points to {PCB_PATH}:")
    for ref, x, y, net_code, net_name in TEST_POINTS:
        print(f"  {ref}: {net_name} at ({x}, {y})")


if __name__ == '__main__':
    main()
