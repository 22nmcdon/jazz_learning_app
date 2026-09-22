<!-- The long "why" for solo practice. The load-bearing rules are summarised in
     CLAUDE.md, which is loaded every session; this file is where each of them
     is argued for, with the counts and the bugs that produced it. The rhythm
     grid this reads against is shared with comping — see docs/RHYTHM.md —
     and comping's own reasoning now lives in docs/COMPING.md.

     If you are changing solo practice, read this. If you are changing
     something else, CLAUDE.md's summary is enough and this file is why it
     says what it says. Do not let the two disagree: a rule moved here without
     a line left behind is a rule a session will not see. -->

# Solo practice

The same chart, the same notes, a different question: where does each one
sit. `LineAnalyzer` is the whole of the theory; `read()` is pure, and a *take*
is that with a memory and a window.

### Reading one note
- `LineAnalyzer::read()` reads one note against the bar's chord as **chord
  tone / scale tone / outside**, names its degree, and says which scale
  accounts for it. Pure, one note, no state — every rule decidable without a
  line lives here.
- **Forgiving is not the same as accepting everything.** Solo mode was
  specified to read a note against *every* scale that fits the chord, on the
  grounds that a player who chose a different valid scale hasn't made a
  mistake. Counted, that leaves **nothing outside anything**: Cmaj7, G7 and
  Bbmaj7 each come out 4 chord tones, 8 scale tones, 0 outside, and Dm7 has
  exactly one note it will not account for. Three tiers collapse into two and
  the feature stops saying anything. Against one scale the same chords read
  4/3/5. So the reading is against one scale, and the forgiveness lives in
  *which* one — `Options::chosenScale`, the scale the player picked from the
  Scales panel. Before widening a rule in the name of being generous, count
  what it leaves.

### The window: open, approach, outside
- **A note outside the harmony is open, not wrong, until the window has
  passed it.** This is the rule the rest of solo practice hangs off. A
  chromatic approach, an enclosure and a passing tone are outside by pitch
  and are the line *working*; nothing tells them apart from a note that
  didn't land until the note after arrives. So `play()` reads the new note,
  looks back over the two behind it promoting any it resolved, then
  *settles* every open note the line can no longer reach. A note outside the
  harmony comes back `NoteColour::unresolved` and becomes `approach` or
  `outside` once, for good — usually on the very next note. `read()` returns
  `outside`, because one note has no line around it to be waiting on.
- **`canStillBeReached()` decides when, and the rule is "could any pattern
  still promote this," not "have two notes gone by."** A note that lands
  somewhere else closes the one before it immediately, because the step
  patterns have had their chance and an enclosure needs that note to be
  outside too. Only a second outside note with room between the two — two to
  four semitones, so a target could sit between them a step from each —
  holds the verdict back a further note. Waiting a fixed two notes meant the
  one piece of bad news this reads arrived a note late in the commonest case
  of all.
- **Nothing is graded on an open note.** `score()` reads
  `LineStats::settled()`. Counting an open note as outside before the window
  decides made the score dip every time a player reached for a chromatic
  approach and climb back when they landed it — which reads as the app
  marking someone down for a phrase it's about to approve of. Do not let a
  percentage, a strip or a tally treat `unresolved` as a verdict.
- **The instant feedback names the resolution, not a mistake.**
  `LineNote::wantsToReach` is the nearest note a step away that would close
  it — root first, then chord tone, then scale tone, semitones before tones.
  "That was wrong" is a guess at that moment and often the wrong one; "a
  semitone up lands on D" is true whichever way the line goes, and it's the
  thing that would have helped. The bad news is delivered late instead, by
  `strandedByLastNote()`, which is the earliest it's honestly available.
- **Which gesture got a note home is recorded, and is not a tier.**
  `ApproachKind` is chromatic / passing / enclosure. All three land, all
  three count identically in `LineStats`, and the score has no opinion about
  which — but they aren't the same thing to play, and an enclosure is
  deliberate in a way a passing tone isn't. Rules try **most specific first**
  (enclosure, passing tone, chromatic approach) because a note can honestly
  answer to more than one, and a promoted note is never re-promoted: the
  second note of an enclosure *is* a chromatic approach on its own, and
  letting the simpler rule reach it first would lose the harder thing the
  player did.
- **Nothing that has settled is ever revisited.** A note that landed stays
  landed and a note left hanging stays hanging, so a reading never changes
  twice under a player who's watching it.
- **The window doesn't stop at the barline.** Running chromatically into the
  next chord is idiomatic, so a promotion can change a bar the player has
  already left — which is why `soloPlayNote` returns a `bars` array of
  everything that moved, not just the bar the note landed in. A shell
  reading only `bar` leaves the previous one drawing numbers that stopped
  being true.
