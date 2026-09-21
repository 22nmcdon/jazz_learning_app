<!-- Why the practice record keeps what it keeps, and refuses what it refuses.
     Read this before touching `PracticeLog.h`, the two practice calls in
     `EngineApi`, or the dashboard in `web/index.html`. CLAUDE.md's
     "nothing comes back unless you asked for it by name" invariant is the
     short version of the storage half. -->

# Your practice — what is kept, and what is deliberately not

Every other reader in this engine is handed one take and says what that take
did. `PracticeLog` is handed *many* and answers a different question — the one
a single take structurally cannot:

> **What am I actually practising, and what am I avoiding?**

A take can tell you how the last two minutes went. Only a record can tell you
that you have never once soloed over a half-diminished chord, or that the back
eight of the tune you have played eleven times has been reached twice.

## The rule: counts, never a mark

`LineStats::score()` says of itself, in its own doc comment, that it is
*"a reading of a bar, not a grade for a player"*. A mark plotted over weeks is
exactly the grade it refuses to be — so **nothing in this feature keeps one**.

This is enforced in three places rather than remembered as a convention:

- `PracticeLog.cpp` never calls `score()`, and `PracticeTake` has no field for
  one. Comping's `CompEvaluation::fit` is left off the row for the same reason:
  it is the comping half's score.
- The wire has its own emitter, `practiceNotesJson`, rather than reusing
  `lineStatsJson` — because that one carries `"score"`. The counts cross and
  the mark does not, so a page drawing this panel has **nothing to plot** even
  if somebody later decides a line would look good.
- A smoke check reads the whole rendered panel and fails on the word.

The distinction that makes this coherent rather than squeamish: *a count set
beside an earlier count is still a fact. A mark set beside an earlier mark is a
verdict about a person.* "Your line is 10% outside lately, against 60% across
the earlier half of this record" is two facts. "You are at 74 and you were at
61" is a report card.

This is the same rule CLAUDE.md states for solo practice — rhythm and
voice-leading produce words, never points — one step further out. It does not
contradict comping scoring placement, for the reason `docs/COMPING.md` gives: a
`CompStyleDefinition` is a written standard the player chose off a menu, and
there is no such standard for "a month of practice".

## The engine gains no memory

`readPractice` and `readTuneProgress` are free functions over a vector the
caller owns. The engine has no store, no registry, and — as everywhere else —
no clock: `today` arrives as an argument, the way a `BarPosition` does.

A history is **data the shell hands over on every call**, exactly like a
chart's progression text and like a comping style described rather than named.
That is what keeps both readings as unit-testable as `LineAnalyzer`, which is
the property `docs/HANDOFF.md` was protecting when it asked whether the engine
should ever learn about history. It does not. It reads one it is given.

The history crosses as **flat delimited text, never JSON**, in the direction
this wire has always run: results are JSON because encoding them is the shell's
business, inputs are text because the engine has no JSON reader and must not
grow one. Five delimiters, none of which can occur inside a number, so nothing
needs escaping — and the tune is a **number**, which is what keeps the grammar
free of quoting. What a player calls a tune is the page's business.

**An empty history is a record with nothing in it; anything else that does not
parse is an error.** The same asymmetry a described comping style draws. A
shell's bug must never read back as a player who has not practised.

## What one tune remembers

The per-bar half is the finding this feature exists for.

Everybody starts at the top. A chorus outlasts the patience of whoever is
playing it. So the back of a standard gets a fraction of the practice the front
does — and that is invisible from inside any one take and obvious across
eleven. `readTuneProgress` counts, per bar, **how many takes ever reached it**,
and the dashboard draws every bar of the chart whether or not it was reached,
because the pale ones are the point.

Three things it refuses to say, each with a test:

- **One take is not a habit.** Below three takes it returns counts and no
  observations at all. With one take, every bar reached was reached in "every
  take" — true, and worth nothing.
- **A tune played end to end every time gets no sentence.** The reading speaks
  only when the tune is actually lopsided. Handing a complete chorus a line
  about its neglected bars would be inventing a fault to have an opinion about.
- **A bar is named as difficult only when it stands apart** from the rest of
  the tune. This is the reading `LineAnalyzer` already makes of one take, one
  level out — where it means something different: in one take an outside bar
  was a moment; in nine takes it is the bar of this tune that is actually hard.

Bars a since-shortened chart no longer has are dropped rather than drawn. The
tune on the stand is the tune the reading is about.

## Saved tunes, and the invariant that had to move

The per-tune half needs saved tunes, and the page deliberately had none: a
chart was gone on reload.

That design was over-stated rather than wrong. What it protected is that the
page opens on a clean stand — nothing half-played returns, no take is restored
into the session, you are never handed a chart you did not ask for. All of that
still holds. What changed is that a chart you deliberately saved and named is
yours to open again. See CLAUDE.md for the amended wording; the check that
enforces it is: save a tune, reload, and the stand still holds the chart the
page shipped with.

Two decisions inside that are worth keeping:

- **Saving under a name you have used replaces that tune and keeps its id.** A
  second tune of the same name would orphan every take already logged against
  the first.
- **Forgetting a tune does not delete its practice.** Those takes happened, and
  the record is about the player rather than about the tune — so the rows stay
  and stop belonging to anything. Ids are never reused, because the library's
  `next` only goes up.

## The dashboard, and why it is not in the settings panel

`#progressDialog` opens from a button of its own in the top bar. What is behind
it is a **reading**, and `#menuPanel` holds settings — a distinction this page
is in the middle of sharpening.

The button is a glyph rather than a label, and that was measured rather than
preferred. Top-bar height comes straight out of the chart, `.top-bar-right` is
already full, and a labelled button wraps the row:

| width | none | "Your practice" | "Progress" | "Record" | "Log" | glyph |
|---|---|---|---|---|---|---|
| 1024 | 91 | 129 | 129 | 129 | 129 | **91** |
| 1100 | 91 | 129 | 129 | 91 | 91 | **91** |
| 1280 | 91 | 129 | 129 | 91 | 91 | **91** |

Only the glyph is free at every width — even three letters cost 38px of music
at 1024, which is a laptop rather than an edge case. The numbers live beside
the rule in the stylesheet. **The top bar has room for one more labelled
control and does not currently have it spare**, which is the thing to know
before moving anything else up there.

Note also that **390px is not the width that catches this**: `.top-bar-right`
is already four lines deep there, so one more control changes nothing. The
check measures bar height with the button and without it, at 390 *and* 1024.

## The page works nothing out

Everything the panel draws comes back from the engine — the summary, the
observations, the coverage lists, the per-bar counts. The page computes no
percentage, no total and no sentence of its own, for the same reason
`scaleStyles()` and `compStyles()` are not copied into a shell: a second
opinion about what a history means is a second opinion to drift.

The one thing the page does own is the clock, which is the division the whole
engine works to.
