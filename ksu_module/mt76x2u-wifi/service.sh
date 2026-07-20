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

# 2) Load the mt76 stack in dependency order. cfg80211 is usually already
#    resident; mac80211 usually is NOT on MTK (connac is fullmac). Errors from
#    already-loaded modules are ignored.
for ko in cfg80211 mac80211 mt76 mt76-usb mt76x02-lib mt76x02-usb \
          mt76x2-common mt76x2u; do
    KO="$MODDIR/modules/$ko.ko"
    [ -f "$KO" ] || continue
    if insmod "$KO" 2>/dev/null; then
        say "insmod $ko: ok"
    else
        say "insmod $ko: skipped (already loaded or missing dep)"
    fi
done

say "done — resident mt76 modules: $(lsmod 2>/dev/null | grep -c '^mt76')"
