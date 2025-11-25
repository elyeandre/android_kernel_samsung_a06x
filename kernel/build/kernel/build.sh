#!/bin/bash

# Copyright (C) 2019 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#################################################################################
# BUILD.SH - KLEAF KERNEL BUILD ORCHESTRATION SCRIPT
#
# PURPOSE:
#   This is the primary orchestration script for building Android Linux kernels
#   with support for various compilation modes, external modules, ABI tracking,
#   and specialized image generation (boot images, ramdisk, device tree blobs).
#
# ARCHITECTURE OVERVIEW:
#   The script manages several key phases:
#   1. Environment Setup and Validation - Initializes paths and config sources
#   2. Build Configuration Processing - Handles mixed builds (GKI + vendor)
#   3. Kernel Configuration - Defconfig, LTO settings, KMI symbol management
#   4. Kernel Compilation - Core vmlinux and module builds via make
#   5. Module Processing - External modules, initramfs, DLKM images
#   6. Distribution Assembly - Copying artifacts, generating boot images
#   7. Post-Build Validation - ABI checks, trace_printk detection
#
# KEY KLEAF INTEGRATION POINTS:
#   - Script will eventually redirect to Bazel build system
#   - Supports mixed builds where GKI kernel comes from separate build
#   - Respects hermetic toolchain requirements and KBUILD_MIXED_TREE
#
# EXTERNAL DEPENDENCIES:
#   - build/build_utils.sh - Utility functions (rel_path, create_modules_staging)
#   - build/_setup_env.sh - Environment initialization (OUT_DIR, KERNEL_DIR, etc.)
#   - kernel config system - scripts/config for programmatic .config modification
#   - make / kbuild - Linux kernel build system
#   - Various tools: avbtool, soong_zip, pahole, llvm-strip, mkbootfs
#################################################################################

# Usage:
#   build/build.sh <make options>*
# or:
#   To define custom out and dist directories:
#     OUT_DIR=<out dir> DIST_DIR=<dist dir> build/build.sh <make options>*
#   To use a custom build config:
#     BUILD_CONFIG=<path to the build.config> <make options>*
#
# Examples:
#   To define custom out and dist directories:
#     OUT_DIR=output DIST_DIR=dist build/build.sh -j24 V=1
#   To use a custom build config:
#     BUILD_CONFIG=common/build.config.gki.aarch64 build/build.sh -j24 V=1
#
# The following environment variables are considered during execution:
#
#   BUILD_CONFIG
#     Build config file to initialize the build environment from. The location
#     is to be defined relative to the repo root directory.
#     Defaults to 'build.config'.
#
#   BUILD_CONFIG_FRAGMENTS
#     A whitespace-separated list of additional build config fragments to be
#     sourced after the main build config file. Typically used for sanitizers or
#     other special builds.  
#
#   FAST_BUILD
#     If defined, trade run-time optimizations for build speed. In other words,
#     if given a choice between a faster build and a run-time optimization,
#     choose the shorter build time. For example, use ThinLTO for faster
#     linking and reduce the lz4 compression level to speed up ramdisk
#     compression. This trade-off is desirable for incremental kernel
#     development where fast turnaround times are critical for productivity.
#
#   OUT_DIR
#     Base output directory for the kernel build.
#     Defaults to <REPO_ROOT>/out/<BRANCH>.
#
#   DIST_DIR
#     Base output directory for the kernel distribution.
#     Defaults to <OUT_DIR>/dist
#
#   MAKE_GOALS
#     List of targets passed to Make when compiling the kernel.
#     Typically: Image, modules, and a DTB (if applicable).
#
#   EXT_MODULES
#     Space separated list of external kernel modules to be build.
#
#   EXT_MODULES_MAKEFILE
#     Location of a makefile to build external modules. If set, it will get
#     called with all the necessary parameters to build and install external
#     modules.  This allows for building them in parallel using makefile
#     parallelization.
#
#   KCONFIG_EXT_PREFIX
#     Path prefix relative to either ROOT_DIR or KERNEL_DIR that points to
#     a directory containing an external Kconfig file named Kconfig.ext. When
#     set, kbuild will source ${KCONFIG_EXT_PREFIX}Kconfig.ext which can be
#     used to set configs for external modules in the defconfig.
#
#   UNSTRIPPED_MODULES
#     Space separated list of modules to be copied to <DIST_DIR>/unstripped
#     for debugging purposes.
#
#   COMPRESS_UNSTRIPPED_MODULES
#     If set to "1", then compress the unstripped modules into a tarball.
#
#   COMPRESS_MODULES
#     If set to "1", then compress all modules into a tarball. The default
#     is without defining COMPRESS_MODULES.
#
#   LD
#     Override linker (flags) to be used.
#
#   HERMETIC_TOOLCHAIN
#     When set, the PATH during kernel build will be restricted to a set of
#     known prebuilt directories and selected host tools that are usually not
#     provided by prebuilt toolchains.
#
#  ADDITIONAL_HOST_TOOLS
#     A whitespace separated set of tools that will be allowed to be used from
#     the host when running the build with HERMETIC_TOOLCHAIN=1.
#
#   ABI_DEFINITION
#     Location of the abi definition file relative to <REPO_ROOT>/KERNEL_DIR
#     If defined (usually in build.config), also copy that abi definition to
#     <OUT_DIR>/dist/abi.xml when creating the distribution.
#
#   KMI_SYMBOL_LIST
#     Location of the main KMI symbol list file relative to
#     <REPO_ROOT>/KERNEL_DIR If defined (usually in build.config), also copy
#     that symbol list definition to <OUT_DIR>/dist/abi_symbollist when
#     creating the distribution.
#
#   ADDITIONAL_KMI_SYMBOL_LISTS
#     Location of secondary KMI symbol list files relative to
#     <REPO_ROOT>/KERNEL_DIR. If defined, these additional symbol lists will be
#     appended to the main one before proceeding to the distribution creation.
#
#   KMI_ENFORCED
#     This is an indicative option to signal that KMI is enforced in this build
#     config. If set to "1", downstream KMI checking tools might respect it and
#     react to it by failing if KMI differences are detected.
#
#   GENERATE_VMLINUX_BTF
#     If set to "1", generate a vmlinux.btf that is stripped of any debug
#     symbols, but contains type and symbol information within a .BTF section.
#     This is suitable for ABI analysis through BTF.
#
# Environment variables to influence the stages of the kernel build.
#
#   SKIP_MRPROPER
#     if set to "1", skip `make mrproper`
#
#   SKIP_DEFCONFIG
#     if set to "1", skip `make defconfig`
#
#   SKIP_IF_VERSION_MATCHES
#     if defined, skip compiling anything if the kernel version in vmlinux
#     matches the expected kernel version. This is useful for mixed build, where
#     GKI kernel does not change frequently and we can simply skip everything
#     in build.sh. Note: if the expected version string contains "dirty", then
#     this flag would have not cause build.sh to exit early.
#
#   PRE_DEFCONFIG_CMDS
#     Command evaluated before `make defconfig`
#
#   POST_DEFCONFIG_CMDS
#     Command evaluated after `make defconfig` and before `make`.
#
#   POST_KERNEL_BUILD_CMDS
#     Command evaluated after `make`.
#
#   LTO=[full|thin|none]
#     If set to "full", force any kernel with LTO_CLANG support to be built
#     with full LTO, which is the most optimized method. This is the default,
#     but can result in very slow build times, especially when building
#     incrementally. (This mode does not require CFI to be disabled.)
#     If set to "thin", force any kernel with LTO_CLANG support to be built
#     with ThinLTO, which trades off some optimizations for incremental build
#     speed. This is nearly always what you want for local development. (This
#     mode does not require CFI to be disabled.)
#     If set to "none", force any kernel with LTO_CLANG support to be built
#     without any LTO (upstream default), which results in no optimizations
#     and also disables LTO-dependent features like CFI. This mode is not
#     recommended because CFI will not be able to catch bugs if it is
#     disabled.
#
#   TAGS_CONFIG
#     if defined, calls ./scripts/tags.sh utility with TAGS_CONFIG as argument
#     and exit once tags have been generated
#
#   IN_KERNEL_MODULES
#     if defined, install kernel modules
#
#   SKIP_EXT_MODULES
#     if defined, skip building and installing of external modules
#
#   DO_NOT_STRIP_MODULES
#     if set to "1", keep debug information for distributed modules.
#     Note, modules will still be stripped when copied into the ramdisk.
#
#   EXTRA_CMDS
#     Command evaluated after building and installing kernel and modules.
#
#   DIST_CMDS
#     Command evaluated after copying files to DIST_DIR
#
#   SKIP_CP_KERNEL_HDR
#     if defined, skip installing kernel headers.
#
#   BUILD_BOOT_IMG
#     if defined, build a boot.img binary that can be flashed into the 'boot'
#     partition of an Android device. The boot image contains a header as per the
#     format defined by https://source.android.com/devices/bootloader/boot-image-header
#     followed by several components like kernel, ramdisk, DTB etc. The ramdisk
#     component comprises of a GKI ramdisk cpio archive concatenated with a
#     vendor ramdisk cpio archive which is then gzipped. It is expected that
#     all components are present in ${DIST_DIR}.
#
#     When the BUILD_BOOT_IMG flag is defined, the following flags that point to the
#     various components needed to build a boot.img also need to be defined.
#     - MKBOOTIMG_PATH=<path to the mkbootimg.py script which builds boot.img>
#       (defaults to tools/mkbootimg/mkbootimg.py)
#     - GKI_RAMDISK_PREBUILT_BINARY=<Name of the GKI ramdisk prebuilt which includes
#       the generic ramdisk components like init and the non-device-specific rc files>
#     - VENDOR_RAMDISK_BINARY=<Space separated list of vendor ramdisk binaries
#        which includes the device-specific components of ramdisk like the fstab
#        file and the device-specific rc files. If specifying multiple vendor ramdisks
#        and identical file paths exist in the ramdisks, the file from last ramdisk is used.>
#     - KERNEL_BINARY=<name of kernel binary, eg. Image.lz4, Image.gz etc>
#     - BOOT_IMAGE_HEADER_VERSION=<version of the boot image header>
#       (defaults to 3)
#     - BOOT_IMAGE_FILENAME=<name of the output file>
#       (defaults to "boot.img")
#     - KERNEL_CMDLINE=<string of kernel parameters for boot>
#     - KERNEL_VENDOR_CMDLINE=<string of kernel parameters for vendor boot image,
#       vendor_boot when BOOT_IMAGE_HEADER_VERSION >= 3; boot otherwise>
#     - VENDOR_FSTAB=<Path to the vendor fstab to be included in the vendor
#       ramdisk>
#     - TAGS_OFFSET=<physical address for kernel tags>
#     - RAMDISK_OFFSET=<ramdisk physical load address>
#     If the BOOT_IMAGE_HEADER_VERSION is less than 3, two additional variables must
#     be defined:
#     - BASE_ADDRESS=<base address to load the kernel at>
#     - PAGE_SIZE=<flash page size>
#     If BOOT_IMAGE_HEADER_VERSION >= 3, a vendor_boot image will be built
#     unless SKIP_VENDOR_BOOT is defined. A vendor_boot will also be generated if
#     BUILD_VENDOR_BOOT_IMG is set.
#
#     BUILD_VENDOR_BOOT_IMG is incompatible with SKIP_VENDOR_BOOT, and is effectively a
#     nop if BUILD_BOOT_IMG is set.
#     - MODULES_LIST=<file to list of modules> list of modules to use for
#       vendor_boot.modules.load. If this property is not set, then the default
#       modules.load is used.
#     - TRIM_UNUSED_MODULES. If set, then modules not mentioned in
#       modules.load are removed from initramfs. If MODULES_LIST is unset, then
#       having this variable set effectively becomes a no-op.
#     - MODULES_BLOCKLIST=<modules.blocklist file> A list of modules which are
#       blocked from being loaded. This file is copied directly to staging directory,
#       and should be in the format:
#       blocklist module_name
#     - MKBOOTIMG_EXTRA_ARGS=<space-delimited mkbootimg arguments>
#       Refer to: ./mkbootimg.py --help
#     If BOOT_IMAGE_HEADER_VERSION >= 4, the following variable can be defined:
#     - VENDOR_BOOTCONFIG=<string of bootconfig parameters>
#     - INITRAMFS_VENDOR_RAMDISK_FRAGMENT_NAME=<name of the ramdisk fragment>
#       If BUILD_INITRAMFS is specified, then build the .ko and depmod files as
#       a standalone vendor ramdisk fragment named as the given string.
#     - INITRAMFS_VENDOR_RAMDISK_FRAGMENT_MKBOOTIMG_ARGS=<mkbootimg arguments>
#       Refer to: https://source.android.com/devices/bootloader/partitions/vendor-boot-partitions#mkbootimg-arguments
#
#   VENDOR_RAMDISK_CMDS
#     When building vendor boot image, VENDOR_RAMDISK_CMDS enables the build
#     config file to specify command(s) for further altering the prebuilt vendor
#     ramdisk binary. For example, the build config file could add firmware files
#     on the vendor ramdisk (lib/firmware) for testing purposes.
#
#   SKIP_UNPACKING_RAMDISK
#     If set, skip unpacking the vendor ramdisk and copy it as is, without
#     modifications, into the boot image. Also skip the mkbootfs step.
#
#   AVB_SIGN_BOOT_IMG
#     if defined, sign the boot image using the AVB_BOOT_KEY. Refer to
#     https://android.googlesource.com/platform/external/avb/+/master/README.md
#     for details on what Android Verified Boot is and how it works. The kernel
#     prebuilt tool `avbtool` is used for signing.
#
#     When AVB_SIGN_BOOT_IMG is defined, the following flags need to be
#     defined:
#     - AVB_BOOT_PARTITION_SIZE=<size of the boot partition in bytes>
#     - AVB_BOOT_KEY=<absolute path to the key used for signing> The Android test
#       key has been uploaded to the kernel/prebuilts/build-tools project here:
#       https://android.googlesource.com/kernel/prebuilts/build-tools/+/refs/heads/master/linux-x86/share/avb
#     - AVB_BOOT_ALGORITHM=<AVB_BOOT_KEY algorithm used> e.g. SHA256_RSA2048. For the
#       full list of supported algorithms, refer to the enum AvbAlgorithmType in
#       https://android.googlesource.com/platform/external/avb/+/refs/heads/master/libavb/avb_crypto.h
#     - AVB_BOOT_PARTITION_NAME=<name of the boot partition>
#       (defaults to BOOT_IMAGE_FILENAME without extension; by default, "boot")
#
#   BUILD_INITRAMFS
#     if set to "1", build a ramdisk containing all .ko files and resulting
#     depmod artifacts
#
#   BUILD_SYSTEM_DLKM
#     if set to "1", build a system_dlkm.img containing all signed GKI modules
#     and resulting depmod artifacts. GKI build exclusive; DO NOT USE with device
#     build configs files.
#
#   SYSTEM_DLKM_MODULES_LIST
#     location (relative to the repo root directory) of an optional file
#     containing the list of kernel modules which shall be copied into a
#     system_dlkm partition image.
#
#   MODULES_OPTIONS
#     A /lib/modules/modules.options file is created on the ramdisk containing
#     the contents of this variable, lines should be of the form: options
#     <modulename> <param1>=<val> <param2>=<val> ...
#
#   MODULES_ORDER
#     location of an optional file containing the list of modules that are
#     expected to be built for the current configuration, in the modules.order
#     format, relative to the kernel source tree.
#
#   GKI_MODULES_LIST
#     location of an optional file containing the list of GKI modules, relative
#     to the kernel source tree. This should be set in downstream builds to
#     ensure the ABI tooling correctly differentiates vendor/OEM modules and GKI
#     modules. This should not be set in the upstream GKI build.config.
#
#   VENDOR_DLKM_MODULES_LIST
#     location (relative to the repo root directory) of an optional file
#     containing the list of kernel modules which shall be copied into a
#     vendor_dlkm partition image. Any modules passed into MODULES_LIST which
#     become part of the vendor_boot.modules.load will be trimmed from the
#     vendor_dlkm.modules.load.
#
#   VENDOR_DLKM_MODULES_BLOCKLIST
#     location (relative to the repo root directory) of an optional file
#     containing a list of modules which are blocked from being loaded. This
#     file is copied directly to the staging directory and should be in the
#     format: blocklist module_name
#
#   VENDOR_DLKM_PROPS
#     location (relative to the repo root directory) of a text file containing
#     the properties to be used for creation of a vendor_dlkm image
#     (filesystem, partition size, etc). If this is not set (and
#     VENDOR_DLKM_MODULES_LIST is), a default set of properties will be used
#     which assumes an ext4 filesystem and a dynamic partition.
#
#   LZ4_RAMDISK
#     if set to "1", any ramdisks generated will be lz4 compressed instead of
#     gzip compressed.
#
#   LZ4_RAMDISK_COMPRESS_ARGS
#     Command line arguments passed to lz4 command to control compression
#     level (defaults to "-12 --favor-decSpeed"). For iterative kernel
#     development where faster compression is more desirable than a high
#     compression ratio, it can be useful to control the compression ratio.
#
#   TRIM_NONLISTED_KMI
#     if set to "1", enable the CONFIG_UNUSED_KSYMS_WHITELIST kernel config
#     option to un-export from the build any un-used and non-symbol-listed
#     (as per KMI_SYMBOL_LIST) symbol.
#
#   KMI_SYMBOL_LIST_STRICT_MODE
#     if set to "1", add a build-time check between the KMI_SYMBOL_LIST and the
#     KMI resulting from the build, to ensure they match 1-1.
#
#   KMI_STRICT_MODE_OBJECTS
#     optional list of objects to consider for the KMI_SYMBOL_LIST_STRICT_MODE
#     check. Defaults to 'vmlinux'.
#
#   GKI_DIST_DIR
#     optional directory from which to copy GKI artifacts into DIST_DIR
#
#   GKI_BUILD_CONFIG
#     If set, builds a second set of kernel images using GKI_BUILD_CONFIG to
#     perform a "mixed build." Mixed builds creates "GKI kernel" and "vendor
#     modules" from two different trees. The GKI kernel tree can be the Android
#     Common Kernel and the vendor modules tree can be a complete vendor kernel
#     tree. GKI_DIST_DIR (above) is set and the GKI kernel's DIST output is
#     copied to this DIST output. This allows a vendor tree kernel image to be
#     effectively discarded and a GKI kernel Image used from an Android Common
#     Kernel. Any variables prefixed with GKI_ are passed into into the GKI
#     kernel's build.sh invocation.
#
#     This is incompatible with GKI_PREBUILTS_DIR.
#
#   GKI_PREBUILTS_DIR
#     If set, copies an existing set of GKI kernel binaries to the DIST_DIR to
#     perform a "mixed build," as with GKI_BUILD_CONFIG. This allows you to
#     skip the additional compilation, if interested.
#
#     This is incompatible with GKI_BUILD_CONFIG.
#
#     The following must be present:
#       vmlinux
#       System.map
#       vmlinux.symvers
#       modules.builtin
#       modules.builtin.modinfo
#       Image.lz4
#
#   BUILD_DTBO_IMG
#     if defined, package a dtbo.img using the provided *.dtbo files. The image
#     will be created under the DIST_DIR.
#
#     The following flags control how the dtbo image is packaged.
#     MKDTIMG_DTBOS=<list of *.dtbo files> used to package the dtbo.img. The
#     *.dtbo files should be compiled by kbuild via the "make dtbs" command or
#     by adding each *.dtbo to the MAKE_GOALS.
#     MKDTIMG_FLAGS=<list of flags> to be passed to mkdtimg.
#
#   DTS_EXT_DIR
#     Set this variable to compile an out-of-tree device tree. The value of
#     this variable is set to the kbuild variable "dtstree" which is used to
#     compile the device tree, it will be used to lookup files in FILES as well.
#     If this is set, then it's likely the dt-bindings are out-of-tree as well.
#     So be sure to set DTC_INCLUDE in the BUILD_CONFIG file to the include path
#     containing the dt-bindings.
#
#     Update the MAKE_GOALS variable and the FILES variable to specify
#     the target dtb files with the path under ${DTS_EXT_DIR}, so that they
#     could be compiled and copied to the dist directory. Like the following:
#         DTS_EXT_DIR=common-modules/virtual-device
#         MAKE_GOALS="${MAKE_GOALS} k3399-rock-pi-4b.dtb"
#         FILES="${FILES} rk3399-rock-pi-4b.dtb"
#     where the dts file path is
#     common-modules/virtual-device/rk3399-rock-pi-4b.dts
#
#   BUILD_VENDOR_KERNEL_BOOT
#     if set to "1", build a vendor_kernel_boot for kernel artifacts, such as kernel modules.
#     Since we design this partition to isolate kernel artifacts from vendor_boot image,
#     vendor_boot would not be repack and built if we set this property to "1".
#
#   BUILD_GKI_CERTIFICATION_TOOLS
#     if set to "1", build a gki_certification_tools.tar.gz, which contains
#     the utilities used to certify GKI boot-*.img files.
#
#   BUILD_GKI_ARTIFACTS
#     if defined when $ARCH is arm64, build a boot-img.tar.gz archive that
#     contains several GKI boot-*.img files with different kernel compression
#     format. Each boot image contains a boot header v4 as per the format
#     defined by https://source.android.com/devices/bootloader/boot-image-header
#     , followed by a kernel (no ramdisk). The kernel binaries are from
#     ${DIST_DIR}, e.g., Image, Image.gz, Image.lz4, etc. Individual
#     boot-*.img files are also generated, e.g., boot.img, boot-gz.img and
#     boot-lz4.img. It is expected that all components are present in
#     ${DIST_DIR}.
#
#     if defined when $ARCH is x86_64, build a boot.img with the kernel image,
#     bzImage under ${DIST_DIR}. No boot-img.tar.gz will be generated because
#     currently there is only a x86_64 GKI image: the bzImage.
#
#     if defined when $ARCH is neither arm64 nor x86_64, print an error message
#     then exist the build process.
#
#     When the BUILD_GKI_ARTIFACTS flag is defined, the following flags also
#     need to be defined.
#     - MKBOOTIMG_PATH=<path to the mkbootimg.py script which builds boot.img>
#       (defaults to tools/mkbootimg/mkbootimg.py)
#     - BUILD_GKI_BOOT_IMG_SIZE=<The size of the boot.img to build>
#       This is required, and the file ${DIST_DIR}/Image must exist.
#     - BUILD_GKI_BOOT_IMG_GZ_SIZE=<The size of the boot-gz.img to build>
#       This is required only when ${DIST_DIR}/Image.gz is present.
#     - BUILD_GKI_BOOT_IMG_LZ4_SIZE=<The size of the boot-lz4.img to build>
#       This is required only when ${DIST_DIR}/Image.lz4 is present.
#     - BUILD_GKI_BOOT_IMG_<COMPRESSION>_SIZE=<The size of the
#       boot-${compression}.img to build> This is required
#       only when ${DIST_DIR}/Image.${compression} is present.

