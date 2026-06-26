# Flux Design System

Flux is an **AI-first** mobile OS: the assistant *is* the interface, not an app
grid. This document is the canonical design language behind that idea — the
rules the C rendering code (`shell/src/ui.c`, `icons.c`, `anim.h`) implements.

The goal of the redesign was to move Flux away from a *terminal-like, technical,
raw* feel toward something that reads as:

> **A calm, intelligent assistant that communicates through icons and motion.**

---

## 1. Design philosophy

1. **The assistant is the surface.** The home screen is a conversation, not a
   launcher. Everything the user needs is reachable by *asking* or by a small
   set of icon affordances.
2. **Icon-first.** Where a meaningful symbol exists, the symbol leads. Text is a
   fallback, not the default. Icons are the primary UI language.
3. **Motion is feedback, not decoration.** Every interaction answers with a
   small, calm movement (150–350 ms). The system feels alive but never busy.
4. **One identity, no chaos.** A single background family and a single accent
   across the *entire* OS. No per-screen colors, no rainbow.
5. **Safety in actions.** When the AI proposes a real-world action (mail, SMS,
   call) it *never* executes directly — it shows a clear, icon-driven
   confirmation the user can cancel, edit, or confirm.
6. **Delegation, made visible.** Internally Flux delegates to specialized
   subagents (messaging, calendar, files, system, web). The UI *shows which
   capability is acting* — but the user never manages agents directly.

---

## 2. Icon system

