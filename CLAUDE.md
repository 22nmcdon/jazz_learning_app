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
  should produce the same event shape.
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

Android is the exception to CMake-everywhere: JUCE's CMake support does not cover Android,
so that target needs the Projucer/Gradle exporter over the same source tree. Nothing in
the source layout assumes either build system.

## Feature Modules (Reference)

### Reharmonization Assistant
- Create progression manually or import (iReal Pro; MusicXML/MuseScore as stretch goal).
- Tap/click a measure → panel with suggested scale + dropdown/bottom-sheet of all valid
  alternate scales.
- Planned additions: voice-leading visualizer, style presets (bebop / modal / quartal /
  Brazilian), difficulty tagging (safe vs. advanced substitutions), export back to iReal
  Pro or PDF lead sheet.

### Real-Time Chord/Voicing Analyzer
- Analyzes a played chord against the expected chord symbol, accounting for voicing type
  (root-position solo piano, two-handed no-root, left-hand-only no-root, etc.), with
  improvement suggestions.
- Planned additions: solo/improv feedback layer (flag notes outside the target scale,
  framed constructively), personal voicing library builder, idiomatic voicing "sentence
  starters" for weak chord types.

### Supporting Modules (Round Out Practice Loop)
- Ear training tied to the loaded chart
- Metronome / practice-loop mode integrated with the analyzer
- Progress tracking (voicing accuracy, weakest chord types, reharm styles explored)

## Open Questions (Don't Assume — Ask)

- Is iReal Pro import sufficient for the POC, or is MusicXML/MuseScore import needed now?
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