# Note: For historic reasons, internally, OUT_DIR will be copied into
# COMMON_OUT_DIR, and OUT_DIR will be then set to
# ${COMMON_OUT_DIR}/${KERNEL_DIR}. This has been done to accommodate existing
# build.config files that expect ${OUT_DIR} to point to the output directory of
# the kernel build.
#
# The kernel is built in ${COMMON_OUT_DIR}/${KERNEL_DIR}.
# Out-of-tree modules are built in ${COMMON_OUT_DIR}/${EXT_MOD} where
# ${EXT_MOD} is the path to the module source code.

#################################################################################
# SCRIPT EXECUTION MODE AND ERROR HANDLING
#
# PURPOSE:
#   - `set -e` causes the script to exit immediately if any command exits with
#     a non-zero status code (error)
#   - This ensures build failures don't silently continue
#   - Prevents cascading failures where later stages use corrupted artifacts
#     from earlier failed stages
#
# BEHAVIOR:
#   When a command fails:
#   1. Script exits with that command's error code
#   2. No further commands execute
#   3. User sees exact point of failure
#
# KLEAF CONTEXT:
#   Bazel's strict error handling mirrors this pattern
#   Mixed builds require predictable, immediate failure on errors
#################################################################################
set -e

#################################################################################
# ENVIRONMENT PRESERVATION FOR MIXED BUILDS
#
# PURPOSE:
#   Save the current shell environment to restore it later for recursive
#   build.sh invocations during mixed builds (GKI + vendor device kernel)
#
# WHY THIS IS NEEDED:
#   - Mixed builds invoke build.sh recursively with different BUILD_CONFIG
#   - First invocation sets up GKI build environment (e.g., GKI_BUILD_CONFIG)
#   - When building device kernel, we need clean environment without GKI vars
#   - Using `export -p` captures all current variables and their values
#   - tmpfile stores this so we can restore with `source` later
#
# KLEAF INTEGRATION:
#   Kleaf build system also manages environment isolation for parallel builds
#   This mirrors that pattern for backward compatibility with build.sh
#
# TECHNICAL DETAILS:
#   - `mktemp` creates secure temporary file with unique name in /tmp
#   - `export -p` outputs all exported variables in format: declare -x VAR=value
#   - Later restored with: source ${OLD_ENVIRONMENT}
#################################################################################
OLD_ENVIRONMENT=$(mktemp)
export -p > ${OLD_ENVIRONMENT}

#################################################################################
# ROOT DIRECTORY AND BUILD UTILITIES INITIALIZATION
#
# PURPOSE:
#   1. Determine absolute path to repository root (ROOT_DIR)
#   2. Source utility functions and environment setup
#
# EXECUTION FLOW:
#   $(dirname $(readlink -f $0))/gettop.sh
#   ├─ $(readlink -f $0) = Resolve build.sh to absolute path
#   ├─ dirname = Get directory containing build.sh
#   └─ gettop.sh = Script that walks up directory tree to find ROOT_DIR
#
# WHAT GETS SOURCED:
#   1. build/build_utils.sh
#      - rel_path() - Calculate relative paths between directories
#      - create_modules_staging() - Set up module staging directory
#      - Other utility functions for the build system
#
#   2. build/_setup_env.sh
#      - Initializes critical variables:
#        * KERNEL_DIR - Path to kernel source relative to ROOT_DIR
#        * OUT_DIR - Output directory for build artifacts
#        * DIST_DIR - Distribution directory for final artifacts
#        * ARCH - Target architecture (arm64, x86_64, etc.)
#        * DEFCONFIG - Defconfig target (e.g., defconfig)
#        * TOOL_ARGS - Tool-specific arguments (CC, LD, etc.)
#        * MAKEFLAGS - Initial make flags
#        * FILES - List of files to copy to DIST_DIR
#        * MAKE_GOALS - Targets for make (Image, modules, dtbs)
#      - Reads BUILD_CONFIG file if specified
#      - Reads BUILD_CONFIG_FRAGMENTS for optional configurations
#
# KLEAF CONTEXT:
#   _setup_env.sh is the bridge between build.sh and Kleaf's build system
#   It prepares all variables that Kleaf would handle in .bzl files
#################################################################################
export ROOT_DIR=$($(dirname $(readlink -f $0))/gettop.sh)
source "${ROOT_DIR}/build/build_utils.sh"
source "${ROOT_DIR}/build/_setup_env.sh"

#################################################################################
# KLEAF MIGRATION WARNING AND DEPRECATION HANDLING
#
# PURPOSE:
#   Display warning that build.sh is deprecated; users should migrate to Bazel
#   Attempts to convert build.sh invocation to equivalent Bazel command
#   Provides helpful guidance for migration path
#
# TECHNICAL DETAILS:
#   - Suppresses warning only if KLEAF_SUPPRESS_BUILD_SH_DEPRECATION_WARNING=1
#   - Calls convert_to_bazel.sh to generate equivalent bazel command
#   - If conversion succeeds, shows user the bazel command they should use
#   - If conversion fails, shows migration documentation link
#
# KLEAF INTEGRATION POINT:
#   This is a critical integration point between old build.sh system and new
#   Kleaf/Bazel build system. Eventually, build.sh will be removed entirely.
#   For now, it's maintained for backward compatibility.
#
# LATER: RECURSIVE BUILD HANDLING
#   Sets KLEAF_SUPPRESS_BUILD_SH_DEPRECATION_WARNING=1 so that recursive
#   invocations (like GKI builds) don't spam the same warning multiple times
#################################################################################
(
    [[ "$KLEAF_SUPPRESS_BUILD_SH_DEPRECATION_WARNING" == "1" ]] && exit 0 || true
    echo     "Inferring equivalent Bazel command..."
    bazel_command_code=0
    eq_bazel_command=$( # Capture output of bazel command conversion tool
        ${ROOT_DIR}/build/kernel/kleaf/convert_to_bazel.sh # error messages goes to stderr
    ) || bazel_command_code=$?
    echo     "*****************************************************************************" >&2
    echo     "* WARNING: build.sh is deprecated for this branch. Please migrate to Bazel.  " >&2
    echo     "*   See build/kernel/kleaf/README.md                                         " >&2
    if [[ $bazel_command_code -eq 0 ]]; then
        echo "*          Possibly equivalent Bazel command:                                " >&2
        echo "*" >&2
        echo "*   \$ $eq_bazel_command" >&2
        echo "*" >&2
    else
        echo "WARNING: Unable to infer an equivalent Bazel command.                        " >&2
    fi
    echo     "* To suppress this warning, set KLEAF_SUPPRESS_BUILD_SH_DEPRECATION_WARNING=1" >&2
    echo     "*****************************************************************************" >&2
    echo >&2
)
# Suppress deprecation warning for recursive build.sh invocation with GKI_BUILD_CONFIG
# This prevents the warning from appearing for every recursive build.sh call
export KLEAF_SUPPRESS_BUILD_SH_DEPRECATION_WARNING=1

#################################################################################
# MAKE ARGUMENTS AND STAGING DIRECTORY INITIALIZATION
#
# PURPOSE:
#   1. Capture all command-line arguments passed to build.sh
#   2. Set up parallelism for make based on CPU count
#   3. Initialize critical staging directories for modules and artifacts
#
# MAKE_ARGS:
#   - Array captures all arguments from command line (e.g., -j24 V=1)
#   - Will be passed to all `make` invocations
#   - Allows users to control build parallelism and verbosity
#
# MAKEFLAGS SETUP:
#   - `-j$(nproc)` = Enable parallel jobs equal to number of CPU cores
#   - Example: 8-core system → -j8 for maximum parallelism
#   - Significantly speeds up builds on multi-core systems
#   - Can be overridden by MAKEFLAGS environment variable
#
# STAGING DIRECTORIES:
#   These are temporary directories where build artifacts are collected
#   before being packaged into final distributions (ramdisk, boot.img, etc.)
#
#   - MODULES_STAGING_DIR: Where compiled .ko modules are installed before packing
#   - MODULES_PRIVATE_DIR: Unstripped modules for debugging purposes
#   - KERNEL_UAPI_HEADERS_DIR: User-space API headers exported by kernel
#   - INITRAMFS_STAGING_DIR: Staging area for ramdisk/initramfs packing
#   - SYSTEM_DLKM_STAGING_DIR: GKI module dynamic partition staging
#   - VENDOR_DLKM_STAGING_DIR: Vendor module dynamic partition staging
#   - MKBOOTIMG_STAGING_DIR: Boot image components staging
#
# READLINK -M:
#   - `-m` flag means "make canonical path even if file doesn't exist"
#   - Resolves symlinks and relative paths to absolute paths
#   - Ensures consistent paths for all tool invocations
#
# KLEAF CONTEXT:
#   Kleaf creates similar staging structure through Bazel rules
#   This shows the logical separation between different output stages
#################################################################################
MAKE_ARGS=( "$@" ) # Capture all command-line arguments for make
export MAKEFLAGS="-j$(nproc) ${MAKEFLAGS}" # Parallelize based on CPU count
export MODULES_STAGING_DIR=$(readlink -m ${COMMON_OUT_DIR}/staging)
export MODULES_PRIVATE_DIR=$(readlink -m ${COMMON_OUT_DIR}/private)
export KERNEL_UAPI_HEADERS_DIR=$(readlink -m ${COMMON_OUT_DIR}/kernel_uapi_headers)
export INITRAMFS_STAGING_DIR=${MODULES_STAGING_DIR}/initramfs_staging
export SYSTEM_DLKM_STAGING_DIR=${MODULES_STAGING_DIR}/system_dlkm_staging
export VENDOR_DLKM_STAGING_DIR=${MODULES_STAGING_DIR}/vendor_dlkm_staging
export MKBOOTIMG_STAGING_DIR="${MODULES_STAGING_DIR}/mkbootimg_staging"

