# Diagnostiikkapadit (Test Points) — 4-layer PCB

14 SMD-testipistettä PCB:n taustapuolella (B.Cu), vasemmassa reunassa pystyrivinä.
Kaikki padit ovat 1.5mm pyöreitä kuparipadeja ilman juotepastaa — mittapää koskettaa suoraan kupariin.

## Padien sijainnit ja signaalit

```
    PCB:n vasen reuna (x = 103mm)
    ────────────────────────────────

    TP1  ● GND              y = 108.0mm
    TP2  ● +5V              y = 110.5mm
         ─── jännitteet ───
    TP3  ● IEC_ATN           y = 113.0mm
    TP4  ● IEC_CLK           y = 115.5mm
    TP5  ● IEC_DATA          y = 118.0mm
         ─── IEC-väylä ───
    TP6  ● FLOPPY_MOTOR      y = 120.5mm
    TP7  ● FLOPPY_STEP       y = 123.0mm
    TP8  ● FLOPPY_RDATA      y = 125.5mm
    TP9  ● FLOPPY_WDATA      y = 128.0mm
    TP10 ● FLOPPY_SIDE1      y = 130.5mm
         ─── floppy-väylä ───
    TP11 ● SPI_SCK            y = 133.0mm
    TP12 ● SPI_MOSI           y = 135.5mm
    TP13 ● SPI_MISO           y = 138.0mm
    TP14 ● SPI_CS_SRAM        y = 140.5mm
         ─── SPI-väylä ───
```

## Signaalien selitykset

### Jännitteet (TP1-TP2)

| Padi | Signaali | Odotettu taso | Merkitys |
|------|----------|---------------|----------|
| TP1 | GND | 0V | Maareferenssi. Kiinnitä yleismittarin musta johto tähän. |
| TP2 | +5V | 4.8–5.2V | Käyttöjännite. Jos puuttuu, mikään ei toimi. Tarkista virtalähde ja barrel jack. |

### IEC-väylä / C64 (TP3-TP5)

Nämä signaalit kulkevat C64:n ja Arduinon välillä 1kΩ suojavastusten läpi.
Kaikki ovat open collector: lepotilassa HIGH (4.7kΩ pull-up → +5V), aktiivisena LOW.

| Padi | Signaali | Lepotila | Aktiivinen | Merkitys |
|------|----------|----------|------------|----------|
| TP3 | IEC_ATN | HIGH (~5V) | LOW (0V) | **Attention** — C64 lähettää komennon. Pulssittaa LOW kun C64 kutsuu laitetta. Jos pysyy LOW, C64 on jumissa. Jos pysyy HIGH, C64 ei kommunikoi. |
| TP4 | IEC_CLK | HIGH (~5V) | LOW (0V) | **Clock** — kellosignaali. Datansiirron aikana vaihtelee HIGH/LOW. Bittiväli ~60-80µs. |
| TP5 | IEC_DATA | HIGH (~5V) | LOW (0V) | **Data** — databitit. Talker vetää LOW lähettäessään 0-bitin. |

**Pikadiagnostiikka:**
- Kaikki HIGH → C64 ei ole käynnissä tai IEC-kaapeli irti
- ATN pulssittaa, CLK/DATA eivät → Arduino ei vastaa C64:lle
- Kaikki LOW → oikosulku tai komponentti viallinen

### Floppy-signaalit (TP6-TP10)

Nämä ohjataan 74HC595 shift registerin (TP6-TP7, TP10) tai suoran GPIO:n (TP8-TP9) kautta.
Kaikki floppy-signaalit ovat aktiivisia LOW-tilassa (negatiivinen logiikka).

| Padi | Signaali | Lepotila | Aktiivinen | Merkitys |
|------|----------|----------|------------|----------|
| TP6 | FLOPPY_MOTOR | HIGH (~5V) | LOW (0V) | **Moottorin käynnistys.** LOW = moottori pyörii. Tarkista: aseman moottori pyörii kun LOW. Jos HIGH eikä moottori pyöri, signaali ei kulje. |
| TP7 | FLOPPY_STEP | HIGH (~5V) | pulssi LOW | **Askelpulssi.** Jokainen LOW-pulssi siirtää lukupäätä yhden raidan. Seuraavien aikana pitäisi näkyä 3ms pulsseja oskilloskoopilla. |
| TP8 | FLOPPY_RDATA | HIGH (~5V) | pulssijuna | **Luettu data (MFM).** Nopea pulssijuna (~4-8µs väli) kun asema lukee levyä. Vaatii oskilloskoopin. Jos pysyy HIGH levyn pyöriessä, lukupää tai asema on viallinen. |
| TP9 | FLOPPY_WDATA | HIGH (~5V) | pulssijuna | **Kirjoitettava data (MFM).** Nopea pulssijuna kirjoituksen aikana. Vaatii oskilloskoopin. |
| TP10 | FLOPPY_SIDE1 | HIGH (~5V) | LOW (0V) | **Levyn puoli.** HIGH = puoli 0 (yläpuoli), LOW = puoli 1 (alapuoli). DD-levyillä vain puoli 0. |

