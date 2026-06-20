# Flux — Hybrid AI Architecture

Design for embedding AI deeply into Flux OS across a resource-constrained
device (`fluxaid` + `flux-shell`) and a powerful LAN backend (MacBook running
Gemma-class local models), with cloud (Anthropic) as last-resort fallback.

This document is opinionated. Where the current code forces a decision, the
decision is named explicitly and justified against `fluxai/src/*` as it exists
today.

---

## 0. The three facts that shape everything

Before any new feature, four properties of the current code dictate the design.
Ignoring them produces a pretty diagram that won't compile onto this daemon.

1. **`fluxaid` is single-threaded and fully blocking.** `handle_client()`
   (`fluxai/src/main.c:42`) runs a synchronous, up-to-20s `curl` call
   (`provider.c:179`) *inside* the `accept()` loop. While one request is in
   flight, the daemon answers nothing else and the proactive timer
   (`main.c:97`) cannot fire. **A hybrid router that adds a *second* network
   hop (device → MacBook → maybe cloud) makes this worse unless concurrency is
   fixed first.** This is the single most important change.

2. **The shell IPC is blocking and single-shot.** `flux_ipc_send_raw()`
   (`shell/src/ipc.c:34`) issues one `read()` and freezes the UI until the
   whole `A:…END` arrives. There is no streaming and no "thinking…" state.
   Progressive UX is impossible without changing this function and the
   wire protocol.

3. **Routing today is binary and keyword-based.** `flux_actions_try()`
   (`actions.c:72`) matches a handful of German substrings; everything else
   falls through to the cloud (`main.c:55-56`). There is no concept of a LAN
   backend, no privacy classification, no latency budget.

4. **The trust model is already correct in spirit — keep it.** The model only
   ever *proposes* `ACTION:` and `TOOL:` blocks; `exec.c` and `tools.c` do the
   actual work *on the device* after user confirmation. Secrets (`api_key`,
   SMTP password) live only in the daemon and **never enter a prompt**. The
   hybrid design must preserve this: **the backend is a brain, not a pair of
   hands.**

**Guiding principle:** *The MacBook gains the right to think about your data on
your LAN. It never gains the right to act, to hold secrets, or to touch the
filesystem.* Every routing and security decision below follows from that one
sentence.

---

## 1. Architecture Design

### 1.1 Tier map — what runs where

```
┌──────────────────────────── DEVICE (Flux, ARM64, constrained) ─────────────────────────┐
│                                                                                          │
│  flux-shell (UI, /dev/fb0)                                                               │
│     │  Unix socket  /run/flux/fluxai.sock   (Q:/X:  ->  P:/A:/ERR:/END, now streaming)   │
│     ▼                                                                                     │
│  fluxaid (system AI daemon)                                                              │
│   ┌────────────────────────────────────────────────────────────────────────────────┐   │
│   │ router.c        intent classify + tier decision (privacy/latency/complexity)     │   │
│   │ actions.c       TIER 0  deterministic local intents (time/date/battery/uptime)   │   │
│   │ cache.c         TIER 0  response + tool + negative cache (sha256 keyed)           │   │
│   │ backend.c       TIER 2  client to MacBook (HTTP/1.1 + SSE over existing libcurl)  │   │
│   │ provider.c      TIER 3  cloud fallback (Anthropic) — unchanged role              │   │
│   │ tools.c/exec.c  ACTIONS always execute HERE, never remote (secrets, fs, mail)     │   │
│   │ secrets         api_key, SMTP creds — read only by exec/provider, never sent up   │   │
│   └────────────────────────────────────────────────────────────────────────────────┘   │
│                                    │ HTTPS + PSK (LAN)                                    │
└────────────────────────────────────┼─────────────────────────────────────────────────────┘
                                      ▼
┌──────────────────────────── MacBook (fluxd-backend, trusted-on-LAN) ─────────────────────┐
│  HTTP server (mTLS / PSK)                                                                 │
│   ┌──────────────────────────────────────────────────────────────────────────────────┐  │
│   │ /v1/infer     streaming chat completion (Gemma via llama.cpp / Ollama / MLX)       │  │
│   │ /v1/health    cheap liveness + model + load info (router probes this)              │  │
│   │ /v1/embed     optional: embeddings for semantic cache / memory retrieval           │  │
│   │ model-runner  pluggable: gemma-2-9b, qwen, … behind one adapter interface          │  │
│   │ NO secrets, NO device fs, NO send-mail. Emits ACTION:/TOOL: proposals only.        │  │
│   └──────────────────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────────────────────┘
                                      │ only if backend offline AND user allows cloud
                                      ▼
                              Anthropic API (provider.c, today's path)
```

