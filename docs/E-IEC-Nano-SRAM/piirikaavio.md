# Vaihtoehto E: IEC + Arduino Nano + SPI SRAM — Piirikaavio

## Kokonaiskaavio

```
                                   Ulkoinen
                                   5V/2A PSU
                                   ┌───────┐
                                   │ +5V   │
                                   │ GND   │
                                   └───┬───┘
                                       │
              ┌────────────────────────┼────────────────────────┐
              │                        │                        │
              ▼                        ▼                        ▼
       ┌──────────────┐         ┌──────────────┐        ┌────────────┐
       │   Floppy     │         │  Arduino     │        │  C64       │
       │   asema      │         │  Nano        │        │            │
       │   PC 3.5" HD │         │  + SPI SRAM  │        │  IEC-port  │
       └──────┬───────┘         │  + 74HC595   │        └──────┬─────┘
              │                 └──────┬───────┘               │
              │  34-pin                │  SPI              6-pin DIN
              │  lattakaapeli          │  väylä             IEC-kaapeli
              └────── GPIO ────────────┴──── GPIO ─────────────┘

  Kaikki komponentit ovat 5V — ei yhtään tasonmuunninta!
```

## IEC-väylä (suora 5V kytkentä)

```
  Sama kuin vaihtoehto D — suora kytkentä, ei tasonmuuntimia.

           C64                                    Arduino Nano
     6-pin DIN-liitin                             (5V logiikka)
    ┌──────────────┐                             ┌──────────────┐
    │              │         1kΩ suoja            │              │
    │  Pin 3: ATN  ├─────────┤├──────────────────┤ D2 (INT0)    │
    │              │         1kΩ                  │              │
    │  Pin 4: CLK  ├─────────┤├──────────────────┤ D3           │
    │              │         1kΩ                  │              │
    │  Pin 5: DATA ├─────────┤├──────────────────┤ D4           │
    │              │         1kΩ                  │              │
    │  Pin 6: RESET├─────────┤├──────────────────┤ D5           │
    │              │                              │              │
    │  Pin 2: GND  ├──────────────────────────────┤ GND          │
    │              │                              │              │
    └──────────────┘                             └──────────────┘

  Valinnaiset pull-up vastukset:

         +5V         +5V         +5V
          │           │           │
        4.7kΩ       4.7kΩ       4.7kΩ
          │           │           │
    ATN ──┘     CLK ──┘    DATA ──┘
```

## 23LC256 SPI SRAM (32 KB)

```
                    +5V
                     │
                    100nF      (bypass-kondensaattori
                     │          mahdollisimman lähelle
                    GND         piirin VCC/GND-pinnejä)

                23LC256
               ┌────┬────┐
    D10 ───────┤1 /CS  VCC├───── +5V
               │         8│
    D12 ←──────┤2 SO  /HLD├───── +5V  (HOLD ei käytössä, vedetään HIGH)
               │         7│
               │3 NC   SCK├───── D13 (SCK)
               │         6│
    GND ───────┤4 VSS   SI├───── D11 (MOSI)
               └──────────┘  5

  Pinnit:
    1  /CS   ← Arduino D10 (Chip Select, active LOW)
    2  SO    → Arduino D12 (MISO, data out)
    3  NC    (ei kytketä)
    4  VSS   → GND
    5  SI    ← Arduino D11 (MOSI, data in)
    6  SCK   ← Arduino D13 (SPI Clock)
    7  /HOLD ← +5V (ei käytetä, vedetään HIGH)
    8  VCC   ← +5V

  SPI Mode 0 (CPOL=0, CPHA=0), max 20 MHz
  Arduino SPI @ 8 MHz (16 MHz / 2) → ~1 MB/s
```

## 74HC595 Shift Register (floppy output -signaalit)

