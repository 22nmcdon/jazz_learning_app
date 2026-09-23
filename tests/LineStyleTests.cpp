#include "TestFramework.h"

#include "jazz/core/LineStyle.h"
#include "jazz/core/Scale.h"

#include <set>

using namespace jazz::core;

TEST ("every line style is filled in, and says something a generator can use")
{
    for (const auto& style : lineStyles())
    {
        CHECK (! style.key.empty());
        CHECK (! style.name.empty());
        CHECK (! style.summary.empty());

        // A phrase range that is not one would make the planner's modulo
        // arithmetic meaningless rather than loud.
        CHECK (style.shortestPhrase >= 1);
        CHECK (style.longestPhrase >= style.shortestPhrase);

        CHECK (style.shortestRest >= 1);
        CHECK (style.longestRest >= style.shortestRest);

        CHECK (! style.startTicks.empty());

        for (auto tick : style.startTicks)
        {
            CHECK (tick >= 0);
            CHECK (tick < ticksPerBeat);
        }

        CHECK (style.descending >= 0);
        CHECK (style.descending <= 100);

        CHECK (style.lowestNote >= 0);
        CHECK (style.highestNote > style.lowestNote);
    }
}

TEST ("a line style names a scale vocabulary the engine actually has")
{
    /*  The line style absorbs the scale style rather than replacing it, so the
        key it carries has to be one `scaleStyles()` still knows. A typo here
        would not fail loudly - `findScaleStyle` returns null and the reading
        silently widens to every scale, which is the "nothing is outside
        anything" collapse `LineAnalyzer`'s own header describes. */
    for (const auto& style : lineStyles())
    {
        CHECK (! style.scaleStyle.empty());
        CHECK (findScaleStyle (style.scaleStyle) != nullptr);
    }
}

TEST ("the line styles on offer are genuinely different from one another")
{
    std::set<std::string> keys;

    for (const auto& style : lineStyles())
        keys.insert (style.key);

    CHECK_EQ (keys.size(), lineStyles().size());

    /*  Different in the things that make a line sound like itself, not only in
        their names. Bebop runs longer than blues and leaves less room after a
        phrase; modal and pentatonic write nothing chromatic at all. Checked at
        both ends, because a catalogue fitted to one entry passes every test
        written about that entry. */
    const auto& bebop = lineStyleFor ("bebop");
    const auto& blues = lineStyleFor ("blues");

    CHECK (bebop.longestPhrase > blues.longestPhrase);
    CHECK (bebop.longestRest < blues.longestRest);

    CHECK (bebop.usesApproaches);
    CHECK (blues.usesApproaches);
    CHECK (! lineStyleFor ("modal").usesApproaches);
    CHECK (! lineStyleFor ("pentatonic").usesApproaches);
}

TEST ("a line style nobody has heard of still writes a line")
{
    // The promise `compStyleFor` and `grooveFor` both make, for the same
    // reason: a stored key this version has renamed should still play.
    CHECK_EQ (lineStyleFor ("no-such-style").key, lineStyles().front().key);
    CHECK_EQ (lineStyleFor ("").key, lineStyles().front().key);

    // And the fallback is bebop, which is the vocabulary `LineAnalyzer` was
    // built to read - not an arbitrary first entry.
    CHECK_EQ (lineStyles().front().key, std::string ("bebop"));
}
