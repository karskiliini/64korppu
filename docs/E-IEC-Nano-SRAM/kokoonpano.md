# 64korppu E — Kokoonpano-ohje (2-layer)

## Tarvikkeet

- Juotoskolvi (~350 °C)
- Lyijyllinen tinajuote (Sn60/Pb40, ø 0.5–1.0 mm)
- Sivuleikkurit
- Kolmas käsi tai PCB-pidike
- IC-kannat: DIP-8 (U2) ja DIP-16 (U3)
- Pin headerit: 2 × 15 female (U1 Nano)

## Komponenttilista

| Ref | Komponentti | Kotelo | Huom |
|-----|------------|--------|------|
| U1 | Arduino Nano | 2×15 pin header | Female headerit levylle |
| U2 | 23LC512 / 23LCV512 | DIP-8 | SPI SRAM 64KB |
| U3 | 74HC595 | DIP-16 | Shift register |
| J1 | DIN-6 liitin | Pystysuora | IEC-väylä (C64) |
| J2 | IDC 2×17 header | Pystysuora | Floppy-kaapeli |
| J3 | Barrel jack | 5.5/2.1 mm | Virtaliitin |
| R1–R7 | Vastukset | Axial | IEC-väylän vastukset |
| R8 | Vastus | Axial | LED-virranrajoitin |
| R9–R12 | Vastukset | Axial | Floppy pull-up |
| C1–C3 | Keraaminen kondensaattori | Disc 100 nF | Bypass-kondikat |
| C4 | Elektrolyyttikondensaattori | Radial | Syöttösuodatus |
| D1 | LED | 3 mm / 5 mm | Tila-LED |
| H1–H4 | Kiinnitysruuvit | M3 | Valinnainen |

## Juottamisen periaate

Kaikki komponentit ovat through-hole (THT):

1. **Työnnä** komponentin jalat levyn etupuolelta (silkscreen-teksteillä merkitty puoli) reikien läpi
2. **Käännä** levy ympäri
3. **Juota** jalat takapuolelta (B.Cu)
4. **Leikkaa** ylimääräiset jalat sivuleikkurilla

Kaikki juotetaan samalta puolelta — ei tarvitse juottaa mitään etupuolella.

## Juotusjärjestys

Juota matalimmasta korkeimpaan, jotta komponentit pysyvät paikallaan kun levy on ylösalaisin:

### 1. Vastusten R1–R12

- Taivuta jalat 90° kohdasta jossa ne menevät runkoon
- Työnnä reikiin, taivuta jalat hieman levyn alla jotta pysyy paikallaan
- Juota, leikkaa ylimääräiset jalat

### 2. IC-kannat (U2, U3)

- **Älä juota siruja suoraan** — käytä IC-kantoja
- Tarkista kannan lovi vastaa silkscreen-merkintää (pin 1)
- Juota ensin kaksi vastakkaista kulmaa, tarkista suoruus, juota loput

### 3. Keraamiskondensaattorit C1–C3

- Ei napaisuutta, menee kummin päin tahansa

### 4. LED D1

- **Napainen!** Pidempi jalka = anodi (+), lyhyempi = katodi (−)
- Tarkista silkscreen-merkintä levyltä

### 5. Elektrolyyttikondensaattori C4

- **Napainen!** Raita kotelossa = miinusjalka (−)
- Tarkista silkscreen-merkintä

### 6. Liittimet J1, J2, J3

- **J1 (DIN-6):** IEC-liitin, shield-jalat menevät myös reikiin
- **J2 (IDC 2×17):** Pitkä header, tarkista suunta (lovi osoittaa oikein)
- **J3 (Barrel jack):** Isot padit, tarvitsee enemmän lämpöä

### 7. Arduino Nano pin headerit

- Juota 2 × 15 pin female headerit levylle
- Nano painetaan paikoilleen headereihin (irrotettava)

### 8. IC-piirit kantaan

- Aseta U2 (23LC512) ja U3 (74HC595) kantoihinsa
- Tarkista pin 1 -merkintä (lovi) ennen painamista

## Juotosvinkit

- **Kolvi padiin ja jalkaan yhtä aikaa ~2 s** → syötä tinaa → irrota kolvi
- Hyvä juotos on kiiltävä ja kartion muotoinen (ei pallon muotoinen)
- Kylmäjuotos (mattapintainen, rosoinen) → lämmitä uudelleen ja lisää tinaa
- Tina-silta kahden padin välillä → poista juotospumpulla tai juotosnauhalla

## Reset-nappi (valinnainen)

Arduino Nanon voi varustaa ulkoisella reset-napilla:

1. Kytke hetkellinen painonappi (normally open) Nanon **RST**-pinnin ja **GND**-pinnin väliin
2. Nanossa on sisäinen pull-up-vastus RST-pinnissä, joten ulkoista pull-upia ei tarvita
3. Napin painaminen vetää RST-pinnin maahan → Nano resetoituu

Nappi ei ole PCB:llä — se kytketään hyppylangalla suoraan Nanon headereihin.

> **Vinkki:** 100 nF keraaminen kondensaattori napin yli (RST–GND) vähentää mekaanista värähtelyä (debounce), mutta ei ole välttämätön.

## Test pointit (TP1–TP14)

Levyn takapuolella (B.Cu) on 14 testipistettä. Näitä **ei juoteta** — ne ovat mittauspisteitä joihin voi koskea yleismittarin koetinkärjellä vianetsinnässä. Katso [diagnostiikkapadit.md](diagnostiikkapadit.md) lisätietoja.
