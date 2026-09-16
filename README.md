# Jazz Learning App — POC

A cross-platform jazz education app built on JUCE. Build and reharmonise chord
progressions, see which scales fit each chord, and get feedback on the voicings you play.

This repository is the proof of concept for the two core modules in
[`docs/jazz_learning_app_design.pdf`](docs/jazz_learning_app_design.pdf): the
**Reharmonisation Assistant** and the **Real-Time Chord/Voicing Analyzer**, sharing one
responsive UI and one UI-agnostic theory engine.

![Desktop layout](docs/screenshot-desktop.png)

## Layout

| Directory | Layer | Depends on |
|---|---|---|
| `modules/core_engine` | Chord parsing, scale suggestion, reharmonisation, voicing analysis, note-input abstraction. Pure C++17. | nothing |
| `modules/engine_api` | The engine's answers as JSON - one wire format, read by both shells. Pure C++17. | core engine |
| `web` | **The user interface.** One page, served on the web and hosted by the app, plus the WebAssembly build, the offline worker and the smoke test that drives the built page. | engine API (as JSON) |
| `app` | Platform shell: a webview showing `web/`, plus MIDI devices, the audio device and its electric piano, and file reading. | engine API, JUCE |
| `tests` | Engine unit tests (225), no JUCE, no third-party framework. | core engine, engine API |

The core engine links no JUCE at all — that boundary is what keeps a future AUv3/VST3
target possible without a rewrite, and the build enforces it (see below).

**There is one user interface, and it is the web page.** The desktop app shows that same
page in a webview and keeps for itself only what a page cannot do: opening MIDI devices,
owning an audio device, reading a file off a disk. The engine it talks to is the native
C++ already in the process - the app needs no Emscripten to build, and the WebAssembly
build exists only for the browser. Before this, the two shells were separate
implementations of the same screen, and keeping them in step cost more than it bought.

## Building

Requires CMake 3.22+ and a C++17 compiler. JUCE is resolved by
`cmake/GetJUCE.cmake`, in this order: `-DJUCE_PATH=/path/to/JUCE`, a `JUCE/` checkout
beside this repo, or `FetchContent` from GitHub (JUCE 8.0.4).

```bash
# Everything: engine, UI, standalone app, tests
cmake -S . -B build -DJUCE_PATH=/path/to/JUCE
cmake --build build

# Engine + tests only - no JUCE, no GUI libraries needed
cmake -S . -B build-core -DJAZZ_BUILD_APP=OFF
cmake --build build-core
./build-core/tests/jazz_core_tests     # or: ctest --test-dir build-core
```

Both configurations run in CI on every push (`.github/workflows/ci.yml`): the engine job
deliberately runs on a machine with no JUCE and no GUI packages, so if it fails the module
boundary has been crossed rather than the runner being short a dependency.

On Linux the app additionally needs WebKitGTK for its webview
(`libgtk-3-dev`, `libwebkit2gtk-4.1-dev`; 4.0 also works). macOS and Windows need nothing
extra - WKWebView is part of the system and WebView2 comes in through JUCE.

On Linux the app target also needs the usual JUCE packages (`libasound2-dev`, `libx11-dev`,
`libxcomposite-dev`, `libxcursor-dev`, `libxext-dev`, `libxinerama-dev`, `libxrandr-dev`,
`libxrender-dev`, `libfreetype6-dev`, `libfontconfig1-dev`, `libglu1-mesa-dev`,
`mesa-common-dev`). macOS and Windows need no extra packages.

**iOS**: configure with the JUCE iOS toolchain (`-DCMAKE_SYSTEM_NAME=iOS -GXcode`) and
open the generated project. **Android**: JUCE's CMake support does not cover Android, so
that target needs the Projucer/Gradle exporter over the same sources — expect this to be
where MIDI bugs show up first (see the platform notes in `CLAUDE.md`).

## Trying it

