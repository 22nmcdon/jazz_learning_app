# CLAUDE.md

Guidance for Claude Code (or any Claude instance) working in this repository.

## Project Overview

A standalone, cross-platform jazz education application built on the **JUCE framework**,
targeting desktop (macOS/Windows) and mobile (iOS/Android) from a single shared codebase.
The app helps jazz musicians build and reharmonize chord progressions, understand which
scales fit each chord, and get real-time feedback on the voicings they play.

Two core modules make up the POC:

1. **Reharmonization Assistant** — build or import a chord progression, tap a measure to
   see suggested scales (and browse alternatives), and explore reharmonizations.
2. **Real-Time Chord/Voicing Analyzer** — play a chord (MIDI keyboard or on-screen keyboard)
   against a loaded chart and get feedback on whether the voicing played matches the chord
   symbol's intent, with suggestions to improve it.

Full feature rationale and rationale for design decisions live in
`jazz_learning_app_design.pdf` (design doc) — treat that as the source of truth for *why*,
and this file as the source of truth for *how to work in the repo*.

## Architecture — Read This Before Adding Code

The project is split into three layers as **separate modules**. This boundary is
intentional and load-bearing — do not blur it.

| Layer | Responsibility | May depend on |
|---|---|---|
| **Core Engine** | Chord parsing, reharmonization logic, scale suggestion, voicing analysis. Pure C++, no JUCE GUI classes. | Nothing platform-specific |
| **Shared UI Component Library** | Chart view, on-screen keyboard widget, scale-suggestion panel, voicing feedback panel — each size-class–aware. | Core Engine (data only) |
| **Platform Shell** | MIDI I/O per platform, file import/export (iReal Pro, MusicXML), app lifecycle. | JUCE platform APIs |

Rules of thumb when writing or reviewing code:

- **Core Engine never includes JUCE GUI headers.** If a class in this layer needs to
  reference `juce::Component`, `juce::Graphics`, etc., that's a sign the logic belongs
  one layer up.
- **UI components read from Core Engine, never the reverse.** The engine has no knowledge
  that a UI exists.
- **MIDI input is abstracted behind one interface** so Core Engine / UI logic never
  branches on whether a note came from hardware MIDI or the on-screen keyboard — both
  should produce the same event shape. The sustain pedal travels that same interface
  (`NoteInputListener::sustainChanged`) rather than being squeezed into `NoteEvent`: a
  pedal is not a note, but what it changes — which notes still count as sounding — is
  exactly what every listener downstream is already tracking. Controller 64 and the
  on-screen control are indistinguishable by the time anything acts on them.
- **File formats split the same way.** Getting bytes out of a file — opening a PDF, asking
  a PDF library for its text — is the shell's job. Deciding which of that text is a chord
  chart, and turning chords into an iReal Pro link or back, is the engine's, and it does it
  without knowing what a file is: `ChartFormats` takes positioned text in and gives a
  `Chart` out. A new format should add a reader to the shell, not theory to it.
- **Read a format against a real export, not a fixture you wrote.** Every bug in the
  import path so far was one no invented fixture would have had: chord symbols arriving
  one letter per text run, page coordinates running the other way, real flat signs
  instead of "b", and iReal Pro PDFs that contain no chord text at all because the
  symbols are drawn and only a spoken description is left behind. Get a file out of the
  app in question and read that.
- Keeping Core Engine UI-agnostic is what makes a future AUv3/VST3 plugin target
  (sharing the same engine) possible without a rewrite. Don't take shortcuts that couple
  engine logic to the standalone app shell.

## UI Strategy: One Responsive UI, Not Per-Platform UIs

There is a single shared UI component library across desktop and mobile — not separate
UI codebases. Components adapt via **size classes** (compact / regular / expanded) using
JUCE's FlexBox/Grid, not hardcoded pixel layouts.

When building or modifying a UI component, account for:

- **Layout breakpoints** — multi-pane views (e.g. chart + analyzer side-by-side) collapse
  to tabbed/stacked views below a width threshold.
- **Interaction affordances differ by input type, not by platform** — e.g. the reharm
  assistant's scale picker is a dropdown on desktop/mouse, a bottom sheet on touch. Same
  underlying component, different presentation — implement as one component with a
  presentation mode, not two components.
- **On-screen keyboard** is a first-class input method, not a fallback. It must support
  **multi-touch chord entry**: simultaneous touches should register as one voicing event,
  not a stream of individual note-on messages. Default visible octave range is
  size-class–dependent (1–2 octaves, octave-shiftable, on compact/mobile).
- **Popups/modals**: desktop can use hover states and small floating popups; touch needs
  larger tap targets and full-sheet modals.

