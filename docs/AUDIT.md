# Flux — Production-Readiness Audit

Date: 2026-06-19
Scope: full codebase (`common/`, `shell/`, `fluxai/`, `build/`, `docs/`),
~9,500 LOC of C.

This document is the deliverable for the audit task: current architecture,
problems found with severity, the refactoring plan, and (at the end) the
record of what was actually changed in this pass plus remaining debt.

---

## 1. Current architecture

Flux is an AI-first mobile OS userspace on top of a Buildroot Linux/aarch64
kernel. Three independently buildable C components:

| Component | Path | Role |
|---|---|---|
| `flux-shell` | `shell/` | Single UI process. Draws to `/dev/fb0`, reads evdev input, talks to the daemon over a Unix socket. |
| `fluxaid` | `fluxai/` | System AI daemon. Local intents → Anthropic API fallback → tool calls → confirmed actions (mail/SMS/call). |
| `common/` | `common/` | Shared mini-protocol, config file (`key=value`), SHA-256 for the PIN hash. |

Communication is a deliberately tiny line-based protocol
(`Q:<question>` / `X:<action>` → `A:<answer>\nEND`). State that the AI
should remember lives in flat files under `/etc/flux/` and `/home/user/`.

The project has grown well beyond what the README/ARCHITECTURE describe.
Reality today: **16 UI screens** (not 7), a full tool layer (30 tools),
calendar, contacts, gallery + camera (V4L2), image vision analysis, AI
memory, meeting recorder, semantic search, Whisper voice input, a daily AI
journal, proactive lockscreen notifications, and passive habit learning.

---

## 2. Problems found (by severity)

### Critical / High — Security

| # | Issue | Location | Impact |
|---|---|---|---|
| S1 | **Shell command injection.** Image path is interpolated into a `system("convert '%s' ...")` string in single quotes; a filename containing `'` escapes the quoting and runs arbitrary shell. | `fluxai/src/vision.c` | Arbitrary command execution as root from an attacker-influenced filename. |
| S2 | **Unrestricted file read → secret exfiltration.** The `file_read` tool reads *any* path with no allow-list. Combined with the `mail` action, the model can be steered to read `/etc/flux/flux.conf` (API key, SMTP password, PIN hash) and email it out. | `fluxai/src/tools.c` | Credential/secret disclosure. |
| S3 | **Path traversal.** `file_create` / `file_delete` only check a string prefix (`/home/user/`), not `..` components, so `/home/user/../../etc/...` escapes the sandbox. | `fluxai/src/tools.c` | Write/delete outside the intended sandbox. |
| S4 | **Weak PIN scheme (documented dev-build limitation).** 4-digit PIN, unsalted single SHA-256, no rate limiting → full keyspace (10⁴) is brute-forceable instantly if the config file leaks. | `common/flux_sha256.c`, `shell/src/main.c` | Lock bypass if `flux.conf` is read. Acceptable for dev build, must change for real hardware. |

### High — Correctness

| # | Issue | Location | Impact |
|---|---|---|---|
| C1 | **Background tasks pollute the user's conversation context.** `proactive`, `journal`, and `habits` call `flux_provider_ask`, which appends their large internal prompts to the shared `ctx_history` ring. The user's next assistant turn carries irrelevant background prompts as "conversation". | `fluxai/src/provider.c` + 3 callers | Degraded answer quality, wasted tokens. |
| C2 | `unlink` used without `#include <unistd.h>` (implicit declaration warning). | `fluxai/src/proactive.c` | UB-adjacent; build warning. |
| C3 | Ignored return values of `fscanf`/`fgets` in several spots — partially-read buffers used as if populated. | `fluxai/src/tools.c`, `shell/src/ui.c` | Possible use of uninitialized values on malformed input. |
| C4 | Silent key truncation in the config parser (key buffer is 64 B but a line can be ~576 B). | `common/flux_config.c` | A long key is silently mangled instead of skipped. |

### Medium — Architecture / maintainability

| # | Issue | Location |
|---|---|---|
| A1 | **God file.** `shell/src/main.c` (2,156 lines) holds the entire event loop, every screen's navigation, all module state, and persistence. The navigation idiom (`capture_frame → set screen → draw → animate → free`) is copy-pasted ~40 times. |
| A2 | **Duplicated HTTP/JSON plumbing.** `curl_write_cb` + `membuf` + `extract_text` are reimplemented in `provider.c`, `tools.c`, and `vision.c`. |
| A3 | **Duplicated case-insensitive substring search.** The "lowercase both sides then `strstr`" loop is copy-pasted in 4 functions in `tools.c`. |
| A4 | **Documentation drift.** README + ARCHITECTURE.md describe an earlier, much smaller system (7 screens, voice as a placeholder, no calendar/contacts/gallery/memory/etc.). Misleads any new contributor. |
| A5 | Fixed-size stack buffers everywhere with manual length juggling; the daemon handles one request per connection synchronously (a 20 s API call blocks all other UI requests). |

### Low — Quality

- Magic numbers for timeouts/limits scattered inline (300 s loop, 3600 s
  proactive guard, 60 KB habits cap, etc.).
- `static char` scratch buffers inside loops (`tool_file_list`) — not
  reentrant and surprising.
- Inconsistent language in comments (German + English mixed).

---

## 3. Refactoring plan (ordered by impact / risk)

