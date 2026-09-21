# CLAUDE.md

Guidance for Claude Code (or any Claude instance) working in this repository.

For **what the app does and how each feature behaves**, see `README.md`. For
the reasoning behind solo practice, see `docs/SOLO_PRACTICE.md`; for comping,
`docs/COMPING.md`; for the rhythm grid they share, `docs/RHYTHM.md`. Read the
relevant one before touching `LineAnalyzer`, `Comping.h`/`compPlan()`, or
`Rhythm.h`. This file is only for what you need to not break the repo or redo
settled work. For what is **left** to do — work in progress, unbuilt features
and their open design questions, and known loose ends — see `docs/HANDOFF.md`.

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

- **The page is a frame, not a document.** Three zones: `.top-bar` at its own
  height, `<main class="chart-zone">` taking everything left and scrolling
  *itself*, and `.dock` at the bottom. `body` is a flex column at `100dvh`.
  Laid out as a document — masthead, article, footer, all scrolling together —
  the chart got whatever was left over and lost: four bars of twelve were
  readable at 1280×860, and **none at all** at 430×860. Anything added to the
  top bar or the dock is taken out of the chart, so it has to earn it.
- **The chart's head is in the top bar**, not in the sheet. A lead sheet is
  engraved feel / title / credit and still reads that way, along a line; in the
  sheet it scrolled away with the music, so the one thing naming what you are
  playing left the screen as soon as you played past the first line.
- **The top bar's second row is the transport strip**, and it holds what a
  player reaches for while playing: Static / In time, the metre and tempo
  (`#timeSig`, `#tempo`), the take button and the beat dots. What a take is set
  up with rather than played with - count-in, loop, ramp, reharmonise-as-you-go
  - is one popover off it (`#transportButton`). **Its row count must not depend
  on what is showing.** Most of the strip needs a clock, and the take button
  needs a mode that can grade one, so a strip left to wrap freely is one, two or
  three rows deep depending on which corner of mode x In time you are in - and
  every one of those moves the top of the chart. The two groups
  (`.transport-clock`, `.transport-take`) are each one row at any width and each
  reserve that row empty. A check measures the chart's top edge in both settings
  at two sizes and fails if it moves.
- **A reserved height is a pixel count, so it holds or fails to hold *per
  typeface*.** The page renders in two by design (`docs/BRANDING.md`): Jost over
  the web, the fallback stack in the app and offline. Every control on the strip
  is a button or a form control whose height comes from `line-height: normal` —
  the font's own metrics — so each one is given an explicit `line-height` (a
  `height` for the select, which does not size from one) and the reservation is
  sized against that arithmetic. Sized against one face instead, it was 29px
  against controls that are 37–39 in the other, stopped binding altogether, and
  shipped a chart that moved when the clock came on while every local run said
  otherwise. **Measuring a layout on a machine that cannot reach Google Fonts
  measures the wrong page** — so the check runs a second pass with
  `ascent-override`/`descent-override` forcing taller metrics, which needs no
  network.
- **There is one settings panel, `#menuPanel`, and it has sections.** Six of
  them: Sound, The band, Voicings (chords), Scales (solo), MIDI keyboard, About.
  There were two panels in opposite corners and nothing said which held what —
  the band's piano sound and your own were in different panels, both called
  *Sound*. A new **setting** goes in a section or starts one; it does not start a
  second panel. Below 760px the panel is a bottom sheet, which is why it has a
  `.menu-head` close row — at that width it covers the button that would
  otherwise close it.
- **Two menus are deliberately not in it, and both are about a moment rather
  than a preference.** `#transportButton` is the take's — count-in, loop, ramp,
  reharmonise-as-you-go — and lives on the transport strip. `#chartButton` is
  the tune's — import/export, reharmonise, edit, restore — and lives above the
  chart it acts on. The chart's tools spent one commit inside `#menuPanel` and
  it was the wrong room: what is behind *Practice* is how you practise, and
  which tune is on the stand is a different question. **Three menus is the
  ceiling**; a fourth means something is in the wrong one.
