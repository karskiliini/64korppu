# Vaihtoehto D: IEC + Arduino Nano — Piirikaavio

## Kokonaiskaavio

```
                                   Ulkoinen
                                   5V/2A PSU
                                   ┌───────┐
                                   │ +5V   │
                                   │ GND   │
                                   └───┬───┘
                                       │
                    ┌──────────────────┼──────────────────────┐
                    │                  │                      │
                    ▼                  ▼                      ▼
             ┌──────────────┐   ┌──────────────┐    ┌────────────────┐
             │   Floppy     │   │  Arduino     │    │  C64           │
             │   asema      │   │  Nano        │    │                │
             │   (PC 3.5"   │   │              │    │  IEC-portti    │
             │   HD)        │   │  5V natiivi! │    │  (6-pin DIN)   │
             │  34-pin IDC  │   │  (ei tason-  │    │                │
             │              │   │  muuntimia!) │    │                │
             └──────┬───────┘   └──────┬───────┘    └──────┬─────────┘
                    │                  │                    │
                    └──── GPIO ────────┴──── GPIO ──────────┘
```

## IEC-väylä — Suora kytkentä (ei tasonmuuntimia!)

```
  Arduino Nano on 5V-laite, samoin C64:n IEC-väylä.
  Tasonmuuntimia EI tarvita. Suora kytkentä riittää!

  IEC-väylä on open-collector: laitteet vetävät linjoja LOW.
  Arduino-pinni konfiguroidaan:
    - Assert: pinMode(OUTPUT), digitalWrite(LOW)
    - Release: pinMode(INPUT)  (ulkoinen pull-up nostaa HIGH:ksi)
    - Read: digitalRead()

           C64                                    Arduino Nano
     6-pin DIN-liitin                             (5V logiikka)
    ┌──────────────┐                             ┌──────────────┐
    │              │         1kΩ suoja            │              │
    │  Pin 3: ATN  ├─────────┤├──────────────────┤ D2 (INT0)    │
    │              │                              │              │
    │  Pin 4: CLK  ├─────────┤├──────────────────┤ D3           │
    │              │         1kΩ                  │              │
    │  Pin 5: DATA ├─────────┤├──────────────────┤ D4           │
    │              │         1kΩ                  │              │
    │  Pin 6: RESET├─────────┤├──────────────────┤ D5           │
    │              │         1kΩ                  │              │
    │  Pin 2: GND  ├─────────────────────────────┤ GND          │
    │              │                              │              │
    │  Pin 1: SRQ  │  (ei käytetä)               │              │
    └──────────────┘                             └──────────────┘

  Pull-up vastukset (IEC-väylän ulkoiset):

         +5V
          │
         1kΩ          Nämä ovat IEC-väylän
          ├─── ATN    pull-up vastukset.
          │           Normaalisti C64:n
         1kΩ          sisällä, mutta
          ├─── CLK    lisä-pull-upit
          │           parantavat
         1kΩ          signaalilaatua.
          ├─── DATA
          │
         GND (ei kytketä, vain kaavion selkeys)

  HUOM: IEC-väylässä C64 tarjoaa omat pull-upit (1kΩ).
  Lisä-pull-upit (4.7kΩ) voidaan lisätä Nano-puolelle
  signaalilaadun parantamiseksi:

         +5V                        +5V
          │                          │
        4.7kΩ                      4.7kΩ
          │                          │
  ATN ────┼──── D2          CLK ─────┼──── D3
          │                          │

         +5V
          │
        4.7kΩ
          │
  DATA ───┼──── D4
          │
```

## Floppy-asema — Suora kytkentä

