<!-- The long "why" for comping, and where the plan actually got to. The
     load-bearing pieces are summarised in CLAUDE.md, which is loaded every
     session; this file is where each is argued for. Shares its rhythm grid
     with solo practice (docs/RHYTHM.md) and its voicing shapes with chord
     practice (idiomaticVoicings / VoicingAnalyzer) — don't let this file and
     those diverge on the pieces they hold in common.

     If you are changing comping, read this. If you are changing something
     else, CLAUDE.md's summary is enough and this file is why it says what it
     says. -->

# Comping — the band behind the soloist

`README.md` describes what comping sounds like and does; this is why it's
built the way it is, and how much of the original seven-step plan is actually
done.

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
- **Engine calls are chained, not raced.** Each bar is booked a bar ahead. If
  two bars were awaited concurrently instead, both would lead from the same
  previous voicing and the second would jump — precisely what the anchor
  window exists to prevent. One promise chain, so calls resolve in the order
  they were booked.

### `fitsStyle()`: the invariant, checked both ways
Everything the generator plays for a style must pass the same "is this in
style" test an evaluator will one day use to grade a player — the same trap
`idiomaticVoicings` and `VoicingAnalyzer` are held out of on the chord-practice
side. Anticipation is checked one way only: a hit that pushed must come from a
slot that pushes, but a slot that pushes may honestly produce a hit that
didn't, because the last bar of a range has no next chord to pull forward.

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

### Where the plan got to

Comping was built to a seven-step plan, of which **1, 2, 3, 5, and the style
picker out of 7 were the agreed scope.** So a later session doesn't re-plan a
finished step or assume an unfinished one is done:

| Step | State | Notes |
|---|---|---|
| 1. Subdivision grid | done | `docs/RHYTHM.md`. |
| 2. `CompStyleDefinition` | done, bar one bullet | Onset slots, register, density, cross-barline anticipation all present. "Typical duration" has no field — see above. |
| 3. The generator | done | `compPlan()` — seeded, planned ahead, `fitsStyle()`-checked. |
| 4. The evaluator | **not built** | `fitsStyle()`'s slot half exists; missing the register/density half, `VoicingAnalyzer` reuse for "what was played," and somewhere to show a verdict (step 6). |
| 5. `compStyles()` on the wire | done | `EngineApi`; both shells build the menu from it, no local copy. |
| 6. Where it lives in the mode structure | decided, not built | A third mode, "Comping practice." What shipped instead — comping as backing, generator only — needed none of the mode machinery and lives inside solo practice's *In time*. |
| 7. UI and menu wiring | half | Style picker + instrument toggles/sound pickers shipped; the evaluator's dock surface didn't (correctly — building it against a guessed mode location was against the plan's own rule). |

The walking bass and the recorded instruments were both asked for separately
and aren't steps of this plan; neither is solo practice's `Attack` model
(reading a chord as one gesture — see `docs/SOLO_PRACTICE.md`), which came out
of a bug report rather than this plan.

### Before starting the evaluator or the third mode
`state.mode` is two-valued today, and `data-mode`/`applyMode`/the palette
tokens/the masthead/the per-mode cheat sheets are all built around exactly
two values. Read this before opening step 6 — it's the expensive part.