- **The chart is justified into its zone, by `layOutChart()`.** A twelve-bar
  tune is 404px of music in a 560px zone on a 1440×900 window, and top-anchored
  the difference banks at the bottom as dead cream. It is given away in two
  goes: the **gaps between systems grow first**, capped at `MAX_EXTRA_GAP`,
  because slack spread through the chart reads as breathing room while the same
  slack at the end reads as a void — then **what is left becomes symmetric
  padding on `.sheet`**, which is the only thing that helps a tune of one
  system, since one system has no gaps. When the chart is taller than the zone
  this does nothing at all and the zone scrolls as it always did. It measures
  with both custom properties cleared, or it measures its own last answer.
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
  `max-width: 760px` is the page's own and still the only one that lays the
  page out differently. `#helpDialog` adds `min-width: 700px` (two columns once
  there is width for them) and `max-height: 700px` (tighter spacing on a short
  window) — a *height* query, which is the first of those here. No
  compact/regular/expanded size classes. Add another only when something
  actually collides, and say in the commit message what collided and at what
  size; both of the help dialog's were added that way, against a measurement.
  Test with `JAZZ_UI_SIZE=430x860` or a 430px viewport — and note that neither
  of the help dialog's breakpoints is exercised at that size.
- **Differences are by input type, not platform** (e.g. the scale picker is a
  dialog or a bottom sheet depending on room, never two implementations).
- **Anything mode- or platform-specific is `data-mode`/`onTheWeb` + a CSS/JS
  branch — never a second component.** Both modes also have a **Static / In
  time** setting meaning the same kind of thing in each — the same question with
  a clock behind it, and readings that are silent without one. Reach for that
  before reaching for a third mode, a second keyboard or a second dock.
- **The bar dialog has three panels and shows two.** Scales (solo), Reharmonise
  (chord practice) and **This bar's take**, which appears only when the last
  take covered that bar. The tab strip is hidden whenever there is only one
  panel to show, which is what it has always done — a bar with take data is
  simply the first case where there are two. The other mode's tab is *hidden*,
  not disabled: it is a question this mode is not about, which is why the
  panels were split by mode in the first place.
- **The cheat sheet is five sections and never scrolls.** `#helpDialog` has its
  own tab strip — Chart, Playing, Reading, In time, Band — because it stopped
  fitting: solo practice's sheet was 2,650px of content in 590px of dialog at a
  phone width. Sections that mean the same in both modes are written **once**
  and shown in both; only what genuinely differs (what the keys do, what the
  page is reading) carries `data-mode`, on the individual `<li>` rather than on
  the list. An entry is a bold lead you can scan plus a quieter detail. A check
  measures every section in both modes at two sizes and fails if any has to
  scroll — so a section that grows has to be split, not allowed to overflow.
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
check that before reaching for `apt-get`); another builds the full app.

**The test count is generated, not typed.** `README.md` and the page's colophon
quote it; the suite is the only thing that knows it, and it went stale twice
when both were written by hand. `tools/test-count.sh` asks the built binary and
writes the number into both files; `--check` changes nothing and fails, and is
what CI runs. **After adding a test, run it** rather than editing either file.
It is also an error for a file to contain *no* count — a generator quietly
maintaining nothing looks exactly like one with nothing to do.

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
  **A chord in a line is read as a chord, and nothing new reads it.** The line
  reading is per-note and complete, but a chord is not a fact about any of its
  notes — Eb Gb A C over Dm7 is a passing Ebdim7, and no reading of that Eb
  gets to the first sentence. So `chordSoFar()` hands the attack it already
  groups to `VoicingAnalyzer` (the shape, and whether the notes carry the bar)
  and `ChordIdentifier` (what they spell instead). That is the boundary the
  header states — a voicing is a thing, a line is a stream — kept by *calling*
  the readers rather than growing a second one; a chord reader inside
  `LineAnalyzer` is the thing not to write. The reading leads with the **top**
  note, because a block-chord soloist harmonises downwards from the melody. It
  is read **when struck**, against the symbol that was on the stand at the
  time, so reharmonising mid-take cannot rewrite it. And `saysTheChord` is not
  a pass mark: the chords between the chart's own are most of what block-chord
  playing is, so one that spells something else is named, never marked.