```
  Floppy-aseman signaalit ovat myös 5V TTL — suora kytkentä!

         Arduino Nano                         34-pin IDC liitin
        ┌──────────────┐                     ┌─────────────────────┐
        │              │                     │                     │
        │ D6  /DENSITY ├─────────────────────┤  pin 2   /DENSITY   │
        │              │                     │  (LOW=HD 1.44MB)    │
        │ D7  /MOTEA ──├─────────────────────┤  pin 10  /MOTEA     │
        │              │                     │                     │
        │ D8  /DRVSEL ─├─────────────────────┤  pin 12  /DRVSB     │
        │              │                     │                     │
        │ D9  /MOTOR ──├─────────────────────┤  pin 16  /MOTOR     │
        │              │                     │                     │
        │ D10 /DIR ────├─────────────────────┤  pin 18  /DIR       │
        │              │                     │                     │
        │ D11 /STEP ───├─────────────────────┤  pin 20  /STEP      │
        │              │                     │                     │
        │ D12 /WDATA ──├─────────────────────┤  pin 22  /WDATA     │
        │              │                     │                     │
        │ D13 /WGATE ──├─────────────────────┤  pin 24  /WGATE     │
        │              │                     │                     │
        │ A0  /TRK00 ──├─────────────────────┤  pin 26  /TRK00     │
        │              │                     │  (input, pull-up)   │
        │ A1  /WPT ────├─────────────────────┤  pin 28  /WPT       │
        │              │                     │  (input, pull-up)   │
        │ A2  /RDATA ──├─────────────────────┤  pin 30  /RDATA     │
        │              │                     │  (input, pull-up)   │
        │ A3  /SIDE1 ──├─────────────────────┤  pin 32  /SIDE1     │
        │              │                     │                     │
        │ A4  /DSKCHG ─├─────────────────────┤  pin 34  /DSKCHG    │
        │              │                     │  (input, pull-up)   │
        │              │                     │                     │
        │ GND ─────────├─────────────────────┤  pins 1,3,5,...,33  │
        │              │                     │  (kaikki GND)       │
        └──────────────┘                     └─────────────────────┘

  Input-pinnien (A0, A1, A2, A4) sisäiset pull-upit aktivoidaan
  Arduinon firmwaressa: pinMode(pin, INPUT_PULLUP)
```

## Arduino Nano — Pinout

```
                        ┌──────────┐
                  ┌─────┤  USB     ├─────┐
                  │     └──────────┘     │
                  │     Arduino Nano     │
                  │                      │
    (TX)     D1  [1] │                      │ [30] VIN
    (RX)     D0  [2] │                      │ [29] GND
           /RST  [3] │                      │ [28] /RST
             GND [4] │                      │ [27] +5V ← virtalähde
    ATN      D2  [5] │                      │ [26] A7  (vain analog.)
    CLK      D3  [6] │                      │ [25] A6  (vain analog.)
    DATA     D4  [7] │                      │ [24] A5 ← LED/debug
    RESET    D5  [8] │                      │ [23] A4 ← /DSKCHG
    DENSITY  D6  [9] │                      │ [22] A3 ← /SIDE1
    MOTEA    D7 [10] │                      │ [21] A2 ← /RDATA
    DRVSEL   D8 [11] │                      │ [20] A1 ← /WPT
    MOTOR    D9 [12] │                      │ [19] A0 ← /TRK00
    DIR     D10 [13] │                      │ [18] AREF
    STEP    D11 [14] │                      │ [17] 3V3 (ei käytetä)
    WDATA   D12 [15] │                      │ [16] D13 ← WGATE
                  │                      │
                  └──────────────────────┘

  GPIO-yhteenveto:
    D0-D1:   Serial (USB debug, ei käytössä normaalisti)
    D2-D5:   IEC-väylä (ATN, CLK, DATA, RESET)
    D6-D13:  Floppy output-signaalit
    A0-A2:   Floppy input-signaalit (TRK00, WPT, RDATA)
    A3:      Floppy SIDE1 (output)
    A4:      Floppy DSKCHG (input)
    A5:      Status LED / debug
    A6-A7:   Vapaat (vain analoginen input)
```

## MFM-lukupiiri (Timer1 Input Capture)

