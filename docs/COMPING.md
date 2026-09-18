<!-- The long "why" for comping. The load-bearing rules are summarised in
     CLAUDE.md, which is loaded every session; this file is where each of them is
     argued for, with the counts and the bugs that produced it.

     If you are changing comping - the styles, the generator, the evaluator or
     chord practice's In time - read this. If you are changing something else,
     CLAUDE.md's summary is enough and this file is why it says what it says. Do
     not let the two disagree: a rule moved here without a line left behind is a
     rule a session will not see. -->

# Comping

One artifact and three readers. `CompStyleDefinition` says where a style puts its
chords, how many of them there are and what register they sit in; the generator samples
from it, the menu is built from it, and the evaluator now scores a player against it. A
style described in one place and re-described in another is how the two drift - the same
reason `scaleStyles()` lives in the engine.

## Where the exercise lives, and the decision that was reversed

The comping plan's step 6 asked where a comping *exercise* belongs in the mode structure,
and it was answered once, in CLAUDE.md, as **a third mode**: scoring a player's own
comping was "neither of the questions the two modes ask". That answer did not survive
contact with the **Static / In time** axis solo practice already had.

In time is not a different mode there. It is the same mode with a clock, and the clock
adds readings that are simply silent without it. That is exactly the relationship between
chord practice and comping:

| | Static | In time |
|---|---|---|
| **Chord practice** | one voicing, held - does it say what the bar says? | struck, placed - and did it land where the style puts it? |
| **Solo practice** | one note, read against the bar | a take, with the line's rhythm read too |

The voicing half of comping *is* `VoicingAnalyzer`, on the same chart, in the same dock.
Comping is chord practice with a clock, so it goes where the clock goes.

What that saved is precisely what CLAUDE.md warned a third mode would cost. `state.mode`
stays two-valued, so there is no third palette block, no third cheat-sheet flag, no third
`.mode-stack` sibling and no third pill in a switch that is `overflow: hidden` at 430px.
`data-mode` stays a strict single-value compare, which also left `web/smoke-test.mjs`'s
`[data-mode='solo']` selectors working untouched. And it settled the question the
third-mode design had no good answer to - what the bar dialog shows while comping - for
free: it shows substitutions, because a comper reharmonises.

Two rules narrowed rather than disappearing, and both got better for it:

- *"Comping belongs to solo practice only"* became **the generated piano never sounds
  under someone being asked to play chords**. True in both modes, and it leaves the
  walking bass free to play under you - which is what makes a doubled root worth
  mentioning at all.
- *"The keys latch"* became chord practice **static**. In time they do not: a comp is
  struck, not held.

## Where a note falls does not score a solo, and here it scores a comp

This is the one place the repo argues against a rule it states elsewhere, so the argument
is written out rather than assumed. CLAUDE.md says, of solo practice, **"rhythm produces
words, never points"**. That bullet is now marked as solo practice's, and this is why.

A solo line has no stated standard for placement. The chart says which chord and never
says where a note belongs in the bar, so a number for placement would be the engine
inventing a standard and then marking a player against it. That is why
`LineAnalyzer`'s score reads nothing rhythmic, and why a line's shape produces words.

A comp has one, and the player picked it off a menu. A `CompStyleDefinition` is a
written-down statement of where the hits go (`CompSlot`), how many there are
(`fewestPerBar`/`mostPerBar`) and what register they sit in
(`lowestNote`/`highestNote`), and the generator is already held to it in both directions.
Scoring a player against the same slots the band is held to is not a new judgement. It is
the same act as `VoicingAnalyzer` scoring a voicing against the symbol the chart wrote, or
`LineAnalyzer::Options::chosenScale` holding a player to the scale they chose out of the
panel. Take the style away and there is nothing here to score - which is why a verdict
always names one.

And the line holds on the other side. Everything a style does **not** pin down still
produces words: how long a chord rang, whether the comp left room for the line, whether it
varied, whether the root was doubled. None of them is in the definition, so none of them
is in the number.

### What the number is made of

```
placementFit = 100 x hits in one of the style's slots  /  hits carrying a position
registerFit  = 100 x hits wholly inside the register   /  hits struck
densityFit   = mean over bars of clamp(100 - 25 x (hits outside [fewest, most]), 0, 100)

fit = (placementFit x 2 + registerFit + densityFit) / 4
```

