#!/usr/bin/env bash
# Cross-compile STATIC wpa_supplicant + wpa_cli for aarch64 Android using the NDK.
#
# Mirrors the on-device Termux recipe (docs/wpa_supplicant_static_termux.md) but
# with the NDK's clean sysroot, so it's reproducible in CI. libnl is baked in;
# the binaries link only bionic system libs (libc/libdl).
#
# Usage: ksu_module/build_wpa_supplicant.sh [output_dir]
#   default output_dir: dist/wpa
set -euo pipefail

NDK_VERSION="${NDK_VERSION:-r26d}"
API="${API:-24}"
LIBNL_VER="3.7.0"
WPA_VER="2.11"

# How to link the final binaries:
#   dynamic       (default, proven) libnl is baked in; libc.so/libdl.so stay
#                 dynamic. Those are bionic itself and always present at
#                 /system/lib64 on a booted device, so this is fully portable.
#   static-bionic (EXPERIMENTAL) additionally links bionic statically, leaving a
#                 binary with no NEEDED entries at all. Only useful where
#                 /system is not mounted (chroot/container, recovery). Bionic
#                 discourages static executables - dlopen/NSS/DNS break - but
#                 wpa_supplicant with nl80211 + internal crypto + file config
#                 uses none of those.
LINK_MODE="${LINK_MODE:-dynamic}"
case "$LINK_MODE" in
    dynamic|static-bionic) ;;
    *) echo "[ERROR] LINK_MODE must be 'dynamic' or 'static-bionic' (got '$LINK_MODE')" >&2
       exit 1 ;;
esac
echo "[*] link mode: ${LINK_MODE}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WORK="${WORK:-${REPO_ROOT}/out/wpa-build}"
OUTDIR="${1:-${REPO_ROOT}/dist/wpa}"
NDK_HOME="${WORK}/android-ndk-${NDK_VERSION}"
NL_PREFIX="${WORK}/nl-static"

mkdir -p "$WORK"
cd "$WORK"

# ---------------------------------------------------------------- NDK toolchain
if [ ! -d "$NDK_HOME" ]; then
    echo "[*] Downloading Android NDK ${NDK_VERSION}..."
    curl -L --retry 3 -o ndk.zip \
        "https://dl.google.com/android/repository/android-ndk-${NDK_VERSION}-linux.zip"
    unzip -q ndk.zip && rm -f ndk.zip
fi
TC="${NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/bin"
export CC="${TC}/aarch64-linux-android${API}-clang"
export AR="${TC}/llvm-ar"
export RANLIB="${TC}/llvm-ranlib"
[ -x "$CC" ] || { echo "[ERROR] NDK clang not found at $CC" >&2; exit 1; }

# bionic does not make in_addr_t/in_port_t visible the way libnl's sources
# expect: libnl reaches <arpa/inet.h> through its own -I./include/linux-private
# path and the typedefs never materialise, so every use in arpa/inet.h and
# netlink-private/{types,utils}.h fails with "unknown type name".
#
# This is NOT a Termux quirk - Termux uses the NDK's bionic headers, so the NDK
# sysroot reproduces it identically (verified on ndk-r26d in CI). Define the two
# types on the command line; safe because nothing on bionic emits a conflicting
# `typedef ... in_addr_t` in these translation units, which is exactly why the
# on-device build succeeded with the same flags.
FIX="-Din_addr_t=uint32_t -Din_port_t=uint16_t"
echo "[*] bionic header workaround: $FIX"

# bionic implements pthread/rt inside libc and the NDK ships no libpthread, so
# autotools' AC_CHECK_LIB(pthread, pthread_mutex_lock) fails with
# "libpthread is required". Provide empty stub archives so -lpthread/-lrt
# resolve; the real symbols come from libc at link time.
COMPAT="${WORK}/compat"
if [ ! -f "${COMPAT}/libpthread.a" ]; then
    echo "[*] Creating libpthread/librt stubs for the NDK sysroot..."
    mkdir -p "$COMPAT"
    echo 'static int _ksu_compat_stub __attribute__((unused));' > "${WORK}/_stub.c"
    "$CC" -c "${WORK}/_stub.c" -o "${WORK}/_stub.o"
    for stub in pthread rt; do
        "$AR" rcs "${COMPAT}/lib${stub}.a" "${WORK}/_stub.o"
    done
