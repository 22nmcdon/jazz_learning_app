# CLAUDE.md

Guidance for Claude Code (or any Claude instance) working in this repository.

**This file is the rules; `docs/` holds the arguments.** Solo practice is in
`docs/SOLO_PRACTICE.md`, comping in `docs/COMPING.md`, the rhythm grid they
share in `docs/RHYTHM.md`, the practice record in `docs/PROGRESS.md`, the
typefaces in `docs/BRANDING.md`. Read the relevant one before touching
`LineAnalyzer`, `Comping.h`/`compPlan()`, `Rhythm.h` or `PracticeLog.h`. For
**what the app does and how each feature behaves**, see `README.md`. For what
is **left** to do, see `docs/HANDOFF.md`; for why a rule below is the rule,
when the reasoning belongs to no single feature, see `docs/DECISIONS.md`.

Every session loads this file, so it earns its length by being the part you
cannot look up afterwards: a rule, the one sentence saying what breaks without
it, and where the reasoning lives. Reasoning that belongs to one feature goes
to that feature's doc.

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

**The wire is four files, one per question it answers** — `ChartApi.cpp`,
`VoicingApi.cpp`, `CompingApi.cpp`, `SoloApi.cpp` — behind one unchanged public
header. `src/Wire.h` holds only what **two callers in two different files**
need; a helper one domain uses stays in that domain's file, beside the call
that wants it.

## UI Conventions

One page (`web/index.html`) serves both desktop and mobile — "responsive"
means CSS, not per-platform builds.

- **Every pixel number below was measured, and re-measured after the doctype
  landed.** The page rendered in quirks mode for its whole life; the tags are
  there now, and the only numbers that moved were `.chart-bar` (19px → 26px at
  every width) and therefore the sheet's top edge, 7px lower. **A width set with
  Playwright's `viewport` is a CSS-pixel width**, which is exactly the case a
  missing viewport meta does not break — so four checks run in an `isMobile`
  context, and those are the ones that can tell a real phone from a narrow
  window.
- **The page is a frame, not a document.** Three zones: `.top-bar` at its own
  height, `<main class="chart-zone">` taking everything left and scrolling
  *itself*, and `.dock` at the bottom. `body` is a flex column at `100dvh`.
  Laid out as a document the chart gets whatever is left over and loses: four
  bars of twelve were readable at 1280×860 and **none at all** at 430×860.
  Anything added to the top bar or the dock is taken out of the chart.
- **The top bar has room for one more labelled control, and does not have it
  spare.** A labelled button in `.top-bar-right` costs **38px of chart** at
  1024px — a laptop, not an edge case — even with a three-letter label. So the
  practice-record button is a glyph with its name on `aria-label`. Before
  putting anything else there: something has to leave first, and **390px is not
  the width that catches it**, because `.top-bar-right` is already four lines
  deep there. Measure bar height with the control and without it, at a narrow
  width *and* at 1024.
- **The row above the chart is where a control goes instead, and it is free.**
  `.chart-bar` exists either way and is the same height whatever is in it, so a
  button hung from it (`.chart-bar-end`, right-aligned) costs the chart **zero**
  — measured at 390, 900, 1024 and 1440 with the sheet's top edge unmoved. What
  it does cost is that those buttons scroll away with a long chart, which is
  right for what you set the room up with and wrong for anything you reach for
  mid-take; those still belong on the transport strip.
  **The check measures `.chart-bar`, never `.chart-zone`.** The zone is the flex
  child that takes whatever the window has left, so it never changes size and
  measuring it asks nothing — a negative control proved exactly that.
- **The chart's head is in the top bar**, not in the sheet: in the sheet it
  scrolled away with the music, so the one thing naming what you are playing
  left the screen as soon as you played past the first line.
