# Systemless Firmware & Kernel-Module Install via KernelSU-Next / Magisk

Research notes for shipping the **MT76x2U** USB Wi-Fi driver (`mt76x2u.ko` +
firmware `mt7662.bin` / `mt7662_rom_patch.bin`) without repacking `vendor.img`.
Grounded in the KernelSU-Next **v3.2.0** source vendored in this tree
(`kernel-5.15/KernelSU-Next`) and the A06x resolved `.config`.

---

## TL;DR

1. **Mounting** in KSU-Next 3.x is **not built-in** — it is delegated to a
   *metamodule* (`meta-overlayfs`). A module's `system/` tree is only overlaid
   if a metamodule is installed (`module.rs:766` gates on `system/` + no
   `skip_mount`). Magisk still has magic-mount (bind) built into its core.
2. **Firmware** is the hard part, not the `.ko`. This kernel's direct loader
   searches *custom paths first, then only `/lib/firmware`* — **`/vendor/firmware`
   is not in the kernel default array**. Two reliable routes exist (below).
3. **Module signing is a non-issue here.** `CONFIG_MODULE_SIG_PROTECT=y` makes
   `sig_enforce` a compile-time `false`, so *any* `.ko` loads — and an in-tree
   build signs `mt76x2u.ko` with `certs/mtk_signing_key.pem` anyway.
4. **Recommended:** a **script-based** module (`insmod` + `firmware_class.path`
   override). It needs no metamodule, no overlay, and dodges the firmware
   mount-namespace trap entirely.

---

## 1. How KernelSU-Next mounting actually works

### 1.1 Metamodule delegation (the 3.x change)

Older KSU / Magisk mount `$MODPATH/system` automatically. **KSU-Next 3.x moved
mounting out of the core into a pluggable *metamodule*** (`metamodule.rs`):

- The metamodule is discovered via a symlink `/data/adb/metamodule` or by
  scanning modules for `metamodule=1` in `module.prop`
  (`metamodule.rs::get_metamodule_path`).
- The official one is **`meta-overlayfs`**. Without a metamodule installed,
  **your `system/` directory is silently ignored.**
- Regular modules whose `system/` should be mounted are flagged in
  `module.rs:766`:
  ```rust
  let need_mount = path.join("system").exists() && !path.join("skip_mount").exists();
  ```

Implication: if you go the overlay route (§4B), you must **install
`meta-overlayfs` first**, or nothing mounts.

### 1.2 OverlayFS vs Magic Mount

| | Mechanism | Notes |
|---|---|---|
| **meta-overlayfs** (KSU-Next) | `mount -t overlay overlay -o lowerdir=/system,upperdir=$MOD/system,workdir=$MOD/work /system` | Kernel OverlayFS. Needs `CONFIG_OVERLAY_FS`. Layered, clean. |
| **Magic Mount** (Magisk core) | per-file **bind mounts** over individual target paths | No overlay dependency; higher mount count. |

Both apply to any partition — `/system`, and via the auto-generated symlink
`$MODID/system/vendor → /vendor`, also **`/vendor`** (where firmware lives).

### 1.3 Module `system/` layout & partition symlinks

Per the KSU module guide, inside a module:

```
$MODID/
├── module.prop            # id, name, version, versionCode(int), author, description  (LF endings!)
├── system/                # overlaid onto /system  (needs a metamodule in KSU-Next 3.x)
│   └── vendor/            # → /vendor   (symlink auto-generated: $MODID/vendor)
│       └── firmware/      # → /vendor/firmware
│           ├── mt7662.bin
│           └── mt7662_rom_patch.bin
├── post-fs-data.sh        # BLOCKING, runs BEFORE the metamodule mount
├── post-mount.sh          # runs AFTER the mount is up
├── service.sh             # NON-BLOCKING, late_start (good for insmod)
└── boot-completed.sh      # after ACTION_BOOT_COMPLETED
```

**Script timing (authoritative order):**
`post-fs-data` → *metamodule mount* → `post-mount` → `service` (parallel) →
`boot-completed`. So `/vendor/firmware` overlay content only exists from
`post-mount` onward, **not** during `post-fs-data`.

---

## 2. The firmware problem (this kernel's loader)

`drivers/base/firmware_loader/main.c` on this device (`CONFIG_FW_LOADER=y`,
`CONFIG_FW_LOADER_USER_HELPER=y`):