```
  8 floppy output -signaalia ohjataan yhdellä 74HC595:llä
  SPI-väylän kautta. Tämä vapauttaa 8 GPIO-pinniä Nanosta.

                    +5V
                     │
                    100nF
                     │
                    GND

                74HC595
               ┌────┬────┐
  /DENSITY ←───┤1 QB  VCC├───── +5V
               │        16│
  /MOTEA   ←───┤2 QC  QA ├───── /SIDE1 (→ IDC pin 32)
               │        15│
  /DRVSEL  ←───┤3 QD  SER├───── D11 (MOSI, jaettu SRAM:n kanssa)
               │        14│
  /MOTOR   ←───┤4 QE RCLK├───── D6  (Latch clock, oma GPIO!)
               │        12│
  /DIR     ←───┤5 QF SRCK├───── D13 (SCK, jaettu SRAM:n kanssa)
               │        11│
  /STEP    ←───┤6 QG /CLR├───── +5V (ei clearata, vedetään HIGH)
               │        10│
  /WGATE   ←───┤7 QH  /OE├───── GND (output aina aktiivinen)
               │         9│
  GND ─────────┤8 GND QH'├───── (ei käytetä, kaskadointiin)
               └──────────┘

  Toiminta:
  1. Arduino lähettää 8 bittiä SPI:llä (MOSI + SCK)
     - /CS_SRAM on HIGH (SRAM ei reagoi)
  2. Arduino pulsittaa D6:n (RCLK) HIGH→LOW
     → 74HC595 siirtää shift registerin sisällön lähtöihin
  3. Floppy-signaalit päivittyvät

  Bittijärjestys (MSB first):
    Bit 7 (QH) = /WGATE
    Bit 6 (QG) = /STEP
    Bit 5 (QF) = /DIR
    Bit 4 (QE) = /MOTOR
    Bit 3 (QD) = /DRVSEL
    Bit 2 (QC) = /MOTEA
    Bit 1 (QB) = /DENSITY
    Bit 0 (QA) = /SIDE1

  Koodiesimerkki:
    void floppy_set_outputs(uint8_t bits) {
        digitalWrite(CS_SRAM, HIGH);   // Varmista SRAM pois
        SPI.transfer(bits);            // Siirrä 8 bittiä
        digitalWrite(LATCH_595, HIGH); // Lukitse lähtöihin
        digitalWrite(LATCH_595, LOW);
    }
```

## SPI-väylän jakaminen

```
  SRAM ja 74HC595 jakavat SPI-väylän (MOSI, MISO, SCK).
  Chip Select -signaalit erottavat ne:

         Arduino Nano
        ┌──────────────┐
        │              │
        │ D11 (MOSI) ──┼──────┬──────────────┐
        │              │      │              │
        │ D12 (MISO) ──┼──────┤              │
        │              │      │              │
        │ D13 (SCK) ───┼──────┤──────────┐   │
        │              │      │          │   │
        │ D10 (/CS) ───┼──┐   │          │   │
        │              │  │   │          │   │
        │ D6 (LATCH) ──┼──┼───┼──────┐   │   │
        │              │  │   │      │   │   │
        └──────────────┘  │   │      │   │   │
                          │   │      │   │   │
                     ┌────┴───┴──┐ ┌─┴───┴───┴──┐
                     │ 23LC256  │ │  74HC595    │
                     │           │ │             │
                     │ /CS ← D10│ │ SER ← MOSI  │
                     │ SI ← MOSI│ │ SRCLK ← SCK │
                     │ SO → MISO│ │ RCLK ← D6   │
                     │ SCK ← SCK│ │             │
                     └──────────┘ └─────────────┘

  Käyttöprotokolla:
  ─────────────────

  SRAM-operaatio:
    1. D10 = LOW  (SRAM valittu)
    2. D6  = LOW  (595 latch ei muutu)
    3. SPI.transfer(...)
    4. D10 = HIGH (SRAM deselected)

  595-operaatio:
    1. D10 = HIGH (SRAM deselected)
    2. SPI.transfer(byte)  ← bittit siirtyvät 595:een
    3. D6 = HIGH → LOW     ← latchi! lähdöt päivittyvät

  Näin SRAM ja 595 eivät häiritse toisiaan.
```

## Floppy-asema (34-pin IDC)

```
  Output-signaalit 74HC595:n kautta:

         74HC595                          34-pin IDC
        ┌──────────────┐                 ┌──────────────────┐
        │              │                 │                  │
        │ QB /DENSITY ─┼─────────────────┤  pin 2           │
        │ QC /MOTEA ───┼─────────────────┤  pin 10          │
        │ QD /DRVSEL ──┼─────────────────┤  pin 12          │
        │ QE /MOTOR ───┼─────────────────┤  pin 16          │
        │ QF /DIR ─────┼─────────────────┤  pin 18          │
        │ QG /STEP ────┼─────────────────┤  pin 20          │
        │ QH /WGATE ───┼─────────────────┤  pin 24          │
        │ QA /SIDE1 ───┼─────────────────┤  pin 32          │
        └──────────────┘                 │                  │
                                         │                  │
  Suorat GPIO (tarkka ajoitus tarpeen):  │                  │
                                         │                  │
         Arduino Nano                    │                  │
        ┌──────────────┐                 │                  │
        │ D7  /WDATA ──┼─────────────────┤  pin 22          │
        │              │                 │  (MFM write data)│
        │ D8  /RDATA ──┼─────────────────┤  pin 30          │
        │  (ICP1)      │   ┌─ 10kΩ──5V  │  (MFM read data) │
        │              │   │             │                  │
        │ A0  /TRK00 ──┼───┤─────────────┤  pin 26          │
        │              │   ┌─ 10kΩ──5V   │                  │
        │ A1  /WPT ────┼───┤─────────────┤  pin 28          │
        │              │   ┌─ 10kΩ──5V   │                  │
        │ A2  /DSKCHG ─┼───┤─────────────┤  pin 34          │
        │              │                 │                  │
        │ GND ─────────┼─────────────────┤  pins 1,3,...,33 │
        └──────────────┘                 └──────────────────┘

  HUOM: /WDATA (D7) ja /RDATA (D8) tarvitsevat suoran GPIO-kytkennän
  koska MFM-koodaus/-dekoodaus vaatii mikrosekuntitason ajoitusta.
  Shift register olisi liian hidas näille signaaleille.
```

