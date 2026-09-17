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

`docs/jazz_learning_app_design.pdf` is the original design doc. Treat it as the source of
truth for the *original* why - the scope that was set before any of this was built - and
not for how anything works now. Everything decided since is in this file and in
`README.md`, and where the two disagree the PDF is the older one: it predates the move to
a webview UI, solo practice, and most of what the engine does. This file is the source of
truth for how to work in the repo.

## Architecture — Read This Before Adding Code

The project is split into three layers as **separate modules**. This boundary is
intentional and load-bearing — do not blur it.

| Layer | Responsibility | May depend on |
|---|---|---|
| **Core Engine** | Chord parsing, reharmonization logic, scale suggestion, voicing analysis. Pure C++, no JUCE GUI classes. | Nothing platform-specific |
| **Engine API** (`modules/engine_api`) | The engine's answers as JSON - one wire format for every shell. Pure C++, no JUCE, no Emscripten. | Core Engine |
| **User Interface** (`web/`) | The single interface: lead sheet, keyboard, panels, dialogs. Served on the web, hosted by the app. | Engine API, as JSON |
| **Platform Shell** (`app/`) | A webview showing `web/`, plus MIDI I/O, the audio device, file reading, app lifecycle. | JUCE platform APIs |

**There is one UI and it is the web page.** The JUCE component UI was removed once the
webview carried everything; do not reintroduce a second implementation of a screen. A
change to how the app looks belongs in `web/index.html`, where the website gets it too.

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

There is one interface and it is `web/index.html` - the same file in the desktop app and
on the website. Not a shared component library: literally one document. So "responsive"
here means CSS, and every mechanism below is the page's own. This section used to describe
JUCE FlexBox, size classes and `juce::Component`s, which is what the UI was built from
before the webview carried everything; none of that survived the move, and a session
following it would rebuild exactly the thing the Architecture section says not to.

- **One breakpoint, at 760px.** Below it the sheet head centres, the bars grow, and every
  dialog becomes a bottom sheet rather than a floating panel. That is the whole of it -
  there are no compact / regular / expanded size classes. Add a second breakpoint only
  when something actually collides, which `JAZZ_UI_SIZE=430x860` or a 430px viewport is
  for.
- **Interaction affordances differ by input type, not by platform**, and the page gets
  that from the same rule rather than from a decision: the scale picker is a floating
  dialog where there is room and a bottom sheet where there is not, and it is one dialog
  either way. Do not write a second version of a screen for touch.
- **Mode-specific anything is `data-mode` plus `applyMode`**, never a second component -
  see *Both modes* below. The same goes for anything that differs between the two shells:
  `onTheWeb`, not a fork.
- **The on-screen keyboard is a first-class input method, not a fallback.** Two octaves by
  default; a connected MIDI keyboard widens it to four, and a note played outside that
  widens it further, so a two-handed voicing is never partly off the end. There is no
  octave-shift control - the widening replaced the need for one. In chord practice the
  keys latch, so fingers landing one after another add up to one voicing with no
  multi-touch event handling anywhere; in solo practice they do not latch, because a line
  is played rather than held.
- **Tap targets are sized for touch at every width**, not only below the breakpoint, so
  there is no "touch mode" to get out of step with.

Do not introduce a second, platform-specific UI layer - a JUCE screen, a native sheet, a
separate stylesheet for mobile. That was a deliberate scope decision, and the JUCE
component UI was removed to honour it.

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

`web/` is two things at once, and it helps to keep them apart. It holds **the UI** - the
one page both shells show - and it holds a **second transport to the engine**: Emscripten
compiles the engine to WebAssembly (`web/build.sh`) so a browser can reach it without the
native build. The second of those is also a standing check that the engine stays portable
- if a change makes `web/build.sh` fail, something platform-specific has leaked into the
Core Engine. Like `app/`, neither half may contain any theory.

`.github/workflows/ci.yml` enforces the first of those rules on every push: one job
configures with `-DJAZZ_BUILD_APP=OFF` on a runner with no GUI packages installed at all,
the other installs the JUCE dependencies and builds the app. That first job failing means
the layering broke, not that the runner is short a package — read it that way before
reaching for an `apt-get`. It also asks the suite how many tests it has and checks that
against the number quoted in `README.md` and in the page's colophon, because that number
had gone stale twice by the time anyone noticed.

