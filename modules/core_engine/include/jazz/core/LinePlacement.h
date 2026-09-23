#pragma once

#include "jazz/core/LineAnalyzer.h"
#include "jazz/core/LineStyle.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace jazz::core
{

/** Where a solo line put its notes, read against the style the player chose.

    **This is a second number and it is deliberately not reachable from the
    first.** `LineStats::score()` reads colour and nothing else, and the test
    that says so - *"where a note sat in the bar never moves the score"* - has
    to stay green. It stays green here by construction rather than by
    discipline: the placement reading lives in its own file, takes the style as
    an argument, and never touches a `LineStats`. `score()` could not consult
    it if somebody wanted it to.

    **Why solo practice may score placement at all.** `docs/SOLO_PRACTICE.md`
    closed that question and named the one thing that would reopen it - *"a
    rhythmic vocabulary a player deliberately picks to be held to"*. A
    `LineStyleDefinition` is that, chosen off the dock's own picker, and it is
    the same standing a `CompStyleDefinition` has when comping scores
    placement. The rule was never "solo practice does not score rhythm"; it was
    **a number needs a standard the player chose**, and until a line style
    existed solo practice had none. Take the style away and there is nothing
    here to read, which is why a shell that has not been told one must send no
    placement at all rather than a nought.

    **Only what the style states as a rule is in the number.** That is the line
    that keeps this explainable, and three of the style's own fields fall the
    other side of it:

      - `startTicks` is a *bias*, and `planPhrases` says so in as many words -
        "a weighting rather than a constraint". Scoring a bias would mark a
        player down for the 30% of phrases the generator itself starts
        elsewhere.
      - `descending` is a percentage over a corpus. A line that rose more than
        it fell is a line, not a fault.
      - `usesApproaches` is about colour, and colour is `score()`'s question
        already. Counting it here would mark the same note twice out of two
        numbers a player is looking at side by side.

    Each of them still produces words, which is the same division `LineAnalyzer`
    has always drawn and the same one `CompEvaluation` draws between its `fit`
    and its `observations`.

    **The engine still has no clock.** Everything below is arithmetic on
    `BarPosition`s the shell computed - `docs/RHYTHM.md`'s grid, nothing else.
    A take played with no transport carries no positions and gets no number,
    exactly as a comp does.

    @see docs/SOLO_PRACTICE.md, docs/COMPING.md
*/

/** Where one onset sits against the style's grid.

    Two tiers rather than comping's three, and the missing one is `theFigure`:
    a comping style writes down the actual chords the band plays, and a line
    style does not - it writes the subdivision, the phrase lengths and the
    register, and leaves the notes to the player. There is no figure to be on.

    Read per **onset** rather than per note. A block chord in a line is one
    moment with several notes in it, and counting its voices separately would
    quietly weight a chordal player's placement four times as heavily as a
    single-note player's. Register is the other way round and is read per note,
    because every voice of a chord has a register of its own.
*/
enum class OnsetPlacement
{
    onTheGrid,    ///< on the subdivision this style is written in
    offTheGrid,   ///< a subdivision this style does not write
    unplaced      ///< no clock behind it, so there is no placement to read
};

std::string onsetPlacementName (OnsetPlacement placement);

/** A run of notes and the silence after it.

    **A gap is read as a rest, and that is the honest limit of this.** A
    `LineNote` carries no duration - nothing in the engine does - so a long
    held note and a short note followed by silence arrive identically. The
    boundary is therefore a gap of at least the style's own shortest rest plus
    one step of its grid, which is exactly how `planPhrases` lays a rest out
    and comfortably longer than any gap inside a phrase.
*/
struct LinePhrase
{
    std::size_t firstNote {};   ///< index into the line the reading was given
    std::size_t lastNote {};

    /** Notes, counting a chord as one - the same onset the placement reads. */
    int onsets {};

    int measureIndex {};        ///< the bar it began in
    BarPosition startsAt {};

    /** It began on a tick this style likes to begin on. A bias, so it is said
        and never scored - see the note at the top of this file. */
    bool startedOnAStyleTick {};

    /** Onsets past the style's longest phrase, and zero when inside it.

        One direction only, and the reason is the one `CompBarReading::tooBusy`
        gives: a phrase shorter than the style's shortest is a player leaving
        space, which is one of the most idiomatic things anybody does. A phrase
        that runs on past the longest is the pitfall the research names - a
        line that never breathes - and is the half worth marking.
    */
    int overBy {};
};

/** A stretch of line, read back against the style it was played in. */
struct LinePlacementReading
{
    std::string styleKey;
    std::string styleName;

    /** 0-100: the grid, the phrasing and the register.

        Empty when nothing was played, and empty when nothing carried a
        position - not zero, for the reason `CompEvaluation::fit` gives and
        `LineStats` draws between nothing and zero. The grid is two of the four
        parts of this and the phrasing needs positions too, so without a clock
        there is no fit to report.
    */
    std::optional<int> fit;

    int gridFit {};       ///< the three parts, so a number can be explained
    int phraseFit {};
    int registerFit {};

    int onsets {};
    int onsetsPlaced {};
    int onsetsOnTheGrid {};

    int notes {};
    int notesInRegister {};

    /** What the line did on the bar's accented beats, over the whole take.

        `LineBar` has counted this per bar since the grid arrived and nothing
        has ever been able to see it. Words, here as there: where a note sits
        is not a better or worse note, and none of this is in `fit`.
    */
    int notesOnStrongBeats {};
    int chordTonesOnStrongBeats {};

    /** Approach notes that landed on an accented beat (R1).

        A rule the *engine* holds its own generator to - `lineFaults` refuses
        it - rather than one the style states, so it is said and not scored.
        That distinction is the whole of why this reading is explainable: the
        number contains what the player chose to be held to, and nothing the
        engine merely believes.
    */
    int chromaticsOnTheBeat {};

    std::vector<OnsetPlacement> placements;   ///< one per onset, in playing order
    std::vector<LinePhrase> phrases;

    std::string summary;

    /** The words half: everything true of the placement that is not in the
        number, and everything in the number a number alone does not say. */
    std::vector<std::string> observations;
};

/** Reads where a line put its notes, against the style it claims to be in.

    Takes what `improvisedLine` takes and consumes what a take produces, which
    is the same symmetry `evaluateComp` holds to `compPlan`: every line the
    writer writes in a style has to come back out of here fitting that style,
    or the two are two descriptions of one thing and have started to drift.

    @param line          the take, in playing order - `LineAnalyzer::notes()`.
    @param style         the one the player picked. There is no "no style"
                         overload on purpose: a caller with no style has
                         nothing to read and should not call this.
    @param beatsPerBar   the chart's metre, for which beats are strong.
*/
LinePlacementReading readLinePlacement (const std::vector<LineNote>& line,
                                        const LineStyleDefinition& style,
                                        int beatsPerBar);

} // namespace jazz::core
