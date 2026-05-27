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
    
    # Check if running as root/sudo
    if [[ $EUID -ne 0 ]]; then
        echo -e "${RED}[ERROR]${NC} Need sudo to install missing packages"
        echo -e "${YELLOW}[INFO]${NC} Run: ${BLUE}sudo $0${NC}"
        exit 1
    fi
    
    echo -e "${YELLOW}[INFO]${NC} Installing ${#missing[@]} missing packages..."
    
    # Update package lists only when needed
    if ! apt-get update -qq 2>&1 | grep -q "^E:"; then
        :
    else
        echo -e "${RED}[ERROR]${NC} Failed to update package lists" >&2
        return 1
    fi
    
    # Install only missing packages (hide output)
    if DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${missing[@]}" > /dev/null 2>&1; then
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

    # Common exports from Samsung's build_kernel.sh
    export ARCH=arm64
    export PLATFORM_VERSION=13
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
        cp "${WDIR}/out/target/product/a06x/obj/KERNEL_OBJ/kernel-5.15/arch/arm64/boot/Image"* "${WDIR}/dist"

    local exit_code=$?
    cd "${WDIR}"
    return $exit_code
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
