# LZ4-pakkausprotokolla — 64korppu ↔ karskiROM

Tämä dokumentti määrittelee LZ4-pakkausprotokollan 64korppu-laitteen
(Arduino Nano) ja karskiROM:n (C64 custom KERNAL) välillä.

## Yleiskatsaus

```
Arduino Nano (64korppu)              C64 (karskiROM)
┌─────────────────────┐              ┌─────────────────────┐
│ Lue sektori         │              │ Custom LOAD-rutiini │
│ SRAM:sta / FAT12    │              │                     │
│       ↓             │   IEC-väylä  │       ↓             │
│ Pakkaa LZ4          │ ──────────→  │ Pura LZ4            │
│ lohkoprotokollalla  │  2-bit tai   │ suoraan kohde-      │
│                     │  std IEC     │ osoitteeseen        │
└─────────────────────┘              └─────────────────────┘
```

## Protokollakerrokset

```
Kerros 3:  Pakkaus      LZ4 lohkoprotokolla     ← XZ-komentokanavasopimus
Kerros 2:  Siirto       JiffyDOS 2-bit / std    ← autodetect ATN-ajoituksesta
Kerros 1:  Fyysinen     IEC CLK+DATA             ← aina sama rauta
```

Jokainen kerros on itsenäinen. Pakkaus toimii minkä tahansa
siirtokerroksen päällä.

## 2-bit-siirtoprotokolla

karskiROM käyttää täsmälleen samaa wire-tason protokollaa kuin JiffyDOS.
Asema (64korppu tai JiffyDOS 1541) ei erota karskiROM:ia JiffyDOS:sta.

### Handshake

1. C64 vapauttaa ATN-linjan
2. C64 pitää DATA:n alhaalla ~260µs (200–320µs)
3. Asema mittaa pulssin keston
   - 200–320µs → JiffyDOS/karskiROM tunnistettu → 2-bit mode
   - Muuten → standardi IEC

### Tavunsiirto (2-bit, 4 kierrosta)

```
Kierros   CLK      DATA     Bitit
1         bit 0    bit 1    LSB-pari
2         bit 2    bit 3
3         bit 4    bit 5
4         bit 6    bit 7    MSB-pari
```

Ajoitukset:
- 13µs per kierros → 52µs per tavu
- EOI: 200µs viive ennen viimeistä tavua
- Tavujen välissä: 30µs

## XZ-komentokanava

Pakkaus aktivoidaan eksplisiittisesti komentokanavallla (SA 15).
Ilman aktivointia kaikki data lähetetään pakkaamattomana.

### Komennot

```
XZ:1    Aktivoi LZ4-pakkaus
XZ:0    Poista LZ4-pakkaus käytöstä (oletus)
XZ:S    Kysy tila → vastaus "XZ:0" tai "XZ:1"
```

### Käyttö BASIC:sta (karskiROM hoitaa automaattisesti)

```basic
OPEN 15,8,15,"XZ:1" : CLOSE 15
LOAD "OHJELMA",8,1
```

### Tilan elinikä

- Asetus pysyy voimassa kunnes `XZ:0`, laite-reset tai IEC bus reset
- karskiROM lähettää `XZ:1` automaattisesti kun tunnistaa 64korppu-laitteen
- Standardi JiffyDOS ROM ei lähetä XZ-komentoja → pakkaus ei aktivoidu

## Lohkoprotokolla

Kun pakkaus on aktiivinen (XZ:1), tiedostodata ja hakemistolistaus
lähetetään lohkoissa normaalin tavuvirran sijaan.

### Lohkorakenne

```
┌──────────────────────────────────────────────┐
│ Header (4 tavua)                             │
│   [2B] compressed_size (N), little-endian    │
│   [2B] raw_size (M), little-endian           │
├──────────────────────────────────────────────┤
│ Payload (N tavua)                            │
│   LZ4-pakattu data                           │
└──────────────────────────────────────────────┘
```

### EOF-merkki

```
┌──────────────────────────────────────────────┐
│ [2B] 0x0000 = ei enää lohkoja                │
└──────────────────────────────────────────────┘
```

### Arvot