- **One note can resolve one open note and strand another, in the same
  breath.** `resolvedByLastNote()` and `strandedByLastNote()` are not
  alternatives, and a shell must read both every time. Play Eb, then F#,
  then G: the G is the chromatic approach the F# earned, while the Eb is
  left having enclosed nothing.
- **It runs without a take.** Notes outside a take go into a three-note
  `recent` buffer, resolved and settled there but never counted. Someone who
  hasn't armed anything is the person most likely to be trying chromatic
  notes, and telling them those were misses is the lesson the whole window
  exists to stop — they're told at the same moment an armed player would be.
  The buffer trims *after* settling, or a note could drop off the front
  while still open and take its verdict with it.

### The take
- The take lives in the engine, reached through four entry points in
  `jazz::api` (`soloStartTake`, `soloSetBar`, `soloPlayNote`, `soloEndTake`) —
  the one stateful corner of that API, deliberately: the alternative is the
  shell resending every note played so far, which puts the take in the UI.
- **`endTake()` closes whatever is still open**, as outside: there will be no
  more notes, so the resolution isn't coming. A summary carrying "waiting to
  see" would be waiting for good, which is why every test that reads a
  colour back ends the take first.
- **A take marks the chart, not only the dock.** The page keeps bar stats
  itself from what every `soloPlayNote` already returns, then replaces the
  lot with the engine's own breakdown on `soloEndTake` so a long take can't
  drift.
- Either static or in time — the clock needed nothing new from the engine,
  because it moves the bar the same way a click does (see *In time* below).

### The score, and the shape of a line
- **The score is the only judgement in the engine, so every number in it is
  named.** `LineStats::score()` reads a bar 0–100 over settled notes: chord
  tones, scale tones and approach notes all land, an outside note is worth a
  quarter, and what's left is balance — worth fifteen points at most,
  fading in with the length of the bar. The constants live named at the top
  of `LineAnalyzer.cpp` rather than inside the arithmetic, because each is
  arguable, and the tests are where the argument is held: leaning off the
  chord costs what leaning onto it does, two notes aren't unbalanced, an
  outside bar still scores its quarter, and the result never leaves 0–100.
  Approach notes count as colour rather than chord tones for balance — the
  opposite of never leaving the chord.
- **A line has shape as well as content, and the shape doesn't touch the
  score.** The summary also reads how the line *moved*: bars it never
  coloured (`LineBar::neverLeftTheChord`, per bar rather than per N notes,
  because the bar is a boundary the player already feels), leaps not
  followed by a step, and a take that never left a hand's width. All three
  produce words, none produces points. Mixing motion into the score would
  make a number nobody could explain out of one that can be.
- **Nothing calls a note wrong.** A reading of a bar isn't a mark for a
  player, and the wording on the page has to keep saying so.

### Scale styles
- **Scale styles** (`ScaleStyle` in `Scale.h`) are the soloing vocabularies
  the Practice menu offers — the modes, melodic minor, harmonic minor, bebop,
  pentatonics and blues, whole tone and diminished, everything. Built out of
  `ScaleFamily` rather than lists of scale names, so a shape added to the
  catalogue joins its style by itself. A style with nothing for a chord
  falls back to the whole catalogue and says so; an unknown key widens
  rather than empties, so a key stored by another version is harmless.
- **The dock names the scale, and derives it the way it derives what it
  sends.** Solo practice writes `Expecting D Dorian` where chord practice
  writes `Expecting Dm7`, from `state.scales[state.chosenScale]` — the same
  expression that feeds `soloChosenScale()`, so it's the same answer
  `LineAnalyzer` will reach, fallbacks included. Asking the engine
  separately would be a second path to the same fact; guessing it would be
  theory in the page. It made a quiet bug loud once: reopening a bar used to
  reset the chosen scale to the top of the list while the engine kept
  reading against the old one, which nothing on screen could contradict
  until something on screen named it.

### In time — the transport
- The switch was a door with nothing behind it for as long as there was
  nothing behind it — half a transport is worse than none. Behind it now: a
  tempo, a count-in, the chart moving a bar to the click, and a loop over a
  range of bars.
- **The clock is the page's; the engine has none.** `LineAnalyzer` has no
  time in it, which is what makes it testable, so the transport calls
  `selectBar()` on each downbeat and the engine hears about it through
  `soloSetBar` exactly as it would from a mouse click. It can't tell the two
  apart, and that's the point — arming a take over a roll needed no new
  code. Nothing in the engine may start depending on when a note arrived.
- **Beats are booked, not ticked.** A `setInterval` at the beat drifts, and a
  metronome that drifts is worse than none, so every tick books the beats
  falling inside a lookahead window with times computed from one origin.
  Changing tempo mid-roll has to re-anchor that origin, or the next beat is
  spaced the new way from an origin that meant the old one.
