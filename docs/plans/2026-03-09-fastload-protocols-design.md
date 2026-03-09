# Fast-Load Protocol Support — Design Document

**Päivämäärä:** 2026-03-09
**Kohde:** firmware/E-IEC-Nano-SRAM (Arduino Nano + 23LC256)

## Tavoite

Lisätä JiffyDOS-, Burst- ja EPYX FastLoad -fast-load-protokollatuki
Option E:n IEC-firmwareen modulaarisella arkkitehtuurilla. Ei rautamuutoksia —
kaikki protokollat käyttävät samoja CLK/DATA-linjoja (D3/D4) eri tavalla.

## Arkkitehtuuri

### Function pointer -taulu

Jokainen protokolla rekisteröi omat funktionsa yhteiseen tauluun:

```c
typedef enum {
    FASTLOAD_NONE = 0,
    FASTLOAD_JIFFYDOS,
    FASTLOAD_BURST,
    FASTLOAD_EPYX,
    FASTLOAD_MAX
} fastload_type_t;

typedef struct {
    fastload_type_t type;
    const char *name;
    bool (*detect)(void);
    bool (*send_byte)(uint8_t byte, bool eoi);
    bool (*receive_byte)(uint8_t *byte, bool *eoi);
    void (*on_atn_end)(void);
} fastload_protocol_t;
```

Keskeiset funktiot:
- `fastload_init()` — rekisteröi kaikki protokollat
- `fastload_detect()` — kokeilee kaikkia, palauttaa tunnistetun
- `fastload_active()` — nykyinen aktiivinen protokolla (tai NULL)
- `fastload_reset()` — palaa standardiin IEC:hen

### Tiedostorakenne

```
firmware/E-IEC-Nano-SRAM/
  include/
    fastload.h              # Yhteinen rajapinta + tyypit
    fastload_jiffydos.h     # JiffyDOS-vakiot ja ajoitukset
    fastload_burst.h        # Burst mode -vakiot
    fastload_epyx.h         # EPYX FastLoad -vakiot
  src/
    fastload.c              # Protokollarekisteri, dispatch, init
    fastload_jiffydos.c     # JiffyDOS: detect, send, receive
    fastload_burst.c        # Burst: detect, send, receive
    fastload_epyx.c         # EPYX: detect, send, receive
```

### Integraatio iec_protocol.c:hen

Kolme muutoskohtaa:

1. **Talker-tila:** `iec_send_byte()` → tarkistaa `fastload_active()`, käyttää sen `send_byte()`:a
2. **ATN vapautuu:** kutsuu `fastload_detect()` + `on_atn_end()`
3. **UNTALK/UNLISTEN:** kutsuu `fastload_reset()`

Nykyiset `iec_send_byte()` ja `iec_receive_byte()` säilyvät muuttumattomina
standardi-IEC:n toteutuksena.

## Protokollat

### JiffyDOS

- **Nopeus:** ~5-10 KB/s (vs. standardi ~1 KB/s)
- **C64-puoli:** KERNAL-ROM korvataan JiffyDOS-ROMilla
- **Tunnistus:** ATN-sekvenssin jälkeen C64 pitää DATA-linjan alhaalla ~260µs.
  Nano mittaa pulssin keston ja tunnistaa JiffyDOS:n.
- **Tavunsiirto:** 2 bittiä kerrallaan CLK+DATA -linjoilla, 4 kierrosta = 1 tavu.
  Jokainen kierros ~13µs → kokonainen tavu ~52µs (vs. standardi ~960µs).

| Kierros | CLK    | DATA   | Bitit    |
|---------|--------|--------|----------|
| 1       | bit 0  | bit 1  | LSB-pari |
| 2       | bit 2  | bit 3  |          |
| 3       | bit 4  | bit 5  |          |
| 4       | bit 6  | bit 7  | MSB-pari |

- **EOI:** pidempi viive (~200µs) ennen viimeistä tavua
- **Ajoitukset:**
  - `JIFFY_DETECT_MIN_US` = 200µs (DATA-pulssin minimi)
  - `JIFFY_DETECT_MAX_US` = 320µs (DATA-pulssin maksimi)
  - `JIFFY_BIT_PAIR_US` = 13µs (per 2-bit-kierros)
  - `JIFFY_EOI_DELAY_US` = 200µs
  - `JIFFY_BYTE_DELAY_US` = 30µs

