# Build Flow — SM-A066B (A06X) Kernel

Device: Samsung Galaxy A06X (SM-A066B)
Firmware: A066BXXS3AYI3 · One UI 8 (Android 15)
Kernel: 5.15 · SoC: MediaTek MT6835

---

## Overview

Three images are produced and flashed:

| Image | Partition | Contains |
|-------|-----------|----------|
| `boot.img` | `boot` | kernel `Image` + generic init ramdisk |
| `vendor_boot.img` | `vendor_boot` | platform ramdisk (`ramdisk.img.lz4`) + early-boot `.ko` modules + DTB |
| `vendor_dlkm.img` | `vendor_dlkm` | erofs partition with most vendor `.ko` modules |

---

## Stage 1 — Kernel Compilation (`build.sh`)

```
build.sh
  ├── generates build.config via kernel-5.15/scripts/gen_build_config.py
  ├── sets defconfig: a06x_00_defconfig + entry_level.config overlay
  ├── compiles kernel-5.15/ with Clang (clang-r450784e)
  └── produces:
       ├── out/.../KERNEL_OBJ/kernel-5.15/arch/arm64/boot/Image   (uncompressed kernel)
       ├── out/.../KERNEL_OBJ/staging/*.ko                         (built modules)
       ├── out/.../KERNEL_OBJ/kernel-5.15/System.map               (symbol table)
       └── dist/boot.img  (packed by prebuilts/mkbootimg.py)
```

`boot.img` is a standard Android boot image v4 containing:
- kernel `Image` (uncompressed)
- a minimal generic ramdisk

KernelSU-Next is compiled **into** the kernel at this stage (not a module — see Stage 4).

---

## Stage 2 — vendor_dlkm rebuild (`build_vendor_dlkm.sh`)

```
prebuilts_a06x/scripts/build_vendor_dlkm.sh
  │
  ├── 1. setup_workspace()
  │     Copies prebuilts_a06x/vendor_dlkm/extracted_vendor_dlkm/
  │       → out/vendor_dlkm_work/staging/
  │     (skeleton: etc/, .repack_info/ — no .ko files committed)
  │     Removes .gitkeep placeholder, creates staging/lib/modules/
  │
  ├── 2. package_modules()
  │     Calls LKM_Tools/03.prepare_vendor_dlkm.sh with:
  │       - modules_list.txt  → which .ko files belong here
  │       - staging/          → source of freshly built .ko files
  │       - modules.load      → load order
  │       - System.map        → symbol map for dep resolution
  │       - llvm-strip        → strip debug symbols
  │       → writes stripped .ko + modules.{dep,alias,load} to out/vendor_dlkm_work/modules_out/
  │
  └── 3. repack_image()
        Merges modules_out/ into staging/lib/modules/
        Runs: mkfs.erofs --all-root \
                --file-contexts=.repack_info/file_contexts.txt \
                -z lz4 -T <stock_timestamp> \
                dist/vendor_dlkm.img  staging/
```

The `extracted_vendor_dlkm/` skeleton committed in the repo:
```
prebuilts_a06x/vendor_dlkm/extracted_vendor_dlkm/
  ├── etc/
  │   ├── build.prop
  │   ├── fs_config_dirs
  │   ├── fs_config_files
  │   └── NOTICE.xml.gz
  ├── .repack_info/
  │   ├── file_contexts.txt   ← SELinux labels (pattern-based, used by mkfs.erofs)
  │   ├── fs-config.txt
  │   ├── metadata.txt
  │   ├── original_checksums.txt
  │   └── symlink_info.txt
  └── lib/modules/.gitkeep    ← empty placeholder, removed at build time
```

---

## Stage 3 — vendor_boot rebuild (`build_vendor_boot.sh`)

```
prebuilts_a06x/scripts/build_vendor_boot.sh
  │
  ├── 1. setup_boot_editor()
  │     Clones cfig/Android_boot_image_editor v15_r1 (if not present)
  │       → prebuilts_a06x/boot_editor/  (gitignored, cloned at CI time)
  │     Restores pre-extracted state:
  │       Copies prebuilts_a06x/vendor_boot/unzip_boot/
  │         → boot_editor/build/unzip_boot/
  │     Sets JAVA_HOME to JDK 17 (required by boot_editor Gradle)
  │
  ├── 2. package_modules()
  │     Calls LKM_Tools/02.prepare_vendor_boot_modules.sh with:
  │       - vendor_boot/modules_list.txt
  │       - staging/  (built .ko files)
  │       - vendor_boot/modules.load
  │       - System.map
  │       - llvm-strip
  │       → writes stripped .ko to boot_editor/build/unzip_boot/root.1/lib/modules/
  │
  └── 3. repack_image()
        cd boot_editor && ./gradlew pack
        Gradle repacks unzip_boot/ → vendor_boot.img.signed
        Moved to dist/vendor_boot.img
```

