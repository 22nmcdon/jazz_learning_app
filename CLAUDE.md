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

Both modes now have a **Static / In time** setting, and in each it means the same kind of
thing: the same question with a clock behind it, and readings that are simply silent
without one. In solo practice the clock counts a line; in chord practice it is comping.
Three things a session keeps wanting to build already exist as that setting - do not
reach for a third mode, a second keyboard or a second dock.

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
- The keys **latch** in *Static*: a voicing is something you hold, and `state.heldNotes` is
  what the analyser is handed.

### In time — comping
The same mode with a clock behind it, which is where comping as an exercise lives. Not a
third mode, deliberately: see the bullet in *The band's instruments are recordings* for why
that was reversed.

**The reasoning behind every rule below - the argument for scoring placement, the two bugs
the widened invariant caught and the worked examples - is in
[`docs/COMPING.md`](docs/COMPING.md). Read it before changing any of this.**

- **In time, the keys do not latch.** A comp is struck, not held, so each gesture is its
  own hit and `analyseVoicing` stands down in favour of the per-hit path. Switching between
  Static and In time lets go of the keys for the same reason a change of mode does - the
  latch rule changed under them.
- **One gesture is one chord, and here the page waits** - the one place it does. Solo
  practice never waits for a chord to be finished because a line's unit is the note; a
  comp's unit is the *chord*, and one note is not a voicing, is not in or out of a register
  and cannot have the bass player's note underneath it. The position is the **first** note's.
- **The page may remember what was played; it may never remember what it meant.** `comp.hits`
  is a list of `{bar, at, notes}` - the same class of thing as `state.heldNotes`. Every
  verdict, live and final, comes back from the engine, which is why `compHit`/`compTake` are
  pure and why the solo take is still the only stateful corner of `jazz::api`.
- **Only a bar played through counts.** A take stopped mid-bar drops it, or every
  four-to-the-bar take ends marked sparse in its last bar. The page counts a bar on the
  downbeat of the next one.
- **Anticipation is read from the notes, and a tie is not a push.** A player does not
  declare a push; the voicing is analysed against this bar's chord and the next one and the
  strictly better score wins. Over a bar repeating its chord the two are identical, and
  promoting a tie would call every hit on an anticipating slot a push.
- **`inItsOwnBar()` is not optional.** The page quantises a moment, so a chord struck just
  before a downbeat arrives as beat 4 of a bar of four and one read back from a tick count
  arrives as beat -1. Both are the neighbouring bar.
- **The root in the bass is the evaluator's to say, not `VoicingAnalyzer`'s.** Asking through
  `practiseType` would dock the *voicing's* score for a comping reason and call a shell
  voicing the wrong shape. The fault is not the shape, it is that somebody else is playing
  that note - so it produces a chip and words and no points.
- **Density is clamped to the metre.** `fewestPerBar` is a plain count and a count does not
  survive a change of metre the way a slot does: four to the bar is three chords in three.

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
- **The engine still has no clock, and must not get one.** `LineAnalyzer` has no time in
  it, which is what makes it testable. The transport lives in the page: it calls
  `selectBar()` on each downbeat and the engine learns about it through `soloSetBar` the
  same way a mouse click teaches it. **Nothing in the engine may start depending on when a
  note arrived.**
- **The transport books beats, it does not tick them.** A `setInterval` at the beat
  drifts, and a metronome that drifts is worse than none. Every tick books the beats
  falling inside a lookahead window, with times computed from one origin - so changing the
  tempo mid-roll has to re-anchor that origin, or the next beat is spaced the new way from
  an origin that meant the old one. The metre cannot be re-anchored the same way, because
  the count-in and the bar arithmetic are both in beats-per-bar: changing it mid-roll
  stops and starts the clock in the metre just chosen.
- **The metre is the chart's, not the transport's.** `ChartFormats` has always read a time
  signature out of an iReal Pro link and off a PDF; it only reached the page when the
  sheet head got somewhere to put it, and until then a waltz was counted in four. It rides
  `holdChart` as `beatsPerBar` / `beatUnit`, seeds `transport.beats`, and fills both the
  picker and the beat dots. `transport.beats`, never a literal 4 - the dots, the count-in
  and the bar arithmetic all read it. (The writer does not put it back out yet, so export
  does not round-trip the metre; that is one line in `ChartFormats` when someone wants it.)