`.github/workflows/pages.yml` is where the Emscripten build actually runs, so a change
that breaks the engine's portability fails there rather than nowhere. It builds the wasm,
then drives the built page in a real browser (`web/smoke-test.mjs`) before the deploy
step, so a page that does not boot fails the job instead of reaching the site. Run the
smoke test yourself after touching the page - `./web/build.sh out && cp web/index.html
web/sw.js out && node web/smoke-test.mjs out` - because a build that passes has never been
the thing that goes wrong here.

Android is the exception to CMake-everywhere: JUCE's CMake support does not cover Android,
so that target needs the Projucer/Gradle exporter over the same source tree. Nothing in
the source layout assumes either build system.

## Feature Modules — What Is Built

`README.md` describes the behaviour in detail; this is the map from feature to code, and
the line between what exists and what does not. Do not re-plan something in the first list.

The app has **two modes over one chart**, so this is organised the same way: what chord
practice is, what solo practice is, and what the two share. A change that belongs to both
belongs in the third section, and almost every mistake made here so far was putting
something in one mode that the other quietly needed too.

## Chord practice — built

The mode the app opens in. It asks whether the voicing you played says what the bar says.

### Reharmonization Assistant
- Build a progression by hand or import one. `ChartFormats` reads both iReal Pro link
  formats, the `.html` iReal Pro shares, and the text of a PDF lead sheet; it writes an
  iReal Pro link back, and the browser shell prints a lead sheet.
- Click a measure twice → every substitution that could stand in its place, grouped by
  family, each carrying a difficulty tag (safe / advanced / risky), a style tag, and a
  ranking by guide-tone voice leading (`Reharmonizer`).
- Whole-tune reharmonisation in six named plans, each bar decided against the chart as it
  stands.

### Real-Time Chord/Voicing Analyzer
- Classifies what was played against the symbol, allowing for voicing type, and explains
  what is missing, outside, clashing or muddy (`VoicingAnalyzer`).
- Offers idiomatic voicings to play — the "sentence starters" — built from the structures
  players actually learn (`Voicing.cpp`), in plain and tension-rich forms.
- Names a voicing with no chart and no expected chord (`ChordIdentifier`).
- Reads a played voicing back as a *substitution* when it spells one, rather than as a
  broken version of the written chord.
- The keys **latch**: a voicing is something you hold, and `state.heldNotes` is what the
  analyser is handed.

## Solo practice — built

The same chart, the same notes, a different question: where does each one sit.
`LineAnalyzer` is the whole of the theory; `read()` is pure, and a *take* is that with a
memory and a window.

**The reasoning behind every rule below - the counts, the worked examples and the bugs
that produced them - is in [`docs/SOLO_PRACTICE.md`](docs/SOLO_PRACTICE.md). Read it
before changing any of this.** What follows is the short form, and it is here rather than
there because these are the ones that break something when a session has not read them.

- **A note outside the harmony is open, not wrong, until the window has passed it.**
  `play()` returns `NoteColour::unresolved` for it, and it becomes `approach` or `outside`
  once, for good - usually on the very next note. `read()`, being pure and seeing one
  note, returns `outside`, because one note has no line around it to be waiting on.
- **`canStillBeReached()` decides when**, and the rule is "could any pattern still promote
  this", never "have N notes gone by".
- **Nothing is graded on an open note.** `score()` reads `LineStats::settled()`. No
  percentage, strip or tally may treat `unresolved` as a verdict.
- **The instant feedback names the resolution, not a mistake.** `LineNote::wantsToReach`.
  The bad news is delivered late instead, by `strandedByLastNote()`, which is the earliest
  it is honestly available.
- **`resolvedByLastNote()` and `strandedByLastNote()` are not alternatives.** One note can
  resolve one open note and strand another in the same breath; a shell must read both,
  every time.
- **The window does not stop at the barline**, so a promotion can change a bar already
  left behind. `soloPlayNote` returns a `bars` array of everything that moved - a shell
  reading only `bar` leaves the previous one drawing numbers that stopped being true.
- **Nothing that has settled is ever revisited**, and **`endTake()` closes whatever is
  still open** as outside.
