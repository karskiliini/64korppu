================================================================================
  VAIHTOEHTO B: Expansion Port + FDC-piiri — PIIRIKAAVIO
================================================================================


================================================================================
  C64 EXPANSION PORT -LIITÄNTÄ (44-pin edge connector)
================================================================================

  C64 expansion port pinout (yläpuoli = 1-22, alapuoli = A-Z):

  Yläpuoli:                          Alapuoli:
  ──────────────────────────         ──────────────────────────
   1  GND                            A  GND
   2  +5V                            B  /ROMH (ei käytetä)
   3  +5V                            C  /RESET
   4  /IRQ                           D  /NMI
   5  R/W                            E  Φ2 (clock)
   6  Dot Clock                      F  A15
   7  /IO1 ($DE00-$DEFF)             G  A14
   8  /IO2 ($DF00-$DFFF)             H  A13
   9  /ROML                          J  A12
  10  /EXROM                         K  A11
  11  BA                             L  A10
  12  /DMA (ei käytetä)              M  A9
  13  D7                             N  A8
  14  D6                             P  A7
  15  D5                             R  A6
  16  D4                             S  A5
  17  D3                             T  A4
  18  D2                             U  A3
  19  D1                             V  A2
  20  D0                             W  A1
  21  /GAME                          X  A0
  22  GND                            Y  GND


================================================================================
  OSOITEDEKOODAUS (74LS138 + 74LS04)
================================================================================

  FDC mapataan $DE00-$DE07 (IO1-alue, A7-A3 = 00000):
  /IO1 on jo aktiivinen kun osoite on $DE00-$DEFF.
  Lisädekoodaus: A7-A3 kaikki LOW → FDC valittu.

                        +5V
                         │
              ┌──────────┴──────────┐
              │      74LS138        │
              │   3-to-8 decoder    │
              │                     │
   A7 ────────┤ A (pin 1)   Y0 ────┤──── /FDC_CS (aktiivinen kun A7-A3=00000)
   A6 ────────┤ B (pin 2)   Y1     │
   A5 ────────┤ C (pin 3)   Y2     │
              │              Y3     │
  /IO1 ──────┤ /G2A (pin 4) Y4     │
   GND ──────┤ /G2B (pin 5) Y5     │
              │              Y6     │
              │              Y7     │
              │                     │
   A4 ───┐   │  G1 (pin 6)         │
   A3 ─┐ │   └─────────────────────┘
        │ │
        │ │   ┌──────────┐
        │ └───┤ 74LS04   │
        │     │ INV #1   ├──┐
        └─────┤ INV #2   ├──┤
              └──────────┘  │
                            │
                     NOR-logiikka:
                     /FDC_CS aktiv. kun /IO1=LOW JA A7-A3=00000

  Yksinkertaisempi vaihtoehto (5-input NOR):

   /IO1 ──┐
    A7  ──┤
    A6  ──┼──── 5-input NOR ──── /FDC_CS
    A5  ──┤    (74LS260 tai
    A4  ──┤     74LS02 ketju)
    A3  ──┘


  Käytännöllisempi dekoodaus 74LS138:lla:
  ═══════════════════════════════════════

              +5V
               │
    ┌──────────┴──────────┐
    │      74LS138        │
    │                     │
    │ A  ← A3             │
    │ B  ← A4             │
    │ C  ← A5             │
    │                     │
    │ /G2A ← /IO1         │    /FDC_CS (active LOW kun $DE00-$DE07)
    │ /G2B ← A7           │──── Y0 ──> FDC /CS
    │  G1  ← /A6 (inv)    │
    │                     │
    └─────────────────────┘
                │
        /A6:  A6 ──┤>──── G1
              74LS04


================================================================================
  FDC-PIIRI (WD37C65 / Intel 82077AA)