- **Comping** — done, both halves: the band that plays under a soloist, and the
  exercise. The exercise lives in **chord practice's *In time***, not in a third
  mode; that decision was recorded as a third mode for a long time and was
  reversed, because In time already meant "the same mode, with a clock, adding
  readings that are silent without one" — which is exactly what comping is to
  chord practice. `state.mode` is still two-valued.
  **The band's voicings are a weighted draw, not a minimum.** There used to be
  exactly one voicing per chord for ever; `compingVoicing`'s seeded overload
  now shortlists candidates within a spread of the best and draws by shape
  weight — two-handed rootless plain 100, rich 80, one-hand rootless 34/20,
  and **never a shell** (the root at the bottom is the fault `readCompHit`
  flags in a player). Weight decides the draw, never the cost: in the cost it
  rules a shape out instead of making it rarer. Travel is measured *per voice*,
  because a summed distance penalises a shape for having fewer notes and kept
  the thin shapes unreachable. The **unseeded** overload is unchanged and must
  stay so — it answers "what would a comper play here", which has one answer
  and is the ii-V-I the README walks through. The page puts the **chorus** in
  the seed, which is what makes the second time round the form a second time.
  A style also says **how long each hit rings** (`CompSlot::heldFor`, falling
  back to `CompStyleDefinition::heldFor`), trimmed by `compPlan` so nothing
  sounds into the chord after it — one instrument plays them in order, and a
  number a shell had to correct would be two opinions about one thing. **The
  evaluator says nothing about duration and cannot**: a `PlayedHit` has nowhere
  to put one. That is the decision, not an omission — a style says what the
  *band* does, and a comper holding a chord through a four-to-the-bar is
  reading a style that does not say not to.
  **The four styles are a catalogue, not a ceiling: a style can be described
  rather than named.** The three comping calls take a style *reference* - a
  catalogue key, or a whole description of a style the engine has never seen -
  and a user-made style is **data the shell hands over on every call**, the way
  a chart's progression text is. The engine gained no registry and no memory;
  core needed no change at all, because every function there already took a
  `const CompStyleDefinition&`. The description is **flat delimited text, never
  JSON** - results are JSON because encoding them is the shell's business,
  inputs are text because the engine has no JSON reader and must not grow one.
  It carries no key, name or summary, which is what keeps the grammar free of
  quoting. **An unknown key falls back to the first style; a malformed
  description is an error** - a renamed style should still comp, but a broken
  message must not comp four-to-the-bar under someone who just wrote their own
  figure. **`variation` is advised, never clamped**: `compPlan` is seeded and
  byte-reproducible so a clamp is a latent change to every existing style, and
  the consequence it guards is a sound rather than a fault. See
  `docs/COMPING.md`.
  **A style's slots are the figure the band plays, never a fence around the
  player**: placement is read against the grid the style's `feel` implies, and
  the slots keep a tier of their own for words rather than points. Reading the
  two as one was a real bug — the Charleston accepted three of the eight
  positions a swing comper uses, and nothing in the catalogue accepted the and
  of one at all. See `docs/COMPING.md` before touching any of it.
- **The band's velocity is humanised in the page, not the engine.** Stroke to
  stroke, voice to voice inside a chord (top carries, inner sit under, bottom
  is the hand), and lighter off the beat than on it — seeded off `spread()`
  like everything else, so a loop still comes round the same. A style says
  where chords fall, how long they ring and in what register; this is the
  difference between a player and a sequencer playing that part, which is why
  it is not on `CompStyleDefinition`. **What crosses to the app is a
  multiplier, never a level** — the two shells have their own idea of how loud
  an accompaniment is, and each keeps it.
- **Drums** — done. The one member of the band with no engine call: the page
  owns the pattern, each shell owns the sound (synthesised in both, like the
  click — there is no kit in `assets/` and four unpitched one-shots would not
  earn one). Written on the shared grid and swung through `beatsIntoBar` like
  everything else, so the ride and the comp agree about where the and of two
  is. The pattern follows the **metre**, not the comping style, and needs the
  clock — there is no figure to play to a bar you are sitting on.
- **Voice-leading visualiser** — done, **on the keys, not on the chart**. It was
  drawn over the bars first and that was the wrong place: a picture of where the
  guide tones are answers a question a player having it explained already knows
  the answer to, and the one they actually have — where do my fingers go — it
  leaves them to work out. So `voiceGuideTones()` takes the chord that is coming
  *and the notes just played*, and the two keys are badged with their degree.
  **Voiced, not named**: the same chord asked of a left hand low down and of two
  hands in the middle gives marks an octave apart, and the octave is the half a
  chart cannot give you. The notes are **paired** to the tones so the hand moves
  least overall — tone by tone, both claim the same finger and the other voice is
  stranded — and one note leads into both when there is no second one, which is
  the ordinary case in solo practice. A guide tone already under a finger keeps
  its badge, inverted: that one does not move. The page points at the next bar
  that says something **different**, not simply the next bar, or two bars of one
  chord read "stay where you are" and then say nothing on the bar it changes.
  `guideToneMotion` is still what answers it for two chord *symbols* and is
  untouched.
