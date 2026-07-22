#!/usr/bin/env bash
# check_kabi.sh — detect KMI/ABI breakage between kernel builds.
#
# Every exported symbol in Module.symvers carries a CRC derived from its type
# signature (CONFIG_MODVERSIONS=y). If the CRC of a symbol that stock vendor
# modules use changes, those modules fail to load with:
#     "<mod>: disagrees about version of symbol <sym>"
# ...which on this device means no display/touch/PMIC -> no boot animation.
#
# Usage:
#   check_kabi.sh --save     record the current build as the baseline
#   check_kabi.sh            compare the current build against the baseline
#
# Typical flow: --save on a known-good build, then run it after each change.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
KOBJ="${REPO_ROOT}/out/target/product/a06x/obj/KERNEL_OBJ"
SYMVERS="$(find "$KOBJ" -name Module.symvers -print -quit 2>/dev/null || true)"
BASELINE="${REPO_ROOT}/prebuilts_a06x/kabi_baseline.txt"

[ -n "$SYMVERS" ] || { echo "[ERROR] Module.symvers not found — build the kernel first." >&2; exit 1; }

# Which symbols matter: prefer the generated KMI whitelist, else the tree's list,
# else fall back to every exported symbol (noisier, but never misses a break).
KMI_LIST=""
for cand in "$KOBJ/kernel-5.15/abi_symbollist.raw" \
            "$KOBJ/abi_symbollist.raw" \
            "${REPO_ROOT}/kernel-5.15/android/abi_gki_aarch64"; do
    [ -f "$cand" ] && { KMI_LIST="$cand"; break; }
done

extract() {  # -> "symbol CRC" for KMI symbols only
    if [ -n "$KMI_LIST" ]; then
        awk 'NR==FNR { if ($0 !~ /^[[:space:]]*(#|\[|$)/) want[$1]=1; next }
             { sym=$2; if (sym in want) print sym, $1 }' \
            "$KMI_LIST" "$SYMVERS" | sort
    else
        awk '{ print $2, $1 }' "$SYMVERS" | sort
    fi
}

if [ "${1:-}" = "--save" ]; then
    extract > "$BASELINE"
    echo "[OK] baseline saved: $BASELINE ($(wc -l < "$BASELINE") symbols)"
    echo "     source: $SYMVERS"
    [ -n "$KMI_LIST" ] && echo "     KMI list: $KMI_LIST" || echo "     KMI list: (none — using ALL exported symbols)"
    exit 0
fi

[ -f "$BASELINE" ] || { echo "[ERROR] no baseline. Run: $0 --save (on a known-good build)" >&2; exit 1; }

CUR="$(mktemp)"; trap 'rm -f "$CUR"' EXIT
extract > "$CUR"

echo "[*] baseline: $(wc -l < "$BASELINE") symbols   current: $(wc -l < "$CUR") symbols"

# CRC changed on a symbol present in both = ABI break
CHANGED="$(join "$BASELINE" "$CUR" 2>/dev/null | awk '$2 != $3 { print "  " $1 "  " $2 " -> " $3 }' || true)"
# Symbol disappeared = also a break (module referencing it won't resolve)
GONE="$(comm -23 <(cut -d' ' -f1 "$BASELINE") <(cut -d' ' -f1 "$CUR") | sed 's/^/  /' || true)"
ADDED="$(comm -13 <(cut -d' ' -f1 "$BASELINE") <(cut -d' ' -f1 "$CUR") | wc -l)"

rc=0
if [ -n "$CHANGED" ]; then
    echo; echo "[BREAK] CRC changed on KMI symbols (vendor modules will refuse to load):"
    echo "$CHANGED"; rc=1
fi
if [ -n "$GONE" ]; then
    echo; echo "[BREAK] KMI symbols no longer exported:"
    echo "$GONE"; rc=1
fi
[ "$ADDED" -gt 0 ] && echo "[info] $ADDED newly exported symbol(s) — additions are ABI-safe."

if [ "$rc" -eq 0 ]; then
    echo "[OK] no KMI/ABI breakage detected."
else
    echo
    echo "Most common cause: a CONFIG you enabled adds an #ifdef'd field to a"
    echo "shared struct (task_struct, etc.), changing its layout. Check which"
    echo "struct the listed symbols take, and guard the field to keep layout."
fi
exit "$rc"
