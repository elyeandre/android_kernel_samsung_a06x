#!/system/bin/sh
# MT76x2U USB Wi-Fi (MT7612U) — systemless loader.
# Runs at late_start service (NON-BLOCKING); /data is up by now.
# Rationale + kernel details: docs/kernelsu_firmware_module_install.md
MODDIR=${0%/*}
TAG=mt76x2u_wifi
say() { log -t "$TAG" "$1" 2>/dev/null; echo "$TAG: $1" >> "$MODDIR/load.log" 2>/dev/null; }

: > "$MODDIR/load.log" 2>/dev/null
say "start (MODDIR=$MODDIR)"

# 1) Register our bundled firmware dir with the kernel loader (checked BEFORE
#    /lib/firmware), preserving any path the vendor preset. This is the reliable
#    route: request_firmware() reads the path directly, so it is immune to the
#    mount-namespace issues that affect overlay/magic-mount firmware.
FWP=/sys/module/firmware_class/parameters/path
if [ -e "$FWP" ]; then
    CUR=$(cat "$FWP" 2>/dev/null)
    case ",$CUR," in
        *",$MODDIR/firmware,"*) say "fw path already set" ;;
        *) if echo "$MODDIR/firmware${CUR:+,$CUR}" > "$FWP" 2>/dev/null; then
               say "fw path -> $(cat "$FWP" 2>/dev/null)"
           else
               say "WARN: could not write $FWP (SELinux/denied)"
           fi ;;
    esac
else
    say "WARN: $FWP missing (CONFIG_FW_LOADER not exposing path?)"
fi

# 1b) Relabel the firmware so the kernel domain (u:r:kernel:s0) may read it in
#     ENFORCING mode. Files under /data/adb are not a type the kernel can read,
#     so without this the load only works under `setenforce 0`. vendor_firmware_file
#     is a type the kernel already reads for normal firmware. Backed by sepolicy.rule.
if [ -d "$MODDIR/firmware" ]; then
    if chcon -R u:object_r:vendor_firmware_file:s0 "$MODDIR/firmware" 2>/dev/null; then
        say "firmware relabeled -> vendor_firmware_file"
    else
        say "WARN: chcon firmware failed (check dmesg | grep avc; see sepolicy.rule)"
    fi
fi

# 2) Load the mt76 stack in dependency order. cfg80211/mac80211 are NOT loaded
#    here — they are already resident on the device (the stock wlan_drv_gen4m
#    pulls cfg80211 in), and loading duplicates risks a version mismatch.
#    Errors from already-loaded modules are ignored.
for ko in mt76 mt76-usb mt76x02-lib mt76x02-usb mt76x2-common mt76x2u; do
    KO="$MODDIR/modules/$ko.ko"
    [ -f "$KO" ] || continue
    if insmod "$KO" 2>/dev/null; then
        say "insmod $ko: ok"
    else
        say "insmod $ko: skipped (already loaded or missing dep)"
    fi
done

# 3) Bind an adapter that was already plugged in before the driver loaded.
#    (A hotplug AFTER boot auto-probes; a dongle present at boot stays on the
#    generic usb driver until we bind its interface explicitly.) Requires the
#    device to be in Wi-Fi mode already (0e8d:7612/7632) — the in-kernel
#    unusual_devs.h eject handles the 2870->7612 switch on this kernel.
DRV=/sys/bus/usb/drivers/mt76x2u
if [ -d "$DRV" ]; then
    for dev in /sys/bus/usb/devices/*; do
        [ -f "$dev/idVendor" ] || continue
        [ "$(cat "$dev/idVendor" 2>/dev/null)" = "0e8d" ] || continue
        case "$(cat "$dev/idProduct" 2>/dev/null)" in 7612|7632) ;; *) continue ;; esac
        for intf in "$dev":*; do
            [ -e "$intf" ] || continue
            ifn=$(basename "$intf")
            [ -e "$DRV/$ifn" ] && { say "already bound: $ifn"; continue; }
            if [ -L "$intf/driver" ]; then
                cur=$(basename "$(readlink "$intf/driver")")
                [ "$cur" = "mt76x2u" ] && continue
                printf '%s' "$ifn" > "/sys/bus/usb/drivers/$cur/unbind" 2>/dev/null
            fi
            if printf '%s' "$ifn" > "$DRV/bind" 2>/dev/null; then
                say "bound $ifn -> mt76x2u"
            else
                say "bind $ifn failed (enforcing SELinux? check dmesg avc)"
            fi
        done
    done
fi

say "done — resident mt76 modules: $(lsmod 2>/dev/null | grep -c '^mt76')"
