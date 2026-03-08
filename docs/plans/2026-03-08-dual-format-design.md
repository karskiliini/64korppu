# Dual-Format Support: HD (FAT12) + DD (1581 CBMFS)

Suunnitteludokumentti: 64korppu-aseman kaksoistiedostojärjestelmätuki.

## Yhteenveto

Asema tukee kahta levyformaattia automaattisella tunnistuksella:

| Levy | Formaatti | Kapasiteetti | Sektorit | Käyttö |
|------|-----------|-------------|----------|--------|
| HD (1.44MB) | FAT12 | 1440 KB | 80×2×18, 512B | PC-yhteensopivuus, .D64/.D81-imaget |
| DD (720/800KB) | 1581 CBMFS | 800 KB (3160 blockia) | 80×2×10, 512B | Natiivi Commodore-yhteensopivuus |

## Formaatin tunnistus

### Fyysinen taso: HD/DD-reikä

3.5" levyissä on HD-tunnistusreikä (vastapäätä kirjoitussuojakytkintä). Asemassa
on sensori, joka luetaan GPIO-pinnistä:

- **Pin LOW (0):** HD-levy → datanopeus 500 kbps, 18 sektoria/raita
- **Pin HIGH (1):** DD-levy → datanopeus 250 kbps, 10 sektoria/raita

Tunnistus tapahtuu automaattisesti levyn insertoinnissa eikä vaadi käyttäjän
toimenpiteitä.

### Tiedostojärjestelmän tunnistus (DD-levy)

DD-levyllä voi olla joko 1581 CBMFS tai FAT12-720K. Tunnistus:

1. Lue raita 40, sektori 0 (1581:n hakemistosektori)
2. Tarkista "3D" magic-tavut (disk ID)
3. Jos löytyy → CBMFS 1581
4. Jos ei → lue sektori 0 (boot sector), tarkista FAT12 BPB
5. Jos FAT12 → FAT12-720K
6. Ei kumpikaan → formatoimaton levy

### Arkkitehtuurikaavio

```
┌─────────────────────────────────────────────────────────┐
│                    FLOPPY DRIVE                         │
│  ┌──────────┐   ┌──────────────┐   ┌────────────────┐  │
│  │ HD/DD    │   │ /DENSITY pin │   │  MFM data      │  │
│  │ sensori  │   │ (datanopeus) │   │  (luku/kirj)   │  │
│  └────┬─────┘   └──────▲───────┘   └───────▲────────┘  │
└───────┼────────────────┼───────────────────┼────────────┘
        │ GPIO           │ GPIO              │ PIO/DMA
┌───────┼────────────────┼───────────────────┼────────────┐
│       ▼                │                   │            │
│  ┌─────────────────────┴───────────────────┴─────────┐  │
│  │              floppy_ctrl                           │  │
│  │  • lukee HD/DD-pinnin                             │  │
│  │  • asettaa /DENSITY (HD=500kbps, DD=250kbps)      │  │
│  │  • valitsee geometrian runtime-structista          │  │
│  └──────────────────────┬────────────────────────────┘  │
│                         │ disk_geometry_t*               │
│                         ▼                                │
│  ┌───────────────────────────────────────────────────┐  │
│  │              format_detect                        │  │
│  │  • HD → lukee boot-sektori → FAT12 BPB           │  │
│  │  • DD → lukee raita 40, sektori 0 → CBMFS?       │  │
│  │         lukee sektori 0 → FAT12-720K?             │  │
│  └──────────────────────┬────────────────────────────┘  │
│                         │ disk_format_t                  │
│                         ▼                                │
│  ┌───────────────────────────────────────────────────┐  │
│  │              cbm_dos                              │  │
│  │  • ohjaa oikealle FS-moduulille                   │  │
│  │  • raportoi formaatin hakemiston otsikossa:       │  │
│  │    FAT12-HD: "64KORPPU" + "FAT"                   │  │
│  │    CBMFS:    "DISKNAME" + "3D"                    │  │
│  │    FAT12-DD: "64KORPPU" + "72K"                   │  │
│  └──────────────────────┬────────────────────────────┘  │
│                         │ IEC-väylä (ATN/CLK/DATA)      │
└─────────────────────────┼────────────────────────────────┘
                          ▼
                   ┌──────────────┐
                   │ Commodore 64 │
                   │ LOAD "$",8   │
                   │ → formaatti  │
                   │ näkyy otsik. │
                   └──────────────┘
```

