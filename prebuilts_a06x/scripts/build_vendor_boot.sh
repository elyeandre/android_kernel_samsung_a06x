#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

LKM_TOOLS_DIR="${REPO_ROOT}/prebuilts_a06x/LKM_Tools"
KBUILD_PATH="${REPO_ROOT}/out/target/product/a06x/obj/KERNEL_OBJ"

STAGING_DIR="${KBUILD_PATH}/staging"
SYSTEM_MAP="${KBUILD_PATH}/kernel-5.15/System.map"
STRIP_TOOL="${REPO_ROOT}/kernel/prebuilts/clang/host/linux-x86/clang-r450784e/bin/llvm-strip"
VENDOR_BOOT_MODULES_LIST="${LKM_TOOLS_DIR}/vendor_boot/modules_list.txt"
VENDOR_BOOT_MODULES_LOAD="${LKM_TOOLS_DIR}/vendor_boot/modules.load"
STOCK_IMG="${REPO_ROOT}/stock_images/vendor_boot.img"
OUTPUT_IMG="${REPO_ROOT}/dist/vendor_boot.img"

UNPACK_BOOTIMG="${REPO_ROOT}/prebuilts_a06x/mkbootimg/unpack_bootimg.py"
MKBOOTIMG="${REPO_ROOT}/prebuilts_a06x/mkbootimg/mkbootimg.py"

WORK_DIR="${REPO_ROOT}/out/vendor_boot_work"
RAMDISK_DIR="${WORK_DIR}/ramdisk"
MODULES_OUTPUT_DIR="${RAMDISK_DIR}/lib/modules"

unpack_stock() {
    rm -rf "${WORK_DIR}"
    mkdir -p "${WORK_DIR}/unpacked" "${RAMDISK_DIR}"
    python3 "${UNPACK_BOOTIMG}" \
        --boot_img "${STOCK_IMG}" \
        --out "${WORK_DIR}/unpacked" \
        --format mkbootimg > "${WORK_DIR}/mkbootimg_args.txt"
    lz4 -d "${WORK_DIR}/unpacked/vendor_ramdisk00" "${WORK_DIR}/ramdisk.cpio"
    cd "${RAMDISK_DIR}" && cpio -idm < "${WORK_DIR}/ramdisk.cpio"
    cd "${REPO_ROOT}"
}

package_modules() {
    mkdir -p "${MODULES_OUTPUT_DIR}"
    "${LKM_TOOLS_DIR}/02.prepare_vendor_boot_modules.sh" \
        "${VENDOR_BOOT_MODULES_LIST}" \
        "${STAGING_DIR}" \
        "${VENDOR_BOOT_MODULES_LOAD}" \
        "${SYSTEM_MAP}" \
        "${STRIP_TOOL}" \
        "${MODULES_OUTPUT_DIR}"
}

repack_image() {
    cd "${RAMDISK_DIR}"
    find . | cpio -H newc -o > "${WORK_DIR}/ramdisk_new.cpio" 2>/dev/null
    lz4 -l -9 "${WORK_DIR}/ramdisk_new.cpio" "${WORK_DIR}/vendor_ramdisk00_new"
    cd "${REPO_ROOT}"

    sed -i "s|${WORK_DIR}/unpacked/vendor_ramdisk00|${WORK_DIR}/vendor_ramdisk00_new|g" \
        "${WORK_DIR}/mkbootimg_args.txt"

    mkdir -p "$(dirname "${OUTPUT_IMG}")"
    eval python3 "${MKBOOTIMG}" \
        --vendor_boot "${OUTPUT_IMG}" \
        "$(cat "${WORK_DIR}/mkbootimg_args.txt")"
    echo "[INFO] vendor_boot.img -> ${OUTPUT_IMG} ($(du -h "${OUTPUT_IMG}" | cut -f1))"
}

{ unpack_stock && package_modules && repack_image; } || {
    echo "[ERROR] Failed to build vendor_boot"
    exit 1
}
