# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

FluxOS is a mobile operating system whose home screen **is** an AI assistant —
there is deliberately no app grid. It consists of two own C components running
on top of a stock Linux kernel:

- **`flux-shell`** (`shell/`) — the single UI process. A state machine that
  draws directly to the framebuffer (`/dev/fb0`).
- **`fluxaid`** (`fluxai/`) — the system AI daemon. Listens on a Unix socket,
  answers local intents first, falls back to a cloud LLM, and executes
  user-confirmed actions (mail/SMS/call).

The kernel + rootfs are built by **Buildroot** (not hand-written). Target
platforms: QEMU `aarch64 virt` and a real **Raspberry Pi 5**.

## Build & run commands

```bash
# Host smoke test (no QEMU/device). flux-shell only compiles here — drawing
# needs /dev/fb0, so it won't actually render on the host.
cd fluxai && make && ./fluxaid &
cd ../shell && make

# Build either component for the target with the Buildroot toolchain:
make CROSS_COMPILE=aarch64-linux-gnu-     # (run inside shell/ or fluxai/)

# Full QEMU image (Buildroot builds toolchain+kernel; first run is slow).
# Requires Buildroot cloned at /tmp/flux-build/buildroot (build.sh prints the
# exact `git clone --branch 2024.02.x ...` command if missing).
./build/build.sh
./build/run-qemu.sh                 # headless by default; GUI: FLUX_DISPLAY=gtk ./build/run-qemu.sh

# Raspberry Pi 5
./build/build-pi5.sh                # → .../out-pi5/images/sdcard.img
./build/flash-pi5.sh /dev/sdX       # writes to microSD — verify the device first

# Inspect QEMU framebuffer headlessly (QMP screendump)
./build/screendump.sh
```

There is **no test framework**. Verification is: (1) compile cleanly with
`-Wall -Wextra -Wformat-security`, (2) render the UI to PNGs offscreen via
`shell/tools/render_screenshots.c` (uses `flux_fb_open_null()` instead of
`/dev/fb0`, writes PNGs with libpng). The README screenshots come from this
exact path, so it doubles as a visual regression check of the real draw code.

**Toolchain note:** `build.sh` deliberately compiles `flux-shell`/`fluxaid`
with the cross-compiler Buildroot produces (`$OUT_DIR/host/bin/*-linux-gnu-gcc`),
not a system one — otherwise the binaries get a glibc version mismatch against
the rest of the rootfs. The Makefiles take `CROSS_COMPILE` as the prefix.

## Architecture (the big picture)

The whole system is three layers, each doing exactly one thing:

```
flux-shell  ──Unix socket /run/flux/fluxai.sock──▶  fluxaid  ──▶  Linux kernel
(draws)        Q:<frage> | X:<aktion>               (thinks)      (Buildroot)
```

**Protocol — `common/flux_protocol.h` (read this first).** Line-based, one
request per connection, *deliberately not JSON*:
- `Q:<question>\n` — a question. Truncated at the first newline.
- `X:<type>\nTO:…\nSUBJECT:…\nBODY:\n<text>` — an action the user already
  confirmed. Kept multi-line (must not be truncated).
- Server replies `A:<answer>\nEND\n` or `ERR:<msg>\nEND\n`.

**The confirmation contract is the core invariant.** The AI never executes
mail/SMS/call directly. When `fluxaid`'s LLM detects a clear action intent, its
system prompt (`fluxai/src/provider.c`) makes it reply to a `Q:` with a
structured `ACTION:<type>\nTO:…\nSUBJECT:…\nBODY:\n…` block instead of free
text. `flux-shell` parses that (`shell/src/action.c`), shows the confirmation
screen, and only a tap on "Senden" sends the `X:` execute request. Preserve
this flow when touching `provider.c`, `action.c`, `exec.c`, or the protocol.

**Request order in `fluxaid` for `Q:` (`fluxai/src/main.c`):**
1. `actions.c` — local intents (time, date, weekday, battery, uptime, WLAN-/
   Flugmodus-Status). If matched, the
   question never leaves the device (speed + privacy).
2. `provider.c` — cloud fallback only if (1) found nothing. Multi-provider:
   Anthropic (Messages API + prompt caching), DeepSeek and NVIDIA NIM (both
   OpenAI-compatible). Active provider + keys come from `/etc/flux/flux.conf`
   or `FLUX_AI_API_KEY`/`DEEPSEEK_API_KEY`/`NVIDIA_API_KEY`. The LLM can also
   call tools (`tools.c`: `web_search`, `mail_unread`, `mail_read`, vision …).

`X:` requests go straight to `exec.c`, which dispatches by type to `mail.c`
(real SMTP via libcurl) or `telephony.c`.

**`flux-shell` is a state machine** (`flux_screen_t` in `shell/src/ui.h`,
driven by `shell/src/main.c`): Lock → (PIN, only if a `pin_hash` is set) →
Assistant (the home screen) → Confirm / Edit / Settings / Files. Settings and
Files are switched client-side without a daemon roundtrip (same "local first"
stance as `actions.c`).

**Rendering has one hard rule:** every list/keyboard has a single geometry
function used by *both* drawing and hit-testing (`build_kbd_geom`,
`build_pin_geom`, `build_confirm_buttons`, `build_list_rows` in `ui.c`). Never
compute layout separately for draw vs. tap — they drift apart. The framebuffer
(`fb.c`) keeps two buffers (`back`, `prev`); `flux_fb_present()` copies only
changed rows — this is the entire "smooth without a GPU" trick.

**Swappable backends.** Where hardware is absent in QEMU, the code reports it
honestly instead of faking success, and isolates it behind one file so a real
backend is a drop-in replacement without protocol/UI changes:
- `fluxai/src/telephony.c` — SMS/call modem stub → replace with ofono/ModemManager.
- `shell/src/voice_unlock.c` — RMS fingerprint stub → replace with ECAPA-TDNN.
- The mic button / `whisper.cpp` and battery sensor are similarly honest stubs.

## Conventions

- **C11**, `-O2 -Wall -Wextra -Wformat-security -std=gnu11 -D_GNU_SOURCE`.
  Keep it warning-clean; that is the closest thing to a CI gate.
- **No new abstraction without a concrete existing requirement.** The roadmap
  in `README.md` lists what was *deliberately* not built yet — don't pre-build it.
- **Vendored single-header libs** (don't rewrite): `stb_easy_font.h` (font,
  public domain), `nanosvg.h`/`nanosvgrast.h` (SVG, zlib).
- **Honesty over fake success** is a product principle, not just a dev habit:
  when a sensor/modem/mic is missing, say so in the UI; don't invent a result.
- Code comments, commit messages, and all UI strings are in **German** — match
  that when editing existing files.
- Secrets (`pin_hash`, SMTP password, API keys) live in `/etc/flux/flux.conf`
  (`chmod 0600`) via `common/flux_config.c`; the UI shows them only as
  "gesetzt"/`********`, never plaintext.

## Key files to read before changing behavior

- `common/flux_protocol.h` — the shell↔daemon contract.
- `fluxai/src/main.c`, `provider.c`, `exec.c` — daemon request flow + LLM prompt.
- `shell/src/main.c`, `ui.h`, `action.c` — UI state machine + action parsing.
- `docs/ARCHITECTURE.md` — the authoritative design rationale.
