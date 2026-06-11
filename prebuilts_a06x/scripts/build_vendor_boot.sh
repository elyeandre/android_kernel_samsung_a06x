#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

LKM_TOOLS_DIR="${REPO_ROOT}/prebuilts_a06x/LKM_Tools"
KBUILD_PATH="${REPO_ROOT}/out/target/product/a06x/obj/KERNEL_OBJ"
BOOT_EDITOR_DIR="${REPO_ROOT}/prebuilts_a06x/boot_editor"

STAGING_DIR="${KBUILD_PATH}/staging"
SYSTEM_MAP="${KBUILD_PATH}/kernel-5.15/System.map"
STRIP_TOOL="${REPO_ROOT}/kernel/prebuilts/clang/host/linux-x86/clang-r450784e/bin/llvm-strip"
VENDOR_BOOT_MODULES_LIST="${LKM_TOOLS_DIR}/vendor_boot/modules_list.txt"
VENDOR_BOOT_MODULES_LOAD="${LKM_TOOLS_DIR}/vendor_boot/modules.load"
STOCK_IMG="${REPO_ROOT}/stock_images/vendor_boot.img"
OUTPUT_IMG="${REPO_ROOT}/dist/vendor_boot.img"

# boot_editor expects the source image named vendor_boot.img in its root
# and LKM_Tools drops modules into build/unzip_boot/root.1/lib/modules/
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
    # Set JAVA_HOME to JDK 17 (required by boot_editor)
    export JAVA_HOME="${JAVA_HOME_17_X64:-$JAVA_HOME}"
}

unpack_stock() {
    echo "[INFO] Placing stock vendor_boot.img into boot_editor..."
    cp "${STOCK_IMG}" "${BOOT_EDITOR_DIR}/vendor_boot.img"
    cd "${BOOT_EDITOR_DIR}"
    ./gradlew unpack
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
    cd "${BOOT_EDITOR_DIR}"
    ./gradlew pack
    mkdir -p "$(dirname "${OUTPUT_IMG}")"
    mv vendor_boot.img.signed "${OUTPUT_IMG}"
    cd "${REPO_ROOT}"
    echo "[INFO] vendor_boot.img -> ${OUTPUT_IMG} ($(du -h "${OUTPUT_IMG}" | cut -f1))"
}

{ setup_boot_editor && unpack_stock && package_modules && repack_image; } || {
    echo "[ERROR] Failed to build vendor_boot"
    exit 1
}