Do not introduce a second, platform-specific UI layer without discussing it first — this
was a deliberate scope decision to keep a solo/small-team POC maintainable.

## Input Scope (Current Phase)

In scope:
- MIDI keyboard (USB and Bluetooth)
- On-screen keyboard (touch and mouse)

Out of scope for now — do not add audio/pitch-detection input (mic or pickup) without
an explicit decision to expand scope; it's a much larger DSP undertaking than MIDI/on-screen
input and was deferred deliberately.

## Platform Notes

- **iOS**: Bluetooth MIDI works via JUCE's MIDI API out of the box; USB MIDI needs a
  Lightning/USB-C camera adapter.
- **Android**: Bluetooth MIDI supported since Android 6; USB MIDI via OTG adapters.
  JUCE's Android MIDI handling is less mature than iOS — expect this to be the more
  likely source of platform-specific bugs; test here first when MIDI issues are reported.
- App target type is a JUCE **standalone GUI Application** (`juce_add_gui_app`), not an
  audio plugin client. A DAW-plugin (AUv3/VST3) target is a possible future addition that
  would reuse the Core Engine module — keep that reuse path open.

## Build System

The project is built with **CMake**, not the Projucer — one `CMakeLists.txt` per module,
with JUCE pulled in by `cmake/GetJUCE.cmake` (a local `-DJUCE_PATH=...`, a sibling
checkout, or `FetchContent` as a fallback). See `README.md` for the commands.

Two rules the build itself enforces:

- **The Core Engine target links no JUCE.** `cmake -DJAZZ_BUILD_APP=OFF` builds the engine
  and its tests on a machine with no JUCE and no GUI libraries at all. If a change makes
  that configuration fail to build, the layering has been broken.
- **Tests live with the engine, not the UI.** Anything worth testing belongs in
  `modules/core_engine`; `tests/` links only against it and uses the small harness in
  `tests/TestFramework.h` (no third-party test dependency).

There is a third platform shell besides `app/`: `web/` compiles the engine to WebAssembly
with Emscripten (`web/build.sh`) for a browser demo of the engine. It exists as a standing
check that the engine stays portable - if a change makes `web/build.sh` fail, something
platform-specific has leaked into the Core Engine. Like `app/`, it must contain no theory.

`.github/workflows/ci.yml` enforces the first of those rules on every push: one job
configures with `-DJAZZ_BUILD_APP=OFF` on a runner with no GUI packages installed at all,
the other installs the JUCE dependencies and builds the app. That first job failing means
the layering broke, not that the runner is short a package — read it that way before
reaching for an `apt-get`. The Emscripten build is *not* in CI, so run `web/build.sh`
yourself after touching the engine; it is the check that nothing platform-specific crept
in, and nothing else will catch that for you.

Android is the exception to CMake-everywhere: JUCE's CMake support does not cover Android,
so that target needs the Projucer/Gradle exporter over the same source tree. Nothing in
the source layout assumes either build system.

## Feature Modules — What Is Built

`README.md` describes the behaviour in detail; this is the map from feature to code, and
the line between what exists and what does not. Do not re-plan something in the first list.

### Reharmonization Assistant — built
- Build a progression by hand or import one. `ChartFormats` reads both iReal Pro link
  formats, the `.html` iReal Pro shares, and the text of a PDF lead sheet; it writes an
  iReal Pro link back, and the browser shell prints a lead sheet.
- Click a measure → suggested scale plus every valid alternate, each with a rationale
  (`ScaleSuggester`, over a 29-shape catalogue in `Scale.cpp`).
- Substitutions grouped by family, each carrying a difficulty tag (safe / advanced /
  risky), a style tag, and a ranking by guide-tone voice leading (`Reharmonizer`).
- Whole-tune reharmonisation in six named plans, each bar decided against the chart as it
  stands.

### Real-Time Chord/Voicing Analyzer — built
- Classifies what was played against the symbol, allowing for voicing type, and explains
  what is missing, outside, clashing or muddy (`VoicingAnalyzer`).
- Offers idiomatic voicings to play — the "sentence starters" — built from the structures
  players actually learn (`Voicing.cpp`), in plain and tension-rich forms.
- Names a voicing with no chart and no expected chord (`ChordIdentifier`).
- Reads a played voicing back as a *substitution* when it spells one, rather than as a
  broken version of the written chord.

### Both shells, and why they look alike
The JUCE app and the browser page carry the same features and very nearly the same
screen: one palette in `theme`, one lead sheet in `ChartView`, one practice menu, one
dock. Where they differ, it is because a shell cannot do the thing, not because someone
styled it differently — so a change to how the app *looks* usually belongs in
`modules/shared_ui`, where both would get it, and never in `app/`.