#################################################################################
# MIXED BUILD CONFIGURATION VALIDATION
#
# PURPOSE:
#   Ensure conflicting mixed build configurations are not both specified
#
# VALIDATION RULES:
#   1. SKIP_VENDOR_BOOT and BUILD_VENDOR_BOOT_IMG are mutually exclusive
#      - SKIP_VENDOR_BOOT: Don't create vendor_boot partition
#      - BUILD_VENDOR_BOOT_IMG: Do create vendor_boot partition
#      - Cannot do both; would be contradictory
#
#   2. GKI_BUILD_CONFIG and GKI_PREBUILTS_DIR are mutually exclusive
#      - GKI_BUILD_CONFIG: Build GKI kernel from source
#      - GKI_PREBUILTS_DIR: Use pre-compiled GKI binaries
#      - Cannot do both; would waste time/resources
#################################################################################
if [ -n "${SKIP_VENDOR_BOOT}" -a -n "${BUILD_VENDOR_BOOT_IMG}" ]; then
  echo "ERROR: SKIP_VENDOR_BOOT is incompatible with BUILD_VENDOR_BOOT_IMG." >&2
  exit 1
fi

#################################################################################
# MIXED BUILD ORCHESTRATION: GKI + VENDOR DEVICE KERNEL
#
# PURPOSE:
#   When GKI_BUILD_CONFIG is specified, perform a "mixed build":
#   1. Build GKI kernel from separate source tree
#   2. Build device kernel modules against GKI kernel
#   3. Combine GKI kernel + vendor modules for final image
#
# ARCHITECTURAL OVERVIEW:
#   Traditional Build:
#   ┌─ Device Kernel Tree
#   ├─ Compile vmlinux + modules
#   └─ Package into boot image
#
#   Mixed Build:
#   ┌─ GKI Kernel Tree           ┌─ Device Kernel Tree
#   ├─ Compile vmlinux            ├─ Compile modules against
#   ├─ Stage GKI artifacts        │  GKI kernel symvers
#   └─ Set KBUILD_MIXED_TREE     └─ Create final image with
#                                     GKI kernel + device modules
#
# IMPLEMENTATION:
#   1. Check incompatibility with GKI_PREBUILTS_DIR
#   2. Set up separate output directories (GKI_OUT_DIR, GKI_DIST_DIR)
#   3. Validate that MAKE_GOALS doesn't include kernel compilation
#      (device kernel should only compile modules)
#   4. Build recursive environment setup
#   5. Invoke build.sh recursively with GKI build config
#   6. Copy GKI artifacts to device DIST_DIR
#   7. Set KBUILD_MIXED_TREE to tell kernel build about GKI artifacts
#
# KLEAF CONTEXT:
#   This is a critical feature for Android's GKI strategy:
#   - GKI (Generic Kernel Image) is built by Google/platform team
#   - Vendors/OEMs add their custom modules on top
#   - Allows faster updates and better compatibility
#################################################################################
if [ -n "${GKI_BUILD_CONFIG}" ]; then
  # Check for incompatible configuration: can't use both GKI_BUILD_CONFIG and prebuilts
  if [ -n "${GKI_PREBUILTS_DIR}" ]; then
      echo "ERROR: GKI_BUILD_CONFIG is incompatible with GKI_PREBUILTS_DIR." >&2
      exit 1
  fi

  # Set up separate output directory for GKI kernel build
  GKI_OUT_DIR=${GKI_OUT_DIR:-${COMMON_OUT_DIR}/gki_kernel}
  GKI_DIST_DIR=${GKI_DIST_DIR:-${GKI_OUT_DIR}/dist}

  # Validate that device kernel isn't trying to compile kernel binaries
  # In mixed build, only GKI kernel should compile vmlinux/Image, not device kernel
  if [[ "${MAKE_GOALS}" =~ image|Image|vmlinux ]]; then
    echo " Compiling Image and vmlinux in device kernel is not supported in mixed build mode"
    exit 1
  fi

  #############################################################################
  # RECURSIVE BUILD ENVIRONMENT CONSTRUCTION FOR GKI KERNEL
  #
  # PURPOSE:
  #   Set up environment variables for recursive build.sh invocation to build GKI
  #   This includes inheritance of certain settings and stripping device-specific
  #   settings that shouldn't apply to GKI build
  #
  # ENVIRONMENT INHERITANCE:
  #   Variables inherited from device build unless explicitly overridden:
  #   - SKIP_MRPROPER: Whether to skip mrproper (cleanup config/objects)
  #   - LTO: Link-Time Optimization setting (none, thin, full)
  #   - SKIP_DEFCONFIG: Whether to skip defconfig generation
  #   - SKIP_IF_VERSION_MATCHES: Skip build if version unchanged
  #
  # VARIABLES EXPLICITLY CLEARED FOR GKI BUILD:
  #   - EXT_MODULES: Device-specific external modules
  #   - GKI_BUILD_CONFIG: Prevent recursive mixed builds
  #   - KCONFIG_EXT_PREFIX: Device-specific external kconfig
  #
  # GKI_* VARIABLE TRANSFORMATION:
  #   Variables starting with GKI_ are passed to GKI build without prefix:
  #   Example: GKI_BUILD_CONFIG=common/build.config.gki.aarch64
  #            → BUILD_CONFIG=common/build.config.gki.aarch64 (in GKI build)
  #
  # PROCESS:
  #   1. export -p shows current environment in shell declaration format
  #   2. sed extracts lines matching pattern GKI_<variable>=<value>
  #   3. Transforms them to <variable>=<value> (removes GKI_ prefix)
  #   4. tr '\n' ' ' converts newlines to spaces for array syntax
  #   5. Result is added to GKI_ENVIRON array
  #
  # KLEAF PARALLEL:
  #   Kleaf's --nonstamp_host_forced_args and environment handling achieves
  #   similar environment isolation for different build targets
  #############################################################################
  
  # Initialize GKI environment with core settings inherited from device build
  # Inherit SKIP_MRPROPER, LTO, SKIP_DEFCONFIG unless overridden by GKI_* variables
  GKI_ENVIRON=(
    "SKIP_MRPROPER=${SKIP_MRPROPER}"         # Preserve cleanup preference
    "LTO=${LTO}"                             # Preserve LTO setting
    "SKIP_DEFCONFIG=${SKIP_DEFCONFIG}"       # Preserve defconfig skipping
    "SKIP_IF_VERSION_MATCHES=${SKIP_IF_VERSION_MATCHES}" # Preserve version check
  )
  
  # Explicitly unset EXT_MODULES since they should be compiled against the device kernel
  # GKI kernel should not include device-specific external modules
  GKI_ENVIRON+=(
    "EXT_MODULES="  # Empty string = unset this variable for GKI build
  )
  
  # Explicitly unset GKI_BUILD_CONFIG in case it was set by in the old environment
  # This prevents recursive mixed builds (GKI_BUILD_CONFIG within GKI build)
  # e.g. GKI_BUILD_CONFIG=common/build.config.gki.x86 ./build/build.sh would cause
  # gki build recursively - we must prevent this
  GKI_ENVIRON+=(
    "GKI_BUILD_CONFIG="  # Empty string = unset this variable
  )
  
  # Explicitly unset KCONFIG_EXT_PREFIX in case it was set by the older environment.
  # Device-specific external Kconfig should not apply to generic GKI kernel
  GKI_ENVIRON+=(
    "KCONFIG_EXT_PREFIX="  # Empty string = unset this variable
  )
  
  # Extract any variables prefixed with GKI_ and transform them
  # Any variables prefixed with GKI_ get set without that prefix in the GKI build environment
  # e.g. GKI_BUILD_CONFIG=common/build.config.gki.aarch64 -> BUILD_CONFIG=common/build.config.gki.aarch64
  # The sed pattern: -E 's/.* GKI_([^=]+=.*)$/\1/p'
  #   - Matches lines with " GKI_" (space + GKI_)
  #   - Captures group: everything from first letter after GKI_ to end of line
  #   - Replaces with captured group (effectively removes "export -p " prefix and GKI_ prefix)
  # The tr '\n' ' ' converts multiple lines into space-separated string
  GKI_ENVIRON+=(
    $(export -p | sed -n -E -e 's/.* GKI_([^=]+=.*)$/\1/p' | tr '\n' ' ')
  )
  
  # Set GKI build output directories
  GKI_ENVIRON+=(
    "OUT_DIR=${GKI_OUT_DIR}"    # GKI output directory
    "DIST_DIR=${GKI_DIST_DIR}"  # GKI distribution directory
  )
  
  #############################################################################
  # RECURSIVE BUILD.SH INVOCATION FOR GKI KERNEL
  #
  # PURPOSE:
  #   Execute build.sh recursively to build GKI kernel with isolated environment
  #
  # COMMAND BREAKDOWN:
  #   env -i bash -c "..."
  #   ├─ env -i: Start with empty environment (no inherited variables)
  #   │  This ensures clean isolation for GKI build
  #   │
  #   ├─ bash -c: Execute shell commands
  #   │
  #   └─ Command sequence:
  #      1. source ${OLD_ENVIRONMENT}
  #         Restore variables saved before GKI build setup
  #         This gives us original build environment
  #      2. rm -f ${OLD_ENVIRONMENT}
  #         Clean up temporary file since we've sourced it
  #      3. export ${GKI_ENVIRON[*]}
  #         Export GKI environment variables constructed above
  #      4. ./build/build.sh $*
  #         Invoke build.sh recursively with original command-line args
  #         E.g., if called as: build/build.sh -j24 V=1
  #         GKI build will also get: ./build/build.sh -j24 V=1
  #
  # ERROR HANDLING:
  #   - || exit 1: If GKI build fails, exit immediately with error code
  #   - Prevents device kernel build from proceeding with missing GKI artifacts
  #
  # EXAMPLE EXECUTION:
  #   Original: GKI_BUILD_CONFIG=gki/build.config ./build/build.sh -j24
  #   Becomes:  env -i bash -c "
  #              source /tmp/tmpXXXXXX     # Restore original env
  #              export BUILD_CONFIG=gki/build.config
  #              export OUT_DIR=out/gki_kernel
  #              ./build/build.sh -j24     # Build GKI with clean config
  #             "
  #
  # KLEAF PERSPECTIVE:
  #   This is similar to how Kleaf runs subtargets with restricted environments
  #   using --nonstamp_host_forced_args and other environment flags
  #############################################################################
  ( env -i bash -c "source ${OLD_ENVIRONMENT}; rm -f ${OLD_ENVIRONMENT}; export ${GKI_ENVIRON[*]} ; ./build/build.sh $*" ) || exit 1

  # After GKI build completes, we need to tell the device kernel build about
  # the GKI artifacts it should use when building modules
  # Dist dir must have vmlinux.symvers, modules.builtin.modinfo, modules.builtin
  # These are used by kbuild to understand what symbols are already exported
  # by the GKI kernel, so device modules don't try to export them
  MAKE_ARGS+=(
    "KBUILD_MIXED_TREE=$(readlink -m ${GKI_DIST_DIR})"
  )
else
  # No mixed build, just clean up the temporary environment file
  rm -f ${OLD_ENVIRONMENT}
fi

#################################################################################
# EXTERNAL KCONFIG CONFIGURATION (KCONFIG_EXT_PREFIX)
#
# PURPOSE:
#   Handle external Kconfig files for device-specific kernel configuration
#   This allows additional Kconfig options defined outside kernel source tree
#
# USE CASE:
#   Vendor/OEM kernel may need custom configuration options not in mainline
#   KCONFIG_EXT_PREFIX points to directory containing Kconfig.ext with these
#
# PROCESS:
#   1. Ensure KCONFIG_EXT_PREFIX ends with "/" (it's a directory prefix)
#   2. Try to find Kconfig.ext in two possible locations:
#      - Relative to ROOT_DIR: ${ROOT_DIR}/${KCONFIG_EXT_PREFIX}Kconfig.ext
#      - Relative to KERNEL_DIR: ${KERNEL_DIR}/${KCONFIG_EXT_PREFIX}Kconfig.ext
#   3. Recalculate to be relative to KERNEL_DIR if found relative to ROOT_DIR
#   4. Pass to make as part of MAKE_ARGS
#
# KCONFIG_EXT_PREFIX NORMALIZATION:
#   - Users can specify relative to either ROOT_DIR or KERNEL_DIR
#   - Script detects which one and normalizes to KERNEL_DIR-relative
#   - Makes it more forgiving for users
#
# TECHNICAL DETAIL:
#   The check "=~ \/$" uses regex to test if last character is "/"
#   If not, we append it to ensure valid directory prefix
#################################################################################
if [ -n "${KCONFIG_EXT_PREFIX}" ]; then
  # Since this is a prefix, make sure it ends with "/" for consistency
  if [[ ! "${KCONFIG_EXT_PREFIX}" =~ \/$ ]]; then
    KCONFIG_EXT_PREFIX=${KCONFIG_EXT_PREFIX}/
  fi

  # KCONFIG_EXT_PREFIX needs to be relative to KERNEL_DIR but we allow one to set
  # it relative to ROOT_DIR for ease of use. So figure out what was used.
  if [ -f "${ROOT_DIR}/${KCONFIG_EXT_PREFIX}Kconfig.ext" ]; then
    # KCONFIG_EXT_PREFIX is currently relative to ROOT_DIR. So recalculate it to be
    # relative to KERNEL_DIR using the rel_path utility
    KCONFIG_EXT_PREFIX=$(rel_path ${ROOT_DIR}/${KCONFIG_EXT_PREFIX} ${KERNEL_DIR})
  elif [ ! -f "${KERNEL_DIR}/${KCONFIG_EXT_PREFIX}Kconfig.ext" ]; then
    # Neither location has the file, so error out
    echo "Couldn't find the Kconfig.ext in ${KCONFIG_EXT_PREFIX}" >&2
    exit 1
  fi

  # Ensure the result ends with "/" for consistency as a directory prefix
  if [[ ! "${KCONFIG_EXT_PREFIX}" =~ \/$ ]]; then
    KCONFIG_EXT_PREFIX=${KCONFIG_EXT_PREFIX}/
  fi
  
  # Pass to make so kbuild can find the external Kconfig
  MAKE_ARGS+=(
    "KCONFIG_EXT_PREFIX=${KCONFIG_EXT_PREFIX}"
  )
fi

#################################################################################
# OUT-OF-TREE DEVICE TREE CONFIGURATION (DTS_EXT_DIR)
#
# PURPOSE:
#   Support compiling device tree sources from outside kernel source tree
#   This allows vendors to maintain device tree separately from kernel
#
# USE CASE:
#   Vendor may have device-specific device trees in separate repository
#   Can compile them alongside kernel without modifying kernel source
#
# PROCESS:
#   1. Only process if DTS_EXT_DIR is set
#   2. Check if MAKE_GOALS includes device tree targets (dtbs, *.dtb, *.dtbo)
#   3. If yes, determine if DTS_EXT_DIR is relative to ROOT_DIR or KERNEL_DIR
#   4. Normalize to be KERNEL_DIR-relative (like KCONFIG_EXT_PREFIX)
#   5. Pass as "dtstree" kbuild variable to make
#
# KBUILD INTEGRATION:
#   - "dtstree" is a standard kbuild variable
#   - Tells kbuild where to find device tree source files
#   - Also used by script to find built .dtb files for copying to DIST_DIR
#
# WHY THIS MATTERS:
#   Device tree is critical for Android devices:
#   - Describes hardware layout, pinouts, drivers, etc.
#   - Different OEMs have vastly different hardware
#   - Needs frequent updates independent of kernel
#   - Out-of-tree DT allows parallel development
#################################################################################
if [ -n "${DTS_EXT_DIR}" ]; then
  # Only process DTS_EXT_DIR if MAKE_GOALS includes device tree compilation
  # Check if MAKE_GOALS has patterns matching dtbs, .dtb or .dtbo files
  if [[ "${MAKE_GOALS}" =~ dtbs|\\.dtb|\\.dtbo ]]; then
    # DTS_EXT_DIR needs to be relative to KERNEL_DIR but we allow one to set
    # it relative to ROOT_DIR for ease of use. So figure out what was used.
    if [ -d "${ROOT_DIR}/${DTS_EXT_DIR}" ]; then
      # DTS_EXT_DIR is currently relative to ROOT_DIR. So recalculate it to be
      # relative to KERNEL_DIR
      DTS_EXT_DIR=$(rel_path ${ROOT_DIR}/${DTS_EXT_DIR} ${KERNEL_DIR})
    elif [ ! -d "${KERNEL_DIR}/${DTS_EXT_DIR}" ]; then
      # Directory doesn't exist in either location
      echo "Couldn't find the dtstree -- ${DTS_EXT_DIR}" >&2
      exit 1
    fi
    # Pass to kbuild via MAKE_ARGS
    MAKE_ARGS+=(
      "dtstree=${DTS_EXT_DIR}"  # kbuild variable for device tree location
    )
  fi