- **The metre is the chart's, and can't be re-anchored the way tempo can.**
  Both the count-in and the bar arithmetic are counted in beats-per-bar, so
  a metre changed mid-roll would mean two bar lengths measured from one
  origin — it stops the clock and starts again in the metre just chosen.
  The value comes off the chart (`ChartFormats` has always read a time
  signature; it only reached the page once the sheet head had somewhere to
  put it). Read `transport.beats`, never a literal four — the dots, the
  count-in and the loop arithmetic all do.
- **Arming and rolling are one gesture.** Two buttons made two states
  nobody meant — a take over a chart that never moves, a chart rolling with
  nothing counting — so `toggleTake()` is both, and the space bar calls it
  (fenced out of fields where a space is a character, rather than fenced to
  `document.body`, because a take starts right after clicking a bar or the
  button — exactly where focus isn't). The take starts before the clock, so
  the transport's first `selectBar` reaches an engine that already has one.
- **The count-in is not bar one.** The chart doesn't wear the rolling mark
  until a real beat lands — a bar claiming to be current while the dots are
  still counting says the player is somewhere they aren't.
- **The click is the one place the shells are honestly unequal.** Scheduled
  into Web Audio at an exact time on the page; sent over the event bridge
  when due in the app (that bridge has no "play this at" argument), so it
  arrives a message-thread hop later. Both shells read the same clock, so
  chart and click agree in both. Don't paper over the difference by
  pretending to schedule in the app — if it matters, give the bridge a time.

### Reading where a note fell
The grid itself (24 ticks/beat, swing, strong beats) is shared with comping
and documented once in `docs/RHYTHM.md`. What's specific to solo practice:

- **A position is not a time.** `BarPosition` is a position in a bar the way
  `measureIndex` is a position in a chart. The shell owns the clock and
  converts a moment into one of these before the engine sees it, and back
  again to play it. Nothing here may start depending on when a note actually
  arrived.
- **It's an overload, not a defaulted argument.** There's no position that
  means "no position," and a made-up downbeat would be read as a real one.
  `soloPlayNote` signals it with a negative beat instead. A take played
  statically is read exactly as it was before any of this existed — every
  reading below is additive and silent without positions.
- **"Passed through" versus "sat on" is the whole reason the grid was
  wanted.** The same pitch against the same chord is what every bebop line
  is made of at speed and what sounds like a mistake when dwelt on. Only the
  rhythm separates them. Filled in behind, like `resolvesTo`, because it's a
  fact about the gap to the *next* note — measured **through the barline**,
  or every pushed note in the idiom reads as one the line sat on.
- **An approach note is left out of it.** It stepped home, which is the
  verdict that matters about it; saying it was also passed through adds
  nothing and would count the line's best notes among the ones being asked
  about.
- **None of it touches the score**, and that's the rule this whole file is
  built on, not an omission. Where a note sits in a bar doesn't make it a
  better or worse note, and the moment placement moved the number the number
  would stop being explainable. There's a test that says so: the same notes
  score the same whether or not positions were sent.
- **Whether that's final was an open question, and it's closed: it is.** The
  question was put as "rhythm produces words, is that the answer or a stage",
  and answered as neither a preference nor a matter of nerve. It turns on one
  test — **is there a standard the player chose?** Comping has one, a
  `CompStyleDefinition` picked off a menu, which is exactly why placement
  scores there and why that isn't a contradiction. A chart names the chord and
  has never said where in a bar a note belongs, so there is no such standard
  here and a number would be one the engine made up and then marked somebody
  against. What would reopen it is that standard coming to exist — a rhythmic
  vocabulary a player deliberately picks to be held to — not a cleverer way of
  counting. Until then, words.

### Chords in a line
Players comp behind themselves and solo in block chords, and the most
idiomatic move either makes is sliding a whole voicing chromatically into
the next one. G13, the same shape with two voices pushed down a semitone to
make a G7alt, then Cmaj7:

```
G13      F3  A3   B3  E4
G7alt    F3  Ab3  B3  Eb4      Ab and Eb are outside G mixolydian
Cmaj7    E3  G3   B3  D4       and both step into it - Ab to G, Eb to D
```

Read as a stream of single notes, that was `CSCS CXCX CCCS`: two notes that
went nowhere. The note *after* the Ab is the chord's own B, four semitones
away, so the window tried its patterns against a note that was never a
resolution and couldn't be one. It now reads `CSCS CACA CCCS`, with nothing
outside — which is what it sounds like.

- **An attack is the unit, not a note.** Notes struck together are one
  attack; they neither resolve nor strand one another, and the window runs
  attack to attack. Every rule is the rule it always was with "the note
  before" widened to "any note of the attack before," so each voice finds
  its own way home and a line with nothing struck together reads exactly as
  it did — every test written before any of this existed passed untouched.
- **The shell says which notes were struck together, because nothing else
  can.** The pitches don't tell a chord from a line — the same notes are
  both, which is the control case in the tests. This isn't the clock coming
  back: "struck together" is a fact about a gesture, the way a measure
  index is a fact about a bar, and the engine still has no idea what a
  second is.
- **Nothing waits for the chord to be finished.** The flag says a note
  *joined* an attack, which is knowable the instant it arrives, so no shell
  buffers and no note is read later than it was. The one cost: the window
  can't tell a chord's first note from an ordinary next note, so it judges
  on the first and takes the judgement back when the rest of the same chord
  answers it — the only exception to *nothing settled is ever revisited*,
  and the rule it leaves is the one that was meant.
- **So the two lists are cleared per attack, not per note** — otherwise a
  shell reading them after each note of a chord would show "Ab3 never
  landed" for the twenty milliseconds between the voicing's lowest note and
  the one actually resolving it.
- **Forty milliseconds, and MIDI only.** Held keys are the obvious signal
  and the wrong one — a legato line holds the note before too. Time is
  right, and the window sits between two bad mistakes: wider and sixteenths
  at 240 (62ms apart) read as chords and stop resolving each other;
  narrower and a rolled chord comes apart again. A pointer plays one key at
  a time however fast it's clicked, so the on-screen keyboard never joins an
  attack at all.
- **An attack does not cross a barline.** Two notes read against different
  bars were played against different chords whatever the shell believed —
  grouping them would stop each resolving the other, exactly wrong for
  running into the downbeat of the next chord.
- **A note struck with three others is still one note.** Counted, coloured,
  scored and positioned like any other; `chordsPlayed` and
  `chordVoicesResolved` exist so the summary can say back what the player
  was doing, not so it can weigh it. The one rhythmic adjustment: a chord
  isn't passed through by its own notes — what says how long the line
  stayed there is the next thing *struck*.

### Reading the chord itself
Everything above is the line reading applied to a gesture that happens to have
several notes in it, and it's complete — every note of a voicing is coloured,
counted and scored exactly as the same note played alone would be. What it
can't reach is the chord, because **a chord is not a fact about any of its
notes**. Eb Gb A C over Dm7 is an Ebdim7 passing through; Eb on its own over
Dm7 is a b9, and no reading of that Eb, however correct, gets to the first
sentence.

So `LineAnalyzer::chordSoFar()` reads the attack as a chord, and `TakeSummary`
carries every chord of the take.

- **Nothing new reads a chord.** The shape comes from
  `VoicingAnalyzer::classify`, whether the notes carry the bar's chord from
  `VoicingAnalyzer::analyse`, and what they spell instead from
  `ChordIdentifier`. `LineAnalyzer`'s own header says a voicing is a thing and
  a line is a stream; the way to keep that true is for the chord half never to
  grow its own idea of what a shell voicing is. `readChord` arranges three
  readings and writes a sentence — that's the whole of it.
- **The top note is the line.** A block-chord soloist harmonises downwards from
  the melody, so the reading leads with the highest note and the rest is what
  was put under it. Read the other way round it says the same thing about a
  voicing whichever of its notes the player was actually singing, which is the
  half they came for. Its colour and degree are taken from the reading that
  note already has rather than worked out twice.
- **`saysTheChord` is not a pass mark**, and the gap between it and
  `chordsPlayed` is not an error rate. The chords *between* the chart's own are
  most of what block-chord playing is made of, so a voicing that doesn't spell
  the bar gets named — `spelled` is what it was — rather than marked. Both
  numbers are on the summary so it can say which of the two things a take was;
  neither touches the score, and **no chord is scored anywhere**. Same rule as
  rhythm and voice-leading, one reading further out.
- **Read when struck, not when summarised.** Reharmonise-as-you-play moves a
  bar's symbol mid-take, so a summary re-reading the take's first voicing
  against the bar's *current* chord would be rewriting history to match a
  decision made afterwards — the same reason `setOptions` leaves the notes
  already played with the reading they were given. Each chord is read against
  what was on the stand when it was struck, and kept.
- **It grows with the chord rather than waiting for it.** Four keys arrive as
  four calls and nothing buffers them, so the reading goes two notes, three,
  four in front of the player — and replaces its own entry each time, so one
  voicing leaves one chord behind it and not three.
- **No examples offered.** The voicing analyser suggests idiomatic
  alternatives when a voicing could be better, which is the right thing to say
  to someone drilling a shape and the wrong thing to say to someone mid-line:
  they aren't trying to play the chart's chord, they're playing over it.
- **No bar, no reading.** A set of notes has a name of its own, but this is a
  reading of a chord *over* something. "How does this sit against nothing" is
  not a question with a chord in it.