- **The page's layout** — done, and the reasoning is in UI Conventions above.
  Three zones; the chart's head in the top bar; the cheat sheet sectioned. The
  rolling bar brings itself into view, which nothing on this page did for
  anything before — there was no `scrollIntoView`, `scrollTo` or `scrollTop` in
  the file at all, so on a tune longer than the chart zone the mark simply
  rolled off the bottom. The transport is out of the menu and onto a strip of
  its own, the two settings dropdowns are one sectioned panel with the chart's
  tools in a menu of their own above the music, the dock foot is two groups —
  what you press and what you read — practice settings survive a reload, and the
  chart is justified into its zone rather than leaving a void under it (see UI
  Conventions above). The three screenshots in `docs/` are shot against this
  shape, in the webfont; `docs/HANDOFF.md` says how, since a machine that cannot
  reach Google Fonts will otherwise photograph the fallback face.
- **Not built, deliberately open**: personal voicing
  library, ear training, progress tracking beyond current per-take/session
  stats, licks/line suggestions,
  MusicXML/MuseScore import, PDF reading/printing in the JUCE app (the engine's
  reader is shared and format-agnostic; the app just lacks a PDF text-extraction
  library). Each one's open design question is in `docs/HANDOFF.md`.

## Critical Invariants

These aren't stylistic preferences — violating them breaks something in a way
that's easy to miss in review:

- **Every beat time is an origin plus a beat count at one spacing**, so anything
  that changes the spacing mid-roll must move the origin to the beat the change
  takes effect on — and must reset `transport.countInBeats`, which is a fact
  about *this roll* rather than about the count-in checkbox. Worked out from the
  checkbox instead, a tempo change halfway through a take counted the player in
  again. The tempo ramp changes tempo only at a chorus boundary, and does it
  while beats are being *booked* rather than when one sounds, so every beat in
  the diary was written down at the tempo it will be played at.
- **Reharmonise as you play changes the chart on the *last* bar of a chorus,
  not at the top of the next one.** Reharmonising asks the engine and redraws,
  which is a turn of the event loop; the band is booked a bar ahead and
  synchronously. At the boundary the first bar of the new chorus still plays
  the old chord. A bar early, everything is the new tune by the downbeat. It
  restores `reharm.before` when the transport stops — an exercise, not an edit
  — and clears only the `reharmonised` marks it put there itself. **How much**
  (`reharm.amount`: a bar / a few bars / the whole tune) and **how far**
  (`reharm.reach`) are separate questions, and a plan carries its own reach so
  the second is hidden for the whole-tune amount. The whole-tune amount applies
  a named plan to the chart *as it stands*, which is what makes it escalate;
  a plan with nothing left to change falls through to moving a bar. **In time
  with the exercise armed, `planDialog` chooses the take's plan instead of
  rewriting the chart** — same dialog, same six write-ups, the clock decides
  which question is being asked.
- **The engine has no clock and must never get one.** Time/position enters as
  data (a `BarPosition`, a beat+tick on the shared grid in `docs/RHYTHM.md`)
  that the shell computed from its own clock — never as something the engine
  infers about when a call arrived. This is what keeps `LineAnalyzer` and
  comping's `compPlan()` testable.
