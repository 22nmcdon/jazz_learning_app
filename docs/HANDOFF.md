# Handoff

What is half-done, what is not started, and what is quietly wrong. `CLAUDE.md`
says what you need to know to not break the repo; this says what is left to do
in it.

Keep it honest: when a section here is finished, delete it rather than marking
it done. A handoff that accumulates is one nobody reads.

---

## Where this is

Branch: `claude/gifted-wozniak-7z4jt6`.

The last three commits restructured the page's layout:

| Commit | What it did |
|---|---|
| `a296503` | Three-zone frame: `body` is a flex column at `100dvh`, a new `.chart-zone` takes the remaining height and scrolls itself, `.dock` stopped being sticky. The rolling bar now brings itself into view — nothing in the page scrolled anything before. |
| `6bf5152` | Cut the masthead's prose and moved the chart's head into a one-row `.top-bar`. Bars readable went 4/12 → 12/12 at 1280×860 and **0/12 → 12/12** at 430×860. |
| `603ffd6` | Sectioned the cheat sheet into five tabs so it never has to be scrolled, and added six topics it never covered. |

Verify anything you change:

```
./web/build.sh out && cp web/index.html web/sw.js out && node web/smoke-test.mjs out
./tools/test-count.sh --check
cmake --build build          # the page is copied into the JUCE shell
```

---

## The layout work: commits 3 to 7

The first two of seven are done. These five are planned in detail and agreed;
the shape of the finished thing is a one-row top bar, a chart that takes every
remaining pixel, and a dock — with **one** settings panel instead of two
dropdowns in opposite corners.

The problem being solved, so a cold reader can judge the plan rather than just
follow it: settings live in `#menuPanel` (top right, 310px, seven groups,
seventeen controls, already scrolling) **and** `#compingPanel` (hanging off the
chart toolbar, four more groups). Nothing says which holds what. The smoke test
opens one or the other **62 times** to drive the app. The *Playing* group alone
is eleven rows nested three deep.

### 3 — The transport strip

Move `#playStatic`/`#playLive`, `#tempo`, `#timeSig`, `#armTake`/`#armLabel` and
`#beatRow` into `.top-bar`. Every handler binds by `id`, so relocating the
markup is enough. The deeper options — `#countIn`, `#loopFrom`/`#loopTo`,
`#ramp` + `#rampBy`/`#rampTo`, `#reharmLive` + `#reharmAmount`/`#reharmReach`,
and `#playingNote` — become **one** popover off the strip: one level of nesting
instead of three. Delete the emptied *Playing* group from `#menuPanel`.

The strip must reserve its height in Static, or switching Static↔In time shifts
the chart and the mode-change check fails.

Accepted trade-off: `#armTake` moves away from the keyboard. It is pressed once
per take, the space bar still arms it, and the beat dots belong beside the
tempo.

> **The test work is the bulk of this one.** `smoke-test.mjs` has roughly 34
> `#menuButton` and 28 `#compingButton` open/close pairs that exist only to
> reach a control behind a dropdown. Most become direct clicks — but they must
> be removed **as pairs**. Half a pair leaves the menu open over the page and
> every later click lands on the wrong thing. Also re-point `smoke-test.mjs`'s
> `.sheet-head #tempo` / `#timeSig` assertion to the strip.

### 4 — One settings panel

Merge `#compingPanel` into `#menuPanel`; delete `#compingWrap`,
`#compingButton`, `setCompingOpen()` and its listener. The `.menu-wrap`
outside-click rule keeps working with one panel. Widen to
`min(420px, calc(100vw - 32px))` and section it: **Sound**, **The band**,
**Voicings** (chords), **Scales** (solo), **MIDI keyboard**, **Chart**
(`#ioButton`, `#planButton`, `#editToggle`, `#restoreChart`), **About** (the
colophon, which is currently parked at the foot of the chart zone). Delete
`.sheet-tools` and `.tools-right`; `#editor` moves with `#editToggle`. At narrow
widths, reuse the bottom-sheet pattern the 760px query already gives dialogs.

**This is where the unbuilt features land**, which is most of why it is worth
doing: the comping style editor and voicing library as dialogs off *The band*
and *Voicings*, progress tracking off a new *Practice history* section, ear
training as a section that opens its own exercise the way *Chart* does. The
current growth pattern — another row in *Playing*, another button in
`.dock-foot` — is out of road.

All 28 `#compingButton` sites become `#menuButton`; the colophon check must open
the panel first.

### 5 — Regroup the dock foot

Twelve children on one wrapping row become two groups: **actions**
(`#playChord`, `#nameChord`, `#showVoicing`, `#guideButton`, `#sustainPedal`,
`#clearKeys`) and **status** (`#practising`, the two `.legend` spans,
`#leadLegend`). `#armTake` and `#beatRow` left for the top bar in commit 3. Drop
the inline `margin-left: auto` on `#clearKeys` in favour of the group split.

### 6 — Remember practice settings

Almost nothing survives a reload: only `jazzVoicingStyle`, `jazzScaleStyle` and
the cheat-sheet-seen flags. Add helpers beside the existing try/catch storage
code and persist tempo, metre, In time, sound bank, band members, comping style,
sounds, guide tones and loop range. Same flat `jazz*` naming, so no migration.

