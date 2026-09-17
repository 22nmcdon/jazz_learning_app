<!-- The long "why" for solo practice. The load-bearing rules are summarised in
     CLAUDE.md, which is loaded every session; this file is where each of them is
     argued for, with the counts and the bugs that produced it.

     If you are changing solo practice, read this. If you are changing something
     else, CLAUDE.md's summary is enough and this file is why it says what it
     says. Do not let the two disagree: a rule moved here without a line left
     behind is a rule a session will not see. -->

# Solo practice

The same chart, the same notes, a different question: where does each one sit.
`LineAnalyzer` is the whole of the theory; `read()` is pure, and a *take* is that with a
memory and a window.

### Reading one note
- `LineAnalyzer::read()` reads one note against the bar's chord as **chord tone / scale
  tone / outside**, names its degree, and says which scale accounts for it. Pure, one
  note, no state - every rule that can be decided without a line lives here.
- **Forgiving is not the same as accepting everything.** Solo mode was specified to read a
  note against *every* scale that fits the chord, on the grounds that a player who chose a
  different valid scale has not made a mistake. Counted, that leaves **nothing outside
  anything**: Cmaj7, G7 and Bbmaj7 each come out 4 chord tones, 8 scale tones, 0 outside,
  and Dm7 has exactly one note it will not account for. Three tiers collapse into two and
  the feature stops saying anything. Against one scale the same chords read 4 / 3 / 5. So
  the reading is against one scale and the forgiveness lives in *which* one -
  `Options::chosenScale`, the scale the player picked out of the Scales panel. Before
  widening a rule in the name of being generous, count what it leaves.

### The window: open, approach, outside
- **A note outside the harmony is open, not wrong, until the window has passed it.** This
  is the rule the rest of solo practice hangs off. A chromatic approach, an enclosure and
  a passing tone are outside by pitch and are the line *working*; nothing tells them apart
  from a note that did not land until the note after arrives. So `play()` reads the new
  note, looks back over the two behind it promoting any it resolved, then *settles* every
  open note the line can no longer reach. A note outside the harmony comes back
  `NoteColour::unresolved` and becomes `approach` or `outside` once, for good - usually on
  the very next note. `read()` returns `outside`, because one note has no line around it
  to be waiting on.
- **`canStillBeReached()` decides when, and the rule is "could any pattern still promote
  this", not "have two notes gone by".** A note that lands somewhere else closes the one
  before it immediately, because the step patterns have had their chance and an enclosure
  needs that note to be outside too. Only a second outside note with room between the two
  - two to four semitones, so a target could sit between them a step from each - holds the
  verdict back a further note. Waiting a fixed two notes meant the one piece of bad news
  this reads arrived a note late in the commonest case of all.
- **Nothing is graded on an open note.** `score()` reads `LineStats::settled()`. Counting
  an open note as outside for the notes before the window decides made the score dip every
  time a player reached for a chromatic approach and climb back when they landed it, which
  reads as the app marking someone down for a phrase it is about to approve of. Do not let
  a percentage, a strip or a tally treat `unresolved` as a verdict.
- **The instant feedback names the resolution, not a mistake.** `LineNote::wantsToReach`
  is the nearest note a step away that would close it - root first, then chord tone, then
  scale tone, semitones before tones. "That was wrong" is a guess at that moment and often
  the wrong one; "a semitone up lands on D" is true whichever way the line goes, and it is
  the thing that would have helped. The bad news is delivered late instead, by
  `strandedByLastNote()`, which is the earliest it is honestly available.
- **Which gesture got a note home is recorded, and is not a tier.** `ApproachKind` is
  chromatic / passing / enclosure. All three land, all three count identically in
  `LineStats`, and the score has no opinion about which - but they are not the same thing
  to play, and an enclosure especially is deliberate in a way a passing tone is not. The
  rules are tried **most specific first** (enclosure, passing tone, chromatic approach)
  because a note can honestly answer to more than one and a promoted note is never
  re-promoted: the second note of an enclosure *is* a chromatic approach on its own, and
  letting the simpler rule reach it first would lose the harder thing the player did.
- **Nothing that has settled is ever revisited.** A note that landed stays landed and a
  note left hanging stays hanging, so a reading never changes twice under a player who is
  watching it.
- **The window does not stop at the barline.** Running chromatically into the next chord
  is idiomatic, so a promotion can change a bar the player has already left - which is why
  `soloPlayNote` returns a `bars` array of everything that moved, not just the bar the
  note landed in. A shell reading only `bar` leaves the previous one drawing numbers that
  stopped being true.
- **One note can resolve one open note and strand another, in the same breath.**
  `resolvedByLastNote()` and `strandedByLastNote()` are not alternatives, and a shell must
  read both every time. Play Eb, then F#, then G: the G is the chromatic approach the F#
  earned, while the Eb is left having enclosed nothing. The page took whichever list had
  something in it first, so the other note's key was never let go of and sat lit and
  breathing for the rest of the take, asking a question that had been answered.
- **It runs without a take.** Notes outside a take go into a three-note `recent` buffer,
  resolved and settled there but never counted. Someone who has not armed anything is the
  person most likely to be trying chromatic notes, and telling them those were misses is
  the lesson the whole window exists to stop - and they are told at the same moment an
  armed player would be. The buffer is trimmed *after* settling, or a note could drop off
  the front while still open and take its verdict with it.

