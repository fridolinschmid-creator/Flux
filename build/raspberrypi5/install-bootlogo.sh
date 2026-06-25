#!/usr/bin/env bash
# install-bootlogo.sh -- legt das eigene Flux-Boot-Logo in den
# Kernel-Quellbaum, bevor Buildroot den Kernel baut.
#
# Der Linux-Kernel zeigt beim Booten das Bild aus
#   drivers/video/logo/logo_linux_clut224.ppm
# (224-Farben-PPM). Wir ueberschreiben genau diese Datei mit unserem
# Logo. Aktiviert wird das Logo ueber linux-pi5.fragment
# (CONFIG_LOGO + CONFIG_LOGO_LINUX_CLUT224).
#
# Voraussetzung: eine Datei flux-bootlogo.ppm in diesem Verzeichnis.
# So erzeugt man sie aus einem PNG (z.B. dem ChatGPT-Logo):
#   convert flux-logo.png -resize 320x320 -dither FloydSteinberg \
#           -colors 224 flux-bootlogo.ppm
# (ImageMagick; 224 Farben ist das Maximum fuer logo_linux_clut224.)
set -euo pipefail

OUT_DIR="${1:?Aufruf: install-bootlogo.sh <buildroot-out-dir>}"
PI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOGO_SRC="$PI_DIR/flux-bootlogo.ppm"

if [ ! -f "$LOGO_SRC" ]; then
    echo "Kein $LOGO_SRC vorhanden -- ueberspringe (Standard-Logo bleibt)."
    exit 1
fi

# Den gerade entpackten Kernel-Quellbaum finden (linux-custom oder
# linux-<version>).
KDIR="$(ls -d "$OUT_DIR"/build/linux-* 2>/dev/null | grep -v 'headers\|tools' | head -1 || true)"
if [ -z "$KDIR" ] || [ ! -d "$KDIR/drivers/video/logo" ]; then
    echo "Kernel-Quellbaum noch nicht entpackt -- install-bootlogo.sh"
    echo "nach dem ersten 'make linux-extract' erneut ausfuehren, oder"
    echo "build-pi5.sh ein zweites Mal starten."
    exit 1
fi

cp "$LOGO_SRC" "$KDIR/drivers/video/logo/logo_linux_clut224.ppm"
echo "Boot-Logo installiert -> $KDIR/drivers/video/logo/logo_linux_clut224.ppm"
echo "Kernel beim naechsten Build neu uebersetzen lassen:"
echo "  make -C \$BR_DIR O=$OUT_DIR linux-rebuild all"
