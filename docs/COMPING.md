<!-- The long "why" for comping, and where the plan actually got to. The
     load-bearing pieces are summarised in CLAUDE.md, which is loaded every
     session; this file is where each is argued for. Shares its rhythm grid
     with solo practice (docs/RHYTHM.md) and its voicing shapes with chord
     practice (idiomaticVoicings / VoicingAnalyzer) — don't let this file and
     those diverge on the pieces they hold in common.

     If you are changing comping, read this. If you are changing something
     else, CLAUDE.md's summary is enough and this file is why it says what it
     says. -->

# Comping — the band behind the soloist, and the exercise

`README.md` describes what comping sounds like and does; this is why it's
built the way it is, and how much of the original seven-step plan is actually
done.

Comping is two things now: a band that plays under a soloist, which is most of
this file, and an **exercise** — chord practice's *In time*, where you do the
comping and the same style data reads the other way round. The second half of
this file is that, starting at *Comping as an exercise*.

### The split: which notes vs. when
`compingVoicing()` (and `compPlan()`) answer *which notes*, and have no idea a
clock exists. Every question of *when* belongs to the page's transport, which
already existed for the click. A comp with its own rhythm generator in the
engine would smuggle time into `LineAnalyzer`'s module by the back door — the
same boundary that keeps the rest of the engine testable.

### `CompStyleDefinition` is the one artifact
The generator, the evaluator seam (`fitsStyle()`), and the menu all read one
definition (`Comping.h`). A style described in one place and re-described in
another is how the two drift — the same reason `scaleStyles()` lives in the
engine rather than in a shell.

- **A slot is written the way a player describes one.** `beat` empty means
  every beat (four-to-the-bar is one slot, in any metre); negative counts
  back from the end (so "the and of four" is the same slot in three). A slot
  naming a beat the metre doesn't have says nothing, which is more honest
  than folding it onto one that exists.
- **The shape was proved against styles that genuinely differ** — dense
  four-to-the-bar against sparse Basie, and a ballad whose feel is triplets
  rather than eighths — rather than fitted to one style and generalized
  afterward. A style the shape can't express is a sign the shape is wrong,
  not the style.
- **What it doesn't have yet: hit duration.** A hit rings until the next one
  stops it, whatever the style — the one bullet of the agreed shape with no
  field. Real difference between a Basie punch and a ballad's sustain, and
  the first thing to add if this shape reopens.

### What it plays: voicing choice, not a new shape
The shapes are the same two-handed rootless shapes chord practice already
offers (`idiomaticVoicings`). What comping adds is *which one, in which
register*, given where the hands just were: the search is over both shapes
across an anchor window, scored by distance from `previousNotes`, tie-broken
back to the natural anchor. Dm7 → G7 comes out F3 C4 E4 B4 into F3 B3 E4 A4:
two notes held, two moved a semitone.

- **Voice leading alone drifts.** "Nearest voicing to the last one" has no
  opinion about register, so a progression that keeps rising takes the hands
  up with it until nobody actually comps there. The regression test for this
  is a tune that climbs, not a ii-V-I (a ii-V-I would pass with no window at
  all).
- **The window is the style's, and it wasn't always.** `compingVoicing()` took
  a two-argument form that bounded only its *anchor*, to this file's own idea
  of a comping register — while `CompStyleDefinition::lowestNote`/`highestNote`
  sat there with **no reader at all**. An anchor is a voicing's bottom note and
  a two-handed voicing reaches about two octaves above it, so the generator
  played three semitones under Basie's own floor of 48 and nothing noticed
  until the evaluator read those fields. There is now an overload taking a
  stated window, and `compPlan()` passes the style's; the sweep runs to
  `highestNote - twoHandedReach`, because an anchor higher than that cannot fit
  under the ceiling anyway. That relation is also what keeps the two-argument
  form byte-identical to what it always returned.
- **The previous voicing lives on the page, not the engine** — same reason
  the take is the one stateful corner of `jazz::api`. The page already has to
  know what it played in order to have played it; a comp that remembered its
  own last chord would be a second copy of that state for no gain.

### The generator: planned ahead, seeded
`compPlan()` plans a range of bars in one pass, not beat by beat — a hit that
anticipates the next bar has to know what that chord is, and a voicing has to
be led from the one before, neither of which is knowable in the moment it's
played. Planning ahead also removed the page's promise chain: with the whole
plan in hand, there's nothing left to race.