### The take
- The take lives in the engine, reached through four entry points in `jazz::api`
  (`soloStartTake`, `soloSetBar`, `soloPlayNote`, `soloEndTake`). They are the one
  stateful corner of that API, and deliberately: the alternative is the shell resending
  every note played so far, which puts the take in the UI.
- **`endTake()` closes whatever is still open**, as outside: there will be no more notes,
  so the resolution is not coming. A summary carrying "waiting to see" would be waiting
  for good, which is why every test that reads a colour back ends the take first.
- **A take marks the chart, not only the dock.** Every bar played over carries a four-part
  strip in the dock's own colours, in proportion, so which bar went wrong is a glance down
  the chart rather than a read of a panel. The page keeps those numbers itself, from the
  bar stats every `soloPlayNote` already returns, and replaces the lot with the engine's
  breakdown on `soloEndTake` so a long take cannot drift.
- Static, with no clock. A tempo-driven version would drive `setTarget()` from one and
  need nothing else from the engine.

### The score, and the shape of a line
- **The score is the only judgement in the engine, so every number in it is named.**
  `LineStats::score()` reads a bar 0-100 over settled notes: chord tones, scale tones and
  approach notes all land, an outside note is worth a quarter, and what is left is
  balance, worth fifteen points at most and fading in with the length of the bar. The
  constants live at the top of `LineAnalyzer.cpp` with names rather than inside the
  arithmetic, because each is arguable - and the tests are where the argument is held:
  that leaning off the chord costs what leaning onto it does, that two notes are not
  unbalanced, that an outside bar still scores its quarter, and that the result never
  leaves 0-100. Approach notes count as colour rather than as chord tones for the balance
  term - they are the opposite of never leaving the chord.
- **A line has shape as well as content, and the shape does not touch the score.** The
  summary also reads how the line *moved*: bars it never coloured
  (`LineBar::neverLeftTheChord`, per bar rather than per N notes, because the bar is a
  boundary the player already feels and any note count would be invented), leaps that were
  not followed by a step, and a take that never left a hand's width. All three produce
  words, none produces points. Mixing advice about motion into the score would make a
  number nobody could explain out of one that can be.
- This sits alongside a rule the rest of the file keeps: **nothing calls a note wrong**. A
  reading of a bar is not a mark for a player, and the wording on the page has to keep
  saying so.

### Scale styles
- **Scale styles** (`ScaleStyle` in `Scale.h`) are the soloing vocabularies the Practice
  menu offers - the modes, melodic minor, harmonic minor, bebop, pentatonics and blues,
  whole tone and diminished, everything. They are built out of `ScaleFamily` rather than
  lists of scale names, so a shape added to the catalogue joins its style by itself. A
  style with nothing for a chord falls back to the whole catalogue and says so; an unknown
  key widens rather than empties, so a key stored by another version is harmless.
- **The dock names the scale, and derives it the way it derives what it sends.** Solo
  practice writes `Expecting D Dorian` where chord practice writes `Expecting Dm7`, and
  the name comes from `state.scales[state.chosenScale]` - the same expression that feeds
  `soloChosenScale()` and so the same answer `LineAnalyzer` will reach, fallbacks
  included. Asking the engine for it separately would be a second path to the same fact
  and a second thing to go stale; guessing it would be theory in the page. It also made a
  quiet bug loud: reopening a bar used to reset the chosen scale to the top of the list
  while the engine kept reading against the old one, which nothing on screen could
  contradict until something on screen named it.

### In time — the transport
- The switch was a door with nothing behind it for as long as there was nothing behind it,
  on the grounds that half a transport is worse than none. What is behind it now is a
  clock: a tempo, a count-in, the chart moving a bar to the click, and a loop over a range
  of bars.
- **The clock is the page's and the engine has none.** `LineAnalyzer` has no time in it,
  which is what makes it testable, so the transport calls `selectBar()` on each downbeat
  and the engine hears about it through `soloSetBar` exactly as it would from a mouse
  click. It cannot tell the two apart, and that is the point: arming a take over a roll
  needed no new code at all. Nothing in the engine may start depending on when a note
  arrived.
- **Beats are booked, not ticked.** A `setInterval` at the beat drifts, and a metronome
  that drifts is worse than none, so every tick books the beats falling inside a lookahead
  window with their times computed from one origin. Changing the tempo mid-roll therefore
  has to re-anchor that origin - otherwise the next beat is spaced the new way from an
  origin that meant the old one, and the whole line jumps.
- **The count-in is not bar one.** The chart does not wear the rolling mark until a real
  beat lands. A bar claiming to be current while the dots are still counting says the
  player is somewhere they are not, which is the whole thing a count-in exists to prevent.
- **The click is the one place the shells are honestly unequal.** Scheduled into Web Audio
  at an exact time on the page; sent over the event bridge when due in the app, because
  that bridge has no "play this at" argument, and so arriving a message-thread hop later.
  Both shells read the same clock so chart and click agree in both. Do not paper over the
  difference by pretending to schedule in the app - if it matters, give the bridge a time.
- **Rhythm still counts for nothing.** The transport moves bars; no reading anywhere knows
  which beat a note fell on. Landing chord tones on strong beats, and telling an avoid note
  passed through from one sat on, are what a clock unblocks - not what it includes.
