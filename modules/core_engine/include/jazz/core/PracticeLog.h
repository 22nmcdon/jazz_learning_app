#pragma once

#include "jazz/core/Chart.h"
#include "jazz/core/LineAnalyzer.h"

#include <string>
#include <vector>

namespace jazz::core
{

/*  What a practice record keeps, and what it deliberately does not.

    Every other reader in this engine is handed one take and says what that take
    did. This one is handed *many* takes and says what the player has been
    doing, which is a different question and the only one a single take cannot
    answer: not how well it went, but what was practised - and, far more useful,
    what was not.

    Three things hold this apart from a scoreboard.

    **There is no memory here.** These are free functions over a vector the
    caller owns. The engine gains no store, no registry and no clock: `today`
    arrives as an argument, the way a `BarPosition` does, because the shell owns
    the clock and this file must stay as testable as `LineAnalyzer`. A history
    is data the shell hands over on every call, exactly like a chart's
    progression text or a described comping style.

    **No number is kept across time.** `LineStats::score()` says of itself that
    it is "a reading of a bar, not a grade for a player", and a mark plotted over
    weeks is precisely the grade it refuses to be. So nothing here reads it, and
    nothing here reports one. `LineStats` is reused for the counts because its
    percentages are carefully made to add to 100 and a second implementation of
    that rounding would be a second implementation to drift - but the score is
    the one thing on it this file never calls. A count is a fact; a fact set
    beside an earlier fact is still a fact. A mark set beside an earlier mark is
    a verdict about a person.

    **What is remembered is that you practised, never what you played.** A row
    is counts, a day number and which harmony was under them. There is no line
    here, no voicing, no note - nothing from which a performance could be
    reconstructed, and nothing that would let this file grow into a second
    opinion about a take that `LineAnalyzer` has already read.
*/

/** Which half of the app a take was.

    Both halves keep a row. A record that counted only the soloing would say a
    player had not practised on the days they spent comping, which is the one
    thing a practice record must not get wrong.
*/
enum class PracticeMode
{
    soloing,
    comping
};

/** One bar of one take, as counts.

    The measure index is the bar's place in the chart, which is what makes the
    per-tune reading possible: the same bar of the same tune, across every take
    over it, is the thing nobody can see from inside one take.
*/
struct PracticeBar
{
    int measureIndex {};
    LineStats notes;
};

/** One finished take.

    Flat and small on purpose - this is what a shell has to store, and a shell's
    store is somebody's browser. Everything here is an integer or a bitmask.
*/
struct PracticeTake
{
    /** Days since whatever epoch the shell counts from, and the only time-like
        thing in this file. It must be the same epoch as the `today` handed to
        `readPractice`; nothing here can check that, so the shell owes it. */
    int day {};

    PracticeMode mode { PracticeMode::soloing };

    /** The saved tune this take was over, or 0 for a chart that was never
        saved. A number rather than a name, which is what keeps the wire format
        free of quoting - the same reason a described comping style carries no
        name. */
    int tune {};

    /** Wall-clock length, which only a shell can know. Zero is allowed and
        means "not measured", never "instant". */
    int seconds {};

    /** One bit per `ChordQuality`, and one per pitch class: the harmony this
        take was played over.

        A bitmask rather than a list because the question asked of it is only
        ever "has this ever happened", and because the union of thirty takes has
        to stay cheap. */
    unsigned int qualities {};
    unsigned int roots {};

    /** Every note of the take, by tier, and every bar of it separately.

        `notes` is not derived from `bars` and must not be assumed to be: a
        shell may keep the take-wide counts for a take whose bars it has
        dropped, and `readPractice` reads the take-wide ones. */
    LineStats notes;
    std::vector<PracticeBar> bars;

    /** Line shape, for the take-wide reading. All counts, no marks. */
    int leaps {};
    int leapsResolved {};
    int chordsPlayed {};

    /** Comping counts, from `CompEvaluation`. Zero on a soloing row.

        `CompEvaluation::fit` is deliberately absent, for the reason at the top
        of this file: it is the comping half's score. */
    int hitsOnTheFigure {};
    int hitsIdiomatic {};
    int hitsPushed {};
    int hitsOffStyle {};
};

/** Sets the bit for a chord quality or a pitch class in one of the masks
    above. Free functions rather than methods so a shell reading a stored row
    builds the mask the same way the engine does. */
unsigned int qualityBit (ChordQuality quality) noexcept;
unsigned int rootBit (PitchClass root) noexcept;

/** The harmony covered by a stretch of chart, as the two masks above.

    This is where a take's coverage comes from: the engine names which quality a
    symbol is, because which quality a symbol is is theory, and a shell holding
    its own copy of that is a copy to go wrong the first time a symbol is
    spelled a new way.
*/
void coverageOf (const Chart& chart, int fromBar, int toBar,
                 unsigned int& qualities, unsigned int& roots);

/** How much practice, over what, and what has been left alone. */
struct PracticeReading
{
    int takes {};
    int bars {};
    int minutes {};

    /** Distinct days with a take on them, and the width of the record in days.

        Two numbers rather than one because they say different things: six days
        out of seven is practice, six days out of ninety is six days. */
    int daysPractised {};
    int span {};
    int daysSinceLast {};

    int soloTakes {};
    int compTakes {};

    /** Named, not masked, because this is what a panel draws and a sentence
        reads. Missing is the half worth having: a player knows what they have
        been playing. */
    std::vector<std::string> qualitiesMet;
    std::vector<std::string> qualitiesMissing;
    std::vector<std::string> rootsMet;
    std::vector<std::string> rootsMissing;

    /** Every soloed note in the record, by tier. Comping rows contribute
        nothing here - a comp has hits, not a line. */
    LineStats notes;

    std::string summary;

    /** The words half, most useful first, in the voice `LineAnalyzer` uses:
        a note outside the scale is outside the scale, never wrong, and a thing
        not practised is not practised, never neglected.
    */
    std::vector<std::string> observations;
};

/** Reads a whole practice record.

    @param takes  in any order; this sorts its own copy by day.
    @param today  the shell's day number, on the same epoch as `PracticeTake::day`.
                  A row dated after it is not an error - a clock can go back -
                  and counts as today.
*/
PracticeReading readPractice (const std::vector<PracticeTake>& takes, int today);

} // namespace jazz::core