fi

#################################################################################
# CHANGE TO ROOT DIRECTORY
#
# PURPOSE:
#   Ensure all relative paths resolve correctly
#   Build system expects to run from repository root
#
# CONTEXT:
#   Users can invoke build.sh from anywhere in the repository
#   This cd ensures consistent working directory for all operations
#################################################################################
cd ${ROOT_DIR}

#################################################################################
# SKIP_IF_VERSION_MATCHES OPTIMIZATION
#
# PURPOSE:
#   Early exit optimization for incremental/CI builds
#   Skip compilation if kernel version hasn't changed
#
# USE CASE:
#   In mixed builds or CI systems, GKI kernel may not change frequently
#   Can skip expensive rebuild if built vmlinux already has correct version
#   Saves significant CI time and resources
#
# LOGIC:
#   1. Only proceed if SKIP_IF_VERSION_MATCHES flag is set
#   2. Check if vmlinux already exists in DIST_DIR
#   3. Get expected kernel version by running: make kernelrelease
#   4. Extract version string from vmlinux binary using grep
#   5. Compare: if versions match and "dirty" flag absent, skip build
#   6. "dirty" check is important: uncommitted changes mean rebuild needed
#
# GREP OPTIMIZATION:
#   Split into two steps for performance:
#   1. grep -o -a -m1 "Linux version [^ ]* "
#      - -o: only output matching part
#      - -a: treat file as text (binary safe)
#      - -m1: stop after first match (fast)
#      - Pattern finds "Linux version X.Y.Z "
#   2. grep -q " ${kernelversion} "
#      - -q: quiet mode (no output, just return code)
#      - Check if version string exists
#
# KLEAF CONTEXT:
#   Kleaf's caching and build graph analysis achieves similar skip optimization
#   For build.sh, this manual check provides fast-path for common scenario
#################################################################################
if [ -n "${SKIP_IF_VERSION_MATCHES}" ]; then
  if [ -f "${DIST_DIR}/vmlinux" ]; then
    # Get kernel version that would be built
    kernelversion="$(cd ${KERNEL_DIR} && make -s ${TOOL_ARGS} O=${OUT_DIR} kernelrelease)"
    # Split grep into 2 steps. "Linux version" will always be towards top and fast to find. Don't
    # need to search the entire vmlinux for it
    if [[ ! "$kernelversion" =~ .*dirty.* ]] && \
       grep -o -a -m1 "Linux version [^ ]* " ${DIST_DIR}/vmlinux | grep -q " ${kernelversion} " ; then
      # Version matches and is not dirty, skip entire build
      echo "========================================================"
      echo " Skipping build because kernel version matches ${kernelversion}"
      exit 0
    fi
  fi
fi

#################################################################################
# OUTPUT DIRECTORY CREATION
#
# PURPOSE:
#   Create output and distribution directories before build begins
#   These directories store all build artifacts
#
# TIMING:
#   Done after all preliminary checks but before actual compilation
#   Ensures directories exist for all build phases
#
# MKDIR -P:
#   -p flag creates parent directories as needed
#   No error if directories already exist
#################################################################################
mkdir -p ${OUT_DIR} ${DIST_DIR}