fi
export LDFLAGS="-L${COMPAT} ${LDFLAGS:-}"

# ------------------------------------------------------------- static libnl 3.7
if [ ! -f "${NL_PREFIX}/lib/libnl-3.a" ]; then
    echo "[*] Building static libnl ${LIBNL_VER}..."
    [ -d "libnl-${LIBNL_VER}" ] || {
        curl -L --retry 3 -o libnl.tar.gz \
            "https://github.com/thom311/libnl/releases/download/libnl3_7_0/libnl-${LIBNL_VER}.tar.gz"
        tar xf libnl.tar.gz
    }
    ( cd "libnl-${LIBNL_VER}"
      ./configure --host=aarch64-linux-android --prefix="${NL_PREFIX}" \
          --enable-static --disable-shared --disable-cli \
          CC="$CC" AR="$AR" RANLIB="$RANLIB" CPPFLAGS="$FIX" LDFLAGS="$LDFLAGS"
      make -j"$(nproc)"
      make install )
fi

# ------------------------------------------------------- static wpa_supplicant
echo "[*] Building static wpa_supplicant + wpa_cli ${WPA_VER}..."
[ -d "wpa_supplicant-${WPA_VER}" ] || {
    curl -L --retry 3 -o wpa.tar.gz "https://w1.fi/releases/wpa_supplicant-${WPA_VER}.tar.gz"
    tar xf wpa.tar.gz
}
cd "wpa_supplicant-${WPA_VER}/wpa_supplicant"

# With -static everything is static already, so the -Bstatic/-Bdynamic dance is
# unnecessary (and -Bdynamic at the end would fight it).
if [ "$LINK_MODE" = "static-bionic" ]; then
    WPA_LIBS="-L${NL_PREFIX}/lib -lnl-genl-3 -lnl-3"
    WPA_LDFLAGS="-static"
else
    WPA_LIBS="-L${NL_PREFIX}/lib -Wl,-Bstatic -lnl-genl-3 -lnl-3 -Wl,-Bdynamic"
    WPA_LDFLAGS=""
fi

# WPA2/WPA3-PSK, internal crypto (no OpenSSL), no SAE (needs OpenSSL EC), static libnl.
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
CFLAGS  += -I${NL_PREFIX}/include/libnl3 ${FIX}
LIBS    += ${WPA_LIBS}
LDFLAGS += ${WPA_LDFLAGS}
EOF

make clean >/dev/null 2>&1 || true
make -j"$(nproc)" CC="$CC" AR="$AR" wpa_supplicant wpa_cli

# ----------------------------------------------------------------- collect/verify
mkdir -p "$OUTDIR"
cp wpa_supplicant wpa_cli "$OUTDIR/"
"${TC}/llvm-strip" "$OUTDIR/wpa_supplicant" "$OUTDIR/wpa_cli" 2>/dev/null || true
echo "== link verification (${LINK_MODE}) =="
NEEDED="$("${TC}/llvm-readelf" -d "$OUTDIR/wpa_supplicant" 2>/dev/null | grep NEEDED || true)"
if [ -n "$NEEDED" ]; then
    printf '%s\n' "$NEEDED"
else
    echo "  (no dynamic section — fully static)"
fi

# libnl must never be dynamic in either mode: it is the whole point of the build.
if printf '%s' "$NEEDED" | grep -q 'libnl'; then
    echo "[ERROR] wpa_supplicant still links libnl dynamically" >&2
    exit 1
fi
if [ "$LINK_MODE" = "static-bionic" ] && [ -n "$NEEDED" ]; then
    echo "[ERROR] static-bionic requested but the binary still has NEEDED entries" >&2
    exit 1
fi

file "$OUTDIR/wpa_supplicant" 2>/dev/null || true
echo "[OK] binaries (${LINK_MODE}) -> $OUTDIR"
ls -la "$OUTDIR"
