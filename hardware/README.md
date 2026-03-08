# Hardware — KiCad-piirilevyt

## Hakemistorakenne

```
hardware/
└── E-IEC-Nano-SRAM/
    ├── 64korppu-E.kicad_pro        # KiCad-projekti
    ├── 64korppu-E.kicad_sch        # Piirikaavio
    ├── 64korppu-E.kicad_pcb        # Piirilevylayout
    ├── fp-lib-table                # Footprint-kirjaston asetukset
    └── 64korppu-E.pretty/          # Projektikohtaiset footprintit
        └── DIN-6_IEC_Vertical.kicad_mod
```

## Vaatimukset

- **KiCad 8** (https://www.kicad.org/)

## Projektin avaaminen

1. Avaa KiCad
2. **File → Open Project**
3. Valitse `hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pro`

Piirikaavio avautuu Schematic Editorilla, piirilevylayout PCB Editorilla.

## Automaattireititys Freeroutingilla

Freerouting reitittää piirilevyn vedot automaattisesti KiCadin tuottamasta netlististä.

1. **KiCad PCB Editor:** File → Export → Specctra DSN — tallenna `.dsn`-tiedosto
2. Lataa Freerouting: https://github.com/freerouting/freerouting
3. Käynnistä:
   ```bash
   java -jar freerouting.jar
   ```
4. Avaa `.dsn`-tiedosto ja klikkaa **Autoroute**
5. Kun reititys valmis: **File → Export Specctra Session** (`.ses`)
6. **KiCad PCB Editor:** File → Import → Specctra Session — valitse `.ses`-tiedosto

Tarkista tulos DRC:llä (Inspect → Design Rules Checker) ennen Gerber-vientiä.

## Gerber-vienti (JLCPCB / PCBWay)

### Gerber-tiedostot

1. **KiCad PCB Editor:** File → Fabrication Outputs → Gerbers
2. Varmista että seuraavat tasot ovat valittuna:
   - `F.Cu` — etupuolen kupari
   - `B.Cu` — takapuolen kupari
   - `F.SilkS` — etupuolen silkkipaino
   - `B.SilkS` — takapuolen silkkipaino
   - `F.Mask` — etupuolen juotosmaskimaski
   - `B.Mask` — takapuolen juotosmaskimaski
   - `Edge.Cuts` — levyn ääriviivat
3. Klikkaa **Plot**

### Poraustiedostot

1. File → Fabrication Outputs → Drill Files
2. Käytä oletusasetuksia (Excellon-formaatti)
3. Klikkaa **Generate Drill File**

### Tilaus

1. Pakkaa kaikki Gerber- ja poraustiedostot yhteen `.zip`-tiedostoon
2. Lataa osoitteeseen [JLCPCB](https://jlcpcb.com/) tai [PCBWay](https://pcbway.com/)
3. Tarkista esikatselu ennen tilausta
