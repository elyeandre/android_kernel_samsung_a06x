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

# Pre-committed extracted vendor_dlkm skeleton (etc/ + .repack_info/, no .ko files)
PRE_EXTRACTED_DIR="${REPO_ROOT}/prebuilts_a06x/vendor_dlkm/extracted_vendor_dlkm"
FILE_CONTEXTS="${PRE_EXTRACTED_DIR}/.repack_info/file_contexts.txt"
STOCK_TIMESTAMP=1757501825

WORK_DIR="${REPO_ROOT}/out/vendor_dlkm_work"
STAGING_TREE="${WORK_DIR}/staging"       # copy of PRE_EXTRACTED_DIR for this build
MODULES_OUTPUT_DIR="${WORK_DIR}/modules_out"  # LKM_Tools output (isolated)

OUTPUT_IMG="${REPO_ROOT}/dist/vendor_dlkm.img"

setup_workspace() {
    rm -rf "${WORK_DIR}"
    mkdir -p "${STAGING_TREE}"
    # Copy pre-extracted skeleton (preserves etc/, .repack_info/) into staging tree
    cp -a "${PRE_EXTRACTED_DIR}/." "${STAGING_TREE}/"
    # .gitkeep is a placeholder only — remove it so modules go in cleanly
    rm -f "${STAGING_TREE}/lib/modules/.gitkeep"
    mkdir -p "${STAGING_TREE}/lib/modules"
}

package_modules() {
    # 03.prepare_vendor_dlkm.sh opens with rm -rf "$OUTPUT_DIR" — point it at
    # MODULES_OUTPUT_DIR (fully isolated from STAGING_TREE).
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
    local modules_dir="${STAGING_TREE}/lib/modules"

    # Populate staging tree with freshly built + stripped modules
    cp "${MODULES_OUTPUT_DIR}"/*.ko "${modules_dir}/"
    cp "${MODULES_OUTPUT_DIR}"/modules.* "${modules_dir}/"

    mkdir -p "$(dirname "${OUTPUT_IMG}")"
    # --all-root   : set uid/gid 0:0 (correct for vendor partition files)
    # --file-contexts: apply SELinux labels from pre-extracted .repack_info
    mkfs.erofs \
        --all-root \
        --file-contexts="${FILE_CONTEXTS}" \
        -z lz4 -T ${STOCK_TIMESTAMP} \
        "${OUTPUT_IMG}" \
        "${STAGING_TREE}"
    echo "[INFO] vendor_dlkm.img -> ${OUTPUT_IMG} ($(du -h "${OUTPUT_IMG}" | cut -f1))"
}

{ setup_workspace && package_modules && repack_image; } || {
    echo "[ERROR] Failed to build vendor_dlkm"
    exit 1
}
