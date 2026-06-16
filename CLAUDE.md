# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Flux is an AI-first mobile OS prototype: a Linux/ARM64 kernel (Buildroot)
with two custom C components on top — `flux-shell` (the only UI process,
no app grid, you unlock straight into an AI assistant) and `fluxaid` (a
system daemon that answers local device intents and falls back to an
LLM). It targets QEMU `aarch64 virt` today; real phone hardware is an
explicit, unstarted roadmap step.

Source comments and commit messages are written in German; keep that
convention when editing existing files.

## Commands

### Host-only compile check (fast, no `/dev/fb0` or socket needed for compiling)
```bash
cd fluxai && make          # builds ./fluxaid natively
cd shell && make           # builds ./flux-shell natively (compiles, but
                            # drawing needs a real /dev/fb0 -- i.e. QEMU)
make clean                 # in either dir
```

### Running fluxaid standalone on the host (no QEMU needed)
`fluxaid` only needs a Unix socket, so the daemon logic can be exercised
without booting anything:
```bash
cd fluxai && make && ./fluxaid &
# then drive it with a raw socket client, e.g. Python:
python3 - <<'EOF'
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("/run/flux/fluxai.sock")
s.send(b"Q:wie spaet ist es\n")
print(s.recv(8192))
EOF
```
This is the fastest way to test/iterate on `actions.c`, `contacts.c`,
`email.c`, `provider.c`, and the protocol dispatch in `main.c` — no
kernel build required.

### Full image build + QEMU boot (slow first time — builds a full
Buildroot toolchain/kernel)
```bash
./build/build.sh        # needs a Buildroot checkout; see error message
                         # it prints for the exact clone command/branch
                         # if $FLUX_BUILDROOT_DIR (default
                         # /tmp/flux-build/buildroot) doesn't exist
./build/run-qemu.sh      # boots the built image in QEMU (headless by
                         # default; FLUX_DISPLAY=gtk for a real window)
```
`build.sh` cross-compiles `flux-shell`/`fluxaid` with the *exact* cross
compiler Buildroot itself produced (avoids glibc version mismatches
against the rest of the rootfs), copies both binaries into
`build/overlay/usr/bin/`, then has Buildroot repack the final image.

The defconfig is tracked in-repo at
`build/configs/flux_aarch64_virt_defconfig` and copied into the
Buildroot checkout by `build.sh` before building — Buildroot only finds
named defconfigs in its own `configs/`, so don't edit a copy living
only inside `$FLUX_BUILDROOT_DIR`; edit the repo copy and rerun
`build.sh`.

