# Mikrokontrollerit ja kehitysalustat — vertailu

Tämä dokumentti listaa kaikki Arduino-laudat ja vastaavat kehitysalustat,
jotka voisivat soveltua 64korppu-projektiin.

## Projektin vaatimukset

```
IEC-väylä:       4 GPIO  (ATN interrupt, CLK, DATA, RESET)
Floppy output:   9 GPIO  (DENSITY, MOTEA, DRVSEL, MOTOR, DIR, STEP, WDATA, WGATE, SIDE1)
Floppy input:    4 GPIO  (TRK00, WPT, RDATA, DSKCHG)
─────────────────────────
Yhteensä:       17 GPIO  (minimi ilman SRAM:ia)
+ SPI SRAM:     +4 GPIO  (MOSI, MISO, SCK, CS)  → 21 GPIO

Lisävaatimukset:
- Timer Input Capture tai vastaava MFM-dekoodaukseen (500 kbps, 2 µs/bitti)
- Ulkoinen keskeytys ATN-signaalille
- MFM-kirjoitus: tarkka ajoitus (~2 µs resoluutio)
- 5V-logiikka suositeltava (IEC + floppy ovat 5V) — 3.3V vaatii tasonmuuntimia
```

---

## Arduino-laudat

### 5V-laudat (ei tarvitse tasonmuuntimia)

| Lauta | Prosessori | MHz | RAM | Flash | GPIO | Hinta | Soveltuvuus |
|---|---|---|---|---|---|---|---|
| **Arduino Uno** | ATmega328P | 16 | 2 KB | 32 KB | 20 | ~5€ (klooni) | Toimii, RAM tiukka |
| **Arduino Nano** | ATmega328P | 16 | 2 KB | 32 KB | 22 | ~3-5€ (klooni) | Toimii, kompakti |
| **Arduino Pro Mini 5V** | ATmega328P | 16 | 2 KB | 32 KB | 20 | ~2-3€ (klooni) | Pienin, ei USB:ta |
| **Arduino Leonardo** | ATmega32U4 | 16 | 2.5 KB | 32 KB | 20 | ~5-8€ (klooni) | +500B RAM, natiivi USB |
| **Arduino Micro** | ATmega32U4 | 16 | 2.5 KB | 32 KB | 20 | ~5-8€ (klooni) | Sama kuin Leonardo, pienempi |
| **Arduino Mega 2560** | ATmega2560 | 16 | **8 KB** | 256 KB | **54** | ~8-12€ (klooni) | Paljon RAM:ia ja GPIO:ta |
| **Arduino Nano Every** | ATmega4809 | 20 | **6 KB** | 48 KB | 20 | ~8-10€ | Parempi Nano, CCL-logiikka |

### 3.3V-laudat (tarvitsee tasonmuuntimet IEC:lle ja flopylle)

| Lauta | Prosessori | MHz | RAM | Flash | GPIO | Hinta | Soveltuvuus |
|---|---|---|---|---|---|---|---|
| **Arduino Due** | SAM3X8E (ARM) | 84 | **96 KB** | 512 KB | 54 | ~12-15€ | Tehokas, paljon muistia |
| **Arduino Zero** | SAMD21 (ARM) | 48 | **32 KB** | 256 KB | 20 | ~15-20€ | ARM Cortex-M0+ |
| **Arduino Nano 33 IoT** | SAMD21 + WiFi | 48 | **32 KB** | 256 KB | 14 | ~18€ | WiFi turha, vähän GPIO:ta |
| **Arduino Nano 33 BLE** | nRF52840 | 64 | **256 KB** | 1 MB | 14 | ~20€ | BLE turha, vähän GPIO:ta |
| **Arduino Nano RP2040** | RP2040 | 133 | **264 KB** | 16 MB | 20 | ~20€ | Pico Nano-muodossa |
| **Arduino MKR Zero** | SAMD21 | 48 | **32 KB** | 256 KB | 22 | ~25€ | SD-korttipaikka mukana |

---

## Arduino-yhteensopivat alustat

### 5V-alustat

| Lauta | Prosessori | MHz | RAM | Flash | GPIO | Hinta | Soveltuvuus |
|---|---|---|---|---|---|---|---|
| **STM32 Blue Pill** | STM32F103C8 | 72 | **20 KB** | 64 KB | 33 | ~2-3€ | 5V-tolerantti input! |
| **Teensy 2.0** | ATmega32U4 | 16 | 2.5 KB | 32 KB | 25 | ~20€ | Hyvä USB, kuten Leonardo |

### 3.3V-alustat