- **Placement carries twice the weight** because placement is what a comping style *is*. A
  comper in the right register playing the wrong figure is not comping in that style; a
  comper playing the right figure a little low still is.
- **Register is binary per hit.** Partial credit by distance is a number nobody can
  explain, so the excursion is kept (`outsideRegisterBy`) and said in words instead - "a
  semitone low" rather than 94%.
- **The voicing's score never folds in.** Two different standards: the fit is read against
  the style the player chose, the voicing against the symbol the chart wrote. One number
  out of two questions stops being explainable the first time somebody asks which half of
  it moved. They sit side by side in the dock and are never averaged.
- **The bass note costs nothing.** It is not in `CompStyleDefinition` and not the symbol's
  business, so it produces a chip and an observation and no points. That is the line the
  fit is drawn on, and holding it is what keeps the number meaning one thing.
- **`fit` is empty rather than zero** when nothing was played, and when nothing carried a
  position. A nought out of a hundred for a player who has not played is a mark rather
  than a reading - the same distinction `LineStats` draws between nothing and zero. The
  wire sends `null` and the page hides the meter.

## A hit's verdict is settled the moment it is struck

Which is why the evaluator is **stateless**, and why the solo take stays the one stateful
corner of `jazz::api`.

The solo take has to remember, because a note's *reading changes after the fact*: the
window promotes an `unresolved` note to `approach` two notes later, and
`resolvedByLastNote()`/`strandedByLastNote()` exist only because of that. A comped chord
has no window over it. Its slot is its slot, its register is its register, and what its
notes said is what they said; nothing played afterwards revises any of them. Everything a
memory would buy is a *count*.

So `compHit` reads one chord and `compTake` reads a list of them, both pure. The one
cross-hit fact the live line wants - how many chords are already in this bar - travels as
an `int`, which is a count the page may take rather than a rule it applies.

**The page may remember what was played; it may never remember what it meant.** A list of
`{bar, beat, tick, notes}` is the same class of thing as `state.heldNotes` - a record of
events carrying no judgement. Every verdict, live and final, comes back from the engine.

The half that genuinely cannot be known per hit is the sparse direction of density: a bar
left too quiet leaves no trace in the hits themselves. That is why `compTake` is told
which bars the take covered, and why the page counts a bar only once it has been played
through. Counting a bar a take was stopped in the middle of marks every four-to-the-bar
take sparse in its last bar.

## One gesture is one chord, and here the page waits

Solo practice's rule is that **nothing waits for a chord to be finished** - `play()` takes
`Attack::withPrevious` and reads each note as it arrives. Comping takes the opposite
decision, and the difference is what the unit is.

In a line the unit is the note, so there is always something honest to say the instant one
arrives, and the window takes its judgement back later if the rest of a chord answers it.
In a comp the unit is the **chord**: one note is not a voicing, is not in or out of a
register, and cannot have the bass player's note underneath it. There is nothing to report
until the gesture is there.

So the page gathers the notes struck together and asks once. The position is the **first**
note's: a chord is one place on paper, and a voicing rolled across two ticks is one chord
struck slightly unevenly rather than two chords. The precedent was already in the file -
chord practice's static path gathers notes behind `midi.settleTimer` in exactly this shape.
What is different in time is that each gesture is its own hit, where static deliberately
keeps adding to one.

## Anticipation is read from the notes, and a tie is not a push

A player does not declare an anticipation. They play the next chord early, and the notes
are the only evidence there is. So when a hit lands on a slot that `anticipates` and there
is a next bar, the voicing is analysed against both chords and the better
`VoicingAnalysis::score` wins.

**Strictly** better. Over a bar repeating its chord the two readings are identical, and
promoting a tie would call every hit on an anticipating slot a push - including in `| Dm7 |
Dm7 |`, where there is nothing to push into. Calling that an anticipation would be
inventing intent.

This is `fitsStyle`'s one-way asymmetry seen from the player's side. There the rule is
that a hit which pushed must have come from a slot that pushes, while a slot that pushes
may honestly produce a hit that did not - because the last bar of a range has no next
chord to pull forward. Here the same latitude runs the other way: a pushing slot may carry
a hit that meant this bar's chord.

