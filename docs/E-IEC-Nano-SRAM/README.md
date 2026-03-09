# E: IEC-väylä + Arduino Nano + SPI SRAM (Nano tehostettuna)

> C64:n IEC-sarjaväylä → Arduino Nano + 23K256 SRAM → PC 3.5" HD floppy

## Yhteenveto

Vaihtoehto D:n parannettu versio. Microchipin 23K256 SPI SRAM (32 KB) ratkaisee Nanon 2 KB muistirajoituksen. 74HC595 shift register ohjaa floppy-signaaleja SPI-väylän kautta, vapauttaen GPIO-pinnejä. Edelleen täysin 5V-natiivi — ei tasonmuuntimia.

## Tiedostot

| Tiedosto | Sisältö |
|---|---|
| [kuvaus.md](kuvaus.md) | SRAM-vaihtoehdot, muistikartta, SPI-väylän jakaminen, 74HC595-ohjaus, firmware-rakenne |
| [piirikaavio.md](piirikaavio.md) | 23K256 + 74HC595 SPI-kytkentä, kokonaispiirikaavio, bypass-kondensaattorit |
| [diagnostiikkapadit.md](diagnostiikkapadit.md) | 14 testipistettä: signaalit, odotetut tasot, mittausohje, vianetsintä |

## PCB

| Variantti | Mitat | Kerrokset |
|-----------|-------|-----------|
| 2-layer | 140.0 × 120.0 mm | F.Cu + B.Cu |
| 4-layer | 70.0 × 60.0 mm | F.Cu / GND / +5V / B.Cu |

![64korppu-E 4-layer PCB](../../hardware/E-IEC-Nano-SRAM/4-layer/64korppu-E-4layer-3d.png)

## Piirikaavio

![Vaihtoehto E: Nano + SRAM](img/piirikaavio.png)

## Avainominaisuudet

- **32 KB SRAM** — koko raita, FAT-taulu ja puskurit mahtuvat
- **Ei tasonmuuntimia** — 5V natiivi (Nano + 23K256 + 74HC595 + floppy)
- **SPI-väylän jakaminen** — SRAM ja shift register samalla väylällä
- **Edullinen** (~10-13€)

## Hinta: ~10-13€

## Haaste

SPI SRAM lisää hieman latenssia. Single-core (ei rinnakkaista IEC + floppy). 32 KB Flash voi olla tiukka.
