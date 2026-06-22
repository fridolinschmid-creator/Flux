#!/usr/bin/env python3
"""flux-log-server.py -- Minimaler Referenz-Server fuer Flux-Logs/Fehler.

Empfaengt, was Flux (fluxaid) ueber `log_backend_url` schickt, speichert es
und zeigt es im Browser an. Bewusst nur Python-Standardbibliothek -- kein
Framework, kein Setup. NICHT fuer Produktion gedacht (kein Auth, kein TLS):
ein Entwickler-/Diagnose-Helfer fuers eigene Netz (z.B. auf dem MacBook).

Endpunkte (passend zu fluxai/src/logsync.c):
    POST /ingest   -- kompletter Logfile-Inhalt (Sammel-Upload, text/plain)
    POST /report   -- ein einzelnes Ereignis "level|modul|text"
    GET  /         -- HTML-Ansicht (neueste zuerst)
    GET  /raw      -- alles als Klartext

Start:
    python3 tools/flux-log-server.py 8899
    # dann in Flux:  Einstellungen -> Fehler-Backend (URL) -> http://<host>:8899
"""
import sys
import html
import json
import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# In-Memory-Speicher (einfach gehalten). Optional zusaetzlich auf Platte.
EVENTS = []          # Liste aus dict: {time, device, kind, text}
STORE_FILE = "flux-log-store.jsonl"


def _now():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _persist(entry):
    """Best-effort: zusaetzlich in eine JSONL-Datei schreiben."""
    try:
        with open(STORE_FILE, "a", encoding="utf-8") as f:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")
    except OSError as e:
        print("WARN: konnte Store nicht schreiben:", e, file=sys.stderr)


def _add(device, kind, text):
    entry = {"time": _now(), "device": device, "kind": kind, "text": text}
    EVENTS.append(entry)
    _persist(entry)
    print(f"[{entry['time']}] {kind} von {device} ({len(text)} Zeichen)")


PAGE_HEAD = """<!doctype html><html lang=de><head><meta charset=utf-8>
<meta name=viewport content="width=device-width, initial-scale=1">
<title>Flux Logs</title><style>
body{font-family:-apple-system,system-ui,sans-serif;margin:0;background:#0b0e14;color:#e6e9ef}
header{padding:14px 18px;background:#11161f;border-bottom:1px solid #222c3c;
 position:sticky;top:0;display:flex;justify-content:space-between;align-items:center}
h1{font-size:17px;margin:0}.sub{color:#7d8aa3;font-size:13px}
.ev{border-bottom:1px solid #1b2230;padding:10px 18px}
.meta{font-size:12px;color:#7d8aa3;margin-bottom:4px}
.dev{color:#10b981}.kind{color:#f59e0b}
pre{margin:0;white-space:pre-wrap;word-break:break-word;font-size:13px;line-height:1.4}
.err{color:#f87171}.warn{color:#fbbf24}
.empty{padding:40px 18px;color:#7d8aa3}
</style></head><body><header><div><h1>Flux Logs &amp; Fehler</h1>
<div class=sub>%d Ereignis(se) &middot; neueste zuerst &middot; auto-refresh 5s</div></div>
<div class=sub>POST /ingest &middot; /report</div></header>
<meta http-equiv=refresh content=5>
"""


def render():
    out = [PAGE_HEAD % len(EVENTS)]
    if not EVENTS:
        out.append('<div class=empty>Noch nichts empfangen. In Flux unter '
                   '&bdquo;Logs an Backend senden&ldquo; ausloesen.</div>')
    for e in reversed(EVENTS):
        text = html.escape(e["text"])
        cls = ""
        low = e["text"].lower()
        if "error" in low or "fatal" in low or "fehlgeschlagen" in low:
            cls = "err"
        elif "warn" in low:
            cls = "warn"
        out.append(
            f'<div class=ev><div class=meta><span class=dev>{html.escape(e["device"])}</span>'
            f' &middot; <span class=kind>{html.escape(e["kind"])}</span>'
            f' &middot; {e["time"]}</div><pre class="{cls}">{text}</pre></div>')
    out.append("</body></html>")
    return "".join(out)


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="text/html; charset=utf-8"):
        data = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/raw":
            txt = "\n\n".join(f"[{e['time']}] {e['device']} {e['kind']}\n{e['text']}"
                              for e in EVENTS)
            self._send(200, txt or "(leer)", "text/plain; charset=utf-8")
        elif self.path in ("/", "/index.html"):
            self._send(200, render())
        else:
            self._send(404, "not found", "text/plain")

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        body = self.rfile.read(length).decode("utf-8", "replace") if length else ""
        device = self.headers.get("X-Flux-Device", "unbekannt")
        if self.path == "/ingest":
            _add(device, "log-upload", body)
            self._send(200, "ok", "text/plain")
        elif self.path == "/report":
            # Format "level|modul|text"
            parts = body.split("|", 2)
            if len(parts) == 3:
                lvl, mod, msg = parts
                _add(device, f"report/{lvl}", f"[{mod}] {msg}")
            else:
                _add(device, "report", body)
            self._send(200, "ok", "text/plain")
        else:
            self._send(404, "not found", "text/plain")

    def log_message(self, *args):
        pass  # eigenes, knappes Logging in _add()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8899
    srv = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"Flux-Log-Server laeuft auf http://0.0.0.0:{port}  (Strg+C zum Beenden)")
    print(f"  Ansicht im Browser:  http://localhost:{port}/")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nBeendet.")


if __name__ == "__main__":
    main()