A related trap, and the reason `inItsOwnBar()` exists: the page quantises a moment onto the
grid, so a chord struck a hair before a downbeat arrives as beat 4 of a bar of four, and a
position read back from a tick count arrives as beat -1 (`BarPosition::fromTicks` floors).
Both are the neighbouring bar. Left alone, a comper pushing the and of four a few
milliseconds late is marked off style at a position no slot has ever offered.

## The root in the bass is ours to say, not `VoicingAnalyzer`'s

`VoicingAnalyzer::Options::practiseType = twoHandedRootless` would flag a doubled root for
free, and it is the wrong tool. It would take points off the *voicing's* score for a reason
that belongs to the comp rather than to the symbol, rewrite the summary into "this exercise
is on two-handed rootless voicings", and call a shell voicing or a rootless left hand the
wrong shape - both of which are perfectly good comping.

The fault is not the shape. It is that somebody else is already playing that note. So the
reading is the evaluator's own, in two strengths: `rootAnywhere` is worth a word, and
`takesTheBassNote` - the root at the bottom of the voicing, where it actually collides with
the bass player - is worth a chip. `VoicingAnalyzer` is reused exactly as it is.

## The two numbers that had never been read

Writing the evaluator turned up two things about the style data that nothing had ever
observed, because until now nothing read those fields at all.

**`lowestNote`/`highestNote` had no reader.** They were written down when the four styles
were and never consulted. `compPlan()` leant on `compingVoicing()`, which bounded only its
*anchor* to this file's own window (`lowestCompAnchor = 45`, `highestCompAnchor = 57`) -
and a two-handed voicing spreads roughly two octaves above its anchor. Swept over a real
chord vocabulary, the generator played three semitones under Basie's and Charleston's own
floor of 48. The moment the evaluator read register, the generator failed its own style.

That was fixed in the generator, not conceded in the evaluator: `compingVoicing` gained an
overload taking a stated window, and `compPlan` passes the style's. The band is now held to
the same two numbers a player is.

**Density is a count, and a count does not survive a change of metre.** `CompSlot` was
designed carefully so a figure means the same thing in three as in four - an empty `beat`
is every beat, a negative one counts back from the end. `fewestPerBar` has no such
protection: four-to-the-bar is four chords in four and *three* in three, and holding a
waltz to the written four marked the generator's own comp sparse in every bar. So the
density window is clamped to the number of places the style actually has in that metre,
which is the same move `slotPositions` makes when it returns nothing for a beat the metre
has not got. A style has less to say in a metre it was not written for; asking it for
chords it has nowhere to put is not a reading of the player.

Both were found by the same test, and neither would have been found by the old one. The
invariant used to check the generator against the *slot* half alone, against one chart in
one metre. It now checks placement, register and density, over four styles, 24 seeds, 16
bars of a wide chord vocabulary and three metres. **An invariant that only ever asserts
"yes" is satisfied by a function that always says yes**, which is why the negative
direction is now tested too: a hit the style never offers, a push from a slot that does
not push, a slot the metre has not got.

## What the evaluator deliberately does not judge

**How long a chord was held.** `CompStyleDefinition` says where a style puts a hit and
says nothing about whether it is stabbed or held, so a comper holding every chord through
a four-to-the-bar is reading a style that does not say not to. It remains the one bullet
of the definition's agreed shape with no field, and the first thing to add if that shape
is reopened. Until then nothing may mark a player for it, and the page sends no note-offs
for a hit at all.

**Whether the comp left room for a soloist**, whether it varied, whether the voice leading
moved. All real, none of them in the definition, all of them words if they are ever said.

## Still open

- **Per-bar comping marks on the chart.** `paintTakeMarks()` draws a solo take's numbers on
  the bars and stays solo-only. The comping take's `bars` array is the same shape, so this
  is a page change and not an engine one.
- **Drums.** Named in the comping menu and not built - the one piece of the rhythm section
  that needs no theory and no engine call, only a pattern and a kit.
- **A metre that survives export**, which the readers bring in and the iReal Pro writer does
  not put back out.