#################################################################################
# GKI PREBUILTS COPYING (Alternative to GKI_BUILD_CONFIG)
#
# PURPOSE:
#   Instead of building GKI kernel from source, use pre-built GKI artifacts
#   Faster alternative when GKI binaries already available
#
# USE CASE:
#   CI/Release builds where GKI already built on separate infrastructure
#   Allows vendor/OEM to skip GKI recompilation, just add own modules
#   Typical: vendor uses GKI from last build, adds latest vendor modules
#
# REQUIRED ARTIFACTS:
#   The following files must exist in GKI_PREBUILTS_DIR:
#   - vmlinux: Unstripped kernel binary with debug symbols
#   - System.map: Kernel symbol table
#   - vmlinux.symvers: Module symbol exports from vmlinux
#   - modules.builtin: List of modules built into kernel
#   - modules.builtin.modinfo: Module metadata for built-in modules
#   - Image.lz4: Compressed kernel image
#
# COPY LOGIC:
#   1. cmp -s: Compare files silently (exit code only, no output)
#   2. If file differs: cp -v copies with verbose output
#   3. If file identical: skip copy to save time
#   4. Implements smart incremental copy
#
# KBUILD_MIXED_TREE:
#   Same variable used with GKI_BUILD_CONFIG path
#   Points kbuild to pre-built GKI artifacts
#################################################################################
if [ -n "${GKI_PREBUILTS_DIR}" ]; then
  echo "========================================================"
  echo " Copying GKI prebuilts"
  GKI_PREBUILTS_DIR=$(readlink -m ${GKI_PREBUILTS_DIR})
  if [ ! -d "${GKI_PREBUILTS_DIR}" ]; then
    echo "ERROR: ${GKI_PREBULTS_DIR} does not exist." >&2
    exit 1
  fi
  for file in ${GKI_PREBUILTS_DIR}/*; do
    filename=$(basename ${file})
    # Only copy if file doesn't already exist in DIST_DIR or has different content
    if ! $(cmp -s ${file} ${DIST_DIR}/${filename}); then
      cp -v ${file} ${DIST_DIR}/${filename}
    fi
  done
  # Tell kbuild where to find GKI symbols and build information
  MAKE_ARGS+=(
    "KBUILD_MIXED_TREE=${GKI_PREBUILTS_DIR}"
  )
fi

#################################################################################
# MRPROPER: CLEAN KERNEL BUILD TREE
#
# PURPOSE:
#   Remove old build artifacts and configuration state from kernel source
#   Ensures clean build state, prevents stale object files or configs
#
# WHAT MRPROPER DOES:
#   - Removes all generated files (.o, .ko, etc.)
#   - Removes old .config file
#   - Removes build dependencies (.*.cmd files)
#   - Resets kernel tree to clean source state
#
# WHEN SKIPPED:
#   SKIP_MRPROPER=1 allows incremental builds
#   Faster for development but risky if codebase changed
#   Not recommended except for iterative local development
#
# MAKE_ARGS USAGE:
#   "${MAKE_ARGS[@]}" passes all command-line arguments
#   This includes things like: -j24 V=1 EXTRA_FLAGS=...
#
# SET -X / SET +X:
#   set -x: Echo each command before executing (verbose)
#   set +x: Turn off verbose mode
#   Helps user see exactly what build is doing
#################################################################################
echo "========================================================"
echo " Setting up for build"
if [ "${SKIP_MRPROPER}" != "1" ] ; then
  set -x # Enable command echo
  (cd ${KERNEL_DIR} && make ${TOOL_ARGS} O=${OUT_DIR} "${MAKE_ARGS[@]}" mrproper)
  set +x # Disable command echo
fi

#################################################################################
# PRE-DEFCONFIG COMMANDS
#
# PURPOSE:
#   Execute user-defined commands before kernel configuration
#   Allows vendor to patch sources, update configs, etc. before defconfig
#
# USE CASE:
#   Apply vendor-specific patches to source code
#   Generate Kconfig.ext with vendor options
#   Set up build environment
#
# EVAL:
#   eval allows running arbitrary commands from string
#   Users can set complex commands with pipes, redirects, etc.
#################################################################################
if [ -n "${PRE_DEFCONFIG_CMDS}" ]; then
  echo "========================================================"
  echo " Running pre-defconfig command(s):"
  set -x
  eval ${PRE_DEFCONFIG_CMDS}
  set +x
fi

#################################################################################
# DEFCONFIG: GENERATE KERNEL CONFIGURATION FROM DEFAULTS
#
# PURPOSE:
#   Generate initial .config file from defconfig target
#   This creates default kernel configuration based on architecture/SOC
#
# TYPICAL DEFCONFIG VALUES:
#   - "defconfig": Standard defconfig for architecture
#   - "gki_defconfig": Google's Generic Kernel Image defconfig
#   - "vendor_defconfig": Vendor-specific defconfig
#
# DEFCONFIG PROCESS:
#   1. Reads arch/ARCH/configs/${DEFCONFIG}.config (if exists)
#   2. Applies architecture-specific defaults
#   3. Creates .config in OUT_DIR ready for modification
#
# PROCESS FLOW:
#   1. Run defconfig to generate initial .config
#   2. Run POST_DEFCONFIG_CMDS to modify it (if provided)
#   3. Later stages will modify specific options (LTO, KMI, etc.)
#
# SKIP_DEFCONFIG:
#   If set, skips this step - assumes .config already exists
#   Used when .config was previously generated/hand-edited
#################################################################################
if [ "${SKIP_DEFCONFIG}" != "1" ] ; then
  set -x
  (cd ${KERNEL_DIR} && make ${TOOL_ARGS} O=${OUT_DIR} "${MAKE_ARGS[@]}" ${DEFCONFIG})
  set +x

  #############################################################################
  # POST-DEFCONFIG COMMANDS
  #
  # PURPOSE:
  #   Execute user-defined commands after defconfig generation
  #   Allow vendor to customize kernel configuration
  #
  # USE CASE:
  #   Apply vendor-specific kernel config changes
  #   Disable security features for testing
  #   Enable vendor-specific drivers or features
  #
  # TYPICAL COMMANDS:
  #   Using scripts/config utility:
  #   - Enable CONFIG_VENDOR_FOO: scripts/config -e CONFIG_VENDOR_FOO
  #   - Disable CONFIG_BAR: scripts/config -d CONFIG_BAR
  #   - Set string value: scripts/config --set-str CONFIG_PARAM value
  #############################################################################
  if [ -n "${POST_DEFCONFIG_CMDS}" ]; then
    echo "========================================================"
    echo " Running pre-make command(s):"
    set -x
    eval ${POST_DEFCONFIG_CMDS}
    set +x
  fi
fi

#################################################################################
# LINK-TIME OPTIMIZATION (LTO) CONFIGURATION
#
# PURPOSE:
#   Programmatically set LTO mode in kernel configuration
#   Three modes available: full (best optimization, slow), thin (balanced),
#   none (fastest compilation, no LTO optimizations)
#
# KCONFIG OPTIONS:
#   - LTO_CLANG: Master LTO enable switch
#   - LTO_NONE: Disable all LTO
#   - LTO_CLANG_FULL: Full LTO (whole program analysis)
#   - LTO_CLANG_THIN: Thin LTO (parallel linking, faster)
#   - THINLTO: Old name for LTO_CLANG_THIN (legacy support)
#
# SCRIPTS/CONFIG UTILITY:
#   - -d: Disable option (CONFIG_OPTION=n)
#   - -e: Enable option (CONFIG_OPTION=y)
#   - --file: Specify .config file to modify
#
# LTO MODES EXPLAINED:
#
#   LTO=none:
#   ├─ Disables all LTO-related configs
#   ├─ Disables CFI (Control Flow Integrity)
#   ├─ Fastest compilation
#   └─ Worst runtime performance
#       NOT RECOMMENDED for production
#
#   LTO=thin:
#   ├─ Enables LTO_CLANG with THIN mode
#   ├─ Parallel linking between object files
#   ├─ Good balance: faster than full, better optimization than none
#   ├─ Maintains CFI support
#   └─ RECOMMENDED for development/iterative builds
#
#   LTO=full:
#   ├─ Enables LTO_CLANG with FULL mode
#   ├─ Whole-program analysis and optimization
#   ├─ Best runtime performance/optimization
#   ├─ Very slow compilation (hours for full kernel)
#   ├─ Maintains CFI support
#   └─ Used for release/optimized builds
#
# KLEAF CONTEXT:
#   Kleaf has similar LTO configuration via Kconfig fragments
#   This shows how kbuild handles LTO configuration system-level
#
# OLDDEFCONFIG:
#   After modifying .config with scripts/config:
#   1. Some options may have dependencies
#   2. Need to regenerate .config to satisfy dependencies
#   3. olddefconfig: Update .config with new options, keep existing
#   4. Ensures .config is valid for this kernel version
#################################################################################
if [ "${LTO}" = "none" -o "${LTO}" = "thin" -o "${LTO}" = "full" ]; then
  echo "========================================================"
  echo " Modifying LTO mode to '${LTO}'"

  set -x
  if [ "${LTO}" = "none" ]; then
    # Disable all LTO options
    ${KERNEL_DIR}/scripts/config --file ${OUT_DIR}/.config \
      -d LTO_CLANG \           # Disable master LTO switch
      -e LTO_NONE \            # Enable "no LTO" option
      -d LTO_CLANG_THIN \      # Disable thin LTO
      -d LTO_CLANG_FULL \      # Disable full LTO
      -d THINLTO               # Disable old thin LTO name (for compatibility)
  elif [ "${LTO}" = "thin" ]; then
    # Enable thin LTO for faster compilation
    # This is best-effort; some kernels don't support LTO_THIN mode
    # THINLTO was the old name for LTO_THIN, and it was 'default y'
    ${KERNEL_DIR}/scripts/config --file ${OUT_DIR}/.config \
      -e LTO_CLANG \           # Enable master LTO switch
      -d LTO_NONE \            # Disable "no LTO"
      -e LTO_CLANG_THIN \      # Enable thin LTO mode
      -d LTO_CLANG_FULL \      # Disable full LTO
      -e THINLTO               # Enable old thin LTO name for compatibility
  elif [ "${LTO}" = "full" ]; then
    # Enable full LTO for maximum optimization
    # THINLTO was the old name for LTO_THIN, and it was 'default y'
    ${KERNEL_DIR}/scripts/config --file ${OUT_DIR}/.config \
      -e LTO_CLANG \           # Enable master LTO switch
      -d LTO_NONE \            # Disable "no LTO"
      -d LTO_CLANG_THIN \      # Disable thin LTO
      -e LTO_CLANG_FULL \      # Enable full LTO mode
      -d THINLTO               # Disable old thin LTO name
  fi
  # Recalculate .config to resolve dependencies after LTO changes
  (cd ${OUT_DIR} && make ${TOOL_ARGS} O=${OUT_DIR} "${MAKE_ARGS[@]}" olddefconfig)
  set +x
elif [ -n "${LTO}" ]; then
  # Invalid LTO value specified
  echo "LTO= must be one of 'none', 'thin' or 'full'."
  exit 1
fi

#################################################################################
# TAGS CONFIGURATION (DEBUG: CTAGS/CSCOPE GENERATION)
#
# PURPOSE:
#   Generate tags file for source code navigation
#   Used by editors/IDEs for code completion and jump-to-definition
#
# TAGS_CONFIG OPTIONS:
#   - "tags": Generate ctags file
#   - "cscope": Generate cscope database
#
# WHEN USED:
#   Developers use this for source code indexing
#   Not needed for normal builds, mainly for development environment
#
# EARLY EXIT:
#   When TAGS_CONFIG is set, script runs tags generation then exits
#   Doesn't continue with kernel compilation
#   User invokes build.sh separately with TAGS_CONFIG set
#
# SRCARCH VARIABLE:
#   Set to target architecture for correct tag generation
#   Examples: arm64, x86_64, etc.
#################################################################################
if [ -n "${TAGS_CONFIG}" ]; then
  echo "========================================================"
  echo " Running tags command:"
  set -x
  (cd ${KERNEL_DIR} && SRCARCH=${ARCH} ./scripts/tags.sh ${TAGS_CONFIG})
  set +x
  exit 0  # Exit early - no kernel compilation needed for tags
fi

#################################################################################
# ABI PROPERTY FILE INITIALIZATION
#
# PURPOSE:
#   Create abi.prop file that stores metadata about ABI artifacts
#   This file documents what ABI definitions were used for this build
#
# ABI (Application Binary Interface):
#   - Contract between kernel and kernel modules
#   - Exported symbols that modules can use
#   - Must be stable for vendor modules to work across kernel updates
#   - KMI (Kernel Module Interface): Android's term for stable ABI
#
# CONTENT:
#   abi.prop records:
#   - KMI_DEFINITION: ABI definition file (usually abi.xml)
#   - KMI_MONITORED: Whether ABI changes are tracked
#   - KMI_ENFORCED: Whether ABI must match strict requirements
#   - KMI_SYMBOL_LIST: File containing allowed exported symbols
#   - KERNEL_BINARY: vmlinux (for ABI analysis)
#   - MODULES_ARCHIVE: Compressed modules (if using separate archive)
#
# TRUNCATE:
#   ": > ${ABI_PROP}" creates empty file
#   Removes any previous content from earlier builds
#################################################################################
# Truncate abi.prop file - create fresh starting point
ABI_PROP=${DIST_DIR}/abi.prop
: > ${ABI_PROP}  # Create empty file by redirecting null to it

#################################################################################
# ABI DEFINITION SETUP
#
# PURPOSE:
#   Configure ABI (Application Binary Interface) tracking and monitoring
#   Ensures kernel module compatibility across different kernel versions
#
# COMPONENTS:
#
#   1. ABI_DEFINITION:
#      - XML file describing kernel's ABI/interface
#      - Usually located in kernel source: android/abi_${arch}.xml
#      - Lists public symbols, their types, sizes, offsets
#      - Used to detect breaking changes in kernel updates
#
#   2. KMI_MONITORED:
#      - Flag indicating ABI is being tracked/monitored
#      - Downstream tools will check for ABI changes
#      - If 0/not set: ABI changes not checked (no compatibility guarantee)
#      - If 1: ABI changes will cause warnings/failures
#
#   3. KMI_ENFORCED:
#      - Flag requiring strict ABI compatibility
#      - If 1: Build fails if ABI changes detected
#      - If 0/not set: ABI changes allowed but documented
#      - Used in production/release builds
#
# WORKFLOW:
#   1. If ABI_DEFINITION set in build config:
#      - Copy ABI_DEFINITION to DIST_DIR/abi.xml
#      - Record in abi.prop: KMI_DEFINITION=abi.xml
#      - Mark KMI_MONITORED=1
#      - If KMI_ENFORCED=1, record that too
#
# GKI CONTEXT:
#   Google's GKI (Generic Kernel Image) uses strict ABI monitoring
#   Ensures vendor modules remain compatible across GKI updates
#   - GKI kernel publishes abi.xml
#   - Vendors must compile modules compatible with that ABI
#   - Can't change kernel structures, symbol visibility, etc.
#################################################################################
if [ -n "${ABI_DEFINITION}" ]; then

  ABI_XML=${DIST_DIR}/abi.xml

  # Record ABI definition file in metadata
  echo "KMI_DEFINITION=abi.xml" >> ${ABI_PROP}
  echo "KMI_MONITORED=1"        >> ${ABI_PROP}

  # If KMI is enforced (strict mode), record it
  if [ "${KMI_ENFORCED}" = "1" ]; then
    echo "KMI_ENFORCED=1" >> ${ABI_PROP}
  fi
fi

#################################################################################
# KMI SYMBOL LIST SETUP
#
# PURPOSE:
#   Configure which kernel symbols are exported for module use
#   Kernel can export hundreds/thousands of symbols; most are internal
#   KMI_SYMBOL_LIST controls which symbols are considered public/stable
#
# COMPONENTS:
#
#   1. KMI_SYMBOL_LIST (Primary):
#      - Main file listing exported symbol names
#      - Format: one symbol per line
#      - Examples:
#        - sys_open
#        - do_sys_open
#        - usb_register
#   2. ADDITIONAL_KMI_SYMBOL_LISTS (Secondary):
#      - Optional additional symbol lists to append
#      - Allows combining multiple symbol lists
#      - Used when multiple components contribute symbols
#
# PROCESS_SYMBOLS SCRIPT:
#   ${ROOT_DIR}/build/abi/process_symbols
#   - Merges primary + additional symbol lists
#   - Validates symbols exist in vmlinux
#   - Generates normalized symbol list: abi_symbollist
#   - Generates report: abi_symbollist.report
#
# OUTPUT:
#   - abi_symbollist: Processed, validated symbol list
#   - abi_symbollist.report: Report of processed symbols
#   - Record in abi.prop: KMI_SYMBOL_LIST=abi_symbollist
#
# SPECIAL SYMBOL LISTS:
#   - "android/abi_gki_aarch_galaxy_presubmit": GKI presubmit list
#   - "android/abi_greylist": Greylist of symbols (deprecated but tracked)
#   These are internal Android symbol lists
#
# KLEAF INTEGRATION:
#   Kleaf has corresponding rules for symbol list processing
#   This shows the kbuild-level implementation
#################################################################################
if [ -n "${KMI_SYMBOL_LIST}" ]; then
  # Define output filename for processed symbol list
  ABI_SL=${DIST_DIR}/abi_symbollist
  
  # Record symbol list in metadata file
  echo "KMI_SYMBOL_LIST=abi_symbollist" >> ${ABI_PROP}
fi

#################################################################################
# KERNEL BINARY AND MODULES ARCHIVE METADATA
#
# PURPOSE:
#   Record essential artifact information in abi.prop for downstream tools
#
# KERNEL_BINARY:
#   - Always vmlinux (uncompressed kernel binary with debug symbols)
#   - Compressed versions (Image, Image.gz, Image.lz4) are for boot
#   - vmlinux is used for ABI analysis because it contains all symbols
#
# MODULES_ARCHIVE:
#   - If COMPRESS_UNSTRIPPED_MODULES=1, modules packed into tarball
#   - Unstripped modules needed for debugging (contain debug symbols)
#   - Debug tools use these for source-level debugging
#################################################################################
# define the kernel binary and modules archive in the $ABI_PROP
echo "KERNEL_BINARY=vmlinux" >> ${ABI_PROP}
if [ "${COMPRESS_UNSTRIPPED_MODULES}" = "1" ]; then
  echo "MODULES_ARCHIVE=${UNSTRIPPED_MODULES_ARCHIVE}" >> ${ABI_PROP}
fi

#################################################################################
# COPY ABI DEFINITION FILE
#
# PURPOSE:
#   Copy ABI definition XML file from kernel source to distribution directory
#   ABI definition needed by downstream ABI checking tools
#
# TYPICAL LOCATION:
#   android/abi_${ARCH}.xml in kernel source tree
#   Example: android/abi_arm64.xml for ARM64 architecture
#
# PUSHD/POPD:
#   pushd: Change directory and push to stack
#   popd: Pop from stack and return to previous directory
#   Allows running commands in specific directory without subshell
#################################################################################
# Copy the abi_${arch}.xml file from the sources into the dist dir
if [ -n "${ABI_DEFINITION}" ]; then
  echo "========================================================"
  echo " Copying abi definition to ${ABI_XML}"
  pushd $ROOT_DIR/$KERNEL_DIR
    cp "${ABI_DEFINITION}" ${ABI_XML}
  popd
fi

#################################################################################
# PROCESS AND SETUP KMI SYMBOL LIST
#
# PURPOSE:
#   Process KMI symbol list for strict mode ABI checking
#   Implements symbol list-based KMI enforcement
#
# KEY CONCEPTS:
#
#   1. TRIM_NONLISTED_KMI:
#      - If 1: Only export symbols in KMI_SYMBOL_LIST
#      - Symbols not in list won't be exported (invisible to modules)
#      - Reduces kernel surface area
#      - Enforces "stable ABI = only these symbols"
#
#   2. KMI_SYMBOL_LIST_STRICT_MODE:
#      - If 1: Verify symbols exported match the symbol list exactly
#      - Build fails if actual symbols don't match list
#      - Catches accidental symbol export changes
#      - Only used with TRIM_NONLISTED_KMI
#
# IMPLEMENTATION:
#
#   A. Symbol List Processing:
#      1. Merge KMI_SYMBOL_LIST + ADDITIONAL_KMI_SYMBOL_LISTS
#      2. Add internal symbol lists (greylist, presubmit)
#      3. Generate abi_symbollist.raw (flattened list)
#
#   B. If TRIM_NONLISTED_KMI=1:
#      1. Create abi_symbollist.raw: symbol names only, one per line
#      2. Use scripts/config to enable CONFIG_TRIM_UNUSED_KSYMS
#      3. Set CONFIG_UNUSED_KSYMS_WHITELIST to abi_symbollist.raw
#      4. Kernel build will:
#         - Analyze which symbols are used internally
#         - Keep only used symbols + whitelisted symbols
#         - Unexport all others
#      5. Run olddefconfig to validate configuration
#      6. Verify CONFIG_UNUSED_KSYMS_WHITELIST was applied
#
#   C. If KMI_SYMBOL_LIST_STRICT_MODE=1:
#      1. Requires TRIM_NONLISTED_KMI=1 (must enforce via trimming first)
#      2. After build, runs compare_to_symbol_list
#      3. Verifies actual exported symbols match abi_symbollist.raw exactly
#      4. Fails build if mismatch found
#
# KLEAF CONTEXT:
#   Kleaf has separate rules for symbol list processing and trimming
#   This shows kbuild-level implementation of KMI enforcement
#################################################################################
# Copy the abi symbol list file from the sources into the dist dir
if [ -n "${KMI_SYMBOL_LIST}" ]; then
  # Use build/abi/process_symbols to merge symbol lists
  # This script:
  # - Merges main KMI_SYMBOL_LIST with ADDITIONAL_KMI_SYMBOL_LISTS
  # - Validates symbols exist
  # - Generates processed abi_symbollist
  # - Generates abi_symbollist.report
  ${ROOT_DIR}/build/abi/process_symbols --out-dir="$DIST_DIR" --out-file=abi_symbollist \
    --report-file=abi_symbollist.report --in-dir="$ROOT_DIR/$KERNEL_DIR" \
    "${KMI_SYMBOL_LIST}" ${ADDITIONAL_KMI_SYMBOL_LISTS} \
    "android/abi_gki_aarch_galaxy_presubmit" "android/abi_greylist"
  
  pushd $ROOT_DIR/$KERNEL_DIR
  
  if [ "${TRIM_NONLISTED_KMI}" = "1" ]; then
      # TRIM_NONLISTED_KMI mode: Only export whitelisted symbols
      # Create the raw symbol list for kernel config
      # flatten_symbol_list converts symbol list to format kernel's UNUSED_KSYMS_WHITELIST expects
      cat ${ABI_SL} | \
              ${ROOT_DIR}/build/abi/flatten_symbol_list > \
              ${OUT_DIR}/abi_symbollist.raw

      # Update the kernel configuration to trim unexported symbols
      # -d UNUSED_SYMBOLS: Disable old unused symbols config
      # -e TRIM_UNUSED_KSYMS: Enable new trim unused ksyms feature
      # --set-str: Set CONFIG_UNUSED_KSYMS_WHITELIST to our symbol list file
      ./scripts/config --file ${OUT_DIR}/.config \
              -d UNUSED_SYMBOLS -e TRIM_UNUSED_KSYMS \
              --set-str UNUSED_KSYMS_WHITELIST ${OUT_DIR}/abi_symbollist.raw
      
      # Recalculate .config to validate new settings
      (cd ${OUT_DIR} && \
              make O=${OUT_DIR} ${TOOL_ARGS} "${MAKE_ARGS[@]}" olddefconfig)
      
      # Verify the config was actually applied
      # If CONFIG_UNUSED_KSYMS_WHITELIST not in .config, something went wrong
      grep CONFIG_UNUSED_KSYMS_WHITELIST ${OUT_DIR}/.config > /dev/null || {
        echo "ERROR: Failed to apply TRIM_NONLISTED_KMI kernel configuration" >&2
        echo "Does your kernel support CONFIG_UNUSED_KSYMS_WHITELIST?" >&2
        exit 1
      }

    elif [ "${KMI_SYMBOL_LIST_STRICT_MODE}" = "1" ]; then
      # ERROR: Strict mode requires trimming to be enabled first
      # Trimming ensures symbols are actually controlled
      # Strict mode just validates the symbol list
      echo "ERROR: KMI_SYMBOL_LIST_STRICT_MODE requires TRIM_NONLISTED_KMI=1" >&2
    exit 1
  fi
  popd # $ROOT_DIR/$KERNEL_DIR
elif [ "${TRIM_NONLISTED_KMI}" = "1" ]; then
  # ERROR: Trimming needs symbol list to know which symbols to keep
  echo "ERROR: TRIM_NONLISTED_KMI requires a KMI_SYMBOL_LIST" >&2
  exit 1
elif [ "${KMI_SYMBOL_LIST_STRICT_MODE}" = "1" ]; then
  # ERROR: Strict mode needs symbol list to compare against
  echo "ERROR: KMI_SYMBOL_LIST_STRICT_MODE requires a KMI_SYMBOL_LIST" >&2
  exit 1
fi

#################################################################################
# MAIN KERNEL COMPILATION
#
# PURPOSE:
#   Execute the main kernel build process
#   Compiles vmlinux (kernel binary) and MAKE_GOALS targets
#
# BUILD PROCESS:
#   1. Change to OUT_DIR (where kbuild expects to be)
#   2. Run make with:
#      - O=${OUT_DIR}: Output directory for build artifacts
#      - ${TOOL_ARGS}: Toolchain settings (CC, LD, etc.)
#      - "${MAKE_ARGS[@]}": User-provided arguments (-j24, V=1, etc.)
#      - ${MAKE_GOALS}: Build targets (Image, modules, dtbs, etc.)
#   3. This is where most of the build time is spent
#
# TYPICAL BUILD GOALS:
#   - vmlinux: Kernel binary (required)
#   - modules: Kernel module .ko files (optional)
#   - dtbs: Device tree binaries (optional)
#   - Image: Architecture-specific kernel binary (arm64)
#   - bzImage: x86 kernel binary
#
# SET -X / SET +X:
#   set -x enables command logging so user sees what's being compiled
#   Important because kernel build takes significant time
#################################################################################
echo "========================================================"
echo " Building kernel"

set -x
(cd ${OUT_DIR} && make O=${OUT_DIR} ${TOOL_ARGS} "${MAKE_ARGS[@]}" ${MAKE_GOALS})
set +x

#################################################################################
# POST-KERNEL BUILD COMMANDS
#
# PURPOSE:
#   Execute user-defined commands after kernel compilation completes
#   Allow vendor to perform post-build tasks
#
# USE CASES:
#   - Sign kernel binary
#   - Process kernel symbols
#   - Perform additional checks
#   - Prepare custom artifacts
#################################################################################
if [ -n "${POST_KERNEL_BUILD_CMDS}" ]; then
  echo "========================================================"
  echo " Running post-kernel-build command(s):"
  set -x
  eval ${POST_KERNEL_BUILD_CMDS}
  set +x
fi

#################################################################################
# MODULES ORDER VALIDATION
#
# PURPOSE:
#   Verify that all expected kernel modules were built
#   Detects if any modules failed to compile silently
#
# MODULES_ORDER FILE:
#   - Text file listing all modules that should be built
#   - Format matches modules.order generated by kbuild
#   - Examples:
#     kernel/module1.ko
#     kernel/net/module2.ko
#
# DIFF OUTPUT:
#   If generated modules.order differs from expected:
#   - Shows which modules are missing
#   - Shows unexpected modules
#   - Helps catch accidental module build failures
#
# WHEN USED:
#   GKI and validated device kernel configs
#   Ensures deterministic, reproducible builds
#################################################################################
if [ -n "${MODULES_ORDER}" ]; then
  echo "========================================================"
  echo " Checking the list of modules:"
  if ! diff -u "${KERNEL_DIR}/${MODULES_ORDER}" "${OUT_DIR}/modules.order"; then
    echo "ERROR: modules list out of date" >&2
    echo "Update it with:" >&2
    echo "cp ${OUT_DIR}/modules.order ${KERNEL_DIR}/${MODULES_ORDER}" >&2
    exit 1
  fi
fi

#################################################################################
# KMI STRICT MODE CHECKING (OPTIONAL)
#
# PURPOSE:
#   Verify that actual kernel symbols match the KMI symbol list exactly
#   Post-build validation: catch accidental symbol exposure changes
#
# WHEN USED:
#   Only if KMI_SYMBOL_LIST_STRICT_MODE=1
#   Only if SKIP_KMI_COMPARING != 1
#   Requires TRIM_NONLISTED_KMI to be enabled during build
#
# PROCESS:
#   1. Get list of GKI system modules: android/gki_system_dlkm_modules
#   2. Build list of strict mode objects to check:
#      - Always vmlinux
#      - For each module: module name without .ko extension
#      - Example: vmlinux usb_storage ext4 vfat
#   3. Call compare_to_symbol_list with Module.symvers
#      - Module.symvers has actual exported symbols after build
#      - abi_symbollist.raw has expected symbols
#   4. If they don't match exactly: build fails
#
# GKI CONTEXT:
#   Google's GKI uses this to ensure stable symbol interface
#   Prevents vendors from accidentally depending on internal symbols
#   Makes it safe for vendors to develop independently
#################################################################################
echo "KMI_SYMBOL_LIST_STRICT_MODE=${KMI_SYMBOL_LIST_STRICT_MODE}"
echo "SKIP_KMI_COMPARING=${SKIP_KMI_COMPARING}"

if [[ "${KMI_SYMBOL_LIST_STRICT_MODE}" = "1" ]] && [[ "${SKIP_KMI_COMPARING}" != "1" ]]; then
  echo "========================================================"
  echo " Comparing the KMI and the symbol lists:"
  set -x

  # Get list of GKI modules to check
  gki_modules_list="${ROOT_DIR}/${KERNEL_DIR}/android/gki_system_dlkm_modules"
  # Build object list: vmlinux + all modules (without .ko extension)
  KMI_STRICT_MODE_OBJECTS="vmlinux $(sed 's/\\.ko$//' ${gki_modules_list} | tr '\n' ' ')" \
    # Run comparison tool to verify symbols match
    ${ROOT_DIR}/build/abi/compare_to_symbol_list "${OUT_DIR}/Module.symvers"             \
    "${OUT_DIR}/abi_symbollist.raw"
  set +x
fi

#################################################################################
# MODULE INSTALLATION SETUP
#
# PURPOSE:
#   Prepare module installation directory
#   Initialize variables for module handling
#
# MODULES_STAGING_DIR:
#   Clean staging directory before installing modules
#   mkdir -p ensures directory exists
#
# MODULE_STRIP_FLAG:
#   Control whether modules are stripped of debug symbols
#   DO_NOT_STRIP_MODULES=1: Keep debug symbols in distributed modules
#   Default: Strip modules for smaller size
#   Note: Modules are always stripped in ramdisk/initramfs anyway
#################################################################################
rm -rf ${MODULES_STAGING_DIR}
mkdir -p ${MODULES_STAGING_DIR}

if [ "${DO_NOT_STRIP_MODULES}" != "1" ]; then
  # If not explicitly keeping debug symbols, strip modules for smaller size
  MODULE_STRIP_FLAG="INSTALL_MOD_STRIP=1"
fi

#################################################################################
# KERNEL MODULES INSTALLATION
#
# PURPOSE:
#   Install built kernel modules into staging directory
#   This happens if either:
#   - BUILD_INITRAMFS=1 (need modules in initramfs)
#   - IN_KERNEL_MODULES is set (want modules in distribution)
#
# INSTALLATION PROCESS:
#   make modules_install:
#   1. Installs .ko files to ${MODULES_STAGING_DIR}/lib/modules/<version>/
#   2. Creates module.dep and other dependency files
#   3. Runs depmod to update module interdependencies
#   4. If INSTALL_MOD_STRIP=1, strips debug symbols from modules
#
# STAGING DIRECTORY STRUCTURE:
#   ${MODULES_STAGING_DIR}/
#   ├─ lib/modules/<kernel_version>/
#   │  ├─ kernel/ (kernel modules)
#   │  ├─ modules.dep (module dependencies)
#   │  ├─ modules.dep.bin
#   │  ├─ modules.alias
#   │  ├─ modules.order
#   │  └─ ... other metadata
#   └─ (later used for ramdisk/boot image)
#
# TOOL_ARGS:
#   Contains architecture-specific and toolchain settings:
#   - ARCH=arm64
#   - CC=clang
#   - LD=ld.lld
#   - etc.
#################################################################################
if [ "${BUILD_INITRAMFS}" = "1" -o  -n "${IN_KERNEL_MODULES}" ]; then
  echo "========================================================"
  echo " Installing kernel modules into staging directory"

  (cd ${OUT_DIR} &&                                                           \
   make O=${OUT_DIR} ${TOOL_ARGS} ${MODULE_STRIP_FLAG}                        \
        INSTALL_MOD_PATH=${MODULES_STAGING_DIR} "${MAKE_ARGS[@]}" modules_install)
fi

#################################################################################
# EXTERNAL MODULES: MAKEFILE-BASED BUILD
#
# PURPOSE:
#   Build external modules using a provided Makefile
#   Alternative to building modules individually
#
# USE CASE:
#   Vendor provides single Makefile for all external modules
#   Makefile handles parallel compilation of multiple modules
#   More efficient than sequential module builds
#
# REQUIREMENTS:
#   EXT_MODULES_MAKEFILE: Path to Makefile (must be provided)
#
# PARAMETERS PASSED TO MAKEFILE:
#   - KERNEL_SRC: Path to kernel source
#   - O: Output directory
#   - TOOL_ARGS: Toolchain settings
#   - MODULE_STRIP_FLAG: Whether to strip modules
#   - INSTALL_HDR_PATH: Where to install UAPI headers
#   - INSTALL_MOD_PATH: Where to install modules
#
# MAKEFILES CAN:
#   - Build multiple modules in parallel
#   - Handle interdependencies
#   - Install to staging directories
#   - More flexibility than individual module builds
#################################################################################
if [[ -z "${SKIP_EXT_MODULES}" ]] && [[ -n "${EXT_MODULES_MAKEFILE}" ]]; then
  echo "========================================================"
  echo " Building and installing external modules using ${EXT_MODULES_MAKEFILE}"

  make -f "${EXT_MODULES_MAKEFILE}" KERNEL_SRC=${ROOT_DIR}/${KERNEL_DIR} \
          O=${OUT_DIR} ${TOOL_ARGS} ${MODULE_STRIP_FLAG}                 \
          INSTALL_HDR_PATH="${KERNEL_UAPI_HEADERS_DIR}/usr"              \
          INSTALL_MOD_PATH=${MODULES_STAGING_DIR} "${MAKE_ARGS[@]}"
fi

#################################################################################
# EXTERNAL MODULES: INDIVIDUAL MODULE BUILD
#
# PURPOSE:
#   Build external kernel modules individually
#   Each module is built separately against the kernel
#
# EXTERNAL MODULES CONCEPT:
#   Modules not in kernel source tree
#   Built against kernel sources but maintained separately
#   Examples: device drivers, proprietary modules, custom features
#
# PROCESS FOR EACH MODULE:
#   1. Calculate relative path (EXT_MOD_REL) from KERNEL_DIR to module
#   2. Create output directory for object files
#   3. Run make with module source and output location
#   4. Run modules_install to install to staging
#
# REL_PATH CALCULATION:
#   Module source: ${ROOT_DIR}/${EXT_MOD} (absolute)
#   Kernel dir: ${KERNEL_DIR} (relative to ROOT_DIR)
#   Result: EXT_MOD_REL (module source relative to kernel dir)
#
#   Why this matters:
#   - kbuild expects M= parameter to be relative to kernel source
#   - If M is absolute, object files end up in source directory (bad)
#   - If M is relative, object files end up in OUT_DIR (good)
#
# MODULE INSTALLATION:
#   INSTALL_MOD_DIR="extra/${EXT_MOD}" groups modules by source
#   Creates: lib/modules/<version>/extra/<module_name>/
#   Allows tracking which modules came from where
#
# TWO-STEP MAKE INVOCATION:
#   1. First make: Compile module
#   2. Second make modules_install: Install to staging
#   Some versions require this split; some can do both in one step
#################################################################################
if [[ -z "${SKIP_EXT_MODULES}" ]] && [[ -n "${EXT_MODULES}" ]]; then
  echo "========================================================"
  echo " Building external modules and installing them into staging directory"

  for EXT_MOD in ${EXT_MODULES}; do
    # The path that we pass in via the variable M needs to be a relative path
    # relative to the kernel source directory. The source files will then be
    # looked for in ${KERNEL_DIR}/${EXT_MOD_REL} and the object files (i.e. .o
    # and .ko) files will be stored in ${OUT_DIR}/${EXT_MOD_REL}. If we
    # instead set M to an absolute path, then object (i.e. .o and .ko) files
    # are stored in the module source directory which is not what we want.
    EXT_MOD_REL=$(rel_path ${ROOT_DIR}/${EXT_MOD} ${KERNEL_DIR})
    
    # The output directory must exist before we invoke make. Otherwise, the
    # build system behaves horribly wrong.
    mkdir -p ${OUT_DIR}/${EXT_MOD_REL}
    
    set -x
    # First pass: Compile the module
    make -C ${EXT_MOD} M=${EXT_MOD_REL} KERNEL_SRC=${ROOT_DIR}/${KERNEL_DIR}  \
                       O=${OUT_DIR} ${TOOL_ARGS} "${MAKE_ARGS[@]}"
    
    # Second pass: Install module to staging directory
    make -C ${EXT_MOD} M=${EXT_MOD_REL} KERNEL_SRC=${ROOT_DIR}/${KERNEL_DIR}  \
                       O=${OUT_DIR} ${TOOL_ARGS} ${MODULE_STRIP_FLAG}         \
                       INSTALL_MOD_PATH=${MODULES_STAGING_DIR}                \
                       INSTALL_MOD_DIR="extra/${EXT_MOD}"                     \
                       INSTALL_HDR_PATH="${KERNEL_UAPI_HEADERS_DIR}/usr"      \
                       "${MAKE_ARGS[@]}" modules_install
    set +x
  done

fi

#################################################################################
# GKI CERTIFICATION TOOLS PACKAGING
#
# PURPOSE:
#   Generate tarball with tools needed to certify GKI boot images
#   These tools verify boot images meet GKI requirements
#
# WHAT'S INCLUDED:
#   - avbtool: Android Verified Boot signing tool
#   - certify_bootimg: Tool to certify GKI boot image format
#
# LOCATION:
#   Tools come from: prebuilts/kernel-build-tools/linux-x86/bin/
#
# OUTPUT:
#   gki_certification_tools.tar.gz in DIST_DIR
#   Downstream processes extract and use these tools
#
# USE CASE:
#   GKI builds: Generate certification tools for quality assurance
#   Non-GKI builds: Usually not needed
#################################################################################
if [ "${BUILD_GKI_CERTIFICATION_TOOLS}" = "1"  ]; then
  GKI_CERTIFICATION_TOOLS_TAR="gki_certification_tools.tar.gz"
  echo "========================================================"
  echo " Generating ${GKI_CERTIFICATION_TOOLS_TAR}"
  # List of certification tools/binaries to include
  GKI_CERTIFICATION_BINARIES=(avbtool certify_bootimg)
  GKI_CERTIFICATION_TOOLS_ROOT="${ROOT_DIR}/prebuilts/kernel-build-tools/linux-x86"
  # Build list of bin/ files to include in tarball
  GKI_CERTIFICATION_FILES="${GKI_CERTIFICATION_BINARIES[@]/#/bin/}"
  # Create tarball with certification tools
  tar -czf ${DIST_DIR}/${GKI_CERTIFICATION_TOOLS_TAR} \
    -C ${GKI_CERTIFICATION_TOOLS_ROOT} ${GKI_CERTIFICATION_FILES}
fi

#################################################################################
# TEST_MAPPING FILES AGGREGATION
#
# PURPOSE:
#   Collect all TEST_MAPPING files from repository
#   TEST_MAPPING files describe which tests should run for code changes
#
# PROCESS:
#   1. Find all TEST_MAPPING files in repository (excluding .git, .repo, out)
#   2. Write list to TEST_MAPPING_FILES
#   3. Use soong_zip to create test_mappings.zip
#   4. Include in distribution for CI systems
#
# USE CASE:
#   CI systems use test mappings to determine which tests to run
#   When kernel code changes, run only relevant tests
#   Speeds up CI by avoiding unnecessary test runs
#
# FIND PATTERNS:
#   - -not -path "*.git*": Exclude .git directories
#   - -not -path "*.repo*": Exclude repo metadata
#   - -not -path "*out*": Exclude build output
#################################################################################
echo "========================================================"
echo " Generating test_mappings.zip"
TEST_MAPPING_FILES=${OUT_DIR}/test_mapping_files.txt
find ${ROOT_DIR} -name TEST_MAPPING \
  -not -path "${ROOT_DIR}/\\.git*" \
  -not -path "${ROOT_DIR}/\\.repo*" \
  -not -path "${ROOT_DIR}/out*" \
  > ${TEST_MAPPING_FILES}
soong_zip -o ${DIST_DIR}/test_mappings.zip -C ${ROOT_DIR} -l ${TEST_MAPPING_FILES}

#################################################################################
# EXTRA BUILD COMMANDS
#
# PURPOSE:
#   Execute user-defined commands after kernel and modules built
#   Allow vendor to perform custom post-build steps
#
# TYPICAL USES:
#   - Sign kernel/modules
#   - Generate custom artifacts
#   - Run validation checks
#   - Prepare distribution packages
#################################################################################
if [ -n "${EXTRA_CMDS}" ]; then
  echo "========================================================"
  echo " Running extra build command(s):"
  set -x
  eval ${EXTRA_CMDS}
  set +x
fi

#################################################################################
# DEVICE TREE OVERLAY COMPILATION
#
# PURPOSE:
#   Compile device tree overlays from ODM (OEM/Device Manufacturer) directories
#   Overlays allow vendors to customize device tree without modifying base
#
# ODM_DIRS:
#   Space-separated list of OEM/ODM directory paths
#   Example: "vendor/samsung vendor/qualcomm"
#
# OVERLAY_DIR STRUCTURE:
#   device/${ODM_DIR}/overlays/
#   ├─ <overlayname>.dts (device tree source)
#   └─ ... more overlays
#
# COMPILATION:
#   Each overlay compiled using dtc (device tree compiler)
#   Output .dtbo files collected in OVERLAYS_OUT for later copying
#
# USE CASE:
#   OEMs can overlay vendor-specific device tree without modifying Google's base
#   Example: Add vendor-specific PMIC configuration
#   Keeps vendor changes separate and maintainable
#################################################################################
OVERLAYS_OUT=""
for ODM_DIR in ${ODM_DIRS}; do
  OVERLAY_DIR=${ROOT_DIR}/device/${ODM_DIR}/overlays

  if [ -d ${OVERLAY_DIR} ]; then
    # Create output directory for this ODM's overlays
    OVERLAY_OUT_DIR=${OUT_DIR}/overlays/${ODM_DIR}
    mkdir -p ${OVERLAY_OUT_DIR}
    # Compile overlays using kbuild
    make -C ${OVERLAY_DIR} DTC=${OUT_DIR}/scripts/dtc/dtc                     \
                           OUT_DIR=${OVERLAY_OUT_DIR} "${MAKE_ARGS[@]}"
    # Collect compiled .dtbo files for copying to DIST_DIR later
    OVERLAYS=$(find ${OVERLAY_OUT_DIR} -name "*.dtbo")
    OVERLAYS_OUT="$OVERLAYS_OUT $OVERLAYS"
  fi
done

#################################################################################
# COPY BUILD ARTIFACTS TO DISTRIBUTION DIRECTORY
#
# PURPOSE:
#   Copy kernel and module binaries to DIST_DIR for packaging/distribution
#   DIST_DIR is where final release artifacts are collected
#
# FILES VARIABLE:
#   User-specified list of files to copy
#   Typically includes:
#   - Image (ARM64 kernel binary)
#   - Image.gz (compressed kernel)
#   - Image.lz4 (LZ4 compressed kernel)
#   - modules (kernel modules)
#   - vmlinux (uncompressed kernel with symbols)
#   - System.map (kernel symbol table)
#
# SOURCE LOCATIONS:
#   1. Check ${OUT_DIR}/${FILE} (normal location)
#   2. For .dtb/.dtbo: Check ${OUT_DIR}/${DTS_EXT_DIR}/${FILE}
#      (if DTS_EXT_DIR is set for out-of-tree device trees)
#
# ERROR HANDLING:
#   If file not found in either location: skip with message
#   Build doesn't fail (file might be optional)
#################################################################################
echo "========================================================"
echo " Copying files"
for FILE in ${FILES}; do
  if [ -f ${OUT_DIR}/${FILE} ]; then
    # File found in normal output location
    echo "  $FILE"
    cp -p ${OUT_DIR}/${FILE} ${DIST_DIR}/
  elif [[ "${FILE}" =~ \\.dtb|\\.dtbo ]]  && \
      [ -n "${DTS_EXT_DIR}" ] && [ -f "${OUT_DIR}/${DTS_EXT_DIR}/${FILE}" ] ; then
    # DTS_EXT_DIR is recalculated before to be relative to KERNEL_DIR
    # For device tree files, check out-of-tree directory
    echo "  $FILE"
    cp -p "${OUT_DIR}/${DTS_EXT_DIR}/${FILE}" "${DIST_DIR}/"
  else
    # File not found - report but don't fail
    echo "  $FILE is not a file, skipping"
  fi
done

#################################################################################
# KERNEL GDB SCRIPTS PACKAGING
#
# PURPOSE:
#   Package kernel debugging scripts for GDB (GNU Debugger)
#   These scripts enable kernel-level debugging
#
# FILES:
#   - vmlinux-gdb.py: Main GDB integration script
#   - scripts/gdb/linux/*.py: Linux kernel-specific debugging scripts
#
# USE CASE:
#   Developers use these scripts to debug kernel issues
#   Load into GDB with: source vmlinux-gdb.py
#   Enables kernel-aware debugging, crash analysis
#
# OUTPUT:
#   kernel-gdb-scripts.tar.gz in DIST_DIR
#################################################################################
if [ -f ${OUT_DIR}/vmlinux-gdb.py ]; then
  echo "========================================================"
  KERNEL_GDB_SCRIPTS_TAR=${DIST_DIR}/kernel-gdb-scripts.tar.gz
  echo " Copying kernel gdb scripts to $KERNEL_GDB_SCRIPTS_TAR"
  # Create tarball with all GDB scripts
  (cd $OUT_DIR && tar -czf $KERNEL_GDB_SCRIPTS_TAR --dereference vmlinux-gdb.py scripts/gdb/linux/*.py)
fi

#################################################################################
# DEVICE TREE OVERLAY FILE DISTRIBUTION
#
# PURPOSE:
#   Copy compiled device tree overlays to DIST_DIR
#   Maintain directory structure from overlays build
#
# OVERLAY_DIST_DIR:
#   Mirror directory structure from build to distribution
#   Extracts relative path from overlays and recreates it
#
# PATH MANIPULATION:
#   ${FILE#${OUT_DIR}/overlays/}
#   Remove OUT_DIR/overlays prefix to get relative path
#   Example: out/overlays/samsung/foo.dtbo -> samsung/foo.dtbo
#################################################################################
for FILE in ${OVERLAYS_OUT}; do
  # Calculate relative path in DIST_DIR mirroring build structure
  OVERLAY_DIST_DIR=${DIST_DIR}/$(dirname ${FILE#${OUT_DIR}/overlays/})
  echo "  ${FILE#${OUT_DIR}/}"
  mkdir -p ${OVERLAY_DIST_DIR}
  cp ${FILE} ${OVERLAY_DIST_DIR}/
done

#################################################################################
# KERNEL UAPI HEADERS INSTALLATION AND DISTRIBUTION
#
# PURPOSE:
#   Extract and package user-space kernel API headers
#   UAPI (User API) headers are public interfaces for user-space programs
#
# TYPES OF HEADERS:
#   - UAPI Headers: Public interfaces (exported with install_headers)
#   - Internal Headers: Kernel-only headers (not installed)
#
# USE CASE:
#   User-space programs need to include kernel headers
#   Examples: libc, OpenSSL, graphics libraries, etc.
#   These are separate from kernel compilation
#
# INSTALLATION PROCESS:
#   1. Create KERNEL_UAPI_HEADERS_DIR/usr
#   2. Run make headers_install to extract UAPI headers
#   3. kbuild copies all headers from include/ and arch/*/include/uapi/
#   4. Remove ..install.cmd and .install files (build artifacts)
#   5. Package into kernel-uapi-headers.tar.gz
#
# .install.cmd AND .install FILES:
#   These are internal kbuild tracking files
#   Not needed in distribution, just clutter
#   Remove with: find ... -exec rm '{}' +
#
# TAR COMMAND:
#   --directory=${KERNEL_UAPI_HEADERS_DIR}: Change to headers dir before archiving
#   usr/: Archive only the usr directory (UAPI headers)
#   Creates tarball ready for distribution to user-space build environments
#################################################################################
if [ -z "${SKIP_CP_KERNEL_HDR}" ]; then
  echo "========================================================"
  echo " Installing UAPI kernel headers:"
  mkdir -p "${KERNEL_UAPI_HEADERS_DIR}/usr"
  # Install headers using kernel Makefile target
  make -C ${OUT_DIR} O=${OUT_DIR} ${TOOL_ARGS}                                \
          INSTALL_HDR_PATH="${KERNEL_UAPI_HEADERS_DIR}/usr" "${MAKE_ARGS[@]}" \
          headers_install
  # The kernel makefiles create files named ..install.cmd and .install which
  # are only side products. We don't want those. Let's delete them.
  find ${KERNEL_UAPI_HEADERS_DIR} \( -name ..install.cmd -o -name .install \) -exec rm '{}' +
  KERNEL_UAPI_HEADERS_TAR=${DIST_DIR}/kernel-uapi-headers.tar.gz
  echo " Copying kernel UAPI headers to ${KERNEL_UAPI_HEADERS_TAR}"
  tar -czf ${KERNEL_UAPI_HEADERS_TAR} --directory=${KERNEL_UAPI_HEADERS_DIR} usr/
