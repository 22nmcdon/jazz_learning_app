# Decisions

Why the rules in `CLAUDE.md` are the rules, where the reasoning belongs to no
single feature. A feature's own argument lives in its own doc —
`docs/SOLO_PRACTICE.md`, `docs/COMPING.md`, `docs/PROGRESS.md`,
`docs/RHYTHM.md`, `docs/BRANDING.md`.

This is the opposite of `docs/HANDOFF.md`, which says what is left to do and
whose own rule is to **delete** a section once it is finished. Nothing here is
ever finished: a decision stays written down so the next session does not
re-make it, and so the version that was wrong first is on the record rather
than only the version that is right now.

---

`CLAUDE.md` was 881 lines and is loaded into every session, so it keeps the
rule and the sentence saying what breaks without it. What it used to also carry
— the argument, and the version of the thing that was wrong first — lives in
the feature docs where that feature is explained, or here where it belongs to
no one feature. None of it was deleted; this is where the rest landed.

### The page rendered in quirks mode for its whole life

Every pixel number in `CLAUDE.md`'s UI Conventions was **first measured in
quirks mode**. The page shipped with no doctype and no viewport meta: it
rendered in `BackCompat` everywhere, and on a 393px phone the layout engine
assumed 980px and scaled down, so the 760px breakpoint never fired on hardware.

The tags are there now and everything was re-measured. The only numbers that
moved were `.chart-bar` (19px → 26px at every width, a line box the quirks
parser did not make) and therefore the sheet's top edge, 7px lower. Every check
still passed, which is the part worth remembering: **no check in the suite
could see the bug**, because a width set with Playwright's `viewport` is a
CSS-pixel width and that is exactly the case a missing viewport meta does not
break. The four `isMobile` checks were added for that reason and are the only
ones that can tell a real phone from a narrow window.

### `#menuPanel` held six sections, and three were in the wrong room

*The band* is three switches, a style picker with an editor button and two
sound pickers — seven controls wanting two columns, which a 300px column
sharing its height with five other sections could not give them. *Voicings* and
*Scales* are the reading the page holds you to, which you change **looking at
the music** rather than looking at a menu. All three left for panels of their
own on the chart's row, and what is left is a settings panel in the honest
sense: you open it when something about the machine is wrong, not while you are
playing.

The rule that came out of it — *the machine versus the music* — is the line the
six-section version did not draw.

### Three menus was the ceiling, and the ceiling was the wrong shape of rule

It counted panels when the thing worth counting is questions. It expected a
fourth panel to mean something was in the wrong room; what actually happened is
that one panel turned out to be holding three. The half that was doing the work
survives as **a panel is one question, and its button sits where that question
is asked**.

### Comping was recorded as a third mode for a long time

It was reversed, because *In time* already meant "the same mode, with a clock,
adding readings that are silent without one" — which is exactly what comping is
to chord practice. `state.mode` is still two-valued.

### The voice-leading visualiser was drawn over the bars first

That was the wrong place. A picture of where the guide tones are answers a
question a player having it explained already knows the answer to, and the one
they actually have — *where do my fingers go* — it leaves them to work out. So
it moved onto the keys.

### "Practice settings are remembered; work is not"

Over-stated rather than wrong. What it was protecting is that the page opens on
a clean stand: nothing half-played returns, no take is restored, and you are
never handed a chart you did not ask for. All of that still holds. What the
tune library added is that a chart you *deliberately saved and named* is yours
to open again — so the line is **asked-for versus assumed**, not
settings-versus-work.

### Reharmonisation: rule-based, and the question was a bug

~~How much of the reharm suggestion engine should be rule-based vs. data/ML-informed?~~

`ReharmStyle` tagged all 36 rules and `Options::style` filtered on them, and no
shell could reach it — so the first move was to collect that rather than reach
for a corpus. Counting the tags is what mattered: `brazilian` was on **no rule
at all** and `quartal` on **one**, so the menu that filter would have shipped
had an entry that was a lie. Both were deleted, the m11 rule retagged `modal`,
and what is on the wire now is two vocabularies that exist.

A corpus-ranked reharmoniser would replace 36 rules that each carry their own
explanation with a number that carries none, which is a bad trade in a teaching
app. The way back in, if anyone wants one, is **more rules or better tags**,
not a model.

**That rule is the general one**: an enum value no record carries is a menu
entry that is a lie. It is enforced for the lick catalogue too —
`LickSource::transcription` and `LickOrnament::grace` were deleted for the same
reason, and a test says a `ReharmStyle` lives in the enum only while at least
one rule is tagged with it.

### MusicXML / MuseScore import

~~Is it needed, given iReal Pro and PDF both ship?~~ **No.** Not wanted.

### A dense, DAW-style desktop layout

~~Worth designing for now, or deferred?~~ **No, and the evidence moved against
it.** The page is laid out as an instrument rather than an article — a fixed
three-zone frame with the chart taking every pixel the other two do not — but
it stays *one responsive column*. No side panel, no wide-screen layout of its
own.

The thing to weigh is no longer what it was. Settings did *not* converge into
one panel to be pinned open; they spread into five, each hung beside the thing
it acts on. So a side panel now means moving a control **away** from what it
changes, which is the move the chart's row was chosen over.

### Numbers that moved out of `CLAUDE.md` with their arguments

- **Why the chart is justified at all.** A twelve-bar tune is 404px of music in
  a 560px zone on a 1440×900 window; top-anchored, the difference banks at the
  bottom as dead cream.
- **`readLinePlacement`'s three parts are weighted 2/1/1**, the way comping's
  are — the grid (per *onset*, so a block chord counts once), the phrasing
  (one-way: running on costs, stopping early does not) and the register (per
  note).
- **`LineBar`'s strong-beat counts cross the wire**, through
  `LineAnalyzer::barFor`. They had been counted since the grid arrived and
  nothing outside the engine could see them.