**Pikadiagnostiikka:**
- MOTOR LOW mutta asema ei pyöri → tarkista floppy-virta (4-pin Berg) ja IDC-kaapeli
- RDATA pysyy HIGH moottorin pyöriessä → lukupää ei lue, levy viallinen, tai asema rikki
- STEP ei pulssita seek-komennolla → 74HC595 tai SPI-väylä viallinen

### SPI-väylä (TP11-TP14)

SPI-väylä on jaettu 23LC256 SRAM:n ja 74HC595 shift registerin kesken.
SCK ja MOSI menevät molemmille, MISO tulee vain SRAM:lta. CS_SRAM valitsee SRAM:n.

| Padi | Signaali | Lepotila | Aktiivinen | Merkitys |
|------|----------|----------|------------|----------|
| TP11 | SPI_SCK | LOW (0V) | kellopulssijuna | **SPI Clock.** 8 MHz kellosignaali SPI-siirron aikana (Mode 0, CPOL=0). Oskilloskoopilla näkyy siistit 125ns pulssit. |
| TP12 | SPI_MOSI | vaihtelee | datapulssit | **Master Out, Slave In.** Arduino → SRAM/595 data. Vaihtuu SCK:n nousevalla reunalla. |
| TP13 | SPI_MISO | HIGH (Z) | datapulssit | **Master In, Slave Out.** SRAM → Arduino data. Aktiivinen vain kun CS_SRAM = LOW. Jos pysyy HIGH tai LOW CS:n ollessa LOW ja SCK pulssittaessa → SRAM viallinen tai kytös rikki. |
| TP14 | SPI_CS_SRAM | HIGH (~5V) | LOW (0V) | **SRAM Chip Select.** LOW = SRAM valittu. HIGH = SRAM pois, 595 voidaan ohjata. Jos pysyy LOW → SPI-väylä jumissa. |

**Pikadiagnostiikka:**
- SCK ei pulssita → SPI ei alustautunut, tarkista Arduino-firmware
- CS_SRAM pysyy LOW → koodi ei vapauta SRAM:a, 595 ei saa komentoja
- MISO pysyy HIGH CS:n ollessa LOW → SRAM-piiri viallinen tai kylmäjuotos

## Mittausohje

### Yleismittarilla (DC-jännite)

1. Kytke musta johto **TP1 (GND)**
2. Mittaa punainen johto muihin padeihin
3. Tarkista:
   - TP2 (+5V): pitää olla 4.8–5.2V
   - Lepotilassa IEC-signaalit (TP3-TP5): ~5V (pull-up)
   - Lepotilassa floppy-signaalit (TP6-TP10): ~5V (inaktiivinen)
   - TP14 (CS_SRAM): ~5V (SRAM ei valittuna)

### Oskilloskoopilla

1. GND-clip → **TP1 (GND)**
2. Probe → haluttu signaali
3. Hyödylliset asetukset:

| Signaali | Aikaskaalaus | Jänniteskaalaus | Mitä etsiä |
|----------|-------------|-----------------|------------|
| IEC_ATN | 1ms/div | 2V/div | ~80µs LOW-pulssit komennon alussa |
| IEC_CLK | 50µs/div | 2V/div | 60-80µs bittikello datan aikana |
| FLOPPY_STEP | 5ms/div | 2V/div | 3ms LOW-pulssit seekin aikana |
| FLOPPY_RDATA | 2µs/div | 2V/div | MFM-pulssijuna 4-8µs välein |
| SPI_SCK | 100ns/div | 2V/div | 8 MHz neliöaalto (125ns jakso) |

## Vianetsintätaulukko

| Oire | Tarkista | Todennäköinen syy |
|------|----------|-------------------|
| +5V puuttuu | TP2 | Virtalähde, barrel jack, sulake |
| C64 ei löydä laitetta | TP3 (ATN) | IEC-kaapeli, suojavastukset R1-R4 |
| ATN pulssittaa, ei vastausta | TP4, TP5 | Arduino-firmware, pull-up vastukset R5-R7 |
| Floppy ei pyöri | TP6 (MOTOR) | IDC-kaapeli, floppy-virta, 74HC595 |
| Ei lue levyä | TP8 (RDATA) | Asema viallinen, levy viallinen, lukupää |
| SPI ei toimi | TP11 (SCK) | Arduino SPI-alustus, piirin kytös |
| SRAM-virhe | TP13, TP14 | 23LC256 viallinen, kylmäjuotos |
