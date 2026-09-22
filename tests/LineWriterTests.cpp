#include "TestFramework.h"
#include "jazz/core/LineWriter.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jazz::core;

namespace
{
    Chart chartFrom (const std::string& text)
    {
        const auto result = parseProgressionText (text);
        CHECK (result.ok());
        return *result.chart;
    }

    /** The written line played back through a take, exactly as a player would.

        This is the whole point of the feature and so it is the whole point of
        these tests: the analyser has to read back what the writer wrote. Run
        through a real take rather than through `read()`, because `read()` sees
        one note with no line around it and answers `outside` for anything
        chromatic by design - an approach note only exists once the note after
        it has arrived.
    */
    std::vector<LineNote> readBack (const Chart& chart,
                                    const std::vector<WrittenNote>& line,
                                    const std::string& chosenScale = "",
                                    const std::string& style = "")
    {
        LineAnalyzer::Options options;
        options.chosenScale = chosenScale;
        options.style = style;
        options.beatsPerBar = chart.timeSignature.numerator;

        LineAnalyzer analyzer;
        analyzer.setOptions (options);
        analyzer.startTake();

        auto bar = -1;

        for (const auto& note : line)
        {
            if (note.measureIndex != bar)
            {
                bar = note.measureIndex;

                if (const auto* chord = chart.chordAt (bar))
                    analyzer.setTarget (bar, *chord);
            }

            analyzer.play (note.midiNote, note.at);
        }

        analyzer.endTake();
        return analyzer.notes();
    }
}

TEST ("a written line is read back as the line it was written as")
{
    /*  The invariant this feature lives on. Same shape as
        `VoicingAnalyzerTests`' rule that every voicing `idiomaticVoicings`
        offers must classify as the type it was offered for, and the same
        reason: otherwise the app hands you a line and then marks it wrong. */
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    for (std::uint32_t seed = 1; seed <= 24; ++seed)
    {
        const auto written = improvisedLine (chart, 0, 3, "", "", seed);
        CHECK (! written.empty());

        const auto read = readBack (chart, written);
        CHECK_EQ (read.size(), written.size());

        for (std::size_t i = 0; i < read.size() && i < written.size(); ++i)
        {
            CHECK (read[i].midiNote == written[i].midiNote);
            CHECK (read[i].colour == written[i].colour);

            /*  The gesture is deliberately *not* asserted to be any particular
                one. The writer commits to the colour; which kind of approach a
                note turns out to be depends on the note after it and is the
                analyser's to name. What is asserted is that an approach note
                got home by some gesture rather than by none. */
            if (written[i].colour == NoteColour::approach)
                CHECK (read[i].approachKind != ApproachKind::none);
        }
    }
}

TEST ("the same seed writes the same line, note for note")
{
    const auto chart = chartFrom ("| Dm7 | G7 |");
    const auto once = improvisedLine (chart, 0, 1, "", "", 7);
    const auto again = improvisedLine (chart, 0, 1, "", "", 7);
    const auto other = improvisedLine (chart, 0, 1, "", "", 8);

    CHECK_EQ (once.size(), again.size());

    for (std::size_t i = 0; i < once.size(); ++i)
    {
        CHECK (once[i].midiNote == again[i].midiNote);
        CHECK (once[i].at.inTicks() == again[i].at.inTicks());
    }

    // And a different seed is a different line, or the seed does nothing.
    auto differs = once.size() != other.size();

    for (std::size_t i = 0; i < once.size() && i < other.size(); ++i)
        if (once[i].midiNote != other[i].midiNote || once[i].at.inTicks() != other[i].at.inTicks())
            differs = true;

    CHECK (differs);
}

TEST ("a written line stays inside a soloist's register")
{
    // A tune that climbs, for the reason the comping register test uses one:
    // following the harmony upward is exactly how a generator walks off the top.
    const auto chart = chartFrom ("| Cmaj7 | Ebmaj7 | Gbmaj7 | Amaj7 | Cmaj7 | Ebmaj7 |");

    for (std::uint32_t seed = 1; seed <= 12; ++seed)
        for (const auto& note : improvisedLine (chart, 0, 5, "", "", seed))
        {
            CHECK (note.midiNote >= lowestLineNote);
            CHECK (note.midiNote <= highestLineNote);
        }
}

TEST ("the strong beats carry the chord")
{
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    for (std::uint32_t seed = 1; seed <= 12; ++seed)
        for (const auto& note : improvisedLine (chart, 0, 3, "", "", seed))
        {
            if (! isStrong (note.at, 4))
                continue;

            /*  A strong beat is a chord tone, unless it is the note approaching
                the next bar - which is the one place a line is allowed to be
                outside on a strong beat, and is the whole gesture. */
            CHECK (note.colour == NoteColour::chordTone
                   || note.colour == NoteColour::approach);
        }
}

TEST ("a line writes nothing outside the bars it was asked for")
{
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    for (const auto& note : improvisedLine (chart, 1, 2, "", "", 3))
    {
        CHECK (note.measureIndex >= 1);
        CHECK (note.measureIndex <= 2);
        CHECK (note.at.beat >= 0);
        CHECK (note.at.beat < 4);
    }

    // Past the end of the chart is an empty line rather than a crash or a
    // bar that does not exist.
    CHECK (improvisedLine (chart, 9, 12, "", "", 3).empty());
    CHECK (improvisedLine (chart, -1, 2, "", "", 3).empty());
}

TEST ("a scale the player chose is the scale the line is built from")
{
    /*  The reason `readingScaleFor` is public. A line written against the
        engine's first answer and read against the player's choice colours its
        own notes wrong - so both ends ask the same question. */
    const auto chart = chartFrom ("| Dm7 | Dm7 |");
    const auto written = improvisedLine (chart, 0, 1, "D Dorian", "modes", 5);

    CHECK (! written.empty());

    const auto read = readBack (chart, written, "D Dorian", "modes");
    CHECK_EQ (read.size(), written.size());

    for (std::size_t i = 0; i < read.size() && i < written.size(); ++i)
        CHECK (read[i].colour == written[i].colour);
}

TEST ("a waltz is written in three")
{
    // The metre is the chart's, not the progression text's - so it is set the
    // way `CompingTests` sets one rather than spelled into the bars.
    auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 |");
    chart.timeSignature.numerator = 3;

    const auto written = improvisedLine (chart, 0, 2, "", "", 4);

    CHECK (! written.empty());

    for (const auto& note : written)
        CHECK (note.at.beat < 3);
}