- **`ApproachKind` (chromatic / passing / enclosure) is not a tier.** All three land and
  count identically; only the key and the chip tell them apart. The rules are tried
  most-specific-first because a note can honestly answer to more than one.
- **The score is the only judgement in the engine**, its constants are named at the top of
  `LineAnalyzer.cpp`, and **nothing calls a note wrong** - a reading of a bar is not a mark
  for a player, and the page's wording has to keep saying so.
- **A line's shape does not touch the score.** Bars that never left the chord, unresolved
  leaps and a narrow range produce words, never points.
- **The take is the one stateful corner of `jazz::api`** - `soloStartTake`, `soloSetBar`,
  `soloPlayNote`, `soloEndTake` - and deliberately so.
- **Scale styles come from the engine** (`scaleStyles()`), never from a list in a shell,
  and the forgiveness lives in `Options::chosenScale` rather than in reading against every
  scale that fits.
- **Static, with no clock.** `LineAnalyzer` has no time in it, which is what makes it
  testable; a transport drives `setTarget()` from outside and needs nothing else from the
  engine. **"In time" is a door with nothing behind it** and must not be made to do
  something approximate - build the transport with the metronome and the practice loop, or
  leave the door shut.

## Both modes — one page, one chart

- **A mode is not a screen.** `state.mode` flips and the chart, the keyboard, the MIDI
  connection and the audio device all stay exactly where they are; what changes is where a
  played note goes and what the dock says back. The selected bar is one pointer shared by
  both.
- **Everything that swaps with the mode swaps the same way**: `data-mode` on the element
  and `applyMode` hides the other one. The bar dialog, the cheat sheet, the masthead, the
  hint above the chart and the Practice menu's groups are all that one mechanism, so
  adding a mode-specific anything is a markup attribute rather than code. The one variant
  is `.mode-stack`: a wrapper putting both wordings in a single grid cell, where
  `applyMode` swaps them by `visibility` so the one not showing keeps its space. That is
  for the two with the chart underneath them - the masthead's lede and the hint - because
  the two wordings are different lengths and hiding one outright let the whole chart jump
  by a line. Keep a stacked pair roughly the same length anyway; the stack stops the jump,
  matching lengths stops the gap.
- **The bar dialog belongs to whichever mode is on.** Scales in solo practice,
  substitutions in chord practice, never both: they answer different questions - what to
  play over this bar, and what this bar should be - and showing both meant every visit
  opened with a choice nobody asked for.
- **Both directions let go of the keys.** A voicing carried into solo practice is a chord
  nothing over there will ever read, and a line carried back into chord practice would be
  analysed as a voicing nobody played. `applyMode` clears held notes, the pedal's held
  notes and any key still lit for an open note, in either direction.
- **The mode changes the light, not the meaning.** `body.soloing` redefines the palette
  tokens - paper down a stop and cooler, the rose accent to slate - and every rule in the
  sheet already reads those, so the whole page restyles without a single component being
  named. Two things are deliberately fixed:
    - **The colours that mean something do not move.** Sage, gold, green, teal and rust
      say chord tone, scale tone, approach note, enclosure and outside in both modes, and
      the mode accent is picked to stay clear of all of them. Approach was a lightened
      rust once, on the reasoning that the note is outside by pitch; that was the wrong
      thing to say. An approach note is the line working, and a colour on the way to the
      one that means "did not land" reads as a near miss. An enclosure earns its own
      colour on the key and the chip, where one note's story is told, and shares the
      approach tier's colour on the bar strip, where four tiers are counted - it is not a
      fifth tier and must not become one.
    - **The keys are left out of the fade.** They answer a finger, and a key that took
      280ms to look pressed would be worse at the job it does every second.
  A colour written as `rgba(...)` rather than a token is a colour that will not follow the
  mode - that is what `--wash` exists for.
- **Late feedback has to be loud, or it is not feedback.** Solo practice's verdict is
  about a note one or two behind the one under the player's fingers, so a sentence
  appended to a soft grey line went unread - which was reported, and fairly. The
  `.open-note` chip sits in one fixed place in the dock and is the colour of the answer,
  and **the key the open note was played on stays lit** rather than fading like every
  other note, until the line says what it was. The keyboard is where the player is
  looking, so that is where the answer goes. `.solo-live` reserves two lines' height for
  the same reason `.feedback` does: a dock that grew and shrank under the keyboard would
  be worse than the thing it is pointing out.