```c
// main.c:471
static const char * const fw_path[] = {
    fw_path_para[0], ... fw_path_para[9],     // 10 runtime-settable custom slots, checked FIRST
    "/lib/firmware/updates/" UTS_RELEASE,
    "/lib/firmware/updates",
    "/lib/firmware/" UTS_RELEASE,
    "/lib/firmware"                            // <-- NO /vendor/firmware in the kernel default!
};
// main.c:546
module_param_cb(path, &firmware_param_ops, NULL, 0644);
MODULE_PARM_DESC(path, "customized firmware image search path with a higher priority than default path");
```

So a `request_firmware("mt7662.bin")` from `mt76x2u` is served by one of:

- **Route K (kernel direct):** custom `fw_path_para[]` slots, then `/lib/firmware`.
  The custom slots are settable at runtime via
  `/sys/module/firmware_class/parameters/path` (comma-separated, higher priority).
- **Route U (userspace helper):** `CONFIG_FW_LOADER_USER_HELPER=y` lets Android
  **ueventd** serve firmware from its configured `firmware_directories`
  (which *do* include `/vendor/firmware`, `/vendor/etc/firmware`, …) when the
  kernel direct lookup misses.

### Mount-namespace trap
Route K reads the path **directly in kernel space** (init/global namespace).
A Magic-Mount/overlay done in a private namespace may be **invisible** to it.
That's why the cleanest systemless firmware method is to point the *kernel's own*
search path at a real directory, rather than trying to overlay `/lib/firmware`:

```sh
# runtime, higher priority than all defaults — no namespace issues
echo "$MODDIR/firmware" > /sys/module/firmware_class/parameters/path
```
Route U (overlay `system/vendor/firmware`) also works because ueventd reads the
*mounted* `/vendor` — but it depends on the metamodule + overlay being visible
to ueventd, and is more moving parts.

> ⚠️ Writing `.../parameters/path` **replaces** the custom slots. If the vendor
> preset a value (via `firmware_class.path=` on cmdline), read-modify-write to
> preserve it (see the template in §5).

---

## 3. The module-loading problem (this kernel's signing)

Config: `CONFIG_MODULE_SIG=y`, `CONFIG_MODULE_SIG_PROTECT=y`,
`CONFIG_MODULE_SIG_KEY="certs/mtk_signing_key.pem"`, **no** `MODULE_SIG_FORCE`,
**no** lockdown.

```c
// kernel/module.c:274
#if defined(CONFIG_MODULE_SIG) && !defined(CONFIG_MODULE_SIG_PROTECT)
static bool sig_enforce = IS_ENABLED(CONFIG_MODULE_SIG_FORCE);
...
#else
#define sig_enforce false        // MODULE_SIG_PROTECT is set → constant false
#endif
bool is_module_sig_enforced(void) { return sig_enforce; }   // always false here

// module.c:2966 (unsigned module path)
if (is_module_sig_enforced()) { ...return -EKEYREJECTED; }   // never taken
#else /* CONFIG_MODULE_SIG_PROTECT */
    return 0;                                                // unsigned → allowed
#endif
```

**Conclusion:** on this A06x kernel, signature enforcement is a **no-op** —
any `.ko` loads. And because you build `mt76x2u.ko` *in this same tree*,
`CONFIG_MODULE_SIG=y` auto-signs it with the embedded MTK key, so it is a
first-class signed module regardless. No `sig_enforce`, no tainting concerns.

### insmod vs modprobe & load order
A bare module dir has no `modules.dep`, so use **`insmod` in dependency order**.
`cfg80211`/`mac80211` are **already resident** on the device (the stock
`wlan_drv_gen4m` pulls them in) — do **not** bundle or load duplicates, which
would risk a version mismatch. Load only the mt76 stack:

```
mt76.ko → mt76-usb.ko → mt76x02-lib.ko
→ mt76x02-usb.ko → mt76x2-common.ko → mt76x2u.ko
```

All must share the **running kernel's vermagic** — which they do, since they come
out of the same build. Errors from already-loaded modules are ignored.

---

## 4. Two proper install methods

### Method A — script-based (recommended)
No metamodule, no overlay, no namespace trap.

- `.ko` files load via `insmod` (kernel reads the file by fd — mount namespace
  irrelevant).
