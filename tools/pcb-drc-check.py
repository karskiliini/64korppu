#!/usr/bin/env python3
"""
Post-routing DRC analyzer for KiCad PCB files.

Parses kicad-cli DRC JSON output, classifies violations by severity,
and returns non-zero exit code for critical issues.

Severity classification:
  CRITICAL — stops pipeline, must fix before manufacturing
  WARNING  — should review, but board likely works
  INFO     — cosmetic or expected issues

Usage:
  ./tools/pcb-drc-check.py <pcb-file> [--max-unconnected N]
"""
import json
import subprocess
import sys
import tempfile
import os

# --- Violation classification ---

# CRITICAL: these indicate real electrical or manufacturing problems
CRITICAL_TYPES = {
    'unconnected_items',    # broken net — circuit won't work
    'short',                # short circuit between nets
    'shorting_items',       # trace shorts two different nets
    'duplicate_footprints', # two components with same ref
}

# WARNING: real issues but may be acceptable for prototypes
WARNING_TYPES = {
    'clearance',            # trace/pad too close to another
    'copper_edge_clearance', # copper too close to board edge
    'hole_clearance',       # trace too close to drill hole
    'track_width',          # trace narrower than minimum
    'track_dangling',       # trace with unconnected end
    'via_diameter',         # via smaller than minimum
    'annular_width',        # annular ring too small
    'drill_out_of_range',   # drill size outside fab limits
    'solder_mask_bridge',   # solder mask bridges different nets
    'silk_over_copper',     # silkscreen on exposed copper
}

# INFO: cosmetic or expected (internal footprint issues, lib warnings)
INFO_TYPES = {
    'starved_thermal',      # thermal relief has fewer spokes
    'lib_footprint_issues', # library not found (path issue)
    'lib_footprint_mismatch', # footprint differs from library
    'courtyard_overlap',    # courtyard intersections
    'missing_courtyard',    # component without courtyard
    'extra_footprint',      # footprint without schematic symbol
    'missing_footprint',    # schematic symbol without footprint
}

# Known acceptable violations (specific patterns to skip)
ACCEPTABLE_PATTERNS = [
    # DIN-6 internal pad clearances (inherent to connector design)
    ('clearance', 'J1', 'J1'),
    # Mounting holes touching board edge (standard practice)
    ('copper_edge_clearance', 'H1', None),
    ('copper_edge_clearance', 'H2', None),
    ('copper_edge_clearance', 'H3', None),
    ('copper_edge_clearance', 'H4', None),
    # DIN-6 shield pad touching board edge (connector placed at edge)
    ('copper_edge_clearance', 'J1', None),
]


def get_refs_from_items(items):
    """Extract component references from violation items."""
    refs = []
    for item in items:
        desc = item.get('description', '')
        # "PTH pad 3 [IEC_ATN] of J1" → "J1"
        if ' of ' in desc:
            ref = desc.split(' of ')[-1]
            refs.append(ref)
        elif desc.startswith('Zone '):
            refs.append(None)
        elif 'Edge.Cuts' in desc or 'Rectangle' in desc:
            refs.append(None)
        else:
            refs.append(None)
    return refs


def is_acceptable(violation):
    """Check if violation matches a known acceptable pattern."""
    vtype = violation.get('type', '')
    refs = get_refs_from_items(violation.get('items', []))

    for pattern_type, ref1, ref2 in ACCEPTABLE_PATTERNS:
        if vtype != pattern_type:
            continue
        if ref2 is None:
            # Match if any ref matches ref1
            if ref1 in refs:
                return True
        else:
            # Match if both refs present
            if ref1 in refs and ref2 in refs:
                return True
    return False


def classify_violation(violation):
    """Classify a violation as CRITICAL, WARNING, or INFO."""
    vtype = violation.get('type', '')

    if is_acceptable(violation):
        return 'ACCEPTED'

    if vtype in CRITICAL_TYPES:
        return 'CRITICAL'
    elif vtype in WARNING_TYPES:
        return 'WARNING'
    elif vtype in INFO_TYPES:
        return 'INFO'
    else:
        # Unknown type → treat as WARNING
        return 'WARNING'


def format_violation(v, show_detail=True):
    """Format a single violation for display."""
    desc = v.get('description', 'Unknown')
    items = v.get('items', [])
    vtype = v.get('type', '?')

    line = f"  [{vtype}] {desc}"
    if show_detail and items:
        for item in items:
            pos = item.get('pos', {})
            x, y = pos.get('x', 0), pos.get('y', 0)
            line += f"\n    → {item.get('description', '?')} at ({x:.1f}, {y:.1f})"
    return line


