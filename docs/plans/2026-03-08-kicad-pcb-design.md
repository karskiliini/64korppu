# KiCad PCB Design — Vaihtoehto E (IEC + Nano + SPI SRAM)

## Päätökset

- **Levy:** ~85 × 55 mm, 2-kerroksinen (top + bottom)
- **Nano:** Pin header -sokkelit, irrotettava moduuli
- **Liittimet:** IEC DIN-6 vasemmalla, 34-pin IDC oikealla, DC jack ylhäällä
- **Automaattireititys:** Freerouting (.dsn export)

## Komponentit

| Ref | Komponentti | Paketti | Kpl |
|-----|-------------|---------|-----|
| U1 | Arduino Nano | 2×15 pin header | 1 |
| U2 | 23LC256 | DIP-8 | 1 |
| U3 | 74HC595 | DIP-16 | 1 |
| J1 | 6-pin DIN | DIN-6 (IEC) | 1 |
| J2 | 34-pin IDC | 2×17 shrouded header | 1 |
| J3 | DC jack | 5.5/2.1mm barrel | 1 |
| R1-R4 | 1 kOhm | axial | 4 |
| R5-R7 | 4.7 kOhm | axial | 3 |
| R8 | 330 Ohm | axial | 1 |
| R9-R12 | 10 kOhm | axial | 4 |
| C1-C3 | 100 nF | ceramic disc | 3 |
| C4 | 10 uF | electrolytic | 1 |
| D1 | LED green | 3mm | 1 |

## Netlist

```
IEC-väylä:
  J1.ATN  → R1 → U1.D2
  J1.CLK  → R2 → U1.D3
  J1.DATA → R3 → U1.D4
  J1.RST  → R4 → U1.D5
  J1.GND  → GND

IEC pull-up:
  R5: +5V → J1.ATN (after R1)
  R6: +5V → J1.CLK (after R2)
  R7: +5V → J1.DATA (after R3)

SPI SRAM (23LC256):
  U1.D10  → U2./CS
  U1.D11  → U2.SI
  U1.D12  ← U2.SO
  U1.D13  → U2.SCK
  U2./HOLD → +5V
  U2.VCC  → +5V
  U2.VSS  → GND

74HC595:
  U1.D11  → U3.SER
  U1.D13  → U3.SRCLK
  U1.D6   → U3.RCLK
  U3./CLR → +5V
  U3./OE  → GND
  U3.VCC  → +5V
  U3.GND  → GND

Floppy (via 74HC595):
  U3.QA → J2./SIDE1
  U3.QB → J2./DENSITY
  U3.QC → J2./MOTEA
  U3.QD → J2./DRVSEL
  U3.QE → J2./MOTOR
  U3.QF → J2./DIR
  U3.QG → J2./STEP
  U3.QH → J2./WGATE

Floppy (suora GPIO):
  U1.D7  → J2./WDATA
  U1.D8  ← J2./RDATA
  U1.A0  ← J2./TRK00   (R9 pull-up 10k)
  U1.A1  ← J2./WPT     (R10 pull-up 10k)
  U1.A2  ← J2./DSKCHG  (R11 pull-up 10k)
  U1.D8 pull-up: R12 10k → +5V

LED:
  U1.A5 → R8(330) → D1 → GND

Bypass:
  C1: U1.+5V ↔ GND
  C2: U2.VCC ↔ GND
  C3: U3.VCC ↔ GND
  C4: J3.+5V ↔ GND (bulk)

Virta:
  J3.+5V → +5V (kaikille)
  J3.GND → GND (kaikille)
```

## Sijoittelu

```
              J3 (DC jack)
        ┌────────┬─────────────────┐
        │  C4                      │
        │                          │
  J1 ───┤  R1-R4    ┌──────────┐   ├─── J2
 (DIN-6)│  R5-R7    │ U1 Nano  │   │  (34-pin IDC)
        │           │ headers  │   │
        │  ┌────┐   └──────────┘   │
        │  │ U2 │                  │
        │  │SRAM│   ┌──────────┐   │
        │  └────┘   │U3 74HC595│   │
        │           └──────────┘   │
        │  D1 R8   C1 C2 C3       │
        │          R9-R12          │
        └──────────────────────────┘
```

## Reititysprioriteetti

1. SPI-väylä (MOSI/MISO/SCK) — lyhyet vedot
2. MFM (WDATA/RDATA) — suorat lyhyet vedot
3. IEC — DIN-6 → vastukset → Nano
4. GND-täyttö bottom-kerroksella