| Lauta | Prosessori | MHz | RAM | Flash | GPIO | Hinta | Soveltuvuus |
|---|---|---|---|---|---|---|---|
| **Raspberry Pi Pico** | RP2040 (2 ydintä) | 133 | **264 KB** | 2 MB | 26 | ~4-5€ | **Projektin päävalinta** |
| **Raspberry Pi Pico W** | RP2040 + WiFi | 133 | **264 KB** | 2 MB | 26 | ~7€ | WiFi turha |
| **Raspberry Pi Pico 2** | RP2350 (2 ydintä) | 150 | **520 KB** | 4 MB | 26 | ~5-6€ | Picon seuraaja, PIO v2 |
| **STM32 Black Pill** | STM32F411CE | 100 | **128 KB** | 512 KB | 32 | ~3-5€ | Tehokas, edullinen |
| **Teensy 4.0** | i.MX RT1062 | 600 | **1 MB** | 2 MB | 24 | ~25€ | Ylilyönti, kallis |
| **Teensy 4.1** | i.MX RT1062 | 600 | **1 MB** | 8 MB | 42 | ~35€ | Ylilyönti, kallis |
| **ESP32-DevKit** | ESP32 (2 ydintä) | 240 | **520 KB** | 4 MB | 25 | ~4-5€ | WiFi/BT turha, ajoitus? |
| **ESP32-S3** | ESP32-S3 (2 ydintä) | 240 | **512 KB** | 8 MB | 36 | ~5-7€ | USB OTG, paljon GPIO |
| **Seeed XIAO SAMD21** | SAMD21 | 48 | **32 KB** | 256 KB | 11 | ~5€ | Liian vähän GPIO:ta |
| **Seeed XIAO RP2040** | RP2040 | 133 | **264 KB** | 2 MB | 11 | ~5€ | Liian vähän GPIO:ta |
| **Adafruit Feather M4** | SAMD51 | 120 | **192 KB** | 512 KB | 21 | ~25€ | Kallis, hyvä suorituskyky |
| **WeAct STM32F411** | STM32F411CE | 100 | **128 KB** | 512 KB | 32 | ~3€ | Black Pill klooni |

---

## GPIO-riittävyys

Minimi 17 GPIO (ilman SRAM:ia), 21 GPIO SRAM:n kanssa.

```
                               GPIO    Riittääkö?
                               ────    ──────────
5V-laudat:
  Arduino Uno / Nano / Pro Mini  20-22   Kyllä (17/22), SRAM:lla tiukka (21/22)
  Arduino Leonardo / Micro       20      Kyllä (17/20), SRAM:lla tiukka (21 > 20!)
  Arduino Mega 2560              54      Reilusti
  Arduino Nano Every             20      Kyllä, SRAM:lla tiukka
  STM32 Blue Pill                33      Reilusti
  Teensy 2.0                     25      Reilusti

3.3V-laudat:
  Raspberry Pi Pico / Pico 2     26      Reilusti
  Arduino Due                    54      Reilusti
  Arduino Nano RP2040            20      Kyllä, SRAM:lla tiukka
  STM32 Black Pill               32      Reilusti
  ESP32-DevKit                   25      Reilusti (mutta osa pinneistä rajoitettuja)
  Teensy 4.0                     24      Kyllä
  Teensy 4.1                     42      Reilusti
  Arduino Nano 33 IoT/BLE        14      EI RIITÄ
  Seeed XIAO (kaikki)            11      EI RIITÄ
```

### Leonardo/Micro + SRAM -ongelma

ATmega32U4-lautojen 20 GPIO:sta D0/D1 ovat vapaita (natiivi USB ei käytä niitä),
joten 21 GPIO on juuri mahdollista kun kaikki pinnit käytetään. Ei yhtään
vapaata debug-pinnistä.

---

## RAM-riittävyys (ilman ulkoista SRAM:ia)

```
MFM-raita:    ~12 500 B (yksi raita 500 kbps × 200 ms)
FAT-taulu:    ~4 500 B (1.44 MB levyn FAT12)
Sektori:         512 B
IEC-puskuri:     256 B
Pino + muut:     512 B
─────────────────────────
Yhteensä:    ~18 280 B  (jos kaikki mahtuu kerralla)
Minimi:       ~1 536 B  (sektori + IEC + pino, ei raitapuskuria)

                     RAM      Raitapuskuri?   FAT kokonaan?
                     ───      ─────────────   ─────────────
ATmega328P           2 KB     Ei              Ei
ATmega32U4           2.5 KB   Ei              Ei
ATmega4809           6 KB     Ei              Kyllä (4.5 KB)
ATmega2560           8 KB     Ei              Kyllä
STM32F103 (Blue)     20 KB    Kyllä           Kyllä
SAMD21               32 KB    Kyllä           Kyllä
RP2040 (Pico)        264 KB   Kyllä           Kyllä
STM32F411 (Black)    128 KB   Kyllä           Kyllä
ESP32                520 KB   Kyllä           Kyllä
SAM3X8E (Due)        96 KB    Kyllä           Kyllä
i.MX RT1062 (Teensy) 1 MB     Kyllä           Kyllä
```

---

## MFM-ajoituksen soveltuvuus

MFM-dekoodaus vaatii 2 µs tarkkuutta (500 kbps HD floppy).
CPU-syklejä per MFM-bitti = kellotaajuus × 2 µs:

