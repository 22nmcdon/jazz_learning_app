# CLAUDE.md

Guidance for Claude Code (or any Claude instance) working in this repository.

For **what the app does and how each feature behaves**, see `README.md`. For
the reasoning behind solo practice, see `docs/SOLO_PRACTICE.md`; for comping,
`docs/COMPING.md`; for the rhythm grid they share, `docs/RHYTHM.md`. Read the
relevant one before touching `LineAnalyzer`, `Comping.h`/`compPlan()`, or
`Rhythm.h`. This file is only for what you need to not break the repo or redo
settled work.

## Project Overview

A standalone, cross-platform jazz education app built on **JUCE**, targeting
desktop (macOS/Windows) and mobile (iOS/Android) from one shared codebase. It
has two modes over one chart: **chord practice** (reharmonization + real-time
voicing feedback) and **solo practice** (line-by-line feedback on
improvisation). Full feature behavior is in `README.md`.

`docs/jazz_learning_app_design.pdf` is the *original* design doc — treat it as
history, not current truth. It predates the webview UI and solo practice.
Where it disagrees with this file or `README.md`, this file wins.

## Architecture — Read Before Adding Code

Three layers, as separate modules. Do not blur this boundary.

| Layer | Responsibility | May depend on |
|---|---|---|
| **Core Engine** | Chord parsing, reharm logic, scale suggestion, voicing/line analysis. Pure C++, no JUCE GUI. | Nothing platform-specific |
| **Engine API** (`modules/engine_api`) | JSON wire format for every shell. Pure C++, no JUCE, no Emscripten. | Core Engine |
| **User Interface** (`web/`) | The one interface (lead sheet, keyboard, panels), served on the web and hosted by the app. | Engine API |
| **Platform Shell** (`app/`) | Webview hosting `web/`, plus MIDI I/O, audio device, file reading, app lifecycle. | JUCE platform APIs |

- **There is one UI: `web/index.html`.** The JUCE component UI was removed. Don't
  reintroduce a second implementation of a screen — a visual change belongs in
  `web/index.html`.
- **Core Engine never includes JUCE GUI headers.** A reference to `juce::Component`
  etc. in that layer means the logic belongs one layer up.
- **UI reads from Core Engine, never the reverse.**
- **MIDI input is abstracted behind one interface** (hardware and on-screen
  produce the same event shape). The sustain pedal travels the same interface
  (`NoteInputListener::sustainChanged`) rather than being folded into `NoteEvent`.
- **File formats split the same way**: getting bytes out of a file is the
  shell's job; deciding what the text means is the engine's (`ChartFormats`
  takes positioned text in, gives a `Chart` out). A new format adds a reader
  to the shell, not theory to the engine.
- **Test format readers against a real export, not an invented fixture** —
  every import bug so far (split-run chord text, flipped page coordinates,
  real flat signs, PDFs with drawn-not-textual symbols) was one no fixture
  would have had.
- Keeping Core Engine UI-agnostic keeps a future AUv3/VST3 target open. Don't
  couple engine logic to the standalone shell.

## UI Conventions

One page (`web/index.html`) serves both desktop and mobile — "responsive"
means CSS, not per-platform builds.

- **One breakpoint, 760px.** No compact/regular/expanded size classes. Add a
  second breakpoint only when something actually collides (test with
  `JAZZ_UI_SIZE=430x860` or a 430px viewport).
- **Differences are by input type, not platform** (e.g. the scale picker is a
  dialog or a bottom sheet depending on room, never two implementations).
- **Anything mode- or platform-specific is `data-mode`/`onTheWeb` + a CSS/JS
  branch — never a second component.**
- **The on-screen keyboard is first-class**, not a fallback: 2 octaves by
  default, widens with a connected MIDI keyboard or an out-of-range note. No
  octave-shift control.
- Do not introduce a second platform-specific UI layer (JUCE screen, native
  sheet, separate mobile stylesheet) — this was a deliberate scope decision.

## Input Scope (Current Phase)