- **Seeded, with the bar mixed in.** Same seed → same comp, note for note.
  Planning bars 4–7 alone gives the same four bars as planning 0–7 and taking
  the tail, so a loop coming round again is the same band, not a different
  one each chorus. The hash is hand-rolled because `std::mt19937`'s
  *distributions* aren't specified across standard libraries — a plan that
  differed between browser and app would be two bands playing.
- **It varies, because a style is a figure and not a loop.** `variation` is how
  often the band steps off its own slots onto the feel's grid — the same rule the
  evaluator reads, pointed at the band. Without it the Charleston picked from
  three slots at 95, 90 and 30, so eight bars of one chord came out as very
  nearly the same two chords eight times over; with it, six distinct figures.
  **It sits below the style's lightest slot on purpose**: the density trim takes
  the heaviest candidates, so a variation weight above the figure's own would
  quietly replace the figure with the vocabulary and the band would stop sounding
  like the style it was asked for. Four-to-the-bar has none, and its feel is the
  beat rather than the eighth — it plays every beat and nothing else, which is
  the whole of what it is.
- **The density trim drops by weight, not by position.** It used to sort by
  position and resize, keeping the *earliest* hits — a front-loading bias that
  got much worse once the whole vocabulary was on offer. Stable, so equal weights
  keep the order they were offered in and a plan is the same everywhere;
  `std::sort` promises nothing about ties, which is the same reason the hash is
  hand-rolled.
- **Engine calls are chained, not raced.** Each bar is booked a bar ahead. If
  two bars were awaited concurrently instead, both would lead from the same
  previous voicing and the second would jump — precisely what the anchor
  window exists to prevent. One promise chain, so calls resolve in the order
  they were booked.

### `fitsStyle()`: the invariant, checked both ways
Everything the generator plays for a style must pass the same "is this in
style" test the evaluator uses to grade a player — the same trap
`idiomaticVoicings` and `VoicingAnalyzer` are held out of on the chord-practice
side. Anticipation is checked one way only: a hit that pushed must come from a
slot that pushes, but a slot that pushes may honestly produce a hit that
didn't, because the last bar of a range has no next chord to pull forward.

**It asks about the style's vocabulary, not its figure.** The narrow question —
is this one of the style's own slots — is `slotAt()`. The two parted company when
the band learned to vary: `compPlan()` plays its figure most of the time and the
feel's grid the rest, so holding it to its slots alone would have made it fail
its own test. Inside the bar, too: `onTheGrid()` knows nothing of the metre, so a
beat the metre hasn't got would otherwise pass on the strength of its tick.

`fitsStyle()` gained `slotAt()` underneath it — the one answer to "which slot is
this", so the generator, this and the evaluator cannot disagree — and stayed
about a *generated* hit. A
`CompHit` carries two things it already knows, the chord it voices and whether
it pushed, and for a hit somebody played both of those are **answers** rather
than inputs. A signature taking both kinds would mean two different things
about its own arguments, which is why `PlayedHit` is a type of its own.

**An invariant that only ever asserts "yes" is satisfied by a function that
always says yes.** The negative direction went untested for as long as this
existed: a hit on a position the style never offers, a push from a slot that
doesn't push, a slot the metre hasn't got. It is tested now, and so are
register and density — over four styles, 24 seeds, sixteen bars of a wide
chord vocabulary and three metres. That sweep is what caught both bugs in this
file.

### The walking bass
`walkingBass()` is its own call and its own plan — not a field on
`CompStyleDefinition` — because it's one note to the beat and it walks
whatever the piano is doing. A change of comping style must not make the bass
player start over.

- **Built in *runs*, not beat by beat.** A run is a chord arriving and the
  beats before the next one does. Written beat by beat the line has nothing
  to aim at — "the nearest chord tone" walks straight back where it came from
  (the first version played D, C, D, D over one bar of Dm7). A run knows its
  root and the approach it has to reach by its last beat; everything between
  is travel.