## Arduino Nano — Pinout (Vaihtoehto E)

```
                        ┌──────────┐
                  ┌─────┤  USB     ├─────┐
                  │     └──────────┘     │
                  │     Arduino Nano     │
                  │                      │
    (TX)     D1  [1] │                      │ [30] VIN
    (RX)     D0  [2] │                      │ [29] GND
           /RST  [3] │                      │ [28] /RST
             GND [4] │                      │ [27] +5V ← PSU
    ATN      D2  [5] │                      │ [26] A7
    CLK      D3  [6] │                      │ [25] A6
    DATA     D4  [7] │                      │ [24] A5 ← LED
    RESET    D5  [8] │                      │ [23] A4 (vapaa)
    595LATCH D6  [9] │                      │ [22] A3 (vapaa)
    WDATA    D7 [10] │                      │ [21] A2 ← /DSKCHG
    RDATA    D8 [11] │(ICP1)                │ [20] A1 ← /WPT
    (vapaa)  D9 [12] │                      │ [19] A0 ← /TRK00
    /CS_SRAM D10[13] │                      │ [18] AREF
    MOSI     D11[14] │→ SRAM SI + 595 SER   │ [17] 3V3
    MISO     D12[15] │← SRAM SO             │ [16] D13 SCK
                  │                      │        → SRAM SCK
                  │                      │        + 595 SRCLK
                  └──────────────────────┘

  GPIO-yhteenveto:
    D0-D1:   Serial (USB debug)
    D2-D5:   IEC-väylä (ATN/INT0, CLK, DATA, RESET)
    D6:      74HC595 Latch (RCLK)
    D7:      /WDATA (suora GPIO, MFM-kirjoitus)
    D8:      /RDATA (ICP1, MFM-luku Timer1 Input Capture)
    D9:      Vapaa
    D10:     23LC256 /CS
    D11-D13: SPI (MOSI, MISO, SCK) → SRAM + 595
    A0-A2:   Floppy input (/TRK00, /WPT, /DSKCHG)
    A3-A4:   Vapaat
    A5:      Status LED
```

## Virtalähde

```
    Ulkoinen 5V/2A PSU
    ┌──────────┐
    │          │
    │ +5V ─────┼──┬───── Arduino Nano +5V (pin 27)
    │          │  │
    │          │  ├───── 23LC256 VCC (pin 8)
    │          │  │
    │          │  ├───── 74HC595 VCC (pin 16)
    │          │  │
    │          │  └───── Floppy-asema +5V
    │          │
    │ GND ─────┼──┬───── Arduino Nano GND
    │          │  ├───── 23LC256 VSS (pin 4)
    │          │  ├───── 74HC595 GND (pin 8)
    │          │  └───── Floppy-asema GND
    └──────────┘
```

## Kokonaispiirikaavio