def run_drc(pcb_path):
    """Run KiCad DRC and return parsed JSON."""
    with tempfile.NamedTemporaryFile(suffix='.json', delete=False) as f:
        json_path = f.name

    try:
        result = subprocess.run(
            ['kicad-cli', 'pcb', 'drc',
             '--format', 'json',
             '--severity-all',
             '--all-track-errors',
             '--output', json_path,
             pcb_path],
            capture_output=True, text=True
        )
        # kicad-cli prints summary to stdout
        for line in result.stdout.strip().split('\n'):
            if line.strip():
                print(f"  kicad-cli: {line.strip()}")

        with open(json_path) as f:
            return json.load(f)
    finally:
        os.unlink(json_path)


def main():
    if len(sys.argv) < 2:
        print(f"Käyttö: {sys.argv[0]} <pcb-tiedosto> [--max-unconnected N]")
        sys.exit(1)

    pcb_path = sys.argv[1]
    max_unconnected = 0  # default: no unconnected allowed

    for i, arg in enumerate(sys.argv):
        if arg == '--max-unconnected' and i + 1 < len(sys.argv):
            max_unconnected = int(sys.argv[i + 1])

    # --- Run DRC ---
    print(f"DRC: {pcb_path}")
    data = run_drc(pcb_path)

    # --- Collect all violations ---
    all_violations = data.get('violations', [])
    unconnected = data.get('unconnected_items', [])

    # Add unconnected as violations too
    for u in unconnected:
        u['type'] = 'unconnected_items'
        all_violations.append(u)

    # --- Classify ---
    classified = {'CRITICAL': [], 'WARNING': [], 'INFO': [], 'ACCEPTED': []}
    for v in all_violations:
        severity = classify_violation(v)
        classified[severity].append(v)

    # --- Report ---
    print()

    # Critical
    if classified['CRITICAL']:
        print(f"\033[0;31m{'='*50}")
        print(f" KRIITTISET VIRHEET: {len(classified['CRITICAL'])}")
        print(f"{'='*50}\033[0m")
        for v in classified['CRITICAL']:
            print(format_violation(v))
        print()

    # Warnings
    if classified['WARNING']:
        print(f"\033[1;33m{'='*50}")
        print(f" VAROITUKSET: {len(classified['WARNING'])}")
        print(f"{'='*50}\033[0m")
        # Group by type
        by_type = {}
        for v in classified['WARNING']:
            vtype = v.get('type', '?')
            by_type.setdefault(vtype, []).append(v)

        for vtype, violations in sorted(by_type.items()):
            print(f"\n  {vtype} ({len(violations)} kpl):")
            for v in violations[:5]:
                print(format_violation(v, show_detail=True))
            if len(violations) > 5:
                print(f"    ... ja {len(violations) - 5} muuta")

        print()

    # Info
    if classified['INFO']:
        print(f"\033[0;34mINFO: {len(classified['INFO'])} huomautusta\033[0m", end="")
        by_type = {}
        for v in classified['INFO']:
            vtype = v.get('type', '?')
            by_type.setdefault(vtype, []).append(v)
        details = [f"{t}: {len(vs)}" for t, vs in sorted(by_type.items())]
        print(f" ({', '.join(details)})")

    # Accepted
    if classified['ACCEPTED']:
        print(f"\033[0;32mHYVÄKSYTTY: {len(classified['ACCEPTED'])} tunnettua ongelmaa\033[0m", end="")
        by_type = {}
        for v in classified['ACCEPTED']:
            vtype = v.get('type', '?')
            by_type.setdefault(vtype, []).append(v)
        details = [f"{t}: {len(vs)}" for t, vs in sorted(by_type.items())]
        print(f" ({', '.join(details)})")

    # --- Summary ---
    print()
    n_crit = len(classified['CRITICAL'])
    n_warn = len(classified['WARNING'])
    n_info = len(classified['INFO'])
    n_acc = len(classified['ACCEPTED'])

    # Check unconnected against threshold
    n_unconnected = len(unconnected)
    unconnected_ok = n_unconnected <= max_unconnected

    # Check for real critical issues (excluding allowed unconnected)
    real_critical = [v for v in classified['CRITICAL']
                     if v.get('type') != 'unconnected_items']
    unconnected_over = max(0, n_unconnected - max_unconnected)

    if real_critical or not unconnected_ok:
        print(f"\033[0;31m{'='*50}")
        print(f" DRC HYLÄTTY")
        print(f"{'='*50}\033[0m")
        if real_critical:
            print(f"  {len(real_critical)} kriittistä virhettä (ei unconnected)")
        if not unconnected_ok:
            print(f"  {n_unconnected} kytkemätöntä (max {max_unconnected})")
        print(f"  Korjaa virheet ennen valmistusta!")
        sys.exit(1)
    else:
        print(f"\033[0;32m{'='*50}")
        print(f" DRC HYVÄKSYTTY")
        print(f"{'='*50}\033[0m")
        print(f"  Kriittisiä: {n_crit} (unconnected: {n_unconnected}/{max_unconnected})")
        print(f"  Varoituksia: {n_warn}")
        print(f"  Huomautuksia: {n_info}")
        print(f"  Hyväksytty: {n_acc}")
        sys.exit(0)


if __name__ == '__main__':
    main()
