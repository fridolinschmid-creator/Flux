#!/usr/bin/env bash
# build-whisper.sh -- whisper.cpp fuer aarch64 cross-kompilieren und
# zusammen mit dem ggml-tiny.bin Modell ins Rootfs-Overlay legen.
#
# Voraussetzungen:
#   - Buildroot-Toolchain bereits gebaut (d.h. build.sh Schritt 1 lief)
#   - cmake >= 3.14 auf dem Host
#   - wget oder curl fuer Modell-Download
#
# Aufruf (nach build.sh Schritt 1):
#   FLUX_BUILDROOT_OUT=/tmp/flux-build/out ./build/build-whisper.sh
#
# Ergebnis:
#   build/overlay/usr/local/bin/whisper-cli   (aarch64 Binary)
#   build/overlay/usr/share/whisper/ggml-tiny.bin  (Sprachmodell, ~75 MB)
#
# Danach build.sh erneut ausfuehren -- es packt die Overlay-Dateien ins
# rootfs.ext4 ein. flux-shell findet whisper-cli und ggml-tiny.bin dann
# automatisch (voice.c sucht in /usr/local/bin/ und /usr/share/whisper/).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${FLUX_BUILDROOT_OUT:-/tmp/flux-build/out}"
OVERLAY_DIR="$ROOT_DIR/build/overlay"
BUILD_TMP="${TMPDIR:-/tmp}/flux-whisper-build"

WHISPER_TAG="${FLUX_WHISPER_TAG:-v1.7.4}"
WHISPER_REPO="https://github.com/ggerganov/whisper.cpp.git"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin"
MODEL_FILE="$OVERLAY_DIR/usr/share/whisper/ggml-tiny.bin"

# ---------- Cross-Compiler aus Buildroot-Output ermitteln --------------------
if [ ! -d "$OUT_DIR/host/bin" ]; then
    echo "ERROR: Buildroot-Output nicht gefunden unter $OUT_DIR"
    echo "       Erst build.sh Schritt 1 ausfuehren."
    exit 1
fi

CROSS_GCC="$(ls "$OUT_DIR"/host/bin/*-linux-gnu-gcc 2>/dev/null | head -1)"
if [ -z "$CROSS_GCC" ]; then
    echo "ERROR: Kein Cross-GCC in $OUT_DIR/host/bin gefunden."
    exit 1
fi

CROSS_PREFIX="${CROSS_GCC%gcc}"
CROSS_CXX="${CROSS_PREFIX}g++"
SYSROOT="$("$CROSS_GCC" -print-sysroot)"

echo "==> Cross-GCC:  $CROSS_GCC"
echo "==> Sysroot:    $SYSROOT"

# ---------- whisper.cpp klonen / aktualisieren -------------------------------
mkdir -p "$BUILD_TMP"

if [ ! -d "$BUILD_TMP/whisper.cpp/.git" ]; then
    echo "==> whisper.cpp klonen (Tag $WHISPER_TAG) ..."
    git clone --depth 1 --branch "$WHISPER_TAG" "$WHISPER_REPO" \
        "$BUILD_TMP/whisper.cpp"
else
    echo "==> whisper.cpp Quellcode bereits vorhanden, ueberspringe Clone."
fi

# ---------- Cross-kompilieren ------------------------------------------------
echo "==> Cross-kompilieren fuer aarch64 ..."
cmake -S "$BUILD_TMP/whisper.cpp" \
      -B "$BUILD_TMP/whisper-build" \
      -DCMAKE_BUILD_TYPE=Release \
      -DWHISPER_BUILD_TESTS=OFF \
      -DWHISPER_BUILD_EXAMPLES=ON \
      -DCMAKE_C_COMPILER="$CROSS_GCC" \
      -DCMAKE_CXX_COMPILER="$CROSS_CXX" \
      -DCMAKE_SYSROOT="$SYSROOT" \
      -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"

cmake --build "$BUILD_TMP/whisper-build" \
      --target whisper-cli \
      --parallel "$(nproc)"

# ---------- Binary ins Overlay kopieren --------------------------------------
mkdir -p "$OVERLAY_DIR/usr/local/bin"
cp "$BUILD_TMP/whisper-build/bin/whisper-cli" \
   "$OVERLAY_DIR/usr/local/bin/whisper-cli"
chmod +x "$OVERLAY_DIR/usr/local/bin/whisper-cli"
echo "==> Binary: $OVERLAY_DIR/usr/local/bin/whisper-cli"

# ---------- Modell downloaden (nur wenn noch nicht vorhanden) ----------------
mkdir -p "$(dirname "$MODEL_FILE")"
if [ -f "$MODEL_FILE" ]; then
    echo "==> Modell bereits vorhanden: $MODEL_FILE ($(du -sh "$MODEL_FILE" | cut -f1))"
else
    echo "==> Modell herunterladen (~75 MB): $MODEL_URL"
    if command -v wget &>/dev/null; then
        wget -c -O "$MODEL_FILE" "$MODEL_URL"
    elif command -v curl &>/dev/null; then
        curl -L -C - -o "$MODEL_FILE" "$MODEL_URL"
    else
        echo "ERROR: weder wget noch curl gefunden -- Modell manuell laden:"
        echo "       wget -O $MODEL_FILE \\"
        echo "         $MODEL_URL"
        exit 1
    fi
    echo "==> Modell: $MODEL_FILE ($(du -sh "$MODEL_FILE" | cut -f1))"
fi

echo
echo "Fertig. Jetzt build.sh erneut ausfuehren, um das Image zu aktualisieren:"
echo "  ./build/build.sh"
echo
echo "Im laufenden Gast landet whisper-cli unter /usr/local/bin/whisper-cli"
echo "und das Modell unter /usr/share/whisper/ggml-tiny.bin."
echo "flux-shell erkennt beides automatisch -- Mikrofon-Taste wird aktiv."
