#!/bin/sh
# Use from Linux/WSL with an AArch64 SDK. Does not install or modify host packages.
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"
: "${PKG_CONFIG:=pkg-config}"
case "$(gcc -dumpmachine 2>/dev/null || true)" in
    aarch64*) : "${CROSS_COMPILE:=}" ;;
    *)
        : "${CROSS_COMPILE:=aarch64-linux-gnu-}"
        : "${PKG_CONFIG_LIBDIR:=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig}"
        : "${PKG_CONFIG_SYSROOT_DIR:=/}"
        export PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
        ;;
esac
export PKG_CONFIG
if [ "${1:-}" = "--doctor" ]; then
    shift
    exec make TARGET_RGB30=1 CROSS_COMPILE="$CROSS_COMPILE" PORTMASTER_DIAG=1 _portmaster-doctor-inner "$@"
fi
make -j "${JOBS:-2}" TARGET_RGB30=1 CROSS_COMPILE="$CROSS_COMPILE" "$@"
binary=_obj_/rgb30/cuzebox
"${CROSS_COMPILE}readelf" -h "$binary"
"${CROSS_COMPILE}readelf" -d -V "$binary" > _obj_/rgb30/runtime-dependencies.txt
printf '\nBinary: %s\nDependency/ABI report: _obj_/rgb30/runtime-dependencies.txt\n' "$binary"
