# Vaihtoehto E: IEC-väylä + Arduino Nano + Ulkoinen SRAM

## Arkkitehtuuri

```
C64 ──[IEC-sarjaväylä]──> Arduino Nano ──[34-pin]──> PC 3.5" HD floppy
                                │
                           [SPI-väylä]
                                │
                          SRAM (64KB)
```

## Motivaatio

Vaihtoehto D:n (pelkkä Nano) suurin ongelma on 2 KB sisäinen RAM:
- Kokonainen MFM-raita = ~12.5 KB (ei mahdu)
- FAT-taulu = 4.5 KB (ei mahdu)
- Sektoripuskuri + IEC-puskuri + pino = ~1.5 KB

Lisäämällä ulkoinen SRAM-piiri saadaan riittävästi muistia ilman
Picon monimutkaisuutta, ja säilytetään Nanon 5V-etu (ei tasonmuuntimia).

## SRAM-vaihtoehdot

### Vaihtoehto 1: 23LC512 SPI SRAM (Valittu)

| Ominaisuus | Arvo |
|---|---|
| Valmistaja | Microchip |
| Kapasiteetti | 64 KB (512 Kbit) |
| Liitäntä | SPI (20 MHz max) |
| Käyttöjännite | 1.8V - 5.5V (**5V yhteensopiva!**) |
| Pinnit | 8 (DIP-8 / SOIC-8) |
| Hinta | ~1.5€ (uusi, Mouser/Digikey/eBay) |
| Arduino-pinnit | 4 kpl (MOSI, MISO, SCK, /CS) |
| Osoiteleveys | 16-bit (vs. 23LC1024:n 24-bit) |

**Miksi tämä:** LZ4-pakkauksen puskurit (~1 KB) mahtuvat ulkoiseen SRAM:iin,
vapauttaen ATmega328P:n 2 KB sisäisen RAM:in pinolle ja muuttujille.
Sama DIP-8 pinout ja SPI-komennot kuin 23LC256/23LC1024. Drop-in vaihto.

### Vaihtoehto 3: 62256 Parallel SRAM (vanhoista laitteista)

| Ominaisuus | Arvo |
|---|---|
| Kapasiteetti | 32 KB |
| Liitäntä | Rinnakkainen (15 osoite + 8 data) |
| Saatavuus | Vanhoista tietokoneista, pelikonsoleista |
| Ongelma | **Tarvitsee 23 I/O-pinniä — Nanossa ei riitä!** |

**Ei käytännöllinen Nanon kanssa** ilman shift-rekistereitä (74HC595)
osoiteväylän multipleksointiin. Mahdollinen mutta monimutkainen.

### Vaihtoehto 4: 6264 SRAM (C64:stä tai vastaavasta)

- 8 KB, rinnakkainen
- Löytyy vanhoista C64-koneista, Amiga-laajennuksista, jne.
- Sama ongelma kuin 62256: liikaa pinnejä

### Suositus: 23LC256 (SPI)

SPI-liitäntä on ylivoimainen Nano-projektissa:
- Vain 4 pinniä (vs. 23+ rinnakkaiselle)
- 20 MHz SPI → ~2 MB/s siirtonopeus
- 32 KB riittää (todellinen käyttö ~22 KB)
- 5V natiivi

## Muistikartta (23LC512, 64 KB)

```
SRAM-osoite    Käyttö                    Koko
───────────────────────────────────────────────────
0x00000-0x030CF  MFM-raitapuskuri          12.5 KB
0x030D0-0x042CF  FAT-taulun cache          4.5 KB
0x042D0-0x044CF  Sektoripuskuri #1         512 B
0x044D0-0x046CF  Sektoripuskuri #2         512 B
0x046D0-0x056CF  Hakemistopuskuri          4 KB
0x056D0-0x058CF  IEC-kanavan puskuri       512 B
0x058D0-0x05ACF  LZ4 raakadata-puskuri     512 B
0x05AD0-0x05D0B  LZ4 kehyspuskuri          556 B
0x05D0C-0x0FFFF  Vapaa (~41.2 KB)          käytettävissä
───────────────────────────────────────────────────
```