```
  ATmega328P:n Timer1 Input Capture -ominaisuutta käytetään
  MFM flux-transitioiden ajoituksen mittaamiseen.

  ONGELMA: Input Capture (ICP1) on kiinteästi pin D8:ssa,
  mutta D8 on käytössä /DRVSEL:lle!

  RATKAISU 1: Vaihda /DRVSEL ja /RDATA pinnit:
    A2 → /DRVSEL (output)
    D8 → /RDATA via ICP1 (input capture)

  RATKAISU 2: Käytä Analog Comparator -tuloa (AIN1 = D7):
    D7 → /RDATA (Analog Comparator triggeri)
    Siirrä /MOTEA toiseen pinniin.

  RATKAISU 3 (suositeltu): Pin Change Interrupt (PCINT) A2:lla:
    A2 = PCINT10, voidaan käyttää PCINT1-keskeytyksellä.
    Ei yhtä tarkka kuin ICP, mutta riittävä.

  Paras ratkaisu: GPIO-uudelleenjärjestely:
  ═══════════════════════════════════════════

    D8 (ICP1) ← /RDATA   (Input Capture, tarkka ajoitus!)
    A2        ← /DRVSEL   (siirretty tänne, output)

  Päivitetty pinout:

    IEC:
      D2 = ATN, D3 = CLK, D4 = DATA, D5 = RESET

    Floppy output:
      D6 = /DENSITY, D7 = /MOTEA, A2 = /DRVSEL, D9 = /MOTOR
      D10 = /DIR, D11 = /STEP, D12 = /WDATA, D13 = /WGATE
      A3 = /SIDE1

    Floppy input:
      D8 = /RDATA (ICP1!), A0 = /TRK00, A1 = /WPT, A4 = /DSKCHG


  Timer1 Input Capture -konfiguraatio:
  ────────────────────────────────────

           /RDATA (floppy pin 30)
              │
              │  Flux transitions: pulssi ~200ns LOW
              │
              ▼
    D8 (ICP1) ──→ Timer1 Input Capture Register (ICR1)
                   │
                   │  Keskeytys jokaisella flux-transitiolla:
                   │  ICR1 sisältää Timer1-arvon transition-hetkellä.
                   │  Erotus edelliseen = pulssiväli.
                   │
                   │  @ 16 MHz:
                   │    4µs (short)  = 64 sykliä   → "10"
                   │    6µs (medium) = 96 sykliä   → "100"
                   │    8µs (long)   = 128 sykliä  → "1000"
                   │
                   │  Toleranssiraja (~±25%):
                   │    Short:  48-80 sykliä
                   │    Medium: 80-112 sykliä
                   │    Long:   112-144 sykliä
                   │
                   ▼
              ISR(TIMER1_CAPT_vect)
              {
                  uint16_t now = ICR1;
                  uint16_t interval = now - last_capture;
                  last_capture = now;
                  // Decode MFM cell...
              }
```

## Virtalähde

```
  Arduino Nano voidaan syöttää:
    a) USB:n kautta (debug/ohjelmointi)
    b) VIN-pinnin kautta (7-12V, on-board regulaattori)
    c) +5V-pinnin kautta (suoraan 5V)

  Suositeltu kytkentä:

    Ulkoinen 5V/2A PSU
    ┌──────────┐
    │          │
    │ +5V ─────┼──┬────── Arduino Nano pin 27 (+5V)
    │          │  │
    │          │  ├────── Floppy-asema +5V (pin 4)
    │          │  │
    │ GND ─────┼──┼────── Arduino Nano GND
    │          │  │
    │          │  └────── Floppy-asema GND
    └──────────┘


  Vaihtoehto: USB-virta Nanolle + erillinen floppy-virta:

    USB (tietokone/laturi)
    ┌──────────┐
    │ +5V ─────┼──── Arduino Nano USB (500mA max)
    └──────────┘

    Erillinen 5V/1A PSU
    ┌──────────┐
    │ +5V ─────┼──── Floppy-asema +5V
    │ GND ─────┼──┬─ Floppy-asema GND
    └──────────┘  └─ Arduino Nano GND (GND:t yhdistettävä!)
```

## Kokonaispiirikaavio

```
                    C64                              Floppy-asema
                  ┌──────┐                          ┌───────────┐
                  │      │                          │ PC 3.5"   │
                  │ IEC  │     Arduino Nano          │ HD 1.44MB │
                  │ port │    ┌────────────┐         │           │
                  │      │    │  ATmega328P│         │  34-pin   │
    6-pin DIN:    │      │    │  16 MHz    │         │  IDC      │
                  │      │    │  5V native │         │           │
              1kΩ │      │    │            │         │           │
    ATN ──────┤├──┤pin 3 ├────┤ D2 (INT0)  │         │           │
              1kΩ │      │    │            │         │           │
    CLK ──────┤├──┤pin 4 ├────┤ D3         │         │           │
              1kΩ │      │    │            │         │           │
    DATA ─────┤├──┤pin 5 ├────┤ D4         │         │           │
              1kΩ │      │    │            │         │           │
    RESET ────┤├──┤pin 6 ├────┤ D5         │         │           │
                  │      │    │            │         │           │
    GND ──────────┤pin 2 ├────┤ GND        │         │           │
                  │      │    │            │         │           │
                  └──────┘    │ D6 ────────┼─────────┤ /DENSITY  │
                              │ D7 ────────┼─────────┤ /MOTEA    │
                              │ A2 ────────┼─────────┤ /DRVSEL   │
     Ulkoinen 5V PSU          │ D9 ────────┼─────────┤ /MOTOR    │
     ┌──────────┐             │ D10 ───────┼─────────┤ /DIR      │
     │ +5V ─────┼──┬──────────┤ D11 ───────┼─────────┤ /STEP     │
     │          │  │   +5V    │ D12 ───────┼─────────┤ /WDATA    │
     │ GND ─────┼──┼──────────┤ D13 ───────┼─────────┤ /WGATE    │
     └──────────┘  │   GND   │            │         │           │
                   │          │ D8(ICP1)───┼─────────┤ /RDATA    │
                   │          │ A0 ────────┼─────────┤ /TRK00    │
                   │          │ A1 ────────┼─────────┤ /WPT      │
                   │          │ A3 ────────┼─────────┤ /SIDE1    │
                   │          │ A4 ────────┼─────────┤ /DSKCHG   │
                   │          │            │         │           │
                   │          │ GND ───────┼─────────┤ GND       │
                   │          │            │         │ +5V ──┐   │
                   │          │ A5 ──┤├──LED│         └───────┼───┘
                   │          │     330Ω   │                  │
                   │          └────────────┘                  │
                   │                                          │
                   └──────────── +5V ─────────────────────────┘

  Huom: Kaikki GND:t yhdistetään (C64, Nano, Floppy, PSU)
```