- **Arming a take and starting the clock are one gesture.** Two buttons made two states
  nobody meant - a take over a chart that never moves, and a chart rolling with nothing
  counting - so `toggleTake()` does both, and the space bar is the same call. The take
  starts first, because the transport's first `selectBar` has to reach an engine that
  already has one. Space is fenced out of fields where a space is a character
  (`typingInto`) rather than fenced to `document.body`: a take is started right after
  clicking a bar or the button, which is exactly where the body is not.
- **The click is the one place the two shells are honestly unequal.** On the web it is
  scheduled into Web Audio at an exact time. In the app the sound is native, across an
  event bridge with no "play this at" argument, so the click is *sent* when due and
  arrives a message-thread hop later. Both shells read the same clock, so chart and click
  agree in both; what the app gives up is a few milliseconds of jitter. Do not paper over
  that by pretending to schedule in the app - fix it, if it matters, by giving the bridge
  a time.
- **Where a note fell is now on the wire, and it is still not time.** `soloPlayNote` takes
  a beat and a tick, and a negative beat means the shell has no clock and cannot say -
  which is not the downbeat, and the engine is careful to treat it as neither. A take
  played statically is read exactly as it always was: every rhythmic reading is additive
  and silent without positions. **Nothing in the engine may start depending on when a note
  arrived** - a `BarPosition` is a position, like a measure index, and the shell turns its
  own clock into one before the engine sees it.
- **Rhythm produces words, never points - in solo practice.** `score()` is untouched by
  any of it. Where a note sits in a bar does not make it a better or worse note, and the
  moment it moved the score the score would stop being explainable - the same rule the
  rest of *A line's shape does not touch the score* is built on.

  **Comping scores placement, and that is not a contradiction.** A line has no stated
  standard for where its notes fall, so a number would be the engine inventing one and
  then marking a player against it. A `CompStyleDefinition` *is* that standard, written
  down, chosen off a menu, and already the thing the generator is held to in both
  directions. The full argument is in [`docs/COMPING.md`](docs/COMPING.md); what matters
  here is that the two rules are about two different questions and neither may be quoted
  at the other.
- **"Sat on" versus "passed through" is the whole reason the grid exists.** The same pitch
  against the same chord is what every bebop line is made of at speed and what sounds like
  a mistake when dwelt on; only the rhythm tells them apart. `LineNote::passedThrough` is
  filled in behind, like `resolvesTo`, because it is a fact about the gap to the *next*
  note. An eighth is the boundary, and the gap is measured **through the barline** - the
  and of four into the next downbeat is an eighth, not a bar and a bit.
- **An approach note is left out of that reading.** It stepped home, which is the verdict
  that matters about it; adding "and it was passed through" would count the line's best
  notes among the ones being asked about.
- **A line is not always one note at a time, and the shell is the one that knows.** Players
  comp behind themselves and solo in block chords, and the most idiomatic move either makes
  is sliding a whole voicing chromatically into the next one - G7, a G7alt under half of it,
  Cmaj7. Read as a stream the inner voices never resolve, because the note after a chord's
  Ab is the chord's own B rather than the G it was heading for. That read as four failed
  resolutions, which was reported, and it was the reading that was wrong rather than the
  playing.
- **`Attack` is the whole of the fix, and it is a gesture rather than a time.** `play()`
  takes `Attack::withPrevious` for a note struck with the one before it, and notes so joined
  are one attack: **they neither resolve nor strand one another**, and the window runs
  attack to attack so each voice finds its own way home. Every rule is the rule it always
  was with "the note before" widened to "any note of the attack before" - which is why a
  line with nothing struck together reads exactly as it did, and why all of the existing
  tests passed untouched.
- **It is the shell's to say because only the shell can.** Nothing about the pitches tells a
  chord from a line - the same notes are both - so this is knowledge the page has and the
  engine must not invent. It is still not a clock: "struck together" is a fact about a
  gesture, like a measure index is a fact about a bar. The page works it out from its own
  clock (40ms, and MIDI only - a pointer plays one key at a time however fast it is
  clicked, so a click is always its own attack) and the engine never learns what a second
  is.
- **Nothing waits for a chord to be finished.** The flag says a note *joined* an attack,
  which is knowable the instant it arrives, so no shell buffers and no reading is delayed.
  The cost is the one exception to *nothing settled is ever revisited*: the window cannot
  tell a chord's first note from an ordinary next note, so it judges on the first and takes
  the judgement back when the rest of the same chord answers it. Nothing across two
  gestures is ever revisited, which is the rule that was actually meant. `resolvedByLastNote`
  and `strandedByLastNote` are therefore cleared **per attack, not per note**, so a shell
  reading them after each note of a chord sees one corrected answer rather than a flicker.