- firmware found via `firmware_class.path` override (Route K).
- Load in `service.sh` (non-blocking, late_start) — a USB dongle isn't needed
  early, and `service.sh` runs after data is up.

### Method B — overlay-based (systemless /vendor)
- Put `.ko` under `system/vendor/lib/modules/` and firmware under
  `system/vendor/firmware/`, let the overlay place them on `/vendor`.
- **Requires `meta-overlayfs` installed** (KSU-Next 3.x).
- Still usually needs a script to actually `insmod` (Android won't auto-modprobe
  a USB dongle), and firmware then rides Route U. More parts, more failure modes.
- Only pick this if you want the files to *appear* under `/vendor` for other
  consumers; otherwise Method A is strictly simpler.

---

## 5. Concrete module template (Method A) — `mt76x2u-wifi`

```
mt76x2u-wifi/
├── module.prop
├── service.sh
├── firmware/
│   ├── mt7662.bin
│   └── mt7662_rom_patch.bin
└── modules/
    ├── mt76.ko  mt76-usb.ko  mt76x02-lib.ko
    ├── mt76x02-usb.ko  mt76x2-common.ko
    └── mt76x2u.ko
```

**module.prop** (LF line endings, `versionCode` integer):
```
id=mt76x2u_wifi
name=MT76x2U USB Wi-Fi (MT7612U)
version=v1.0
versionCode=1
author=Jerickson Mayor
description=Loads the mt76x2u USB Wi-Fi stack and provides its firmware, systemless.
```

**service.sh**:
```sh
#!/system/bin/sh
MODDIR=${0%/*}

# 1) Point the kernel firmware loader at our dir (highest priority),
#    preserving any existing custom path.
FWP=/sys/module/firmware_class/parameters/path
CUR=$(cat "$FWP" 2>/dev/null)
case ",$CUR," in
  *",$MODDIR/firmware,"*) : ;;                         # already present
  *) echo "$MODDIR/firmware${CUR:+,$CUR}" > "$FWP" ;;  # prepend
esac

# 2) Load the mt76 stack; ignore "already loaded".
#    cfg80211/mac80211 are already resident (stock wlan_drv_gen4m) — don't
#    bundle or reload them.
for ko in mt76 mt76-usb mt76x02-lib mt76x02-usb mt76x2-common mt76x2u; do
    insmod "$MODDIR/modules/$ko.ko" 2>/dev/null
done

log -t mt76x2u_wifi "load done: $(lsmod | grep -c mt76) mt76 modules resident"
```

Notes / caveats:
- **SELinux:** writing the firmware sysfs param and `insmod` run in KSU's
  `su`-ish domain, which KSU's built-in sepolicy normally permits. If a write is
  denied, check `dmesg | grep avc` and add a rule via `sepolicy.rule`.
- The dongle's firmware is only requested when it's **plugged in**, so loading at
  boot with no device present is fine — the driver waits.
- If you'd rather auto-load only on hotplug, ship `modules.alias` + a ueventd
  trigger; not worth it for a single dongle.
- Package this as a flashable zip (KSU/Magisk installer format) or just drop the
  folder in `/data/adb/modules/` and reboot.

---

## 6. Verified end-to-end on real hardware (SM-A066B + Comfast CF-WU785AC)

A full bring-up of an MT7612U dongle confirmed the design and refined it:

**What was confirmed**
- **`firmware_class.path` works; the `/vendor/firmware` overlay does not.** On this
  device `/vendor/firmware/mediatek/` does not exist, so magic-mount has nothing to
  anchor and the overlay route is a dead end — exactly why §4A is the default.
- The path is **preset to `/vendor/firmware,/efs/wifi`**, so `service.sh` must
  read-modify-write (prepend), never clobber. Confirmed.
- The driver requests **`mt7662.bin` / `mt7662_rom_patch.bin`** (flat, no `u`, no
  `mediatek/`). The USB-specific `mt7662u.*` contents work fine under those names,
  so the packager fetches the plain names and falls back to the `u` variants.

