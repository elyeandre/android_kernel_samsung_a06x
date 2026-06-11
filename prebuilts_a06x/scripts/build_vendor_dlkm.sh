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

# Separate dirs so LKM_Tools' internal rm -rf never touches the extracted image
EXTRACT_DIR="${WORK_DIR}/extracted"     # full stock erofs extract (lib/ + etc/)
MODULES_OUTPUT_DIR="${WORK_DIR}/modules_out"  # LKM_Tools writes modules here

OUTPUT_IMG="${REPO_ROOT}/dist/vendor_dlkm.img"
STOCK_IMG="${REPO_ROOT}/stock_images/vendor_dlkm.img"
STOCK_TIMESTAMP=1757501825

extract_stock() {
    echo "[INFO] Extracting stock vendor_dlkm.img..."
    rm -rf "${EXTRACT_DIR}"
    # Extract the entire erofs image (lib/ and etc/ both land in EXTRACT_DIR)
    fsck.erofs --extract="${EXTRACT_DIR}" "${STOCK_IMG}"
}

package_modules() {
    # 03.prepare_vendor_dlkm.sh starts with rm -rf "$OUTPUT_DIR", so point it
    # at MODULES_OUTPUT_DIR (isolated from EXTRACT_DIR).
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
    local modules_dir="${EXTRACT_DIR}/lib/modules"

    # Replace stock modules with the freshly built + stripped set
    rm -f "${modules_dir}"/*.ko "${modules_dir}"/modules.*
    cp "${MODULES_OUTPUT_DIR}"/*.ko "${modules_dir}/"
    cp "${MODULES_OUTPUT_DIR}"/modules.* "${modules_dir}/"

    mkdir -p "$(dirname "${OUTPUT_IMG}")"
    mkfs.erofs -z lz4 -T ${STOCK_TIMESTAMP} "${OUTPUT_IMG}" "${EXTRACT_DIR}"
    echo "[INFO] vendor_dlkm.img -> ${OUTPUT_IMG} ($(du -h "${OUTPUT_IMG}" | cut -f1))"
}

{ extract_stock && package_modules && repack_image; } || {
    echo "[ERROR] Failed to build vendor_dlkm"
    exit 1
}
