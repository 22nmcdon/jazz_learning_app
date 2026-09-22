#pragma once

#include "jazz/core/Chart.h"
#include "jazz/core/LineAnalyzer.h"
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

    It is **generated, not curated**. Stored patterns are memory the engine
    would have to keep, and "the engine gains no memory" is the answer this repo
    has reached for a chart's progression text, a described comping style and a
    practice history. A lick that has to be transposed and fitted to the bar is
    engine work anyway.
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

/** A line over a range of bars, in eighths, to play back or play along with.

    The rules, in the order a player would say them:

      - **A chord tone on the strong beats.** `strengthAt` says which those are
        and it is metre-aware, so a waltz gets the one it has rather than a
        four's two. This is also what `LineAnalyzer` reads a line for, which is
        the point.
      - **Eighths, with rests**, so it breathes rather than running. Written
        straight, at tick 0 and tick 12: swing is the shell's, per
        `docs/RHYTHM.md`, and nothing here may bend a note.
      - **A chromatic approach into the next bar's first note** when the chord
        changes - the move that makes a line sound like bebop rather than an
        arpeggio, and the same rule `walkingBass` has one register down.
      - **Scale tones in between**, stepping where a step is available, from
        the scale a take would read against rather than a scale of its own.
      - **Inside a soloist's register throughout**, for the reason the bass
        line is bounded: a line free to follow the harmony upward climbs off
        the keyboard inside a chorus.

    @param chosenScale  the scale the player picked, as `LineAnalyzer::Options`
                        takes it. Empty means the engine's own first answer.
                        Passing what the take will read against is what keeps
                        the written colours and the read ones the same.
    @param seed         the same seed gives the same line, note for note, in
                        both shells - `compPlan`'s contract and its hash.
*/
std::vector<WrittenNote> improvisedLine (const Chart& chart,
                                         int fromBar,
                                         int toBar,
                                         const std::string& chosenScale,
                                         const std::string& scaleStyle,
                                         std::uint32_t seed);

/** The register a written line stays inside. A soloist's two octaves, not a
    keyboard's seven: a line that wandered outside these is one a player cannot
    copy without moving their hands somewhere the exercise never asked for.
*/
constexpr int lowestLineNote = 55;    ///< G3
constexpr int highestLineNote = 84;   ///< C6

} // namespace jazz::core
