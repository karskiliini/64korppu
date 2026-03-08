# Vaihtoehto B: Expansion Port + FDC-piiri (Nopein)

## Arkkitehtuuri

```
C64 ──[Expansion port]──> PCB [FDC + logiikka] ──[34-pin]──> PC 3.5" HD floppy
```

## Toimintaperiaate

- FDC-piiri (esim. WD37C65 tai Intel 82077AA) ohjaa korppuasemaa
- FDC muistimapitetaan C64:n osoiteavaruuteen expansion portin kautta ($DExx/$DFxx)
- Custom ROM tai ladattava ajuri C64-puolella käsittelee FAT12:n ja tarjoaa LOAD/SAVE

## Komponenttilista

| Komponentti | Kuvaus | Hinta (arvio) |
|---|---|---|
| WD37C65 / 82077AA | Floppy Disk Controller | ~5-15€ (käytetty) |
| 74LS138 | Osoitedekooderi | ~1€ |
| 74LS245 x2 | Väylä buffer (data) | ~2€ |
| 74LS04 | Invertteriportit | ~1€ |
| 8KB EEPROM (28C64) | Custom ROM | ~3€ |
| 34-pin IDC -liitin | PC-floppy | ~1€ |
| 44-pin edge connector | Expansion port | ~3€ |
| Kide 16 MHz + 24 MHz | FDC:n kellotus | ~2€ |
| Vastukset, kondensaattorit | Pull-upit, suodatus | ~2€ |
| **Yhteensä** | | **~20-30€** |

## PCB-vaatimukset

### Osoitedekoodaus

FDC mapataan I/O-alueelle $DE00-$DE0F:

```
A15-A8 = $DE (1101 1110)
A7-A4  = $0  (0000)
A3-A0  → FDC:n rekisterivalinta

/IO1 (active low kun $DE00-$DEFF) + A7-A4 dekoodaus → FDC /CS
```

### FDC-rekisterit

| Osoite | Rekisteri | R/W |
|---|---|---|
| $DE00 | Status Register A (SRA) | R |
| $DE01 | Status Register B (SRB) | R |
| $DE02 | Digital Output Register (DOR) | W |
| $DE03 | Tape Drive Register | - |
| $DE04 | Main Status Register (MSR) | R |
| $DE04 | Data Rate Select Register (DSR) | W |
| $DE05 | Data Register (FIFO) | R/W |
| $DE07 | Digital Input Register (DIR) | R |
| $DE07 | Configuration Control Register (CCR) | W |

### Väylä-bufferointi

- C64 dataväylä (D0-D7) ↔ 74LS245 ↔ FDC data
- C64 osoiteväylä (A0-A3) → FDC A0-A3
- /IO1 + dekoodaus → FDC /CS
- C64 R/W → FDC /RD, /WR
- C64 Φ2 → ajoitusreferenssi

## Firmware/ROM-rakenne

```
$8000-$9FFF  Custom cartridge ROM (8KB)
├── FAT12 tiedostojärjestelmäajuri
├── FDC-ohjausrutiinit
├── LOAD/SAVE hook (KERNAL vectorit $0330-$0333)
├── Hakemistolistaus ($ komento)
└── Komentotulkki (formatointi, tiedostohallinta)
```

## Plussat

- Nopein tiedonsiirto (kymmeniä KB/s)
- FDC hoitaa MFM-koodauksen ja matalan tason ohjauksen
- Lähimpänä "oikeaa" levyohjainta
- Ei tarvita MCU:ta

## Miinukset

- Vie expansion portin (tarvitaan port expander muille laitteille)
- WD37C65/82077AA -piirit vaikeasti saatavilla uutena
- Monimutkaisin PCB-suunnittelu (osoitedekoodaus, väylä buffering, ajoitus)
- Vaatii custom ROM:in tai ajurin C64-puolella
- FDC:n DMA ei ole käytettävissä C64:ssä → polled I/O (hitaampi)
- 5V-logiikka, ei tasonmuunnosongelmaa mutta vanhat komponentit

## Toteutusvaiheet

1. FDC-piirin hankinta ja datasheet-tutkimus
2. Osoitedekoodauksen suunnittelu ja prototyyppi
3. FDC-ohjausrutiinien kirjoittaminen 6502-assemblylla
4. FAT12-ajurin toteutus
5. KERNAL-vektorien hookkaus LOAD/SAVE:lle
6. PCB-suunnittelu
7. ROM-polttaminen ja testaus
