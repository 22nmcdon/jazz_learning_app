#pragma once

#include "jazz/core/Chart.h"
#include "jazz/core/ChordSymbol.h"
#include "jazz/core/LineStyle.h"
#include "jazz/core/Rhythm.h"

#include <string>
#include <string_view>
#include <vector>

namespace jazz::core
{

/** The vocabulary a line is quoted from, as against generated note by note.

    `LineWriter` builds a line from atoms - a chord tone on the strong beats,
    scale tones between them, a chromatic approach when the chord is about to
    change. That is the layer the research said to build first, and it is the
    layer that keeps a line *correct*. What it cannot do is sound like anybody,
    because real lines are made of patterns: Owens catalogued 64 Parker
    formulas across 250 transcriptions, and Norgaard found 82.6% of Parker's
    notes begin a four-interval pattern that recurs elsewhere in the corpus.

    So this is a **catalogue, not a corpus**, and the distinction is the same
    one the reharmoniser draws. Thirty-six rules that each explain themselves
    were chosen there over a ranking that cannot, and a lick here carries its
    `attribution` and its `source` for exactly that reason - the app can say
    "that was the Barry Harris Bb7 line" the way the bar dialog says what a
    substitution is, rather than playing you something good and being unable to
    tell you what it was.

    **It is deliberately not the whole generator.** The research that produced
    these licks is candid about why: Impro-Visor built a probabilistic grammar
    precisely because a hand-built lick database *"would need to be extremely
    large"* and was *"very labor-intensive"*. Its own suggestion is 50-70%
    authored material with generated atoms filling the gaps, and it says in as
    many words that the ratio is a design suggestion to tune by ear. So
    `LineStyleDefinition::lickShare` is a number somebody is expected to argue
    with, and the atom generator underneath is untouched.

    **A lick may break rules `lineFaults` enforces, and that is the point.**
    L03 side-slips a half step over the V - Eb C Bb Ab over G7, every note
    outside G Mixolydian, the Eb landing on beat one - which `lineFaults` reads
    as `chromaticOnTheBeat`. It is not a mistake; it is a documented device,
    and a reading that says "that Eb was outside, and it landed on Ab" is true
    and is the thing worth telling a player. `lineFaults` is therefore a
    validator for **generated** material: it stops the atom writer drifting
    away from the style it claims to be playing. An authored lick answers to
    its provenance instead.

    **What a lick may not do is lie about a colour**, and this is forced rather
    than chosen. A take is read by `LineAnalyzer`'s ordinary rules and has no
    idea a lick was involved, so `LineWriter` computes every note's colour the
    way the reader will - exactly as it does for a generated note. `LickRole`
    below is *provenance and a cross-check*, never a promise, and a test names
    the places the two disagree rather than tidying them away.

    **The corpus caveat that applies to `LineStyleDefinition` does not apply
    here**, and it is worth saying which way round it falls. The phrase-length
    and interval statistics behind a line style come from the Weimar database,
    which is monophonic horn solos. These licks are mostly *piano*: Barry
    Harris, Wynton Kelly, Red Garland, Oscar Peterson, and the teaching sites
    that transcribe them. For a pianist's app that is the right way round.

    @see docs/SOLO_PRACTICE.md, LineWriter.h
*/

/** One chord a lick is written over, as a shape rather than as a symbol.

    A lick belongs to a **progression**, not to a key: "the ii-V-I one" is what
    it is, and D E F A over Dm7 is the same lick as E F# G B over Em7. So the
    chords are held the way `VoicingShape` holds a voicing - a quality, and an
    offset from a root that is supplied later - which is that idea one
    dimension up, and transposes for the same reason.
*/
struct LickChord
{
    ChordQuality quality {};

    /** Semitones above the root of the lick's *first* chord. */
    int rootOffset {};

    /** Where this chord arrives and how long it lasts, in ticks from the
        lick's own start. Two chords to a bar is a turnaround; one across two
        bars is a static dominant. */
    int startTick {};
    int lengthTicks {};
};

/** What the source says a note is doing.

    The research's own note categories, which it chose to match this engine's
    classifier. They are **not** `NoteColour` and must not be made to share a
    type: a colour is what the analyser reads off a line, and this is what the
    person who wrote the lick down said about it. The two agree most of the
    time and the places they part are interesting - a side-slipped cell's notes
    are `colourTone` to the source and read `outside` in a take, which is true
    twice over rather than a contradiction.
*/
enum class LickRole
{
    chordTone,    ///< C
    colourTone,   ///< L - a tension the chord takes
    scaleTone,    ///< S
    approach,     ///< A
    enclosure,    ///< E - one of a pair taking a target from both sides
    outside       ///< X
};

std::string lickRoleName (LickRole role);

/** A pianistic decoration attached to a note.

    Grace notes and crushes are the one piano-specific thing in the catalogue
    that is not just "which notes" - a blue note sliding into a chord tone is
    played as one impulse, and written as two notes it is a different gesture.
    Carried so the data is not lossy; what a shell does with it is the shell's.
*/
enum class LickOrnament
{
    none,
    grace,   ///< struck just before its target, taking time from the note before
    crush    ///< struck with its target and released early - the blues crush
};

std::string lickOrnamentName (LickOrnament ornament);

/** One note of a lick.

    **Pitch is a degree plus an octave, and the octave is not decoration.**
    A degree alone gives a pitch class, and a pitch class alone cannot
    reproduce a line: B D F Ab over G7 rises through an octave and a half, and
    the same four degrees flattened into one octave is a different figure. So
    the octave is authored from the written pitch, and a note's MIDI value is

        rootOfFirstChord + chords[chordIndex].rootOffset + degree + 12 * octave

    which reproduces the source's contour exactly, in whatever key and register
    the caller places the lick. It is the same reason `voicingFromShape` keeps
    an anchor rather than dividing by twelve: the octave is the half a degree
    cannot give you.
*/
struct LickNote
{
    /** Ticks from the lick's start. **Negative for a pickup** - a lick that
        leads in from the bar before starts at -24 or -12, which is what
        `startsOnAPickup` warns a caller about. */
    int tick {};