### 1.2 Boundaries — what stays local, what is forwarded, when we fall back

| Class | Example | Default route | Rationale |
|---|---|---|---|
| **Deterministic system intent** | "wie spät", "Akku", "uptime" | **Tier 0 local, never leaves device** | Already instant in `actions.c`; a round trip would be absurd. |
| **Local data lookup** | "welche Termine heute", "lies datei X" | **Tier 0 tool, local** | The data lives on the device. The model isn't needed to *fetch* it; only to phrase it. Run the tool locally, only escalate phrasing if needed. |
| **General reasoning / chat** | "erklär mir X", "fass das zusammen" | **Tier 2 MacBook** if reachable, else Tier 3 cloud (if allowed), else degraded local answer | Gemma 9B on LAN is fast and private. Cloud only when LAN is down. |
| **PII-bearing reasoning** | anything carrying memory.txt, contacts, message bodies | **Tier 2 MacBook only**; cloud **blocked** unless user opts in per-request | LAN is the privacy sweet spot: capable *and* off the internet. |
| **Action proposal** | "maile Anna, dass…" | Model (Tier 2/3) *proposes* `ACTION:`; **execution is always Tier 0 local** via `exec.c` after confirm | Preserves existing trust model; secrets never travel. |
| **Vision** | `image_analyze` | Tier 2 if backend has a vision model, else Tier 3 | Heaviest payload; keep on LAN when possible. |

**Fallback ladder (per request):** Tier 0 (try local intent/cache) →
Tier 2 (MacBook, if `/v1/health` is fresh) → Tier 3 (cloud, if allowed by
privacy class *and* configured) → **graceful degraded answer** (honest "offline"
message + whatever local tools can produce). Never crash, never fabricate —
that ethos is already in `provider.h` and must hold.

---

## 2. Communication Layer (device ⇄ MacBook)

### 2.1 Transport — decision: **HTTP/1.1 + chunked SSE over the libcurl already linked**

Rejected alternatives, with reasons:

- **gRPC** — needs protobuf + http2 + codegen; a heavy C++/C runtime on a
  Buildroot ARM64 image whose entire ethos is "every allocation counts"
  (`flux_protocol.h:4`). Not worth it for one client and ~3 endpoints.
- **Raw TCP custom protocol** — you'd reinvent framing, TLS, retries. `curl`
  already solves all three and is *already a dependency* (`provider.c`,
  `tools.c`).
- **Unix tunnel / SSH** — couples deployment to having an account on the
  MacBook; brittle for a "product."

**Chosen:** the MacBook runs a small HTTP server. `fluxaid`'s new `backend.c`
talks to it with the same `curl_easy_*` pattern `provider.c` already uses, with
`CURLOPT_WRITEFUNCTION` consuming a **stream** instead of buffering. Zero new
device dependencies. This is the single most important "don't be clever"
decision in the whole design.

### 2.2 Message format

- **Request:** compact JSON (the device already hand-builds JSON in
  `provider.c:130`; reuse `json_escape_append`). Keep it tiny.