fi

#################################################################################
# KERNEL HEADERS DISTRIBUTION (DEVELOPMENT HEADERS)
#
# PURPOSE:
#   Package development headers for kernel module compilation
#   Contains necessary headers for external module builds
#
# WHAT'S INCLUDED:
#   - arch/*/include/: Architecture-specific headers
#   - include/: Generic kernel headers
#   - OUT_DIR generated files: kconfig, generated configs
#
# USE CASE:
#   External module builds need these headers
#   Include statements in external module source reference them
#
# TAR OPTIONS:
#   - --absolute-names: Store paths as-is
#   - --dereference: Follow symlinks and store actual files
#   - --transform: Rewrite paths in tarball
#     Original: arch/arm64/include/asm/types.h
#     In tar: kernel-headers/arch/arm64/include/asm/types.h
#   - --null -T -: Read file list from stdin (from find via pipe)
#
# TRANSFORM PATTERNS:
#   Two transformations applied in sequence:
#   1. s,.*$OUT_DIR,,: Remove OUT_DIR prefix from object file paths
#   2. s,^,kernel-headers/,: Add kernel-headers/ prefix to all files
#
# WHY BOTH TRANSFORMS:
#   1. Some files from OUT_DIR (generated .h)
#   2. Some from kernel source tree
#   3. Normalize all to have "kernel-headers/" prefix
#   4. User extracts to get consistent directory structure
#################################################################################
if [ -z "${SKIP_CP_KERNEL_HDR}" ] ; then
  echo "========================================================"
  KERNEL_HEADERS_TAR=${DIST_DIR}/kernel-headers.tar.gz
  echo " Copying kernel headers to ${KERNEL_HEADERS_TAR}"
  pushd $ROOT_DIR/$KERNEL_DIR
    # Find all .h files in arch and include, and generated files in OUT_DIR
    find arch include $OUT_DIR -name *.h -print0               \
            | tar -czf $KERNEL_HEADERS_TAR                     \
              --absolute-names                                 \
              --dereference                                    \
              --transform "s,.*$OUT_DIR,,"                     \
              --transform "s,^,kernel-headers/,\"               \
              --null -T -
  popd