The app opens on a built-in practice chart, drawn as a lead sheet: systems of four bars
divided by barlines, the feel written top left, the title in the middle and the composer
on the right. Click a bar to move to it; click the bar you are already on for its scales
and reharmonisations. Play the chord on a MIDI keyboard or the on-screen keyboard to get
feedback on the voicing.

The desktop app and the browser page are not merely alike: they are the same page. A
screenshot of one is a screenshot of the other, because there is one file. What differs is
what is behind it - native C++ in the app, WebAssembly in the browser - and which of the
two is answering is written in the colophon at the foot of the page.

The keys on the on-screen keyboard **stay down** when clicked, so a chord is built one
note at a time and held: they are a voicing you are holding rather than a piano you are
playing. Clicking a key again, or **Clear keys**, lets go. On touch several fingers
register as one voicing the same way. Any MIDI keyboard found at startup — or plugged in
or paired later — is opened automatically.

A first visit opens a short cheat sheet covering the three things that cannot be guessed
from looking - that a bar is clicked twice, that the keys latch, and where MIDI comes
from. It appears once; the **?** beside the Practice menu brings it back.

A **sustain pedal** works, on both shells and in both senses: the notes keep sounding
after the keys lift, and they keep counting as part of the chord. A voicing spread out
under the pedal — root, then the third, then the seventh, each key released before the
next is struck — is read as the chord it adds up to rather than as a run of single notes,
which is how a pianist actually plays one. Controller 64 from a hardware pedal and the
on-screen **Sustain** control are the same thing by the time anything downstream sees
them, so the on-screen one is there for the many people who have a keyboard but no pedal.

The **Practice** menu at the top right sets a voicing shape for the whole
session - root position, shell, rootless left hand, two-handed rootless, solo - and every
voicing you play is then checked against it, so a rootless voicing played during a
root-position exercise is reported even though the notes spell the chord. The same menu
says how much colour **Show me one** should put in what it plays: the base shape, or the
same shape with the tensions. Pressing the button again walks on to the next shape rather
than repeating the last one. What it shows goes under your hands rather than merely onto
the screen, so it sounds, is analysed, and can be named - the same path a played chord
takes. On the browser page the menu also connects a MIDI keyboard through the Web MIDI
API; that needs Chrome or Edge, and an embedded page may not be allowed to ask for
permission at all, in which case use the page in its own tab or the desktop app, which
talks to MIDI devices directly.

The menu also carries the sound bank: an **electric piano**, or silent. Both shells
synthesise the same voice rather than sampling it - one sine ringing another, with the
modulation dying away faster than the note, so the attack barks and the tail settles - the
page through Web Audio and the app through its own audio device. A mouse can only press
one key at a time, so **Play chord** (or the space bar) sounds every key currently down at
once. Connecting a MIDI keyboard widens the drawn keyboard to four octaves, and anything
played outside that widens it further, so a two-handed voicing is never partly off the
end; both shells do this.

Clicking a bar moves to it; clicking the bar you are already on opens its scales and
reharmonisations. Stepping along the chart to check one voicing after another therefore
never puts a dialog in front of the keyboard, and the arrow keys move between bars without
taking a hand off the keys.

**Name it** asks the other question — not "is this the right chord for the bar" but "what
did I just play", with no chart involved. **Edit chart** types chords into bars directly,
**Reharmonise the tune** applies a plan to the whole chart at once, and when a voicing you
play turns out to spell a substitution rather than the written chord, **Write it into the
bar** keeps it.

**Import / export**, in the Practice menu, opens a chart that came from somewhere else and
writes the one on screen back out. Both shells read an iReal Pro link, the `.html` file
iReal Pro sends when you share a song, or a progression typed as `| Dm7 | G7 | Cmaj7 |`,
pasted in or picked as a file, and both put an `irealbook://` link back on the clipboard
that iReal Pro opens directly.

