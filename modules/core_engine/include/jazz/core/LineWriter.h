#pragma once

#include "jazz/core/Chart.h"
#include "jazz/core/LineAnalyzer.h"
#include "jazz/core/LineStyle.h"
#include "jazz/core/Rhythm.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jazz::core
{

/** Writes a line, where `LineAnalyzer` reads one.

    Its own header because reading a line and writing one are two jobs.
    `LineAnalyzer`'s own header draws the line it keeps - a voicing is a thing,
    a line is a stream - and a generator living inside it would be the same
    kind of blur as a chord reader living inside it.

    **The notes it writes carry the analyser's own vocabulary**, not a parallel
    enum of its own, and that is the whole design rather than a convenience.
    What makes this feature worth having is that the page plays you a line, you
    play it back, and the reading you already get tells you how it went - so a
    generator whose idea of "approach note" were its own would be handing you a
    line the app then marks as something else. `LineWriterTests` holds that as
    an invariant: every note this writes must read back from a take as the
    colour it was written as. Same shape as `VoicingAnalyzerTests`' rule that
    every voicing `idiomaticVoicings` offers must classify as the type it was
    offered for, and it exists for the same reason.

    It is **generated rather than quoted**, and that is a narrower claim than
    the one that used to be here. This said "generated, not curated", on the
    grounds that stored patterns are memory the engine would have to keep - but
    "the engine gains no memory" is about not retaining what a *user* did, and a
    static table is what `compStyles()`, `scaleStyles()` and the 36 reharm rules
    already are. The research is blunt that lines are made of patterns: Owens
    catalogued 64 Parker formulas across 250 transcriptions, and Norgaard found
    82.6% of Parker's notes begin a four-interval pattern that recurs elsewhere
    in the corpus. A cell catalogue is a later pass; what is here now is the
    layer the research says matters more, which is phrase shape and rhythm.
*/
struct WrittenNote
{
    int measureIndex {};
    BarPosition at {};
    int midiNote {};
    std::string chordSymbol;

    /** What the analyser should call it - and will, or a test fails. */
    NoteColour colour {};
};

/*  There is no `ApproachKind` here, and the first version of this had one.

    It claimed `chromatic` for every approach note it wrote, and the round-trip
    test refused it: the analyser reads the *gesture* from the line around the
    note and tries the most specific rule first, so an approach that happens to
    be stepped into and stepped out of the same way is a **passing tone** and
    is read as one. The writer cannot know which it will turn out to be,
    because that depends on the note after it, which is chosen later.

    So the writer commits to the colour - this note is an approach, outside by
    pitch and resolving by step - and lets the reading name what kind. That is
    the right division: the colour is a fact about the note, and the gesture is
    a fact about the line.
*/

/** Where one phrase sits: a run of notes and the silence after it.

    The planner's output, and the thing the old version of this file did not
    have. It used to lay a full eighth grid over each bar and thin it with an
    independent coin flip per slot - which gives a texture rather than a
    phrase, and produced exactly the pitfall the research names: every phrase
    a bar long and starting on beat one.
*/
struct PlannedNote
{
    int measureIndex {};
    BarPosition at {};

    /** The last note before a rest. Where a line is allowed to be chromatic on
        the way out, and where it must not be left hanging. */
    bool endsPhrase {};
};

/** Lays out phrases across a range of bars, before any note is chosen.

    Pitch decisions cannot see phrase shape and phrase decisions do not need to
    see pitch, so they are two passes. The research puts it first in its own
    implementation order for the same reason - "before choosing any notes, pick
    a phrase type, a length, a start position and a following rest".
*/
std::vector<PlannedNote> planPhrases (const LineStyleDefinition& style,
                                      int fromBar,
                                      int toBar,
                                      int beatsPerBar,
                                      std::uint32_t seed);

/** Something a written line does that its own style says it should not.

    The hard constraints, run as a validator. The research recommends exactly
    this - "your existing classifier can score candidates and reject
    violations" - and it is also how the generator is stopped from drifting
    away from the catalogue it claims to be playing from.
*/
enum class LineFault
{
    outsideTheRegister,    ///< R12
    offTheStyleGrid,       ///< R15: a note where this style has no subdivision
    chromaticOnTheBeat,    ///< R1: an approach note on a strong beat
    approachThatNeverLands ///< R2: an approach not followed by a step
};

struct LineFinding
{
    LineFault fault {};
    std::size_t noteIndex {};
    std::string message;
};

/** Reads a written line against the style it claims to be in.

    Empty means it obeys. Used by the tests rather than by `improvisedLine`
    itself, which is built so as not to break these in the first place - a
    generator that produced faults and then filtered them would be two
    descriptions of one style, which is the drift `CompStyleDefinition`'s own
    header warns about.
*/
std::vector<LineFinding> lineFaults (const std::vector<WrittenNote>& line,
                                     const LineStyleDefinition& style,
                                     int beatsPerBar);

/** A line over a range of bars, to play back or play along with.

    The rules, in the order a player would say them:

      - **Phrases first**, then notes. A phrase is a run of notes of the
        style's length and then a rest, and it starts where the style says
        phrases start - off the beat, for the styles that do.
      - **A chord tone on the strong beats.** `strengthAt` says which those are
        and it is metre-aware, so a waltz gets the one it has rather than a
        four's two. This is also what `LineAnalyzer` reads a line for.
      - **Written straight** on the style's own subdivision. Swing is the
        shell's, per `docs/RHYTHM.md`, and nothing here may bend a note.
      - **A chromatic approach into the next chord**, for the styles that use
        them, never on a strong beat - the move that makes a line sound like
        bebop rather than an arpeggio, and the same rule `walkingBass` has one
        register down.
      - **Scale tones in between**, from the scale a take would read against
        rather than a scale of its own, with the style's descending bias.
      - **Inside the style's register throughout**, for the reason the bass
        line is bounded: a line free to follow the harmony upward climbs off
        the keyboard inside a chorus.

    @param chosenScale  the scale the player picked, as `LineAnalyzer::Options`
                        takes it. Empty means the engine's own first answer.
                        Passing what the take will read against is what keeps
                        the written colours and the read ones the same.
    @param lineStyle    a `LineStyleDefinition::key`. Its `scaleStyle` is what
                        reaches `LineAnalyzer::Options`, so writer and reader
                        still agree about which scales are in play.
    @param seed         the same seed gives the same line, note for note, in
                        both shells - `compPlan`'s contract and its hash.
*/
std::vector<WrittenNote> improvisedLine (const Chart& chart,
                                         int fromBar,
                                         int toBar,
                                         const std::string& chosenScale,
                                         const std::string& lineStyle,
                                         std::uint32_t seed);

/** The register a written line stays inside when its style does not say.

    A soloist's two octaves, not a keyboard's seven: a line that wandered
    outside these is one a player cannot copy without moving their hands
    somewhere the exercise never asked for. `LineStyleDefinition` carries its
    own pair and defaults them to these.
*/
constexpr int lowestLineNote = 55;    ///< G3
constexpr int highestLineNote = 84;   ///< C6

} // namespace jazz::core
