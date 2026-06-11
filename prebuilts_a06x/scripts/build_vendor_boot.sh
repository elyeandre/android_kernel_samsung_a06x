#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

LKM_TOOLS_DIR="${REPO_ROOT}/prebuilts_a06x/LKM_Tools"
KBUILD_PATH="${REPO_ROOT}/out/target/product/a06x/obj/KERNEL_OBJ"
MKBOOTIMG="${REPO_ROOT}/prebuilts_a06x/mkbootimg/mkbootimg.py"

# Pre-committed vendor_boot ramdisk state (root.1/ skeleton + dtb)
VENDOR_BOOT_PREBUILT="${REPO_ROOT}/prebuilts_a06x/vendor_boot/unzip_boot"

STAGING_DIR="${KBUILD_PATH}/staging"
SYSTEM_MAP="${KBUILD_PATH}/kernel-5.15/System.map"
STRIP_TOOL="${REPO_ROOT}/kernel/prebuilts/clang/host/linux-x86/clang-r450784e/bin/llvm-strip"
VENDOR_BOOT_MODULES_LIST="${LKM_TOOLS_DIR}/vendor_boot/modules_list.txt"
VENDOR_BOOT_MODULES_LOAD="${LKM_TOOLS_DIR}/vendor_boot/modules.load"

WORK_DIR="${REPO_ROOT}/out/vendor_boot_work"
ROOT_DIR="${WORK_DIR}/root.1"
MODULES_OUTPUT_DIR="${ROOT_DIR}/lib/modules"
OUTPUT_IMG="${REPO_ROOT}/dist/vendor_boot.img"

# Header params from stock A066BXXS3AYI3 vendor_boot.json
# (headerVersion=4, pageSize=4096, kernelLoadAddr=0x40000000)
VENDOR_CMDLINE="bootopt=64S3,32N2,64N2 loop.max_part=7"
PAGESIZE=4096
HEADER_VERSION=4
BOARD="SRPXI30B003"
BASE=0x40000000
RAMDISK_OFFSET=0x26F84000   # ramdisk.loadAddr(0x66F84000) - base(0x40000000)
TAGS_OFFSET=0x07C80000      # tagsLoadAddr(0x47C80000)    - base(0x40000000)
DTB_OFFSET=0x07C80000       # dtb.loadAddr(0x47C80000)    - base(0x40000000)

setup_workspace() {
    rm -rf "${WORK_DIR}"
    mkdir -p "${WORK_DIR}"
    # Copy pre-extracted ramdisk skeleton (fstab.mt6835, dpolicy, lib/modules/)
    cp -a "${VENDOR_BOOT_PREBUILT}/root.1" "${ROOT_DIR}"
    # .gitkeep is a placeholder only — remove before LKM_Tools writes modules
    rm -f "${MODULES_OUTPUT_DIR}/.gitkeep"
    mkdir -p "${MODULES_OUTPUT_DIR}"
}

package_modules() {
    "${LKM_TOOLS_DIR}/02.prepare_vendor_boot_modules.sh" \
        "${VENDOR_BOOT_MODULES_LIST}" \
        "${STAGING_DIR}" \
        "${VENDOR_BOOT_MODULES_LOAD}" \
        "${SYSTEM_MAP}" \
        "${STRIP_TOOL}" \
        "${MODULES_OUTPUT_DIR}"
}

repack_image() {
    local ramdisk="${WORK_DIR}/vendor_ramdisk.lz4"
    local dtb="${VENDOR_BOOT_PREBUILT}/dtb"

    echo "[INFO] Packing root.1/ → cpio → lz4 ramdisk..."
    (cd "${ROOT_DIR}" && find . | sort | cpio -H newc -o 2>/dev/null) | \
        lz4 -l -12 -c - > "${ramdisk}"

    mkdir -p "$(dirname "${OUTPUT_IMG}")"
    python3 "${MKBOOTIMG}" \
        --header_version ${HEADER_VERSION} \
        --vendor_boot "${OUTPUT_IMG}" \
        --vendor_cmdline "${VENDOR_CMDLINE}" \
        --base ${BASE} \
        --ramdisk_offset ${RAMDISK_OFFSET} \
        --tags_offset ${TAGS_OFFSET} \
        --dtb "${dtb}" \
        --dtb_offset ${DTB_OFFSET} \
        --pagesize ${PAGESIZE} \
        --board "${BOARD}" \
        --ramdisk_type platform \
        --ramdisk_name "" \
        --vendor_ramdisk_fragment "${ramdisk}"

    echo "[INFO] vendor_boot.img -> ${OUTPUT_IMG} ($(du -h "${OUTPUT_IMG}" | cut -f1))"
}

{ setup_workspace && package_modules && repack_image; } || {
    echo "[ERROR] Failed to build vendor_boot"
    exit 1
}
