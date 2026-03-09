# LZ4-pakkaus IEC-väylällä — Design Document

**Päivämäärä:** 2026-03-09
**Kohde:** firmware/E-IEC-Nano-SRAM (Arduino Nano + 23LC256)
**Liittyy:** karskiROM (erillinen projekti, custom C64 KERNAL ROM)

## Tavoite

Lisätä LZ4-pakkaus 64korppu-firmwareen siten, että Arduino Nano pakkaa
tiedostodatan ja hakemistolistauksen lennossa ennen IEC-siirtoa. C64:n
custom KERNAL ROM (karskiROM) purkaa datan vastaanottaessa.

Pakkaus on opt-in: aktivoidaan komentokanavallla. Ilman aktivointia
kaikki toimii kuten ennenkin — normaali C64, JiffyDOS ja EPYX.

## Arkkitehtuuri

### Protokollakerrokset

```
Kerros 3:  Pakkaus      LZ4 lohkoprotokolla     ← XZ-komentokanavasopimus
Kerros 2:  Siirto       JiffyDOS 2-bit / std    ← autodetect ATN-ajoituksesta
Kerros 1:  Fyysinen     IEC CLK+DATA             ← aina sama rauta
```

Pakkaus on siirtokerroksen yläpuolella — sama lohkoprotokolla toimii
sekä standardi IEC:llä että JiffyDOS:lla.

### Yhteensopivuusmatriisi

| C64 ROM | Asema | Siirto | Pakkaus | Nopeus |
|---------|-------|--------|---------|--------|
| Normaali KERNAL | 64korppu | Standardi IEC | Ei | ~0.4 KB/s |
| JiffyDOS ROM | 64korppu | 2-bit | Ei | ~5 KB/s |
| EPYX cartridge | 64korppu | EPYX | Ei | ~3 KB/s |
| **karskiROM** | **64korppu** | **2-bit + LZ4** | **Kyllä** | **~9 KB/s** |
| karskiROM | Normaali 1541 | Standardi IEC | Ei | ~0.4 KB/s |
| karskiROM | JiffyDOS 1541 | 2-bit | Ei | ~5 KB/s |

### 2-bit-siirtoprotokolla (JiffyDOS-yhteensopiva)

karskiROM käyttää täsmälleen samaa 2-bit-siirtoprotokollaa kuin JiffyDOS
wire-tasolla. Nano ei tiedä eikä tarvitse tietää kumpi ROM on kyseessä.

Tunnistus on kaksisuuntainen:
1. C64 (karskiROM) pitää DATA:n alhaalla ~260µs ATN:n jälkeen
2. Asema (64korppu) tunnistaa JiffyDOS-handshaken → 2-bit mode
3. Jos asema ei tunnista (normaali 1541) → fallback standardi IEC

## XZ-komentokanava

C64 aktivoi pakkauksen lähettämällä komennon komentokanavallle (SA 15):

```
OPEN 15,8,15,"XZ:1"    → pakkaus päälle
OPEN 15,8,15,"XZ:0"    → pakkaus pois (oletus)
OPEN 15,8,15,"XZ:S"    → kysy tila → Nano vastaa "XZ:0" tai "XZ:1"
```

Asetus on globaali ja pysyy voimassa kunnes:
- `XZ:0` lähetetään
- Laite resetoidaan
- IEC bus reset (ATN+RESET)

karskiROM lähettää `XZ:1` automaattisesti bootissa kun tunnistaa
64korppu-laitteen.

## Lohkoprotokolla

Kun pakkaus on aktiivinen, tiedostodata ja hakemistolistaus lähetetään
lohkoissa standardi-IEC-tavuvirran sijaan:

```
Per lohko:
  [2 tavua]  pakattu koko (N), little-endian, 1-512
  [2 tavua]  raakakoko (M), little-endian, 1-256
  [N tavua]  LZ4-pakattu data

EOF:
  [2 tavua]  0x0000 = ei enää lohkoja
```

