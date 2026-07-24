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
# Command-line options
#
# Every flag just sets the environment variable the rest of the script already
# reads, so `FOO=bar ./build.sh` and `./build.sh --foo bar` are equivalent.
# Parsed here, up front, because the Localversion block below consumes several
# of them immediately.
# ============================================================================
SKIP_DEPS=0
usage() {
    cat <<'EOF'
Usage: ./build.sh [options]

  -n, --name NAME       Base name of the output zip           (env RELEASE_NAME)
                        default: kernel-a06x-<version>
  -V, --version VER     Artifact version tag (does NOT change vermagic)
                        (env BUILD_KERNEL_VERSION, default: dev)
  -b, --mode-b          Custom-vermagic build (Mode B), vermagic ...-a06x-dev.
                        Then flash ALL images together. For a different suffix
                        use --kmi.                             (env KMI_LOCALVERSION)
      --kmi STR         Set CONFIG_LOCALVERSION directly, e.g. --kmi -mykernel
                        (Mode B). Omit entirely for stock KMI = Mode A, the
                        default (boot.img alone).
      --user NAME       KBUILD_BUILD_USER  - branding only, KMI-safe (def elyeandre)
      --host NAME       KBUILD_BUILD_HOST  - branding only, KMI-safe (def a06x)
  -m, --menuconfig      Open menuconfig during the build
  -s, --skip-deps       Skip apt dependency installation
  -h, --help            Show this help

Examples:
  ./build.sh                     # Mode A, stock KMI (flash boot.img alone)
  ./build.sh -n a06x-mt76-r1     # custom output name
  ./build.sh --mode-b            # Mode B (vermagic ...-a06x-dev)
  ./build.sh -s                  # reuse already-installed deps
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--name)       export RELEASE_NAME="${2:?--name needs a value}"; shift 2 ;;
        -V|--version)    export BUILD_KERNEL_VERSION="${2:?--version needs a value}"; shift 2 ;;
        -b|--mode-b)     export KMI_LOCALVERSION="-a06x-dev"; shift ;;
        --kmi)           export KMI_LOCALVERSION="${2:?--kmi needs a value (omit for stock KMI)}"; shift 2 ;;
        --user)          export KBUILD_BUILD_USER="${2:?--user needs a value}"; shift 2 ;;
        --host)          export KBUILD_BUILD_HOST="${2:?--host needs a value}"; shift 2 ;;
        -m|--menuconfig) export MAKE_MENUCONFIG=1; shift ;;
        -s|--skip-deps)  SKIP_DEPS=1; shift ;;
        -h|--help)       usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

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
        # vendor_dlkm.img is EROFS (mkfs.erofs); vendor_boot repack (boot_editor)
        # is a Gradle/Java tool needing JDK 17+. On CI these came from the runner
        # image; on a VPS build.sh must provide them.
        erofs-utils
        openjdk-17-jdk-headless
        zip
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
    # DIST_DIR. Append the kernel binaries so build_boot_images() can find them.
    #
    # Both are exported: Image.gz is what gets packed (see KERNEL_BINARY below),
    # while the raw Image is still published as an artifact for repacking by
    # hand with magiskboot. Image.gz is arm64's default target
    # (arch/arm64/Makefile: KBUILD_IMAGE := $(boot)/Image.gz), so the build
    # already produces it.
    printf '\nFILES="${FILES} arch/arm64/boot/Image arch/arm64/boot/Image.gz"\n' \
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
    export MAKE_MENUCONFIG="${MAKE_MENUCONFIG:-0}"

    export GKI_KERNEL_BUILD_OPTIONS=(
        "LTO=thin"
        "SKIP_MRPROPER=1"
        "KMI_SYMBOL_LIST_STRICT_MODE=0"
        "ABI_DEFINITION="
        "MAKE_MENUCONFIG=${MAKE_MENUCONFIG}"
        "BUILD_BOOT_IMG=1"
        "MKBOOTIMG_PATH=${WDIR}/prebuilts_a06x/mkbootimg/mkbootimg.py"
        # Pack the gzip kernel to match stock. `magiskboot unpack` of a working
        # boot.img reports KERNEL_FMT [gzip]; CI packed raw Image, which is a
        # real deviation from stock (and 42MB vs 19MB in a 64MB partition).
        "KERNEL_BINARY=Image.gz"
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

    # NOTE: do NOT append SEANDROIDENFORCE here.
    #
    # Samsung bootloaders do want that 16-byte magic, but AVB_SIGN_BOOT_IMG=1
    # makes avbtool pad boot.img to exactly AVB_BOOT_PARTITION_SIZE
    # (67108864). Appending afterwards yields 67108880 bytes and fastboot
    # refuses it:
    #   boot partition size: 67108864, boot image size: 67108880
    #   FAILED (remote: 'Value too large for defined data type')
    #
    # The correct layout is [boot image][SEANDROIDENFORCE][AVB footer], i.e.
    # the magic has to be added BEFORE signing. Doing that means disabling the
    # GKI script's own AVB step and running avbtool add_hash_footer here
    # instead. Until that is implemented, repack with magiskboot against a
    # known-good boot.img - it preserves the footer and the kernel format.

    return $exit_code
}