- **The top bar's second row is the transport strip**, holding what a player
  reaches for while playing: Static / In time, the metre and tempo (`#timeSig`,
  `#tempo`), the take button and the beat dots. What a take is *set up* with —
  count-in, loop, ramp, reharmonise-as-you-go — is one popover off it
  (`#transportButton`). **Its row count must not depend on what is showing.**
  The two groups (`.transport-clock`, `.transport-take`) are each one row at any
  width and each reserve that row empty; left to wrap freely the strip is one,
  two or three rows deep depending on which corner of mode × In time you are in,
  and every one of those moves the top of the chart. A check measures the
  chart's top edge in both settings at two sizes and fails if it moves.
- **A reserved height is a pixel count, so it holds or fails to hold *per
  typeface*.** The page renders in two by design (`docs/BRANDING.md`). Every
  control on the strip is a button or form control whose height comes from
  `line-height: normal` — the font's own metrics — so each is given an explicit
  `line-height` (a `height` for the select, which does not size from one) and
  the reservation is sized against that arithmetic. Sized against one face
  instead it was 29px against controls that are 37–39 in the other, and shipped
  a chart that moved while every local run said otherwise. **Measuring a layout
  on a machine that cannot reach Google Fonts measures the wrong page** — so the
  check runs a second pass with `ascent-override`/`descent-override` forcing
  taller metrics, which needs no network.
- **`#menuPanel` is the machine, and it is three sections**: Sound, MIDI
  keyboard, About — what the app is running on, not what you are practising.
  Below 760px it is a bottom sheet, which is why it has a `.menu-head` close
  row: at that width it covers the button that would otherwise close it.
  **The line to check before adding a section is about the machine versus about
  the music.**
