#!/bin/bash
# Build the flashable MT76x2U KernelSU/Magisk module zip:
#   1. copy the freshly-built mt76 stack .ko from the kernel build tree
#   2. fetch the MT7612U firmware from linux-firmware (placed flat)
#   3. zip with module.prop at the archive root
#
# Usage: ksu_module/build_module_zip.sh [output.zip]
# Default output: dist/mt76x2u-ksu-module.zip
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMPLATE="$SCRIPT_DIR/mt76x2u-wifi"
KBUILD="${REPO_ROOT}/out/target/product/a06x/obj/KERNEL_OBJ"
OUT_ZIP="${1:-${REPO_ROOT}/dist/mt76x2u-ksu-module.zip}"

# Prefer signed modules from the modules_install staging tree, fall back to the
# raw object tree. (Signing is not enforced on this kernel, but prefer signed.)
SEARCH_DIRS=("${KBUILD}/staging" "${KBUILD}")

# mt76 stack in dependency order (see mt76x2u-wifi/service.sh)
KOS=(cfg80211 mac80211 mt76 mt76-usb mt76x02-lib mt76x02-usb mt76x2-common mt76x2u)

# linux-firmware: files live under mediatek/ but the driver opens the bare
# names, so download from mediatek/ and store flat. The USB-specific mt7662u.*
# variants are the ones verified working on the MT7612U (CF-WU785AC), so fall
# back to them (saved under the requested non-u name) if the plain file is gone.
FW_BASE="https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/mediatek"
FW_FILES=(mt7662.bin mt7662_rom_patch.bin)
declare -A FW_ALT=([mt7662.bin]=mt7662u.bin [mt7662_rom_patch.bin]=mt7662u_rom_patch.bin)

find_ko() {
    local ko="$1" d src
    for d in "${SEARCH_DIRS[@]}"; do
        [ -d "$d" ] || continue
        src="$(find "$d" -name "${ko}.ko" -print -quit 2>/dev/null || true)"
        [ -n "$src" ] && { printf '%s\n' "$src"; return 0; }
    done
    return 1
}

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -a "$TEMPLATE/." "$STAGE/"
rm -f "$STAGE"/modules/.gitkeep "$STAGE"/firmware/.gitkeep
mkdir -p "$STAGE/modules" "$STAGE/firmware"

echo "[*] Collecting kernel modules from build tree..."
found=0
for ko in "${KOS[@]}"; do
    if src="$(find_ko "$ko")"; then
        cp "$src" "$STAGE/modules/${ko}.ko"
        echo "    + ${ko}.ko"
        found=$((found + 1))
    else
        echo "    - ${ko}.ko NOT FOUND"
    fi
done
if [ "$found" -eq 0 ]; then
    echo "[ERROR] No mt76 modules found — did the build run with CONFIG_MT76x2U=m?" >&2
    exit 1
fi
if [ ! -f "$STAGE/modules/mt76x2u.ko" ]; then
    echo "[ERROR] mt76x2u.ko missing — the driver itself was not built." >&2
    exit 1
fi

echo "[*] Fetching MT7612U firmware from linux-firmware..."
for fw in "${FW_FILES[@]}"; do
    dst="$STAGE/firmware/${fw}"
    if curl -fsSL --retry 3 --retry-delay 2 "${FW_BASE}/${fw}" -o "$dst"; then
        echo "    + firmware/${fw} ($(stat -c%s "$dst" 2>/dev/null || echo '?') bytes)"
    elif curl -fsSL --retry 3 --retry-delay 2 "${FW_BASE}/${FW_ALT[$fw]}" -o "$dst"; then
        echo "    + firmware/${fw} (from ${FW_ALT[$fw]}, $(stat -c%s "$dst" 2>/dev/null || echo '?') bytes)"
    else
        echo "    ! could not fetch ${fw} or ${FW_ALT[$fw]} — shipping without it" >&2
        rm -f "$dst"
    fi
done

echo "[*] Zipping module (module.prop at root)..."
mkdir -p "$(dirname "$OUT_ZIP")"
rm -f "$OUT_ZIP"
( cd "$STAGE" && zip -qr "$OUT_ZIP" . -x '*.gitkeep' )
echo "[OK] $OUT_ZIP"
echo "     modules: $found   firmware: $(ls -1 "$STAGE"/firmware/*.bin 2>/dev/null | wc -l)"
