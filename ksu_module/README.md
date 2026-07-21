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

`service.sh`, at late_start:
1. **Firmware path** — prepends `$MODDIR/firmware` to
   `/sys/module/firmware_class/parameters/path` (preserving the preset
   `/vendor/firmware,/efs/wifi`), checked before `/lib/firmware`.
2. **SELinux** — `chcon`s the firmware to `vendor_firmware_file` so the kernel can
   read it in **enforcing** mode; backed by the bundled `sepolicy.rule` (no
   `setenforce 0`).
3. **Load** — `insmod`s the mt76 stack in dependency order. `insmod` loads by fd,
   unaffected by mount namespaces, and this kernel doesn't enforce module
   signatures (`CONFIG_MODULE_SIG_PROTECT` → `sig_enforce = false`).
4. **Bind** — binds an adapter that was already plugged in before the driver
   loaded (a post-boot hotplug auto-probes on its own).

No `system/` overlay, so **no `meta-overlayfs` metamodule is required**.

### USB mode-switch (kernel side)
The dongle first enumerates as `0e8d:2870` (ZeroCD mass-storage installer) and
must be ejected to `0e8d:7612` before the Wi-Fi driver binds. This kernel handles
it in-tree via `usb_stor_mt762x_init()` in
`drivers/usb/storage/{unusual_devs.h,initializers.c}` (the `usb_modeswitch -K`
equivalent), so **no on-device `usb_modeswitch` is needed** after a kernel rebuild.

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