### Inspecting a running QEMU instance without a display
QEMU runs with `-display none` and a QMP control socket
(`/tmp/flux-qemu-qmp.sock`) by default, so it works headless/in a
sandbox:
```bash
./build/screendump.sh /tmp/out.ppm   # QMP screendump -> convert to png
                                      # with ImageMagick `convert` if you
                                      # need to view it (PIL may not be
                                      # available in all environments)
```
Synthetic input (keyboard/touch) is driven over the same QMP socket via
`input-send-event` (`send-key` for hardware-keyboard keys, abs
x/y + button events in QEMU's 0–32767 range for touch/tap/swipe) — there
is no committed helper script for this, write one ad hoc against the
QMP socket when testing UI changes.

## Architecture

Three independently testable layers — see `docs/ARCHITECTURE.md` for
full detail, this is the map you need to navigate the source:

```
flux-shell (shell/)  --Unix socket-->  fluxaid (fluxai/)  --HTTP-->  local LLM / Anthropic cloud
   |
   draws directly to /dev/fb0
```

### `flux-shell` (`shell/src/`)
Single UI process, no app-grid concept. A `flux_screen_t` state machine
with three screens (`shell/src/main.c`): `FLUX_SCREEN_LOCK`,
`FLUX_SCREEN_ASSISTANT`, `FLUX_SCREEN_CALL` (a clearly-labeled simulated
call screen — QEMU has no modem/SIM, so it never pretends to place a
real call).

Multi-step dialogs (creating a contact, composing an email) are **owned
entirely by the shell** as a `flux_dialog_t` state machine in `main.c` —
`fluxaid` only ever sees the final, single `C:`/`M:` request once the
dialog completes. Don't add dialog state to `fluxaid`; that statelessness
is intentional (see protocol section below).

Key files:
- `fb.c`/`fb.h` — double-buffered framebuffer writes; `flux_fb_present()`
  diffs row-by-row against the previous frame and only copies what
  changed. This dirty-tracking is the entire reason the UI feels smooth
  without GPU compositing — don't bypass it with direct writes.
- `stb_easy_font.h` — vendored Public Domain bitmap font (Sean Barrett);
  text is rendered as vector quads, not a TTF renderer.
- `input.c`/`input.h` — generic evdev discovery via `EVIOCGBIT` (not
  hardcoded to one device type); keyboard and touch/pointer fds are held
  open in parallel and `select()`-ed together. Touch produces
  `FLUX_EV_TAP` (with coordinates) or `FLUX_EV_SWIPE_UP`.
- `ui.c`/`ui.h` — screen drawing *and* the on-screen-keyboard hit-testing
  share one geometry function (`build_kbd_geom`) so drawing and tap
  detection can't drift apart.
- `ipc.c`/`ipc.h` — thin client wrappers around the daemon protocol
  (`flux_ipc_ask`, `flux_ipc_add_contact`, `flux_ipc_find_contact`,
  `flux_ipc_send_email`), all built on one shared connect/send/receive
  helper.

### `fluxaid` (`fluxai/src/`)
System daemon (not an app), starts before the shell, listens on a Unix
domain socket (`/run/flux/fluxai.sock`). **One request per connection,
intentionally stateless** — the line-based protocol is defined in
`common/flux_protocol.h`:

```
Q:<frage>                          -> A:<antwort> | ERR:<meldung>
C:<name>\t<telefonnummer>          -> A:<bestaetigung> | ERR:<meldung>
F:<name>                           -> A:<name>\t<telefonnummer> | ERR:<meldung>
M:<empfaenger>\t<betreff>\t<text>  -> A:<bestaetigung> | ERR:<meldung>
```
Response is always one line plus `END\n`. No JSON-RPC, no HTTP server —
deliberately a minimal line protocol for a kernel-adjacent daemon.

Request resolution order for `Q:` (`main.c` dispatches into):
1. `actions.c` — local intents (time, date, battery, uptime, contact
   list) answered using naive substring matching (no NLP). If a local
   intent matches, the question never leaves the device.
2. `provider.c` — only if (1) found nothing:
   a. Local LLM first, only if `FLUX_AI_LOCAL_URL` is set (Ollama or LM
      Studio, both speak the OpenAI-compatible
      `/v1/chat/completions`). Unreachable or unparsable response falls
      through silently to (b) — no behavior change when the variable is
      unset.
   b. Anthropic cloud API using the user's own `FLUX_AI_API_KEY`. No
      key configured -> an honest "not configured" message, never a
      fabricated answer or a crash. This same honesty principle applies
      throughout the codebase (e.g. the battery-sensor message when
      running in QEMU with no `power_supply` node) — don't invent
      plausible-looking data to paper over missing functionality.

`contacts.c` owns `/var/lib/flux/contacts.tsv` (tab-separated,
case-insensitive name matching/dedup) exclusively — the shell never
touches that file directly, only through `C:`/`F:`.

`email.c` sends via libcurl/SMTP with TLS forced
(`CURLOPT_USE_SSL = CURLUSESSL_ALL`, credentials never sent in
plaintext), configured via `FLUX_SMTP_URL`/`FLUX_SMTP_FROM` (required)
and `FLUX_SMTP_USER`/`FLUX_SMTP_PASS` (optional). Unconfigured -> honest
error, no simulated send.

`provider.c`'s `extract_json_string()` is a deliberately naive JSON
field extractor (not a real parser) — it must tolerate optional
whitespace around `:` per the JSON spec, since some serializers
(including Python's `json.dumps`) insert a space after the colon. If you
touch this function, preserve that tolerance.

### Kernel/rootfs (`build/`)
Buildroot-managed; no handwritten boot code. The TLS backend
(`BR2_PACKAGE_OPENSSL` + `BR2_PACKAGE_LIBCURL_OPENSSL`) is required —
without it the cross-compiled libcurl has no TLS at all, breaking both
the cloud fallback (https) and email (smtps/STARTTLS). `build/overlay/`
only replaces `/etc/inittab` (starts `fluxaid` then `flux-shell` via
`respawn`, keeps a serial getty for debugging) and ships the two
binaries; everything else is stock Buildroot.

## Security note (dev build)

The built image has a passwordless root login on the serial console and
no app sandbox — an accepted tradeoff for a dev/demo build, not a
production security model.