- Raakakoko M on tyypillisesti 256 tavua (viimeinen lohko voi olla lyhyempi)
- Jos data ei pakkaudu, N ≥ M ja lohko sisältää literaaleja
- C64 lukee headerin (4 tavua), lukee N tavua, purkaa → M tavua muistiin

### Miksi 256B lohkokoko?

- Mahtuu ATmega328P:n sisäiseen RAMiin
- LZ4 hash-taulu (256 × 2B = 512B) on järkevän kokoinen
- FAT12-sektorikoko on 512B → 2 lohkoa per sektori
- C64:llä 256B on sivun kokoinen → helppo osoitelogiikka

## LZ4-pakkaaja (Nano-puoli)

Minimaalinen LZ4-block-pakkaaja. Ei LZ4-frame-headeria — oma
lohkoprotokolla hoitaa kehystyksen.

### LZ4-formaatti (lohkon sisällä)

```
LZ4 sequence:
  [token]     4 ylintä bittiä = literal-pituus, 4 alinta = match-pituus
  [literal*]  0-N literaalitavua
  [offset]    2 tavua little-endian, match-etäisyys taaksepäin
  [match-ext] lisäpituustavut jos match > 15+4
```

Standardin LZ4 block format, joten C64-puolen purku on yhteensopiva
minkä tahansa LZ4-purkaimen kanssa.

### Hash-taulu

- 256 entryä × 2 tavua = 512B sisäistä RAMia
- Allokoidaan pinosta pakkausfunktion ajaksi
- Vapautuu automaattisesti funktion palatessa
- Pienempi hash (128 × 2B = 256B) vaihtoehto jos RAM loppuu

## Resurssit

| Resurssi | Nykyinen | LZ4 lisää | Yhteensä |
|----------|----------|-----------|----------|
| Flash | 13,342B (40.7%) | ~800B | ~14,142B (43.2%) |
| Sisäinen RAM | 1,317B (64.3%) | ~260B (hash pinosta) | ~1,577B (77.0%) |
| Ulkoinen SRAM | 22,736B | 0B | 22,736B |

## Tiedostot

| Tiedosto | Toimenpide |
|----------|-----------|
| `include/lz4_compress.h` | Uusi: LZ4-pakkaajan rajapinta |
| `src/lz4_compress.c` | Uusi: LZ4-block-pakkaaja |
| `include/compress_proto.h` | Uusi: lohkoprotokolla + XZ-tilanhallinta |
| `src/compress_proto.c` | Uusi: pakattu lähetys, lohkokehystys |
| `src/cbm_dos.c` | Muokkaus: XZ-parsinta, talk-loopin pakkaus |
| `include/config.h` | Muokkaus: pakkausasetukset |
| `test/test_lz4_compress.c` | Uusi: host-yksikkötestit |

## Testaus

### Host-testit (~10-15 testiä)

- LZ4-pakkaus tunnetulla datalla → vertaa odotettuun
- Roundtrip: pakkaa + pura (referenssi-LZ4-purkajalla)
- Lohkoprotokollan kehystys ja parsinta
- XZ-komennon parsinta (XZ:0, XZ:1, XZ:S, virheelliset)
- Reunatapaukset: tyhjä data, ei-pakattava data, tasan 256B
- Hakemistolistauksen pakkaus

### Manuaalinen testaus

- VICE-emulaattori + karskiROM → lataus 64korppu-laitteelta
- Vertaa pakatun ja pakkaamattoman siirron nopeutta
- Yhteensopivuus: normaali C64 + 64korppu (ei pakkausta)
- Yhteensopivuus: JiffyDOS C64 + 64korppu (ei pakkausta)

## Toteutusjärjestys

1. `lz4_compress.c/.h` — pakkaaja + yksikkötestit
2. `compress_proto.c/.h` — lohkoprotokolla + XZ-tilanhallinta
3. `cbm_dos.c` integraatio — talk-loopin muutos
4. Host-testit kaikille komponenteille
5. Makefile-päivitys