### Sekvenssi: DD-levyn tunnistus

```
  C64          cbm_dos       format_detect     floppy_ctrl       Drive
   │              │               │               │               │
   │              │               │               │  levy sisään  │
   │              │               │               │◄──────────────│
   │              │               │               │ lue HD/DD pin │
   │              │               │               │──────────────►│
   │              │               │               │ pin=1 (DD)    │
   │              │               │               │◄──────────────│
   │              │               │               │ /DENSITY=HIGH │
   │              │               │               │ geom=80×10    │
   │              │               │◄──────────────│               │
   │              │               │               │               │
   │              │               │ lue T40/S0    │               │
   │              │               │──────────────►│  seek+read    │
   │              │               │               │──────────────►│
   │              │               │ sektoridata   │               │
   │              │               │◄──────────────│◄──────────────│
   │              │               │ "3D" magic?   │               │
   │              │               │ → KYLLÄ       │               │
   │              │ format=CBMFS  │               │               │
   │              │◄──────────────│               │               │
   │ LOAD "$",8  │               │               │               │
   │─────────────►│ hakemisto     │               │               │
   │              │ 1581-tyyliin  │               │               │
   │◄─────────────│               │               │               │
   │ 0 "DISK" 3D │               │               │               │
   │ 3159 FREE.  │               │               │               │
```

### Sekvenssi: N-komento (formatointi)

```
  C64                     cbm_dos                  floppy_ctrl
   │                         │                         │
   │ "N0:MYDISK,ID"          │                         │
   │────────────────────────►│ kysy HD/DD              │
   │                         │────────────────────────►│
   │                         │ DD                      │
   │                         │◄────────────────────────│
   │                         │                         │
   │                         │ formatoi 1581 CBMFS     │
   │                         │ (80 raitaa, BAM, hak.)  │
   │                         │────────────────────────►│
   │                         │ ...kirjoitus...         │
   │                         │◄────────────────────────│
   │ 00, OK,00,00            │                         │
   │◄────────────────────────│                         │
```

## Modulaarinen arkkitehtuuri

### Geometria-abstractio

Nykyiset kovakoodatut vakiot korvataan runtime-structilla:

```c
typedef struct {
    uint8_t  tracks;          // 80
    uint8_t  sides;           // 2
    uint8_t  sectors_per_track; // 18 (HD) tai 10 (DD)
    uint16_t sector_size;     // 512
    uint16_t total_sectors;   // laskettu
    uint16_t data_rate_kbps;  // 500 (HD) tai 250 (DD)
} disk_geometry_t;

static const disk_geometry_t GEOM_HD = { 80, 2, 18, 512, 2880, 500 };
static const disk_geometry_t GEOM_DD = { 80, 2, 10, 512, 1600, 250 };
```

### Tiedostojärjestelmä-abstractio

```c
typedef enum {
    DISK_FORMAT_NONE,
    DISK_FORMAT_FAT12_HD,    // 1.44MB FAT12
    DISK_FORMAT_FAT12_DD,    // 720KB FAT12
    DISK_FORMAT_CBMFS_1581,  // 800KB 1581 CBMFS
} disk_format_t;
```

CBM-DOS käyttää formaattitietoa ohjaukseen:

- `DISK_FORMAT_FAT12_*` → `fat12.c` -moduuli
- `DISK_FORMAT_CBMFS_1581` → `cbmfs.c` -moduuli (uusi)

### D81-tuen valmistelu

`cbmfs.c` suunnitellaan siten, että sama koodi palvelee:

1. **Natiivi DD-levy:** sektorit luetaan suoraan floppy_ctrl:n kautta
2. **D81-image (tuleva):** sektorit luetaan FAT12-tiedostosta