### Burst Mode

- **Nopeus:** ~15 KB/s
- **C64-puoli:** Ei toimi C64:llä — vaatii C128:n CIA:n CNT-pinnikytkennän.
  Toteutetaan tulevaisuusvarauksena C128-yhteensopivuutta varten.
- **Tunnistus:** C128 lähettää `U0`-komennon komentokanaville (SA 15).
  Nano tunnistaa tämän komentopuskurista.
- **Tavunsiirto:** Kellotahdistettu 8-bitin sarjasiirto:
  1. Talker asettaa DATA-linjan tilan
  2. Talker pulsuttaa CLK:ta — 1 pulssi/bitti, 8 pulssia/tavu
  3. Listener lukee DATA:n CLK:n nousevalla reunalla
  4. ~8µs per bitti → ~65µs per tavu
- **Ajoitukset:**
  - `BURST_CLK_PULSE_US` = 8µs
  - `BURST_SETUP_US` = 4µs (data setup ennen CLK-reunaa)
  - `BURST_BYTE_DELAY_US` = 20µs

### EPYX FastLoad

- **Nopeus:** ~3-5 KB/s
- **C64-puoli:** EPYX FastLoad -cartridge (tai klooni)
- **Tunnistus:** EPYX yrittää ladata "drive code" -ohjelman 1541:lle
  M-W (Memory-Write) -komennoilla. Nano tunnistaa M-W-sekvenssin
  komentopuskurista eikä tallenna dataa vaan merkitsee EPYX:n aktiiviseksi.
- **Tavunsiirto:** 2-bit-protokolla eri ajoituksella kuin JiffyDOS:

| Vaihe       | Toiminto                                      |
|-------------|-----------------------------------------------|
| Sync        | C64 pudottaa CLK, laite pudottaa DATA         |
| Kierros 1-4 | CLK=bit(2n), DATA=bit(2n+1), ~14µs/kierros    |
| ACK         | Laite nostaa DATA, C64 nostaa CLK              |

- **Rajoitukset:** Vain LOAD-tuki (ei SAVE, ei drive monitor, ei kopiointi)
- **Ajoitukset:**
  - `EPYX_BIT_PAIR_US` = 14µs
  - `EPYX_HANDSHAKE_US` = 20µs
  - `EPYX_BYTE_DELAY_US` = 25µs

## Resurssit

| | JiffyDOS | Burst | EPYX FastLoad | Yhteensä |
|---|----------|-------|---------------|----------|
| Flash-arvio | ~500B | ~350B | ~650B | ~1800B |
| RAM-lisäys | ~0B | ~0B | ~10B | ~10B |
| Rautamuutokset | Ei | Ei | Ei | Ei |

Nykyinen firmware: Flash 11,842B (36,1%), RAM 1,233B (60,2%).
Lisäyksen jälkeen: Flash ~13,650B (41,6%), RAM ~1,243B (60,7%).
Mahtuu hyvin ATmega328P:n 32KB flashiin ja 2KB RAMiin.

## Testaus

### Host-testit (`test/test_nano_fastload.c`, ~15-20 testiä)

Protokollakerros on GPIO-ajoituksiin sidottu, joten testataan hostilla:
- Protokollarekisterin hallinta (init, rekisteröinti, detect, reset)
- `fastload_active()` palauttaa oikean/NULL
- JiffyDOS-bittimanipulaatio (tavun pakkaus/purku 2-bit-pareiksi)
- EPYX M-W-tunnistuslogiikka puskurista
- Burst U0-komennon parsinta

### VICE-emulaattori (manuaalinen)

- JiffyDOS-ROM VICE:ssä → lataus Nano-laitteelta
- EPYX FastLoad -cartridge-image VICE:ssä
- Ajoitusten verifiointi logiikka-analysaattorilla oikealla raudalla

## Toteutusjärjestys

1. `fastload.c` + `fastload.h` (rekisteri, dispatch)
2. `fastload_jiffydos.c/.h` (tunnistus + send/receive)
3. `fastload_burst.c/.h` (tunnistus + send/receive)
4. `fastload_epyx.c/.h` (tunnistus + send/receive)
5. `iec_protocol.c` integraatio
6. Host-testit
7. Makefile-päivitys