Nanon sisäinen 2 KB RAM käytetään:
- Pinolle ja muuttujille (~512 B)
- SPI-siirtopuskurille (~256 B)
- IEC-tilakoneelle (~256 B)
- MFM-dekoodauksen välipuskurille (~512 B)
- LZ4-pakkauksen aikana: hash-taulu pinossa (~512 B, väliaikainen)
- Loppuosa vapaana

## Komponenttilista

| Komponentti | Kuvaus | Hinta (arvio) |
|---|---|---|
| Arduino Nano (klooni) | ATmega328P, 16 MHz, 5V | ~3-5€ |
| 23LC512 | 64KB SPI SRAM, DIP-8 | ~1.5€ |
| 6-pin DIN -liitin | IEC-väylä C64:lle | ~2€ |
| 34-pin IDC -liitin | PC-floppy | ~1€ |
| 4-pin Berg liitin | Virta floppy-asemalle | ~1€ |
| Vastukset 1kΩ x4 | IEC-suojavastukset | ~0.5€ |
| Kond. 100nF x3 | Bypass | ~0.5€ |
| **Yhteensä** | | **~10-13€** |

## GPIO-allokaatio

### SPI-väylä (Nano ↔ 23LC256)

| Nano-pinni | SPI-signaali | 23LC256 pin | Huomio |
|---|---|---|---|
| D13 (SCK) | SPI Clock | pin 6 (SCK) | Jaettu floppy /WGATE kanssa! |
| D11 (MOSI) | SPI Data In | pin 5 (SI) | Jaettu floppy /STEP kanssa! |
| D12 (MISO) | SPI Data Out | pin 2 (SO) | Jaettu floppy /WDATA kanssa! |
| D10 | /CS (SRAM) | pin 1 (/CS) | - |

**ONGELMA:** SPI-pinnit (D11-D13) ovat samat kuin floppy-signaalit
vaihtoehdossa D. Tarvitaan uudelleenjärjestely!

### Päivitetty GPIO-kartta

```
IEC-väylä (suora 5V kytkentä):
  D2  = ATN (INT0 keskeytys)
  D3  = CLK
  D4  = DATA
  D5  = RESET

SPI SRAM:
  D10 = /CS (SRAM chip select)
  D11 = MOSI (SPI data out)  → 23LC256 SI
  D12 = MISO (SPI data in)   ← 23LC256 SO
  D13 = SCK  (SPI clock)     → 23LC256 SCK

Floppy output (uudelleenjärjestelty!):
  D6  = /DENSITY
  D7  = /MOTEA
  D9  = /MOTOR
  A0  = /DRVSEL  (siirretty analogipinniin)
  A1  = /DIR     (siirretty)
  A2  = /STEP    (siirretty)
  A3  = /WDATA   (siirretty)
  A4  = /WGATE   (siirretty)
  A5  = /SIDE1   (siirretty)

Floppy input:
  D8  = /RDATA   (ICP1 — Timer1 Input Capture!)

Ei enää vapaita pinnejä input-signaaleille /TRK00, /WPT, /DSKCHG!
```

**Pinnit loppuvat!** Arduino Nanossa on 22 digitaalista I/O-pinniä,
ja tarvitsemme: 4 (IEC) + 4 (SPI) + 10 (floppy out) + 4 (floppy in) = 22.

### Ratkaisu: SPI-väylän jakaminen + 74HC595 shift register

Lisätään yksi 74HC595 (shift register) floppy-output-signaalien
ohjaamiseen SPI-väylän kautta. Tämä vapauttaa 8 pinniä!

