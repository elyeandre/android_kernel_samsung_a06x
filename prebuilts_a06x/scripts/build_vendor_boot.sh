#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

LKM_TOOLS_DIR="${REPO_ROOT}/prebuilts_a06x/LKM_Tools"
KBUILD_PATH="${REPO_ROOT}/out/target/product/a06x/obj/KERNEL_OBJ"
BOOT_EDITOR_DIR="${REPO_ROOT}/prebuilts_a06x/boot_editor"

# Pre-committed boot_editor working state (ramdisk.img.lz4 + dtb + root.1/ skeleton)
VENDOR_BOOT_PREBUILT="${REPO_ROOT}/prebuilts_a06x/vendor_boot/unzip_boot"

STAGING_DIR="${KBUILD_PATH}/staging"
SYSTEM_MAP="${KBUILD_PATH}/kernel-5.15/System.map"
STRIP_TOOL="${REPO_ROOT}/kernel/prebuilts/clang/host/linux-x86/clang-r450784e/bin/llvm-strip"
VENDOR_BOOT_MODULES_LIST="${LKM_TOOLS_DIR}/vendor_boot/modules_list.txt"
VENDOR_BOOT_MODULES_LOAD="${LKM_TOOLS_DIR}/vendor_boot/modules.load"
OUTPUT_IMG="${REPO_ROOT}/dist/vendor_boot.img"

# Where LKM_Tools writes modules — inside boot_editor's working copy
MODULES_OUTPUT_DIR="${BOOT_EDITOR_DIR}/build/unzip_boot/root.1/lib/modules"

setup_boot_editor() {
    # Check for gradlew specifically — directory may exist but be empty
    if [ ! -f "${BOOT_EDITOR_DIR}/gradlew" ]; then
        echo "[INFO] Cloning boot_editor v15_r1..."
        rm -rf "${BOOT_EDITOR_DIR}"
        git clone --depth 1 --branch v15_r1 \
            https://github.com/cfig/Android_boot_image_editor.git \
            "${BOOT_EDITOR_DIR}"
    fi

    # Restore pre-committed unzip_boot state into boot_editor's working dir.
    # This replaces any stale state from a previous run.
    echo "[INFO] Restoring pre-extracted vendor_boot state..."
    rm -rf "${BOOT_EDITOR_DIR}/build/unzip_boot"
    mkdir -p "${BOOT_EDITOR_DIR}/build"
    cp -a "${VENDOR_BOOT_PREBUILT}" "${BOOT_EDITOR_DIR}/build/unzip_boot"
    # .gitkeep is a placeholder only — remove before LKM_Tools writes modules
    rm -f "${MODULES_OUTPUT_DIR}/.gitkeep"
    mkdir -p "${MODULES_OUTPUT_DIR}"

    # Set JAVA_HOME to JDK 17+ (required by boot_editor)
    export JAVA_HOME="${JAVA_HOME_17_X64:-$JAVA_HOME}"
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
    cd "${BOOT_EDITOR_DIR}"
    ./gradlew pack
    mkdir -p "$(dirname "${OUTPUT_IMG}")"
    mv vendor_boot.img.signed "${OUTPUT_IMG}"
    cd "${REPO_ROOT}"
    echo "[INFO] vendor_boot.img -> ${OUTPUT_IMG} ($(du -h "${OUTPUT_IMG}" | cut -f1))"
}

{ setup_boot_editor && package_modules && repack_image; } || {
    echo "[ERROR] Failed to build vendor_boot"
    exit 1
}
