# PCB-suunnittelun työnkulku (CLI)

Koko PCB-suunnitteluprosessi komentoriviltä ilman GUI:ta.

## Vaatimukset

- KiCad 9+ (`brew install --cask kicad`)
- Freerouting (`tools/freerouting.jar`) — https://github.com/freerouting/freerouting/releases
- OpenJDK (`brew install openjdk`)
- KiCadin Python: `/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3`

## Pikapolku: kaikki yhdellä komennolla

Kun `.kicad_pcb` on generoitu, **yksi komento** hoitaa kaiken:

```bash
./tools/pcb-pipeline.sh hardware/E-IEC-Nano-SRAM/64korppu-E
```

Pipeline tekee: validointi → DSN export → Freerouting → SES import → zone fill → DRC → Gerberit → GenCAD → zip.

## Vaiheet yksityiskohtaisesti

### 1. Claude Code generoi KiCad-projektitiedostot

Generoitavat tiedostot:
- `.kicad_pro` — projektiasetukset (JSON)
- `.kicad_sch` — piirikaavio (S-expression)
- `.kicad_pcb` — PCB-layout ilman reititystä (S-expression)
- Custom footprintit `.pretty/`-kansioon
- `fp-lib-table` — kirjastoviittaus

### 2. Validoi ja reititä

```bash
./tools/pcb-pipeline.sh hardware/<variant>/<name>
```

### 3. Kopioi tuotokset bin/-kansioon

```bash
cp hardware/<variant>/<name>-gerbers.zip bin/<variant>/
```

### 4. Tilaa

Lataa `<name>-gerbers.zip` palveluun:
- https://jlcpcb.com — halvin, 5 levyä ~$2 + toimitus
- https://www.pcbway.com — hyvä laatu

## Kriittiset säännöt PCB-generoinnissa

**Nämä tulee tarkistaa AINA ennen reititystä.** `pcb-validate.py` tarkistaa automaattisesti.

### 1. Ei `;;`-kommentteja
KiCad 9 hylkää S-expression-tiedostot joissa on `;;`-kommentteja.

### 2. Komponenttien minimietäisyydet
- **Eri komponenttien padien reunojen** välissä vähintään **0.3 mm**
- Erityisvaara: axial-vastukset (P10.16mm) ulottuvat 10.16mm origosta
- DIP-paketit: DIP-16 ulottuu 17.78mm pystysuunnassa, 7.62mm vaakasuunnassa
- 2x17 IDC header: ulottuu 40.64mm pystysuunnassa

### 3. Kaikki padit piirilevyn sisällä
- Jokaisen padin **absoluuttinen** sijainti (footprint origin + pad offset + rotaatio) tulee olla levyn rajojen sisällä
- Vähintään 0.5mm marginaali reunaan

### 4. Komponenttien sijoittelusäännöt
- **Älä aseta IC:itä suoraan toistensa päälle tai viereen** — jätä vähintään 5mm IC-pakettien väliin
- Arduino Nano (2x15, P2.54mm) ulottuu 35.56mm pystysuunnassa
- Sijoita 74HC595 ja 23LC256 **eri puolille** Nanoa, ei suoraan alle

### 5. Piirilevyn koko
- Prototyypille 100-120mm per sivu on hyvä
- Mounting holet M3 (3.2mm drill, 6mm pad) 3mm sisäänvedolla kulmista
- GND copper zone B.Cu-kerrokselle

## Työkalut

| Tiedosto | Kuvaus |
|----------|--------|
| `tools/pcb-pipeline.sh` | Koko pipeline yhdellä komennolla |
| `tools/pcb-validate.py` | Validoi PCB ennen reititystä |
| `tools/freerouting.jar` | Freerouting autorouter |

## Tuotetut tiedostot

```
hardware/<variant>/
├── <name>.kicad_pro          Projektiasetukset
├── <name>.kicad_sch          Piirikaavio
├── <name>.kicad_pcb          PCB-layout (+ tracet reitytyksen jälkeen)
├── <name>.dsn                Specctra DSN (väliaikainen)
├── <name>.ses                Specctra Session (väliaikainen)
├── <name>.cad                GenCAD (Eagle-yhteensopiva)
├── <name>-gerbers.zip        Valmis tilattavaksi
├── <name>.pretty/            Custom footprintit
├── fp-lib-table              Kirjastoviittaus
└── gerbers/                  Gerber + drill -tiedostot

bin/<variant>/
├── <name>-gerbers.zip        Gerberit (kopio)
└── <variant>.zip             Firmware-binääri
```

## DRC-tarkistus erikseen

```bash
kicad-cli pcb drc --severity-all hardware/<variant>/<name>.kicad_pcb
```

## GenCAD → Eagle

Eagle: **File → Import → GenCAD** ja valitse `.cad`-tiedosto.
