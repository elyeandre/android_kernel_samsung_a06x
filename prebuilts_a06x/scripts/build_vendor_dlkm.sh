#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

LKM_TOOLS_DIR="${REPO_ROOT}/prebuilts_a06x/LKM_Tools"
KBUILD_PATH="${REPO_ROOT}/out/target/product/a06x/obj/KERNEL_OBJ"

STAGING_DIR="${KBUILD_PATH}/staging"
SYSTEM_MAP="${KBUILD_PATH}/kernel-5.15/System.map"
STRIP_TOOL="${REPO_ROOT}/kernel/prebuilts/clang/host/linux-x86/clang-r450784e/bin/llvm-strip"
VENDOR_DLKM_MODULES_LIST="${LKM_TOOLS_DIR}/vendor_dlkm/modules_list.txt"
VENDOR_BOOT_MODULES_LIST="${LKM_TOOLS_DIR}/vendor_boot/modules_list.txt"
VENDOR_DLKM_MODULES_LOAD="${LKM_TOOLS_DIR}/vendor_dlkm/modules.load"
WORK_DIR="${REPO_ROOT}/out/vendor_dlkm_work"
MODULES_OUTPUT_DIR="${WORK_DIR}/lib/modules"
OUTPUT_IMG="${REPO_ROOT}/dist/vendor_dlkm.img"
STOCK_IMG="${REPO_ROOT}/stock_images/vendor_dlkm.img"
STOCK_TIMESTAMP=1757501825

package_modules() {
    mkdir -p "${MODULES_OUTPUT_DIR}"
    "${LKM_TOOLS_DIR}/03.prepare_vendor_dlkm.sh" \
        "${VENDOR_DLKM_MODULES_LIST}" \
        "${STAGING_DIR}" \
        "${VENDOR_DLKM_MODULES_LOAD}" \
        "${SYSTEM_MAP}" \
        "${STRIP_TOOL}" \
        "${MODULES_OUTPUT_DIR}" \
        "${VENDOR_BOOT_MODULES_LIST}" \
        "" \
        ""
}

repack_image() {
    # Copy stock etc/ (build.prop, fs_config) alongside lib/
    fsck.erofs --extract="${WORK_DIR}" "${STOCK_IMG}" -- etc/ 2>/dev/null || true
    mkdir -p "$(dirname "${OUTPUT_IMG}")"
    mkfs.erofs -z lz4 -T ${STOCK_TIMESTAMP} "${OUTPUT_IMG}" "${WORK_DIR}"
    echo "[INFO] vendor_dlkm.img -> ${OUTPUT_IMG} ($(du -h "${OUTPUT_IMG}" | cut -f1))"
}

{ package_modules && repack_image; } || {
    echo "[ERROR] Failed to build vendor_dlkm"
    exit 1
}
