#!/bin/bash
set -euo pipefail  # Fail on errors, undefined vars, pipe failures

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

export WDIR="$(pwd)"
mkdir -p "${WDIR}/dist"

# Localversion
#
# vermagic is compared byte-for-byte when loading modules, so the release string
# must match stock exactly. Note how it is assembled -- scripts/setlocalversion
# (line ~188) prepends the GKI branch/KMI generation itself:
#
#   KERNELRELEASE = 5.15.151 + "-android13-8" + CONFIG_LOCALVERSION
#                   ^KERNELVERSION  ^from BRANCH=android13-5.15 + KMI_GENERATION=8
#
#   stock:  5.15.151-android13-8-30546824   => CONFIG_LOCALVERSION="-30546824"
#
# So CONFIG_LOCALVERSION must be ONLY the trailing build id. Do NOT put
# "-android13-8" here or it is emitted twice
# (5.15.151-android13-8-android13-8-...). Setting anything else -- e.g.
# "-a06x-dev", which yields the observed 5.15.151-android13-8-a06x-dev -- makes
# every stock vendor_boot/vendor_dlkm module fail to load, so there is no
# display/touch/PMIC and the device never reaches the boot animation.
#
# Matching stock is what lets this GKI 2.0 kernel be flashed as boot.img alone
# against the stock vendor images. BUILD_KERNEL_VERSION still names artifacts.
if [ -z "${BUILD_KERNEL_VERSION:-}" ]; then
    export BUILD_KERNEL_VERSION="dev"
fi
KMI_LOCALVERSION="${KMI_LOCALVERSION:--30546824}"
printf 'CONFIG_LOCALVERSION_AUTO=n\nCONFIG_LOCALVERSION="%s"\n' "${KMI_LOCALVERSION}" \
    > "${WDIR}/custom_defconfigs/version_defconfig"

# Branding that is SAFE for the KMI: these only feed LINUX_COMPILE_BY/HOST in
# init/version.c (i.e. /proc/version and `uname -a`). vermagic is built from
# UTS_RELEASE alone, so these never affect module loading — unlike
# CONFIG_LOCALVERSION above.
export KBUILD_BUILD_USER="${KBUILD_BUILD_USER:-elyeandre}"
export KBUILD_BUILD_HOST="${KBUILD_BUILD_HOST:-a06x}"

# ============================================================================
# Print Banner
# ============================================================================
print_banner() {
    echo -e "${BLUE}"
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║       Samsung Galaxy A06x Kernel Builder (5.15)          ║"
    echo "║                       by @elyeandre                      ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo -e "${NC}\n"
}

