# Jazz Learning App — POC

A cross-platform jazz education app built on JUCE. Build and reharmonise chord
progressions, see which scales fit each chord, and get feedback on the voicings you play.

This repository is the proof of concept for the two core modules in
[`docs/jazz_learning_app_design.pdf`](docs/jazz_learning_app_design.pdf): the
**Reharmonisation Assistant** and the **Real-Time Chord/Voicing Analyzer**, sharing one
responsive UI and one UI-agnostic theory engine.

![Chord practice: a shell voicing read back, with the low-interval limit flagged](docs/screenshot-desktop.png)

## Layout

| Directory | Layer | Depends on |
|---|---|---|
| `modules/core_engine` | Chord parsing, scale suggestion, reharmonisation, voicing analysis, solo-line reading, note-input abstraction. Pure C++17. | nothing |
| `modules/engine_api` | The engine's answers as JSON - one wire format, read by both shells. Pure C++17. | core engine |
| `web` | **The user interface.** One page, served on the web and hosted by the app, plus the WebAssembly build, the offline worker and the smoke test that drives the built page. | engine API (as JSON) |
| `app` | Platform shell: a webview showing `web/`, plus MIDI devices, the audio device and its electric piano, and file reading. | engine API, JUCE |
| `tests` | Engine unit tests (331), no JUCE, no third-party framework. | core engine, engine API |

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
on the right. Click a bar to move to it; click the bar you are already on for the question
the mode you are in is about.

The desktop app and the browser page are not merely alike: they are the same page. A
screenshot of one is a screenshot of the other, because there is one file. What differs is
what is behind it - native C++ in the app, WebAssembly in the browser - and which of the
two is answering is written in the colophon at the foot of the page.

**Chords** and **Solo**, at the top, are the two things you can practise against that one
chart. Chord practice asks whether the voicing you played says what the bar says. Solo
practice asks a different question of the same notes: where does each one sit. It is a
mode, not a second screen - the chart, the keyboard, your MIDI connection and the sound
all stay exactly where they were, and the bar you are on is the same bar in both.

### Chord practice

Play a chord on a MIDI keyboard or the on-screen keyboard and the dock says what it makes
of it: whether it is the chord the bar asks for, what is missing, what is outside, what is
clashing or muddy, and what would improve it. **Expecting Dm7** at the right of the dock
says which bar it is being checked against.

The **Practice** menu sets a voicing shape for the whole session - root position, shell,
rootless left hand, two-handed rootless, solo - and every voicing you play is then checked
against it, so a rootless voicing played during a root-position exercise is reported even
though the notes spell the chord. The same menu says how much colour **Show me one** should
put in what it plays: the base shape, or the same shape with the tensions. Pressing the
button again walks on to the next shape rather than repeating the last one. What it shows
goes under your hands rather than merely onto the screen, so it sounds, is analysed, and
can be named - the same path a played chord takes.

**Name it** asks the other question - not "is this the right chord for the bar" but "what
did I just play", with no chart involved. **Edit chart** types chords into bars directly,
**Reharmonise the tune** applies a plan to the whole chart at once, and when a voicing you
play turns out to spell a substitution rather than the written chord, **Write it into the
bar** keeps it.

Clicking the bar you are already on opens **its substitutions**, grouped from the closest
to the original harmony outwards, each with a difficulty tag and a rationale. Choosing one
rewrites the bar.

### Solo practice

![Solo practice: a take in progress, each bar scored, an enclosure landing](docs/screenshot-solo.png)

The keys do not latch here, because a line is played rather than held: each note sounds,
is read back - **E4 - the 9th, scale tone, in D Dorian** - and lets go. Every note is read
on its own, so a rolled double-stop is two notes each read where it sits rather than one
chord. Where chord practice writes *Expecting Dm7*, this mode writes **Expecting D Dorian** -
the scale you are actually being held to.

**A note outside the harmony is not called a mistake when you play it.** It is **open**:
you have started something, and at that instant nothing can know how it ends. A chromatic
approach, an enclosure and a passing tone are all outside by pitch and all three are the
line working; what tells them apart from a note that simply did not land is *where the line
goes next*. So the app does not guess. What it says instead is the thing that is true right
then - *a semitone up lands on D4, the root* - which is both more use than a verdict and
correct whichever way the line goes.

Then the next note usually decides it. Step home, or take the target from both sides first,
and the note is read again as an **approach note** that counts as landing. Play something
that lands somewhere else instead and it settles as **outside** right there. Only one thing
buys another note: a second outside note with room between the two for a target, because
that is an enclosure still in play and closing the first one would be calling a miss on a
phrase about to land. It can all happen across a barline, because running chromatically into
the next chord is one of the most idiomatic things in the idiom, and it works whether or not
a take is armed - a player trying things out is the one most likely to be experimenting with
chromatic notes, and is told at exactly the same moment an armed one would be.