In scope: MIDI keyboard (USB/Bluetooth), on-screen keyboard (touch/mouse).
Out of scope: audio/pitch-detection input — do not add without an explicit
decision to expand scope (much larger DSP undertaking, deferred deliberately).

## Platform Notes

- **iOS**: Bluetooth MIDI works via JUCE out of the box; USB needs a
  Lightning/USB-C camera adapter.
- **Android**: Bluetooth MIDI since Android 6; USB via OTG. JUCE's Android MIDI
  is less mature than iOS — test here first when MIDI issues are reported.
- Target is a JUCE **standalone GUI app** (`juce_add_gui_app`), not a plugin
  client. An AUv3/VST3 target reusing Core Engine is a possible future addition.

## Build System

CMake, not the Projucer — one `CMakeLists.txt` per module, JUCE pulled in by
`cmake/GetJUCE.cmake`. Commands are in `README.md`.

Two rules the build enforces:
- **Core Engine links no JUCE.** `cmake -DJAZZ_BUILD_APP=OFF` must build the
  engine and its tests with no JUCE/GUI libraries present. If a change breaks
  this, the layering has been broken.
- **Tests live with the engine** (`tests/`, against `modules/core_engine` only,
  using the harness in `tests/TestFramework.h` — no third-party test dep).

`web/` is both the UI and a second transport to the engine (Emscripten →
WebAssembly via `web/build.sh`). If `web/build.sh` fails, something
platform-specific has leaked into Core Engine.

CI (`.github/workflows/ci.yml`): one job builds with `-DJAZZ_BUILD_APP=OFF` on a
GUI-less runner (a failure here means broken layering, not a missing package —
check that before reaching for `apt-get`); another builds the full app. CI also
checks the test count against what `README.md`/the page's colophon quote (it's
gone stale twice).

`.github/workflows/pages.yml` builds the wasm, deploys to GitHub Pages, and runs
`web/smoke-test.mjs` against the built page before deploy. Run it yourself after
touching the page: `./web/build.sh out && cp web/index.html web/sw.js out && node
web/smoke-test.mjs out` — a build that compiles has not been the thing that goes
wrong here.

Android is the exception to CMake-everywhere (JUCE's CMake support doesn't cover
it): uses the Projucer/Gradle exporter over the same source tree.

## Feature Status

Full behavior is in `README.md`. Status only, so a session doesn't re-plan
something finished or assume something unfinished is done:

- **Reharmonization Assistant, Real-Time Chord/Voicing Analyzer** — done.
- **Solo practice** (line analysis, take scoring, walking-bass reading) —
  done. See `docs/SOLO_PRACTICE.md` before touching `LineAnalyzer`.
