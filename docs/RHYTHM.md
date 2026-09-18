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
  writes tick 12, and the page sounds it two-thirds of the way through the
  beat. Only the eighth moves — a triplet is already written where it's
  played, so swinging every subdivision (the obvious implementation) would
  bend a ballad's triplets into something nobody plays. The page is also the
  only thing that un-swings a note coming back in, so the engine only ever
  sees straight notation.
- **`strengthAt()` is metre-aware, not tabulated.** The second strong beat is
  the one starting the bar's second half; an odd metre has no second half,
  so a waltz has only its downbeat. That's the character of three, not a
  special case bolted onto a table.

A position on this grid (a beat + a tick) is a **position, not a time** — the
way a measure index is a position in a chart, not a moment. Whoever owns the
clock (the page's transport) turns a moment into one of these before the
engine sees it, and turns one back into a moment to play it. See
`docs/SOLO_PRACTICE.md` and `docs/COMPING.md` for how each consumer reads it.
