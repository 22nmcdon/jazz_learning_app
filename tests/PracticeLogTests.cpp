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
