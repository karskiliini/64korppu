# 64korppu — CLAUDE.md

C64 + PC 1.44MB 3.5" HD floppy -yhdistäjä. Käyttäjä kommunikoi suomeksi. "puske" = git push.

## Projektirakenne

- `firmware/` — laiteohjelmat per vaihtoehto (A-E)
- `hardware/` — KiCad PCB-projektit (2-layer ja 4-layer variantit)
- `bin/` — valmiit binäärit, gerber-zipit
- `tools/` — PCB pipeline, validointi, Freerouting
- `test/` — C-yksikkötestit (`make test`)
- `docs/` — dokumentaatio per vaihtoehto

## PCB-generoinnin pakollinen prosessi

**Jokaisen PCB-muutoksen lopputulos on AINA: 0 kriittistä, 0 varoitusta, 0 unconnected.**

### Vaihe 1: Layout ja validointi

1. Generoi/muokkaa `.kicad_pcb` (KiCad Python API tai käsin)
2. Aja `python3 tools/pcb-validate.py <pcb-file>` — tarkistaa:
   - Ei `;;`-kommentteja (KiCad 9 hylkää ne)
   - Sulut tasapainossa
   - Kaikki padit levyn sisällä
   - Ei pad-päällekkäisyyksiä
   - **Komponenttien fyysiset rungot eivät törmää** (barrel jack, DIP, Nano, IDC jne.)
3. Korjaa kaikki validointivirheet ennen etenemistä

### Vaihe 2: Pipeline

```bash
./tools/pcb-pipeline.sh hardware/<variant>/<nimi>
# Parametri ILMAN .kicad_pcb -päätettä
```

Pipeline tekee: validoi → DSN export → Freerouting autoroute → SES import → zone fill → DRC → Gerber export → GenCAD → zip.

### Vaihe 3: DRC-iterointi — 0/0/0

Aja DRC erikseen kunnes puhdas:

```bash
python3 tools/pcb-drc-check.py <pcb-file> --max-unconnected 0
```

**Tyypilliset korjaukset iterointikierroksilla:**

| Ongelma | Korjaus |
|---------|---------|
| `silk_over_copper` (H1-H4) | Siirrä mounting hole -referenssit F.Fab-layerille |
| `silk_over_copper` (muu) | Siirrä ref-teksti kauemmas padista tai F.Fab-layerille |
| `copper_edge_clearance` (J1) | Hyväksytty poikkeus — DIN-konnektori levyn reunassa |
| `unconnected_items` | Lisää manuaalinen trace KiCad Python API:lla |
| `shorting_items` | Poista kaikki tracet, reititä Freeroutingilla uudelleen |
| `clearance` (J1↔J1) | Hyväksytty poikkeus — DIN-6 sisäiset padit |

**Manuaalinen trace KiCad Pythonilla:**

```python
import pcbnew
board = pcbnew.LoadBoard('file.kicad_pcb')
track = pcbnew.PCB_TRACK(board)
track.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(x1), pcbnew.FromMM(y1)))
track.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(x2), pcbnew.FromMM(y2)))
track.SetWidth(pcbnew.FromMM(0.25))
track.SetLayer(pcbnew.F_Cu)  # tai B_Cu
# Aseta net: board.GetNetInfo().GetNetItem("net_name") tai net_code
track.SetNet(board.GetNetInfo().GetNetItem(net_code))
board.Add(track)
board.Save('file.kicad_pcb')
```

**Referenssin siirto F.Fab-layerille:**

```python
for fp in board.GetFootprints():
    if fp.GetReference() in ('H1', 'H2', 'H3', 'H4'):
        fp.Reference().SetLayer(pcbnew.F_Fab)
```

### Vaihe 4: Lopputuotokset

1. Kopioi gerber-zip: `cp hardware/<variant>/<nimi>-gerbers.zip bin/<variant>/`
2. Commit ja push
3. Tuotetut tiedostot: `.kicad_pcb`, `.dsn`, `.ses`, `.cad`, `gerbers/`, `-gerbers.zip`

## Komponenttien mitat

| Komponentti | Mitat (mm) | Huomiot |
|-------------|-----------|---------|
| Arduino Nano 2x15 P2.54mm | 2.54 × 35.56 | Iso, sijoita keskelle |
| DIP-16 W7.62mm | 7.62 × 17.78 | 74HC595 |
| DIP-8 W7.62mm | 7.62 × 7.62 | 23LC1024 |
| R_Axial P10.16mm | 10.16 vaaka | Pad2 ulottuu 10mm origosta |
| IDC 2x17 P2.54mm | 2.54 × 40.64 | Erittäin pitkä pystysuunnassa |
| DIN-6 | ~15mm halk. | Shield-padit ulottuvat kauas keskipisteestä |

## Kriittiset opit — vältä nämä virheet

1. **Ei `;;`-kommentteja** `.kicad_pcb`-tiedostossa — KiCad 9 hylkää ne
2. **Tarkista aina komponenttien välit** — padit eivät saa olla < 0.5mm toisistaan
3. **Kaikki padit levyn sisällä** — validate.py tarkistaa tämän
4. **Axial-vastusten pad2** ulottuu 10.16mm origosta — huomioi viereisiin komponentteihin
5. **IDC 2x17 header** on 40.64mm pitkä — varaa tilaa
6. **DIN-6 shield-padit** ulottuvat kauas — tarkista etäisyys naapureihin
7. **Vanhat routet aiheuttavat oikosulkuja** — jos layout muuttuu, poista kaikki tracet ja reititä uudelleen
8. **SWIG memory leak -varoitukset** KiCad Python API:sta ovat harmittomia — voi jättää huomiotta
9. **`grep -P` ei toimi macOS:ssä** — käytä `sed` tai `grep -E`
10. **4-layer stackup**: F.Cu (signal) / In1.Cu (GND plane) / In2.Cu (+5V plane) / B.Cu (signal)
11. **2-layer**: F.Cu (signal) / B.Cu (GND pour + signal)

## Hyväksytyt DRC-poikkeukset (pcb-drc-check.py)

Nämä ovat tunnettuja, hyväksyttyjä ongelmia jotka eivät estä valmistusta:

- `clearance` J1↔J1 — DIN-6 konnektorin sisäiset padit ovat lähellä toisiaan (suunniteltu näin)
- `copper_edge_clearance` H1-H4 — kiinnitysreiät levyn kulmissa (standardi)
- `copper_edge_clearance` J1 — DIN-konnektori sijoitettu levyn reunaan (tarkoituksellista)

## Polut (macOS)

- KiCad Python: `/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3`
- Java (Freerouting): `/opt/homebrew/opt/openjdk/bin/java`
- Freerouting JAR: `tools/freerouting.jar`

## Layer-tunnukset KiCad 9 Python API:ssa

- `pcbnew.F_Cu` = 0 (Front Copper)
- `pcbnew.B_Cu` = 2 (Back Copper)
- `pcbnew.In1_Cu` = 4 (Inner 1)
- `pcbnew.In2_Cu` = 6 (Inner 2)
- `pcbnew.Edge_Cuts` = 25
- `pcbnew.F_Fab` = 35 (Front Fabrication — ei näy fyysisessä levyssä)
- `pcbnew.F_SilkS` = silkscreen (näkyy levyssä)
