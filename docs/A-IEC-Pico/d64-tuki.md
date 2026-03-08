# D64 Disk Image -tuki

## Yleiskuvaus

64korppu tukee D64 disk image -tiedostoja. PC:llä kopioidaan `.D64`-tiedosto FAT12-levylle,
ja firmware "mounttaa" sen RAM-puskuriin. Tämän jälkeen D64:n sisältämät tiedostot
ovat käytettävissä normaalisti IEC-väylän kautta.

Tämä mahdollistaa valtavan C64-ohjelmakirjaston käytön: netistä ladattu `.D64`-image
kopioidaan FAT12-korppulevylle, ja C64 lataa suoraan.

## D64-formaatti

Standardi 1541 disk image (35 raitaa):
- 174 848 tavua (683 sektoria × 256 tavua)
- Raidat 1–17: 21 sektoria/raita
- Raidat 18–24: 19 sektoria/raita
- Raidat 25–30: 18 sektoria/raita
- Raidat 31–35: 17 sektoria/raita

Raita 18, sektori 0 = BAM (Block Availability Map) + levyn nimi.
Raita 18, sektori 1+ = Hakemisto (linkitetty lista, 8 merkintää/sektori).

## Käyttöohjeet

### Levyn valmistelu PC:llä

1. Lataa haluamasi `.D64`-tiedosto (esim. `game.d64`)
2. Kopioi FAT12-levylle: `copy game.d64 A:`

### C64-käyttö

```
LOAD "$",8                     — näyttää FAT12-hakemiston (.D64-tiedostot näkyvissä)
OPEN 15,8,15,"CD:GAME.D64"    — mounttaa D64-image (lataa RAM:iin)
LOAD "$",8                     — näyttää D64:n sisäisen hakemiston
LOAD "PELI",8                  — lataa PRG D64:n sisältä
SAVE "OMA.PRG",8               — tallentaa D64-imageen
OPEN 15,8,15,"S:VANHA"        — poistaa tiedoston D64-imagesta
OPEN 15,8,15,"CD:.."           — palaa FAT12-juureen (flush + unmount)
```

### CD-komennon syntaksi

| Komento | Toiminto |
|---|---|
| `CD:GAME.D64` | Mounttaa D64-image |
| `CD:..` | Unmounttaa D64 ja palaa FAT12-juureen |

Komento lähetetään komentokanavalle (SA 15), kuten SD2IEC:ssä ja muissa
moderneissa C64-levyasemaemulaattoreissa.

## Kirjoitustuki (write-back)

Kun D64 on mountattu ja käyttäjä tekee SAVE:n tai muun kirjoitusoperaation:
1. Kirjoitus kohdistuu RAM-puskuriin (nopea)
2. Unmount-hetkellä (`CD:..`) muutokset kirjoitetaan takaisin FAT12-levylle
3. Koko D64-puskuri (174 KB) kirjoitetaan kerralla (~5 s)
4. PC näkee päivitetyn `.D64`-tiedoston normaalisti

## RAM-käyttö

| Komponentti | Koko |
|---|---|
| Pico SRAM yhteensä | 264 KB |
| Firmware (arvio) | ~30–35 KB |
| D64-puskuri | ~171 KB |
| Jäljellä | ~58–63 KB |

## Toteutus

### Firmware-moduulit

| Tiedosto | Rooli |
|---|---|
| `firmware/include/d64.h` | D64-tietorakenteet, julkinen API |
| `firmware/src/d64.c` | D64-parsinta, mount/flush/unmount, BAM-hallinta, tiedosto-I/O |
| `firmware/src/cbm_dos.c` | `d64_mode`-haarautuminen, CD:-komento, D64-moodi LOAD/SAVE:lle |

### Toimintaperiaate

1. **Mount** (`d64_mount`): lukee koko 174 848 tavun D64-imagen FAT12-levyltä Picon RAM-puskuriin 512 tavun paloissa
2. **Luku/kirjoitus**: kaikki D64-operaatiot kohdistuvat suoraan RAM-puskuriin — ei levyhakuja, erittäin nopea
3. **Hakemisto**: parsii raita 18:n linkitetyn hakemistolistan, muuntaa CBM BASIC -listaukseksi
4. **T/S-ketjut**: seuraa track/sector-ketjua lohko kerrallaan (tavu 0–1 = seuraava T/S, tavut 2–255 = data)
5. **BAM**: allokoi/vapauttaa lohkoja suoraan BAM-sektorista (raita 18, sektori 0)
6. **Flush** (`d64_flush`): kirjoittaa koko RAM-puskurin takaisin FAT12-tiedostoksi 512 tavun paloissa
7. **CBM-DOS-integraatio**: `cbm_dos.c`:n `d64_mode`-lippu ohjaa kaikki LOAD/SAVE/$/S:-operaatiot D64-moduulille

### Hakemistolistauksen muoto

D64-moodissa `LOAD "$",8` tuottaa:
```
0 "LEVYN NIMI      " 64 D64
23   "OHJELMA"         PRG
5    "DATA"            SEQ
640 BLOCKS FREE.
```

Otsikkorivillä näkyy D64:n levynimi ja "64 D64" -tunniste (vrt. FAT12-moodissa "64 FAT").

## Rajoitukset

- **Vain 35-track** — standardi D64 (174 848 tavua), ei 40-track extended
- **Vain Pico (vaihtoehto A)** — Nano-variantit eivät pysty (RAM ei riitä)
- **Yksi D64 kerrallaan** — vain yksi mountattu image
- **Flush on hidas** — koko 174 KB kirjoitetaan levylle unmountissa (~5 s)
- **Tuetut tiedostotyypit** — PRG, SEQ, USR, REL (hakemistossa näkyvät kaikki)

## Testaus

D64-moduulille on kattavat host-testit (ei tarvita Pico-laitteistoa):

```bash
cd test
make test          # 27 yksikkötestiä (mount, dir, read, write, delete, BAM, flush)
make               # kääntää myös IEC bridge -integraatiotestin
```

Katso [test/vice/README.md](../../test/vice/README.md) IEC bridge -integraatiotestauksesta.