- **Response:** **Server-Sent Events / NDJSON stream** — one JSON object per
  line, flushed token-by-token (or chunk-by-chunk). This is what unlocks the
  "thinking… → refined answer" UX without a second protocol.

Request schema (`POST /v1/infer`):

```json
{
  "v": 1,
  "request_id": "uuid-or-monotonic",
  "intent_class": "chat | pii_chat | action_propose | vision",
  "model_hint": "gemma2-9b",
  "max_tokens": 600,
  "stream": true,
  "system": "…Flux system prompt, with memory/prefs already injected by device…",
  "messages": [
    {"role": "user", "content": "…"},
    {"role": "assistant", "content": "…"}
  ],
  "tools": ["weather","calendar_list", "..."],   // names only; schemas known to backend
  "locale": "de-DE",
  "ts": 1718873200
}
```

Streaming response frames (NDJSON, one per line):

```json
{"t":"meta","model":"gemma2-9b","queue_ms":4}
{"t":"delta","text":"Der näch"}
{"t":"delta","text":"ste Termin ist…"}
{"t":"tool","name":"calendar_list","arg":""}      // model wants a tool; device runs it
{"t":"done","stop":"end_turn","tokens":142}
```

Key rule: **the backend never returns tool *results*.** It emits a `tool` frame;
`fluxaid` executes the tool locally (`flux_tool_exec`) and continues the
conversation with a follow-up `/v1/infer` carrying the result — exactly the
two-call loop `provider.c:284-312` already implements, just pointed at the LAN.

### 2.3 Latency, retry, timeout, offline

- **Latency budget per tier**, enforced in `router.c`:
  - Tier 0: < 5 ms (no network).
  - Tier 2 first token: target < 400 ms on LAN; **hard deadline 1.5 s to first
    byte**, total 30 s for long generations (streaming hides the tail).
  - Tier 3: today's 20 s `CURLOPT_TIMEOUT` stays.
- **Health-gated routing, not blind retand-pray.** `backend.c` keeps a cached
  `/v1/health` result (TTL ~10 s). The router only routes to Tier 2 if the last
  probe was OK; this avoids paying a TCP-connect timeout on *every* request when
  the MacBook is asleep.
- **Retry:** exactly **one** retry for Tier 2 on connect/first-byte failure with
  a short backoff (250 ms), then **fail over to Tier 3/degraded** — do not
  hammer. Idempotency via `request_id` so a retried request the backend already
  saw isn't double-run.
- **Timeout split:** separate `CURLOPT_CONNECTTIMEOUT_MS` (300 ms on LAN) from
  the total timeout, so a sleeping MacBook is detected in 300 ms, not 20 s.
- **Offline:** see §6.

---

## 3. Intelligence Routing

A new `router.c` in `fluxaid` replaces the implicit `if (!actions_try) provider_ask`
in `main.c:55`. It is a pure function over (request, system state); easy to test
on the host.

### 3.1 Classification — cheap, deterministic, on-device first

```
classify(question) -> { intent_class, privacy_tag, est_complexity }

  1. actions.c match?            -> SYSTEM_INTENT  (handle Tier 0, return)
  2. cache hit (sha256)?         -> CACHED         (return)
  3. carries PII?                -> privacy_tag = PII
       (question references memory/contacts, or contains @, phone, names from
        contacts.txt; or the system prompt would inject memory.txt -> treat as PII)
  4. obvious tool intent?        -> run tool locally first, then phrase
  5. else                        -> GENERAL reasoning
```

Keep this rule-based, not ML. A classifier model on a constrained device buys
little and adds latency; the gains are at Tier 2 where the real model lives.

### 3.2 Tier decision — a small explicit policy table