| Kenttä | Koko | Arvoalue | Kuvaus |
|--------|------|----------|--------|
| compressed_size | 2B LE | 0–512 | 0 = EOF, 1–512 = pakattu koko |
| raw_size | 2B LE | 1–128 | Puretun datan koko (COMPRESS_BLOCK_SIZE) |
| payload | N tavua | — | LZ4 block -formaatti |

### Esimerkkisiirto

```
Nano lähettää (40 KB tiedosto):
  [0x82, 0x00, 0x00, 0x01, <130 tavua LZ4>]    ← lohko 1: 130B pakattu → 256B
  [0x90, 0x00, 0x00, 0x01, <144 tavua LZ4>]    ← lohko 2: 144B pakattu → 256B
  ...
  [0x50, 0x00, 0x80, 0x00, <80 tavua LZ4>]     ← viimeinen: 80B → 128B
  [0x00, 0x00]                                   ← EOF

C64 vastaanottaa:
  loop:
    lue 2 tavua → compressed_size
    jos 0 → EOF, lopeta
    lue 2 tavua → raw_size
    lue compressed_size tavua → puskuri
    pura LZ4 → raw_size tavua kohdeosoitteeseen
    kohdeosoite += raw_size
```

## LZ4 block -formaatti

Pakattu data lohkon sisällä noudattaa standardia LZ4 block -formaattia
(ei LZ4 frame headeria).

### Sequence-rakenne

```
[token] [literals*] [offset] [match-extension*]

token:      1 tavu
            ylin 4 bittiä = literal-pituus (L)
            alin 4 bittiä = match-pituus - 4 (M)

literals:   L tavua raakadataa
            jos L == 15 → lue lisätavuja kunnes < 255

offset:     2 tavua little-endian, match-etäisyys taaksepäin
            (puuttuu jos viimeinen sequence ilman matchia)

match-ext:  jos M == 15 → lue lisätavuja kunnes < 255
            kokonaispituus = M + 4 + summa(lisätavut)
```

### Esimerkki

```
Raakadata:  "HELLO HELLO WORLD"

Pakattu:
  [0x50]           token: 5 literaalia, match 0+4=4
  "HELLO"          5 literaalia
  [0x06, 0x00]     offset 6 taaksepäin (= "HELLO ")
  ...
```

Tämä on standardi LZ4, joten mikä tahansa LZ4-purkaja toimii.

## Yhteensopivuus

### Tuetut konfiguraatiot

| C64 | Asema | Siirto | Pakkaus | Efektiivinen nopeus |
|-----|-------|--------|---------|---------------------|
| Normaali KERNAL | 64korppu | Std IEC | Ei | ~0.4 KB/s |
| Normaali KERNAL | 1541 | Std IEC | Ei | ~0.4 KB/s |
| JiffyDOS ROM | 64korppu | 2-bit | Ei | ~5 KB/s |
| JiffyDOS ROM | JiffyDOS 1541 | 2-bit | Ei | ~5 KB/s |
| EPYX cartridge | 64korppu | EPYX | Ei | ~3 KB/s |
| **karskiROM** | **64korppu** | **2-bit + LZ4** | **Kyllä** | **~9 KB/s** |
| karskiROM | Normaali 1541 | Std IEC | Ei | ~0.4 KB/s |
| karskiROM | JiffyDOS 1541 | 2-bit | Ei | ~5 KB/s |

### Turvallisuus

- Nano ei koskaan pakkaa ilman `XZ:1`-komentoa
- Standardi IEC ja JiffyDOS ROM eivät lähetä XZ-komentoja
- EPYX ei käytä komentokanavaltta → ei ristiriitaa
- Tuntematon XZ-komento palauttaa virheen `30, SYNTAX ERROR,00,00`

## Pakkauksen suorituskyky (arvioitu)

| Datatyyppi | Pakkaussuhde | JiffyDOS + LZ4 |
|------------|-------------|-----------------|
| BASIC-ohjelma | ~2.0:1 | ~10 KB/s |
| Konetason peli | ~1.5:1 | ~7.5 KB/s |
| Sprite/grafiikka | ~2.5:1 | ~12.5 KB/s |
| Hakemistolistaus | ~3.0:1 | ~15 KB/s |
| Jo pakattu data | ~1.05:1 | ~5.2 KB/s |
| **Keskimäärin** | **~1.8:1** | **~9 KB/s** |
