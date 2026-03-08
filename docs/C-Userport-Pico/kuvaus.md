# Vaihtoehto C: User Port + MCU (Hyvä kompromissi)

## Arkkitehtuuri

```
C64 ──[User port]──> MCU (Pico) ──[34-pin]──> PC 3.5" HD floppy
```

## Toimintaperiaate

- MCU kommunikoi C64:n kanssa user portin kautta (rinnakkaisprotokolla)
- MCU ohjaa PC-korppuasemaa 34-pin Shugart-liitännällä
- Custom ajuri C64-puolella hoitaa user port -kommunikaation
- MCU hoitaa FAT12 ja floppy-ohjauksen

## Komponenttilista

| Komponentti | Kuvaus | Hinta (arvio) |
|---|---|---|
| Raspberry Pi Pico | RP2040 MCU | ~5€ |
| 74LVC245 x1 | Tasonmuunnin 3.3V ↔ 5V (user port) | ~1€ |
| 24-pin edge connector | User port -liitin | ~3€ |
| 34-pin IDC -liitin | PC-floppy | ~1€ |
| 4-pin Molex/Berg | Virta floppy-asemalle | ~1€ |
| Vastukset, kondensaattorit | Pull-upit, suodatus | ~1€ |
| **Yhteensä** | | **~12€** |

## PCB-vaatimukset

### User port -kytkennät

C64 user port tarjoaa 8-bittisen rinnakkaisportin (CIA2, $DD00-$DD0F):

| User port pin | Signaali | CIA2 | Pico GPIO | Käyttö |
|---|---|---|---|---|
| C | PB0 | $DD01 bit 0 | GP0 | Data bit 0 |
| D | PB1 | $DD01 bit 1 | GP1 | Data bit 1 |
| E | PB2 | $DD01 bit 2 | GP2 | Data bit 2 |
| F | PB3 | $DD01 bit 3 | GP3 | Data bit 3 |
| H | PB4 | $DD01 bit 4 | GP4 | Data bit 4 |
| J | PB5 | $DD01 bit 5 | GP5 | Data bit 5 |
| K | PB6 | $DD01 bit 6 | GP6 | Data bit 6 |
| L | PB7 | $DD01 bit 7 | GP7 | Data bit 7 |
| B | /FLAG2 | - | GP8 | Strobe (Pico → C64) |
| M | PA2 | $DD00 bit 2 | GP9 | Strobe (C64 → Pico) |
| 1,12 | GND | - | GND | Maa |
| 2 | +5V | - | VBUS | Virta (valinnainen) |

### Protokolla

8-bittinen rinnakkaissiirto handshake-signaaleilla:

```
C64 → Pico (komento/data):
  1. C64 asettaa datan PB0-PB7
  2. C64 nostaa PA2 (strobe)
  3. Pico lukee datan, kuittaa /FLAG2:lla

Pico → C64 (vastaus/data):
  1. Pico asettaa datan GP0-GP7
  2. Pico laskee GP8 (/FLAG2 → IRQ)
  3. C64 lukee PB0-PB7, kuittaa PA2:lla
```

### Floppy-kytkennät

Samat kuin vaihtoehdossa A (34-pin IDC → Pico GPIO:t GP10-GP22).

## Firmware-rakenne (Pico)

```
┌─────────────────────────────────┐
│         Pico Firmware           │
├────────────┬────────────────────┤
│  Core 0    │     Core 1         │
│  User port │  Floppy-ohjaus     │
│  protokolla│  MFM encode/decode │
│  FAT12 FS  │  Track seek/read   │
├────────────┴────────────────────┤
│  Jaettu puskuri (track data)    │
└─────────────────────────────────┘
```

## C64-ajuri

Ladattava ajuri (~1-2KB) joka:
- Hookkaa KERNAL LOAD/SAVE -vektorit ($0330-$0333)
- Kommunikoi Picon kanssa user portin kautta
- Tarjoaa LOAD, SAVE, hakemistolistaus
- Asennetaan komennolla: `LOAD "DRIVER",8 : SYS 49152`

```
; Esimerkki: ajurin latausrutiini
DRIVER_LOAD:
    ; Lähetä LOAD-komento Picolle
    LDA #CMD_LOAD
    STA $DD01       ; PB0-PB7 = komento
    LDA $DD00
    ORA #$04        ; PA2 = 1 (strobe)
    STA $DD00
    ; ... odota vastaus, lue data ...
```

## Plussat

- Nopeampi kuin IEC (~10-20 KB/s rinnakkaissiirrolla)
- Expansion port jää vapaaksi (cartridget, REU, jne.)
- User port on helppo liittää (ei tarvita monimutkaista osoitedekoodausta)
- Sama Pico-alusta kuin vaihtoehdossa A

## Miinukset

- Vaatii ajurin lataamisen C64-puolella ennen käyttöä
- User port varattu (ei voi käyttää RS-232-modeemia, printteri-adapteria)
- Ei suoraan yhteensopiva kaiken olemassa olevan ohjelmiston kanssa
- Ajurin hookkaus voi aiheuttaa yhteensopivuusongelmia joidenkin ohjelmien kanssa
- Vaatii toisen levyaseman (esim. 1541) ajurin lataamiseen ensimmäisellä kerralla

## Toteutusvaiheet

1. User port -kommunikaation testaus breadboardilla
2. 8-bittinen rinnakkaisprotokolla Pico ↔ C64
3. Floppy-ohjaus (sama koodi kuin vaihtoehdossa A)
4. FAT12-tiedostojärjestelmä
5. C64-ajurin kirjoittaminen (6502 assembly)
6. LOAD/SAVE/$ -komennot
7. PCB-suunnittelu
8. Testaus ja viimeistely