```
decide(intent_class, privacy_tag, backend_health, cloud_allowed):

  SYSTEM_INTENT / CACHED      -> TIER 0
  ACTION_PROPOSE              -> brain = best available (T2 else T3-if-allowed);
                                 execution ALWAYS T0
  PII + backend_up            -> TIER 2
  PII + backend_down          -> DEGRADED (do NOT silently send PII to cloud)
  GENERAL + backend_up        -> TIER 2
  GENERAL + backend_down + cloud_allowed -> TIER 3
  GENERAL + backend_down + !cloud         -> DEGRADED
```

The dynamic inputs are real, not decorative:
- **privacy** gates cloud hard (PII never leaves the LAN by default).
- **latency** is handled by the health-gate + connect-timeout split, so a slow
  network *demotes* a tier instead of hanging the UI.
- **complexity** chooses `model_hint` / `max_tokens` (a one-liner doesn't need
  the 9B at 600 tokens).

### 3.3 Caching (`cache.c`)

- **Response cache:** key = `sha256(normalized_question + system_prompt_version)`
  using the existing `common/flux_sha256.c`. Store under `/var/cache/flux/`.
  Only cache **idempotent, non-PII, non-time-sensitive** answers (never "wie
  spät", never anything that injected memory.txt). TTL per class.
- **Tool cache:** generalize the pattern `weather` already uses
  (`tools.c:139`, `/tmp/flux_weather.txt`) into `cache.c` with per-tool TTLs.
- **Negative/health cache:** the `/v1/health` result (§2.3) — so we don't probe
  the MacBook on every keystroke-completed query.
- **Embedding cache (later):** if `/v1/embed` lands, cache memory.txt embeddings
  so retrieval doesn't re-embed every turn.

---

## 4. Privacy & Security

### 4.1 Secrets must never leak — enforce structurally, not by discipline

The reason secrets are safe today is *structural*: `provider.c` builds the prompt
from `memory.txt`/`prefs.txt` but **never reads `api_key`/SMTP into the prompt**;
those are read only by `exec.c`/`mail.c` at execution time. Preserve and harden:

- **One module owns secrets.** Move all secret reads behind a `secrets.c` API
  (`flux_secret_get(name)`), and make `router.c`/`backend.c` *structurally
  unable* to call it. Outbound payloads are assembled by `backend.c`, which has
  no `#include "secrets.h"`. A grep-able invariant beats a code-review promise.
- **Tools that touch secrets are Tier-0-only and not in the remote `tools` list.**
  `mail_send`, telephony, anything reading `/etc/flux/*` creds is never offered
  to the backend, only proposed as an `ACTION:` the *device* executes.
- **Payload scrubber** in `backend.c`: a final assertion that the serialized
  request contains no value returned by `flux_secret_get` (cheap substring
  check). Defense in depth.

### 4.2 Trust boundaries

| Boundary | Trusted with | NOT trusted with |
|---|---|---|
| **flux-shell ↔ fluxaid** (Unix socket) | everything (same device, root daemon) | — |
| **fluxaid ↔ MacBook** (LAN) | content of queries, memory/prefs context, tool *names* | secrets, credential-bearing tool execution, device filesystem writes |
| **fluxaid ↔ Anthropic cloud** | only non-PII GENERAL queries, only if `cloud_allowed` | all PII by default |

The MacBook is **semi-trusted**: trusted to *reason about* your data on your LAN,
never to *hold* it persistently or *act*. Treat it as a thinking peripheral.

### 4.3 Encryption & authentication between devices

- **Transport:** HTTPS even on the LAN. The MacBook backend runs with a
  self-signed cert; the device pins it (`CURLOPT_CAINFO` / `CURLOPT_PINNEDPUBLICKEY`).
- **Authentication:** a **pre-shared key** in `/etc/flux/flux.conf`
  (`backend_psk=…`, mode 0600 — `flux_config_set` already chmods 0600,
  `flux_config.c:79`) sent as a bearer header. Simple, robust, revocable.
- **Mutual auth (product-grade):** upgrade to **mTLS** — device presents a client
  cert provisioned during pairing. This also stops a rogue device on the LAN
  from using your MacBook as free compute.
- **Pairing UX:** show a code on the MacBook, type it on the device once
  (reuse `FLUX_SCREEN_EDIT_BODY` keyboard). Exchanges the PSK/cert. No accounts,
  no cloud.

---

## 5. User Experience

The whole point of streaming (§2.2) is UX. Concretely:

- **Immediate ack:** the instant the user submits, the shell shows the echoed
  question + a `…` spinner. No blank freeze. This requires making
  `flux_ipc_send_raw` non-blocking (§10).
- **Progressive response:** the daemon forwards backend `delta` frames to the
  shell as `P:<partial>` protocol frames; the shell repaints the answer area as
  text streams in (the framebuffer diff in `flux_fb_present` already makes
  partial repaints cheap — this is a perfect fit).
- **Tier-aware affordance:** a tiny glyph indicates source — ⚡ local, ⌂ MacBook,
  ☁ cloud, ⚠ degraded — so the user builds an intuition for privacy & speed.
- **Two-stage answers where useful:** for a heavy query, optionally show a fast
  local heuristic ("Du hast 2 Termine heute") immediately, then let the MacBook
  refine into prose. Mark the refinement clearly so it doesn't look like a glitch.
- **Never block the UI thread.** The shell's `select()` already waits on input
  and could wait on the daemon socket too; the answer arrives as events, not as
  a blocking `read()`.

---

## 6. Offline-First Strategy

Offline is the *normal* case, not an error path. The device must be useful with
neither MacBook nor cloud.

- **MacBook offline (detected in ≤300 ms via connect-timeout + health cache):**
  - SYSTEM_INTENT, all local tools, calendar/contacts/memory CRUD, calculator,
    file ops — **all still work** (they're Tier 0 already).
  - GENERAL non-PII → cloud *if* `cloud_allowed`, else a degraded answer.
  - PII → **never silently to cloud**; honest "Mein lokaler Assistent (MacBook)
    ist gerade offline" + offer to retry or to use cloud *with explicit consent*.
- **Slow network:** the connect-timeout split demotes Tier 2 → next tier instead
  of hanging. Streaming means even a slow-but-up backend feels responsive
  (first token early).
- **Proactive features** (`proactive.c`, `journal.c`, `habits.c`) must tolerate
  the brain being absent: today they call `flux_provider_ask` directly; reroute
  them through `router.c` so they prefer the MacBook, skip gracefully when both
  are down (they already no-op on the "Kein Cloud" string, `proactive.c:158`).
- **Optional Tier 1 (tiny on-device model):** only if/when hardware allows a
  ~1–3B quantized model via llama.cpp. Treat as a *future* slot in the router,
  not a v1 requirement — the constrained device shouldn't pretend to be the
  MacBook.

Degradation order is always: **best brain available → honest local answer →
honest offline message.** Never fabricate.

---

## 7. Extensibility

### 7.1 Adding tools without tight coupling

The current `flux_tool_exec` (`tools.c:1183`) is a 30-arm `if/strcmp` chain and
the description is a hand-written string (`tools.c:1218`) — they drift. Replace
with a **registry**:

```c
typedef struct {
    const char *name;
    const char *desc;          // one line; also fed to the model
    int (*fn)(const char *arg, char *out, size_t cap);
    unsigned flags;            // TOOL_LOCAL_ONLY (touches secrets/fs/creds),
                               // TOOL_CACHEABLE, TOOL_REMOTE_OK
} flux_tool_t;

void flux_tools_register(const flux_tool_t *t);
```

- Dispatch and the model-facing description are **generated from one table** →
  they can't drift.
- `flags` drive policy: `TOOL_LOCAL_ONLY` tools are excluded from the backend's
  `tools` list automatically (§4.1). New calendar/notes tools are one struct +
  one `register` call.
- The backend keeps a **matching schema registry**; the device sends tool *names*
  only, so adding a tool is a device-side change + a backend schema entry.

### 7.2 Plugging in different models

The MacBook backend hides the model behind a single `model-runner` adapter
interface:

```
interface ModelRunner {
  stream infer(system, messages[], tools[], params) -> deltas
  health() -> {model, ctx, load}
}
```

Implementations: `GemmaRunner` (llama.cpp/Ollama/MLX), `QwenRunner`, a
`CloudProxyRunner` (so even Anthropic could be fronted uniformly later). The
device's `model_hint` is advisory; the backend maps it to whatever is loaded.
Swapping Gemma → another model is a backend config change, **zero device
changes**. This is the decoupling that matters for a product.

---

## 8. Performance

- **Minimize round-trips:** the tool loop is the main cost (two `/v1/infer`
  calls). Mitigations: (a) run obvious tools *locally before* the first infer
  when the intent is unambiguous (don't ask the 9B to ask for the calendar it
  could've been handed); (b) allow the backend to batch a tool request +
  continuation in one stream when the tool is `TOOL_REMOTE_OK` and pure.
- **Streaming over buffering:** first-token latency is what users feel; stream
  everywhere (§2.2). The framebuffer's per-line diff (`flux_fb_present`) already
  makes incremental repaint cheap — exploit it.
- **Avoid serialization overhead:** reuse `json_escape_append` (no JSON lib on
  device); keep system prompt assembly out of the hot path by **versioning** it
  and caching the serialized blob until memory/prefs change (mtime check).
- **Connection reuse:** keep a warm `CURL` handle / HTTP keep-alive to the
  MacBook so repeated queries skip TCP+TLS setup (today every call does a fresh
  `curl_easy_init`).
- **Don't over-send context:** `CTX_MAX=3` turns (`provider.c:42`) is fine for
  the device; let the *backend* own longer context/memory retrieval — the device
  shouldn't ship its whole history every turn.

---

## 9. Concrete Output

### 9.1 Data flow (a chat query, MacBook up)

```
user types "fass meine termine diese woche zusammen"
  shell: show echo + spinner; send  Q:<text>\n   (non-blocking)
  fluxaid main loop: hand connection to a worker (no longer blocks)
    router.classify -> GENERAL, privacy_tag=PII (calendar is personal)
    router.decide   -> TIER 2 (backend_health fresh)
    backend.c: build system prompt (memory/prefs injected locally, NO secrets)
               POST /v1/infer (stream)
    MacBook: Gemma streams deltas; emits {"t":"tool","name":"calendar_list"}
    fluxaid: run flux_tool_exec("calendar_list") LOCALLY  (data stays on device)
             POST /v1/infer #2 with tool result
    MacBook: streams final prose deltas
    fluxaid: forward each delta to shell as  P:<chunk>\n ; finish with A:/END
  shell: repaint answer area progressively; show ⌂ (MacBook) glyph
offline at any step -> demote per §3.2 ladder; UI never hangs
```

### 9.2 Protocol schema

**Device ⇄ shell (Unix socket), extended — backward compatible:**

```
Client -> Daemon:   Q:<question>\n                      (unchanged)
                    X:<type>\nTO:…\nSUBJECT:…\nBODY:\n…  (unchanged, action exec)
Daemon -> Client:   P:<partial chunk>\n      (NEW: zero or more, streaming)
                    A:<final answer>\nEND\n  (unchanged terminal frame)
                    ERR:<msg>\nEND\n          (unchanged)
                    S:<glyph>\n               (NEW optional: source tier ⚡⌂☁⚠)
```
Old clients that ignore `P:`/`S:` still work (they read until `END`).

**Daemon ⇄ MacBook (HTTP):** see §2.2 (`/v1/infer`, `/v1/health`, `/v1/embed`).

### 9.3 Modules to implement

**In `fluxaid` (`fluxai/src/`):**
- `router.c/.h` — `classify()` + `decide()` policy (§3). Pure, host-testable.
- `backend.c/.h` — MacBook client: streaming `curl`, health cache, retry/timeout
  split, PSK/mTLS, payload scrubber.
- `cache.c/.h` — response/tool/negative cache over `flux_sha256`.
- `secrets.c/.h` — sole owner of `api_key`/SMTP reads; not included by `backend.c`.
- `tools.c` refactor — registry table + `flags` (§7.1); auto-generate description.
- `main.c` rework — **concurrency** (worker thread or fork per connection) +
  streaming write path; route through `router.c` instead of the hardcoded
  `actions→provider` (`main.c:55`).
- `provider.c` — keep as Tier 3 behind the router; reuse its `curl` + JSON helpers
  in `backend.c`.
- `proactive.c`/`journal.c`/`habits.c` — call `router_ask()` not
  `flux_provider_ask()` directly.

**On the MacBook (`fluxd-backend/`, new — language free, suggest Go or Python):**
- `server` — HTTP/TLS, `/v1/infer` (SSE), `/v1/health`, `/v1/embed`.
- `runner/` — `ModelRunner` interface + `GemmaRunner` (Ollama/llama.cpp/MLX).
- `tools/schemas` — JSON schemas matching device tool names (names-in, schema-here).
- `auth` — PSK verify now, mTLS + pairing later.
- `pairing` — show-code / exchange-key flow.

**Shared (`common/`):**
- `flux_protocol.h` — document the new `P:`/`S:` frames.
- (optional) a tiny `flux_json.h` extracting `json_escape_append`/`extract_text`
  so device and tests share one copy.

### 9.4 Step-by-step migration plan

Each step ships independently and leaves the system working.

1. **Unblock the daemon (prerequisite, no features).** Make `fluxaid` handle
   requests off the accept loop (worker thread or `fork`), so a slow upstream
   can't freeze proactive checks or a second client. Pure refactor of `main.c`;
   behavior identical.
2. **Stream end-to-end with the *current* cloud path.** Add `P:` frames to the
   protocol; make `provider.c` parse Anthropic's SSE (`stream:true`) and emit
   `P:` chunks; make the shell render progressively (`ipc.c` + `ui.c`). Now the
   UX win exists *before* the MacBook does, de-risking the protocol.
3. **Extract `router.c`.** Replace `if(!actions_try) provider_ask` with
   `router_ask()` containing today's exact two-tier behavior. No new routes yet —
   just the seam. Host-test the policy table.
4. **Extract `secrets.c` + `cache.c`.** Lock the secret-ownership invariant and
   add response/tool caching. Independently valuable, no backend needed.
5. **Stand up `fluxd-backend` on the MacBook.** `/v1/health` + `/v1/infer` over
   Gemma, PSK auth, self-signed TLS. Test with `curl` by hand.
6. **Add `backend.c` as Tier 2** behind the router, gated by a config flag
   (`backend_url`, `backend_psk`). Ship the health-gated ladder (§3.2). Now real
   hybrid: MacBook primary, cloud fallback, local always-on.
7. **Refactor `tools.c` to the registry + `flags`;** auto-exclude
   `TOOL_LOCAL_ONLY` from the remote tool list; drive the model description from
   the table.
8. **Harden security:** pinned cert / mTLS, pairing flow, payload scrubber
   assertion, source-tier glyph in UI.
9. **Polish:** keep-alive connection reuse, system-prompt blob caching,
   two-stage fast/refined answers, embeddings cache. Optional Tier 1 tiny local
   model as a future router slot.

---

## 10. The one risk to call out loudly

**Do not build the MacBook tier before step 1.** Adding a second, slower network
hop to a daemon that blocks its entire event loop on the *first* network hop will
make Flux feel worse, not better — every MacBook round-trip would freeze proactive
notifications and any concurrent caller. Concurrency first, streaming second,
hybrid routing third. In that order the system improves at every step; in any
other order, step N regresses what step N−1 shipped.
```
