#include "TestFramework.h"
#include "jazz/core/PracticeLog.h"

#include <string>
#include <vector>

using namespace jazz::core;

namespace
{
    bool says (const std::vector<std::string>& lines, const std::string& fragment)
    {
        for (const auto& line : lines)
            if (line.find (fragment) != std::string::npos)
                return true;

        return false;
    }

    LineStats notesOf (int chordTones, int scaleTones, int approachTones,
                       int unresolved, int outside)
    {
        LineStats stats;
        stats.chordTones = chordTones;
        stats.scaleTones = scaleTones;
        stats.approachTones = approachTones;
        stats.unresolved = unresolved;
        stats.outside = outside;

        return stats;
    }

    /** A soloing take on a given day, over the harmony of a ii-V-I in C unless
        told otherwise. */
    PracticeTake soloTake (int day, LineStats notes)
    {
        PracticeTake take;
        take.day = day;
        take.mode = PracticeMode::soloing;
        take.seconds = 60;
        take.notes = notes;
        take.qualities = qualityBit (ChordQuality::minor)
                       | qualityBit (ChordQuality::dominant)
                       | qualityBit (ChordQuality::major);
        take.roots = rootBit (2) | rootBit (7) | rootBit (0);

        return take;
    }

    Chart chartOf (const std::string& progression)
    {
        auto parsed = parseProgressionText (progression);
        CHECK (parsed.ok());

        return *parsed.chart;
    }
}

TEST ("an empty record says so and reads nothing into it")
{
    const auto reading = readPractice ({}, 100);

    CHECK_EQ (reading.takes, 0);
    CHECK (reading.observations.empty());
    CHECK (reading.summary.find ("Nothing practised yet") != std::string::npos);

    // Not "no chords played over": an empty record has nothing to be missing
    // from, and listing all eight qualities as untouched on a first visit would
    // be a wall of absence rather than a reading.
    CHECK (reading.qualitiesMissing.empty());
    CHECK (reading.qualitiesMet.empty());
}

TEST ("the headline counts takes, days and minutes rather than grading them")
{
    std::vector<PracticeTake> takes {
        soloTake (10, notesOf (8, 4, 0, 0, 2)),
        soloTake (10, notesOf (6, 6, 0, 0, 1)),
        soloTake (12, notesOf (9, 3, 0, 0, 0))
    };

    const auto reading = readPractice (takes, 14);

    CHECK_EQ (reading.takes, 3);
    CHECK_EQ (reading.daysPractised, 2);   // two takes on day 10 are one day
    CHECK_EQ (reading.span, 3);            // days 10 to 12 inclusive
    CHECK_EQ (reading.daysSinceLast, 2);
    CHECK_EQ (reading.minutes, 3);
    CHECK (reading.summary.find ("3 takes over 2 days") != std::string::npos);
    CHECK (reading.summary.find ("2 days ago") != std::string::npos);
}

TEST ("a row dated after today counts as today rather than as a negative gap")
{
    // A clock that went back is not an error the engine gets to have an opinion
    // about - it has no clock of its own to check against.
    const auto reading = readPractice ({ soloTake (200, notesOf (4, 4, 0, 0, 0)) }, 100);

    CHECK_EQ (reading.daysSinceLast, 0);
    CHECK (reading.summary.find ("today") != std::string::npos);
}

TEST ("coverage names what has not been played over")
{
    const auto reading = readPractice ({ soloTake (1, notesOf (10, 5, 0, 0, 1)) }, 1);

    CHECK_EQ (static_cast<int> (reading.qualitiesMet.size()), 3);
    CHECK_EQ (reading.qualitiesMet[0], std::string ("major"));
    CHECK_EQ (static_cast<int> (reading.qualitiesMissing.size()), 5);
    CHECK (says (reading.observations, "Everything so far has been over"));
}

TEST ("three missing qualities are named, and the minor ii-V is the one pointed at")
{
    auto take = soloTake (1, notesOf (10, 5, 0, 0, 1));

    for (auto quality : { ChordQuality::diminished, ChordQuality::augmented })
        take.qualities |= qualityBit (quality);

    take.qualities |= qualityBit (ChordQuality::minorMajor);

    const auto reading = readPractice ({ take }, 1);

    CHECK_EQ (static_cast<int> (reading.qualitiesMissing.size()), 2);
    CHECK (says (reading.observations, "Nothing yet over half-diminished and suspended chords"));
    CHECK (says (reading.observations, "half of every minor ii-V"));
}