```
                     MHz    Syklejä/bitti   Riittääkö?
                     ───    ─────────────   ──────────
ATmega328P           16          32         Tiukka mutta toimii (Timer1 ICP)
ATmega32U4           16          32         Tiukka mutta toimii (Timer1 ICP)
ATmega4809           20          40         Hieman parempi
ATmega2560           16          32         Tiukka mutta toimii
STM32F103            72         144         Hyvä
SAMD21               48          96         Hyvä
RP2040              133         266         Erinomainen (+ PIO!)
RP2350              150         300         Erinomainen (+ PIO v2!)
STM32F411           100         200         Hyvä
ESP32               240         480         Erinomainen (mutta RTOS-jitter!)
SAM3X8E              84         168         Hyvä
i.MX RT1062         600        1200         Ylivoimainen
```

**ESP32-varoitus:** ESP32 pyörittää FreeRTOS-käyttöjärjestelmää, joka aiheuttaa
satunnaista viivettä (jitter) keskeytyksiin. Tämä voi häiritä MFM-dekoodausta,
vaikka CPU-teho sinänsä riittää. Ratkaisu: käytä toista ydintä ilman RTOS:ia
tai RMT-periferiaa (Remote Control Transceiver) pulssinmittaukseen.

---

## Tasonmuunnin-tarve

```
5V-natiivi (ei tasonmuuntimia):
  ✓ Kaikki ATmega-pohjaiset Arduinot (Uno, Nano, Mega, Leonardo...)
  ✓ Teensy 2.0
  △ STM32 Blue Pill (5V-tolerantti INPUT, mutta output on 3.3V)

3.3V (tarvitsee tasonmuuntimia IEC + floppy):
  ✗ Raspberry Pi Pico / Pico 2
  ✗ Arduino Due, Zero, MKR, Nano 33 -sarja
  ✗ STM32 Black Pill
  ✗ ESP32
  ✗ Teensy 4.x
  ✗ Kaikki SAMD-pohjaiset

Tasonmuuntimen hinta: ~1-3€ (2× 74LVC245 tai 4× BSS138 MOSFET)
```

**STM32 Blue Pill -erikoistapaus:** STM32F103:n GPIO-pinnit ovat 5V-tolerantteja
*input*-tilassa, mutta output on 3.3V. IEC-väylä on open-collector (aktiivisena
vedetään alas), joten 3.3V:n LOW-taso riittää. Floppy-signaalit ovat samoin
aktiivi-LOW. Blue Pill saattaa toimia **ilman tasonmuuntimia** open-collector-
kytkennällä, mutta tämä vaatii testausta.

---

## Yhteenveto — parhaat vaihtoehdot

### Tier 1: Suositeltavat

| Lauta | Hinta | Miksi |
|---|---|---|
| **Raspberry Pi Pico** | ~4-5€ | Paras kokonaisuus: PIO, dual-core, 264 KB RAM |
| **Raspberry Pi Pico 2** | ~5-6€ | Picon seuraaja, PIO v2, enemmän RAM:ia |
| **Arduino Nano + 23LC1024** | ~6-8€ | Edullisin 5V-ratkaisu, ei tasonmuuntimia |
| **Arduino Mega 2560** | ~8-12€ | 8 KB RAM + 54 GPIO, 5V, ei SRAM:ia välttämättä tarvita |

### Tier 2: Toimivia vaihtoehtoja

| Lauta | Hinta | Miksi |
|---|---|---|
| **STM32 Blue Pill** | ~2-3€ | Halvin, 20 KB RAM, 5V-tolerantti, mutta voi tarvita tasonmuuntimia |
| **STM32 Black Pill** | ~3-5€ | 128 KB RAM, 100 MHz, mutta 3.3V |
| **Arduino Nano Every** | ~8-10€ | 6 KB RAM, 20 MHz, 5V, CCL-logiikka |
| **Arduino Uno** | ~5€ | Sama kuin Nano, isompi |
| **Arduino Leonardo** | ~5-8€ | +500B RAM vs. Nano, natiivi USB |

### Tier 3: Toimivat mutta huono hinta/hyöty

| Lauta | Hinta | Miksi |
|---|---|---|
| **Arduino Due** | ~12-15€ | Tehokas mutta 3.3V, sama hinta kuin Pico |
| **ESP32** | ~4-5€ | Halpa ja tehokas, mutta RTOS-jitter ja WiFi/BT turhia |
| **Teensy 4.0/4.1** | ~25-35€ | Ylilyönti, kallis |
| **Adafruit Feather M4** | ~25€ | Kallis, 3.3V |

### Ei sovellu

| Lauta | Miksi ei |
|---|---|
| **Arduino Nano 33 IoT/BLE** | Vain 14 GPIO — ei riitä |
| **Seeed XIAO (kaikki)** | Vain 11 GPIO — ei riitä |
| **ESP8266** | Vain 11 GPIO, 3.3V, ei Input Capture |
