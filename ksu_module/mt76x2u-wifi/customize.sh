#!/system/bin/sh
# Install-time hook. Sourced by Magisk's update-binary and by KernelSU's ksud.
# Provides: ui_print, set_perm(_recursive), abort, MODPATH, SKIPUNZIP.

SKIPUNZIP=0

ui_print "- MT76x2U USB Wi-Fi (MT7612U) — systemless module"

# Permissions
set_perm_recursive "$MODPATH" 0 0 0755 0644
[ -f "$MODPATH/service.sh" ]      && set_perm "$MODPATH/service.sh" 0 0 0755
[ -f "$MODPATH/wifi-connect.sh" ] && set_perm "$MODPATH/wifi-connect.sh" 0 0 0755
# Bundled static wpa_supplicant/wpa_cli (from the wpa-supplicant CI) must be executable
[ -d "$MODPATH/bin" ] && set_perm_recursive "$MODPATH/bin" 0 0 0755 0755

KOCOUNT=$(ls -1 "$MODPATH"/modules/*.ko 2>/dev/null | wc -l)
FWCOUNT=$(ls -1 "$MODPATH"/firmware/*.bin 2>/dev/null | wc -l)
WPACNT=$(ls -1 "$MODPATH"/bin/wpa_supplicant 2>/dev/null | wc -l)
ui_print "- Bundled kernel modules : $KOCOUNT .ko"
ui_print "- Bundled firmware files : $FWCOUNT .bin"
ui_print "- Static wpa_supplicant  : $([ "$WPACNT" -gt 0 ] && echo yes || echo no)"

if [ "$KOCOUNT" -eq 0 ]; then
    ui_print "! This is the source template zip (no .ko bundled)."
    ui_print "! Flash the CI-produced *-mt76x2u-ksu-module.zip instead."
    abort   "! Aborting."
fi
if [ "$FWCOUNT" -eq 0 ]; then
    ui_print "! No firmware bundled — Wi-Fi will fail until mt7662.bin and"
    ui_print "! mt7662_rom_patch.bin exist in a kernel firmware search path."
fi

ui_print "- Reboot to load the driver. Plug the MT7612U dongle after boot."