fi

#################################################################################
# VMLINUX BTF GENERATION
#
# PURPOSE:
#   Generate vmlinux.btf: vmlinux with only BTF debug information
#   BTF (BPF Type Format) contains type and function information
#
# WHAT'S BTF:
#   - Compact format for kernel type information
#   - Used by BPF (Berkeley Packet Filter) programs
#   - Used by ABI analysis tools
#   - Much smaller than full debug info
#
# PROCESS:
#   1. Copy vmlinux to vmlinux.btf
#   2. Run pahole -J vmlinux.btf
#      pahole: DWARF to BTF converter
#      -J: Generate BTF from DWARF debug info
#      Removes full DWARF, adds compact BTF section
#   3. Run llvm-strip --strip-debug vmlinux.btf
#      Remove debug symbols (keep BTF section)
#      Result: Small vmlinux.btf with type info but no symbols
#
# USE CASE:
#   ABI analysis: Type information without full binary
#   BPF programs: Type information for safe BPF programming
#   Reduced distribution size compared to full vmlinux
#
# GENERATE_VMLINUX_BTF:
#   Only generate if explicitly requested
#   Takes time: DWARF parsing and BTF generation
#################################################################################
if [ "${GENERATE_VMLINUX_BTF}" = "1" ]; then
  echo "========================================================"
  echo " Generating ${DIST_DIR}/vmlinux.btf"

  (
    cd ${DIST_DIR}
    cp -a vmlinux vmlinux.btf  # Start with copy of full vmlinux
    pahole -J vmlinux.btf      # Convert DWARF debug info to BTF
    llvm-strip --strip-debug vmlinux.btf  # Remove debug symbols, keep BTF
  )

fi