Two things are the browser page's alone, and both for the same reason - they need
something the JUCE shell has no library for. The page reads a **PDF lead sheet** (iReal
Pro exports one, and so does the page) using pdf.js; the app says so and points you at the
link instead of reading a PDF as gibberish. And **Print or save as PDF** prints the lead
sheet alone, menus and keyboard left off the page, which is the browser's print pipeline
doing the work. The chart reader itself is in the engine either way: what the app is
missing is a way to get text out of a PDF, not a way to understand one.
A chart that arrives with a chord the engine cannot read says so and names it rather than
quietly dropping it, and the title, composer and style survive a round trip - they are
written around the music the way a lead sheet writes them, feel top left and composer top
right. An imported chart becomes the chart, so **Restore original** takes back the
reharmonisations you have tried and returns the tune you brought in, not the one the page
happened to open with.

`JAZZ_UI_SIZE` opens the app's window at a given size, which is how the narrow layout
gets checked without a device:

```bash
JAZZ_UI_SIZE=430x860 ./build/app/JazzLearningApp_artefacts/Debug/"Jazz Learning App"
```

<img src="docs/screenshot-compact.png" width="320" alt="Compact layout">

Below 760px the page lays itself out narrow: the sheet head centres, the bars grow, and
every dialog becomes a bottom sheet rather than a floating panel. That is the page's own
CSS doing it, so the browser at the same width does the same thing - there is no second
layout to keep in step.

## Trying the engine in a browser

The interface is a web page already; what changes in a browser is the engine behind it.
JUCE has no supported WebAssembly target, but the engine links no JUCE, so it compiles to
wasm and the same page runs against it.

```bash
sudo apt install emscripten   # or install the emsdk
./web/build.sh                # -> web/dist/jazz-engine.js, wasm included, 415 KB
python3 -m http.server -d web 8000
```

Then open <http://localhost:8000>. The published copy lives on GitHub Pages:

**<https://22nmcdon.github.io/jazz_learning_app/>**

`.github/workflows/pages.yml` builds the WebAssembly engine, drives the built page in a
browser to check it actually boots, and deploys `web/` on every push. Pages has to be
switched on once by hand, in **Settings → Pages → Build and deployment → Source: GitHub
Actions**; until that is done the workflow runs and the deploy step fails, which is the
only signal that the setting is still off.

Pages rather than an embedded page, because of **Web MIDI**. Connecting a keyboard needs a
permission that an embedded frame is usually not allowed even to ask for, so a MIDI
keyboard that works on a served page does nothing inside one. A page served from its own
origin can ask.

`modules/engine_api` turns the engine's answers into JSON, and both shells read that one
wire format; `web/src/JazzWebBindings.cpp` adds nothing but C linkage on top of it, the
same way `app/` adds nothing but a webview. What the served page cannot tell you is
whether the webview in the app is behaving - for that, build the app and run it.

Three things belong to the served page alone, because they have no meaning in a webview
pointed at a local file:

- **A link carries a tune.** *Copy a link that opens this chart*, in the import dialog,
  wraps the current chart - reharmonisations and all - into the page's own address as
  `?chart=`. Opening that link loads the tune.
- **It works offline.** `web/sw.js` is a network-first service worker: online the network
  always wins, so a deploy is never held back by a cache, and offline the last copy of the
  page and its engine are served from one. Nothing else is needed - there is no server
  behind this page once it has loaded.
- **It says when the browser cannot do MIDI**, before a keyboard is plugged in rather than
  after. Safari and Firefox have no Web MIDI at all, an insecure origin cannot use it, and
  an embedded frame is usually not allowed to ask.

`node web/smoke-test.mjs <directory>` drives a built copy of the page in a real browser -
engine up, chart drawn, a bar clicked, a voicing judged, and the offline cache serving the
page with the network cut. The Pages workflow runs it between building and deploying, so
a page that does not boot is a failed job rather than a broken site. It needs `playwright`
(`npm install --no-save playwright && npx playwright install chromium`).

## What the engine does today