The answer is about a note you have already played past, so it arrives somewhere you are
still looking. A chip in the dock holds the open note and changes colour with it - *Db4
wants D4* in grey while it is open, *Db4 landed on D4* in green, *Db4 never landed* in the
outside colour. And the key you played it on **stays lit** rather than fading like every
other note, until the line says what it was; then it relights in that colour for a moment
and goes out. Nothing else on the keyboard moves while a note is open, which is what makes
it catchable out of the corner of an eye.

One note can do both at once - play Eb, then F#, then G, and the G is the chromatic approach
the F# earned while the Eb is left having enclosed nothing - so the chip names both and each
key takes its own colour.

**An enclosure gets a colour of its own**, teal rather than the approach green. It is an
approach note by every measure the engine takes - it lands, it counts as colour, the score
has no opinion about it - but taking a note from both sides before you play it is deliberate
in a way a passing tone is not, and a reading that called the two the same thing would lose
the harder thing you did. The bar strip still counts it as an approach note, because that is
where four tiers are being counted rather than one note's story being told.

**Nothing is graded on a note while it is still open.** That is the practical half of it.
The score reads only the notes whose reading is final, so it no longer dips the moment you
reach for a chromatic approach and climb back two notes later when you land it - which read
as the app marking you down for a phrase it was about to approve of.

**Start a take** to be counted. While a take runs the dock carries a red line and the button
a pulsing dot, because a take counting on quietly is the one thing here that would be
annoying to find out about late. It reports the bar you are on and the take as a whole, and
walking to another bar does not end it - the target moves and the notes keep accumulating,
which is most of what soloing over a chart is. **Stop the take** freezes a summary: how the
whole thing divided up, and which bar pulled away from the rest. A note outside the scale is
*outside the scale*, never wrong.

Every bar you play over also carries its own verdict, **on the bar**: a slim four-part strip
in the same colours the dock uses - chord tone, scale tone, approach note, outside - in
proportion, and a **percentage** for how the bar went. It fills in as you play and stays
after you stop, so the bar that got away is one you can see at a glance down the chart rather
than one you have to read about underneath it. The exact counts are on the bar's tooltip, and
in its name for a screen reader.

The percentage is the one number in this app that is a judgement rather than a count, so it
is worth saying what it judges. It reads the notes whose reading is final, and nothing else.
Of those: chord tones, scale tones and approach notes all count as landing - the difference
between them is colour, not correctness. What is left as *outside* counts a quarter rather
than nothing, because a note that resolved into nothing may still have been the best note in
the line. What is left is balance, worth fifteen points at the very most: a line is chord
tones anchoring it and everything else colouring it, so a bar that never leaves the chord and
a bar that never touches it are both one-sided and are marked the same - and a bar of one or
two notes is not unbalanced, it is short. It is a reading of a bar, not a mark for a player;
nothing anywhere in here calls a note wrong.

Stopping the take also reads the line as a **line**, separately from where its notes sat.
Which bars went by without the line ever leaving the chord - *add some colour*, said about
the bar rather than about any note. Whether big leaps were followed by a step, which is what
fills the gap a leap opens. And whether the whole take stayed inside about a hand's width,
when the horn players it is all stolen from use the range. None of the three touches the
score: they are advice about how a line moves, the score is a reading of where its notes sat,
and mixing the two would make a number nobody could explain out of one that can be.

**Scale style**, in the Practice menu, is the vocabulary you are working out of: the modes,
melodic minor, harmonic minor, bebop, pentatonics and blues, whole tone and diminished, or
everything. It decides which scales a bar is offered and which one it is read against, so
practising the modes over a tune and practising bebop over the same tune are two different
exercises. A style with nothing for a bar - bebop over a diminished chord - shows the whole
catalogue instead and says so, rather than calling every note you play outside.

Clicking the bar you are already on opens **its scales**, narrowed to that style; choosing
one is not just something to look at, it is what the bar is read against from there on, and
it survives the bar being opened again. Between the style and that choice is where the
forgiveness in solo mode lives, and it is deliberate: reading against *every* scale that fits
a chord sounds more generous and in fact leaves nothing outside anything - over Cmaj7, G7 or
Bbmaj7 not one of the twelve notes comes back outside. **Which scale?** says which one you
are being held to, and puts it on the keyboard.

**Playing**, in the same menu, is **Static** or **In time**. Static is the default: the bar
you are on is the bar you chose, it stays there until you move, and nothing is counting
time.

