# D: IEC-väylä + Arduino Nano (Edullinen klassikko)

> C64:n IEC-sarjaväylä → Arduino Nano → PC 3.5" HD floppy

## Yhteenveto

Arduino Nano emuloi CBM-levyasemaa IEC-väylällä. Nanon 5V-logiikka on suoraan yhteensopiva sekä IEC-väylän että floppy-aseman kanssa — tasonmuuntimia ei tarvita lainkaan!

## Tiedostot

| Tiedosto | Sisältö |
|---|---|
| [kuvaus.md](kuvaus.md) | Arkkitehtuuri, GPIO-kartta, Timer1 ICP MFM-strategia, RAM-rajoitukset |
| [piirikaavio.md](piirikaavio.md) | Suora 5V-kytkentä, ICP1 MFM-luku, komponenttilista |

## Avainominaisuudet

- **Ei tasonmuuntimia!** — 5V natiivi, yksinkertaisin kytkentä
- **Edullisin vaihtoehto** (~8€)
- **Tuttu Arduino-ympäristö**

## Hinta: ~8-10€

## Haaste

Vain 2 KB RAM — MFM-raita (12.5 KB) ja FAT-taulu (4.5 KB) eivät mahdu muistiin. Vaatii sektori-kerrallaan reaaliaikaista dekoodausta. Katso vaihtoehto E ratkaisulle.