- **Chord parsing** — `Dm7`, `F#m7b5`, `Bb13#11`, `C7alt`, `EbmMaj7`, `G7sus4`, `Am7/D`
  and the usual spelling variants (`-7`, `mi7`, `ma7`, `^7`, `o7`). Distinguishes tones
  that define a chord from colour tones, which is what makes the analyser forgiving in
  the right places.
- **Scale suggestion** — 29 scale shapes across the major, melodic minor, harmonic minor,
  symmetric, pentatonic and bebop families. A canonical chord-quality mapping supplies the
  primary suggestion; every other scale containing the chord's essential tones is offered,
  ranked by avoid notes, each with a plain-language rationale. Seven-note scales are
  spelled one letter per degree (`G A B C# D E F`, not `G A B Db D E F`).
- **Reharmonisation** — substitutions grouped into families and ordered from the closest
  to the original harmony outwards: *extension* (altered, whole-tone and sus dominants,
  Lydian colour, quartal m11), *diatonic* (iii-7, vi-7), *dominant function* (tritone subs,
  related and tritone ii-Vs, backdoor ii-Vs, secondary dominants), *modal interchange*
  (bVImaj7, bIIImaj7, the bVII approach, minor plagal IVm6, Dorian 6ths, minor-major 7ths,
  borrowed m7b5), *chromatic mediant* (IIImaj7), *passing chords* (diminished passing
  chords, chromatic approach dominants, chromatic ii-Vs) and *bass motion* (inversions,
  a triad over a tonic pedal). Each carries a difficulty tag, a style tag, a ranking by
  guide-tone voice leading into the next chord, and an explanation that names the notes the
  substitution keeps from the original chord.
- **Voicing analysis** — classifies what was played (shell, root position, rootless
  left-hand, two-handed rootless, solo, spread), checks it against the symbol, and explains
  what is missing, outside, clashing or muddy in the low register. A rootless voicing is not
  told off for having no root. Findings are ordered problems-first.
- **The shapes themselves** — each voicing type is built from the structure a player learns
  it by, not from a stack of thirds. A rootless left hand is 3-7-9 and 7-3-13; a two-handed
  voicing is 3-7 under 9-13; a solo voicing holds its own root, the left hand playing the
  root with the 7th, the 5th or the octave depending on how low it sits - the 7th turns to
  mud down there - while the right hand says whatever the left could not. Asking for a rich
  voicing keeps the shape and opens it out with the tensions the symbol names, which is
  where a chord like G7alt actually sounds: 3-7-b9 rather than a plain stack. Every shape
  is offered in the register it belongs in, fits under the hands that have to play it, and
  is read back by the analyser as the shape it was offered for - the last of those is a
  test, because otherwise the app hands you a voicing and then marks it wrong.
- **Risky substitutions, judged bar by bar** — a third difficulty tier beyond safe and
  advanced, for moves that work in the right instance and nowhere else: the tritone major
  seventh (G7 → Dbmaj7), the diminished-cycle dominant, the plagal dominant, an
  upper-structure triad over the root, the hexatonic pole, the tritone major seventh of a
  tonic, Lydian displacement, a major 7th a semitone below a minor chord, the tritone
  minor, and anticipating the next chord over this bass. Rather than warn in the abstract,
  each arrives with a verdict on *this* bar: what the guide tones have to do to get in and
  out of it, what it keeps from the chord it replaces, and which side of the bar is at
  fault when it does not land.
- **Whole-tune reharmonisation** — six named plans, lightest touch first: *Minimal touch*
  (colour on the bars that were only marking time), *Recommended* (safe moves, never two
  bars in a row), *Modal colour* (borrowed chords throughout), *Cycle of fifths* (ii-Vs and
  secondary dominants in front of everything that takes one), *Adventurous* (mediants
  and borrowings, every bar in play) and *Out there* (risky moves, taken only in the bars
  where the voice leading carries them, leaving the rest as written). Each bar is decided
  against the chart as it stands, so a bar sees what the bar before it became; the pass is
  deterministic, and it will not rewrite a bar into the bar before it.