- **A note struck with three others is still one note.** It is counted, coloured, scored and
  positioned exactly like any other. The gesture changes which notes can resolve which, and
  nothing else - `chordsPlayed` and `chordVoicesResolved` are there so the summary can say
  back what the player was doing, not so it can weigh it.
- **An attack does not cross a barline.** Two notes read against different bars were played
  against different chords, whatever the shell believed about the keyboard - and grouping
  them would stop each resolving the other, which is exactly wrong for the commonest thing
  in the idiom.

### The rhythm grid — one representation, two consumers
`modules/core_engine/.../Rhythm.h` is the whole of it, and it is shared by comping and by
solo practice on purpose: the moment there are two grids they disagree about what an
eighth is.

- **24 ticks to the beat**, because 24 divides by 2, 3, 4, 6, 8 and 12 - so straight
  eighths (12), eighth-note triplets (8), sixteenths (6) and the dotted forms all land
  exactly on a tick. A straight eighth grid was the obvious choice and could not have
  written a ballad's triplets or a bebop line's sixteenths at all.
- **Swing is not in the grid.** Swung eighths are a ratio a shell plays them at, not a
  different place to write them: the engine writes tick 12 and the page sounds it two
  thirds of the way through the beat. **Only the eighth moves** - a triplet is already
  written where it is played, so remapping every subdivision (the obvious implementation)
  would bend a ballad's triplets into something nobody plays. The page is also the only
  thing that un-swings a note coming *back* in, so the engine always sees straight
  notation.
- **`strengthAt()` is metre-aware rather than tabulated**, which is what keeps a waltz from
  being a special case: the second strong beat is the one starting the bar's second half,
  and an odd metre has no second half, so only its downbeat is strong. That is the
  character of three, not a gap in a table.

### Comping — the band behind the soloist
- **`CompStyleDefinition` is the one artifact**, and the generator, the evaluator seam and
  the menu all read it. A style described in one place and re-described in another is how
  the two drift - the same reason `scaleStyles()` lives in the engine.
