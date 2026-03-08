# Vaihtoehto D: IEC-väylä + Arduino Nano (Edullinen klassikko)

## Arkkitehtuuri

```
C64 ──[IEC-sarjaväylä]──> Arduino Nano ──[34-pin]──> PC 3.5" HD floppy
```

## Toimintaperiaate

- Arduino Nano emuloi CBM-levyasemaa (device #8) IEC-sarjaväylällä
- Nano ohjaa PC-korppuasemaa suoraan 34-pin Shugart-liitännän kautta
- Nano hoitaa FAT12-tiedostojärjestelmän tulkinnan
- C64:lle ei tarvita ROM-muutoksia

## Miksi Arduino Nano?

1. **5V-logiikka** — ei tarvita tasonmuuntimia IEC-väylälle lainkaan!
2. **ATmega328P** — tuttu ja hyvin dokumentoitu
3. **Edullinen** (~3-5€ klooni)
4. **Arduino-ekosysteemi** — helppo kehitysympäristö
5. **Pieni koko** — sopii kompaktiin koteloon

## Haasteet vs. Pico

| Ominaisuus | Arduino Nano | Raspberry Pi Pico |
|---|---|---|
| CPU | 16 MHz AVR 8-bit | 2x 133 MHz ARM 32-bit |
| RAM | **2 KB** | 264 KB |
| Flash | 32 KB | 2 MB |
| I/O-pinnit | 22 (digitaali) | 26 GPIO |
| Logiikkataso | **5V (natiivi!)** | 3.3V |
| PIO | Ei | 2x PIO (8 SM) |
| Hinta | ~3-5€ | ~4-5€ |

**Kriittinen rajoitus: 2 KB RAM.** Tämä ei riitä kokonaisen raidan (12.5 KB MFM) tai edes FAT-taulun (4.5 KB) puskurointiin. Ratkaisu:

- **Sektori kerrallaan** -käsittely (512 tavua + overhead)
- FAT-taulua luetaan osa kerrallaan
- Ei raitapuskurointia — jokainen sektori luetaan/kirjoitetaan erikseen

## Komponenttilista

| Komponentti | Kuvaus | Hinta (arvio) |
|---|---|---|
| Arduino Nano (klooni) | ATmega328P, 16 MHz, 5V | ~3-5€ |
| 6-pin DIN -liitin | IEC-väylä C64:lle | ~2€ |
| 34-pin IDC -liitin | PC-floppy -liitäntä | ~1€ |
| 4-pin Berg liitin | Virta floppy-asemalle | ~1€ |
| Vastukset 1kΩ x3 | IEC-väylän suojavastukset | ~0.5€ |
| Kond. 100nF x2 | Bypass | ~0.5€ |
| **Yhteensä** | | **~8-10€** |

**Huom:** Ei tarvita tasonmuuntimia! Nano on natiivisti 5V, sama kuin IEC-väylä.

## GPIO-allokaatio

### IEC-väylä (suora kytkentä, 5V ↔ 5V)

| IEC-pin | Signaali | Nano-pinni | Arduino I/O | Suunta |
|---|---|---|---|---|
| 3 | ATN | D2 | Digital 2 | Input (interrupt) |
| 4 | CLK | D3 | Digital 3 | Bidirectional |
| 5 | DATA | D4 | Digital 4 | Bidirectional |
| 6 | RESET | D5 | Digital 5 | Input |
| 2 | GND | GND | GND | - |

### Floppy-asema (suora kytkentä, 5V ↔ 5V)

| 34-pin | Signaali | Nano-pinni | Arduino I/O | Suunta |
|---|---|---|---|---|
| 2 | /DENSITY | D6 | Digital 6 | Output |
| 10 | /MOTEA | D7 | Digital 7 | Output |
| 12 | /DRVSB | D8 | Digital 8 | Output |
| 16 | /MOTOR | D9 | Digital 9 | Output |
| 18 | /DIR | D10 | Digital 10 | Output |
| 20 | /STEP | D11 | Digital 11 | Output |
| 22 | /WDATA | D12 | Digital 12 | Output |
| 24 | /WGATE | D13 | Digital 13 | Output |
| 26 | /TRK00 | A0 | Analog 0 | Input |
| 28 | /WPT | A1 | Analog 1 | Input |
| 30 | /RDATA | A2 | Analog 2 | Input |
| 32 | /SIDE1 | A3 | Analog 3 | Output |
| 34 | /DSKCHG | A4 | Analog 4 | Input |

### Vapaat pinnit
- A5: LED tai debug
- D0/D1: Serial (USB debug)
- A6/A7: Vain analoginen input (ei käytetä)

## Firmware-rakenne

```
┌──────────────────────────────┐
│      Arduino Nano Firmware   │
│      (Single core AVR)       │
├──────────────────────────────┤
│  IEC-protokolla              │
│  (bit-bang, INT0 ATN-keskeyt)│
├──────────────────────────────┤
│  Floppy-ohjaus               │
│  (GPIO bit-bang)             │
├──────────────────────────────┤
│  MFM decode/encode           │
│  (Timer1 capture/compare)    │
├──────────────────────────────┤
│  FAT12 (sektoripohjainen)   │
│  (512B puskuri + FAT-cache)  │
├──────────────────────────────┤
│  CBM-DOS emulaatio           │
│  (LOAD, SAVE, $)             │
└──────────────────────────────┘

RAM-käyttö (~2048 tavua):
  512B  sektoripuskuri
  512B  FAT-osacache (170 FAT-entryä)
  256B  IEC-kanavan puskuri
  256B  pino + muuttujat
  512B  MFM-dekoodauspuskuri (osittainen)
```

## MFM-lukustrategia (rajallinen RAM)

Koska kokonainen raita ei mahdu muistiin, käytetään **reaaliaikaista sektoridekoodausta**:

1. Käynnistä Timer1 Input Capture -tilaan (A2/ICP-pinni)
2. Mittaa flux-transitioiden välit reaaliajassa
3. Dekoodaa MFM bitit lennosta
4. Tunnista IDAM (sync + 0xFE) ja lue sektori-ID
5. Jos oikea sektori: dekoodaa 512 data-tavua suoraan puskuriin
6. Jos väärä sektori: ohita ja odota seuraavaa

Tämä vaatii tarkkaa ajoitusta:
- 500 kbps → 2µs/bitti → 32 CPU-sykliä @ 16 MHz
- Riittävä, mutta ei paljon marginaalia

## Plussat

- **Ei tasonmuuntimia IEC-väylälle** — 5V natiivi!
- Edullisin vaihtoehto (~8€)
- Yksinkertainen kytkentä
- Tuttu Arduino-ympäristö
- Ei vaadi C64:n modifiointia
- Toimii LOAD/SAVE:lla

## Miinukset

- **2 KB RAM on erittäin vähän** — monimutkainen muistinhallinta
- 16 MHz single-core — tiukat ajoitukset MFM:lle
- 32 KB Flash — koodin pitää olla kompaktia
- Ei PIO:ta — MFM-koodaus on puhtaasti ohjelmistopohjainen
- MFM-kirjoitus erittäin haastava toteuttaa tarkalla ajoituksella
- Ei dual-core — IEC ja floppy eivät voi toimia rinnakkain
- Hakemistolistaus hitaampi (FAT luetaan pätkissä)

## Toteutusvaiheet

1. IEC-väylän perusprotokolla (ATN-keskeytys + bit-bang)
2. Floppy moottori/seek/side GPIO-ohjaus
3. MFM-dekoodaus Timer1 Input Capturella
4. FAT12 sektoripohjaisella lukemisella
5. LOAD-komento (lue .PRG flopylta → lähetä IEC:n yli)
6. SAVE-komento (vastaanota IEC:n yli → kirjoita flopylle)
7. Hakemistolistaus ($)
8. PCB-suunnittelu
