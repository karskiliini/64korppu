# MFM-dekoodaus: ATmega328P + 74LS14 Schmitt-trigger

Kattava dokumentaatio MFM (Modified Frequency Modulation) -dekoodauksesta
PC 3.5" HD -floppy-asemasta Arduino Nanolla.

> **KORJAUS (2026-07):** Tämän dokumentin aiemmat versiot sisälsivät
> kaksi perustavaa virhettä, jotka on nyt korjattu:
>
> 1. **HD-intervallit olivat 2× väärin.** 500 kbit/s → databitti = 2µs,
>    puolisolu T = 1µs. Flux-välit ovat 2T/3T/4T = **2/3/4 µs =
>    32/48/64 tikkiä** @16MHz. Aiempi taulukko (64/96/128 tikkiä =
>    4/6/8 µs) oli DD 250 kbit/s -ajoitus.
> 2. **Mitatut "klusterit" (~107/133/155 tikkiä) olivat ISR:n oman
>    palveluajan artefakteja.** C-kielinen capture-ISR SPI-siirtoineen
>    kesti ~6-7 µs — enemmän kuin kolme HD 2T-väliä — joten ICR1
>    ylikirjoittui ja pulssit sulautuivat. Havaittu minimi-intervalli
>    ~107 tikkiä ≈ 6.7 µs on ISR:n kesto, ei levyn signaali. Koko
>    "variable delay" -teoria ja pull-up-viiveen kalibrointi sovittivat
>    tätä artefaktia. Kaappaus on nyt pollattu silmukka (keskeytykset
>    pois), ~25-30 sykliä/reuna.

## Sisällys