    /** How long it sounds. 12 an eighth, 24 a quarter, 8 a triplet eighth,
        2 a grace note - the research's own durations, on this grid. */
    int lengthTicks { ticksPerBeat / 2 };

    /** Semitones above the root of the chord this note is played over. */
    int degree {};

    int chordIndex {};
    int octave {};

    LickRole role {};
    LickOrnament ornament {};
};

/** Where a lick came from, which is what its weight is an opinion about.

    The research is explicit that its own entries are not all the same kind of
    thing, so pretending they are would throw away the most useful thing it
    said. A documented device outranks a teaching site's transcription, which
    outranks a composite somebody assembled out of several - and a lick that is
    now an internet meme outranks nothing at all.
*/
enum class LickSource
{
    documentedDevice,  ///< a named practice: Barry Harris' scale, a bebop enclosure
    transcription,     ///< taken off a specific recorded solo
    teachingSite,      ///< a lesson's worked example
    composite          ///< assembled from documented devices, not quoted
};

std::string lickSourceName (LickSource source);

/** One lick: a figure, the progression it is written over, and where it came
    from. */
struct LickDefinition
{
    std::string key;          ///< "L01", stable across versions; what a shell stores
    std::string name;         ///< "Digital ii, 3-to-b9 V"
    std::string summary;      ///< one line, for the dock

    /** Who to credit, as a player reads it: "Barry Harris", "a Parker
        cliche". Empty is not allowed - a lick with nobody behind it is a
        pattern, and the catalogue is for the ones with somebody behind them. */
    std::string attribution;

    LickSource source {};

    /** How often it is drawn, 0-100. **Weight decides the draw, never the
        cost** - the rule `compingVoicing`'s shape weights already follow. A
        composite is rarer than a documented device; it is not worse where it
        fits. */
    int weight { 100 };

    /** The `LineStyleDefinition::key`s this belongs to. A lick tagged on no
        style is unreachable, and a test says so - the same hole `brazilian`
        left in `ReharmStyle` by being tagged on no rule at all. */
    std::vector<std::string> styles;

    /** Its own subdivision, which need not be its style's.

        A bebop player plays triplets. L11's diminished cascade is written in
        them and every line style's `feel` is the eighth, so a lick carries its
        own and `subdivisionsFor` widens what the readers accept - otherwise
        the app would teach a figure and then mark a player for playing it.
    */
    Subdivision feel { Subdivision::eighth };

    std::vector<LickChord> chords;
    std::vector<LickNote> notes;

    /** It leads in from the bar before its first chord, so some of its notes
        have negative ticks and it needs a bar in front of it to start. */
    bool startsOnAPickup {};

    /** Ticks from the first note to the last, pickup included. */
    int spanInTicks() const noexcept;

    /** The MIDI note this would sound, given where the first chord's root is
        put. See the note on `LickNote` for why the octave is in here. */
    int midiFor (const LickNote& note, int rootOfFirstChord) const noexcept;
};

/** Every lick there is, in catalogue order. */
const std::vector<LickDefinition>& licks();

//==============================================================================
/** A lick, and the place in a chart it fits.

    The lick itself is borrowed rather than copied - the catalogue is a
    function-local `static` that outlives every caller, the same lifetime
    `compStyleFor` hands out.
*/
struct LickMatch
{
    const LickDefinition* lick {};

    /** Where it starts, in ticks from the downbeat of the range's first bar.
        A lick with a pickup still starts here; its own notes reach back. */
    int startTick {};

    /** The pitch class its first chord's root lands on, which is the chart's
        rather than the lick's. The octave is the caller's to choose, for the
        reason `voicingFromShape` gives: a shape has a register and a degree
        does not. */
    int rootPitchClass {};
};

/** Every place in a range of bars where a lick of this style fits.

    **What a lick is keyed on is a chord-sequence shape** - the qualities in
    order and the root offsets between them - which is `VoicingShape` one
    dimension up and transposes for the same reason. D E F A over Dm7 is the
    same lick as E F# G B over Em7, and neither is "the lick in D".

    Three rules, and the third is the one worth stating:

      - the qualities line up, one chart chord to one lick chord;
      - the offsets between the roots line up, which is what makes it a
        progression rather than a key;
      - the **boundaries** line up. A lick's chord has to last exactly as long
        as the chart's, so a figure written two beats to a chord does not get
        stretched over a bar apiece - except the **last**, which may be held
        longer, because a chart sitting on the tonic after the lick has landed
        is still the tonic it landed on.

    Consecutive slots of the same chord are merged before any of that, so two
    bars of Dm7 are one span of Dm7 rather than two that a two-bar lick would
    fail to match.

    Returns them all, in the order they occur, rather than choosing: which one
    to play is a question about the line being written and belongs to the
    writer.
*/
std::vector<LickMatch> licksFitting (const Chart& chart,
                                     const LineStyleDefinition& style,
                                     int fromBar,
                                     int toBar);

/** The lick with this key, or the first one when the key is unknown.

    Forgiving for the reason `compStyleFor` and `lineStyleFor` are, and a
    reference into a function-local `static` for the -Wdangling-reference
    reason written beside `compStyleFor`.
*/
const LickDefinition& lickFor (std::string_view key);

} // namespace jazz::core
