#!/usr/bin/env bash
# run-qemu.sh -- startet das Flux-Image in QEMU (aarch64, virt-Maschine).
#
# Standardmaessig "-display none" (laeuft auch headless/in einer
# Sandbox); Bildschirminhalt laesst sich trotzdem per QMP-Screendump
# pruefen (siehe build/screendump.sh). Mit echtem Desktop:
#   FLUX_DISPLAY=gtk ./build/run-qemu.sh
#
# Audio (Mikrofon fuer whisper.cpp):
#   QEMU stellt ein virtio-sound-pci Geraet bereit (benoetigt QEMU >= 6.0).
#   Standard-Backend: "none" -- Geraeteknoten erscheint im Gast,
#   Aufnahmen liefern Stille (kein Host-Mikrofon angebunden). Damit
#   gibt flux_voice_can_record() erstmals 1 zurueck statt 0.
#   Echtes Mikrofon: FLUX_AUDIO=alsa oder FLUX_AUDIO=pa setzen.
#   Audio ganz deaktivieren: FLUX_AUDIO=off
set -euo pipefail

OUT_DIR="${FLUX_BUILDROOT_OUT:-/tmp/flux-build/out}"
DISPLAY_BACKEND="${FLUX_DISPLAY:-none}"
QMP_SOCK="/tmp/flux-qemu-qmp.sock"
AUDIO_BACKEND="${FLUX_AUDIO:-none}"

# Baue Audio-Argumente auf (leer wenn FLUX_AUDIO=off).
AUDIO_ARGS=()
if [ "$AUDIO_BACKEND" != "off" ]; then
    AUDIO_ARGS=(
        -audiodev "${AUDIO_BACKEND},id=audio0"
        -device   "virtio-sound-pci,audiodev=audio0"
    )
fi

exec qemu-system-aarch64 \
    -M virt -cpu cortex-a53 -smp 2 -m 1024 \
    -kernel "$OUT_DIR/images/Image" \
    -append "rootwait root=/dev/vda console=ttyAMA0" \
    -drive file="$OUT_DIR/images/rootfs.ext4",if=none,format=raw,id=hd0 \
    -device virtio-blk-pci,drive=hd0 \
    -netdev user,id=eth0 -device virtio-net-pci,netdev=eth0 \
    -device virtio-gpu-pci,xres=1080,yres=2400 \
    -device virtio-keyboard-pci \
    -device virtio-tablet-pci \
    "${AUDIO_ARGS[@]}" \
    -display "$DISPLAY_BACKEND" \
    -serial mon:stdio \
    -qmp "unix:${QMP_SOCK},server,nowait"