The `unzip_boot/` skeleton committed in the repo:
```
prebuilts_a06x/vendor_boot/unzip_boot/
  ├── ramdisk.img.lz4              ← 9MB — type PLATFORM ramdisk (kept as-is)
  ├── dtb                          ← 196KB — device tree binary
  ├── vendor_boot.json             ← boot_editor header params
  ├── vendor_boot.avb.json         ← AVB signing params
  └── root.1/
      ├── first_stage_ramdisk/
      │   ├── fstab.mt6835         ← mount table (AVB flags removed — no bootloop)
      │   └── dpolicy
      └── lib/modules/.gitkeep    ← empty placeholder, replaced by LKM_Tools
```

---

## Stage 4 — KernelSU-Next

KernelSU-Next is a **git submodule** at `kernel-5.15/KernelSU-Next/` and is compiled
**directly into the kernel binary** — not as a loadable module.

How it integrates:

```
kernel-5.15/KernelSU-Next/kernel/
  ├── core_hook.c     hooks into fs/exec.c (execve), fs/open.c (faccessat)
  ├── sucompat.c      su binary compatibility layer
  ├── allowlist.c     manages which UIDs are granted root
  ├── ksu.c           main driver, exposes /dev/ksud
  └── ...
```

At build time, `build.sh` passes `CONFIG_KSU=y` which includes the KernelSU-Next
driver in the kernel image.

At runtime:
1. Kernel boots → KSU driver is live in memory
2. KernelSU manager app communicates via `/dev/ksud`
3. `su` requests are intercepted in `execve()` → UID check → root granted if allowed
4. LSM hooks enforce the permission model without modifying SELinux policy

Version shown in the manager app = `30000 + git rev-list --count HEAD` of the submodule.

---

## CI Pipeline (`.github/workflows/kernel-build.yml`)

```
Trigger: push to main  OR  workflow_dispatch
Runner:  ubuntu-22.04

Steps:
  1. checkout (recursive submodules)
  2. free disk space
  3. generate release tag  (rYYYYMMDD-<hex>)
  4. get KernelSU-Next version
  5. build.sh               → dist/boot.img  + out/.../staging/*.ko
  6. verify dist/boot.img exists
  7. generate built_modules.txt
  8. apt install erofs-utils
     build_vendor_dlkm.sh  → dist/vendor_dlkm.img
     build_vendor_boot.sh  → dist/vendor_boot.img
  9. tar -cvf <name>.tar boot.img vendor_dlkm.img vendor_boot.img
 10. upload artifact (.tar.zip)
 11. create GitHub Release (on main branch builds)
```

---

## Flashing via Odin

Extract `boot.img` from the `.tar` archive (the tar contains all three images).

| Odin slot | Image | Notes |
|-----------|-------|-------|
| AP | `boot.img` | kernel + KernelSU-Next |
| — | `vendor_boot.img` | flash manually if needed |
| — | `vendor_dlkm.img` | flash manually if needed |

For most use-cases flashing only `boot.img` via AP is sufficient — the rebuilt
`vendor_boot.img` and `vendor_dlkm.img` are only needed if driver changes were made.

---

## Key Paths

| Purpose | Path |
|---------|------|
| Kernel source | `kernel-5.15/` |
| KernelSU-Next submodule | `kernel-5.15/KernelSU-Next/` |
| Build script | `build.sh` |
| Clang toolchain | `kernel/prebuilts/clang/host/linux-x86/clang-r450784e/` |
| LKM_Tools | `prebuilts_a06x/LKM_Tools/` |
| vendor_dlkm skeleton | `prebuilts_a06x/vendor_dlkm/extracted_vendor_dlkm/` |
| vendor_boot skeleton | `prebuilts_a06x/vendor_boot/unzip_boot/` |
| Boot editor (CI-cloned) | `prebuilts_a06x/boot_editor/` (gitignored) |
| Build output | `out/target/product/a06x/obj/KERNEL_OBJ/` |
| Flashable images | `dist/` |