# ============================================================================
# Function: Install Build Dependencies
# ============================================================================
install_dependencies() {
    local packages=(
        build-essential
        bc
        bison
        flex
        libssl-dev
        libelf-dev
        python3
        git
        kmod
        device-tree-compiler
        lz4
        xz-utils
        zlib1g-dev
    )
    
    echo -e "\n${YELLOW}[INFO]${NC} Checking build requirements..."
    
    # Check which packages are missing (most reliable method)
    local missing=()
    for pkg in "${packages[@]}"; do
        if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed"; then
            missing+=("$pkg")
        fi
    done
    
    # All packages already installed
    if [[ ${#missing[@]} -eq 0 ]]; then
        echo -e "${GREEN}[OK]${NC} All dependencies already installed\n"
        return 0
    fi
    
    # Some packages need installation
    echo -e "${YELLOW}[INFO]${NC} Missing packages: ${missing[*]}"
    
    local SUDO=""
    if [[ $EUID -ne 0 ]]; then
        SUDO="sudo"
    fi

    echo -e "${YELLOW}[INFO]${NC} Installing ${#missing[@]} missing packages..."

    # Update package lists only when needed
    if ! ${SUDO} apt-get update -qq 2>&1 | grep -q "^E:"; then
        :
    else
        echo -e "${RED}[ERROR]${NC} Failed to update package lists" >&2
        return 1
    fi

    # Install only missing packages (hide output)
    if DEBIAN_FRONTEND=noninteractive ${SUDO} apt-get install -y -qq "${missing[@]}" > /dev/null 2>&1; then
        echo -e "${GREEN}[OK]${NC} Dependencies installed\n"
    else
        echo -e "${RED}[ERROR]${NC} Failed to install dependencies" >&2
        return 1
    fi
}

# ============================================================================
# Function: Setup Samsung NDK Toolchain
# ============================================================================
setup_toolchain() {
    # Skip if toolchain already exists
    [[ -d "${WDIR}/kernel/prebuilts" || -d "${WDIR}/prebuilts" ]] && {
        echo -e "\n${GREEN}[OK]${NC} Toolchain already exists\n"
        return 0
    }
    
    local url="https://github.com/elyeandre/android_kernel_samsung_a06x/releases/download/toolchain/toolchain-gcc.tar.gz"
    local temp=$(mktemp -d -p /var/tmp toolchain-XXXXXX)
    local archive="${temp}/toolchain.tar.gz"
    
    # Ensure cleanup on exit
    trap "rm -rf '${temp}'" EXIT ERR INT TERM
    
    echo -e "\n${YELLOW}[INFO]${NC} Downloading Samsung's NDK..."
    if ! curl -L --progress-bar --fail --retry 3 --retry-delay 2 -o "${archive}" "${url}"; then
        echo -e "\n${RED}[ERROR]${NC} Download failed. Check your connection." >&2
        return 1
    fi
    
    # Extract and cleanup archive immediately
    echo -e "\n${YELLOW}[INFO]${NC} Extracting toolchain..."
    if ! tar -xzf "${archive}" -C "${temp}"; then
        echo -e "\n${RED}[ERROR]${NC} Extraction failed. Archive may be corrupted." >&2
        return 1
    fi
    rm -f "${archive}"
    
    # Move toolchain to workspace
    echo -e "\n${YELLOW}[INFO]${NC} Setting up toolchain..."
    
    # Move kernel/prebuilts
    if [[ -d "${temp}/toolchain/kernel/prebuilts" ]]; then
        mv "${temp}/toolchain/kernel/prebuilts" "${WDIR}/kernel/"
        echo -e "${GREEN}[OK]${NC} Kernel prebuilts installed"
    fi
    
    # Move root prebuilts
    if [[ -d "${temp}/toolchain/prebuilts" ]]; then
        mv "${temp}/toolchain/prebuilts" "${WDIR}/"
        echo -e "${GREEN}[OK]${NC} Root prebuilts installed"
    fi
    
    # Verify setup
    if [[ ! -d "${WDIR}/kernel/prebuilts" && ! -d "${WDIR}/prebuilts" ]]; then
        echo -e "\n${RED}[ERROR]${NC} Toolchain setup failed!" >&2
        return 1
    fi
    
    echo -e "${GREEN}[OK]${NC} Toolchain ready\n"
}

# ============================================================================
# Function: Export Common Build Environment
# ============================================================================
export_common_build_env() {
    cd "${WDIR}/kernel-5.15"

    # Cook build config
    python scripts/gen_build_config.py \
        --kernel-defconfig a06x_00_defconfig \
        --kernel-defconfig-overlays "entry_level.config" \
        -m user \
        -o ../out/target/product/a06x/obj/KERNEL_OBJ/build.config &>/dev/null

    # build.config.mtk.aarch64 sets FILES="vmlinux" which only copies vmlinux to
    # DIST_DIR. Append Image so build_boot_images() can find the kernel binary.
    printf '\nFILES="${FILES} arch/arm64/boot/Image"\n' \
        >> "${WDIR}/out/target/product/a06x/obj/KERNEL_OBJ/build.config.mtk"

    # Common exports from Samsung's build_kernel.sh
    export ARCH=arm64
    export CROSS_COMPILE="aarch64-linux-gnu-"
    export CROSS_COMPILE_COMPAT="arm-linux-gnueabi-"
    export OUT_DIR="../out/target/product/a06x/obj/KERNEL_OBJ"
    export DIST_DIR="../out/target/product/a06x/obj/KERNEL_OBJ"
    export BUILD_CONFIG="../out/target/product/a06x/obj/KERNEL_OBJ/build.config"

    cd "${WDIR}"
}

# ============================================================================
# Function: Export Custom Build Environment
# ============================================================================
export_custom_build_env() {
    # Run menuconfig only if you want to.
    # It's better to use MAKE_MENUCONFIG=0 when everything is already properly enabled, disabled, or configured.
    export MAKE_MENUCONFIG=0

    export GKI_KERNEL_BUILD_OPTIONS=(
        "LTO=thin"
        "SKIP_MRPROPER=1"
        "KMI_SYMBOL_LIST_STRICT_MODE=0"
        "ABI_DEFINITION="
        "MAKE_MENUCONFIG=${MAKE_MENUCONFIG}"
        "BUILD_BOOT_IMG=1"
        "MKBOOTIMG_PATH=${WDIR}/prebuilts_a06x/mkbootimg/mkbootimg.py"
        "KERNEL_BINARY=Image"
        "BOOT_IMAGE_HEADER_VERSION=4"
        "SKIP_VENDOR_BOOT=1"
        "AVB_SIGN_BOOT_IMG=1"
        "AVB_BOOT_PARTITION_SIZE=67108864"
        "AVB_BOOT_KEY=${WDIR}/prebuilts_a06x/mkbootimg/tests/data/testkey_rsa2048.pem"
        "AVB_BOOT_ALGORITHM=SHA256_RSA2048"
        "AVB_BOOT_PARTITION_NAME=boot"
        "MKBOOTIMG_EXTRA_ARGS=--os_version 13.0.0 --os_patch_level 2025-09-00 --pagesize 4096"
    )

    if [ "$MAKE_MENUCONFIG" = "1" ]; then
        export HERMETIC_TOOLCHAIN=0
        GKI_KERNEL_BUILD_OPTIONS+=("HERMETIC_TOOLCHAIN=0")
        echo ""
    fi

    # environment variables for custom defconfigs support
    export MERGE_CONFIG="${WDIR}/kernel-5.15/scripts/kconfig/merge_config.sh"

    # Collect realpaths as a space-separated list
    if [ -d "${WDIR}/custom_defconfigs" ]; then
        CUSTOM_DEFCONFIGS_LIST=$(find "${WDIR}/custom_defconfigs" -maxdepth 1 -type f -exec realpath {} \; | tr '\n' ' ')
    else
        CUSTOM_DEFCONFIGS_LIST=""
    fi
    export CUSTOM_DEFCONFIGS_LIST
}

# ============================================================================
# Function: Build GKI Kernel
# ============================================================================
build_gki_kernel() {
    cd "${WDIR}/kernel"

    env "${GKI_KERNEL_BUILD_OPTIONS[@]}" ./build/build.sh && \
        cp "${WDIR}/out/target/product/a06x/obj/KERNEL_OBJ/kernel-5.15/arch/arm64/boot/Image"* "${WDIR}/dist" && \
        cp "${WDIR}/out/target/product/a06x/obj/KERNEL_OBJ/dist/boot.img" "${WDIR}/dist/"

    local exit_code=$?
    cd "${WDIR}"

    # Samsung bootloaders require the literal "SEANDROIDENFORCE" magic appended
    # to the boot image; without it the image is rejected before the kernel ever
    # runs, so there is no panic and no boot animation. mkbootimg does not add
    # it, which is why CI images did not boot while a magiskboot repack of a
    # working image did (the repack preserves the existing footer).
    #
    # Compare, via `magiskboot unpack`:
    #   working : KERNEL_FMT [gzip], SAMSUNG_SEANDROID, VBMETA
    #   CI       : KERNEL_FMT [raw],                    VBMETA
    if [ "$exit_code" -eq 0 ] && [ -f "${WDIR}/dist/boot.img" ]; then
        if tail -c 16 "${WDIR}/dist/boot.img" | grep -q 'SEANDROIDENFORCE'; then
            echo -e "${GREEN}[OK]${NC} boot.img already carries SEANDROIDENFORCE"
        else
            printf 'SEANDROIDENFORCE' >> "${WDIR}/dist/boot.img"
            echo -e "${GREEN}[OK]${NC} Appended SEANDROIDENFORCE to boot.img"
        fi
    fi

    return $exit_code
}

# ============================================================================
# Function: Package as Odin-flashable tar
# ============================================================================
build_odin_tar() {
    echo -e "\n${YELLOW}[INFO]${NC} Packaging Odin-flashable tar...\n"
    cd "${WDIR}/dist"
    tar -cvf "kernel-a06x-${BUILD_KERNEL_VERSION}.tar" boot.img
    echo -e "${GREEN}[OK]${NC} Odin tar created: dist/kernel-a06x-${BUILD_KERNEL_VERSION}.tar\n"
    cd "${WDIR}"
}

# ============================================================================
# Main Execution
# ============================================================================
print_banner

# Check for --skip-deps flag
if [[ "${1:-}" != "--skip-deps" ]]; then
    install_dependencies
else
    echo -e "\n${YELLOW}[INFO]${NC} Skipping dependency installation (--skip-deps flag)\n"
fi

setup_toolchain
export_common_build_env
export_custom_build_env
build_gki_kernel || exit 1
build_odin_tar