```
Floppy output-signaalit 74HC595:n kautta:
  SPI MOSI → 74HC595 SER (data in)
  SPI SCK  → 74HC595 SRCLK (shift clock)
  D6       → 74HC595 RCLK (latch clock)

  74HC595 QA = /SIDE1    (floppy pin 32)
  74HC595 QB = /DENSITY  (floppy pin 2)
  74HC595 QC = /MOTEA    (floppy pin 10)
  74HC595 QD = /DRVSEL   (floppy pin 12)
  74HC595 QE = /MOTOR    (floppy pin 16)
  74HC595 QF = /DIR      (floppy pin 18)
  74HC595 QG = /STEP     (floppy pin 20)
  74HC595 QH = /WGATE    (floppy pin 24)
```

**Päivitetty GPIO-kartta (74HC595:n kanssa):**

```
IEC-väylä:
  D2  = ATN (INT0)
  D3  = CLK
  D4  = DATA
  D5  = RESET

SPI (jaettu SRAM + 74HC595):
  D10 = /CS_SRAM
  D11 = MOSI  → SRAM SI + 74HC595 SER
  D12 = MISO  ← SRAM SO
  D13 = SCK   → SRAM SCK + 74HC595 SRCLK

Floppy (suorat GPIO):
  D6  = 74HC595 RCLK (latch)
  D7  = /WDATA (tarvitsee suora GPIO tarkan ajoituksen takia)
  D8  = /RDATA (ICP1 input capture)
  D9  = vapaa / LED

Floppy input:
  A0  = /TRK00 (input pullup)
  A1  = /WPT   (input pullup)
  A2  = /DSKCHG (input pullup)

Vapaat:
  A3-A5 = vapaat (debug, LED, jne.)
  D0-D1 = Serial (USB debug)
```

## Firmware-rakenne

```
┌──────────────────────────────────────┐
│        Arduino Nano Firmware         │
│        + 23LC512 SRAM               │
├──────────────────────────────────────┤
│  SPI SRAM -ajuri                     │
│  (sram_read, sram_write, sram_seq)   │
├──────────────────────────────────────┤
│  IEC-protokolla                      │
│  (INT0 ATN, bit-bang CLK/DATA)       │
├──────────────────────────────────────┤
│  Floppy-ohjaus                       │
│  (74HC595 output, ICP1 MFM-luku)    │
├──────────────────────────────────────┤
│  MFM decode/encode                   │
│  (ICP1 → SRAM raitapuskuri)          │
├──────────────────────────────────────┤
│  FAT12 (SRAM-pohjaisella cachella)   │
│  (koko FAT mahtuu SRAM:iin!)         │
├──────────────────────────────────────┤
│  CBM-DOS emulaatio                   │
│  (LOAD, SAVE, $, S:, R:, N:)        │
└──────────────────────────────────────┘

RAM-käyttö:
  Nano 2KB:  pino, SPI-puskuri, tilakone, välimuuttujat
  SRAM 64KB: raitapuskuri, FAT-cache, sektorit, hakemisto, LZ4-puskurit
```

### SPI SRAM -käyttö (23LC256)

```c
// 23LC256 SPI komennot (samat kuin 23LC1024)
#define SRAM_READ   0x03   // Lue data
#define SRAM_WRITE  0x02   // Kirjoita data
#define SRAM_RDMR   0x05   // Lue mode register
#define SRAM_WRMR   0x01   // Kirjoita mode register

// Mode: Sequential (oletusarvo, paras suorituskyky)
// Osoite kasvaa automaattisesti → voi lukea/kirjoittaa
// pitkiä lohkoja yhdellä SPI-transaktiolla.

// 23LC256 käyttää 16-bit osoitteita (vs. 23LC1024:n 24-bit)
void sram_read(uint32_t addr, uint8_t *buf, uint16_t len) {
    digitalWrite(CS_SRAM, LOW);
    SPI.transfer(SRAM_READ);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = SPI.transfer(0x00);
    }
    digitalWrite(CS_SRAM, HIGH);
}

void sram_write(uint32_t addr, const uint8_t *buf, uint16_t len) {
    digitalWrite(CS_SRAM, LOW);
    SPI.transfer(SRAM_WRITE);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        SPI.transfer(buf[i]);
    }
    digitalWrite(CS_SRAM, HIGH);
}
```