Two things are the page's alone, both for want of a library rather than a decision:
reading a PDF, and printing one. The engine's chart reader is shared; what the JUCE shell
lacks is a way to get text out of a PDF. If you add one, the reader is already there.

### Where the browser page is published
`.github/workflows/pages.yml` builds the wasm and deploys `web/` to GitHub Pages on every
push to the default branch. That is the demo's real home, not an embedded page: **Web
MIDI** needs a permission an embedded frame generally cannot ask for, so a keyboard that
works on a served page does nothing inside one. Pages needs enabling once by hand
(Settings → Pages → Source: GitHub Actions); if the deploy step is failing while the
build step passes, that setting is the first thing to check.

### Not built — still genuinely open
- Voice-leading visualiser. `guideToneMotion()` is the primitive it would draw.
- Solo/improv feedback layer; personal voicing library; ear training; metronome /
  practice-loop; progress tracking. The analyser's per-voicing score and the feedback
  panel's session average are the hook the last of those would build on.
- MusicXML / MuseScore import. The page reader is format-agnostic enough to feed it.
- Reading and printing a PDF in the JUCE app, as above.

## Open Questions (Don't Assume — Ask)

- iReal Pro and PDF import both ship now. Is MusicXML/MuseScore import needed as well?
- How much of the reharm suggestion engine should be rule-based vs. data/ML-informed?
- Is the solo/improv feedback layer in POC scope or a post-POC addition?
- Is a future dense, DAW-style desktop layout worth designing for now, or deferred?

If work touches one of these, flag the ambiguity rather than silently picking a direction.

## Working Conventions

- Prefer expanding the Core Engine module with unit-testable, UI-agnostic logic first;
  wire it into UI components second.
- When adding a new UI component, default to one component with size-class-aware
  behavior rather than platform-conditional forks.
- Match existing JUCE idioms in the codebase (Projucer-generated project structure,
  `juce::Component` lifecycle, `resized()`/`paint()` conventions) rather than introducing
  new patterns.
- **A wider chord vocabulary is not a better one.** `ChordIdentifier` scores every name it
  knows against the notes, so each one added competes with all the rest — and a name no
  player writes still wins whenever it happens to account for every note. Adding the
  parser's full range once made the common answers worse: a complete `Asus4b9` beat the
  obvious rootless `C13`. The dominant tensions are *generated*, because that family is
  combinatorial and any hand-written list of it will be missing a real chord. Everything
  else is curated on purpose. Before adding a name, check what it costs the chords that
  already work — the test "a rootless thirteenth still beats a complete name nobody
  writes" is that trap nailed down, and a wider vocabulary that breaks it is a worse one.
- **A suggestion the analyser would reject is a bug, not a near miss.** Two invariants in
  `VoicingAnalyzerTests` hold the two halves of the app together: every voicing
  `idiomaticVoicings` offers must classify as the type it was offered for, and none may
  exceed what one hand can reach. Without the first, the app hands you a voicing and then
  marks it wrong; without the second, it hands you one nobody can play. Changing a shape
  table means re-running those, not just the tests for the shape you touched.
- **Paint and layout must not each do the arithmetic.** Several components here draw
  headings and lay out buttons down the same column, and when `paint` and `resized` each
  counted the heights themselves they drifted apart the first time a row changed size.
  Each of those now works its positions out once — `PracticeMenu::layout`,
  `MainComponent::frame` and the rest — and both read the result.
- **Check the narrow window before calling a layout done.** Everything that overlapped on
  a phone-sized window looked perfect on a desktop one: the sheet head, the chart tools
  and the dock's buttons all collided at 420px while being fine at 1200px.
  `JAZZ_UI_SIZE=420x860 JAZZ_UI_TOUCH=1` is two seconds of work and catches all of it.
- **Web MIDI can be tested without a MIDI keyboard.** Stub
  `navigator.requestMIDIAccess` in a copy of `web/index.html` before the page's own
  script tag, hand it a fake input whose `onmidimessage` you keep a reference to, and
  drive the page's real handler with raw bytes; report the outcome through
  `document.title` so headless Chromium's `--dump-dom` can read it. That is how the
  sustain pedal was verified on the page — including the control case of the same broken
  chord without the pedal, which is what proves the pedal is doing the work. Keep the
  harness a generated copy, so the code under test is the shipped file unmodified.
- **A failing test is a question, not a chore.** Several here encoded bugs rather than
  behaviour — two asserted a chord printed a name that silently dropped a note, one
  asserted `F#maj9` should normalise to `Gb`. Work out whether the engine or the
  expectation is wrong before changing either.