```
    C64                                                       Floppy-asema
  ┌──────┐                                                   ┌───────────┐
  │      │                                                   │ PC 3.5"   │
  │ IEC  │      Arduino Nano                                  │ HD 1.44MB │
  │ port │     ┌────────────────┐                             │           │
  │      │     │   ATmega328P   │     23LC256      74HC595   │  34-pin   │
  │      │     │                │    ┌────────┐   ┌────────┐  │  IDC      │
  │      │ 1kΩ │                │    │ 32KB   │   │ 8-bit  │  │           │
  │ ATN  ├─┤├──┤ D2 (INT0)     │    │ SPI    │   │ shift  │  │           │
  │ CLK  ├─┤├──┤ D3            │    │ SRAM   │   │ reg    │  │           │
  │ DATA ├─┤├──┤ D4            │    │        │   │        │  │           │
  │ RESET├─┤├──┤ D5            │    │        │   │        │  │           │
  │ GND  ├─────┤ GND          │    │        │   │        │  │           │
  │      │     │               │    │        │   │        │  │           │
  └──────┘     │ D10 (/CS) ────┼────┤ /CS    │   │        │  │           │
               │               │    │        │   │        │  │           │
               │ D11 (MOSI) ───┼──┬─┤ SI     │ ┌─┤ SER    │  │           │
               │               │  │ │        │ │ │        │  │           │
               │ D12 (MISO) ───┼──┤─┤ SO     │ │ │        │  │           │
               │               │  │ │        │ │ │        │  │           │
               │ D13 (SCK) ────┼──┴─┤ SCK    │ ├─┤ SRCLK  │  │           │
               │               │    │        │ │ │        │  │           │
               │               │    │ /HOLD──┤5V│        │  │           │
               │               │    │ VCC────┤5V│ VCC──5V│  │           │
               │               │    │ GND────┤G │ GND──G │  │           │
               │               │    └────────┘ │ │        │  │           │
               │               │               │ │ QB─────┼──┤ /DENSITY  │
               │ D6 (LATCH) ───┼───────────────┘─┤ RCLK   │  │           │
               │               │                 │ QC─────┼──┤ /MOTEA    │
               │               │                 │ QD─────┼──┤ /DRVSEL   │
               │               │                 │ QE─────┼──┤ /MOTOR    │
               │               │                 │ QF─────┼──┤ /DIR      │
               │               │                 │ QG─────┼──┤ /STEP     │
               │               │                 │ QH─────┼──┤ /WGATE    │
               │               │                 │ QA─────┼──┤ /SIDE1    │
               │               │                 └────────┘  │           │
               │ D7 (/WDATA) ──┼──────────────────────────────┤ /WDATA    │
               │               │           ┌── 10kΩ──5V      │           │
               │ D8 (/RDATA) ──┼───────────┤──────────────────┤ /RDATA    │
               │  (ICP1)       │           │                  │           │
               │               │           ┌── 10kΩ──5V      │           │
               │ A0 (/TRK00) ──┼───────────┤──────────────────┤ /TRK00    │
               │               │           ┌── 10kΩ──5V      │           │
               │ A1 (/WPT) ────┼───────────┤──────────────────┤ /WPT      │
               │               │           ┌── 10kΩ──5V      │           │
               │ A2 (/DSKCHG) ─┼───────────┤──────────────────┤ /DSKCHG   │
               │               │                              │           │
               │ GND ──────────┼──────────────────────────────┤ GND       │
               │               │                              │           │
               │ A5 ──┤├── LED │                              │ +5V ← PSU│
               │      330Ω     │                              │           │
               └───────────────┘                              └───────────┘

  +5V PSU ──────────────────────────────────────────────────────────┐
  GND PSU ──────────────────────────────────────────────────────────┤
                │          │           │            │               │
              Nano +5V   SRAM VCC    595 VCC    Floppy +5V    Bypass caps
              Nano GND   SRAM GND    595 GND    Floppy GND    3x 100nF
```

## Bypass-kondensaattorit

```
  Jokaiselle IC:lle 100nF keraaminen bypass-kondensaattori
  VCC-GND väliin, mahdollisimman lähelle piirin jalkoja:

    +5V ──┤├── GND   (Arduino Nano, lähellä +5V/GND pinnejä)
          100nF

    +5V ──┤├── GND   (23LC256, pin 8 ja pin 4 väliin)
          100nF

    +5V ──┤├── GND   (74HC595, pin 16 ja pin 8 väliin)
          100nF

  Lisäksi bulk-kondensaattori virtalähteen liittimeen:
    +5V ──┤├── GND   (10µF elektrolyyttinen)
```

## Komponenttilista (BOM)

```
  Nro  Komponentti            Paketti       Kpl   Huomio
  ───  ─────────────────────  ────────────  ───   ────────────────────────
   1   Arduino Nano (klooni)  DIP-moduuli    1   ATmega328P, 16MHz, 5V
   2   23LC256               DIP-8          1   32KB SPI SRAM, 5V
   3   74HC595                DIP-16         1   8-bit shift register
   4   Vastus 1kΩ             1/4W           4   IEC-suojavastukset
   5   Vastus 4.7kΩ           1/4W           3   IEC pull-up (valinnainen)
   6   Vastus 10kΩ            1/4W           4   Floppy input pull-up
   7   Vastus 330Ω            1/4W           1   LED-vastus
   8   Kond. 100nF            keraami.       3   Bypass (Nano, SRAM, 595)
   9   Kond. 10µF             elektrol.      1   Bulk virta
  10   LED vihreä             3mm            1   Tilaindikaattori
  11   6-pin DIN liitin       DIN-6          1   IEC-väylä
  12   34-pin IDC header      2x17 pin       1   Floppy-liitäntä
  13   4-pin Berg liitin      Berg           1   Floppy-virta
  14   DC jack 5.5/2.1mm      panel mount    1   Virtalähde
  15   Pin headerit           2.54mm         -   Nanon + IC:den kiinnitys
  ──────────────────────────────────────────────────────────────────
  Yhteensä (ilman Nanoa ja floppy-asemaa):             ~7€
  Yhteensä (Nano-kloonilla):                           ~10-13€
```
