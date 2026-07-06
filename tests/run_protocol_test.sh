#!/usr/bin/env bash
# run_protocol_test.sh -- End-to-End-Test des fluxaid-Protokolls.
#
# Startet den Daemon mit einem Socket unter /tmp (kein Root noetig dank
# FLUX_SOCK_PATH-Override), schickt einige Q:/Protokoll-Requests ueber den
# Testclient und prueft die Antworten. Erwartet, dass fluxaid und der
# Client bereits gebaut sind (siehe top-level Makefile, Ziel "test").
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAEMON="$ROOT_DIR/fluxai/fluxaid"
CLIENT="$ROOT_DIR/tests/protocol_client"

export FLUX_SOCK_PATH="${FLUX_SOCK_PATH:-/tmp/flux-test-$$/fluxai.sock}"
# Hintergrunddienste/Tools nie versehentlich gegen die echte Cloud laufen
# lassen -- dieser Test prueft nur lokale Intents und das Protokoll.
unset FLUX_AI_API_KEY || true

[ -x "$DAEMON" ] || { echo "FEHLER: $DAEMON nicht gebaut"; exit 1; }
[ -x "$CLIENT" ] || { echo "FEHLER: $CLIENT nicht gebaut"; exit 1; }

"$DAEMON" >/tmp/fluxaid-test.log 2>&1 &
DAEMON_PID=$!
cleanup() {
    kill "$DAEMON_PID" 2>/dev/null || true
    wait "$DAEMON_PID" 2>/dev/null || true
    rm -rf "$(dirname "$FLUX_SOCK_PATH")"
}
trap cleanup EXIT

# Auf den Socket warten (max ~5 s).
for _ in $(seq 1 50); do
    [ -S "$FLUX_SOCK_PATH" ] && break
    sleep 0.1
done
[ -S "$FLUX_SOCK_PATH" ] || { echo "FEHLER: Socket nicht erschienen"; cat /tmp/fluxaid-test.log; exit 1; }

fail=0
# $1 = Beschreibung, $2 = Request, $3 = erwartetes Teilstring-Muster
expect() {
    local desc="$1" req="$2" pat="$3" got
    got="$("$CLIENT" "$req" || true)"
    if printf '%s' "$got" | grep -qF "$pat"; then
        echo "  ok: $desc"
    else
        echo "  FAIL: $desc -- erwartet '$pat', bekam: $got"
        fail=1
    fi
}

echo "protocol test (socket: $FLUX_SOCK_PATH)"
expect "Uhrzeit-Intent"        "Q:wie spaet ist es"        "Uhr"
expect "Datums-Intent"         "Q:welches datum haben wir" "Heute ist der"
expect "Akku-Intent (headless)" "Q:wie ist der akku"       "Akku"
expect "Cloud-Fallback ohne Key" "Q:wer bist du"           "Kein Cloud-Zugang"
expect "Protokollende-Marker"  "Q:wie spaet ist es"        "END"
expect "Unbekanntes Protokoll" "Z:bla"                     "ERR:"

if [ "$fail" -ne 0 ]; then
    echo "protocol test: FAILED"
    exit 1
fi
echo "protocol test: PASSED"