- **A slot is written the way a player describes one.** `beat` empty means every beat (so
  four-to-the-bar is one slot, in any metre); negative counts back from the end (so "the
  and of four" is the same slot in three). A slot naming a beat the metre has not got says
  nothing, which is more honest than folding it onto one that exists.
- **The shape was proved against styles that genuinely differ** - dense four-to-the-bar
  against sparse Basie, and a ballad whose feel is triplets rather than eighths - rather
  than fitted to one style and generalised afterwards. Adding a style that the shape
  cannot express is a sign the shape is wrong, not the style.
- **The plan is made ahead, not decided per beat.** Two things need more than the moment
  they are played in: a hit that anticipates has to know the next chord, and a voicing has
  to be led from the one before. `compPlan()` does both in one pass, which also removed
  the page's promise chain - with the whole plan in hand there is nothing left to race.
- **Seeded, with the bar mixed in.** The same seed gives the same comp note for note, and
  planning bars 4-7 alone gives the same four bars as planning 0-7 and taking the tail -
  so a loop coming round again is the same band, not a different one each chorus. The
  hash is hand-rolled because `std::mt19937`'s *distributions* are not specified across
  standard libraries, and a plan that differed between the browser and the app would be
  two bands playing.
- **`fitsStyle()` is the invariant, and it is checked from both directions.** Everything
  the generator plays for a style must pass the evaluator's own "is this in style" test -
  the same trap `idiomaticVoicings` and `VoicingAnalyzer` are held out of. Anticipation is
  checked one way only: a hit that pushed must come from a slot that pushes, but a slot
  that pushes may honestly produce a hit that did not, because the last bar of a range has
  no next chord to pull forward.
- **The comp is a channel of its own in both shells, never the player's own voices.**
  Comping E4 under a soloist playing E4 has to be two notes, or one note-off stops a note
  the other is still sounding. On the web that is `audio.compVoices` beside
  `audio.voices`; in the app it is `Voice::comping`, which is why `noteOff`, the pedal and
  `allNotesOff` all skip comp voices - and why the bridge carries `"comp"` rather than
  reusing `"chord"`, whose first act is to silence everything.
- **The walking bass is built the same way and is not a comping style.** `walkingBass()`
  is one note to the beat, and it walks whatever the piano is doing - which is why it is
  its own call and its own plan rather than a field on a `CompStyleDefinition`. A change
  of comping style must not make the bass player start again.
- **A walking line is built in *runs*, not beat by beat.** A run is a chord arriving and
  the beats before the next one does. Written beat by beat the line has nothing to aim at,
  and "the nearest chord tone" walks straight back where it came from - the first version
  played D, C, D, D over one bar of Dm7. A run knows its root, knows the approach it has
  to reach by its last beat, and everything between is travel.
- **Three rules, in this order**: the root on the beat the chord arrives (the one note the
  line is not free about, because it states the harmony); an approach into the next root
  on the beat before a change (a semitone either side, or a fifth); chord tones between,
  moving towards that approach. Bounded to `lowestBassNote`/`highestBassNote`, because a
  line following the voice leading climbs off the instrument inside a chorus.
- **Drums are named in the menu and not built.** A rhythm section is three instruments;
  leaving the last one off the list entirely would say the feature is finished.

#### Where the comping plan got to
Comping was built to a seven-step plan. Steps **1, 2, 3, 5 and the style picker out of 7**
were the first agreed scope and shipped first; **4, 6 and the rest of 7** were scoped and
built afterwards, which is when step 6's recorded answer turned out to be wrong and was
reversed. This is what each step actually came to, so that a later session neither re-plans
a finished one nor assumes an unfinished one is done.

| Step | State | Where it is, or what is missing |
|---|---|---|
| 1. The subdivision grid | **done** | `Rhythm.h`. An eighth grid was the plan's suggestion and would not have written a ballad's triplets, so it is 24 ticks to the beat instead. The transport books hits at sub-beat positions inside the lookahead it already used for beats (`compBar`, through `beatsIntoBar`). One grid, two consumers - solo practice's rhythmic reading was built onto the same one rather than given its own. |
| 2. `CompStyleDefinition` | **done, bar one bullet** | `Comping.h`. Onset slots (`CompSlot`), register (`lowestNote`/`highestNote`), density (`fewestPerBar`/`mostPerBar`) and anticipation across the barline (`CompSlot::anticipates`) are all there, and the shape was proved against four styles that genuinely differ rather than fitted to one. **"Typical durations" is the bullet that has no field.** A hit rings until the next one stops it and the page decides that, so a style cannot yet say it is stabbed rather than held - which is a real difference between styles and the first thing to add if this shape is reopened. |
| 3. The generator | **done** | `compPlan()` - seeded, planned a range ahead rather than per beat, with `fitsStyle()` written before it and checked from both directions, as the plan asked. Playback walks the plan off the same transport seam as the click. |
| 4. The evaluator | **done** | `readCompHit()` and `evaluateComp()` in `Comping.cpp`. `fitsStyle()` did not grow - it gained `slotAt()` underneath it and stayed about a *generated* hit, because a `CompHit` knows two things a played one does not. Stateless: a comped chord has no window over it, so nothing needs remembering. The invariant now runs through placement, register **and** density, and it caught two real bugs - see *Where the two numbers had never been read* in `docs/COMPING.md`. |
| 5. `compStyles()` on the wire | **done** | `EngineApi`, and both shells build the menu from it with no local copy, the same way `scaleStyles()` works. |
| 6. Where it lives in the mode structure | **done, and not where this table used to say** | **Chord practice's *In time***, not a third mode. The reversal and its reasoning are the bullet above and `docs/COMPING.md`; the short form is that In time already meant "the same mode, with a clock", which is what comping is to chord practice. `state.mode` stays two-valued. |
| 7. UI and menu wiring | **done** | The style picker, the instrument toggles and their sound pickers shipped first. The verdict surface followed once step 6 was settled, and it **grew `.feedback`** rather than adding a panel beside it - it is the same mode's dock answering two halves of one question. A `.comp-place` chip beside the verdict live, the fit on the existing `#meter` and a summary block at the end of a take. |

Three things were built that were not steps, and should not be read back into the plan as
though they were: the **walking bass** and the **recorded instruments** (both asked for
separately, and both documented above), and solo practice reading a **chord as one gesture**
(`Attack`, above), which came out of a report rather than out of this plan.

### The band's instruments are recordings
- **`assets/` holds them, because both shells want them.** The app compiles the WAVs in
  through `juce_add_binary_data`; `web/build.sh` copies the same files next to the page,
  which fetches them. One copy in the repository, two ways of reaching it - putting them
  in `web/` would have made the app fetch over a `file://` URL, which webviews refuse.
- **One note per instrument, pitched by playing it faster or slower.** This is a practice
  app's band, not a sampler. A single well-recorded note stretched over two octaves is the
  difference between a plausible bass and a sine wave; the cost is that the far ends are a
  little short and a little wrong, which is part of why the walking line is bounded to a
  real bass's compass. **The root note each was played at is written down beside the file**
  in both shells - guess it wrong and the whole instrument is transposed.
- **A sampled bank and a synthesised one are one registry.** On the page a bank either
  builds a voice from oscillators or names a file and a root note, and `voiceFor()` hands
  back the same shape either way, so the comp, the bass and the player's own keys schedule
  identically. Adding an instrument is an entry in that table and a file in `assets/`.
- **Recordings are fetched lazily and only on the web.** A visitor reading a chord chart
  should not pay for three recordings, and in the app the sound is native and the files are
  already in the binary. The service worker caches whatever it fetches, so a recording used
  once is there offline afterwards.
- **A sampled voice's envelope only shapes the attack and the release.** The decay is in
  the recording. Running the synth's decay over it as well fades the note out twice.
- **Each instrument is its own channel, in both shells.** Player, comp and bass share a
  keyboard and a voice pool and must not share voices: a bass note and a comped chord an
  octave apart would otherwise stop each other, and the player's own note-off, pedal and
  panic would reach the band. On the page that is three voice stores; in the app it is
  `Voice::comping` and `Voice::walking`, which every player-facing function skips.
- **The evaluator lives in chord practice's *In time*, and the third mode was a wrong
  answer that got reversed.** It was decided once, here, that scoring a player's own
  comping was "neither of the questions the two modes ask" and so deserved a mode of its
  own. That did not survive the **Static / In time** axis solo practice already had: In
  time is not a different mode there, it is the same mode with a clock, adding readings
  that are silent without one. Which is exactly what comping is to chord practice - the
  voicing half is `VoicingAnalyzer` on the same chart in the same dock, and the clock adds
  the half that needs one. So `state.mode` is still two-valued, nothing was built around a
  third, and the bar dialog's question in comping answered itself: substitutions, because
  a comper reharmonises. If you are tempted by a third mode again, the counting is in
  [`docs/COMPING.md`](docs/COMPING.md).

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
- **The generated piano never sounds under someone being asked to play chords.** It would
  be playing the very voicing they are reaching for - in static chord practice the one the
  bar asks for, and in time the one they are being read on. `applyMode` stops it in both,
  and remembers the switch so solo practice finds the band as it was left. The rest of the
  band is not the same question: the **walking bass plays wherever there is a clock**, and
  is what makes a doubled root worth mentioning at all.
- **The Static / In time switch is both modes', not solo practice's.** It used to carry
  `data-mode="solo"`; it carries none now, because absence is how that menu spells "always
  shown". What In time *means* differs by mode, so the note under it is written by
  `describePlaying()` rather than being fixed markup.
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
- Personal voicing library; ear training; progress tracking. The analyser's per-voicing
  score, the feedback panel's session average and now a take's own numbers are the hook
  the last of those would build on. (The metronome and the practice loop ship - they are
  what In time is.)
- **A drummer.** Named in the comping menu and not built. Unlike the bass it needs no
  theory and no engine call at all - a pattern and a kit - so it is the one piece of the
  rhythm section that is entirely a shell problem. The metronome click is the only
  percussion either shell can make today.
- **Licks.** Solo mode's "Which scale?" is the scale half of "show me one"; suggesting a
  *line* to play over a bar is a separate feature needing generated or curated patterns,
  rhythm and register - deliberately not started.
- **Reading a chord as a chord.** Solo practice now reads chordal playing voice by voice,
  which is what a line needs. What it does not do is ask whether the *voicing* said what the
  bar said - that is `VoicingAnalyzer`'s question, and joining the two would mean deciding
  which answer a block-chord solo wants. `LineNote::struckWithPrevious` is the seam: the
  runs of it are the voicings, already on the wire.
- **Per-bar comping marks on the chart.** `paintTakeMarks()` draws a solo take's numbers
  on the bars and is still solo practice's alone. A comping take's `bars` come back in the
  same shape, so this is a page change with no engine in it.
- **How long a comping hit lasts is the page's, not the style's.** `CompStyleDefinition`
  says where a style puts a hit and says nothing about whether it is stabbed or held, so a
  voicing rings until the next one stops it. It is the one bullet of the definition's
  agreed shape that has no field, and the first thing to add if that shape is reopened.
- **Rhythm in the score.** The readings exist - strong beats, sat on versus passed through
  - and deliberately produce words rather than points. Making rhythm *count* is a separate
  decision, and the argument against it is in `score()`'s own doc comment.
- MusicXML / MuseScore import. The page reader is format-agnostic enough to feed it.
- A metre that survives export. The readers bring a time signature in and the page now
  shows it; the iReal Pro writer does not put one back out.
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
