#include "TestFramework.h"
#include "jazz/core/ChordSymbol.h"

using namespace jazz::core;

namespace
{
    ChordSymbol parsed (const std::string& text)
    {
        const auto chord = ChordSymbol::parse (text);
        CHECK (chord.has_value());
        return *chord;
    }

    bool hasTone (const ChordSymbol& chord, int semitones)
    {
        for (const auto& tone : chord.chordTones())
            if (tone.semitones == semitones)
                return true;

        return false;
    }

    bool hasEssentialTone (const ChordSymbol& chord, int semitones)
    {
        for (const auto& tone : chord.essentialTones())
            if (tone.semitones == semitones)
                return true;

        return false;
    }
}

TEST ("parses a plain major triad")
{
    const auto chord = parsed ("C");
    CHECK_EQ (chord.root(), 0);
    CHECK (chord.quality() == ChordQuality::major);
    CHECK (chord.seventh() == SeventhType::none);
}

TEST ("parses roots with accidentals")
{
    CHECK_EQ (parsed ("Bb7").root(), 10);
    CHECK_EQ (parsed ("F#m7").root(), 6);
    CHECK_EQ (parsed ("Eb").root(), 3);
}

TEST ("a plain seventh over a major triad is a dominant chord")
{
    const auto chord = parsed ("G7");
    CHECK (chord.quality() == ChordQuality::dominant);
    CHECK (chord.seventh() == SeventhType::minor);
    CHECK (hasTone (chord, 4));   // major 3rd
    CHECK (hasTone (chord, 10));  // b7
}

TEST ("distinguishes major sevenths from dominants")
{
    for (const auto& text : { "Cmaj7", "CMaj7", "CM7", "Cma7", "C^7" })
    {
        const auto chord = parsed (text);
        CHECK (chord.quality() == ChordQuality::major);
        CHECK (chord.seventh() == SeventhType::major);
    }
}

TEST ("parses minor seventh spellings")
{
    for (const auto& text : { "Dm7", "Dmin7", "Dmi7", "D-7" })
    {
        const auto chord = parsed (text);
        CHECK_EQ (chord.root(), 2);
        CHECK (chord.quality() == ChordQuality::minor);
        CHECK (chord.seventh() == SeventhType::minor);
        CHECK (hasTone (chord, 3));
    }
}

TEST ("parses half-diminished and diminished chords")
{
    const auto halfDim = parsed ("Bm7b5");
    CHECK (halfDim.quality() == ChordQuality::halfDiminished);
    CHECK (hasEssentialTone (halfDim, 6));   // b5 defines the quality
    CHECK (hasEssentialTone (halfDim, 10));

    const auto dim = parsed ("Bdim7");
    CHECK (dim.quality() == ChordQuality::diminished);
    CHECK (dim.seventh() == SeventhType::diminished);
    CHECK (hasTone (dim, 9));
}

TEST ("a minor-major seventh is not read as a minor seventh")
{
    const auto chord = parsed ("CmMaj7");
    CHECK (chord.quality() == ChordQuality::minorMajor);
    CHECK (chord.seventh() == SeventhType::major);
    CHECK (hasTone (chord, 3));
    CHECK (hasTone (chord, 11));
}

TEST ("alt spells out every altered tension")
{
    const auto chord = parsed ("C7alt");
    CHECK (chord.quality() == ChordQuality::dominant);
    CHECK (hasEssentialTone (chord, 1));   // b9
    CHECK (hasEssentialTone (chord, 3));   // #9
    CHECK (hasEssentialTone (chord, 6));   // #11
    CHECK (hasEssentialTone (chord, 8));   // b13
}

TEST ("extensions imply the seventh underneath them")
{
    const auto thirteenth = parsed ("Bb13");
    CHECK (thirteenth.seventh() == SeventhType::minor);
    CHECK (hasTone (thirteenth, 9));

    const auto majorNinth = parsed ("Ebmaj9");
    CHECK (majorNinth.seventh() == SeventhType::major);
    CHECK (hasTone (majorNinth, 2));
}

TEST ("a sixth chord keeps its sixth as an essential tone")
{
    const auto sixth = parsed ("C6");
    CHECK (sixth.seventh() == SeventhType::none);
    CHECK (hasEssentialTone (sixth, 9));

    // Alongside a seventh the same pitch is colour, not identity.
    const auto thirteenth = parsed ("C13");
    CHECK (! hasEssentialTone (thirteenth, 9));
}

TEST ("suspended chords replace the third")
{
    const auto sus4 = parsed ("G7sus4");
    CHECK (sus4.quality() == ChordQuality::suspended);
    CHECK (hasEssentialTone (sus4, 5));
    CHECK (! hasTone (sus4, 4));

    const auto sus2 = parsed ("Gsus2");
    CHECK (hasEssentialTone (sus2, 2));
}

TEST ("reads a slash bass but not a 6/9 chord as one")
{
    const auto slash = parsed ("Am7/D");
    CHECK (slash.bass().has_value());
    CHECK_EQ (*slash.bass(), 2);

    const auto sixNine = parsed ("C6/9");
    CHECK (! sixNine.bass().has_value());
    CHECK (hasTone (sixNine, 9));
    CHECK (hasTone (sixNine, 2));
}

TEST ("rejects text that is not a chord symbol")
{
    CHECK (! ChordSymbol::parse ("").has_value());
    CHECK (! ChordSymbol::parse ("H7").has_value());
    CHECK (! ChordSymbol::parse ("Cmaj7zz").has_value());
    CHECK (! ChordSymbol::parse ("hello").has_value());
}

TEST ("guide tones are the third and the seventh")
{
    const auto guides = parsed ("Dm7").guideTones();
    CHECK_EQ (guides.size(), std::size_t (2));
    CHECK_EQ (guides[0].semitones, 3);
    CHECK_EQ (guides[1].semitones, 10);
}

TEST ("round-trips symbols through toString")
{
    CHECK_EQ (parsed ("Cmaj7").toString(), std::string ("Cmaj7"));
    CHECK_EQ (parsed ("Dm7").toString(), std::string ("Dm7"));
    CHECK_EQ (parsed ("G7").toString(), std::string ("G7"));
    CHECK_EQ (parsed ("Bb13").toString(), std::string ("Bb13"));
    CHECK_EQ (parsed ("Bm7b5").toString(), std::string ("Bm7b5"));
    CHECK_EQ (parsed ("F#dim7").toString(), std::string ("Gbdim7"));
    CHECK_EQ (parsed ("G7sus4").toString(), std::string ("G7sus4"));
    CHECK_EQ (parsed ("Am7/D").toString(), std::string ("Am7/D"));

    // A fully altered dominant is written "alt" rather than spelled out.
    CHECK_EQ (parsed ("D7alt").toString(), std::string ("D7alt"));
}

TEST ("transposition moves root and bass together")
{
    const auto chord = parsed ("Am7/D").transposed (2);
    CHECK_EQ (chord.root(), 11);
    CHECK_EQ (*chord.bass(), 4);
}

TEST ("an eleventh implies the ninth beneath it")
{
    const auto eleventh = parsed ("Dm11");

    CHECK (hasTone (eleventh, 2));    // the 9th
    CHECK (hasTone (eleventh, 5));    // the 11th
    CHECK (! hasEssentialTone (eleventh, 2));  // colour, not identity

    // A chord built with the 11th and one written as m11 must hold the same notes.
    const auto built = ChordSymbol::build (2, ChordQuality::minor, SeventhType::minor,
                                           { Extension::eleven });
    CHECK (built == eleventh);
    CHECK_EQ (built.pitchClassMask(), eleventh.pitchClassMask());
}