- **Rhythm and voice-leading produce words, never points — in solo practice.**
  `score()` is untouched by timing or line-shape; see its own doc comment before
  changing that. **The chord reading is the same rule one step further out**: a
  block chord in a line is named, never scored, and a voicing that spells
  something other than the bar costs the take nothing — there is no number
  anywhere that `saysTheChord` moves. **Comping scores placement, and that is not a contradiction**: a
  line has no written standard for where its notes fall, so a number would be one
  the engine invented and then marked a player against, while a
  `CompStyleDefinition` *is* that standard, chosen off a menu and already the
  thing the generator is held to in both directions. The argument is in
  `docs/COMPING.md`; what matters here is that the two rules answer two different
  questions and neither may be quoted at the other.
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
- **Practice settings are remembered; work is not.** One pair of accessors
  (`remember`/`recall`, plus `rememberFlag`/`recallFlag`/`recallNumber`) is the
  only thing on the page that touches `localStorage`, under flat `jazz*` keys.
  What survives a reload is how the room is set up — tempo, metre, In time, the
  sound bank, the band, the comping style, guide tones, the loop. What must
  never survive is the chart, a take, or anything mid-exercise. Three rules the
  recall keeps: it **makes no sound** (turning the piano on by hand sounds the
  bar you are on, which is wrong for a page that has just opened); it **never
  overrides the chart** (a metre is remembered in the picker's handler, not in
  `setMetre`, which an import also calls); and it **validates every value**,
  since a store the page does not own can hold anything. A recalled loop goes
  back through `fillLoopRange()` — bars 1–12 on a four-bar chart is two numbers,
  not a loop.
  **A comping style you wrote is remembered too, and it is the one structured
  value here.** Its accessors (`rememberObject`/`recallObject`) are built *on*
  the one pair rather than beside it, like the flag and number ones — there is
  still exactly one door to `localStorage`. It is **dropped rather than
  migrated** when it stops matching what the engine sends: the grid's own
  `ticksPerBeat` is stored beside it (derived, never a hand-raised version
  number), the field list is checked against the engine's *current* answer as
  well as against the store, and the feel is held to the list the engine just
  sent. A preference that cannot be read back is one you remake in a minute;
  a migration path for eleven fields is more code than the editor.
- **Engine/page version mismatch has no symptom of its own, so the page checks
  for it.** A cached engine paired with a fresh page makes a feature simply
  *vanish* — empty menu, engine chip still saying ready — because `ccall` on a
  name the engine does not export throws something that names neither the call
  nor the cause. Two things answer that: `web/index.html` requests
  `jazz-engine.js?v=<build stamp>` so the pair is fetched together, and at boot
  it checks every call in `ENGINE_CALLS` against the engine's exports and
  raises a banner naming what is missing. **A call added to the page goes in
  `ENGINE_CALLS` and in `web/build.sh`'s `EXPORTED_FUNCTIONS`** — the check is
  what makes forgetting either one visible instead of silent. It tests
  existence, never invocation (a take is armed by one of these), and says
  nothing when it finds *no* exports at all, because that means the lookup is
  wrong rather than the engine empty — a false banner on a good engine would be
  worse than the silence it replaces. The app needs none of this (one binary,
  no cache between the two) but is covered anyway, out of the `No engine call
  named` answer `WebUi.cpp` already returns.
  **It tests that a call exists, never what shape its answer has**, so a
  feature adding no new call is invisible to it. The comping-style editor is
  the case: an older cached engine takes a described style, fails to find it as
  a key, and comps four-to-the-bar with the chip still reading ready. A change
  like that has to **feature-detect on the data** — the page offers the editor
  only when `compStyles()`'s answer carries a style's `slots` — which is
  strictly better than a name check, since it also catches an engine that has
  the call but the old shape.
- **The frame has four silent failure modes, all of them CSS.** Each looks like
  nothing happening rather than like an error:
  - **`min-height: 0` on `.chart-zone`.** A flex child's `min-height` is `auto`
    — "never smaller than my content" — so without it the zone grows to fit the
    whole chart, the body overflows, and the frame does nothing at all.
  - **`overflow-y`, not `overflow`, on `body`.** Hidden on *both* axes
    propagates to the viewport and pins `documentElement.scrollWidth` to the
    window, which is exactly what the narrow-layout check reads. With it, a
    deliberately 900px-wide control at a 390px viewport was caught by **neither
    half** of that check. Vertical is clipped; horizontal is left visible so an
    overflow is still detectable.
  - **`#helpDialog[open] { display: flex }`.** A `<dialog>` is `display: none`
    until it is open, so a bare `display: flex` shows it on every page load.
  - **`border-bottom` + `:last-child` is wrong for a mode-filtered list.** The
    last *visible* entry is not the last child once an entry is hidden by mode,
    so a rule is left hanging under nothing. Space between entries has no
    opinion about what is hidden.
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
- ~~Is a future dense, DAW-style desktop layout worth designing for now, or
  deferred?~~ **Answered: denser yes, multi-column not yet.** The page is laid
  out as an instrument rather than an article — a fixed three-zone frame with
  the chart taking every pixel the other two do not — but it stays *one
  responsive column*. No side panel, no wide-screen layout of its own. If that
  is revisited, the thing to weigh is that settings are moving into one panel
  (`docs/HANDOFF.md`, commit 4), and a persistent side panel is that panel
  pinned open.

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