## Vaihtoehtoisten pinnien kytkentä (ICP1)

```
  Alkuperäinen vs. optimoitu pin-järjestys:

  Alkuperäinen:              Optimoitu (ICP1 käytössä):
  ─────────────              ──────────────────────────
  D8  = /DRVSEL              D8  = /RDATA  (ICP1!)
  A2  = /RDATA               A2  = /DRVSEL (siirretty)

  Tämä mahdollistaa hardware Input Capture -ajoituksen
  MFM-dekoodaukselle, mikä on huomattavasti tarkempi
  kuin ohjelmistopohjainen mittaus.
```

## Bypass-kondensaattorit

```
  Jokaisen IC:n VCC-GND väliin:

    +5V                        +5V
     │                          │
    ─┤├─ 100nF                 ─┤├─ 100nF
     │   (Arduino Nano          │   (lähellä floppy-
    GND   lähellä +5V/GND)     GND   liitintä)
```

## Komponenttilista (BOM)

```
  Nro  Komponentti            Paketti       Kpl   Huomio
  ───  ─────────────────────  ────────────  ───   ────────────────────────
   1   Arduino Nano (klooni)  DIP-moduuli    1   ATmega328P, 16MHz, 5V
   2   Vastus 1kΩ             1/4W           4   IEC-suojavastukset
   3   Vastus 4.7kΩ           1/4W           3   IEC pull-up (valinnainen)
   4   Vastus 330Ω            1/4W           1   LED-vastus
   5   Kond. 100nF            keraami.       2   Bypass
   6   Kond. 10µF             elektrol.      1   Bulk virta
   7   LED vihreä             3mm            1   Tilaindikaattori
   8   6-pin DIN liitin       DIN-6          1   IEC-väylä
   9   34-pin IDC header      2x17 pin       1   Floppy-liitäntä
  10   4-pin Berg liitin      Berg           1   Floppy-virta
  11   DC jack 5.5/2.1mm      panel mount    1   Virtalähde
  12   Pin headerit           2.54mm         -   Nanon kiinnitys
  ─────────────────────────────────────────────────────────────
  Yhteensä (ilman Nanoa ja floppy-asemaa):           ~5€
  Yhteensä (Nano-kloonilla):                         ~8-10€
```

## Vertailu: Nano vs. Pico (Vaihtoehto A)

```
  ┌──────────────────┬───────────────────┬───────────────────┐
  │ Ominaisuus       │ Arduino Nano      │ RPi Pico          │
  ├──────────────────┼───────────────────┼───────────────────┤
  │ Tasonmuuntimet   │ EI TARVITA (5V!)  │ 4x BSS138 + 8R   │
  │ Komponentteja    │ Vähemmän          │ Enemmän           │
  │ Hinta            │ ~8€               │ ~12€              │
  │ RAM              │ 2 KB (tiukka!)    │ 264 KB (riittävä) │
  │ MFM-dekoodaus    │ Timer1 ICP (OK)   │ PIO (erinomainen) │
  │ MFM-kirjoitus    │ Haastava          │ PIO (helppo)      │
  │ Dual-core        │ Ei                │ Kyllä             │
  │ Kehitysnopeus    │ Hitaampi          │ Nopeampi          │
  │ Kytkentä         │ Yksinkertaisempi  │ Monimutkaisempi   │
  └──────────────────┴───────────────────┴───────────────────┘

  Suositus:
  - Nano: jos haluat yksinkertaisimman mahdollisen kytkennän
    ja olet valmis painimaan RAM-rajoitusten kanssa
  - Pico: jos haluat luotettavamman ja helpommin kehitettävän
    ratkaisun (suositeltu)
```