================================================================================

  82077AA Pinout (68-pin PLCC, oleelliset pinnit):

                      ┌─────────────────────────┐
                      │      82077AA / WD37C65   │
                      │      Floppy Disk         │
                      │      Controller          │
                      │                          │
   C64 dataväylä:     │                          │    34-pin floppy:
                      │                          │
   D0 ─── 74LS245 ───┤ DB0               /DENSEL├────── pin 2
   D1 ─── 74LS245 ───┤ DB1               /MOTEA ├────── pin 10
   D2 ─── 74LS245 ───┤ DB2               /DRVSA ├────── pin 12
   D3 ─── 74LS245 ───┤ DB3               /MOTOR ├────── pin 16
   D4 ─── 74LS245 ───┤ DB4               /DIR   ├────── pin 18
   D5 ─── 74LS245 ───┤ DB5               /STEP  ├────── pin 20
   D6 ─── 74LS245 ───┤ DB6               /WDATA ├────── pin 22
   D7 ─── 74LS245 ───┤ DB7               /WGATE ├────── pin 24
                      │                   /TRK00 ├────── pin 26
   A0 ────────────────┤ A0                /WPT   ├────── pin 28
   A1 ────────────────┤ A1                /RDATA ├────── pin 30
   A2 ────────────────┤ A2                /SIDE1 ├────── pin 32
                      │                   /DSKCHG├────── pin 34
   /FDC_CS ───────────┤ /CS                      │
                      │                   /INDEX ├────── pin 8
   R/W ──┬── INV ─────┤ /RD                      │
         └────────────┤ /WR               /HDSEL ├────── pin 32
                      │                          │
   Φ2 (1 MHz) ───────┤ CLK                      │
                      │                          │
   +5V ───────────────┤ VCC                      │
   GND ───────────────┤ GND                      │
                      │                          │
   24 MHz xtal ──┬────┤ X1                       │
                 └────┤ X2                       │
                      │                          │
                      │ /IRQ ────── → /IRQ (exp.) │
                      │                          │
                      │ /DRQ (ei käytetä,         │
                      │  DMA ei mahdollinen)      │
                      │                          │
                      └──────────────────────────┘


================================================================================
  DATAVÄYLÄ BUFFEROINTI (74LS245)
================================================================================

  C64:n dataväylä (D0-D7) bufferoidaan 74LS245:llä.
  Suunnanvalinta: R/W-signaali.

              +5V
               │
    ┌──────────┴───────────┐
    │      74LS245         │
    │   Bidirectional      │
    │   Bus Transceiver    │
    │                      │
    │ A1 ←──── D0 (C64)   │         │ B1 ────→ DB0 (FDC)
    │ A2 ←──── D1         │         │ B2 ────→ DB1
    │ A3 ←──── D2         │         │ B3 ────→ DB2
    │ A4 ←──── D3         │         │ B4 ────→ DB3
    │ A5 ←──── D4         │         │ B5 ────→ DB4
    │ A6 ←──── D5         │         │ B6 ────→ DB5
    │ A7 ←──── D6         │         │ B7 ────→ DB6
    │ A8 ←──── D7         │         │ B8 ────→ DB7
    │                      │
    │ DIR ←── R/W          │   DIR=HIGH: A→B (C64 kirjoittaa FDC:lle)
    │                      │   DIR=LOW:  B→A (C64 lukee FDC:ltä)
    │ /OE ←── /FDC_CS      │   Bufferi aktiivinen vain kun FDC valittu
    │                      │
    └──────────────────────┘

  Huom: R/W pitää invertoida DIR:lle koska C64:n R/W:
    R/W=HIGH = Read  (data FDC→C64, B→A, DIR=LOW)
    R/W=LOW  = Write (data C64→FDC, A→B, DIR=HIGH)

    DIR ←── /R/W (invertteri 74LS04)


================================================================================
  CARTRIDGE ROM (28C64 EEPROM, 8KB)
================================================================================

  Custom ROM mapitetaan $8000-$9FFF (ROML-alue):

              +5V
               │
    ┌──────────┴───────────┐
    │     28C64 EEPROM     │
    │     8K x 8 bit       │
    │                      │
    │ A0  ←── A0  (C64)    │    D0 ──→ D0 (C64)
    │ A1  ←── A1           │    D1 ──→ D1
    │ A2  ←── A2           │    D2 ──→ D2
    │ A3  ←── A3           │    D3 ──→ D3
    │ A4  ←── A4           │    D4 ──→ D4
    │ A5  ←── A5           │    D5 ──→ D5
    │ A6  ←── A6           │    D6 ──→ D6
    │ A7  ←── A7           │    D7 ──→ D7
    │ A8  ←── A8           │
    │ A9  ←── A9           │
    │ A10 ←── A10          │
    │ A11 ←── A11          │
    │ A12 ←── A12          │
    │                      │
    │ /CE ←── /ROML        │    Aktiivinen kun $8000-$9FFF
    │ /OE ←── Φ2           │    Data valid kun Φ2 HIGH
    │ /WE ←── +5V          │    Ei kirjoitusta (tied HIGH)
    │                      │
    └──────────────────────┘

  Expansion port config signaalit:
    /EXROM ──── GND  (vetää LOW → 8K cartridge mode)
    /GAME  ──── +5V  (jää HIGH)


================================================================================
  KELLOTUS
================================================================================

  82077AA tarvitsee 24 MHz kiteen:

           ┌────────┐
    X1 ────┤ 24 MHz ├──── X2
           │ xtal   │
           └───┬────┘
               │
              GND
              ├── C1 (22pF)
              └── C2 (22pF)

  FDC:n CLK-input: C64:n Φ2 (1 MHz) suoraan expansion portista.


