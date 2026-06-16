#!/usr/bin/env bash
# build.sh -- baut das komplette Flux-Image fuer QEMU aarch64.
#
# Schritt 1: Buildroot baut Toolchain + Linux-Kernel + Basis-Rootfs
#            (inkl. libcurl/ca-certificates fuer fluxaid).
# Schritt 2: flux-shell/fluxaid werden MIT GENAU DEM Cross-Compiler
#            gebaut, den Buildroot in Schritt 1 erzeugt hat (sonst
#            gibt es eine glibc-Versions-Inkompatibilitaet zwischen
#            unseren Binaries und dem Rest des Rootfs).
# Schritt 3: Binaries landen im Rootfs-Overlay, Buildroot baut das
#            finale Image (schnell, da Schritt 1 schon alles
#            Schwere gebaut/gecacht hat).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BR_DIR="${FLUX_BUILDROOT_DIR:-/tmp/flux-build/buildroot}"
OUT_DIR="${FLUX_BUILDROOT_OUT:-/tmp/flux-build/out}"

if [ ! -d "$BR_DIR" ]; then
    echo "Buildroot nicht gefunden unter $BR_DIR -- klonen:"
    echo "  git clone --depth 1 --branch 2024.02.x https://github.com/buildroot/buildroot.git $BR_DIR"
    exit 1
fi

echo "==> [1/3] Buildroot: Toolchain + Kernel + Basis-Rootfs"
export FORCE_UNSAFE_CONFIGURE=1   # diese Pipeline laeuft typischerweise als root in einer Sandbox
make -C "$BR_DIR" O="$OUT_DIR" flux_aarch64_virt_defconfig
make -C "$BR_DIR" O="$OUT_DIR" -j"$(nproc)"

CROSS_PREFIX="$(ls "$OUT_DIR"/host/bin/*-linux-gnu-gcc | head -1 | sed 's/gcc$//')"
SYSROOT="$("${CROSS_PREFIX}gcc" -print-sysroot)"
echo "==> Cross-Compiler: ${CROSS_PREFIX}gcc"
echo "==> Sysroot:        $SYSROOT"

echo "==> [2/3] flux-shell + fluxaid mit Buildroot-Toolchain bauen"
make -C "$ROOT_DIR/shell"  clean
make -C "$ROOT_DIR/fluxai" clean
make -C "$ROOT_DIR/shell"  CROSS_COMPILE="$CROSS_PREFIX"
make -C "$ROOT_DIR/fluxai" CROSS_COMPILE="$CROSS_PREFIX"

mkdir -p "$ROOT_DIR/build/overlay/usr/bin"
cp "$ROOT_DIR/shell/flux-shell"   "$ROOT_DIR/build/overlay/usr/bin/"
cp "$ROOT_DIR/fluxai/fluxaid"     "$ROOT_DIR/build/overlay/usr/bin/"

echo "==> [3/3] Finales Image mit Flux-Binaries neu packen"
make -C "$BR_DIR" O="$OUT_DIR" -j"$(nproc)"

echo
echo "Fertig. Image:  $OUT_DIR/images/Image"
echo "        Rootfs: $OUT_DIR/images/rootfs.ext4"
echo "Starten mit:    ./build/run-qemu.sh"
