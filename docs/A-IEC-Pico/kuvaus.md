# Vaihtoehto A: IEC-väylä + Raspberry Pi Pico (Suositeltu)

## Arkkitehtuuri

```
C64 ──[IEC-sarjaväylä]──> Raspberry Pi Pico ──[34-pin]──> PC 3.5" HD floppy
```

## Toimintaperiaate

- Pico emuloi CBM-levyasemaa (device #8-11) IEC-sarjaväylällä
- Pico ohjaa PC-korppuasemaa suoraan 34-pin Shugart-liitännän kautta
- Pico hoitaa FAT12-tiedostojärjestelmän tulkinnan
- C64:lle ei tarvita ROM-muutoksia peruskäytössä

## Komponenttilista

| Komponentti | Kuvaus | Hinta (arvio) |
|---|---|---|
| Raspberry Pi Pico | RP2040 MCU, 264KB RAM, 2x PIO | ~5€ |
| 74LVC245 x2 | Tasonmuunnin 3.3V ↔ 5V (IEC-väylä) | ~1€ |
| 6-pin DIN -liitin | IEC-väylä C64:lle | ~2€ |
| 34-pin IDC -liitin | PC-floppy -liitäntä | ~1€ |
| 4-pin Molex/Berg | Virta floppy-asemalle | ~1€ |
| Jänniteregulaattori | 5V → 3.3V (jos Picoa syötetään 5V:sta) | ~1€ |
| Vastukset, kondensaattorit | Pull-upit, suodatus | ~1€ |
| **Yhteensä** | | **~12€** |

## PCB-vaatimukset

### Kytkennät

#### IEC-väylä (6-pin DIN → Pico, 74LVC245:n kautta)

| IEC-pin | Signaali | Pico GPIO | Suunta |
|---|---|---|---|
| 1 | SRQ (ei käytetä) | - | - |
| 2 | GND | GND | - |
| 3 | ATN | GP2 | Bidirectional |
| 4 | CLK | GP3 | Bidirectional |
| 5 | DATA | GP4 | Bidirectional |
| 6 | RESET | GP5 | Input |

**Huom:** IEC-signaalit ovat open collector -tyyppisiä. 74LVC245 tasonmuuntimen lisäksi tarvitaan open-drain -konfiguraatio tai erillinen ohjauslogiikka.

#### Floppy-asema (34-pin IDC → Pico)

| 34-pin | Signaali | Pico GPIO | Suunta |
|---|---|---|---|
| 2 | /DENSITY | GP6 | Output (HIGH=HD) |
| 10 | /MOTEA | GP7 | Output |
| 12 | /DRVSB | GP8 | Output (Drive Select B) |
| 16 | /MOTOR | GP9 | Output |
| 18 | /DIR | GP10 | Output |
| 20 | /STEP | GP11 | Output |
| 22 | /WDATA | GP12 | Output |
| 24 | /WGATE | GP13 | Output |
| 26 | /TRK00 | GP14 | Input |
| 28 | /WPT | GP15 | Input |
| 30 | /RDATA | GP16 | Input |
| 32 | /SIDE1 | GP17 | Output |
| 34 | /DSKCHG | GP18 | Input |
| Parittomat | GND | GND | - |

## Firmware-rakenne

```
┌─────────────────────────────────┐
│         Pico Firmware           │
├────────────┬────────────────────┤
│  Core 0    │     Core 1         │
│  IEC-proto │  Floppy-ohjaus     │
│  FAT12 FS  │  MFM encode/decode │
│  Komennot  │  Track seek/read   │
├────────────┴────────────────────┤
│  Jaettu puskuri (track data)    │
└─────────────────────────────────┘
```

### Moduulit

1. **iec_protocol** - ATN, CLK, DATA -signaalien käsittely PIO:lla, laite-emulaatio
2. **floppy_ctrl** - moottori, step, head select, read/write gate
3. **mfm_codec** - MFM-koodaus/dekoodaus PIO:lla
4. **fat12** - FAT-taulun, hakemiston ja tiedostojen käsittely
5. **cbm_dos** - CBM-DOS -emulaatio (LOAD, SAVE, OPEN, $, S:, R:, N:)

## Plussat

- Ei vaadi C64:n modifiointia
- Toimii kaiken C64-ohjelmiston kanssa (LOAD/SAVE)
- Yksinkertaisin PCB-suunnittelu
- Pico on edullinen ja helposti saatavilla
- PIO mahdollistaa tarkan ajoituksen sekä IEC- että floppy-protokollille
- Dual-core: IEC ja floppy-ohjaus rinnakkain

## Miinukset

- IEC-väylä on hidas (~1-3 KB/s vakiona, ~5-10 KB/s JiffyDOS:lla)
- FAT12 8.3-tiedostonimet vs. CBM 16-merkkiset nimet (tarvitaan nimeämiskäytäntö)

## Toteutusvaiheet

1. Floppy-aseman perusohjaus Picolla (moottori, seek, luku)
2. MFM-dekoodaus ja sektorien luku
3. FAT12-tiedostojärjestelmän tulkinta
4. IEC-protokollan toteutus PIO:lla
5. CBM-DOS -emulaatio
6. LOAD/SAVE/$ -komennot
7. PCB-suunnittelu KiCadilla
8. Testaus ja viimeistely