#################################################################################
# GKI BUILD ARTIFACT DISTRIBUTION
#
# PURPOSE:
#   In mixed builds, copy GKI kernel artifacts to final DIST_DIR
#   Allows device kernel build to have access to GKI artifacts
#
# CONTEXT:
#   After GKI build completes and device modules built:
#   Copy GKI kernel, vmlinux, symbols, etc. to final DIST_DIR
#   Device will use GKI kernel in final boot image
#
# FILES COPIED:
#   All files from GKI_DIST_DIR to device DIST_DIR
#   Examples: vmlinux, Image, Image.gz, vmlinux.symvers, etc.
#
# USE CASE:
#   Mixed build workflow:
#   1. Build GKI kernel → GKI_DIST_DIR
#   2. Build device modules (using GKI artifacts)
#   3. Copy GKI artifacts → device DIST_DIR
#   4. Create boot image with GKI kernel + device modules
#################################################################################
if [ -n "${GKI_DIST_DIR}" ]; then
  echo "========================================================"
  echo " Copying files from GKI kernel"
  cp -rv ${GKI_DIST_DIR}/* ${DIST_DIR}/
fi

#################################################################################
# CUSTOM DISTRIBUTION COMMANDS
#
# PURPOSE:
#   Execute user-defined commands after all build steps
#   Allows final customizations before distribution
#
# USE CASES:
#   - Generate checksums
#   - Create manifests
#   - Upload to servers
#   - Generate changelog
#   - Create release notes
#
# WARNING:
#   If UAPI headers not installed and DIST_CMDS needs them:
#   Script warns but continues
#   DIST_CMDS is responsible for checking if headers exist
#################################################################################
if [ -n "${DIST_CMDS}" ]; then
  echo "========================================================"
  echo " Running extra dist command(s):"
  # if DIST_CMDS requires UAPI headers, make sure a warning appears!
  if [ ! -d "${KERNEL_UAPI_HEADERS_DIR}/usr" ]; then
    echo "WARN: running without UAPI headers"
  fi
  set -x
  eval ${DIST_CMDS}
  set +x
fi

#################################################################################
# KERNEL MODULES ARCHIVING AND DISTRIBUTION
#
# PURPOSE:
#   Copy compiled kernel modules to DIST_DIR
#   Optionally compress into tarball
#   Prepare for distribution/installation on device
#
# MODULES FOUND:
#   Find all .ko files in MODULES_STAGING_DIR
#   These were installed during "make modules_install"
#
# MODULE COPY CONDITIONS:
#   Modules copied if:
#   1. IN_KERNEL_MODULES is set (want modules in distribution)
#   2. OR EXT_MODULES is set (external modules built)
#   3. OR EXT_MODULES_MAKEFILE is set (makefile-based builds)
#
# COMPRESS_MODULES:
#   If 1: Create ${MODULES_ARCHIVE} (typically modules.tar.gz)
#   tar command strips directory paths (only .ko files)
#   Result: Can extract anywhere without directory structure
#   Useful for simple installation
#
# UNSTRIPPED MODULES:
#   Separate handling for debug modules (see later section)
#################################################################################
MODULES=$(find ${MODULES_STAGING_DIR} -type f -name "*.ko")
if [ -n "${MODULES}" ]; then
  if [ -n "${IN_KERNEL_MODULES}" -o -n "${EXT_MODULES}" -o -n "${EXT_MODULES_MAKEFILE}" ]; then
    echo "========================================================"
    echo " Copying modules files"
    cp -p ${MODULES} ${DIST_DIR}
    if [ "${COMPRESS_MODULES}" = "1" ]; then
      echo " Archiving modules to ${MODULES_ARCHIVE}"
      tar --transform="s,.*/,," -czf ${DIST_DIR}/${MODULES_ARCHIVE} ${MODULES[@]}
    fi
  fi
  
  #############################################################################
  # INITRAMFS (RAMDISK) GENERATION
  #
  # PURPOSE:
  #   Build initial RAM filesystem (ramdisk/initramfs)
  #   Contains modules and depmod metadata needed at early boot
  #
  # WHEN BUILT:
  #   Only if BUILD_INITRAMFS=1
  #   Typically for GKI, system images, or generic kernels
  #
  # RAMDISK COMPONENTS:
  #   1. Staging directory prepared from MODULES_STAGING_DIR
  #   2. Filtered by MODULES_LIST (if specified)
  #   3. MODULES_BLOCKLIST applied (modules to exclude)
  #   4. TRIM_UNUSED_MODULES option applied
  #
  # CREATE_MODULES_STAGING FUNCTION:
  #   Utility function that:
  #   1. Extracts specified modules from MODULES_STAGING_DIR
  #   2. Creates lib/modules/${version} structure
  #   3. Runs depmod to generate dependency files
  #   4. Applies module blocklists if specified
  #
  # MKBOOTFS:
  #   Creates CPIO archive from staging directory
  #   CPIO format used by Linux kernel for initramfs
  #   Result: initramfs.cpio (uncompressed)
  #
  # RAMDISK_COMPRESS:
  #   Variable set in build config (usually gzip or lz4)
  #   Compresses CPIO to reduce size
  #   Result: initramfs.img (compressed)
  #
  # TYPICAL INITRAMFS CONTENTS:
  #   lib/modules/<version>/
  #   ├─ kernel/ (kernel .ko files)
  #   ├─ modules.dep (dependency list)
  #   ├─ modules.dep.bin (binary dependency list)
  #   ├─ modules.alias (module aliases)
  #   ├─ modules.options (module options)
  #   ├─ modules.load (modules to load)
  #   └─ modules.order
  #
  # modules.load:
  #   Text file listing modules to load at boot
  #   Format: one module per line, no .ko extension
  #   Examples:
  #     kernel/fs/ext4/ext4
  #     kernel/net/netfilter/nf_conntrack
  #
  # BOOT FLOW:
  #   1. Kernel starts with initramfs from boot image
  #   2. Reads modules.load from initramfs
  #   3. Loads each module from initramfs
  #   4. Switches root to real filesystem
  #
  # MODULES_OPTIONS:
  #   Optional file specifying module parameters at boot
  #   Format: options <module_name> <param1>=<val> ...
  #   Examples:
  #     options ext4 errors=remount-ro
  #     options usb_core autosuspend=10
  #
  # VENDOR_BOOT vs BOOT:
  #   - BUILD_VENDOR_BOOT_IMG: modules.load → vendor_boot.modules.load
  #   - BUILD_VENDOR_KERNEL_BOOT: modules.load → vendor_kernel_boot.modules.load
  #   Different boot images for different partitions/stages
  #############################################################################
  if [ "${BUILD_INITRAMFS}" = "1" ]; then
    echo "========================================================"
    echo " Creating initramfs"
    # Clean up previous initramfs staging
    rm -rf ${INITRAMFS_STAGING_DIR}
    # Create modules staging for initramfs
    # Parameters:
    # 1. MODULES_LIST: Which modules to include (optional)
    # 2. MODULES_STAGING_DIR: Source directory
    # 3. INITRAMFS_STAGING_DIR: Destination staging directory
    # 4. MODULES_BLOCKLIST: Modules to exclude (optional)
    # 5. "-e": Exclude modules not in list (from MODULES_LIST)
    create_modules_staging "${MODULES_LIST}" ${MODULES_STAGING_DIR} \
      ${INITRAMFS_STAGING_DIR} "${MODULES_BLOCKLIST}" "-e"

    # Get the modules directory for additional setup
    MODULES_ROOT_DIR=$(echo ${INITRAMFS_STAGING_DIR}/lib/modules/*)
    # Copy modules.load to DIST_DIR for reference
    cp ${MODULES_ROOT_DIR}/modules.load ${DIST_DIR}/modules.load
    # Copy to vendor_boot or vendor_kernel_boot if appropriate
    if [ -n "${BUILD_VENDOR_BOOT_IMG}" ]; then
      cp ${MODULES_ROOT_DIR}/modules.load ${DIST_DIR}/vendor_boot.modules.load
    elif [ -n "${BUILD_VENDOR_KERNEL_BOOT}" ]; then
      cp ${MODULES_ROOT_DIR}/modules.load ${DIST_DIR}/vendor_kernel_boot.modules.load
    fi
    # Write module options to initramfs if specified
    echo "${MODULES_OPTIONS}" > ${MODULES_ROOT_DIR}/modules.options

    # Create CPIO archive from staging directory
    # mkbootfs converts directory structure to CPIO format
    # Output: initramfs.cpio (uncompressed)
    mkbootfs "${INITRAMFS_STAGING_DIR}" >"${MODULES_STAGING_DIR}/initramfs.cpio"
    # Compress CPIO archive using configured compression
    # RAMDISK_COMPRESS is usually: gzip -9 or lz4 -12 --favor-decSpeed
    # Output: initramfs.img (compressed)
    ${RAMDISK_COMPRESS} "${MODULES_STAGING_DIR}/initramfs.cpio" >"${DIST_DIR}/initramfs.img"
  fi
fi

#################################################################################
# SYSTEM DLKM IMAGE GENERATION (GKI-SPECIFIC)
#
# PURPOSE:
#   Build system_dlkm.img partition containing signed GKI modules
#   DLKM = Downloadable Kernel Module
#   system_dlkm partition holds GKI modules separate from system partition
#
# USE CASE:
#   GKI architecture: Separate GKI modules from vendor modules
#   Allows updating GKI modules independently
#   More flexible than vendor_dlkm or ramdisk modules
#
# REQUIREMENTS:
#   - BUILD_SYSTEM_DLKM=1 must be set
#   - Must be in GKI build config (not device config)
#   - SYSTEM_DLKM_MODULES_LIST or MODULES_LIST must be provided
#
# PROCESS:
#   1. Create staging directory for system_dlkm
#   2. Copy modules using create_modules_staging
#   3. Re-sign modules with kernel signing key
#      - GKI modules are signed to prevent tampering
#      - Uses out/certs/signing_key.pem and signing_key.x509
#   4. Create erofs image (efficient read-only filesystem)
#      - erofs with lz4 compression for faster loading
#      - Smaller than ext4 alternatives
#   5. Create hashtree for Android Verified Boot (AVB)
#      - AVB ensures image not modified in transit
#   6. Archive staging directory for reference
#
# EROFS:
#   Enhanced Read-Only File System
#   - Designed for Android partitions
#   - Better compression and I/O than ext4 for read-only data
#   - -zlz4hc: Maximum lz4 compression for smaller size
#
# HASHTREE:
#   Merkle tree for verifying partition integrity
#   --partition_name: Device partition name (system_dlkm)
#   Used by boot loader to verify not tampered
#
# SIGNING:
#   - find ... -exec: Apply signing script to each .ko
#   - scripts/sign-file: Kernel's module signing utility
#   - sha1: Hashing algorithm for signature
#   - signing_key.pem: Private key (keep secret)
#   - signing_key.x509: Public certificate (embedded in kernel)
#################################################################################
if [ "${BUILD_SYSTEM_DLKM}" = "1"  ]; then
  echo "========================================================"
  echo " Creating system_dlkm image"

  rm -rf ${SYSTEM_DLKM_STAGING_DIR}
  # Use SYSTEM_DLKM_MODULES_LIST if available, otherwise MODULES_LIST
  create_modules_staging "${SYSTEM_DLKM_MODULES_LIST:-${MODULES_LIST}}" ${MODULES_STAGING_DIR} \
    ${SYSTEM_DLKM_STAGING_DIR} "${MODULES_BLOCKLIST}" "-e"

  SYSTEM_DLKM_ROOT_DIR=$(echo ${SYSTEM_DLKM_STAGING_DIR}/lib/modules/*)
  # Copy modules.load for boot system reference
  cp ${SYSTEM_DLKM_ROOT_DIR}/modules.load ${DIST_DIR}/system_dlkm.modules.load
  
  # Re-sign the stripped modules using kernel build time key
  # Modules must be signed to comply with kernel CONFIG_MODULE_SIG requirements
  find ${SYSTEM_DLKM_STAGING_DIR} -type f -name "*.ko" \
    -exec ${OUT_DIR}/scripts/sign-file sha1 \
    ${OUT_DIR}/certs/signing_key.pem \
    ${OUT_DIR}/certs/signing_key.x509 {} \;

  # Create erofs image with maximum lz4 compression
  mkfs.erofs -zlz4hc "${DIST_DIR}/system_dlkm.img" "${SYSTEM_DLKM_STAGING_DIR}"
  if [ $? -ne 0 ]; then
    echo "ERROR: system_dlkm image creation failed" >&2
    exit 1
  fi

  # Archive system_dlkm staging directory for reference/debugging
  tar -czf "${DIST_DIR}/system_dlkm_staging_archive.tar.gz" -C "${SYSTEM_DLKM_STAGING_DIR}" .

  # Create AVB (Android Verified Boot) hashtree footer
  # This allows boot loader to verify image integrity
  avbtool add_hashtree_footer \
    --partition_name system_dlkm \
    --image "${DIST_DIR}/system_dlkm.img"
fi

#################################################################################
# VENDOR DLKM IMAGE GENERATION
#
# PURPOSE:
#   Build vendor_dlkm.img partition for vendor-specific modules
#   Separate from system partition for modular updates
#
# WHEN USED:
#   When VENDOR_DLKM_MODULES_LIST is specified
#   Device kernel configs that need vendor modules partition
#
# VENDOR vs SYSTEM DLKM:
#   - system_dlkm: GKI/generic modules (Google-maintained)
#   - vendor_dlkm: Vendor-specific modules (OEM-maintained)
#
# MODULE TRIMMING:
#   Modules that appear in MODULES_LIST (ramdisk/vendor_boot):
#   - Automatically trimmed from vendor_dlkm
#   - Avoids duplicate modules in different partitions
#   - Prevents module loading confusion
#
# BUILD_VENDOR_DLKM FUNCTION:
#   Utility function that:
#   1. Creates vendor_dlkm staging directory
#   2. Copies specified modules
#   3. Trims modules already in vendor_boot
#   4. Creates vendor_dlkm.img
#   5. Signs with AVB if configured
#################################################################################
if [ -n "${VENDOR_DLKM_MODULES_LIST}" ]; then
  build_vendor_dlkm  # Call vendor DLKM build function from build_utils.sh
fi

#################################################################################
# UNSTRIPPED MODULES DISTRIBUTION
#
# PURPOSE:
#   Copy unstripped module .ko files for debugging
#   Unstripped modules contain full debug symbols
#   Used by debuggers/analysis tools
#
# UNSTRIPPED MODULES:
#   - Located in MODULES_PRIVATE_DIR
#   - Preserved during module build (not stripped)
#   - Much larger than stripped versions (~10x)
#   - Not installed on device (only in DIST_DIR for debugging)
#
# COMPRESS_UNSTRIPPED_MODULES:
#   If 1: Create tarball to save space
#   If 0: Keep as individual .ko files
#
# USE CASES:
#   - Crash debugging: Use symbols to analyze crash dumps
#   - Performance profiling: Map addresses to functions
#   - Code analysis: Understand module internals
#
# DIRECTORY STRUCTURE:
#   ${UNSTRIPPED_DIR} = ${DIST_DIR}/unstripped
#   Contains unstripped .ko files
#################################################################################
if [ -n "${UNSTRIPPED_MODULES}" ]; then
  echo "========================================================"
  echo " Copying unstripped module files for debugging purposes (not loaded on device)"
  mkdir -p ${UNSTRIPPED_DIR}
  for MODULE in ${UNSTRIPPED_MODULES}; do
    # Find and copy unstripped module file
    find ${MODULES_PRIVATE_DIR} -name ${MODULE} -exec cp {} ${UNSTRIPPED_DIR} \;
  done
  if [ "${COMPRESS_UNSTRIPPED_MODULES}" = "1" ]; then
    # Create tarball of unstripped modules
    tar -czf ${DIST_DIR}/${UNSTRIPPED_MODULES_ARCHIVE} -C $(dirname ${UNSTRIPPED_DIR}) $(basename ${UNSTRIPPED_DIR})
    # Remove uncompressed files to save space
    rm -rf ${UNSTRIPPED_DIR}
  fi
fi

#################################################################################
# GKI MODULES LIST DISTRIBUTION
#
# PURPOSE:
#   Copy GKI modules list to distribution directory
#   Identifies which modules are GKI vs vendor-specific
#
# USE CASE:
#   Downstream builds can distinguish:
#   - GKI modules: Compiled by Google, stable ABI
#   - Vendor modules: Compiled by OEM, device-specific
#
# FILE FORMAT:
#   Simple text file with module names, one per line
#   Example: gki_system_dlkm_modules
#   Contains: kernel/fs/ext4/ext4.ko, kernel/net/ipv4/tcp.ko, etc.
#################################################################################
[ -n "${GKI_MODULES_LIST}" ] && cp ${ROOT_DIR}/${KERNEL_DIR}/${GKI_MODULES_LIST} ${DIST_DIR}/

#################################################################################
# DISTRIBUTION COMPLETION MESSAGE
#
# PURPOSE:
#   Confirm all files copied to DIST_DIR
#   Show user where final artifacts are located
#################################################################################
echo "========================================================"
echo " Files copied to ${DIST_DIR}"

#################################################################################
# BOOT IMAGE GENERATION
#
# PURPOSE:
#   Create boot image (boot.img) for Android devices
#   Can also create vendor_boot image for header version >= 3
#
# BOOT IMAGE STRUCTURE:
#   boot.img contains:
#   - Boot header (version 1-4)
#   - Kernel image (Image, Image.lz4, etc.)
#   - Optional ramdisk (initramfs.img)
#   - Optional device tree (dtb)
#   - Optional vendor ramdisk (for header >= 3)
#
# BUILD_BOOT_IMG vs BUILD_VENDOR_BOOT_IMG:
#   - BUILD_BOOT_IMG: Create main boot image
#   - BUILD_VENDOR_BOOT_IMG: Create separate vendor_boot
#   - Both can be created; they contain different components
#
# BUILD_BOOT_IMAGES FUNCTION:
#   Utility that:
#   1. Validates required components exist
#   2. Calls mkbootimg.py to create image
#   3. Signs with AVB if configured
#   4. Outputs boot.img and optionally vendor_boot.img
#
# KLEAF CONTEXT:
#   Kleaf has equivalent boot image build rules
#   This shows kbuild-level implementation
#################################################################################
if [ -n "${BUILD_BOOT_IMG}" -o -n "${BUILD_VENDOR_BOOT_IMG}" ] ; then
  build_boot_images  # Call boot image build function from build_utils.sh
fi

#################################################################################
# GKI ARTIFACTS GENERATION (ARM64/x86_64 MULTI-COMPRESSION)
#
# PURPOSE:
#   Create multiple boot images with different kernel compressions
#   Allows devices to choose best compression for their needs
#
# USE CASE:
#   GKI certification: Provide compressed variants
#   Device can choose:
#   - Uncompressed Image: Fastest boot, largest size
#   - Image.gz: Good balance, standard compression
#   - Image.lz4: Good for slower storage, fast decompression
#
# PROCESS:
#   For each compressed variant in DIST_DIR:
#   - Create boot-${compression}.img
#   - Pack into boot-img.tar.gz
#   - Also create individual boot-*.img files
#
# ARCHITECTURE SUPPORT:
#   - ARM64: Multiple compression variants
#   - x86_64: Single bzImage boot image
#   - Others: Error out
#################################################################################
if [ -n "${BUILD_GKI_ARTIFACTS}" ] ; then
  build_gki_artifacts  # Call GKI artifacts function from build_utils.sh
fi

#################################################################################
# DEVICE TREE BLOB IMAGE GENERATION
#
# PURPOSE:
#   Package device tree overlays into dtbo.img
#   Flashed into dtbo partition on device
#
# DTBO.IMG:
#   - Contains multiple device tree blobs
#   - Loaded after main device tree
#   - Allows customization without modifying main DT
#
# USE CASE:
#   Different device variants with same kernel
#   dtbo.img allows customization per variant
#################################################################################
if [ -n "${BUILD_DTBO_IMG}" ]; then
  make_dtbo  # Call dtbo image build function from build_utils.sh
fi

#################################################################################
# TRACE_PRINTK DETECTION AND VERIFICATION
#
# PURPOSE:
#   Detect trace_printk usage in kernel
#   trace_printk is for kernel debugging, not production
#   Warns or fails build if found
#
# WHAT TRACE_PRINTK DOES:
#   - Fast kernel tracing mechanism
#   - Allocates buffer at boot time
#   - Causes boot warnings about trace_printk_init_buffers
#   - Not suitable for production kernels
#
# DETECTION METHOD:
#   readelf -a vmlinux | grep trace_printk_fmt
#   - Looks for trace_printk section in vmlinux
#   - Section present = trace_printk was used
#
# STOP_SHIP_TRACEPRINTK:
#   If set: Build fails on trace_printk detection
#   If not set: Warning only
#   "stop_ship" terminology: Stop before shipping to users
#
# PRODUCTION BUILD REQUIREMENT:
#   No trace_printk in production kernels
#   Remove all pr_debug with TRACE_BPRINTK or similar features
#################################################################################
# No trace_printk use on build server build
if readelf -a ${DIST_DIR}/vmlinux 2>&1 | grep -q trace_printk_fmt; then
  echo "========================================================"
  echo "WARN: Found trace_printk usage in vmlinux."
  echo ""
  echo "trace_printk will cause trace_printk_init_buffers executed in kernel"
  echo "start, which will increase memory and lead warning shown during boot."
  echo "We should not carry trace_printk in production kernel."
  echo ""
  if [ ! -z "${STOP_SHIP_TRACEPRINTK}" ]; then
    echo "ERROR: stop ship on trace_printk usage." 1>&2
    exit 1
  fi
fi

# End of build.sh script
