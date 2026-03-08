# PCB-suunnittelun työnkulku (CLI)

Koko PCB-suunnitteluprosessi komentoriviltä ilman GUI:ta.

## Vaatimukset

- KiCad 9+ (`brew install --cask kicad`)
- Freerouting (`java -jar freerouting.jar`) — https://github.com/freerouting/freerouting/releases
- KiCadin Python: `/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3`

## Vaiheet

### 1. Generoi KiCad-projektitiedostot

Claude Code generoi:
- `.kicad_pro` — projektiasetuks (JSON)
- `.kicad_sch` — piirikaavio (S-expression)
- `.kicad_pcb` — PCB-layout ilman reititystä (S-expression)
- Custom footprintit `.pretty/`-kansioon
- `fp-lib-table` — kirjastoviittaus

**Huom:** PCB-tiedostossa ei saa olla `;;`-kommentteja — KiCad hylkää ne.

### 2. Export DSN (Specctra Design)

```bash
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 -c "
import pcbnew
board = pcbnew.LoadBoard('hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb')
pcbnew.ExportSpecctraDSN(board, 'hardware/E-IEC-Nano-SRAM/64korppu-E.dsn')
print('DSN exported')
"
```

### 3. Automaattireititys Freeroutingilla

```bash
java -jar freerouting.jar -de hardware/E-IEC-Nano-SRAM/64korppu-E.dsn \
                          -do hardware/E-IEC-Nano-SRAM/64korppu-E.ses
```

### 4. Import SES (reititetyt tracet takaisin KiCadiin)

```bash
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 -c "
import pcbnew
board = pcbnew.LoadBoard('hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb')
pcbnew.ImportSpecctraSES(board, 'hardware/E-IEC-Nano-SRAM/64korppu-E.ses')
board.Save('hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb')
print('SES imported, PCB saved with tracks')
"
```

### 5. Gerber-export tilausta varten

```bash
mkdir -p hardware/E-IEC-Nano-SRAM/gerbers

# Kuparikerrokset, silkscreen, mask, reunat
kicad-cli pcb export gerbers \
  --layers F.Cu,B.Cu,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts \
  -o hardware/E-IEC-Nano-SRAM/gerbers/ \
  hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb

# Porausreikätiedostot
kicad-cli pcb export drill \
  -o hardware/E-IEC-Nano-SRAM/gerbers/ \
  hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb

# Pakkaa zip JLCPCB:lle / PCBWaylle
cd hardware/E-IEC-Nano-SRAM/gerbers && zip ../64korppu-E-gerbers.zip * && cd -
```

### 6. Tilaus

Lataa `64korppu-E-gerbers.zip` palveluun:
- https://jlcpcb.com — halvin, 5 levyä ~$2 + toimitus
- https://www.pcbway.com — hyvä laatu

## DRC-tarkistus (valinnainen)

```bash
kicad-cli pcb drc \
  --severity-all \
  --exit-code-violations \
  hardware/E-IEC-Nano-SRAM/64korppu-E.kicad_pcb
```

## Tiedostot

```
hardware/E-IEC-Nano-SRAM/
├── 64korppu-E.kicad_pro          Projektiasetuks
├── 64korppu-E.kicad_sch          Piirikaavio
├── 64korppu-E.kicad_pcb          PCB-layout (+ tracet importin jälkeen)
├── 64korppu-E.dsn                Specctra DSN (väliaikainen)
├── 64korppu-E.ses                Specctra Session (väliaikainen)
├── 64korppu-E-gerbers.zip        Valmis tilattavaksi
├── 64korppu-E.pretty/            Custom footprintit
│   └── DIN-6_IEC_Vertical.kicad_mod
├── fp-lib-table                  Kirjastoviittaus
└── gerbers/                      Gerber + drill -tiedostot
```
