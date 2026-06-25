#!/usr/bin/env bash
# flash-pi5.sh -- schreibt das gebaute Flux-Image auf eine microSD-Karte
# (oder USB-/NVMe-Datentraeger) fuer den Raspberry Pi 5.
#
#   ./build/flash-pi5.sh /dev/sdX      (Linux)
#   ./build/flash-pi5.sh /dev/diskN    (macOS -- vorher: diskutil unmountDisk)
#
# ACHTUNG: ueberschreibt den gesamten Datentraeger. Falsches Geraet =
# Datenverlust. Geraet vorher mit `lsblk` (Linux) / `diskutil list`
# (macOS) sicher identifizieren.
set -euo pipefail

OUT_DIR="${FLUX_BUILDROOT_OUT:-/tmp/flux-build/out-pi5}"
IMG="$OUT_DIR/images/sdcard.img"
DEV="${1:?Aufruf: flash-pi5.sh /dev/sdX  (Zielgeraet, NICHT eine Partition)}"

[ -f "$IMG" ] || { echo "Image nicht gefunden: $IMG -- zuerst ./build/build-pi5.sh"; exit 1; }
[ -b "$DEV" ] || { echo "$DEV ist kein Block-Geraet."; exit 1; }

echo "Image:   $IMG  ($(du -h "$IMG" | cut -f1))"
echo "Ziel:    $DEV"
echo
read -r -p "ALLE Daten auf $DEV werden geloescht. Fortfahren? (yes/NEIN) " ans
[ "$ans" = "yes" ] || { echo "Abgebrochen."; exit 1; }

# bs=4M ist ein guter Kompromiss; status=progress zeigt den Fortschritt.
sudo dd if="$IMG" of="$DEV" bs=4M conv=fsync status=progress
sync
echo
echo "Fertig. Karte in den Pi 5 stecken und einschalten."
echo "Erststart dauert etwas laenger (Dateisystem wird angelegt)."
