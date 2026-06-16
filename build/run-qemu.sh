#!/usr/bin/env bash
# run-qemu.sh -- startet das Flux-Image in QEMU (aarch64, virt-Maschine).
#
# Standardmaessig "-display none" (laeuft auch headless/in einer
# Sandbox); Bildschirminhalt laesst sich trotzdem per QMP-Screendump
# pruefen (siehe build/screendump.sh). Mit echtem Desktop:
#   FLUX_DISPLAY=gtk ./build/run-qemu.sh
set -euo pipefail

OUT_DIR="${FLUX_BUILDROOT_OUT:-/tmp/flux-build/out}"
DISPLAY_BACKEND="${FLUX_DISPLAY:-none}"
QMP_SOCK="/tmp/flux-qemu-qmp.sock"

exec qemu-system-aarch64 \
    -M virt -cpu cortex-a53 -smp 2 -m 1024 \
    -kernel "$OUT_DIR/images/Image" \
    -append "rootwait root=/dev/vda console=ttyAMA0" \
    -drive file="$OUT_DIR/images/rootfs.ext4",if=none,format=raw,id=hd0 \
    -device virtio-blk-pci,drive=hd0 \
    -netdev user,id=eth0 -device virtio-net-pci,netdev=eth0 \
    -device virtio-gpu-pci \
    -device virtio-keyboard-pci \
    -device virtio-tablet-pci \
    -display "$DISPLAY_BACKEND" \
    -serial mon:stdio \
    -qmp "unix:${QMP_SOCK},server,nowait"