1. [MFM-perusteet](#mfm-perusteet)
2. [Signaalin kulku: asema → ATmega](#signaalin-kulku)
3. [Rautamuutokset ja havainnot](#rautamuutokset-ja-havainnot)
4. [Timer1 Input Capture -arkkitehtuuri](#timer1-input-capture)
5. [Raw-intervalli-arkkitehtuuri](#raw-intervalli-arkkitehtuuri)
6. [Kalibrointi: delay-arvon laskenta](#kalibrointi)
7. [Sync-kuvion 0x4489 haku](#sync-kuvion-haku)
8. [HD-raidan rakenne](#hd-raidan-rakenne)
9. [Variable delay -ongelma](#variable-delay)
10. [Debuggausopit](#debuggausopit)
11. [Muistilista: MFM-dekooderin tarkistuspisteet](#muistilista)

---

## MFM-perusteet

MFM koodaa dataa pulssi-intervallien avulla. Jokainen databitti tuottaa 2 MFM-bittiä (clock + data):

| Edellinen data | Nykyinen data | Clock | Data |
|---------------|---------------|-------|------|
| 0             | 0             | 1     | 0    |
| 0             | 1             | 0     | 1    |
| 1             | 0             | 0     | 0    |
| 1             | 1             | 0     | 1    |

**Sääntö**: clock = NOT(prev_data) AND NOT(current_data)

### Intervallityypit

Flux-transitioiden väli MFM:ssä on aina 2T, 3T tai 4T (T = puolisolu
= puoli databittiä):

| Tyyppi | Nimellinen | HD 500kbps @ 16MHz |
|--------|-----------|-------------------|
| 2T     | 2 × 16 = 32 tikkiä | 2.0 µs |
| 3T     | 3 × 16 = 48 tikkiä | 3.0 µs |
| 4T     | 4 × 16 = 64 tikkiä | 4.0 µs |

Databitti = 2 µs HD:lle (500 kbps datarate, 1 Mbps MFM clock),
puolisolu T = 1 µs = 16 tikkiä.

### Tyypilliset tavujen intervallisekvenssit

| Tavu | MFM-bittivirta | Intervallit |
|------|----------------|-------------|
| 0x4E (gap fill) | 10 01 00 10 01 01 01 00 | 3T, 3T, 3T, 2T, 2T, 3T |
| 0x00 (preamble) | 10 10 10 10 10 10 10 10 | 2T, 2T, 2T, 2T, 2T, 2T, 2T, 2T |
| 0xA1 (sync, missing clock) | 01 00 01 00 10 00 10 01 | 4T, 3T, 4T, 3T |
| 0xFE (IDAM) | 01 01 01 01 01 01 01 00 | 2T, 2T, 2T, 2T, 2T, 2T, 2T |

**Huom**: Sync 0xA1:n raaka MFM-kuvio on **0x4489** — tämä on ainoa paikka missä 4T-intervalleja esiintyy rakenteellisesti.

---

## Signaalin kulku

```
  Floppy-asema          74LS14            Invertteri        ATmega328P
  /RDATA (open-         Schmitt-          (74LS14           Timer1 ICP1
   collector)           trigger           gate #2)          (PB0 / D8)
                        Gate #1
  ─┐    ┌─ 5V         ┌───────┐         ┌───────┐         ┌──────────┐
   │    │  150Ω        │       │         │       │         │          │
   │    ├──┤├──────────┤ IN  OUT├─────────┤ IN  OUT├─────────┤ ICP1     │
   │    │              │       │         │       │         │          │
   └────┘              └───────┘         └───────┘         └──────────┘
  ~200ns neg.          1.7V thresh.      Palauttaa          Falling edge
  pulssit              Invertoi          alkup. polarit.    trigger
```

### Signaaliketju

1. **Floppy /RDATA**: Open-collector (SN7438), vedetään HIGH 150Ω pull-upilla
2. **74LS14 gate #1**: Schmitt-trigger invertoi (kynnys 1.7V, hystereesin 0.8V)
3. **74LS14 gate #2**: Toinen invertteri palauttaa alkuperäisen polariteetin
4. **ATmega ICP1**: Falling edge -triggaus kaappaa Timer1-arvon

**Nettoefekti**: Kaksosinversio = alkuperäinen polariteetti. Falling edge kaappaa jokaisen flux-transition.

---

## Rautamuutokset ja havainnot

### 1. Pull-up-vastus: 10kΩ → 150Ω

| Parametri | 10kΩ (alkup.) | 150Ω (korjattu) |
|-----------|--------------|-----------------|
| RC-aikavakio (10pF) | 100 ns | 1.5 ns |
| Rise time | ~300 ns | ~10 ns |
| Sink current @ 5V | 0.5 mA | 33 mA |
| Vaikutus intervalleihin | Ei muutosta! | Ei muutosta! |

**Havainto**: Pull-up-vastuksen muuttaminen EI vaikuttanut mitattuihin intervalleihin. Syy: ATmega328P:n CMOS-kynnys (3.0V = 0.6×Vcc) on liian korkea. Signaali ei koskaan nouse 3.0V:iin riittävän nopeasti riippumatta pull-upista, koska floppy-aseman open-collector -ulostulo ei vedä signaalia täysin HIGH-tilaan 200ns aikana.

**Oppi**: Ongelma oli kynnysarvo, ei RC-aikavakio.

### 2. 74LS14 Schmitt-trigger lisäys

| Parametri | Ilman 74LS14 | 74LS14:n kanssa |
|-----------|-------------|----------------|
| Input threshold | 3.0V (CMOS) | 1.7V (TTL Schmitt) |
| Hystereesin | ~0.1V | 0.8V |
| Delay (mitattu) | ~109 tikkiä | ~32-40 tikkiä |
| 2T-klusteri | ~173 tikkiä | ~106 tikkiä |
| 3T-klusteri | ~205 tikkiä | ~133 tikkiä |

**Havainto**: 74LS14 pienensi viiveen dramaattisesti (~109 → ~40 tikkiä) ja tuotti selkeämmät klusterit. Alempi kynnysarvo (1.7V vs 3.0V) laukaisee aikaisemmin signaalin nousussa.

> **Jälkikäteistulkinta**: yllä olevat klusteriarvot (~106/133 jne.)
> mitattiin ISR-pohjaisella capturella, jonka palveluaika (~107
> tikkiä) dominoi mittausta — ne EIVÄT kuvaa levyn signaalia. Oikeat
> HD-klusterit ovat 32/48/64 tikkiä. Schmitt-trigger on silti hyvä
> lisä signaalin laadulle, mutta se ei ollut pullonkaula.

**Kytkentä**: Kahden 74LS14-portin kautta (double inversion). Yksi piiri sisältää 6 porttia, joten 2 porttia yhdestä IC:stä riittää.

### 3. ICNC1 Noise Canceler -kokeilu

Timer1:n noise canceler (ICNC1) lisää 4 kellosyklin viiveen input captureen.

| Parametri | ICNC1 OFF | ICNC1 ON |
|-----------|-----------|----------|
| Ylimääräinen viive | 0 ns | 250 ns |
| Vaikutus intervalleihin | - | Ei muutosta |

**Havainto**: Noise canceler EI muuttanut intervalleja. Se lisää vakioviiveen jokaiseen kaappaukseen, joka kumoutuu differenssissä (interval = current - previous). 74LS14:n kanssa käytetään ICNC1 OFF, koska Schmitt-triggerin hystereesin hoitaa kohinansuodatuksen.

### 4. Reunan valinta (Rising vs Falling edge)

| Kytkentä | Oikea reuna |
|----------|-------------|
| Suora /RDATA → ICP1 | Falling edge (ICES1=0) |
| 74LS14 yksinkertainen inversio | Rising edge (ICES1=1) |
| 74LS14 kaksosinversio | **Falling edge (ICES1=0)** |

**Nykyinen kokoonpano**: Kaksosinversio → falling edge.

### 5. 74HC595 boot safety

| Ongelma | Ratkaisu |
|---------|---------|
| 74HC595 tuottaa satunnaisia arvoja käynnistyksessä | RCLK pinnattu LOW ensimmäisenä käskynä |
| /WGATE voi vahingossa aktivoitua | /OE (Output Enable) ohjataan A3:lla |

---

## Timer1 Input Capture

ATmega328P Timer1 ICP1 nappaa 16-bittisen aikaleiman jokaisella
flux-transitiolla. Kaappaus on **pollattu silmukka keskeytykset
poissa** — EI ISR:
	
```c
cli();
uint8_t prev8 = ICR1L;
TIFR1 = (1 << ICF1);
while (count < cap) {
    while (!(TIFR1 & (1 << ICF1))) { /* + idle-timeout */ }
    uint8_t now8 = ICR1L;             // 8-bit riittää: intervallit < 108
    TIFR1 = (1 << ICF1);
    uint8_t interval = now8 - prev8;
    prev8 = now8;
    while (!(SPSR & (1 << SPIF)));    // SPI-interlock (ei odota käytännössä)
    SPDR = interval;                  // SRAM:iin sekventiaalikirjoituksella
    count++;
}
sei();
```

### Miksi ei ISR?

C-kielinen ISR (prologi + ICR1 + SPI-siirto + 32-bit laskuri + epilogi)
kestää ~90-110 sykliä ≈ 6-7 µs. HD-signaalissa reunoja tulee 2-4 µs
välein, joten ISR hävisi reunoja jatkuvasti: ICR1 ylikirjoittui ja
intervallit sulautuivat. Mitattu "2T-klusteri" ~107 tikkiä (6.7 µs)
oli täsmälleen ISR:n palveluaika. Pollattu silmukka on ~25-30 sykliä
(< 2 µs) per reuna, ja ICR1:n laitteistolatchi antaa vielä yhden
intervallin verran pelivaraa.

### Kriittiset parametrit

- **Kellotaajuus**: 16 MHz, ei prescaleria → 62.5 ns resoluutio
- **Silmukan kesto**: ~25-30 sykliä < 2 µs → pysyy HD 2T-tahdissa
- **Min. intervalli**: 2T = 2 µs = 32 tikkiä
- **Kaappausmäärä**: 50000 intervallia ≈ 0.7 kierrosta HD:llä
  (~74000 transitiota/kierros); 23LC256:lla rajataan 32768:aan
- **Tallennus**: uint8_t riittää (validit intervallit < 108 tikkiä)
- **Keskeytykset**: pois koko kaappauksen ajan (~150 ms); ATN (INT0)
  jää odottamaan ja palvellaan heti sei():n jälkeen

### Timer1-asetukset

```c
TCCR1A = 0;                    // Normal mode
TCCR1B = (1 << CS10);          // No prescaler (16 MHz)
TCCR1B &= ~(1 << ICES1);      // Falling edge (kaksosinversio)
// ICNC1 OFF — Schmitt-trigger hoitaa kohinansuodatuksen
```

---

## Raw-intervalli-arkkitehtuuri

Aiempi lähestymistapa luokitteli pulssin heti ISR:ssä (2T/3T/4T-koodiksi) ja pakkasi 4 koodia per tavu. Tämä hylättiin koska:

1. ISR:n aikana ei voi tietää oikeaa delay-arvoa
2. Kiinteä luokittelu ei toimi variable delayn kanssa
3. Raakadata mahdollistaa jälkikäteen kokeilun eri parametreilla

**Nykyinen arkkitehtuuri**:
- ISR tallentaa raaka uint8_t -intervallin suoraan SRAM:iin
- 50000 intervallia @ 1 tavu = 50 KB (sopii 23LC512:n 64 KB:hen)
- Dekoodaus tapahtuu jälkikäteen lukemalla SRAM:ista

---

## Kalibrointi

### Analyyttinen kynnyslaskenta histogrammista

Brute-force-kalibrointi hylättiin koska pisteytysalgoritmi tuotti
vääriä tuloksia. Pollatun capturen puhtaalla datalla kalibrointi on
suoraviivainen: klusterit ovat nimellispaikoillaan (32/48/64) ± pieni
jitteri, eikä erillistä "delay"-arvoa ole (vakioviive kumoutuu
intervallien erotuksessa).

1. **Histogrammi**: 8 tikin binnit alueella 24-103 (10 binniä)
2. **Kaksi huippua**: 2 suurinta binniä → 2T- ja 3T-klusterien keskipisteet
3. **Kynnykset**: klusterien puolivälit

```
thr_23   = (c2T + c3T) / 2          # ~40
thr_34   = c3T + (c3T - c2T) / 2    # ~56
thr_4max = thr_34 + (c3T - c2T)     # ~72
```

### Luokittelurajat (nimellinen HD-signaali)

| Raja | Intervalli | Tyyppi |
|------|-----------|--------|
| noise | < 20 tikkiä | hylätään |
| 2T | < 40 tikkiä | 2 solua |
| 3T | < 56 tikkiä | 3 solua |
| 4T | < 72-76 tikkiä | 4 solua |
| inv | ≥ 76 tikkiä | hylätään |

---

## Sync-kuvion haku

### Miksi raaka 0x4489 eikä dekoodattu 0xFE?

Aiempi dekooderi etsi tavuarvoja (0xFE IDAM) dekoodatusta bittivirrasta. Tämä epäonnistui koska:

1. **Offset-ongelma**: Dekoodauksen bitti-offset riippuu kaappauksen aloituskohdasta (satunnainen). Väärä offset tuottaa roskaa.
2. **Framing error**: Yksittäinen väärin luokiteltu intervalli siirtää kaikkien seuraavien tavujen rajoja pysyvästi.
3. **Pisteytyksen epäluotettavuus**: Brute-force offset-haku tuotti vääriä tuloksia koska väärä luokittelu saattoi vahingossa tuottaa tunnistettavia tavuja (esim. 0xA1).

### Oikea lähestymistapa: raaka sync-kuvio

Oikeat floppy-kontrollerit (esim. NEC µPD765) etsivät raakaa MFM-kuviota **0x4489** bittivirrasta. Tämä kuvio on itsesynkronoiva:

```
0x4489 = 0100 0100 1000 1001
```

Tämä on 0xA1:n MFM-koodaus **puuttuvalla kellopulssilla** (missing clock bit). Puuttuva kellopulssi tekee kuviosta ainutlaatuisen — se ei voi esiintyä normaalissa MFM-datassa.

### Sync-haun algoritmi

```
1. Akkumuloi intervalleja bittivirraksi: raw_bits = (raw_bits << cells) | 1
2. Jokaisen intervallin jälkeen tarkista: (raw_bits & 0xFFFF) == 0x4489
3. Laske 3 peräkkäistä sync-merkkiä
4. Sync-merkin jälkeen offset = 0 (tiedetään koska sync on kiinteässä paikassa)
5. Lue seuraava tavu → IDAM (0xFE) tai DAM (0xFB)
```

### Preamble-pohjainen haku (vaihtoehto)

Jos 0x4489-haku ei toimi (esim. 4T-intervallien luokittelu epäonnistuu), vaihtoehtoinen tapa on etsiä **preamble**: 12 × 0x00 = 96 peräkkäistä 2T-intervallia. Preamble on ainutlaatuinen koko raidalla.

---

## HD-raidan rakenne

### 1.44 MB HD-levyn sektorirakenne

```
┌─────────────────────────────────────────────────────────────────────┐
│ GAP 4a: 80 × 0x4E                                                  │
├─────────────────────────────────────────────────────────────────────┤
│ Sektori 1:                                                          │
│  12 × 0x00 (preamble) → 3 × 0xA1 (sync) → 0xFE (IDAM)            │
│  T=0 S=0 R=1 N=2 (4 ID-tavua) → CRC (2 tavua)                    │
│  22 × 0x4E (gap 2)                                                 │
│  12 × 0x00 (preamble) → 3 × 0xA1 (sync) → 0xFB (DAM)            │
│  512 × data → CRC (2 tavua)                                        │
│  54 × 0x4E (gap 3)                                                 │
├─────────────────────────────────────────────────────────────────────┤
│ Sektori 2: ... (sama rakenne)                                       │
│ ...                                                                 │
│ Sektori 18: ...                                                     │
├─────────────────────────────────────────────────────────────────────┤
│ GAP 4b: täyttö kierroksen loppuun                                   │
└─────────────────────────────────────────────────────────────────────┘
```

### Intervallien jakautuminen raidalla

| Alue | Intervallit | Osuus |
|------|-----------|-------|
| Gap fill (0x4E) | 3T, 3T, 3T, 2T, 2T, 3T (toistuu) | ~60% |
| Preamble (0x00) | Pelkää 2T | ~5% |
| Sync (0xA1 × 3) | 4T, 3T, 4T, 3T (toistuu) | ~1% |
| Sektori-ID + CRC | Vaihteleva | ~2% |
| Data (512B/sektori) | Vaihteleva | ~30% |

### FAT12-rakenne levyllä

| LBA | Raita | Sivu | Sektori | Sisältö |
|-----|-------|------|---------|---------|
| 0 | T=0 | S=0 | R=1 | Boot sector |
| 1-9 | T=0 | S=0 | R=2-10 | FAT #1 (9 sektoria) |
| 10-18 | T=0 | S=0-1 | R=11-18, R=1 | FAT #2 |
| 19-32 | T=0 | S=1 | R=2-15 | Root directory |
| 33+ | T=0+ | S=1 | R=16+ | Data-alue |

---

## Variable delay — historiallinen artefakti

### Mitä "variable delay" oikeasti oli

Aiemmat versiot havaitsivat, että mitattu viive riippui edellisestä
intervallista (~30-42 tikkiä), ja rakensivat kalibroinnin tämän
"muuttuvan viiveen" ympärille. Todellinen selitys: **ISR-pohjainen
capture ei ehtinyt mukaan**. ISR kesti ~107 tikkiä; kun reunoja tuli
tiheämmin, ICR1 ylikirjoittui ja mitattu intervalli oli usean oikean
intervallin summa miinus ISR:n viivejäännökset. "Viiveen" riippuvuus
edellisestä intervallista oli suoraa seurausta siitä, kuinka monta
reunaa ehti saapua ISR:n ollessa varattuna.

Pollatulla capturella ilmiötä ei ole: kiinteät kynnykset 40/56/76
luokittelevat nimelliset 32/48/64-klusterit oikein.

### Miksi PLL olisi silti parempi

PC:n floppy-kontrollerit käyttävät **PLL:ää** joka seuraa signaalin
vaihetta ja sopeutuu moottorin nopeusvaihteluun (±1-2%). Kiinteät
kynnykset toimivat, koska klusterien väli (16 tikkiä) on suuri
suhteessa jitteriin, mutta PLL antaisi lisämarginaalia huonokuntoisille
levyille.

---

## Debuggausopit

### 1. Histogrammi on paras diagnostiikkatyökalu

16 tikin binnit 48-208 alueella + min/max antavat välittömästi kuvan signaalin laadusta:
- **2 puhdasta klusteria** (esim. 96-111 ja 128-143): hyvä signaali, gap fill -alue
- **Leveämmät klusterit** + häntä 144-159:ssä: data-alue mukana, mahdollisesti 4T
- **3+ klusteria**: mahdollisia ongelmia tai data-alueen vaihteleva sisältö

### 2. Raaka-tikki-dumppaus paljastaa välittömästi

64 ensimmäistä raaka-arvoa HEX-muodossa näyttävät suoraan:
- Onko gap fill -alue: vuorotteleva `6B 85 6B 86` → 2T, 3T, 2T, 3T
- Onko data-alue: vaihteleva `76 85 79 5E 95` → sekaisia intervalleja
- Missä kohdassa raitaa kaappaus alkoi

### 3. Pisteytysalgoritmit ovat petollisia

Brute-force-kalibrointi tuotti vääriä tuloksia monella tavalla:
- **All-2T → all-0x00**: Väärä delay muutti kaiken 2T:ksi → 0x00-tavut saivat pisteitä
- **Vahingolliset 0xA1-osumat**: Väärä cell-luokittelu tuotti bittivirtaa joka sisälsi 0xA1:n vahingossa
- **Diversiteetti-check liian heikko**: Boolean (has_2t/has_3t) vs. laskuripohjainen (cnt_2t >= 50)

**Oppi**: Analyyttinen laskenta histogrammista on luotettavampi kuin pisteytys.

### 4. Offset on satunnainen ja riippuu kaappauskohdasta

MFM-bittivirran clock/data -parin offset riippuu siitä, mihin kohtaan raitaa kaappaus alkaa. Tämä tarkoittaa:
- Offset=0 tai offset=1, vaihtelee jokaisella kaappauksella
- **Ratkaisu**: Käytä raakaa 0x4489 sync-kuviota joka on itsesynkronoiva

### 5. 4T-intervallien puuttuminen on kriittinen indikaattori

Jos histogrammissa EI näy lainkaan intervalleja > 143:
- Sync-merkinnät (0xA1 missing clock) eivät tuota 4T-intervalleja
- Mahdollinen HW-ongelma: signaaliketju suodattaa pitkät pulssit
- Tai delay-arvo liian korkea → 4T luokitellaan 3T:ksi

### 6. Motor spin-up timeout

`[FLP] motor spin-up timeout (no /RDATA)` — moottori käynnistyi mutta
/RDATA-aktiviteettia ei havaittu 1000ms aikana. Voi johtua:
- Kelkka ei ole raidalla (korjaantuu recalibratessa)
- /RDATA-kaapeli irti tai pull-up puuttuu
- Levy puuttuu asemasta

> **Huom**: aiempi versio tästä tarkistuksesta mittasi "intervalleja"
> 1 ms välein otetuista ICR1-näytteistä — erotus oli aina ~16000
> tikkiä, joten se tulosti "no /RDATA" myös täysin terveellä
> signaalilla. Nykyinen versio laskee ICF1-aktiviteettia per 1 ms
> -ikkuna, mikä toimii oikein.

---

## Muistilista: MFM-dekooderin tarkistuspisteet

### Signaaliketju
- [ ] /RDATA-signaali menee 150Ω pull-upille → 74LS14 → (optio: toinen invertteri) → ICP1 (D8)
- [ ] Falling edge (ICES1=0) kaksosinversiolla, rising edge (ICES1=1) yksinkertaisella inversiolla
- [ ] 74LS14:n Vcc ja GND kytketty, bypass-kondensaattori (100nF)
- [ ] Pull-up 150Ω, EI 10kΩ (vaikka kumpikaan ei vaikuta — 74LS14:n kynnys on ratkaiseva)

### Kaappaus (pollattu)
- [ ] Timer1: normal mode, no prescaler (CS10), no ICNC1
- [ ] Capturen koko: 50000 intervallia ≈ 0.7 kierrosta HD
      (23LC256:lla rajautuu 32768:aan — sram_capacity())
- [ ] Pollattu silmukka (cli/sei), ~26 sykliä/reuna — EI ISR
- [ ] Intervallit uint8_t; validit < 108 tikkiä

### Histogrammi
- [ ] 2 selkeää klusteria näkyvissä (2T ja 3T)
- [ ] 2T-klusteri: ~24-39 (center ~32)
- [ ] 3T-klusteri: ~40-55 (center ~48)
- [ ] 4T-häntä: ~56-71 (sync-alueet)
- [ ] min > 20, max < 108 (terve signaali)

### Kalibrointi
- [ ] Kynnykset klusterien puoliväleistä: ~40 / ~56 / ~72
- [ ] Ei "delay"-käsitettä — vakioviive kumoutuu erotuksessa
- [ ] 4T-intervallit (~64) luokittuvat oikein 4T:ksi

### Sync-haku
- [ ] Raaka 0x4489 kuvio bittivirrassa (ei dekoodattu tavuarvo)
- [ ] 3 peräkkäistä sync-merkkiä vaaditaan
- [ ] Sync:n jälkeen offset=0 → seuraava tavu on address mark
- [ ] Tarvitaan 4T-intervalleja → tarkista histogrammi!

### Sektori-ID validointi
- [ ] T < 80 (track)
- [ ] S ≤ 1 (side)
- [ ] R: 1-18 (sector number, HD)
- [ ] N = 2 (sector size 512B)

### Yleistä
- [ ] CRC-16: CCITT poly 0x1021, init 0xFFFF
- [ ] CRC lasketaan sync-tavuista (3 × 0xA1) + address mark + data
- [ ] SWIG memory leak -varoitukset KiCad Python API:sta: harmittomia
- [ ] `grep -P` ei toimi macOS:ssä — käytä `sed` tai `grep -E`