- **Comping** — style data and generator done; the evaluator (scoring a
  player's own comping) and a third "Comping practice" mode are decided but
  not built. See `docs/COMPING.md` for the full status table before starting
  either — `state.mode` being two-valued today is the expensive part.
- **Drums** — named in the menu, not built (pure shell problem: pattern + kit,
  no engine call needed).
- **Not built, deliberately open**: voice-leading visualizer, personal voicing
  library, ear training, progress tracking beyond current per-take/session
  stats, licks/line suggestions, chordal (not line) reading in solo practice,
  MusicXML/MuseScore import, metre round-tripping on export, PDF
  reading/printing in the JUCE app (the engine's reader is shared and
  format-agnostic; the app just lacks a PDF text-extraction library).

## Critical Invariants

These aren't stylistic preferences — violating them breaks something in a way
that's easy to miss in review:

- **The engine has no clock and must never get one.** Time/position enters as
  data (a `BarPosition`, a beat+tick on the shared grid in `docs/RHYTHM.md`)
  that the shell computed from its own clock — never as something the engine
  infers about when a call arrived. This is what keeps `LineAnalyzer` and
  comping's `compPlan()` testable.
- **Rhythm and voice-leading produce words, never points.** `score()` is
  untouched by timing or line-shape — see `score()`'s own doc comment before
  changing this.
- **Single source of truth for anything on the wire.** `scaleStyles()`,
  `compStyles()` etc. come from the engine via `EngineApi`; never hardcode a
  copy in a shell — it will drift the first time the engine's catalogue
  changes. The same principle is why the rhythm grid has one doc, not three.
- **`VoicingAnalyzerTests` holds two invariants that must hold after any
  change to a voicing/shape table**: every voicing `idiomaticVoicings` offers
  must classify as the type it was offered for, and none may exceed what one
  hand can reach. Comping reuses these shapes — see `docs/COMPING.md`.
- **A wider chord vocabulary is not automatically better** — `ChordIdentifier`
  scores every known name against the notes played, so a new name competes
  with all the rest. Check what a new name costs the chords that already
  resolve correctly (e.g. a complete but obscure name beating a common
  rootless voicing) before adding it.
- **Engine/page version mismatch fails silently, not loudly.** `web/index.html`
  requests `jazz-engine.js?v=<build stamp>`; without it a cached engine paired
  with a fresh page makes a feature simply *vanish* (empty menu, engine chip
  still says ready) rather than erroring.
- **The app's UI must never block on the network** — a render-blocking resource
  that never arrives leaves the whole webview invisible (backgrounds paint, no
  text does). Webfonts are requested by script, only when served over the web.
- **The service worker (`web/sw.js`) never sees the visit that registers it** —
  registration happens on `load`, after both the page and engine are already
  fetched, so it fetches them itself on install. Test offline behavior by
  asserting on the cache's contents, not just a reload (the browser's HTTP cache
  can mask a broken worker).
- **Load the page from a file, not JUCE's resource provider** — the provider
  intermittently fails to deliver a document this size on Linux (page loads,
  script never runs). `WebUi` writes the page to the temp dir and points the
  webview there instead. If revisiting this, verify ~10 runs, not one.
- **Engine calls are async in the app, sync on the web** — every call site
  `await`s `call()`. An engine call made around that funnel works in the browser
  and fails silently in the app.
- **GitHub Pages is the real home of the web demo, not an embedded frame** — Web
  MIDI needs a permission an embedded frame generally can't request.

## Open Questions (Don't Assume — Ask)

- iReal Pro and PDF import both ship. Is MusicXML/MuseScore import needed too?
- How much of the reharm suggestion engine should be rule-based vs. data/ML-informed?
- Is a future dense, DAW-style desktop layout worth designing for now, or deferred?

If work touches one of these, flag the ambiguity rather than silently picking a direction.

## Working Conventions

- Expand Core Engine (unit-testable, UI-agnostic) first; wire into UI second.
- Default new UI components to one size-class-aware component, not
  platform-conditional forks.
- **JUCE idioms apply to the shell only, and the shell is small** — `WebUi`
  (holds the webview) and `MidiDeviceInput` (Bluetooth pairing sheet) are the
  only JUCE classes with real UI. A new `resized()`/`paint()` elsewhere is a sign
  something's being built in the wrong layer.
- **Which scales/styles belong together is theory — the engine holds the list**,
  never a shell-side copy (see Critical Invariants).
- **Check the narrow window before calling a layout done** — things that overlap
  at 420px can look fine at 1200px. `JAZZ_UI_SIZE=430x860` (app) or a 430px
  viewport (browser) catches it in seconds; checking either shell checks both
  (same CSS).
- **Web MIDI can be tested without a MIDI keyboard**: stub
  `navigator.requestMIDIAccess` in a *generated copy* of `web/index.html` (so the
  shipped file stays untouched), drive the real handler with raw bytes, report
  results via `document.title` for headless Chromium's `--dump-dom`.
- **A feature added for the browser lands in the app too** since it's one file —
  `onTheWeb` is the one branch separating them (webfonts, offline worker,
  shareable temp-file links, the "no MIDI" message). Before adding a fifth
  browser-only branch, check the app's behavior isn't just the browser's failing
  quietly.
- **A failing test is a question, not a chore.** Several tests here have encoded
  bugs rather than correct behavior in the past — check which is wrong before
  changing either side.
