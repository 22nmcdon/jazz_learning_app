#include "TestFramework.h"
#include "jazz/core/ChordIdentifier.h"

#include <algorithm>

using namespace jazz::core;

namespace
{
    /** The best reading of a set of notes, or "-" when nothing explains them. */
    std::string nameOf (std::vector<int> notes)
    {
        const ChordIdentifier identifier;
        const auto candidates = identifier.identify (Voicing::fromNotes (std::move (notes)));

        return candidates.empty() ? "-" : candidates.front().chord.toString();
    }

    std::vector<std::string> readingsOf (std::vector<int> notes)
    {
        const ChordIdentifier identifier;
        std::vector<std::string> names;

        for (const auto& candidate : identifier.identify (Voicing::fromNotes (std::move (notes))))
            names.push_back (candidate.chord.toString());

        return names;
    }

    bool offers (const std::vector<std::string>& names, const std::string& name)
    {
        return std::find (names.begin(), names.end(), name) != names.end();
    }
}

TEST ("names plain triads")
{
    CHECK_EQ (nameOf ({ 60, 64, 67 }), std::string ("C"));
    CHECK_EQ (nameOf ({ 60, 63, 67 }), std::string ("Cm"));
    CHECK_EQ (nameOf ({ 60, 63, 66 }), std::string ("Cdim"));
    CHECK_EQ (nameOf ({ 60, 65, 67 }), std::string ("Csus4"));
}

TEST ("names seventh chords")
{
    CHECK_EQ (nameOf ({ 60, 64, 67, 71 }), std::string ("Cmaj7"));
    CHECK_EQ (nameOf ({ 60, 63, 67, 70 }), std::string ("Cm7"));
    CHECK_EQ (nameOf ({ 60, 64, 67, 70 }), std::string ("C7"));
    CHECK_EQ (nameOf ({ 60, 63, 66, 70 }), std::string ("Cm7b5"));
    CHECK_EQ (nameOf ({ 60, 63, 66, 69 }), std::string ("Cdim7"));
    CHECK_EQ (nameOf ({ 60, 63, 67, 71 }), std::string ("CmMaj7"));
}

TEST ("names extended chords")
{
    CHECK_EQ (nameOf ({ 60, 64, 67, 71, 74 }), std::string ("Cmaj9"));
    CHECK_EQ (nameOf ({ 60, 63, 67, 70, 74 }), std::string ("Cm9"));
    CHECK_EQ (nameOf ({ 60, 64, 67, 70, 74 }), std::string ("C9"));
    CHECK_EQ (nameOf ({ 60, 64, 70, 73 }), std::string ("C7b9"));
}

TEST ("prefers the simplest name that explains the notes")
{
    // C E G is a C major triad, not a Cmaj9 with two notes left out.
    CHECK_EQ (nameOf ({ 60, 64, 67 }), std::string ("C"));

    // C E G A is a C6 before it is an Am7 in an inversion.
    CHECK_EQ (nameOf ({ 60, 64, 67, 69 }), std::string ("C6"));
}

TEST ("names what is played, not what it might be missing a root from")
{
    // E G B D is Em7. G6 over E is the next best reading; the rootless Cmaj9
    // is further down, because a name that leaves out its own root is a worse
    // description of four notes than one that does not.
    const auto readings = readingsOf ({ 52, 55, 59, 62 });

    CHECK_EQ (readings.front(), std::string ("Em7"));
    CHECK (offers (readings, "G6/E"));

    const ChordIdentifier deeper { ChordIdentifier::Options { 10, true } };
    std::vector<std::string> names;

    for (const auto& candidate : deeper.identify (Voicing::fromNotes ({ 52, 55, 59, 62 })))
        names.push_back (candidate.chord.toString());

    CHECK (offers (names, "Cmaj9/E"));
}

TEST ("a name that promises colour it did not get ranks below one that does not")
{
    const ChordIdentifier identifier;
    const auto candidates = identifier.identify (Voicing::fromNotes ({ 60, 64, 71 }));

    // Cmaj7 without its 5th beats Cmaj9 without its 5th and its 9th.
    CHECK_EQ (candidates.front().chord.toString(), std::string ("Cmaj7"));
    CHECK (candidates.size() < std::size_t (3));
}

TEST ("puts the bass note in the name when it is not the root")
{
    // E G C: a C major triad over its own third.
    CHECK_EQ (nameOf ({ 52, 55, 60 }), std::string ("C/E"));

    // The same notes rooted on C are just C.
    CHECK_EQ (nameOf ({ 48, 52, 55 }), std::string ("C"));
}

TEST ("names a rootless voicing over the bass it is played on")
{
    // A rootless C13 shape: E Bb D A, with the bass player on C.
    const auto readings = readingsOf ({ 52, 58, 62, 69 });

    CHECK (! readings.empty());
    CHECK (offers (readings, "C13/E"));
}

TEST ("refuses to name a set of notes nothing explains")
{
    CHECK_EQ (nameOf ({ 60, 61, 62, 63 }), std::string ("-"));   // a chromatic cluster
    CHECK_EQ (nameOf ({ 60, 61, 66 }), std::string ("-"));
}

TEST ("says nothing about fewer than three notes")
{
    CHECK_EQ (nameOf ({ 60, 64 }), std::string ("-"));
    CHECK_EQ (nameOf ({ 60 }), std::string ("-"));
    CHECK_EQ (nameOf ({}), std::string ("-"));
}

TEST ("reports the tones a name implies but the player left out")
{
    const ChordIdentifier identifier;
    // C E B: a maj7 with no 5th.
    const auto candidates = identifier.identify (Voicing::fromNotes ({ 60, 64, 71 }));

    CHECK (! candidates.empty());
    CHECK_EQ (candidates.front().chord.toString(), std::string ("Cmaj7"));
    CHECK_EQ (candidates.front().omittedTones.size(), std::size_t (1));
    CHECK_EQ (candidates.front().omittedTones.front(), std::string ("5"));
}

TEST ("every reading accounts for every note played")
{
    const ChordIdentifier identifier;

    for (const auto& notes : std::vector<std::vector<int>> {
             { 60, 64, 67, 71 }, { 52, 55, 59, 62 }, { 53, 57, 60, 64 },
             { 48, 52, 55, 58, 62 }, { 55, 59, 62, 65, 69 } })
    {
        const auto voicing = Voicing::fromNotes (notes);

        for (const auto& candidate : identifier.identify (voicing))
            for (auto note : voicing.midiNotes)
                CHECK (candidate.chord.containsPitchClass (note));
    }
}

TEST ("readings are ordered best first")
{
    const ChordIdentifier identifier;
    const auto candidates = identifier.identify (Voicing::fromNotes ({ 52, 55, 59, 62 }));

    for (std::size_t i = 1; i < candidates.size(); ++i)
        CHECK (candidates[i - 1].score >= candidates[i].score);
}

TEST ("the number of readings can be capped")
{
    const ChordIdentifier identifier { ChordIdentifier::Options { 2, true } };
    CHECK (identifier.identify (Voicing::fromNotes ({ 52, 55, 59, 62 })).size() <= std::size_t (2));
}

TEST ("rootless readings can be turned off")
{
    const ChordIdentifier identifier { ChordIdentifier::Options { 6, false } };

    for (const auto& candidate : identifier.identify (Voicing::fromNotes ({ 52, 55, 59, 62 })))
        CHECK (candidate.rootPlayed);
}