- **Naming a shape** — `ChordIdentifier` answers "what did I just play" with no chart and
  no expected chord. Every note has to be accounted for, so a chromatic cluster returns
  nothing rather than the least bad guess, and a name that leaves out more than it explains
  is not offered: E G B D reads as Em7, then G6/E, with the rootless Cmaj9 further down.
  The dominant tensions it knows are generated rather than listed - a ninth in each of its
  forms, with or without a sharp eleventh, with or without a thirteenth - because that
  family is combinatorial and a hand-written list of it kept missing real chords: a
  7b9#9b13 is what an altered dominant is when the player leaves the #11 out, which is
  most of the time. Everything else in the vocabulary is curated, and deliberately so:
  every name competes with every other, and one that nobody writes still wins whenever it
  happens to account for all the notes, so adding chords the parser accepts but players
  do not use makes the common answers worse rather than better.
- **Spotting a reharmonisation by ear** — a played voicing is read back against every
  substitution available for that bar, so playing Ab C Eb G over a Cmaj7 bar is reported as
  "that is Abmaj7, the bVI major seventh substitution" rather than as a broken Cmaj7. When
  two substitutions spell the same notes, the reading closest to the written chord wins.
- **Reading and writing charts** — `ChartFormats` reads both iReal Pro link formats: the
  plain `irealbook://`, and the `irealb://` link the app itself shares, whose body is
  shuffled in 50-character blocks and has to be put back in order first. It writes a link
  back, keeping the title, composer, style, time signature and the way each chord was
  spelled, so a chart that leaves the engine and comes back is the chart that left.
  It also rebuilds a chart from the text of a page, and a page comes in two kinds. An
  engraved lead sheet gives the chord symbols themselves: the reader stitches the runs
  back into symbols, groups them into lines, works out where the barlines were from the
  spacing, and reads past the tempo marking, the bar numbers and the rest of the page
  furniture to find the title. A page from iReal Pro gives none — it draws its symbols —
  but attaches a spoken description to each ("Bar 12, d Flat Major  7"), which names the
  bar as well as the chord, so that page is read from its own descriptions and does not
  depend on where anything sits or which way the coordinates run. Pulling text out of a
  PDF needs a PDF library and belongs to the shell; deciding which of that text is a chord
  chart needs none and belongs here. Every reader reports the chord symbols it could not
  understand instead of handing back a chart that looks complete and is not.
- **Input abstraction** — hardware MIDI and the on-screen keyboard emit identical events;
  `VoicingCollector` groups notes that arrive together into one voicing, so a rolled chord
  or three fingers landing at once both arrive as a chord rather than a stream of notes.

## Not in this POC

- **MusicXML / MuseScore import.** iReal Pro and PDF import both ship (see above); which
  further format comes next is still an open question in the design doc.
- **Audio/pitch-detection input**, deliberately out of scope for this phase.
- **Solo/improv feedback, voicing library, ear training, metronome/practice loop,
  progress tracking.** The analyser already reports a per-voicing score and the feedback
  panel keeps a session average, which is the hook progress tracking would build on.
- **The voice-leading visualiser** — though `guideToneMotion()` in the engine is the
  primitive it needs.
- **Reading a PDF, and printing one, in the desktop app.** Both need a PDF library the
  JUCE shell does not have; the engine's reader is shared, so only the bytes are missing.

## Open questions carried over from the design doc

These were left open rather than silently decided:

1. **Import scope** — iReal Pro and PDF now both import, which answers the immediate half
   of this. Whether MusicXML/MuseScore is needed as well is still open, and the page
   reader is format-agnostic enough that a MusicXML path would feed the same code.
2. **Rule-based vs. data-informed reharmonisation** — the POC is entirely rule-based, with
   every rule in one file (`Reharmonizer.cpp`) and its own difficulty and style tag, so a
   data-informed ranking could replace the ordering without touching the rules.
3. **Solo/improv feedback layer** — treated as post-POC.
4. **A dense, DAW-style desktop layout** — deferred; it would arrive as a fourth size
   class rather than a second UI.
