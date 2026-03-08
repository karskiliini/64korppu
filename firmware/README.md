# Firmware

Kullekin laitteistovaihtoehdolle on oma hakemistonsa.
Vaihtoehdot vastaavat `docs/`-kansion suunnitelmia.

```
firmware/
├── A-IEC-Pico/          Raspberry Pi Pico, IEC-väylä (suositeltu)
│   ├── src/             C-lähdekoodi
│   ├── include/         Otsikkotiedostot
│   ├── pio/             PIO-ohjelmat (iec_bus, mfm_read, mfm_write)
│   ├── CMakeLists.txt   Pico SDK build
│   └── pico_sdk_import.cmake
│
├── B-Expansion-FDC/     Expansion port + FDC-piiri (ei vielä toteutettu)
├── C-Userport-Pico/     User port + Pico (ei vielä toteutettu)
├── D-IEC-Nano/          Arduino Nano, IEC-väylä (ei vielä toteutettu)
└── E-IEC-Nano-SRAM/     Arduino Nano + SPI SRAM (ei vielä toteutettu)
```

## A-IEC-Pico

Ainoa toistaiseksi toteutettu vaihtoehto. Raspberry Pi Pico emuloi
CBM-levyasemaa IEC-sarjaväylän yli ja ohjaa PC 3.5" HD -korppuasemaa.

Tuetut levyformaatit:
- **HD 1.44 MB** — FAT12-tiedostojärjestelmä
- **DD 720 KB** — CBM 1581 -yhteensopiva CBMFS-tiedostojärjestelmä
- **D64-levykuvat** — 1541-yhteensopiva (luetaan FAT12-levyltä)

Kääntäminen:

```bash
cd firmware/A-IEC-Pico
mkdir build && cd build
cmake ..
make
```

## B–E: Muut vaihtoehdot

Suunnitteludokumentit löytyvät `docs/`-kansiosta. Firmwarea ei ole
vielä toteutettu näille vaihtoehdoille. Jokainen vaihtoehto tulee
noudattamaan samaa logiikkakerrosrakennetta (CBM-DOS, D64, FAT12/CBMFS),
mutta laitteistotason koodi (IEC-protokolla, floppy-ohjaus, MFM) on
alustaspesifistä.