- **The cheat sheet is per mode, and per first visit.** One "seen" flag each, because they
  explain different pages; the chords flag keeps its original key so a returning visitor
  is not shown that half again. The `?` names the mode it will explain.

### How the app and the page share one interface
`app/src/WebUi.cpp` is the whole of it. The page is written to a file at startup and
loaded into a `WebBrowserComponent`; two event channels carry everything else. The page
asks for the engine, for sound and for a file; the shell pushes MIDI notes, the sustain
pedal and device status back. Nothing else may cross that bridge - a rule on one side and
a drawing on the other is how the two shells drifted apart last time.

Engine calls are **asynchronous** in the app and synchronous on the web, which is why
every call site awaits `call()`. Awaiting a plain value costs nothing, so one shape
serves both; an engine call made around that funnel works in the browser and fails
silently in the app.

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

## Not built — still genuinely open
- Voice-leading visualiser. `guideToneMotion()` is the primitive it would draw.
- Personal voicing library; ear training; metronome / practice-loop; progress tracking.
  The analyser's per-voicing score, the feedback panel's session average and now a take's
  own numbers are the hook the last of those would build on.
- **Licks.** Solo mode's "Which scale?" is the scale half of "show me one"; suggesting a
  *line* to play over a bar is a separate feature needing generated or curated patterns,
  rhythm and register - deliberately not started.
- **A tempo-driven solo transport**, which is the same feature as the metronome /
  practice loop and should arrive with it. Solo practice's Practice menu already has the
  switch for it, wired to a note saying it is not built.
- MusicXML / MuseScore import. The page reader is format-agnostic enough to feed it.
- Reading and printing a PDF in the JUCE app, as above.

## Open Questions (Don't Assume — Ask)

- iReal Pro and PDF import both ship now. Is MusicXML/MuseScore import needed as well?
- How much of the reharm suggestion engine should be rule-based vs. data/ML-informed?
- Is a future dense, DAW-style desktop layout worth designing for now, or deferred?

If work touches one of these, flag the ambiguity rather than silently picking a direction.

## Working Conventions

- Prefer expanding the Core Engine module with unit-testable, UI-agnostic logic first;
  wire it into UI components second.
- When adding a new UI component, default to one component with size-class-aware
  behavior rather than platform-conditional forks.
- **JUCE idioms apply to the shell, and the shell is small.** `WebUi` is the only
  `juce::Component` with a layout worth the name - it holds the webview and little else -
  and `MidiDeviceInput` is the only other JUCE class with any UI, for the Bluetooth
  pairing sheet. Match JUCE's conventions there. Everywhere else "the UI" means
  `web/index.html`, and a new `resized()` or `paint()` is a sign something is being built
  in the wrong layer.
- **A wider chord vocabulary is not a better one.** `ChordIdentifier` scores every name it
  knows against the notes, so each one added competes with all the rest — and a name no
  player writes still wins whenever it happens to account for every note. Adding the
  parser's full range once made the common answers worse: a complete `Asus4b9` beat the
  obvious rootless `C13`. The dominant tensions are *generated*, because that family is
  combinatorial and any hand-written list of it will be missing a real chord. Everything
  else is curated on purpose. Before adding a name, check what it costs the chords that
  already work — the test "a rootless thirteenth still beats a complete name nobody
  writes" is that trap nailed down, and a wider vocabulary that breaks it is a worse one.
- **Which scales belong together is theory, so the engine holds the list.** The soloing
  styles the menu offers come from `scaleStyles()` over the wire, not from options
  written into the page. A second copy of that list in a shell is a second copy to go
  stale the first time a scale is added to the catalogue.
- **Before widening a rule in the name of being generous, count what it leaves.** Solo
  practice's "read against every scale that fits" is the worked example - see *Reading one
  note* above, where being maximally forgiving turned out to leave nothing outside
  anything and collapse the feature.