TEST ("the keys played are named when most of them have not been")
{
    const auto reading = readPractice ({ soloTake (1, notesOf (10, 5, 0, 0, 1)) }, 1);

    CHECK_EQ (static_cast<int> (reading.rootsMet.size()), 3);
    CHECK (says (reading.observations, "3 keys so far"));
    CHECK (says (reading.observations, "C, D and G"));
}

TEST ("a record covering all twelve roots says so instead")
{
    auto take = soloTake (1, notesOf (10, 5, 0, 0, 1));

    for (PitchClass root = 0; root < 12; ++root)
        take.roots |= rootBit (root);

    const auto reading = readPractice ({ take }, 1);

    CHECK (reading.rootsMissing.empty());
    CHECK (says (reading.observations, "Every one of the twelve roots"));
}

TEST ("movement is silent until there is enough of a record to be a habit")
{
    std::vector<PracticeTake> takes;

    // Five takes that swing wildly. Five is one short of the floor on purpose:
    // the point is that a big difference is still not reported.
    for (auto i = 0; i < 5; ++i)
        takes.push_back (soloTake (i, i < 2 ? notesOf (2, 2, 0, 0, 16)
                                            : notesOf (16, 4, 0, 0, 0)));

    const auto reading = readPractice (takes, 5);

    CHECK (! says (reading.observations, "Lately your line"));
}

TEST ("movement reads earlier against lately, in counts and never in a mark")
{
    std::vector<PracticeTake> takes;

    for (auto i = 0; i < 3; ++i)
        takes.push_back (soloTake (i, notesOf (4, 4, 0, 0, 12)));      // 60% outside

    for (auto i = 3; i < 6; ++i)
        takes.push_back (soloTake (i, notesOf (12, 6, 0, 0, 2)));      // 10% outside

    const auto reading = readPractice (takes, 6);

    CHECK (says (reading.observations, "Lately your line is 10% notes outside the harmony"));
    CHECK (says (reading.observations, "against 60%"));

    // The thing this feature exists not to do. `score()` is a reading of a bar,
    // and a bar's reading laid end to end over weeks is the grade it refuses to
    // be - so no sentence here carries one.
    CHECK (! says (reading.observations, "score"));
    CHECK (! says (reading.observations, "out of 100"));
}

TEST ("a rate that barely moved is not narrated")
{
    std::vector<PracticeTake> takes;

    for (auto i = 0; i < 3; ++i)
        takes.push_back (soloTake (i, notesOf (10, 6, 0, 0, 4)));      // 20% outside

    for (auto i = 3; i < 6; ++i)
        takes.push_back (soloTake (i, notesOf (10, 7, 0, 0, 3)));      // 15% outside

    const auto reading = readPractice (takes, 6);

    CHECK (! says (reading.observations, "Lately your line"));
}

TEST ("comping takes count as practice and contribute no notes")
{
    PracticeTake comp;
    comp.day = 4;
    comp.mode = PracticeMode::comping;
    comp.seconds = 120;
    comp.hitsOnTheFigure = 6;
    comp.qualities = qualityBit (ChordQuality::dominant);
    comp.roots = rootBit (7);

    const auto reading = readPractice ({ comp }, 4);

    CHECK_EQ (reading.compTakes, 1);
    CHECK_EQ (reading.soloTakes, 0);
    CHECK_EQ (reading.notes.total(), 0);
    CHECK_EQ (reading.minutes, 2);
    CHECK_EQ (reading.daysPractised, 1);
}

TEST ("a record of nothing but soloing is told where the other half is")
{
    std::vector<PracticeTake> takes;

    for (auto i = 0; i < 4; ++i)
        takes.push_back (soloTake (i, notesOf (6, 4, 0, 0, 1)));

    const auto reading = readPractice (takes, 4);

    CHECK (says (reading.observations, "All of it was soloing"));

    // And the mirror: a comping-only record is pointed the other way, so
    // neither half is the one the record assumes you meant.
    std::vector<PracticeTake> comps;

    for (auto i = 0; i < 4; ++i)
    {
        PracticeTake comp;
        comp.day = i;
        comp.mode = PracticeMode::comping;
        comps.push_back (comp);
    }

    CHECK (says (readPractice (comps, 4).observations, "All of it was comping"));
}

TEST ("chordal playing is counted apart from the line and marked the same")
{
    auto take = soloTake (1, notesOf (12, 4, 0, 0, 2));
    take.chordsPlayed = 5;

    const auto reading = readPractice ({ take }, 1);

    CHECK (says (reading.observations, "5 chords played inside a line"));
    CHECK (says (reading.observations, "never scored differently"));
}