- **Three rules, in order:** the root lands on the beat the chord arrives
  (the one note the line isn't free about — it states the harmony); the beat
  before a change leads into the next root (a semitone either side, or a
  fifth); chord tones fill the rest, travelling toward that approach.
- **Bounded to a real bass's compass** (`lowestBassNote`/`highestBassNote`) —
  voice leading left free climbs off the instrument inside a chorus.

### Two channels, never the player's own voices
Comping E4 under a soloist playing E4 has to be two voices in both shells, or
a note-off from either one stops a note the other is still sounding. The
browser keeps `audio.compVoices` beside `audio.voices`; the app flags
`Voice::comping`/`Voice::walking`, which is why `noteOff`, the pedal, and
`allNotesOff` (the player's panic button) all skip them — panicking should
not make the band stop playing. The bridge carries `"comp"` rather than
reusing `"chord"`, whose first act is to silence everything.

### The band's instruments are recordings
`assets/` holds them because both shells want them: the app compiles the WAVs
in via `juce_add_binary_data`; `web/build.sh` copies the same files beside
the page, which fetches them lazily (a visitor reading a chart shouldn't pay
for three recordings, and in the app the sound is native and already in the
binary — the service worker caches whatever it does fetch, so a recording
used once is there offline after).

- **One note per instrument, pitched by playing it faster or slower.** This
  is a practice app's band, not a sampler — a single well-recorded note
  stretched over two octaves beats a sine wave, at the cost of the far ends
  being a little short and a little wrong (part of why the walking line
  stays bounded to a real bass's compass). The root note each was recorded
  at is written down beside the file in both shells — get it wrong and the
  whole instrument transposes.
- **A sampled bank and a synthesized one are one registry.** `voiceFor()`
  hands back the same shape whether a bank builds a voice from oscillators or
  names a file and a root note, so the comp, the bass, and the player's own
  keys schedule identically. Adding an instrument is a table entry plus a
  file in `assets/`.
- **A sampled voice's envelope only shapes attack and release** — decay is in
  the recording; running the synth's decay over it too fades the note out
  twice.

## Comping as an exercise

The same style data, read the other way round: **chord practice's *In time***.
The chart rolls, a bass player walks under you, the piano stands down — it's
your instrument now — and every chord you strike is read twice over. What the
notes said about the bar is the question chord practice always asked; the clock
adds the one that needs it.

### Not a third mode, and that decision was reversed
Step 6 of the plan asked where the exercise lives, and the answer recorded for
a long time — here and in CLAUDE.md — was **a third mode**, on the grounds that
scoring a player's own comping is "neither of the questions the two modes ask".

That didn't survive the **Static / In time** axis solo practice already had. In
time isn't a different mode there: it's the same mode with a clock, adding
readings that are silent without one. Which is exactly what comping is to chord
practice — the voicing half *is* `VoicingAnalyzer`, on the same chart, in the
same dock, and the clock adds where the chord landed.

What that saved is precisely what the old "before you start" note warned a third
mode would cost. `state.mode` stays two-valued, so there's no third palette
block, no third cheat-sheet flag, no third `.mode-stack` sibling and no third
pill in a switch that is `overflow: hidden` at 430px. `data-mode` stays a strict
single-value compare, which also left `web/smoke-test.mjs`'s
`[data-mode='solo']` selectors working untouched. And it answered the question
the third-mode design had no good answer to — what the bar dialog shows while
comping — for free: substitutions, because a comper reharmonises.

Two rules narrowed rather than disappearing, and both got better for it:

- *"Comping belongs to solo practice only"* became **the generated piano never
  sounds under someone being asked to play chords**. True in both modes, and it
  leaves the walking bass free to play under you — which is what makes a doubled
  root worth mentioning at all.
- *"The keys latch"* became chord practice **static**. In time they don't: a
  comp is struck, not held.

### The style is a figure, not a fence

The first version of this reading took a style's slots as the set of places a
player might put a chord. That was wrong, and measurably so. Over the eight
positions a swing comper actually uses in a bar — the four beats and the four
swung ands — here is what the catalogue accepted:

| | four | basie | charleston | ballad |
|---|---|---|---|---|
| `1` | in style | in style | in style | in style |
| `1&` | **OFF** | **OFF** | **OFF** | **OFF** |
| `2` | in style | OFF | **OFF** | OFF |
| `2&` | OFF | in style | in style | OFF |
| `3` | in style | OFF | **OFF** | in style |
| `3&` | **OFF** | **OFF** | **OFF** | **OFF** |
| `4` | in style | OFF | **OFF** | OFF |
| `4&` | OFF | in style | in style | OFF |

The Charleston accepted three of eight, and **no style in the catalogue accepted
the and of one or the and of three at all** — both bread-and-butter comping. A
player reporting it put it plainly: *"when I am comping over a swing tune I don't
only hit on the 1 and the 2&2/3 like Charleston does."* Their example, one and
the and of one, scored nought for placement in every style shipped.

A `CompSlot` says where **the band** puts its chords. It never said anything
about where a player may put theirs, and reading it as though it did is a much
narrower claim than the data makes. So placement has three tiers, and the
standard is the style's **feel**:

- **`theFigure`** — one of the style's own slots. What the band would play here.
- **`idiomatic`** — on the grid the feel implies (`onTheGrid`): the style's
  vocabulary rather than its figure. Eighths for an eighth feel, the three notes
  of the beat for a triplet one.
- **`offStyle`** — off that grid altogether.

The first two score identically. What the figure contributed is said in words,
because a comper who varies is not making a mistake and the number had been
saying they were.

#### Counting what that leaves outside

*Before widening a rule in the name of being generous, count what it leaves* —
the lesson solo practice paid for, where reading a note against every scale that
fit left nothing outside anything. So, counted. `positionFromBeats` snaps a
played moment to ticks `{0, 6, 8, 12, 16, 18}`, folding 16 onto 12 when swinging.

| feel | accepted | reachable | still outside |
|---|---|---|---|
| eighth, swinging | `0, 12` | `0, 6, 8, 12, 18` | **3 of 5** |
| eighth, straight | `0, 12` | all six | **4 of 6** |
| triplet (ballad) | `0, 8, 16` | all six | **3 of 6** |

It does not collapse. Sixteenths stay outside an eighth feel, and a straight
eighth stays outside the ballad's triplet feel — which is the real "you're
playing this ballad like a swing tune" mismatch, and something the old reading
could not express at all. Four-to-the-bar is counted in *beats*, so it still
accepts only the four downbeats: an "and" in Freddie Green's part is not that
style played loosely, it is a different style.

### Where a note falls doesn't score a solo, and here it scores a comp

This is the one place the repo argues against a rule it states elsewhere, so the
argument is written out rather than assumed. CLAUDE.md says, of solo practice,
**"rhythm produces words, never points"**. That bullet is now marked as solo
practice's, and this is why.

A solo line has no stated standard for placement. The chart says which chord and
never says where a note belongs in the bar, so a number for placement would be
the engine inventing a standard and then marking a player against it. That's why
`LineAnalyzer`'s score reads nothing rhythmic, and why a line's shape produces
words.

A comp has one, and the player picked it off a menu. A `CompStyleDefinition` is
a written-down statement of where the hits go, how many there are and what
register they sit in, and the generator is already held to it in both
directions. Scoring a player against the same slots the band is held to isn't a
new judgement — it's the same act as `VoicingAnalyzer` scoring a voicing against
the symbol the chart wrote, or `Options::chosenScale` holding a player to the
scale they chose. Take the style away and there's nothing here to score, which
is why a verdict always names one.

And the line holds on the other side. Everything a style does **not** pin down
still produces words: how long a chord rang, whether the comp left room for the
line, whether it varied, whether the root was doubled.

#### What the number is made of

```
placementFit = 100 x (theFigure + idiomatic)          /  hits carrying a position
registerFit  = 100 x hits wholly inside the register   /  hits struck
densityFit   = mean over bars of clamp(100 - 25 x (hits ABOVE most), 0, 100)

fit = (placementFit x 2 + registerFit + densityFit) / 4
```

- **Placement carries twice the weight** because placement is what a comping
  style *is*. A comper in the right register playing the wrong figure isn't
  comping in that style; a comper playing the right figure a little low still is.
- **The figure and the vocabulary score the same**, per the tiers above. A comper
  who never plays the style's literal figure but lands everything on the feel's
  grid is comping in that style.
- **Density is graded one way only.** Leaving a bar alone is one of the most
  idiomatic things a comper does, so only the busy direction costs anything —
  `fewestPerBar` stays a statement about what the *band* plays. The asymmetry is
  deliberate and has a precedent in `fitsStyle()`, which checks anticipation one
  way only. The one style whose floor is genuinely dense, four-to-the-bar, still
  gets a *word* when a take goes quiet under it, because four to the bar is the
  one thing four to the bar is — but never a point.
- **Register is binary per hit.** Partial credit by distance is a number nobody
  can explain, so the excursion is kept (`outsideRegisterBy`) and said in words
  instead — "a semitone low" rather than 94%.
- **The voicing's score never folds in.** Two different standards: the fit is
  read against the style the player chose, the voicing against the symbol the
  chart wrote. One number out of two questions stops being explainable the first
  time somebody asks which half of it moved. They sit side by side in the dock.
- **The bass note costs nothing.** It isn't in `CompStyleDefinition` and isn't
  the symbol's business, so it produces a chip and an observation and no points.
  That's the line the fit is drawn on, and holding it is what keeps the number
  meaning one thing.
- **`fit` is empty rather than zero** when nothing was played, and when nothing
  carried a position. A nought out of a hundred for a player who hasn't played is
  a mark rather than a reading — the same distinction `LineStats` draws between
  nothing and zero. The wire sends `null` and the page hides the meter.

### A hit's verdict is settled the moment it's struck
Which is why the evaluator is **stateless**, and why the solo take is still the
one stateful corner of `jazz::api`.

The solo take has to remember, because a note's *reading changes after the fact*:
the window promotes an `unresolved` note to `approach` two notes later, and
`resolvedByLastNote()`/`strandedByLastNote()` exist only because of that. A
comped chord has no window over it. Its slot is its slot, its register is its
register, and what its notes said is what they said; nothing played afterwards
revises any of it. Everything a memory would buy is a *count*.

So `compHit` reads one chord and `compTake` reads a list of them, both pure. The
one cross-hit fact the live line wants — how many chords are already in this bar
— travels as an `int`, which is a count the page may take rather than a rule it
applies.

**The page may remember what was played; it may never remember what it meant.**
`comp.hits` is a list of `{bar, at, notes}` — the same class of thing as
`state.heldNotes`, a record of events carrying no judgement. Every verdict, live
and final, comes back from the engine.

The half that genuinely can't be known per hit is the sparse direction of
density: a bar left too quiet leaves no trace in the hits themselves. That's why
`compTake` is told which bars the take covered, and why the page counts a bar
only once it's been played *through*. Counting a bar a take was stopped in the
middle of marks every four-to-the-bar take sparse in its last bar.

### One gesture is one chord, and here the page waits
Solo practice's rule is that **nothing waits for a chord to be finished** —
`play()` takes `Attack::withPrevious` and reads each note as it arrives. Comping
takes the opposite decision, and the difference is what the unit is.

In a line the unit is the note, so there's always something honest to say the
instant one arrives, and the window takes its judgement back later if the rest of
a chord answers it. In a comp the unit is the **chord**: one note is not a
voicing, is not in or out of a register, and can't have the bass player's note
underneath it. There's nothing to report until the gesture is there.

So the page gathers the notes struck together and asks once. The position is the
**first** note's: a chord is one place on paper, and a voicing rolled across two
ticks is one chord struck slightly unevenly rather than two chords. The precedent
was already in the file — chord practice's static path gathers notes behind
`midi.settleTimer` in exactly this shape. What's different in time is that each
gesture is its own hit, where static deliberately keeps adding to one.

### Anticipation is read from the notes, and a tie is not a push
A player doesn't declare an anticipation. They play the next chord early, and the
notes are the only evidence there is. So when a hit lands on a slot that
`anticipates` and there is a next bar, the voicing is analysed against both
chords and the better `VoicingAnalysis::score` wins.

**Strictly** better. Over a bar repeating its chord the two readings are
identical, and promoting a tie would call every hit on an anticipating slot a
push — including in `| Dm7 | Dm7 |`, where there's nothing to push into. That
would be inventing intent. It's `fitsStyle()`'s one-way asymmetry seen from the
player's side.

A related trap, and the reason `inItsOwnBar()` exists: the page quantises a
moment onto the grid, so a chord struck a hair before a downbeat arrives as beat
4 of a bar of four, and a position read back from a tick count arrives as beat -1
(`BarPosition::fromTicks` floors). Both are the neighbouring bar. Left alone, a
comper pushing the and of four a few milliseconds late is marked off style at a
position no slot has ever offered.

### The root in the bass is ours to say, not `VoicingAnalyzer`'s
`VoicingAnalyzer::Options::practiseType = twoHandedRootless` would flag a doubled
root for free, and it's the wrong tool. It would take points off the *voicing's*
score for a reason that belongs to the comp rather than to the symbol, rewrite
the summary into "this exercise is on two-handed rootless voicings", and call a
shell voicing or a rootless left hand the wrong shape — both of which are
perfectly good comping.

The fault isn't the shape. It's that somebody else is already playing that note.
So the reading is the evaluator's own, in two strengths: `rootAnywhere` is worth
a word, and `takesTheBassNote` — the root at the bottom of the voicing, where it
actually collides with the bass player — is worth a chip. `VoicingAnalyzer` is
reused exactly as it is.

### Density is a count, and a count doesn't survive a change of metre
One of two bugs the widened invariant caught. `CompSlot` was designed carefully so
a figure means the same thing in three as in four — an empty `beat` is every
beat, a negative one counts back from the end. `fewestPerBar` has no such
protection: four-to-the-bar is four chords in four and *three* in three, and
holding a waltz to the written four marked the generator's own comp sparse in
every bar.

So the density window is clamped to the number of places the style actually has
in that metre — the same move `slotPositions()` makes when it returns nothing for
a beat the metre hasn't got. A style has less to say in a metre it wasn't written
for; asking it for chords it has nowhere to put isn't a reading of the player.

The same rule decides which bar a hit counts in: **the bar the hands played it**,
anticipations included, because that's how the generator trims and tops up.
Counting a push forward makes four-to-the-bar fail its own density check.

### What the evaluator deliberately doesn't judge
**How long a chord was held** — the durations bullet above. A comper holding
every chord through a four-to-the-bar is reading a style that doesn't say not to,
so nothing may mark them for it, and the page sends no note-offs for a hit at
all.

**Whether the comp left room for a soloist**, whether it varied, whether the
voice leading moved. All real, none of them in the definition, all of them words
if they're ever said.

### Where the plan got to

Comping was built to a seven-step plan. Steps **1, 2, 3, 5 and the style picker
out of 7** were the first agreed scope and shipped first; **4, 6 and the rest of
7** were scoped and built afterwards, which is when step 6's recorded answer
turned out to be wrong and was reversed. So a later session doesn't re-plan a
finished step or assume an unfinished one is done:

| Step | State | Notes |
|---|---|---|
| 1. Subdivision grid | done | `docs/RHYTHM.md`. |
| 2. `CompStyleDefinition` | done, bar one bullet | Onset slots, register, density, cross-barline anticipation and `variation` all present. "Typical duration" is still the one bullet with no field — see above. |
| 3. The generator | done | `compPlan()` — seeded, planned ahead, `fitsStyle()`-checked. |
| 4. The evaluator | **done** | `readCompHit()` and `evaluateComp()` in `Comping.cpp`. Stateless, and `fitsStyle()` didn't grow — see above. The widened invariant caught two bugs in the style data that had gone unseen because nothing read those fields. |
| 5. `compStyles()` on the wire | done | `EngineApi`; both shells build the menu from it, no local copy. |
| 6. Where it lives in the mode structure | **done, and not where this table used to say** | **Chord practice's *In time***, not a third mode — see *Not a third mode* above for why that was reversed. `state.mode` stays two-valued. Comping as backing, which shipped first, still lives inside solo practice's *In time*. |
| 7. UI and menu wiring | **done** | Style picker + instrument toggles/sound pickers shipped first; the verdict surface followed once step 6 was settled, which is the order the plan's own rule asked for. It **grew `.feedback`** rather than adding a panel beside it — the same mode's dock answering two halves of one question. |

The walking bass and the recorded instruments were both asked for separately
and aren't steps of this plan; neither is solo practice's `Attack` model
(reading a chord as one gesture — see `docs/SOLO_PRACTICE.md`), which came out
of a bug report rather than this plan.

### Still open
- **Per-bar comping marks on the chart.** `paintTakeMarks()` draws a solo take's
  numbers on the bars and is still solo practice's alone. A comping take's `bars`
  come back in the same shape, so this is a page change with no engine in it.
- **Hit duration**, the durations bullet above — the one field the agreed shape
  never got, and the reason nothing may mark a player for holding a chord.
- **Drums.** Named in the comping menu and not built: the one piece of the
  rhythm section needing no theory and no engine call, only a pattern and a kit.

And a note for anyone tempted by a third mode again: `state.mode` is still
two-valued, and `data-mode`/`applyMode`/the palette tokens/the masthead/the
per-mode cheat sheets are still built around exactly two. The exercise this file
describes needed none of it, which is the argument.
