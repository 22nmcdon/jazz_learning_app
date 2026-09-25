#pragma once

#include "jazz/core/Rhythm.h"

#include <string>
#include <string_view>
#include <vector>

namespace jazz::core
{

/** How a line is built, and the standard it may be held to.

    The same shape as `CompStyleDefinition`, and deliberately: **one artifact,
    three readers.** `LineWriter` samples from it, a reading measures a player
    against it, and the menu is built from the same list. A style described in
    one place and re-described in another is how the two drift.

    **It absorbs the scale style rather than sitting beside it.** Solo practice
    used to ask two questions - which scales are in play, and nothing else -
    and in practice a player choosing "bebop" is choosing the bebop scales
    *and* the way bebop lines are shaped. Two pickers for one choice is the
    thing `CLAUDE.md`'s "a panel is one question" rule exists to prevent. The
    scale vocabulary is still `scaleStyles()`' - the key lives here, the list
    stays where the theory is - because the bar dialog still reads against it.

    **Why this one may score a player when nothing else in solo practice does.**
    `docs/SOLO_PRACTICE.md` closed the rhythm-scoring question and named the one
    thing that would reopen it: *"a rhythmic vocabulary a player deliberately
    picks to be held to"*. That is what this is. A chart names the chord and has
    never said where in the bar a note belongs - but a line style does, off a
    menu, the same way a `CompStyleDefinition` does for a comp. Take the style
    away and there is nothing here to score, which is why a placement reading
    must stay silent until one is chosen.
*/
struct LineStyleDefinition
{
    std::string key;       ///< stable across versions; what a shell stores
    std::string name;      ///< "Bebop"
    std::string summary;   ///< one line, for the menu

    /** Which scales this style draws on - a `ScaleStyle::key`.

        A key rather than the families themselves, so there is still exactly
        one list of which scales belong together and it is still `Scale.cpp`'s.
    */
    std::string scaleStyle;

    /** A phrase is a run of notes and then a rest (R13).

        Written down as a range rather than an average because the pitfall the
        research names is "every phrase exactly 1, 2 or 4 bars long and
        starting on beat 1" - which is a description of a generator with no
        phrase model at all, and was a fair description of this one.
    */
    int shortestPhrase { 4 };
    int longestPhrase { 12 };

    /** The silence after a phrase, in ticks. Space is what makes a line
        breathe, and it is also where a player listening gets to hear the
        band. */
    int shortestRest { ticksPerBeat };
    int longestRest { ticksPerBeat * 4 };

    /** Where a phrase may begin, as ticks into a beat, commonest first (R14).

        Offbeat starts are the idiom - a chromatic pickup on the "and" of four
        into the downbeat, or a start on the "and" of one - and a generator
        that always began on the beat is the pitfall named above wearing a
        different hat.
    */
    std::vector<int> startTicks { ticksPerBeat / 2, 0 };

    /** The subdivision this style's notes are written on (R15). */
    Subdivision feel { Subdivision::eighth };

    /** How often the line steps down rather than up, 0-100 (R9).

        Parker's intervals are 54% descending, 44.6% ascending; approaches in
        the Weimar corpus descend 64% of the time. A line with no bias reads as
        a scale exercise going nowhere.
    */
    int descending { 54 };

    /** Whether this style writes chromatic approach notes at all (R2).

        Off for the modal and pentatonic styles, where the sound is the scale
        rather than the voice leading, and a chromatic would be an intrusion
        rather than colour.
    */
    bool usesApproaches { true };

    /** How often a phrase of this style **reaches for** a quote, 0-100.

        Not the share of the line that ends up quoted, and the difference is
        worth stating because it is large: a phrase that reaches finds nothing
        to quote whenever no lick fits the harmony under it, and then generates
        as it always did. Measured over two hundred seeds, bebop asking 60 gets
        39% of its notes from the catalogue on a standard, blues asking 70 gets
        64% on a twelve-bar, modal asking 25 gets 12% on So What and pentatonic
        asking 45 gets 33% there. A tune the catalogue was not written for gets
        less again, which is correct - that same twelve-bar gives both
        pentatonic-scaled styles nothing at all, because neither has a figure
        written over a dominant.


        The research's own suggestion is 50-70% drawn from authored cells with
        generated atoms filling the gaps, and it says in as many words that the
        ratio is a design suggestion to tune by ear - so this is a number
        somebody is expected to argue with rather than a finding.

        Per style because the styles are not equally quotable. Blues phrasing
        is made of figures a player has heard a thousand times; a modal line is
        a sound rather than a vocabulary, and the catalogue has two licks for it
        against a dozen for bebop. A style that asked for more quoting than the
        catalogue can supply simply generates the rest - `licksFitting` returns
        nothing and the phrase is written note by note, which is the whole of
        what "atom fallback" means.
    */
    int lickShare { 60 };

    /** The register a line of this style stays inside (R12).

        A soloist's two octaves, not a keyboard's seven: a line that wandered
        outside these is one a player cannot copy without moving their hands
        somewhere the exercise never asked for.
    */
    int lowestNote { 55 };    ///< G3
    int highestNote { 84 };   ///< C6
};

/** Every line style there is, in the order a menu would show them. */
const std::vector<LineStyleDefinition>& lineStyles();

/** The style with this key, or the first one when the key is unknown.

    Never empty, and forgiving for the reason `compStyleFor` gives: a stored
    key this version has renamed should still write a line. Reference into a
    function-local `static`; `string_view` for the -Wdangling-reference reason
    written beside `compStyleFor`.
*/
const LineStyleDefinition& lineStyleFor (std::string_view key);

} // namespace jazz::core