TEST ("coverageOf reads the harmony the engine already knows, not a shell's guess")
{
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Am7b5 |");

    unsigned int qualities = 0;
    unsigned int roots = 0;
    coverageOf (chart, 0, 3, qualities, roots);

    CHECK (qualities & qualityBit (ChordQuality::minor));
    CHECK (qualities & qualityBit (ChordQuality::dominant));
    CHECK (qualities & qualityBit (ChordQuality::major));
    CHECK (qualities & qualityBit (ChordQuality::halfDiminished));
    CHECK (! (qualities & qualityBit (ChordQuality::diminished)));

    CHECK (roots & rootBit (2));    // D
    CHECK (roots & rootBit (7));    // G
    CHECK (roots & rootBit (0));    // C
    CHECK (roots & rootBit (9));    // A
    CHECK (! (roots & rootBit (1)));
}

TEST ("coverageOf clamps a range the chart does not have rather than reading past it")
{
    const auto chart = chartOf ("| Dm7 | G7 |");

    unsigned int qualities = 0;
    unsigned int roots = 0;

    // A take whose last bar was never finished, or a chart edited shorter since
    // the take - both reach here, and neither may walk off the end.
    coverageOf (chart, -3, 99, qualities, roots);

    CHECK (qualities & qualityBit (ChordQuality::minor));
    CHECK (qualities & qualityBit (ChordQuality::dominant));
    CHECK_EQ (static_cast<int> (roots), static_cast<int> (rootBit (2) | rootBit (7)));
}

//  --- what one tune remembers ---------------------------------------------

namespace
{
    /** The take-wide counts are not derived from the bars by the engine - the
        header says so - so a test that wants them consistent adds them itself. */
    void sumInto (LineStats& into, const LineStats& more)
    {
        into.chordTones += more.chordTones;
        into.scaleTones += more.scaleTones;
        into.approachTones += more.approachTones;
        into.unresolved += more.unresolved;
        into.outside += more.outside;
    }

    /** A take over @p reached, each bar carrying @p notes. */
    PracticeTake takeOver (int day, const std::vector<int>& reached, LineStats notes)
    {
        PracticeTake take;
        take.day = day;
        take.mode = PracticeMode::soloing;
        take.tune = 1;

        for (auto bar : reached)
        {
            PracticeBar played;
            played.measureIndex = bar;
            played.notes = notes;
            take.bars.push_back (played);
            sumInto (take.notes, notes);
        }

        return take;
    }

    const std::string twelveBars =
        "| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 | Am7b5 | D7alt | Gm7 | Gm7 |";
}

TEST ("a tune never played still draws every bar of itself")
{
    const auto progress = readTuneProgress (chartOf (twelveBars), {}, 10);

    CHECK_EQ (progress.takes, 0);
    CHECK_EQ (progress.barsInChart, 12);
    CHECK_EQ (static_cast<int> (progress.bars.size()), 12);
    CHECK_EQ (progress.bars[8].chordSymbol, std::string ("Am7b5"));
    CHECK (progress.summary.find ("Never played") != std::string::npos);
    CHECK (progress.observations.empty());
}

TEST ("one take is not a habit, so nothing is read across takes yet")
{
    const auto progress = readTuneProgress (chartOf (twelveBars),
                                            { takeOver (3, { 0, 1, 2, 3 }, notesOf (4, 2, 0, 0, 1)) },
                                            3);

    CHECK_EQ (progress.takes, 1);
    CHECK_EQ (progress.bars[0].takes, 1);
    CHECK_EQ (progress.bars[7].takes, 0);

    // Every bar it reached was reached in "every take", which is true and worth
    // nothing. Bars 5-12 have "never been reached", which after one take is a
    // statement about the take rather than about the player.
    CHECK (progress.observations.empty());
}

TEST ("the front half of a tune gets the practice, and the reading says which bars")
{
    std::vector<PracticeTake> takes;

    for (auto day = 1; day <= 6; ++day)
        takes.push_back (takeOver (day, { 0, 1, 2, 3 }, notesOf (4, 2, 0, 0, 1)));

    // Twice, somebody made it round to the last four.
    takes.push_back (takeOver (7, { 0, 1, 2, 3, 8, 9, 10, 11 }, notesOf (4, 2, 0, 0, 1)));
    takes.push_back (takeOver (8, { 0, 1, 2, 3, 8, 9, 10, 11 }, notesOf (4, 2, 0, 0, 1)));

    const auto progress = readTuneProgress (chartOf (twelveBars), takes, 8);

    CHECK_EQ (progress.takes, 8);
    CHECK_EQ (progress.daysPractised, 8);
    CHECK_EQ (progress.bars[0].takes, 8);
    CHECK_EQ (progress.bars[8].takes, 2);
    CHECK_EQ (progress.bars[4].takes, 0);

    CHECK (says (progress.observations, "Bars 1-4 have been in every take"));
    CHECK (says (progress.observations, "9-12 in no more than 2 takes of 8"));
    CHECK (says (progress.observations, "Bars 5-8 have never been reached"));
}