================================================================================
  KOKONAISPIIRIKAAVIO
================================================================================

         C64 Expansion Port (44-pin edge)
    ┌────────────────────────────────────────────┐
    │  +5V  GND  D0-D7  A0-A15  R/W  Φ2  /IO1  │
    │  /ROML  /EXROM  /GAME  /IRQ  /RESET       │
    └────┬────┬───┬──────┬────┬───┬────┬─────────┘
         │    │   │      │    │   │    │
         │    │   │      │    │   │    │
         │    │   │      │    │   │    └── /IO1 ──┐
         │    │   │      │    │   │               │
         │    │   │      │    │   └── Φ2          │
         │    │   │      │    │                   │
         │    │   │      │    └── R/W             │
         │    │   │      │                        │
         │    │   │      └── A0-A7 ───────┐       │
         │    │   │                       │       │
         │    │   └── D0-D7 ──┐           │       │
         │    │               │           │       │
         │    │               ▼           ▼       ▼
         │    │        ┌────────────┐  ┌─────────────┐
         │    │        │  74LS245   │  │  74LS138     │
         │    │        │ data buf   │  │  + 74LS04    │
         │    │        │            │  │  osoite-     │
         │    │        │ DIR←/R/W   │  │  dekooderi   │
         │    │        │ /OE←/FDC_CS│  │              │
         │    │        └─────┬──────┘  └──────┬───────┘
         │    │              │                │
         │    │              │  /FDC_CS       │
         │    │              │ ◄──────────────┘
         │    │              │
         │    │              ▼
         │    │     ┌─────────────────┐        ┌──────────────┐
         │    │     │    82077AA      │        │   28C64      │
         │    │     │    FDC          │        │   EEPROM     │
         │    │     │                 │        │   8KB ROM    │
         │    │     │  DB0-DB7 ← buf │        │              │
         │    │     │  A0-A2  ← A0-2 │        │ /CE ← /ROML │
         │    │     │  /CS ← dekood  │        │ A0-A12 ← bus│
         │    │     │  /RD,/WR ← R/W │        │ D0-D7 → bus │
         │    │     │  CLK ← Φ2      │        │              │
         │    │     │  24MHz xtal    │        └──────────────┘
         │    │     │                 │
         │    │     │  Floppy I/F:    │
         │    │     │  ┌────────────┐ │
         │    │     │  │ 34-pin IDC │ │
         │    │     │  │ /DENSEL    │ │
         │    │     │  │ /MOTEA     │ │
         │    │     │  │ /DRVSA     │ │
         │    │     │  │ /DIR       │ │
         │    │     │  │ /STEP      │ │
         │    │     │  │ /WDATA     │ │
         │    │     │  │ /WGATE     │ │
         │    │     │  │ /RDATA     │ │
         │    │     │  │ /TRK00     │ │
         │    │     │  │ /WPT       │ │
         │    │     │  │ /SIDE1     │ │
         │    │     │  │ /DSKCHG    │ │
         │    │     │  └─────┬──────┘ │
         │    │     └────────┼────────┘
         │    │              │
         │    │              ▼
         │    │     ┌──────────────┐
         │    │     │  PC 3.5" HD  │
         │    │     │  Floppy      │
         │    │     │  Drive       │
         │    │     └──────────────┘
         │    │
        +5V  GND ──── kaikki GND yhteen


================================================================================
  KOMPONENTTILISTA (BOM)
================================================================================

  Nro  Komponentti            Paketti       Kpl   Huomio
  ───  ─────────────────────  ────────────  ───   ─────────────────────────
   1   82077AA tai WD37C65    PLCC-68/DIP    1   FDC-piiri
   2   28C64 EEPROM           DIP-28         1   8KB custom ROM
   3   74LS245                DIP-20         1   Dataväylä bufferi
   4   74LS138                DIP-16         1   Osoitedekooderi
   5   74LS04                 DIP-14         1   Invertterit
   6   Kide 24 MHz            HC49           1   FDC-kello
   7   Kond. 22pF             keraami.       2   Kide-kuormitus
   8   Kond. 100nF            keraami.       5   Bypass (joka IC)
   9   Kond. 10µF             elektrol.      1   Bulk virta
  10   44-pin edge connector  2x22 2.54mm    1   Expansion port
  11   34-pin IDC header      2x17 pin       1   Floppy-liitäntä
  12   4-pin Berg liitin      Berg           1   Floppy-virta
  13   LED vihreä             3mm            1   Tilaindikaattori
  14   Vastus 330Ω            1/4W           1   LED-vastus

================================================================================