Not the chart, not a take, nothing mid-exercise. A remembered loop must go back
through `fillLoopRange()` — bars 1–12 recalled onto a four-bar chart is not a
loop. Every read stays `try`-wrapped for private windows.

### 7 — Docs, screenshots and counts

**Reshoot `docs/screenshot-*.png`.** All three were taken on 2026-09-17 and
predate the entire restructure — the compact one shows the exact 0-of-12-bars
state the work fixed. They were deliberately left stale rather than shot twice,
since commit 3 changes the top bar again. README carries a note under each
saying so; remove those notes when the images are replaced.

Then update `README.md`'s layout narrative for the finished shape, and run
`./tools/test-count.sh`.

### The second-breakpoint question

`CLAUDE.md` says add one only when something actually collides. The top bar's
contents will not fit one row at 390px. **Try `flex-wrap` first** — wrapping to
two rows may be perfectly good. Add a width breakpoint only if the 390px
overflow check fails, and record in the commit message which element collided
and at what width.

---

## Not built, deliberately open

Each of these needs its design question answered before any code. They are not
ordered.

- **Comping style customisation / an editor.** A `CompStyleDefinition` is slots
  on a grid; an editor is a grid you can click. The question is whether a
  user-made style is a first-class citizen of the engine's catalogue — which
  `CLAUDE.md`'s single-source-of-truth rule says the engine owns — or a shell-side
  overlay the engine never sees.
- **Personal voicing library.** Where does a saved voicing live, and what is it
  saved *against* — a chord symbol, a chord quality, or a bar of a particular
  tune? Storage is the easy half.
- **Progress tracking beyond per-take stats.** Needs a decision on what is worth
  keeping across sessions, and on whether the engine ever learns about history
  (today it has no memory at all, which is what keeps it testable).
- **Ear training.** The one item that is arguably a third mode rather than a
  setting, so it collides with `state.mode` being two-valued. Read
  `docs/COMPING.md`'s argument about what a third mode costs before deciding.
- **MusicXML / MuseScore import.** Still an open question in `CLAUDE.md`: is it
  wanted at all, given iReal Pro and PDF both ship? A reader is shell-side work
  plus nothing in the engine.
- **PDF reading and printing in the JUCE app.** The engine's reader is shared and
  format-agnostic; the app simply lacks a PDF text-extraction library. This is a
  dependency decision, not a design one.
- **Chordal (not line) reading in solo practice.** `LineAnalyzer` reads notes
  one at a time and already knows which were struck together; block-chord
  *reading* is a different question from the per-voice reading it does now.
- **Metronome jitter.** Not investigated. Every beat time is an origin plus a
  beat count at one spacing (see `CLAUDE.md`), so start by checking whether the
  jitter is in the scheduling or in the reporting.
- **Rule-based vs data-informed reharmonisation.** An open question in
  `CLAUDE.md`. The rules are deliberately kept separable and each carries its own
  explanation, so this stays answerable later.
- **Licks / line suggestions.** Named, never designed.

**Out of scope, needs an explicit decision to change:** audio / pitch-detection
input. `CLAUDE.md`'s Input Scope defers it deliberately — it is a much larger
DSP undertaking. Do not start it because a feature seems to want it.

---

## Loose ends

Small, verified, and none of them urgent.

- **`app/src/ElectricPiano.cpp:205`** — `const auto frequency = frequencyOf
  (midiNote);` is declared and never used in that function. A
  `-Wunused-variable` warning. Pre-existing.

- **`web/smoke-test.mjs:764`** — the check *"the clock moves the chart on its
  own"* asserts `#systems .bar:nth-child(2)` is rolling. A `.system`'s first
  child is `.system-number`, so `.bar:nth-child(2)` is the **first** bar of each
  system — the check asserts bar one is rolling, which is where the take starts.
  It is vacuous. Assert a later bar instead.

- **`web/smoke-test.mjs:737`** — `selectOption("#loopTo", "1")`. The options
  carry value `0..11` and label `1..12`, so a bare string is ambiguous. A
  previous session verified it resolves to bar index 0, which makes a **one**-bar
  loop while the comment at L768 says the loop is two bars long. **Re-confirm
  before changing anything** — the two readings differ only in which of value or
  label Playwright tries first. Then use the unambiguous `{ index: n }` form that
  L1804 already uses.

- **The printed chart keeps a faint ring on the selected bar.** The print rules
  clear `.bar`'s background but not the `box-shadow` on `[aria-pressed="true"]`.
  Pre-existing and cosmetic.

---

## What the smoke test already encodes

Two sets of checks were added with the layout work, and they replace the
throwaway probes used to build it — so the numbers quoted in those commit
messages are reproducible without writing measuring scripts again.

- **The frame** — that the page itself does not scroll while the chart does,
  that the dock never covers a bar, and that the rolling bar keeps itself in
  view through a chorus. The last of these was checked with a negative control:
  with the `scrollIntoView` removed it fails and names the bar it lost.
- **The cheat sheet** — every section, in both modes, at a desktop and a phone
  size, reports no overflow. The body keeps `overflow-y: auto` as a safety valve
  for a browser with larger text; what the check asserts is that it never has to
  use it.

The narrow-layout scan at the end of the file is the safety net for all of this.
It scans `.top-bar`, `.top-bar-right`, `.dock-foot` and `.feedback` at 390px.
**Anything that moves a control into a new container must add that container to
the scan**, or the net has a hole in it.