TEST ("a tune played end to end every time is not handed a sentence about neglect")
{
    std::vector<PracticeTake> takes;
    const std::vector<int> wholeChorus { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    for (auto day = 1; day <= 5; ++day)
        takes.push_back (takeOver (day, wholeChorus, notesOf (4, 2, 0, 0, 1)));

    const auto progress = readTuneProgress (chartOf (twelveBars), takes, 5);

    CHECK (! says (progress.observations, "in every take"));
    CHECK (! says (progress.observations, "never been reached"));
}

TEST ("the bar that keeps pulling away is named, and only when it stands apart")
{
    std::vector<PracticeTake> takes;

    for (auto day = 1; day <= 4; ++day)
    {
        auto take = takeOver (day, { 0, 1, 2, 3 }, notesOf (5, 2, 0, 0, 0));

        // Bar 10 - the altered dominant - is where it keeps going outside.
        PracticeBar hard;
        hard.measureIndex = 9;
        hard.notes = notesOf (1, 1, 0, 0, 5);
        take.bars.push_back (hard);
        sumInto (take.notes, hard.notes);

        takes.push_back (take);
    }

    const auto progress = readTuneProgress (chartOf (twelveBars), takes, 4);

    CHECK (says (progress.observations, "Bar 10 (D7alt) is the one that keeps pulling away"));

    // And with the same line everywhere, no bar stands apart from the rest.
    std::vector<PracticeTake> even;

    for (auto day = 1; day <= 4; ++day)
        even.push_back (takeOver (day, { 0, 1, 2, 3, 9 }, notesOf (3, 2, 0, 0, 2)));

    CHECK (! says (readTuneProgress (chartOf (twelveBars), even, 4).observations,
                   "keeps pulling away"));
}

TEST ("bars the chart no longer has are dropped rather than drawn")
{
    std::vector<PracticeTake> takes;

    // Played when the tune was twelve bars; the chart on the stand is now four.
    for (auto day = 1; day <= 3; ++day)
        takes.push_back (takeOver (day, { 0, 1, 9, 11 }, notesOf (4, 2, 0, 0, 1)));

    const auto progress = readTuneProgress (chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |"), takes, 3);

    CHECK_EQ (progress.barsInChart, 4);
    CHECK_EQ (static_cast<int> (progress.bars.size()), 4);
    CHECK_EQ (progress.bars[0].takes, 3);
    CHECK_EQ (progress.bars[2].takes, 0);
}

TEST ("a run of one bar is written as a number, not as a range")
{
    std::vector<PracticeTake> takes;

    for (auto day = 1; day <= 4; ++day)
        takes.push_back (takeOver (day, { 0 }, notesOf (4, 2, 0, 0, 1)));

    takes.push_back (takeOver (5, { 0, 2 }, notesOf (4, 2, 0, 0, 1)));

    const auto progress = readTuneProgress (chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |"), takes, 5);

    CHECK (says (progress.observations, "Bar 1 has been in every take"));
    CHECK (says (progress.observations, "3 in no more than 1 take of 5"));
    CHECK (says (progress.observations, "Bars 2 and 4 have never been reached"));
}

TEST ("coverage reads a list of symbols for a take that never had a chart")
{
    unsigned int qualities = 0;
    unsigned int roots = 0;

    // Solo practice is told its bars one at a time, as symbols - there is no
    // Chart anywhere in that path.
    coverageOf ({ "Dm7", "G7", "Cmaj7" }, qualities, roots);

    CHECK (qualities & qualityBit (ChordQuality::minor));
    CHECK (qualities & qualityBit (ChordQuality::dominant));
    CHECK (qualities & qualityBit (ChordQuality::major));
    CHECK_EQ (static_cast<int> (roots), static_cast<int> (rootBit (2) | rootBit (7) | rootBit (0)));
}

TEST ("a symbol the engine cannot name is a gap in the coverage, not a refusal")
{
    unsigned int qualities = 0;
    unsigned int roots = 0;

    // The take happened. A bar whose symbol will not parse should cost that
    // bar's harmony, never the whole take's.
    coverageOf ({ "Dm7", "", "%%%", "G7" }, qualities, roots);

    CHECK_EQ (static_cast<int> (roots), static_cast<int> (rootBit (2) | rootBit (7)));
    CHECK (qualities & qualityBit (ChordQuality::minor));
    CHECK (qualities & qualityBit (ChordQuality::dominant));
}