### MFM-luku SRAM-puskurilla

```
Ero vaihtoehtoon D:

Vaihtoehto D (ilman SRAM):
  - MFM dekoodataan lennosta, sektori kerrallaan
  - Jos oikea sektori ohitetaan, pitää odottaa koko kierros (~200ms)
  - Hidas ja epäluotettava

Vaihtoehto E (SRAM:lla):
  - Koko raita luetaan SRAM:iin (~12.5 KB)
  - Dekoodataan rauhassa SRAM:sta
  - Kaikki 18 sektoria saatavilla heti
  - Useampi sektori samalta raidalta = vain yksi luku

  Timer1 ICP ISR kirjoittaa suoraan SRAM:iin:
    ISR(TIMER1_CAPT_vect) {
        uint16_t interval = ICR1 - last_capture;
        last_capture = ICR1;
        // Kirjoita SRAM:iin (SPI single byte, nopea)
        sram_write_byte(track_write_ptr++, classify_pulse(interval));
    }
```

## Plussat

- **Ei tasonmuuntimia** — 5V natiivi (Nano + 23LC256 + floppy)
- **Riittävästi muistia** — 32 KB SRAM ratkaisee Nanon 2 KB rajoituksen
- Koko FAT-taulu mahtuu SRAM:iin → nopea tiedostohaku
- Raitapuskuri mahdollinen → luotettavampi MFM-dekoodaus
- Edullinen (~10-13€)
- Yksinkertaisempi kuin Pico-ratkaisu (ei tasonmuuntimia)

## Miinukset

- SPI SRAM lisää latenssia (~50µs per 512B sektoriluku SRAM:sta)
- 74HC595 shift register lisää yhden komponentin
- Floppy-signaalien ohjaus shift registerin kautta hieman hitaampaa
- Edelleen single-core — IEC ja floppy eivät voi toimia rinnakkain
- /WDATA tarvitsee suoran GPIO:n tarkan MFM-ajoituksen takia
- 32 KB Flash voi olla tiukka kaikelle firmwarelle
- SPI-väylän jakaminen SRAM:n ja 74HC595:n kesken vaatii huolellista CS-hallintaa

## Vertailu vaihtoehtoihin D ja A

```
┌──────────────────┬───────────────┬───────────────┬───────────────┐
│ Ominaisuus       │ D: Nano       │ E: Nano+SRAM  │ A: Pico       │
├──────────────────┼───────────────┼───────────────┼───────────────┤
│ RAM              │ 2 KB (!)      │ 2+64 KB       │ 264 KB        │
│ Tasonmuuntimet   │ 0 kpl         │ 0 kpl         │ 4x BSS138     │
│ Komponentteja    │ 7             │ 9 (+SRAM,595) │ 12            │
│ Hinta            │ ~8€           │ ~10-13€       │ ~12€          │
│ Raitapuskuri     │ Ei mahdu      │ Mahtuu (SRAM) │ Mahtuu (RAM)  │
│ FAT-cache        │ Osittainen    │ Kokonainen    │ Kokonainen    │
│ MFM-luotettavuus │ Heikko        │ Hyvä          │ Erinomainen   │
│ Kehitysnopeus    │ Hidas         │ Keskiverto    │ Nopea         │
│ Kytkennän helppous│ Helpoin      │ Helppo        │ Keskiverto    │
└──────────────────┴───────────────┴───────────────┴───────────────┘
```

## Toteutusvaiheet

1. SPI SRAM -ajurin toteutus ja testaus (read/write/sequential)
2. 74HC595 shift register floppy-outputille
3. IEC-protokolla (sama kuin vaihtoehto D)
4. MFM-luku Timer1 ICP:llä → SRAM-raitapuskuri
5. MFM-dekoodaus SRAM:sta
6. FAT12 SRAM-pohjaisella cachella
7. CBM-DOS (LOAD, SAVE, $)
8. MFM-kirjoitus (SRAM → floppy)
9. PCB-suunnittelu
10. Testaus ja viimeistely
