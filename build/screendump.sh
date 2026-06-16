#!/usr/bin/env bash
# screendump.sh -- macht einen Screenshot des laufenden Flux-QEMU
# (auch ohne echten Display-Server, ueber die QMP-Steuerverbindung).
set -euo pipefail

QMP_SOCK="/tmp/flux-qemu-qmp.sock"
OUT_FILE="${1:-/tmp/flux-screenshot.ppm}"

python3 - "$QMP_SOCK" "$OUT_FILE" <<'PYEOF'
import socket, json, sys

sock_path, out_file = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_path)

def recv_json():
    buf = b""
    while not buf.endswith(b"\r\n") and b"\n" not in buf[-2:]:
        chunk = s.recv(4096)
        if not chunk:
            break
        buf += chunk
        if buf.count(b"\n") >= 1:
            break
    return json.loads(buf.splitlines()[0])

recv_json()  # greeting
s.send(b'{"execute":"qmp_capabilities"}\n')
recv_json()
s.send(json.dumps({"execute": "screendump", "arguments": {"filename": out_file}}).encode() + b"\n")
print(recv_json())
PYEOF

echo "Screenshot: $OUT_FILE"
