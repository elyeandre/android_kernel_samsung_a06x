# KernelSU-Next / Magisk module — MT76x2U USB Wi-Fi

Systemless install of the **MT76x2U** (MT7612U) USB 802.11ac driver and its
firmware, so no `vendor.img` repack is needed. Background and the kernel-level
reasoning are in [`docs/kernelsu_firmware_module_install.md`](../docs/kernelsu_firmware_module_install.md).

## Layout

```
ksu_module/
├── build_module_zip.sh          # packs the flashable zip (CI + local)
└── mt76x2u-wifi/                # module source template
    ├── module.prop
    ├── service.sh              # late_start: firmware path + insmod the stack
    ├── customize.sh            # install-time perms + sanity checks
    ├── META-INF/…/update-binary # Magisk/recovery installer (ignored by KSU)
    ├── modules/  (.gitkeep)    # CI fills with the built .ko
    └── firmware/ (.gitkeep)    # CI fills with mt7662*.bin
```

The `modules/` and `firmware/` dirs ship empty in git; the template zip refuses
to install (see `customize.sh`). Only the **CI-built** zip is flashable.

## How it works (script-based, no metamodule)

- **`service.sh`** points the kernel firmware loader at the bundled firmware via
  `/sys/module/firmware_class/parameters/path` (checked before `/lib/firmware`),
  then `insmod`s the mt76 stack in dependency order. `insmod` loads by fd, so it
  is unaffected by mount namespaces, and this kernel does not enforce module
  signatures (`CONFIG_MODULE_SIG_PROTECT` → `sig_enforce = false`).
- No `system/` overlay, so **no `meta-overlayfs` metamodule is required**.

## Build the flashable zip

CI does this automatically (see `.github/workflows/kernel-build.yml`). Locally,
after a kernel build with `CONFIG_MT76x2U=m`:

```bash
bash ksu_module/build_module_zip.sh dist/mt76x2u-ksu-module.zip
```

It pulls the `.ko` from `out/…/KERNEL_OBJ/staging` (falls back to the object
tree) and fetches `mt7662.bin` / `mt7662_rom_patch.bin` from linux-firmware.

## Install

Flash the CI zip in the **KernelSU-Next** manager (Modules → Install from
storage) or the Magisk app, then reboot. Plug the dongle after boot; check
`ksu_module`'s `load.log` inside the module dir or `dmesg | grep mt76`.