**Three refinements now baked into the module + kernel**
1. **USB mode-switch is a prerequisite.** The dongle first enumerates as
   `0e8d:2870` (ZeroCD mass-storage installer) and must be ejected to `0e8d:7612`
   before anything binds. Instead of a hand-built on-device `usb_modeswitch`, an
   in-kernel eject entry was added:
   `drivers/usb/storage/{unusual_devs.h,initializers.c,initializers.h}` →
   `usb_stor_mt762x_init()` sends the SCSI START STOP UNIT eject at probe (the
   `usb_modeswitch -K` equivalent). After a kernel rebuild, plug-in auto-switches.
2. **SELinux (enforcing).** `request_firmware` from `u:r:kernel:s0` cannot read
   files under `/data` by default (observed denial: kernel vs `shell_data_file`).
   `service.sh` `chcon`s the bundled firmware to `vendor_firmware_file` and the
   module ships a `sepolicy.rule`, so no `setenforce 0` is needed.
3. **Already-plugged adapter doesn't auto-probe.** A hotplug after boot probes
   itself, but a dongle present when the driver loads stays on the generic `usb`
   driver. `service.sh` now scans for `0e8d:7612/7632` and writes the interface to
   `…/mt76x2u/bind`.

### Confirmed on-device (gen4m notifier fix)

`ip addr add 192.168.18.214/24 dev wlan1` now completes without rebooting, and
the interface holds the address. The fix is verified *in the running module*,
not just inferred, by the log line the guard emits:

```bash
su -c "dmesg -c > /dev/null"
su -c "ip addr del <ip>/24 dev wlan1"; su -c "ip addr add <ip>/24 dev wlan1"
su -c "dmesg | grep -i netdev_event"
# [wlan][3712]netdev_event:(REQ INFO) netdev_event: wlan1 is not ours, skipping.
```

To check the patch is compiled into the module without triggering it:

```bash
su -c "find /vendor /vendor_dlkm -name 'wlan_drv_gen4m*.ko' 2>/dev/null"
su -c "grep -ac 'is not ours' <path to wlan_drv_gen4m_*.ko>"   # >=1 => patched
```

### Confirmed: the boot failure was boot.img alone

A build that did not boot was isolated to **`boot.img` only** — `vendor_boot` and
`vendor_dlkm` were fine. Cause was the KMI string in vermagic. Note how the
release is assembled (`scripts/setlocalversion` ~line 188):

```
KERNELRELEASE = 5.15.151 + "-android13-8" + CONFIG_LOCALVERSION
stock:          5.15.151-android13-8-30546824  => CONFIG_LOCALVERSION="-30546824"
observed bad:   5.15.151-android13-8-a06x-dev  => stock vendor modules rejected
```

`-android13-8` is added automatically, so `CONFIG_LOCALVERSION` must be **only**
the trailing build id — putting the prefix there emits it twice and still fails.
Any mismatch makes every stock vendor module fail to load: no display/touch/PMIC,
no boot animation, and **no panic**, so `last_kmsg` shows nothing useful.

**Userspace note (out of module scope):** the vendor
`/vendor/bin/hw/wpa_supplicant` is HAL-locked (`hal_wifi_supplicant_default_exec`)
and won't run standalone. Station-mode scanning + monitor mode were validated by
running a **standard `wpa_supplicant` (v2.10) inside a Droidspaces container in
Host-network mode**, with `wlan1` directly visible. `tcpdump` confirmed raw 802.11
capture; `airodump-ng`/`wifite` failed on a known Ubuntu-vs-Kali `PACKET_MR_PROMISC`
tooling gap, not a driver issue.

## 7. Sources

- KernelSU — Module guide: <https://kernelsu.org/guide/module.html>
- KernelSU — Metamodule: <https://kernelsu.org/guide/metamodule.html>
- KernelSU — Difference with Magisk: <https://kernelsu.org/guide/difference-with-magisk.html>
- KernelSU-Next home: <https://kernelsu-next.github.io/webpage/>
- Magical OverlayFS (KSU): <https://kernelsu.gitlab.io/ksu-modules-repo/magical-overlayfs/>
- Hybrid Mount module: <https://modules.kernelsu.org/module/hybrid_mount/>
- KernelSU-Next repo: <https://github.com/KernelSU-Next/KernelSU-Next>
- In-tree evidence: `kernel-5.15/KernelSU-Next/userspace/ksud/src/{metamodule,module}.rs`,
  `kernel-5.15/drivers/base/firmware_loader/main.c:471-547`,
  `kernel-5.15/kernel/module.c:274-296,2918-2980`
```
