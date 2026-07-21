# Static `wpa_supplicant` + `wpa_cli` for A06x (on-device Termux build)

Distilled, working procedure for building **standalone static** `wpa_supplicant`
and `wpa_cli` on the device (Termux) to drive the external MT7612U adapter
(`wlan1`) — the vendor `/vendor/bin/hw/wpa_supplicant` is HAL-locked and can't
run standalone. Verified: the resulting binaries link `libc.so`/`libdl.so` only
(libnl baked in) and run as root on `wlan1`.

Prereqs: the mt76x2u driver + firmware are already up and `wlan1` exists — see
[kernelsu_firmware_module_install.md](kernelsu_firmware_module_install.md).

> **Build as the normal Termux user, not `su`/`.suroot`.** You only need root to
> *run* wpa_supplicant, never to compile it. Building under `su` loses the
> Termux toolchain env.

---

## The two gotchas that cost 8 rounds (read first)

1. **`git://` is blocked** (TCP 9418, Termux/carriers). Use the HTTPS release
   tarball, not `git clone git://w1.fi/...`.
2. **This Termux sysroot never declares `in_addr_t` / `in_port_t`.** No feature
   macro (`_GNU_SOURCE`, etc.) and no include-order fix exposes them — the
   typedefs are simply absent from `<netinet/in.h>`. The only thing that works
   is to **define them on the command line**:
   `-Din_addr_t=uint32_t -Din_port_t=uint16_t`. This is safe *because* nothing
   on this system does `typedef … in_addr_t`, so there's no conflict. (`-D`
   survives libtool; `-include <file>` does **not** — libtool eats the bare
   filename, which is why forcing a header failed.)

---

## Part A — static libnl 3.7.0

```bash
pkg install -y clang make pkg-config flex bison   # NOT 'libnl' — we build it static

cd ~
curl -LO https://github.com/thom311/libnl/releases/download/libnl3_7_0/libnl-3.7.0.tar.gz
tar xf libnl-3.7.0.tar.gz
cd libnl-3.7.0

./configure --prefix="$HOME/nl-static" --enable-static --disable-shared --disable-cli \
    CPPFLAGS="-Din_addr_t=uint32_t -Din_port_t=uint16_t"
make -j"$(nproc)"
make install
ls -la "$HOME/nl-static/lib/"*.a          # -> libnl-3.a, libnl-genl-3.a (+ route/nf/…)
```

Notes:
- `libnl 3.12` fails the same way; version is irrelevant — it's the sysroot.
- `--disable-shared` means the dir holds only `.a`, so nothing can accidentally
  link the shared libnl later.

---

## Part B — static wpa_supplicant 2.11

```bash
cd ~
curl -LO https://w1.fi/releases/wpa_supplicant-2.11.tar.gz
tar xf wpa_supplicant-2.11.tar.gz
cd wpa_supplicant-2.11/wpa_supplicant

cat > .config <<EOF
CONFIG_DRIVER_NL80211=y
CONFIG_LIBNL32=y
CONFIG_CTRL_IFACE=y
CONFIG_BACKEND=file
CONFIG_TLS=internal
CONFIG_INTERNAL_LIBTOMMATH=y
CONFIG_WPA=y
CONFIG_IEEE80211W=y
CONFIG_WPA_CLI_EDIT=y
CFLAGS += -I$HOME/nl-static/include/libnl3 -Din_addr_t=uint32_t -Din_port_t=uint16_t
LIBS   += -L$HOME/nl-static/lib -Wl,-Bstatic -lnl-genl-3 -lnl-3 -Wl,-Bdynamic
EOF

make -j"$(nproc)" wpa_supplicant wpa_cli
readelf -d wpa_supplicant | grep NEEDED   # PASS = only libc.so / libdl.so, no libnl
```

Why this `.config`:
- `CONFIG_TLS=internal` (+ `INTERNAL_LIBTOMMATH`) → **no OpenSSL dependency**;
  libnl is then the only external lib, and we static-link it.
- **No `CONFIG_SAE`.** WPA3/SAE needs elliptic-curve crypto (`crypto_ec_*`,
  `crypto_bignum_*`) that the internal backend doesn't implement — only
  OpenSSL/wolfSSL do — so it fails at link. WPA2-PSK is fully covered without it.
- `-Wl,-Bstatic … -Wl,-Bdynamic` forces libnl from the `.a` while keeping libc
  dynamic (bionic system lib, always present).
- The `Package libnl-3.0 was not found` pkg-config warning is harmless — the
  build falls back to the explicit `-I`/`-L`.
- `wpa_supplicant` uses a plain Makefile (no libtool), so the `-D` flags apply
  cleanly here (unlike the libnl build).

---

## Deploy & run (as root, no LD_LIBRARY_PATH needed)

```bash
su -c "mkdir -p /data/local/tmp/wpa"
su -c "cp ~/wpa_supplicant-2.11/wpa_supplicant/wpa_supplicant \
          ~/wpa_supplicant-2.11/wpa_supplicant/wpa_cli /data/local/tmp/wpa/"
su -c "chmod 755 /data/local/tmp/wpa/wpa_supplicant /data/local/tmp/wpa/wpa_cli"

# station mode on wlan1
su -c "/data/local/tmp/wpa/wpa_supplicant -Dnl80211 -i wlan1 -O /data/local/tmp/wpa/ctrl -B"
su -c "/data/local/tmp/wpa/wpa_cli -p /data/local/tmp/wpa/ctrl -i wlan1 scan"
sleep 4
su -c "/data/local/tmp/wpa/wpa_cli -p /data/local/tmp/wpa/ctrl -i wlan1 scan_results"
```

Connect to a WPA2 network:
```bash
su -c "sh -c 'wpa_passphrase \"<SSID>\" \"<PSK>\" > /data/local/tmp/wpa/net.conf'"
# add ctrl_interface=/data/local/tmp/wpa/ctrl and update_config=1 to net.conf, then:
su -c "/data/local/tmp/wpa/wpa_supplicant -Dnl80211 -i wlan1 -c /data/local/tmp/wpa/net.conf -B"
```

---

## Want WPA3 (SAE) later?
That's the only piece this build drops. It requires OpenSSL (or wolfSSL) for the
EC math, static-linked — a separate, bigger effort on this Termux. WPA2-PSK is
what almost every network uses.

## Reproducible/CI version (implemented)
This doc is the on-device method. The reproducible equivalent is
[`ksu_module/build_wpa_supplicant.sh`](../ksu_module/build_wpa_supplicant.sh),
an NDK cross-compile run by the **"Static wpa_supplicant · wlan1"** workflow
(`.github/workflows/wpa-supplicant.yml`, `workflow_dispatch`). It uses the same
libnl+wpa recipe against the NDK sysroot, auto-detecting whether the `in_addr_t`
`-D` fallback is needed, and uploads `wpa_supplicant`/`wpa_cli` as an artifact.
Drop them into `ksu_module/mt76x2u-wifi/bin/` (with the bundled `wifi-connect.sh
<SSID> [PSK]` helper) and `build_module_zip.sh` will include them in the module.
