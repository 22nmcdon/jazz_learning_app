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
| `modules/shared_ui` | Chart view, on-screen keyboard, scale panel, reharm panel, feedback panel, responsive `MainComponent`. | core engine, JUCE |
| `app` | Platform shell: MIDI devices, window and app lifecycle. | shared UI, JUCE |
| `tests` | Engine unit tests (192), no JUCE, no third-party framework. | core engine |

The core engine links no JUCE at all — that boundary is what keeps a future AUv3/VST3
target possible without a rewrite, and the build enforces it (see below).

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

On Linux the app target needs the usual JUCE packages (`libasound2-dev`, `libx11-dev`,
`libxcomposite-dev`, `libxcursor-dev`, `libxext-dev`, `libxinerama-dev`, `libxrandr-dev`,
`libxrender-dev`, `libfreetype6-dev`, `libfontconfig1-dev`, `libglu1-mesa-dev`,
`mesa-common-dev`). macOS and Windows need no extra packages.

**iOS**: configure with the JUCE iOS toolchain (`-DCMAKE_SYSTEM_NAME=iOS -GXcode`) and
open the generated project. **Android**: JUCE's CMake support does not cover Android, so
that target needs the Projucer/Gradle exporter over the same sources — expect this to be
where MIDI bugs show up first (see the platform notes in `CLAUDE.md`).

## Trying it

The app opens on a built-in practice chart. Click a measure to see its scale and
reharmonisation options; play the chord on a MIDI keyboard or the on-screen keyboard to
get feedback on the voicing.

With a mouse the keyboard defaults to **Hold** (latch) mode, so clicking several keys
builds a chord; on touch, latch is off and several fingers register as one voicing. Any
MIDI keyboard found at startup — or plugged in or paired later — is opened automatically.

The **Practice** menu at the top of the browser page sets a voicing shape for the whole
session - root position, shell, rootless left hand, two-handed rootless - and every voicing
you play is then checked against it, so a rootless voicing played during a root-position
exercise is reported even though the notes spell the chord. The desktop app has the same
control in its header. The menu also connects a MIDI keyboard through the Web MIDI API;
that needs Chrome or Edge, and an embedded page may not be allowed to ask for permission
at all, in which case use the page in its own tab or the desktop app, which talks to MIDI
devices directly.

The menu also carries the sound bank: an **electric piano** synthesised with Web Audio (a
sine ringing another, with the modulation dying away fast so the attack barks), or silent.
Banks are a registry in the page - adding an organ or an acoustic piano later is one entry
and one radio button. A mouse can only press one key at a time, so **Play chord** (or the
space bar) sounds every key currently down at once; it retires itself once a MIDI keyboard
is connected and doing the playing. Connecting a MIDI keyboard widens the drawn keyboard to four octaves,
and anything played outside that widens it further, so a two-handed voicing is never partly
off the end. The desktop app widens its keyboard the same way, but makes no sound yet.

**Import / export** on the browser page opens a chart that came from somewhere else and
writes the one on screen back out. It reads an iReal Pro link, the `.html` file iReal Pro
sends when you share a song, a PDF lead sheet (iReal Pro exports one, and so does this
page), or a progression typed as `| Dm7 | G7 | Cmaj7 |`, pasted in or picked as a file. Going the other way, **Print or save as PDF** prints the
lead sheet alone - menus, keyboard and feedback are left off the page - and **Copy iReal
Pro link** puts an `irealbook://` link on the clipboard that iReal Pro opens directly.
A chart that arrives with a chord the engine cannot read says so and names it rather than
quietly dropping it, and the title, composer and style survive a round trip.

Two environment variables help check the responsive layout without a device:

```bash
JAZZ_UI_SIZE=400x820 JAZZ_UI_TOUCH=1 ./build/app/JazzLearningApp_artefacts/Debug/"Jazz Learning App"
```

<img src="docs/screenshot-compact.png" width="320" alt="Compact layout">

Below 600px wide the panes collapse into tabs and the keyboard drops to two octaves;
`JAZZ_UI_TOUCH=1` switches the scale picker from a popup menu to a bottom sheet and grows
every tap target. Same components either way — there is no second UI.

## Trying the engine in a browser

The JUCE interface cannot run on the web - JUCE has no supported WebAssembly target. The
**engine** can, because it links no JUCE, so `web/` is a third platform shell alongside
`app/`: the same C++ behind a browser front end.

```bash
sudo apt install emscripten   # or install the emsdk
./web/build.sh                # -> web/dist/jazz-engine.js, wasm included, 387 KB
python3 -m http.server -d web 8000
```

Then open <http://localhost:8000>. A published copy of that page is at
<https://claude.ai/code/artifact/bb45867f-890b-46e3-bd96-c9c2a2618713> (private until
shared from the page's share menu).

`web/src/JazzWebBindings.cpp` marshals engine results to JSON; it holds no theory, the same
way `app/` holds none. What the page cannot tell you is whether the JUCE UI works - for
that, build the app and run it, or use `JAZZ_UI_SIZE` / `JAZZ_UI_TOUCH` above.

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
  left-hand, two-handed rootless, spread), checks it against the symbol, and explains what
  is missing, outside, clashing or muddy in the low register. A rootless voicing is not
  told off for having no root. Findings are ordered problems-first.
- **Risky substitutions, judged bar by bar** — a third difficulty tier beyond safe and
  advanced, for moves that work in the right instance and nowhere else: the tritone major
  seventh (G7 → Dbmaj7), the diminished-cycle dominant, the plagal dominant, an
  upper-structure triad over the root, the hexatonic pole, the tritone major seventh of a
  tonic, Lydian displacement, a major 7th a semitone below a minor chord, the tritone
  minor, and anticipating the next chord over this bass. Rather than warn in the abstract,
  each arrives with a verdict on *this* bar: what the guide tones have to do to get in and
  out of it, what it keeps from the chord it replaces, and which side of the bar is at
  fault when it does not land.
- **Whole-tune reharmonisation** — five named plans, lightest touch first: *Minimal touch*
  (colour on the bars that were only marking time), *Recommended* (safe moves, never two
  bars in a row), *Modal colour* (borrowed chords throughout), *Cycle of fifths* (ii-Vs and
  secondary dominants in front of everything that takes one) and *Adventurous* (mediants
  and borrowings, every bar in play) and *Out there* (risky moves, taken only in the bars
  where the voice leading carries them, leaving the rest as written). Each bar is decided against the chart as it stands,
  so a bar sees what the bar before it became; the pass is deterministic, and it will not
  rewrite a bar into the bar before it.
- **Naming a shape** — `ChordIdentifier` answers "what did I just play" with no chart and
  no expected chord. Every note has to be accounted for, so a chromatic cluster returns
  nothing rather than the least bad guess, and a name that leaves out more than it explains
  is not offered: E G B D reads as Em7, then G6/E, with the rootless Cmaj9 further down.
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
- **Import and export in the desktop app.** The codecs live in the engine, so the JUCE
  shell can pick them up; the browser demo is where they are wired to a UI today.

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
