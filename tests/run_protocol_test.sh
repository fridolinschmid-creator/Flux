#!/usr/bin/env bash
# run_protocol_test.sh -- End-to-End-Test des fluxaid-Protokolls.
#
# Startet den Daemon mit einem Socket unter /tmp (kein Root noetig dank
# FLUX_SOCK_PATH-Override), schickt einige Q:/Protokoll-Requests ueber den
# Testclient und prueft die Antworten. Erwartet, dass fluxaid und der
# Client bereits gebaut sind (siehe top-level Makefile, Ziel "test").
#
# Zwei Phasen:
#   1) ohne API-Key  -- lokale Intents + ehrlicher Offline-Fallback
#   2) FLUX_PROVIDER=mock -- deterministischer Provider, prueft den echten
#      Tool-Dispatch (calculate) und die Sicherheits-Guards (file_read auf
#      die Konfig, Pfad-Traversal) end-to-end, ohne Netzwerk.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAEMON="$ROOT_DIR/fluxai/fluxaid"
CLIENT="$ROOT_DIR/tests/protocol_client"

export FLUX_SOCK_PATH="${FLUX_SOCK_PATH:-/tmp/flux-test-$$/fluxai.sock}"
# Hintergrunddienste/Tools nie versehentlich gegen die echte Cloud laufen
# lassen -- dieser Test prueft nur lokale Intents, den Mock und das Protokoll.
unset FLUX_AI_API_KEY || true

[ -x "$DAEMON" ] || { echo "FEHLER: $DAEMON nicht gebaut"; exit 1; }
[ -x "$CLIENT" ] || { echo "FEHLER: $CLIENT nicht gebaut"; exit 1; }

DAEMON_PID=""
stop_daemon() {
    [ -n "$DAEMON_PID" ] || return 0
    kill "$DAEMON_PID" 2>/dev/null || true
    wait "$DAEMON_PID" 2>/dev/null || true
    DAEMON_PID=""
    # Stale Socket entfernen, damit die Readiness-Pruefung der naechsten
    # Phase nicht faelschlich gegen den alten (ungebundenen) Socket anschlaegt.
    rm -f "$FLUX_SOCK_PATH"
}
cleanup() {
    stop_daemon
    rm -rf "$(dirname "$FLUX_SOCK_PATH")"
}
trap cleanup EXIT

# Startet den Daemon und wartet, bis der Socket da ist (max ~5 s).
start_daemon() {
    "$DAEMON" >/tmp/fluxaid-test.log 2>&1 &
    DAEMON_PID=$!
    for _ in $(seq 1 50); do
        [ -S "$FLUX_SOCK_PATH" ] && return 0
        sleep 0.1
    done
    echo "FEHLER: Socket nicht erschienen"; cat /tmp/fluxaid-test.log; exit 1
}

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

# --- Phase 1: ohne Cloud-Key -------------------------------------------------
echo "phase 1: lokale Intents + Offline-Fallback"
start_daemon
expect "Uhrzeit-Intent"         "Q:wie spaet ist es"        "Uhr"
expect "Datums-Intent"          "Q:welches datum haben wir" "Heute ist der"
expect "Akku-Intent (headless)" "Q:wie ist der akku"        "Akku"
expect "Cloud-Fallback ohne Key" "Q:wer bist du"            "Kein Cloud-Zugang"
expect "Protokollende-Marker"   "Q:wie spaet ist es"        "END"
expect "Unbekanntes Protokoll"  "Z:bla"                     "ERR:"
stop_daemon

# --- Phase 2: deterministischer Mock-Provider --------------------------------
echo "phase 2: Mock-Provider (Tool-Dispatch + Sicherheits-Guards)"
export FLUX_PROVIDER=mock
start_daemon
expect "Mock-Antwort deterministisch" "Q:wer bist du"               "[mock]"
expect "Tool-Dispatch: calculate"     "Q:tool:calculate|7*6"        "42"
expect "Guard: file_read blockt Konfig" \
       "Q:tool:file_read|/etc/flux/flux.conf" \
       "Konfigurationsdatei nicht erlaubt"
expect "Guard: file_create blockt Traversal" \
       "Q:tool:file_create|/home/user/../etc/x|y" \
       "ohne '..'"
expect "Guard: file_delete blockt Systempfad" \
       "Q:tool:file_delete|/etc/passwd" \
       "nur unter /home/user/ erlaubt"
stop_daemon
unset FLUX_PROVIDER

if [ "$fail" -ne 0 ]; then
    echo "protocol test: FAILED"
    exit 1
fi
echo "protocol test: PASSED"
