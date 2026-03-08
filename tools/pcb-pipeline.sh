#!/bin/bash
#
# PCB Pipeline: KiCad PCB → Freerouting → Gerbers → zip
#
# Käyttö:
#   ./tools/pcb-pipeline.sh hardware/E-IEC-Nano-SRAM/64korppu-E
#
# Parametri on PCB-tiedoston polku ILMAN .kicad_pcb -päätettä.
# Scripti tekee:
#   1. Validoi PCB:n (pad-overlap, board boundary)
#   2. Exportoi DSN (Specctra)
#   3. Ajaa Freerouting autorouting
#   4. Importoi SES takaisin KiCadiin
#   5. Täyttää GND-zonen
#   6. Ajaa DRC-tarkistuksen
#   7. Exportoi Gerberit + drill
#   8. Exportoi GenCAD (Eagle-yhteensopiva)
#   9. Pakkaa zip
#
set -euo pipefail

KICAD_PYTHON="/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3"
JAVA="/opt/homebrew/opt/openjdk/bin/java"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FREEROUTING="$SCRIPT_DIR/freerouting.jar"

# --- Värit ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${BLUE}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
fail()  { echo -e "${RED}[FAIL]${NC} $*"; exit 1; }

# --- Parametrit ---
if [ $# -lt 1 ]; then
    echo "Käyttö: $0 <pcb-base-path>"
    echo "  Esim: $0 hardware/E-IEC-Nano-SRAM/64korppu-E"
    exit 1
fi

BASE="$1"
PCB="${BASE}.kicad_pcb"
DSN="${BASE}.dsn"
SES="${BASE}.ses"
CAD="${BASE}.cad"
DIR="$(dirname "$BASE")"
NAME="$(basename "$BASE")"
GERBER_DIR="${DIR}/gerbers"
GERBER_ZIP="${DIR}/${NAME}-gerbers.zip"

[ -f "$PCB" ] || fail "PCB-tiedostoa ei löydy: $PCB"
[ -f "$KICAD_PYTHON" ] || fail "KiCad Pythonia ei löydy: $KICAD_PYTHON"
[ -f "$FREEROUTING" ] || fail "Freerouting.jar puuttuu: $FREEROUTING"

# --- 1. Validoi PCB ---
info "Validoidaan PCB..."
"$SCRIPT_DIR/pcb-validate.py" "$PCB" || fail "Validointi epäonnistui"
ok "Validointi OK"

# --- 2. Export DSN ---
info "Exportoidaan DSN..."
$KICAD_PYTHON -c "
import pcbnew
board = pcbnew.LoadBoard('$PCB')
pcbnew.ExportSpecctraDSN(board, '$DSN')
" 2>/dev/null
[ -f "$DSN" ] || fail "DSN export epäonnistui"
ok "DSN: $DSN"

# --- 3. Freerouting ---
info "Ajetaan Freerouting..."
$JAVA -jar "$FREEROUTING" -de "$DSN" -do "$SES" -mt 1 2>&1 | \
    grep -E "(Auto-routing was completed|unrouted|Saving)" || true
[ -f "$SES" ] || fail "Freerouting epäonnistui"
ok "SES: $SES"

# --- 4. Import SES + 5. Fill zones ---
info "Importoidaan reitit ja täytetään zonet..."
$KICAD_PYTHON -c "
import pcbnew
board = pcbnew.LoadBoard('$PCB')
pcbnew.ImportSpecctraSES(board, '$SES')
filler = pcbnew.ZONE_FILLER(board)
filler.Fill(board.Zones())
board.Save('$PCB')
print('OK: routes imported, zones filled')
" 2>/dev/null
ok "Reitit ja zonet OK"

# --- 6. DRC ---
info "Ajetaan DRC..."
DRC_OUT=$(kicad-cli pcb drc --severity-all "$PCB" 2>&1)
echo "$DRC_OUT"
VIOLATIONS=$(echo "$DRC_OUT" | grep -oP 'Found \K\d+(?= violations)' || echo "0")
UNCONNECTED=$(echo "$DRC_OUT" | grep -oP 'Found \K\d+(?= unconnected)' || echo "0")
if [ "$UNCONNECTED" -gt 5 ]; then
    warn "Paljon kytkemättömiä: $UNCONNECTED (>5 — tarkista!)"
fi

# --- 7. Gerber + drill ---
info "Exportoidaan Gerberit..."
mkdir -p "$GERBER_DIR"
kicad-cli pcb export gerbers \
    --layers F.Cu,B.Cu,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts \
    -o "$GERBER_DIR/" "$PCB"
kicad-cli pcb export drill -o "$GERBER_DIR/" "$PCB"
ok "Gerberit: $GERBER_DIR/"

# --- 8. GenCAD ---
info "Exportoidaan GenCAD..."
kicad-cli pcb export gencad -o "$CAD" "$PCB"
ok "GenCAD: $CAD"

# --- 9. Zip ---
info "Pakataan Gerber-zip..."
rm -f "$GERBER_ZIP"
(cd "$GERBER_DIR" && zip -q "../${NAME}-gerbers.zip" *)
ok "Zip: $GERBER_ZIP ($(du -h "$GERBER_ZIP" | cut -f1))"

# --- Yhteenveto ---
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN} PCB Pipeline valmis!${NC}"
echo -e "${GREEN}========================================${NC}"
echo "  PCB:     $PCB"
echo "  Gerbers: $GERBER_ZIP"
echo "  GenCAD:  $CAD"
echo "  DRC:     $VIOLATIONS violations, $UNCONNECTED unconnected"
echo ""
echo "Lataa $GERBER_ZIP palveluun:"
echo "  https://jlcpcb.com"
echo "  https://www.pcbway.com"
