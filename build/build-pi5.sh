#!/usr/bin/env bash
# build-pi5.sh -- baut das komplette Flux-Image fuer einen ECHTEN
# Raspberry Pi 5 (BCM2712, arm64). Ergebnis ist ein fertiges
# sdcard.img, das man 1:1 auf eine microSD-Karte (oder NVMe/USB)
# schreibt -- siehe build/flash-pi5.sh.
#
# Unterschied zu build.sh (QEMU):
#   - Basis ist Buildroots offizielle `raspberrypi5_defconfig` statt
#     der QEMU-virt-Defconfig.
#   - Wir haengen nur ein kleines Flux-Fragment an (libcurl, WLAN-
#     Firmware, unser Overlay, Boot-Logo) statt eine ganze Defconfig
#     von Hand zu pflegen.
#   - Buildroot erzeugt am Ende ein bootfaehiges sdcard.img (FAT-Boot +
#     ext4-Rootfs, Firmware + DTBs inklusive).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PI_DIR="$ROOT_DIR/build/raspberrypi5"
BR_DIR="${FLUX_BUILDROOT_DIR:-/tmp/flux-build/buildroot}"
OUT_DIR="${FLUX_BUILDROOT_OUT:-/tmp/flux-build/out-pi5}"
DEFCONFIG="$BR_DIR/configs/flux_raspberrypi5_defconfig"

if [ ! -d "$BR_DIR" ]; then
    echo "Buildroot nicht gefunden unter $BR_DIR -- klonen:"
    echo "  git clone --depth 1 --branch 2024.02.x https://github.com/buildroot/buildroot.git $BR_DIR"
    exit 1
fi

if [ ! -f "$BR_DIR/configs/raspberrypi5_defconfig" ]; then
    echo "FEHLER: $BR_DIR/configs/raspberrypi5_defconfig fehlt."
    echo "Diese Defconfig gibt es erst ab Buildroot 2024.02 -- bitte"
    echo "eine aktuelle Buildroot-Version auschecken (2024.02.x oder neuer)."
    exit 1
fi

echo "==> [1/4] Flux-Defconfig aus Upstream + Fragment zusammensetzen"
# Upstream-Defconfig als Basis, dann unser Fragment dahinter.
cp "$BR_DIR/configs/raspberrypi5_defconfig" "$DEFCONFIG" 2>/dev/null || \
    cp "$BR_DIR/configs/raspberrypi5_defconfig" "$DEFCONFIG"
{
    echo ""
    echo "# ===== Flux-Ergaenzungen (aus build/raspberrypi5/flux-pi5.fragment) ====="
    # Platzhalter im Fragment durch echte Pfade ersetzen.
    sed \
        -e "s#\$(FLUX_OVERLAY_PI5)#$ROOT_DIR/build/overlay-pi5#g" \
        -e "s#\$(FLUX_CONFIG_TXT)#$PI_DIR/config.txt#g" \
        -e "s#\$(FLUX_LINUX_FRAGMENT)#$PI_DIR/linux-pi5.fragment#g" \
        "$PI_DIR/flux-pi5.fragment"
} >> "$DEFCONFIG"

export FORCE_UNSAFE_CONFIGURE=1   # Pipeline laeuft oft als root in einer Sandbox
make -C "$BR_DIR" O="$OUT_DIR" flux_raspberrypi5_defconfig

echo "==> [2/4] Boot-Logo (eigene Marke) in den Kernel-Quellbaum legen"
"$PI_DIR/install-bootlogo.sh" "$OUT_DIR" || \
    echo "    (Kein eigenes Logo gefunden -- Kernel nutzt vorerst Standard-Logo.)"

echo "==> [3/4] Buildroot: Toolchain + Pi-5-Kernel + Basis-Rootfs"
make -C "$BR_DIR" O="$OUT_DIR" -j"$(nproc)"

CROSS_PREFIX="$(ls "$OUT_DIR"/host/bin/aarch64-*-linux-gnu-gcc | head -1 | sed 's/gcc$//')"
echo "==> Cross-Compiler: ${CROSS_PREFIX}gcc"

echo "==> [4/4] flux-shell + fluxaid mit Pi-Toolchain bauen und einpacken"
make -C "$ROOT_DIR/shell"  clean
make -C "$ROOT_DIR/fluxai" clean
make -C "$ROOT_DIR/shell"  CROSS_COMPILE="$CROSS_PREFIX"
make -C "$ROOT_DIR/fluxai" CROSS_COMPILE="$CROSS_PREFIX"

mkdir -p "$ROOT_DIR/build/overlay-pi5/usr/bin"
cp "$ROOT_DIR/shell/flux-shell"   "$ROOT_DIR/build/overlay-pi5/usr/bin/"
cp "$ROOT_DIR/fluxai/fluxaid"     "$ROOT_DIR/build/overlay-pi5/usr/bin/"

echo "==> [4b/4] fluxweb (WPE-Browser-Companion) bauen -- siehe fluxweb/README.md"
echo "    NICHT verifiziert: erster Build hier ueberhaupt, viele neue"
echo "    Transitiv-Abhaengigkeiten (JavaScriptCore etc.). Bei Fehlern:"
echo "    fluxweb/README.md, dann 'make WPE=1' im fluxweb/-Verzeichnis"
echo "    von Hand mit den Fehlermeldungen weiter debuggen."
PKG_CONFIG_BIN="$(ls "$OUT_DIR"/host/bin/pkg-config 2>/dev/null | head -1)"
if [ -n "$PKG_CONFIG_BIN" ]; then
    make -C "$ROOT_DIR/fluxweb" clean
    if make -C "$ROOT_DIR/fluxweb" WPE=1 CROSS_COMPILE="$CROSS_PREFIX" \
            PKG_CONFIG="$PKG_CONFIG_BIN"; then
        cp "$ROOT_DIR/fluxweb/fluxweb" "$ROOT_DIR/build/overlay-pi5/usr/bin/"
        echo "    fluxweb gebaut und ins Overlay kopiert."
    else
        echo "    fluxweb-Build fehlgeschlagen (erwartbar beim ersten Mal --"
        echo "    siehe fluxweb/README.md 'VERIFY'-Stellen). Image wird OHNE"
        echo "    fluxweb weitergebaut, der Rest des Systems bleibt lauffaehig."
    fi
else
    echo "    Kein host-pkg-config in $OUT_DIR gefunden -- fluxweb uebersprungen."
fi

# Finales Image mit Flux-Binaries neu packen.
make -C "$BR_DIR" O="$OUT_DIR" -j"$(nproc)"

echo
echo "Fertig. SD-Karten-Image:  $OUT_DIR/images/sdcard.img"
echo "Auf SD-Karte schreiben:   ./build/flash-pi5.sh /dev/sdX"
