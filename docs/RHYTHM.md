<!-- Shared by comping and solo practice on purpose — see CLAUDE.md's Critical
     Invariants. If this file and either consumer's "why" doc disagree about
     what an eighth is, this file wins; fix the other one. -->

# The rhythm grid

`modules/core_engine/.../Rhythm.h` is the one grid comping and solo practice
both read against. The moment there are two grids, they disagree about what
an eighth is — so there is exactly one.

- **24 ticks to the beat.** 24 divides by 2, 3, 4, 6, 8 and 12, so straight
  eighths (12), eighth-note triplets (8), sixteenths (6), and the dotted
  forms all land on an exact tick. A straight-eighth grid was the obvious
  first choice and could not have written a ballad's triplets or a bebop
  line's sixteenths at all.
- **Swing lives in the page, not the grid.** A swung eighth is a ratio a
  shell plays it at, not a different place to write it: the engine always
  writes tick 12, and the page decides how late it falls. Only the eighth
  moves — a triplet is already written where it's played, so swinging every
  subdivision (the obvious implementation) would bend a ballad's triplets
  into something nobody plays. The page is also the only thing that un-swings
  a note coming back in, so the engine only ever sees straight notation.
- **How late is the groove's, and the groove is the tune's.** This was one
  constant — two-thirds of the beat, a 2:1 at every tempo and for every tune —
  and it is now `GrooveDefinition`, a catalogue in the engine beside the
  comping styles and the scale styles. A groove says where the upbeat falls as
  a **fraction of a beat**, at the slow and fast ends of a tempo band; the page
  interpolates with the tempo it booked the bar at. Even is 0.5, the fourth of
  five 0.6, the triplet 0.667, the dotted eighth 0.75.
  - It is tempo-dependent because players are: Corcoran & Frieler measured 456
    solos and found swung eighths at about **1.3:1**, with 2:1 used "mostly at
    slow or moderate tempos". A single ratio was most wrong at speed.
  - It is per-tune because feels differ: a ballad sits nearer a dotted eighth,
    a shuffle is one, and a beat divided into five with the upbeat on the
    fourth is a feel of its own.
  - **The grid never learned any of it.** 24 does not divide by five, and it
    does not need to — what changes is the ratio a note is *played* at, never
    the tick it was written on. That is this section's claim, and the fourth of
    five is the case that demonstrates it.
  - `grooveForStyleWord` reads the chart's own style marking, so "Medium
    Swing" and "Bossa Nova" pick different feels without anyone choosing one.
    **Anything unrecognised is even** — a fallback that guessed at swing would
    bend the eighths of every tune that never said it swung.
  - Nothing maps to funk. A funk groove lays back on the **sixteenth**, and
    this model bends only the eighth, so claiming it would be claiming a feel
    the page cannot play.
- **`strengthAt()` is metre-aware, not tabulated.** The second strong beat is
  the one starting the bar's second half; an odd metre has no second half,
  so a waltz has only its downbeat. That's the character of three, not a
  special case bolted onto a table.

A position on this grid (a beat + a tick) is a **position, not a time** — the
way a measure index is a position in a chart, not a moment. Whoever owns the
clock (the page's transport) turns a moment into one of these before the
engine sees it, and turns one back into a moment to play it. See
`docs/SOLO_PRACTICE.md` and `docs/COMPING.md` for how each consumer reads it.