- **A suggestion the analyser would reject is a bug, not a near miss.** Two invariants in
  `VoicingAnalyzerTests` hold the two halves of the app together: every voicing
  `idiomaticVoicings` offers must classify as the type it was offered for, and none may
  exceed what one hand can reach. Without the first, the app hands you a voicing and then
  marks it wrong; without the second, it hands you one nobody can play. Changing a shape
  table means re-running those, not just the tests for the shape you touched.
- **Check the narrow window before calling a layout done.** Everything that overlapped on
  a phone-sized window looked perfect on a desktop one: the sheet head, the chart tools
  and the dock's buttons all collided at 420px while being fine at 1200px.
  `JAZZ_UI_SIZE=430x860` on the app, or a 430px viewport in a browser, is two seconds of
  work and catches all of it. The narrow layout is the page's own CSS - below 760px the
  sheet head centres and every dialog becomes a bottom sheet - so checking either shell
  checks both.
- **Web MIDI can be tested without a MIDI keyboard.** Stub
  `navigator.requestMIDIAccess` in a copy of `web/index.html` before the page's own
  script tag, hand it a fake input whose `onmidimessage` you keep a reference to, and
  drive the page's real handler with raw bytes; report the outcome through
  `document.title` so headless Chromium's `--dump-dom` can read it. That is how the
  sustain pedal was verified on the page — including the control case of the same broken
  chord without the pedal, which is what proves the pedal is doing the work. Keep the
  harness a generated copy, so the code under test is the shipped file unmodified.
- **One page, two hosts: anything that suits only one has to say so.** The served page
  and the app are the same file, so a feature added for the browser lands in the app too,
  where it may be wrong or may hang. `onTheWeb` is the one test that separates them, and
  four things already sit behind it: the webfonts, the offline worker, the shareable link
  (a link to a file in the app's temp directory opens on nobody else's machine) and the
  "this browser has no MIDI" message (the app has real devices and pushes their real
  status). Before adding a fifth, ask which host it is for - and if the answer is both,
  check that the app's answer is not merely the browser's answer failing quietly.
- **The page and the engine are fetched at the same version, and must stay that way.**
  `web/index.html` asks for `jazz-engine.js?v=<build stamp>`. Without it a browser holding
  a cached engine from the last deploy pairs it with a fresh page, the page calls something
  the old engine does not export, `ccall` throws, `call()` turns that into `{ok:false}` and
  the feature is simply *absent* - an empty menu, with the engine chip still saying ready.
  That shipped once. The worker matches and stores with `ignoreSearch` so the query does
  not cost the offline copy, and `loadScaleStyles` says the engine is out of date rather
  than showing nothing. When adding a call the engine did not have last release, remember
  the failure mode is silence.
- **The app's UI must never wait on the network.** The page is the interface, and a
  webview with no route out does not degrade gracefully: a render-blocking stylesheet
  that never arrives leaves the whole interface *invisible* - backgrounds paint, no text
  ever does. The webfonts are therefore requested by script and only when the page is
  served over the web. A `<link rel="stylesheet">` with no `href` is not a safe parking
  spot either; it counts as a stylesheet still on its way, and a pending stylesheet stops
  every script after it from running.
- **A service worker never sees the visit that registered it.** `web/sw.js` fetches the
  page and the engine itself on install, and has to: registration happens on `load`, by
  which time both have already been fetched, so nothing passes through the worker and a
  first visit leaves the cache empty. What makes that expensive to find is that it looks
  like it works - the browser's own HTTP cache will answer a reload through the worker's
  `fetch` often enough for an offline test to pass. Assert on the cache's contents, not
  only on the reload. (And `page.waitForFunction` will not wait for an async predicate:
  it takes the returned promise as the value, and a promise is truthy, so the wait passes
  on its first tick. Poll from the test process instead.)
- **The page is loaded from a file, not JUCE's resource provider.** The provider is the
  tidier mechanism and it does not reliably deliver a document this size on Linux - the
  page arrives, its CSS paints, and its script never runs, perhaps one time in ten. The
  same page loads perfectly from a URL, so `WebUi` writes it into the temp directory each
  launch and points the webview there. If you try the provider again, verify it ten times,
  not once.
- **A failing test is a question, not a chore.** Several here encoded bugs rather than
  behaviour — two asserted a chord printed a name that silently dropped a note, one
  asserted `F#maj9` should normalise to `Gb`. Work out whether the engine or the
  expectation is wrong before changing either.
