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

    // Cmaj7 without its 5th beats Cmaj9 without its 5th and its 9th; further
    // readings exist (C E B is also a rootless Am add9) but rank below both.
    CHECK_EQ (candidates.front().chord.toString(), std::string ("Cmaj7"));
    CHECK_EQ (candidates[1].chord.toString(), std::string ("Cmaj9"));

    for (std::size_t i = 1; i < candidates.size(); ++i)
        CHECK (candidates[i].omittedTones.size() >= candidates.front().omittedTones.size());
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

TEST ("names a suspended thirteenth")
{
    // Eb Bb C Db F Ab - a sus chord with the 9th and 13th in it, which is a
    // sound in its own right rather than a 9sus4 with a note left over.
    CHECK_EQ (nameOf ({ 51, 58, 60, 61, 65, 68 }), std::string ("Eb13sus4"));
}

TEST ("the slash readings of a sus13 are still offered, just not first")
{
    const auto readings = readingsOf ({ 51, 58, 60, 61, 65, 68 });

    CHECK_EQ (readings.front(), std::string ("Eb13sus4"));
    CHECK (offers (readings, "Dbmaj13/Eb"));
    CHECK (offers (readings, "Bbm11/Eb"));
}

TEST ("names the chords added alongside the suspended thirteenth")
{
    CHECK_EQ (nameOf ({ 60, 64, 68, 71 }), std::string ("Cmaj7#5"));
    CHECK_EQ (nameOf ({ 60, 63, 67, 69, 74 }), std::string ("Cm6/9"));
    CHECK_EQ (nameOf ({ 60, 63, 67, 71, 74 }), std::string ("CmMaj9"));
    CHECK_EQ (nameOf ({ 60, 64, 66, 70 }), std::string ("C7b5"));
    CHECK_EQ (nameOf ({ 60, 65, 67, 70, 73 }), std::string ("C7sus4b9"));
}

//==============================================================================
TEST ("an altered dominant with no sharp eleventh still has a name")
{
    // D F# C F Bb Eb - what "alt" is when the player leaves the #11 out, which
    // is most of the time. This was unnameable until the dominant tensions were
    // generated rather than listed.
    CHECK_EQ (nameOf ({ 38, 42, 48, 53, 58, 63 }), std::string ("D7b9#9b13"));
}

TEST ("every dominant the tensions can spell can be named")
{
    // The ninth in each of its forms, with or without the sharp eleventh, with
    // or without a thirteenth: the whole family, not a list of the ones someone
    // remembered. Each is built from its own chord tones and read back.
    for (const auto* ninth : { "", "9", "b9", "#9", "b9#9" })
    {
        for (const auto* eleventh : { "", "#11" })
        {
            for (const auto* thirteenth : { "", "13", "b13" })
            {
                const auto symbol = std::string ("C7") + ninth + eleventh + thirteenth;
                const auto chord = ChordSymbol::parse (symbol);

                CHECK (chord.has_value());

                if (! chord.has_value())
                    continue;

                std::vector<int> notes;

                for (const auto& tone : chord->chordTones())
                    notes.push_back (48 + toPitchClass (static_cast<int> (chord->root()) + tone.semitones));

                const auto readings = readingsOf (notes);

                CHECK (! readings.empty());

                // Named on C, whatever spelling of the alterations wins.
                const auto namedOnC = std::any_of (readings.begin(), readings.end(),
                                                   [] (const std::string& name)
                                                   { return name.rfind ("C", 0) == 0; });

                if (! namedOnC)
                    CHECK_EQ (symbol + " was named", readings.front());

                CHECK (namedOnC);
            }
        }
    }
}

TEST ("no reading is offered twice")
{
    // Several suffixes spell the same chord - "7#11 13" and "79#11 13" are both
    // a 13#11 - and the same name twice reads as two answers to one question.
    const std::vector<std::vector<int>> voicings {
        { 48, 52, 55, 58, 62 }, { 38, 42, 48, 53, 58, 63 }, { 60, 64, 71 },
        { 52, 58, 62, 69 }, { 48, 51, 54, 58 }
    };

    for (const auto& notes : voicings)
    {
        auto names = readingsOf (notes);
        const auto before = names.size();

        std::sort (names.begin(), names.end());
        names.erase (std::unique (names.begin(), names.end()), names.end());

        CHECK_EQ (names.size(), before);
    }
}

TEST ("a rootless thirteenth still beats a complete name nobody writes")
{
    // E Bb D A is a rootless C13. Widening the vocabulary once put an Asus4b9
    // above it, because a name that accounts for every note outranks one that
    // leaves the root to the bass player - which is why the vocabulary holds
    // chords players write rather than everything the parser accepts.
    const auto readings = readingsOf ({ 52, 58, 62, 69 });

    CHECK (offers (readings, "C13/E"));
    CHECK (! offers (readings, "Asus4b9/E"));
}