Tämä saavutetaan I/O-abstraktiolla:

```c
typedef struct {
    int (*read_sector)(uint8_t track, uint8_t sector, uint8_t *buf);
    int (*write_sector)(uint8_t track, uint8_t sector, const uint8_t *buf);
} cbmfs_io_t;

// Natiivi DD-levy: I/O menee floppy_ctrl:lle
// D81-image: I/O menee fat12-tiedoston offsetteihin
```

## Device Number -konfigurointi

### Jumpperi-asetus (vaihe 1)

Kaksi jumpperia GPIO-pinneissä, sisäiset pull-upit:

| J1 | J2 | Laite # |
|----|-----|---------|
| auki | auki | 8 |
| kiinni | auki | 9 |
| auki | kiinni | 10 |
| kiinni | kiinni | 11 |

Luetaan bootissa, ei voi muuttaa ajon aikana.

### EEPROM-override (jatkokehitys)

EEPROM:n ensimmäinen tavu:

| Bitti 7 | Bitit 0-4 | Merkitys |
|---------|-----------|----------|
| 0 | - | Käytä jumpperi-asetusta |
| 1 | laite # (8-30) | Ohita jumpperit, käytä EEPROM-arvoa |

Asetetaan C64:ltä komennoilla:

- `U0>9` — aseta laite numeroksi 9, tallenna EEPROM:iin
- `U0>J` — palaa jumpperi-tilaan (nollaa override-bitti)

Tämä mahdollistaa joustavan konfiguroinnin ilman fyysistä muutosta,
mutta jumpperi toimii aina fallbackina.

**Pico-huomio:** RP2040:ssä ei ole EEPROM:ia, joten arvo tallennetaan
Flash-muistin viimeiselle sektorille. Arduino Nanossa käytetään
natiivia EEPROM-kirjastoa.

## Rajaus

### Vaihe 1 (tämä suunnitelma)

- [x] Automaattinen HD/DD-tunnistus GPIO-pinnistä
- [x] `disk_geometry_t` runtime-abstraktio
- [x] `disk_format_t` ja format_detect-moduuli
- [x] 1581 CBMFS natiivituki DD-levyille (`cbmfs.c`)
- [x] FAT12-720K lukutuki DD-levyille
- [x] Device number jumpperointi (2 jumpperia → 8-11)
- [x] `cbmfs_io_t` -abstraktio D81-tuen valmistelua varten

### Jatkokehitys

- [ ] **D81-imagetuki:** .D81-tiedoston mounttaus FAT12-levyltä `CD:GAME.D81`
- [ ] **1581-alihakemistot:** Partitiotuki CBM-DOS:iin
- [ ] **EEPROM device number:** `U0>N` -komento, override-bitti
- [ ] **D81 ↔ 1581 -kopiointi:** Natiivi-DD-levyn ja D81-imagen välillä
- [ ] **Disk change -tunnistus:** Levynvaihdon automaattinen havaitseminen

## Vaikutus olemassa olevaan koodiin

| Tiedosto | Muutos |
|----------|--------|
| `floppy_ctrl.h` | Vakiot → `disk_geometry_t` struct |
| `floppy_ctrl.c` | HD/DD-pinnin luku, geometrian valinta |
| `mfm_codec.c/h` | Parametrisoi sektorimäärä ja gap-pituudet |
| `fat12.h` | Tuki 720KB BPB-parametreille |
| `fat12.c` | 720KB mount, parametrisoi sektorimäärät |
| `cbm_dos.c` | Format-detect kutsut, N-komennon haaroitus, FS-ohjaus |
| `main.c` | Jumpperi-GPIO:t, device number -luku bootissa |
| **`cbmfs.c` (uusi)** | 1581 CBMFS tiedostojärjestelmä |
| **`cbmfs.h` (uusi)** | CBMFS header |
| **`format_detect.c` (uusi)** | Formaatin tunnistuslogiikka |
| **`format_detect.h` (uusi)** | Format detect header |