**In time** puts a clock behind the chart. **Start a take** then counts a bar in and moves
the chart for you, a bar to the click, looping whatever range of bars you give it - arming
the take and starting the clock are one gesture, from the button in the dock or from the
**space bar**. Dots beside the button say where in the bar you are, one per beat of the
metre - gold while it is counting you in, then the downbeat in red - and the bar the clock is
on wears a line across its top. The take and the clock are the same take: the clock is
walking the chart instead of your mouse, and the engine cannot tell the difference, which is
the point. Everything else works exactly as it does static.

The **tempo and the metre are at the head of the chart**, where a lead sheet writes them,
rather than in the menu - they belong to the tune, not to the practice session, which is also
why an imported one brings its own. iReal Pro links and PDF lead sheets have always carried a
time signature and the readers have always pulled it out; until the head had somewhere to put
it, a waltz arrived as a waltz and was counted in four. The click counts the numerator, so
6/8 is six clicks in a bar rather than two. Changing the metre while it is rolling starts it
again in the metre you just chose, because beat times are counted from one origin at one
spacing and cannot be re-cut mid-flight.

The clock is the page's, not the engine's. `LineAnalyzer` has no time in it at all - that is
what makes it testable - so the transport moves the selected bar and the engine finds out the
same way it would if you had clicked. Beats are *booked* against a lookahead window rather
than ticked off an interval, because an interval drifts and a metronome that drifts is worse
than none.

One honest inequality between the shells: on the browser page the click is scheduled into
Web Audio at an exact time, so it is sample-accurate. In the desktop app the sound is native,
across an event bridge with no "play this at" argument, so the click is sent when it comes
due and arrives a message-thread hop later. Both read the same clock, so the chart and the
click agree in both - what the app gives up is a few milliseconds of jitter on the click.

What the transport does **not** do yet is make rhythm count. Nothing reads which beat a note
landed on, so an avoid note is still named rather than marked down, and a chord tone on beat
one reads the same as one on the and of four. That is the feature a clock unblocks rather
than one it includes.

### Both modes

Switching modes changes **the light, and the words**. Solo practice turns the paper down a
stop and cools it, the rose accent becomes slate, and the masthead reads *Jazz Learning App:
Solo* over a line about playing one - so which mode you are in is something you can feel
without reading the toggle, and something you can read without knowing what the toggle does.
The two mastheads and the two hints above the chart are written one on top of the other
rather than one replacing the other, so a wording that wraps to a different number of lines
cannot make the chart jump when you switch. Nothing moves and nothing is rebuilt - it is the
same room under a different lamp. The colours that mean something stay exactly as they are:
sage, gold, green, teal and rust say chord tone, scale tone, approach note, enclosure and
outside in both modes, and recolouring those would be changing the meaning rather than the
light. Whatever was under your hands is let go of on the way through, in either direction.

The keys on the on-screen keyboard **stay down** when clicked in chord practice, so a chord
is built one note at a time and held: they are a voicing you are holding rather than a piano
you are playing. Clicking a key again, or **Clear keys**, lets go. On touch several fingers
register as one voicing the same way. Any MIDI keyboard found at startup — or plugged in or
paired later — is opened automatically.

A **sustain pedal** works, on both shells and in both senses: the notes keep sounding after
the keys lift, and they keep counting as part of the chord. A voicing spread out under the
pedal — root, then the third, then the seventh, each key released before the next is struck —
is read as the chord it adds up to rather than as a run of single notes, which is how a
pianist actually plays one. Controller 64 from a hardware pedal and the on-screen **Sustain**
control are the same thing by the time anything downstream sees them, so the on-screen one is
there for the many people who have a keyboard but no pedal.

The Practice menu carries the sound bank: an **electric piano**, or silent. Both shells
synthesise the same voice rather than sampling it - one sine ringing another, with the
modulation dying away faster than the note, so the attack barks and the tail settles - the
page through Web Audio and the app through its own audio device. A mouse can only press one
key at a time, so **Play chord** (or the space bar) sounds every key currently down at once.
Connecting a MIDI keyboard widens the drawn keyboard to four octaves, and anything played
outside that widens it further, so a two-handed voicing is never partly off the end; both
shells do this. On the browser page the menu also connects a MIDI keyboard through the Web
MIDI API; that needs Chrome or Edge, and an embedded page may not be allowed to ask for
permission at all, in which case use the page in its own tab or the desktop app, which talks
to MIDI devices directly.

Stepping along the chart to check one bar after another never puts a dialog in front of the
keyboard - it takes a second click on the bar you are already on to open one - and the arrow
keys move between bars without taking a hand off the keys.