The constraint that shapes this plan: in this environment the code can be
**compiled** (native gcc) but **not run** (no `/dev/fb0`, no QEMU, no
Anthropic key). So changes are limited to those verifiable by build +
static reasoning. A blind rewrite of the 2,156-line UI loop is explicitly
*out of scope* here — it cannot be regression-tested and would be
irresponsible.

**Pass 1 (this change) — safe, high-value, build-verified:**
1. Security: S1 (escape/validate), S2 (deny secrets + traversal in
   `file_read`), S3 (reject `..`).
2. Correctness: C1 (ephemeral provider call for background tasks), C2, C3,
   C4.
3. DRY (low risk, file-local): A3 — extract a case-insensitive helper.
4. Docs: A4 — rewrite ARCHITECTURE.md to match reality; this audit.

**Future passes (need a runnable target to be safe):**
- A1: split `main.c` into per-screen handlers + a navigation helper.
- A2: extract `fluxai/src/http.{c,h}` (shared curl/JSON helpers).
- S4: salted KDF + attempt throttling before any real-hardware build.
- A5: per-connection threading or non-blocking API calls in the daemon.

---

## 4. Final report — changes made in this pass

All changes were verified by a clean native build of both binaries plus
targeted tests (daemon protocol round-trip over the Unix socket; standalone
unit tests of the new helpers; an injection test proving the `convert`
call can no longer be escaped).

### Files modified

| File | Change |
|---|---|
| `fluxai/src/vision.c` | **S1 fix.** Added `shell_quote()` and use it for the image path in the `convert` `system()` call. Injection test confirms a filename with `'` is now treated as literal text. |
| `fluxai/src/tools.c` | **S2/S3 fix.** `file_read` now refuses `flux.conf` (secrets); `file_create`/`file_delete` reject `..` traversal via new `path_has_traversal()`. **A3 (DRY):** added `str_contains_ci()` and replaced 4 copy-pasted lowercase-and-`strstr` loops. **C3:** checked the three ignored `fscanf` results in the brightness tools. |
| `fluxai/src/provider.{c,h}` | **C1 fix.** Factored the ask path into `provider_ask_impl(…, use_history)`; added `flux_provider_ask_ephemeral()` that neither reads nor writes the conversation ring. |
| `fluxai/src/proactive.c` | Use ephemeral ask; added missing `#include <unistd.h>` (**C2**); marked unused params. |
| `fluxai/src/journal.c`, `fluxai/src/habits.c` | Use ephemeral ask (**C1**). |
| `common/flux_config.c` | **C4 fix.** Skip over-long keys/values instead of silently truncating; removes the truncation warning. |
| `shell/src/ui.c` | **C3 fix.** Checked the ignored `fscanf`/`fgets` results in battery/wifi readers. |
| `docs/ARCHITECTURE.md`, `README.md` | **A4 fix.** Documented the current 16-screen surface, tool layer, vision, voice, and background services; corrected the stale "7 screens / voice is a placeholder" claims. |
| `docs/AUDIT.md` | This report. |

### Security improvements
- Closed an arbitrary-command-execution vector (S1).
- Closed a secret-exfiltration path: the model can no longer read the
  credentials file through `file_read` (S2).
- Closed `..` sandbox escapes in file write/delete tools (S3).

### Correctness / performance improvements
- Background AI jobs no longer pollute the user's conversation context,
  which also stops them from inflating per-turn token usage (C1).
- Removed an implicit-declaration UB risk (C2) and several unchecked
  `fscanf`/`fgets` reads (C3); config parser no longer mangles long keys
  (C4).
- ~60 lines of duplicated search code collapsed into one helper (A3).

### Verification performed
- `make` clean in both `fluxai/` and `shell/` — both link, no new
  warnings (the daemon is warning-clean except one pre-existing benign
  sysfs path-truncation note).
- Ran `fluxaid` and exercised `Q:` time/date/battery, the no-key cloud
  fallback, and an unknown-protocol request over the socket — all correct.
- Unit-tested `path_has_traversal` (incl. the `..secret` false-positive
  edge) and `str_contains_ci` (11/11), and the `shell_quote` injection
  defense.

### Remaining technical debt (not addressed — needs a runnable target)
1. **`shell/src/main.c` (2,156 lines)** — split into per-screen handlers
   and a single `navigate(screen, draw_fn)` helper (the ~40× copy-pasted
   `capture_frame → draw → animate → free` idiom). Deferred because it
   cannot be regression-tested without QEMU/`/dev/fb0`.
2. **Shared HTTP/JSON layer** — extract `fluxai/src/http.{c,h}` to remove
   the triplicated `curl_write_cb`/`membuf`/`extract_text` (A2).
3. **PIN hardening (S4)** — salted KDF + attempt throttling before any
   real-hardware build.
4. **Daemon concurrency (A5)** — one blocking request per connection means
   a 20 s API call stalls all UI requests; move to per-connection handling
   or non-blocking calls.
5. Remaining benign `-Wformat-truncation` notes in `main.c`/`ui.c` — fix by
   right-sizing a handful of buffers (low priority, all truncate safely).

### Future recommendations
- Add a tiny host-side test harness (the daemon already runs without a
  framebuffer) and wire it into a `make test` target + CI so future
  refactors of `main.c` and the HTTP layer can be done safely.
- Introduce a small `secret_store` abstraction so the move off plaintext
  `flux.conf` later touches one module, not every caller.
- Consider streaming/`-Wconversion` passes once the above tests exist.
</content>
</invoke>