- **Source:** [Lucide](https://lucide.dev) (MIT), rasterized with NanoSVG and
  blitted as a single-color, tintable mask (`flux_fb_blit_mask`). See
  `docs/ICONS.md` for the pipeline and how to add an icon.
- **Stroke style:** one consistent stroke (`stroke-width=2`,
  `stroke-linecap=round`, `stroke-linejoin=round`), minimal, slightly rounded
  geometry. Defined once in `SVG_HEAD` (`icons.c`).
- **Sizes:** 16 (inline/hint), 20 (chips/quick actions), 24 (buttons), 30–34
  (primary action badge), 38 (hero/empty-state mark).
- **Color:** icons inherit the system accent or a muted neutral — never a
  one-off color.

### Icon-over-text rule
Prefer an icon wherever the meaning is unambiguous. Mandatory icon contexts:

| Context | Icon |
|---|---|
| Confirm / send | `check` |
| Cancel / close | `x` |
| Edit | `pencil` |
| AI / assistant | `sparkles` |
| Settings | `settings` (gear) |
| Files | `folder` |
| Messaging / SMS | `message-circle` |
| Mail | `mail` |
| Call | `phone` / `phone-call` |
| Calendar | `calendar` |
| Web / search | `globe` / `search` |

The quick-access row and the system-action buttons are **icon-only** — the
glyphs are universally legible and labels would only add noise.

---

## 3. Motion system

All motion lives in the **150–350 ms** window and uses calm easing
(`shell/src/anim.h`):

| Curve | Use |
|---|---|
| `flux_ease_out_cubic` | appearing / entering (the all-rounder) |
| `flux_ease_in_cubic` | disappearing / leaving |
| `flux_ease_in_out_cubic` | symmetrical transitions |
| `flux_ease_out_back` | a gentle "pop" for icons/badges |
| `flux_pulse` | living accent dots / recording indicators |
| `flux_shimmer` | travelling highlight for loading states |

Named durations: `FLUX_MOTION_FAST_MS` (150), `FLUX_MOTION_BASE_MS` (250),
`FLUX_MOTION_SLOW_MS` (350).

### Interaction catalogue
- **Tap** → soft glow ripple at the touch point (`flux_ui_draw_ripple`,
  `animate_ripple`).
- **Screen change** → slide-up with a small spring settle (`animate_slide_in`).
- **AI response** → progressive fade-in from a dark overlay
  (`animate_answer_fadein`, ~250 ms) — the answer *arrives*, it doesn't snap in.
- **Loading / thinking** → a shimmering bar inside the AI bubble next to the
  spark mark (`flux_shimmer`), instead of a hard spinner. Reduces perceived
  waiting time.
- **Card / row focus** → subtle lift + accent edge.

Principle: never a heavy loader or a jarring cut.

---

## 4. Color system

Flux ships **one** background family and **one** accent. The accent is fixed in
`flux_ui_set_accent()` (the function ignores its argument on purpose — there is
no theme picker).

**Background — "Deep Space" (dark):**

| Token | Hex | Role |
|---|---|---|
| `COL_BG` | `#07080D` | base background |
| `COL_SURFACE` | `#0F1117` | raised surface |
| `COL_SURFACE2` | `#161C27` | cards |
| `COL_SURFACE3` | `#1E2635` | elevated / active |

**Accent — Flux identity (the only accent, system-wide):**

| Token | Hex | Role |
|---|---|---|
| `COL_ACCENT` | `#6366F1` | Indigo — primary accent |
| `COL_ACCENT2` | `#8B5CF6` | Violet — gradient endpoint |

**Text:** `COL_TEXT #F1F5F9`, `COL_TEXT_MUTED #94A3B8`, `COL_DIM #64748B`.

**Semantic (muted, used sparingly):** success `#10B981`, error `#EF4444`,
warning/amber tones. These appear only for status — never as decoration.

**Rules:** no rainbow UI · no per-screen accent · the same Indigo→Violet
gradient marks every primary action, every active state, every AI moment.

### Typography
Flux renders a **real proportional sans-serif** — *Instrument Sans* (SIL OFL),
embedded as TrueType and rasterized with `stb_truetype` (`shell/src/fb.c`,
`font_data.h`). No monospace, no bitmap "terminal" glyphs. The conventions are
unchanged for layout code: `flux_fb_text(x, y, …, scale)` still treats `y` as
the line top and `scale` as the size step (2 = body, 3 = title, 4 = large,
7 = clock), so existing centering via `flux_fb_text_width` just works. Real
umlauts and ß are drawn as proper glyphs (kerned), not approximated. AI replies
read as conversation, not logs.

---

## 5. Screen redesigns

### Lock screen — *minimal, ambient, icon hints*
Large clock over a soft ambient accent halo (concentric dark-indigo circles),
elegant date, optional proactive AI card. The only affordance is a **lock icon
+ up-chevrons** hint — no text instructions. (`flux_ui_draw_lock`)

### Assistant home — *the AI surface*
- Centered **spark mark** (`sparkles`) in the accent gradient circle + “Wie kann
  ich helfen?”.
- **Icon quick-access row** (settings / files / calendar / contacts) — icon-only
  pills.
- **Suggested action chips** (Mail · Wecker · Suche · Termin) as icon-first
  pills that prefill the prompt.
- Voice button + prompt field always present.
- Conversation as bubbles; the AI's reply fades in. (`flux_ui_draw_assistant`)

### System action confirmation — *safe, modern, clear*
The critical screen. (`flux_ui_draw_confirm`)
- A **large action icon** (mail / message / phone) in the accent gradient circle.
- An **agent chip** ("Mail-Agent", "Messaging-Agent", "Telefon-Agent") with a
  living accent dot — this is the delegation cue.
- The editable content card (tap any line to edit it).
- Three **icon buttons**: Cancel **✕**, Edit **✎**, Confirm **✓** (filled accent
  gradient). Each ≥48 px. No text labels.

### Settings — *grouped icon cards*
Each setting is an icon card: a tinted Lucide glyph in a rounded badge, label +
value stacked, chevron affordance. One accent throughout; the old color-theme
row is gone. (`flux_ui_draw_settings`)

### Files & Notifications
Every row/group leads with a meaningful icon (folder / file type / source) to
honor the icon-first rule.

---

## 6. Emotional experience & the agent model

Flux should feel like a **calm, intelligent presence**. You unlock into a quiet,
dark space with a single warm indigo signature. You speak; the assistant answers
with a soft fade. When it's working, a gentle shimmer says *"I'm on it"* rather
than a anxious spinner. When it's about to do something real, it pauses, shows a
big clear symbol and tells you **which capability is handling it**, and waits for
a calm ✕ / ✎ / ✓.

**The subagent model, surfaced honestly:** Flux routes work to internal
specialists — a messaging agent, a calendar agent, a file-system agent, a system
agent, a web agent. The UI reflects this *indirectly*: action confirmations and
busy states name the responsible capability (the agent chip), so a task feels
**delegated**, not manually executed. Crucially, this is read-only signaling.
There is no agent manager, no agent settings, no orchestration UI — the
architecture stays invisible plumbing, and the user only ever talks to one calm
assistant: **Flux**.
