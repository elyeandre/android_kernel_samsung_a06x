#!/system/bin/sh
# wifi-connect.sh <SSID> [PSK] — bring wlan1 up and associate using the bundled
# static wpa_supplicant. Run as root. Omit PSK for an open network.
#
# Uses this module's bin/ if present, else /data/local/tmp/wpa.
MODDIR=${0%/*}
SSID="$1"; PSK="${2:-}"
[ -n "$SSID" ] || { echo "usage: $0 <SSID> [PSK]"; exit 1; }

WPAS="$MODDIR/bin/wpa_supplicant"; WPAC="$MODDIR/bin/wpa_cli"
[ -x "$WPAS" ] || WPAS=/data/local/tmp/wpa/wpa_supplicant
[ -x "$WPAC" ] || WPAC=/data/local/tmp/wpa/wpa_cli
[ -x "$WPAS" ] && [ -x "$WPAC" ] || {
    echo "wpa_supplicant/wpa_cli not found. Build them (see"
    echo "docs/wpa_supplicant_static_termux.md) into $MODDIR/bin or /data/local/tmp/wpa."
    exit 1
}

DIR=/data/local/tmp/wpa; mkdir -p "$DIR"
CONF="$DIR/wlan1.conf"
{
    echo "ctrl_interface=$DIR/ctrl"
    echo "update_config=1"
    echo "network={"
    printf '\tssid="%s"\n' "$SSID"
    if [ -n "$PSK" ]; then printf '\tpsk="%s"\n' "$PSK"; else echo "	key_mgmt=NONE"; fi
    echo "}"
} > "$CONF"

ip link set wlan1 up 2>/dev/null
killall wpa_supplicant 2>/dev/null
"$WPAS" -B -Dnl80211 -i wlan1 -c "$CONF"
sleep 3
"$WPAC" -p "$DIR/ctrl" -i wlan1 status
echo "--- associated? if so, get an IP with a DHCP client on wlan1 (dhcpcd/dhcptool) ---"