A first visit opens a short cheat sheet covering the things that cannot be guessed from
looking. There is one for each mode, because they explain different pages: arriving at solo
practice for the first time opens the solo half, which has more that cannot be guessed (a
take has to be armed, and the keys stop latching). Each appears once; the **?** beside the
Practice menu brings back the one for the mode you are in, and says so when you hover it.

**Import / export**, in the Practice menu, opens a chart that came from somewhere else and
writes the one on screen back out. Both shells read an iReal Pro link, the `.html` file
iReal Pro sends when you share a song, or a progression typed as `| Dm7 | G7 | Cmaj7 |`,
pasted in or picked as a file, and both put an `irealbook://` link back on the clipboard
that iReal Pro opens directly.

Two things are the browser page's alone, and both for the same reason - they need something
the JUCE shell has no library for. The page reads a **PDF lead sheet** (iReal Pro exports
one, and so does the page) using pdf.js; the app says so and points you at the link instead
of reading a PDF as gibberish. And **Print or save as PDF** prints the lead sheet alone,
menus and keyboard left off the page, which is the browser's print pipeline doing the work.
The chart reader itself is in the engine either way: what the app is missing is a way to get
text out of a PDF, not a way to understand one.

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
- **Soloing styles** — the scale catalogue grouped into the vocabularies a player actually
  practises out of, built from scale families rather than lists of names so that a shape
  added to the catalogue joins its style without anyone remembering to add it. The engine
  owns the list and both shells build their menu from it.
- **Reading a line** — `LineAnalyzer` takes notes one at a time rather than a chord at
  once, and reads each against the bar it landed in: a chord tone, a tone in the scale
  that bar is being read against, an approach note, or outside all of them. Every note
  also names its degree against the chord, outside ones included, because "a b9 over
  Cmaj7" says something a player can use and "outside" does not. A scale tone sitting a
  semitone above a chord tone — the 4th over a major seventh — is still a scale tone and
  is named as one to pass through rather than land on, because that is what a line does
  with it. It shares no code with the voicing analyser and should not: a voicing is a
  thing, a line is a stream.
- **A three-note window** — an outside note is not read as outside when it is played. It
  is *open*, and stays open only for as long as some pattern could still reach back and
  claim it: a chromatic approach and a passing tone are settled by the very next note, an
  enclosure by the one after. Then it becomes an approach note or outside, once, and never
  changes again. The engine also says which of the three gestures got it home, and what a
  still-open note would need — the nearest note a step away that would land it, root
  before chord tone before scale tone, semitones before tones — because at the moment of
  playing, that is the only thing that is true.
- **A take** — the window with a memory: it holds the notes from arming to disarming,
  across as many bars as you walk through, and reports the whole thing and each bar in it.
  Each bar gets a score out of 100 read over settled notes alone, and the whole take gets
  read as a *line* as well: bars that never left the chord, leaps not followed by a step,
  and a range that never left a hand's width. None of those three touches the score.
- **Input abstraction** — hardware MIDI and the on-screen keyboard emit identical events;
  `VoicingCollector` groups notes that arrive together into one voicing, so a rolled chord
  or three fingers landing at once both arrive as a chord rather than a stream of notes.

## Not in this POC

- **MusicXML / MuseScore import.** iReal Pro and PDF import both ship (see above); which
  further format comes next is still an open question in the design doc.
- **Audio/pitch-detection input**, deliberately out of scope for this phase.
- **Voicing library, ear training, progress tracking.** The analyser reports a per-voicing
  score, the feedback panel keeps a session average and a solo take now keeps its own
  numbers — between them, the hook progress tracking would build on. The metronome and the
  practice loop ship, as In time above.
- **Licks.** Solo mode tells you the scale; suggesting a *line* to play over a bar needs
  generated or curated patterns, rhythm and register, and is a feature of its own.
- **Rhythmic reading.** The clock exists and nothing reads it: landing chord tones on
  strong beats, and telling an avoid note passed through from one sat on, both need
  `LineAnalyzer` to be told where in the bar a note fell. It has no time in it today,
  deliberately, so that is a change to the engine's shape and should be designed first.
- **A metre that survives export.** The readers bring a time signature in; the iReal Pro
  writer does not put one back out, so a waltz imported and exported comes back in four.
  One line in `ChartFormats`, once someone wants it.
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
3. **Solo/improv feedback layer** — in, both static and in time: you arm a take and either
   walk the chart yourself or let the clock walk it. What is still open is rhythm, above.
4. **A dense, DAW-style desktop layout** — deferred; it would arrive as a fourth size
   class rather than a second UI.