# ============================================================================
# Function: Package as Odin-flashable tar
# ============================================================================
build_vendor_images() {
    echo -e "\n${YELLOW}[INFO]${NC} Rebuilding vendor_dlkm and vendor_boot...\n"

    # boot_editor (vendor_boot repack) needs JDK 17+. CI exports JAVA_HOME_17_X64;
    # on a VPS, fall back to the distro's OpenJDK 17 installed by install_dependencies.
    if [ -z "${JAVA_HOME_17_X64:-}" ] && [ -z "${JAVA_HOME:-}" ]; then
        for j in /usr/lib/jvm/java-17-openjdk-amd64 /usr/lib/jvm/java-17-openjdk* ; do
            [ -d "$j" ] && { export JAVA_HOME="$j"; break; }
        done
    fi

    bash "${WDIR}/prebuilts_a06x/scripts/build_vendor_dlkm.sh"
    bash "${WDIR}/prebuilts_a06x/scripts/build_vendor_boot.sh"
    echo -e "${GREEN}[OK]${NC} vendor_dlkm.img + vendor_boot.img -> dist/\n"
}

# ============================================================================
# Function: Package the flashable release
#
#   dist/<NAME>.zip
#   └── <NAME>/
#       ├── <NAME>.tar        Odin (AP): boot.img + vendor_boot.img
#       ├── vendor_dlkm.img   fastbootd: `fastboot flash vendor_dlkm`
#       └── FLASH.txt         step-by-step guide
#
# vendor_dlkm is a LOGICAL partition inside `super`; Odin only writes physical
# PIT partitions, so it can NEVER flash vendor_dlkm. It ships loose for fastbootd.
# boot and vendor_boot are physical partitions -> they go in the Odin AP tar.
# Override the base name with RELEASE_NAME=... (CI uses the artifact/tag name).
# ============================================================================
package_release() {
    local NAME="${RELEASE_NAME:-kernel-a06x-${BUILD_KERNEL_VERSION}}"
    local OUT="${WDIR}/dist"
    local STAGE="${OUT}/${NAME}"

    echo -e "\n${YELLOW}[INFO]${NC} Packaging release ${NAME}...\n"

    local missing=0
    for img in boot.img vendor_boot.img vendor_dlkm.img ; do
        [ -f "${OUT}/${img}" ] || { echo -e "${RED}[ERROR]${NC} dist/${img} missing"; missing=1; }
    done
    [ "$missing" -eq 0 ] || return 1

    rm -rf "${STAGE}"; mkdir -p "${STAGE}"

    # Odin AP tar: physical partitions only. Odin maps entries to partitions by
    # filename, so raw <partition>.img files flash to their partitions.
    tar -cf "${STAGE}/${NAME}.tar" -C "${OUT}" boot.img vendor_boot.img

    # vendor_dlkm ships loose (fastbootd only)
    cp "${OUT}/vendor_dlkm.img" "${STAGE}/"

    cat > "${STAGE}/FLASH.txt" <<EOF
${NAME}

Flash in TWO steps — vendor_dlkm cannot be flashed by Odin.

1) Odin (Download mode)
   Load  ${NAME}.tar  into the AP slot and flash.
   (boot.img + vendor_boot.img — both physical partitions)

2) fastbootD  (vendor_dlkm is a LOGICAL partition inside 'super'; Odin cannot
   write it — only fastbootD can)
     adb reboot fastboot                          # enter fastbootD
     fastboot flash vendor_dlkm vendor_dlkm.img
     fastboot reboot

Notes
- Do step 1 first, then step 2.
- From bootloader mode, 'fastboot reboot fastboot' also reaches fastbootD.
- 'fastboot flash vendor_dlkm' needs an unlocked bootloader.
EOF

    ( cd "${OUT}" && rm -f "${NAME}.zip" && zip -qr "${NAME}.zip" "${NAME}" )
    rm -rf "${STAGE}"

    echo -e "${GREEN}[OK]${NC} dist/${NAME}.zip"
    echo -e "     |- ${NAME}.tar     (Odin AP: boot + vendor_boot)"
    echo -e "     |- vendor_dlkm.img (fastboot flash vendor_dlkm)"
    echo -e "     \`- FLASH.txt\n"
}

# ============================================================================
# Main Execution
# ============================================================================
print_banner

# Dependency install (skip with -s/--skip-deps)
if [ "${SKIP_DEPS}" -eq 0 ]; then
    install_dependencies
else
    echo -e "\n${YELLOW}[INFO]${NC} Skipping dependency installation (--skip-deps)\n"
fi

setup_toolchain
export_common_build_env
export_custom_build_env
build_gki_kernel || exit 1
build_vendor_images
package_release