- **Five popovers, each one question asked where it applies.** *Practice*
  (`#menuPanel`, top bar) — the machine. *Take setup* (`#transportPanel`,
  transport strip) — this take. *Chart* (`#chartPanel`, left of the chart's row)
  — which tune. *Voicings* / *Scales* (`#readingPanel`, right of that row) —
  what the page is reading you against, under **one** button whose label follows
  the mode, because the two are never both on offer and a button that changed
  what it opened would be a trap. *The band* (`#bandPanel`, beside it) — who is
  playing with you.
  **A panel is one question, and its button sits where that question is asked.**
  A new one names its question and says why the row it is on is the row it
  belongs to; something with no question of its own is a section of an existing
  panel, not a sixth.
- **The chart is justified into its zone, by `layOutChart()`.** Slack is given
  away in two goes: the **gaps between systems grow first**, capped at
  `MAX_EXTRA_GAP`, because slack spread through the chart reads as breathing
  room while the same slack at the end reads as a void — then **what is left
  becomes symmetric padding on `.sheet`**, which is the only thing that helps a
  tune of one system. When the chart is taller than the zone this does nothing
  and the zone scrolls as before. It measures with both custom properties
  cleared, or it measures its own last answer.
  **It re-runs on four things and needs all four**: after `renderSheet()`, when
  `#editToggle` shows or hides the editor, a `ResizeObserver` on `.chart-zone`
  (the page's only one — a `resize` listener would miss the dock changing height
  on a mode switch), and `document.fonts.ready`, since the webfont changes every
  bar's height while leaving the zone's alone. Print overrides both properties
  with `!important`, because a value set from script otherwise beats the
  stylesheet.
- **Viewport-relative heights are `dvh`, never `vh`.** On a phone `vh` is the
  window with the browser's own chrome collapsed, and the part that goes under
  it is the bottom of the frame — the dock.
- **Breakpoints: 760px for the page, and two scoped to the cheat sheet.**
  `max-width: 760px` is the page's own and still the only one that lays the page
  out differently. `#helpDialog` adds `min-width: 700px` (two columns) and
  `max-height: 700px` (tighter spacing on a short window) — a *height* query,
  the first here. No compact/regular/expanded size classes. Add another only
  when something actually collides, and say in the commit message what collided
  and at what size. Test with `JAZZ_UI_SIZE=430x860` or a 430px viewport — and
  note that neither of the help dialog's breakpoints is exercised at that size.
- **Differences are by input type, not platform** (e.g. the scale picker is a
  dialog or a bottom sheet depending on room, never two implementations).
- **Anything mode- or platform-specific is `data-mode`/`onTheWeb` + a CSS/JS
  branch — never a second component.** Both modes have a **Static / In time**
  setting meaning the same thing in each: the same question with a clock behind
  it, and readings that are silent without one. Reach for that before reaching
  for a third mode, a second keyboard or a second dock.
- **The bar dialog has three panels and shows two.** Scales (solo),
  Reharmonise (chord practice) and **This bar's take**, which appears only when
  the last take covered that bar. The tab strip is hidden whenever there is only
  one panel to show. The other mode's tab is *hidden*, not disabled: it is a
  question this mode is not about.
- **The cheat sheet is five sections and never scrolls.** `#helpDialog` has its
  own tab strip — Chart, Playing, Reading, In time, Band. Sections that mean the
  same in both modes are written **once** and shown in both; only what genuinely
  differs carries `data-mode`, on the individual `<li>` rather than on the list.
  An entry is a bold lead you can scan plus a quieter detail. A check measures
  every section in both modes at two sizes and fails if any has to scroll — so a
  section that grows has to be split, not allowed to overflow.
- **The on-screen keyboard is first-class**, not a fallback: 2 octaves by
  default, widens with a connected MIDI keyboard or an out-of-range note. No
  octave-shift control.
- Do not introduce a second platform-specific UI layer (JUCE screen, native
  sheet, separate mobile stylesheet) — this was a deliberate scope decision.

## Input Scope

In scope: MIDI keyboard (USB/Bluetooth), on-screen keyboard (touch/mouse), and
the **computer keyboard**, off by default and turned on under *Practice*.

**The computer keys are given `midiNoteOn`/`midiNoteOff` rather than a path of
their own** — that is the one-input-interface rule, not a convenience: a
computer keyboard is a MIDI keyboard without the hardware, so every mode branch,
the chord window, the pedal's deferred releases and the take all come along.
What it adds that no other input here has is a **chord without hardware**, since
a pointer plays one key at a time however fast it is clicked. Two rows, two
octaves, 48–72, matching the range the keyboard is drawn at, read from
`event.code` so the shape survives a non-QWERTY layout — and therefore **no
octave-shift binding**. `playingByKey()` is what the dock asks, never
`midi.connected`.

**Audio / pitch-detection input is out, and that is an answer rather than a
deferral.** This is a pianist's app the whole way down: `twoHandedReach`, guide
tones badged on *keys*, the low-interval limit, `compingVoicing`'s anchor sweep,
`chordSoFar()` reading an attack as a voicing. Every reader downstream assumes
several notes at once from a keyboard, and a keyboard already has MIDI. Pitch
detection buys monophonic input for a horn player, which is a different app. The
computer keyboard does not reopen this — a typed keyboard is still polyphonic
and still a keyboard, which is why it needed no new reader. What would reopen it
is **a non-keyboard audience**, not the DSP getting easier;
`app/CMakeLists.txt` says as much in the line that turns the microphone
permission off.

## Platform Notes

**What is actually built and run, and what is intent.** Keeping the difference
visible is the point — a claim nobody can check is worse than a gap somebody can
see.

- **Linux is the only platform anything is built on.** CI builds the engine
  without JUCE on a GUI-less runner and the full app on `ubuntu-latest`, and the
  page is driven in headless Chromium. Every measurement in this file was taken
  here.
- **macOS and Windows**: the shell is JUCE and nothing in it is Linux-only, so
  they are expected to build. Neither has been built or run. `WKWebView` in
  particular has never been loaded — see `docs/HANDOFF.md`, which also has it
  down as the strictest of the three webviews about `file://` subresources and
  the first thing to suspect if the app cannot read a PDF on a Mac.
- **iOS and Android have no build at all.** No Xcode project, no `.jucer`, no
  Gradle file. `MidiDeviceInput::showBluetoothPairingDialog()` is written, is
  the only pairing entry point either would need, and **has no caller** — which
  is why "Bluetooth MIDI works out of the box" is a claim this repository cannot
  support. `app/CMakeLists.txt` still requests the Bluetooth permission for it,
  deliberately.
- What is true about mobile MIDI when somebody does build it: iOS takes
  Bluetooth through JUCE and USB through a camera adapter; Android has had
  Bluetooth MIDI since 6 and USB over OTG, and JUCE's Android MIDI is the less
  mature of the two, so it is the one to test first. **That is received
  knowledge, not experience with this app.**
- Target is a JUCE **standalone GUI app** (`juce_add_gui_app`), not a plugin
  client. An AUv3/VST3 target reusing Core Engine is a possible future addition.

## Build System

CMake, not the Projucer — one `CMakeLists.txt` per module, JUCE pulled in by
`cmake/GetJUCE.cmake`. Commands are in `README.md`.

Two rules the build enforces:
- **Core Engine links no JUCE.** `cmake -DJAZZ_BUILD_APP=OFF` must build the
  engine and its tests with no JUCE/GUI libraries present. If a change breaks
  this, the layering has been broken.
- **Tests live with the engine** (`tests/`, against `modules/core_engine` and
  `modules/engine_api` only, using the harness in `tests/TestFramework.h` — no
  third-party test dep).

`web/` is both the UI and a second transport to the engine (Emscripten →
WebAssembly via `web/build.sh`). If `web/build.sh` fails, something
platform-specific has leaked into Core Engine.

CI (`.github/workflows/ci.yml`): one job builds with `-DJAZZ_BUILD_APP=OFF` on a
GUI-less runner (a failure here means broken layering, not a missing package —
check that before reaching for `apt-get`); another builds the full app; a third
runs `node web/unit-test.mjs`, the page's arithmetic, which needs no browser.

**The test count is generated, not typed.** `README.md` and the page's colophon
quote it; the suite is the only thing that knows it, and it went stale twice
when both were written by hand. `tools/test-count.sh` asks the built binary and
writes the number into both files; `--check` changes nothing and fails, and is
what CI runs. **After adding a test, run it** rather than editing either file.
It is also an error for a file to contain *no* count.

`.github/workflows/pages.yml` builds the wasm, deploys to GitHub Pages, and runs
`web/smoke-test.mjs` against the built page before deploy. Run it yourself after
touching the page: `./web/build.sh out && cp web/index.html web/sw.js out && node
web/smoke-test.mjs out` — a build that compiles has not been the thing that goes
wrong here.

Android would be the exception to CMake-everywhere, since JUCE's CMake support
does not cover it — the Projucer/Gradle exporter over the same source tree is
the route. **None of that exists here**, so this is the plan rather than a
description of the build.

## Feature Status

Behaviour is `README.md`'s job and the reasoning is each feature's doc. This is
status plus the one rule a contributor must not break, so a session doesn't
re-plan something finished or assume something unfinished is done.

- **Reharmonization Assistant, Real-Time Chord/Voicing Analyzer** — done.
  **A substitution carries three classifications on three different axes**:
  `SubstitutionFamily` (what the bar dialog groups by), `ReharmPlanKind` (the
  six whole-tune plans) and `ReharmStyle` (the vocabulary filter). Note that
  `ReharmPlanKind::modalColour` gets its effect by filtering on *family*, so the
  two "modal"s in that file are unrelated mechanisms sharing a word.
  **The style filter reaches two of the three reharm calls on purpose** — not
  `recogniseSubstitution`, because narrowing the pool there stops the app
  noticing substitutions the player actually found, and noticing is its whole
  job; and not the plans, for the family-versus-style reason above. See
  `Reharmonizer.h`.
- **Solo practice** (line analysis, take scoring, walking-bass reading) — done.
  Read `docs/SOLO_PRACTICE.md` before touching `LineAnalyzer`. The rules:
  **a chord in a line is read as a chord, and nothing new reads it** —
  `chordSoFar()` hands the attack to `VoicingAnalyzer` and `ChordIdentifier`
  rather than growing a second reader inside `LineAnalyzer`; it is read **when
  struck**, against the symbol that was on the stand at the time; and
  `saysTheChord` is not a pass mark. A take also carries a second number,
  `readLinePlacement` (`LinePlacement.h`), which is **not reachable from
  `score()`** — different file, style as an argument, never touches a
  `LineStats`. The grid both it and `lineFaults` read against is
  `subdivisionsFor`, one source, because the app teaching a figure and then
  marking a player for playing it is the failure the round trip exists to
  prevent.
- **Comping** — done, both halves: the band that plays under a soloist, and the
  exercise. The exercise lives in **chord practice's *In time***, not in a third
  mode; `state.mode` is still two-valued. Read `docs/COMPING.md` before touching
  any of it. The rules: the band's voicings are a **weighted draw and never a
  shell**, with weight deciding the draw and never the cost, and travel measured
  **per voice**; the **unseeded** `compingVoicing` overload answers "what would a
  comper play here", has one answer, and must stay unchanged; the page puts the
  **chorus** in the seed, which is what makes the second time round the form a
  second time; **the evaluator says nothing about duration and cannot**, which is
  the decision rather than an omission. A style can be **described rather than
  named** — a user-made style is data the shell hands over on every call, as
  **flat delimited text, never JSON**, because the engine has no JSON reader and
  must not grow one. **An unknown key falls back to the first style; a malformed
  description is an error.** `variation` is advised, never clamped. **A style's
  slots are the figure the band plays, never a fence around the player.**
- **The band's velocity is humanised in the page, not the engine**, seeded off
  `spread()` so a loop comes round the same. **What crosses to the app is a
  multiplier, never a level** — the two shells have their own idea of how loud an
  accompaniment is, and each keeps it.
- **Drums** — done. The one member of the band with no engine call: the page
  owns the pattern, each shell owns the sound. Written on the shared grid and
  swung through `beatsIntoBar`, so the ride and the comp agree about where the
  and of two is. The pattern follows the **metre**, not the comping style, and
  needs the clock.
- **Voice-leading visualiser** — done, **on the keys, not on the chart**.
  `voiceGuideTones()` takes the chord that is coming *and the notes just
  played*, and badges two keys with their degree. **Voiced, not named**: the
  octave is the half a chart cannot give you. The notes are **paired** to the
  tones so the hand moves least overall, and one note leads into both when there
  is no second one. A guide tone already under a finger keeps its badge,
  inverted. The page points at the next bar that says something **different**,
  not simply the next bar. `guideToneMotion` still answers it for two chord
  *symbols* and is untouched.
- **The page's layout** — done; the reasoning is in UI Conventions above. The
  rolling bar brings itself into view, which nothing on this page did for
  anything before.
- **Progress tracking** — done, and it is a record of *what was practised*
  rather than a scoreboard. See `docs/PROGRESS.md`. The rules: **it keeps no
  version of a mark** — the row has no field for one, the wire has its own
  score-free emitter rather than reusing `lineStatsJson`, and a check reads the
  rendered panel and fails on the word; **the engine learns nothing**, since
  `readPractice` and `readTuneProgress` are free functions over a vector the
  caller owns and `today` arrives as an argument; and a tune's own memory says
  nothing below three takes and names a hard bar only when it stands apart.
- **Reading a PDF** — done in **both** shells. pdf.js is vendored under
  `assets/`, the app compiles it in and `WebUi` writes it beside the page —
  which is why the page is written to a temp **folder** now rather than a temp
  file. The shell sends a PDF's bytes over the bridge and the page's own
  `readPdf` does the rest, so there is one reader and not two. **Printing is
  still the browser's alone**, and the button is hidden in the app.
- **Personal voicing library** — done. A voicing is saved against **a chord
  quality, as semitone offsets from the root**, plus the register the shape sat
  in: offsets transpose, which is how `idiomaticVoicings` already thinks, and
  the register is part of the shape for the same reason `voiceGuideTones` gives.
  `voicingFromShape` picks the root octave by the anchor and walks the octaves
  rather than dividing, which is where the off-by-an-octave lives.
  **`Show me one` cycles yours before the engine's**, and yours are *not*
  filtered by the shape being practised: a shape you chose to keep is not a
  suggestion to be overruled.
  **`savedCollection` is the shelf, written once** — an array and a counter, ids
  that only go up, a cap, and dropping a record rather than migrating it.
- **Line suggestions** — done, and built around phrases. See
  `docs/SOLO_PRACTICE.md`. The rules: **`planPhrases` runs before any note is
  chosen**, and the note chooser walks what it produced — two passes, because
  pitch cannot see phrase shape and phrase shape does not need pitch. A
  `LineStyleDefinition` absorbed the scale style, so solo practice has one
  picker and not two; `scaleStyles()` is unchanged and still what the bar dialog
  reads against. **An approach and the note it lands on are one gesture**, and
  an approach note must be **outside the scale the take reads against** —
  `readingScaleFor` is public so the writer and the reader cannot disagree about
  which scale that is. **It commits to the colour, never to the gesture**: an
  `ApproachKind` on a `WrittenNote` is something the round trip refuses, because
  the analyser reads the gesture from the line *around* a note.
  **It quotes before it invents.** `LickCatalogue.h` holds twenty-three
  documented figures, each keyed on a **chord-sequence shape** and each carrying
  its `attribution` and `source`. A phrase is quoted whole or written note by
  note, never half of each; `lickShare` is how often a phrase *reaches* for a
  quote. Two rules before adding one: **a lick the research does not contain is
  a `composite` unless a real named practice is behind it**, and **a sweep only
  cross-checks what it plays** — a lick over harmony no sweep chart carries is a
  lick nothing checks. **Colours are computed, never authored**, and two tests
  cross-check the source: a note the transcriber calls a chord tone is never
  read as outside, and one it calls outside is never read as a chord tone.
  **`lineFaults` validates *generated* material only** — a documented device may
  be deliberately outside the scale; the register is still checked for every
  note. **A bar with two chords is written against the first**, in the writer
  *and* in the take; a test pins it, and the fix is the take model, not a
  `chordAt` argument. **An ornament is a rendering hint and never a position.**
  **The writer carries the analyser's vocabulary**: `LineWriterTests` asserts
  over twenty-four seeds that every note reads back as the colour it was written
  as, because otherwise the app hands you a line and then marks it wrong.
  In time, the line is a voice the transport books, through the same
  `beatsIntoBar`; it still refuses to sound over a take.
- **Nothing is left under "not built".** `docs/HANDOFF.md` carries loose ends
  only.
- **Closed, so nobody re-plans them**: ear training and MusicXML/MuseScore
  import are **not wanted** (decided, not deferred); audio input is out for the
  reason under *Input Scope*; the dense multi-column layout is answered under
  *Open Questions*.

## Critical Invariants

These aren't stylistic preferences — violating them breaks something in a way
that's easy to miss in review.

- **Every beat time is an origin plus a beat count at one spacing**, so anything
  that changes the spacing mid-roll must move the origin to the beat the change
  takes effect on — and must reset `transport.countInBeats`, which is a fact
  about *this roll* rather than about the count-in checkbox. The tempo ramp
  changes tempo only at a chorus boundary, and does it while beats are being
  *booked* rather than when one sounds, so every beat in the diary was written
  down at the tempo it will be played at.
- **Reharmonise as you play changes the chart on the *last* bar of a chorus,
  not at the top of the next one.** Reharmonising asks the engine and redraws,
  which is a turn of the event loop; the band is booked a bar ahead and
  synchronously, so at the boundary the first bar of the new chorus still plays
  the old chord. It restores `reharm.before` when the transport stops — an
  exercise, not an edit — and clears only the `reharmonised` marks it put there
  itself. **How much** (`reharm.amount`) and **how far** (`reharm.reach`) are
  separate questions, and a plan carries its own reach so the second is hidden
  for the whole-tune amount. The whole-tune amount applies a named plan to the
  chart *as it stands*, which is what makes it escalate. **In time with the
  exercise armed, `planDialog` chooses the take's plan instead of rewriting the
  chart.**
- **The engine has no clock and must never get one.** Time/position enters as
  data (a `BarPosition`, a beat+tick on the shared grid in `docs/RHYTHM.md`)
  that the shell computed from its own clock — never as something the engine
  infers about when a call arrived. This is what keeps `LineAnalyzer` and
  `compPlan()` testable.
  **A groove does not breach this.** `Groove.h` states how far a swung eighth is
  bent as a *relationship*, which the shell evaluates with the tempo it holds;
  `upbeatAt (groove, bpm)` is a pure function of two numbers the caller already
  has. Nothing there moves a tick, and the engine still writes every eighth
  at 12.
- **Swing is per-tune and tempo-dependent, and lives in one function.**
  `beatsIntoBar` on the page, which takes the spacing the bar was *booked* at
  rather than reading the live tempo. Its inverse `positionFromBeats` has to
  move with it, or notes come back onto the grid at a position nobody played.
  The click, the beat dots, the count-in and the ramp are all straight and must
  stay so: the whole page's timing rests on the click being
  `origin + index × spacing`, and the tempo-ramp check measures exactly that to
  0.01s.
- **A number needs a standard the player chose. Everything else is words.**
  It turns on one test — *is there a standard the player picked?* — and never on
  which mode is showing. **Both modes score placement, and it is the same rule
  applied twice rather than two rules**: a `CompStyleDefinition` is the standard
  for a comp, a `LineStyleDefinition` off solo practice's own picker is the
  standard for a line. **`score()` did not change and must not** — the placement
  is a *second* number, in `LinePlacement.h`, and the test that says the same
  notes score the same with and without positions is the evidence.
  **What is still words is everything a chosen standard does not state**: in a
  line style that is `startTicks`, `descending` and `usesApproaches`, and it is
  also R1, a chromatic on an accented beat, which is the engine's rule about its
  own generator rather than something the style put its name to. **The chord
  reading is the same rule one step further out**: a block chord in a line is
  named, never scored, and there is no number anywhere that `saysTheChord`
  moves. **A reading with no chosen standard behind it says nothing rather than
  nought** — `CompEvaluation::fit` and `LinePlacementReading::fit` are both
  `std::optional<int>` and cross the wire as `null`.
- **Single source of truth for anything on the wire.** `scaleStyles()`,
  `compStyles()` etc. come from the engine via `EngineApi`; never hardcode a
  copy in a shell — it will drift the first time the engine's catalogue changes.
  The same principle is why the rhythm grid has one doc, not three.
- **`VoicingAnalyzerTests` holds two invariants that must hold after any change
  to a voicing/shape table**: every voicing `idiomaticVoicings` offers must
  classify as the type it was offered for, and none may exceed what one hand can
  reach. Comping reuses these shapes — see `docs/COMPING.md`.
- **A wider chord vocabulary is not automatically better** — `ChordIdentifier`
  scores every known name against the notes played, so a new name competes with
  all the rest. Check what a new name costs the chords that already resolve
  correctly before adding it.
- **Nothing comes back unless you asked for it by name.** One pair of accessors
  (`remember`/`recall`, plus `rememberFlag`/`recallFlag`/`recallNumber`) is the
  only thing on the page that touches `localStorage`, under flat `jazz*` keys.
  What survives a reload is how the room is set up — tempo, metre, In time, the
  sound bank, the band, the comping style, the reharmonisation vocabulary, guide
  tones, the loop — plus the two shelves you filled on purpose: your saved tunes
  and your saved voicings. What must never survive **on its own** is the chart,
  a take, or anything mid-exercise.
  Three rules the recall keeps: it **makes no sound**; it **never overrides the
  chart** (a metre is remembered in the picker's handler, not in `setMetre`,
  which an import also calls); and it **validates every value**, since a store
  the page does not own can hold anything. A recalled loop goes back through
  `fillLoopRange()`.
  **A comping style you wrote is remembered too, and it is the one structured
  value here.** Its accessors (`rememberObject`/`recallObject`) are built *on*
  the one pair rather than beside it — there is still exactly one door to
  `localStorage`. It is **dropped rather than migrated** when it stops matching
  what the engine sends: the grid's own `ticksPerBeat` is stored beside it
  (derived, never a hand-raised version number), and the field list and the feel
  are both checked against the engine's *current* answer.
  **The line is asked-for versus assumed, not settings versus work.** `boot`
  puts no tune up, and `solo.take` and `comp.hits` still die with the page; a
  chart you deliberately saved and named is yours to open again by picking it. A
  check enforces exactly that: save a tune, reload, and the stand still holds
  the chart the page shipped with.
  **The practice record is remembered on the same terms**, and it is a record
  that you practised rather than a copy of what you played — a row is counts, a
  day number and two bitmasks, so nothing here could reconstruct a performance.
  It is **dropped a row at a time**; a row reaching the engine that its grammar
  refuses reads back as an error over the *whole* history rather than as one
  lost take. Both stores can be forgotten from the panel, separately.
- **Engine/page version mismatch has no symptom of its own, so the page checks
  for it.** A cached engine paired with a fresh page makes a feature simply
  *vanish* — empty menu, engine chip still saying ready. Two things answer that:
  `web/index.html` requests `jazz-engine.js?v=<build stamp>` so the pair is
  fetched together, and at boot it checks every call in `ENGINE_CALLS` against
  the engine's exports and raises a banner naming what is missing. **A call
  added to the page goes in `ENGINE_CALLS` and in `web/build.sh`'s
  `EXPORTED_FUNCTIONS`.** It tests existence, never invocation, and says nothing
  when it finds *no* exports at all, because that means the lookup is wrong
  rather than the engine empty.
  **It tests that a call exists, never what shape its answer has**, so a feature
  adding no new call is invisible to it — the comping-style editor is the case.
  A change like that has to **feature-detect on the data** (the page offers the
  editor only when `compStyles()`'s answer carries a style's `slots`), which
  also catches an engine that has the call but the old shape.
- **The frame has four silent failure modes, all of them CSS.** Each looks like
  nothing happening rather than like an error:
  - **`min-height: 0` on `.chart-zone`.** A flex child's `min-height` is `auto`
    — "never smaller than my content" — so without it the zone grows to fit the
    whole chart, the body overflows, and the frame does nothing at all.
  - **`overflow-y`, not `overflow`, on `body`.** Hidden on *both* axes
    propagates to the viewport and pins `documentElement.scrollWidth` to the
    window, which is exactly what the narrow-layout check reads: with it, a
    deliberately 900px-wide control at a 390px viewport was caught by **neither
    half** of that check. Vertical is clipped; horizontal is left visible so an
    overflow is still detectable.
  - **`#helpDialog[open] { display: flex }`.** A `<dialog>` is `display: none`
    until it is open, so a bare `display: flex` shows it on every page load.
  - **`border-bottom` + `:last-child` is wrong for a mode-filtered list.** The
    last *visible* entry is not the last child once an entry is hidden by mode.
    Space between entries has no opinion about what is hidden.
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

**There are none open right now**, which is a statement with a short shelf life
rather than a boast — the section stays because the rule does: **if work runs
into an ambiguity this file does not answer, flag it rather than silently
picking a direction.** That is how every answer in `docs/DECISIONS.md` got
made.

The three that were here are answered there: reharmonisation is
**rule-based** (the thing that looked like the
question was a bug); MusicXML/MuseScore import is **not wanted**; and a dense,
DAW-style desktop layout is **no** — the page is laid out as an instrument
rather than an article, but it stays one responsive column.

## Working Conventions

- Expand Core Engine (unit-testable, UI-agnostic) first; wire into UI second.
- Default new UI components to one size-class-aware component, not
  platform-conditional forks.
- **JUCE idioms apply to the shell only, and the shell is small** — `WebUi`
  (holds the webview) and `MidiDeviceInput` (Bluetooth pairing sheet) are the
  only JUCE classes with real UI. A new `resized()`/`paint()` elsewhere is a
  sign something's being built in the wrong layer.
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
