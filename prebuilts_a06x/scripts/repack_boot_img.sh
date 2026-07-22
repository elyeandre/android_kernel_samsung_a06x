#!/usr/bin/env bash
# Repack boot.img by taking the STOCK image and swapping ONLY the kernel.
#
# Why not build it from scratch:
#   The GKI build (BUILD_BOOT_IMG=1) calls mkbootimg with hand-written
#   arguments. That produced an image differing from stock in several ways at
#   once - no --ramdisk at all (GKI_RAMDISK_PREBUILT_BINARY was never set),
#   guessed --os_version/--os_patch_level/--pagesize, no --dtb, and an AVB
#   footer re-signed with a test key. The result does not boot: the kernel
#   loads but there is no ramdisk/init, so there is no panic and no boot
#   animation.
#
#   Instead, derive the EXACT mkbootimg arguments from the stock image
#   (unpack_bootimg.py --format mkbootimg), overwrite just the kernel, and
#   repack. Everything else - ramdisk, cmdline, load offsets, page size,
#   os_version, os_patch_level, dtb - is carried over verbatim. This is the
#   mkbootimg equivalent of a `magiskboot repack`.
#
# Usage: repack_boot_img.sh [kernel_Image] [output_boot.img]
#   defaults: dist/Image -> dist/boot.img, stock from stock_images/boot.img
#   override the stock image with STOCK_BOOT=/path/to/boot.img
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MKB="${REPO_ROOT}/prebuilts_a06x/mkbootimg"

STOCK_BOOT="${STOCK_BOOT:-${REPO_ROOT}/stock_images/boot.img}"
NEW_KERNEL="${1:-${REPO_ROOT}/dist/Image}"
OUT_IMG="${2:-${REPO_ROOT}/dist/boot.img}"
WORK="${REPO_ROOT}/out/boot_repack"

[ -f "$STOCK_BOOT" ] || {
    echo "[ERROR] stock boot.img not found: $STOCK_BOOT" >&2
    echo "        Dump it from the device or extract it from the firmware and" >&2
    echo "        place it at stock_images/boot.img (as with vendor_boot/vendor_dlkm)." >&2
    exit 1
}
[ -f "$NEW_KERNEL" ] || { echo "[ERROR] kernel Image not found: $NEW_KERNEL" >&2; exit 1; }

rm -rf "$WORK"; mkdir -p "$WORK/unpacked"

echo "[*] Unpacking stock boot.img and deriving its mkbootimg arguments..."
# --null keeps arguments NUL-separated so values containing spaces (notably
# --cmdline) survive intact; plain output is shell-quoted and would need eval.
python3 "${MKB}/unpack_bootimg.py" \
    --boot_img "$STOCK_BOOT" \
    --out "$WORK/unpacked" \
    --format mkbootimg --null > "$WORK/args.bin"

echo "    stock args: $(tr '\0' ' ' < "$WORK/args.bin")"
echo "    stock kernel : $(stat -c%s "$WORK/unpacked/kernel" 2>/dev/null || echo '?') bytes"
[ -f "$WORK/unpacked/ramdisk" ] &&
    echo "    stock ramdisk: $(stat -c%s "$WORK/unpacked/ramdisk") bytes (preserved)" ||
    echo "    stock ramdisk: none in this image"

echo "[*] Swapping in the freshly built kernel..."
cp "$NEW_KERNEL" "$WORK/unpacked/kernel"
echo "    new kernel   : $(stat -c%s "$WORK/unpacked/kernel") bytes"

echo "[*] Repacking with the stock arguments..."
mkdir -p "$(dirname "$OUT_IMG")"
xargs -0 python3 "${MKB}/mkbootimg.py" --output "$OUT_IMG" < "$WORK/args.bin"

echo "[*] Verifying the result..."
python3 "${MKB}/unpack_bootimg.py" --boot_img "$OUT_IMG" --out "$WORK/verify" \
    | grep -iE 'kernel_size|ramdisk size|header version|page size|os version|os patch'

echo "[OK] $OUT_IMG ($(du -h "$OUT_IMG" | cut -f1))"
