#include "TestFramework.h"
#include "jazz/core/ScaleSuggester.h"

using namespace jazz::core;

namespace
{
    ChordSymbol chordFrom (const std::string& text)
    {
        const auto chord = ChordSymbol::parse (text);
        CHECK (chord.has_value());
        return *chord;
    }

    std::string primaryScaleFor (const std::string& chordText)
    {
        const ScaleSuggester suggester;
        return suggester.primarySuggestionFor (chordFrom (chordText)).scale.name();
    }

    bool offers (const std::vector<ScaleSuggestion>& suggestions, const std::string& scaleName)
    {
        for (const auto& suggestion : suggestions)
            if (suggestion.scale.name() == scaleName)
                return true;

        return false;
    }
}

TEST ("picks the standard chord-scale for each quality")
{
    CHECK_EQ (primaryScaleFor ("Cmaj7"), std::string ("C Ionian"));
    CHECK_EQ (primaryScaleFor ("Cmaj7#11"), std::string ("C Lydian"));
    CHECK_EQ (primaryScaleFor ("Dm7"), std::string ("D Dorian"));
    CHECK_EQ (primaryScaleFor ("G7"), std::string ("G Mixolydian"));
    CHECK_EQ (primaryScaleFor ("G7alt"), std::string ("G Altered"));
    CHECK_EQ (primaryScaleFor ("G7#11"), std::string ("G Lydian Dominant"));
    CHECK_EQ (primaryScaleFor ("Bm7b5"), std::string ("B Locrian natural 2"));
    CHECK_EQ (primaryScaleFor ("Cdim7"), std::string ("C Diminished (Whole-Half)"));
    CHECK_EQ (primaryScaleFor ("CmMaj7"), std::string ("C Melodic Minor"));
}

TEST ("every suggested scale contains every essential chord tone")
{
    const ScaleSuggester suggester;

    for (const auto& text : { "Cmaj7", "Dm7", "G7b9", "F#m7b5", "Ab13#11", "Cm6" })
    {
        const auto chord = chordFrom (text);

        for (const auto& suggestion : suggester.suggestionsFor (chord))
            for (const auto& tone : chord.essentialTones())
                CHECK (suggestion.scale.contains (chord.root() + tone.semitones));
    }
}

TEST ("flags the fourth as an avoid note over a major seventh chord")
{
    const ScaleSuggester suggester;
    const auto suggestions = suggester.suggestionsFor (chordFrom ("Cmaj7"));

    for (const auto& suggestion : suggestions)
    {
        if (suggestion.scale.name() != "C Ionian")
            continue;

        CHECK_EQ (suggestion.avoidNotes.size(), std::size_t (1));
        CHECK_EQ (suggestion.avoidNotes.front(), 5);  // F
    }
}

TEST ("lydian has no avoid notes, which is why it outranks ionian for maj7#11")
{
    const ScaleSuggester suggester;

    for (const auto& suggestion : suggester.suggestionsFor (chordFrom ("Cmaj7")))
        if (suggestion.scale.name() == "C Lydian")
            CHECK (suggestion.avoidNotes.empty());
}

TEST ("dorian has no avoid notes over a minor seventh chord")
{
    const ScaleSuggester suggester;

    for (const auto& suggestion : suggester.suggestionsFor (chordFrom ("Cm7")))
        if (suggestion.scale.name() == "C Dorian")
            CHECK (suggestion.avoidNotes.empty());
}

TEST ("a b9 over a dominant is colour, not an avoid note")
{
    const ScaleSuggester suggester;
    const auto suggestions = suggester.suggestionsFor (chordFrom ("G7"));

    for (const auto& suggestion : suggestions)
        if (suggestion.scale.name() == "G Diminished (Half-Whole)")
            CHECK (suggestion.avoidNotes.empty());
}

TEST ("offers alternatives beyond the primary suggestion")
{
    const ScaleSuggester suggester;
    const auto suggestions = suggester.suggestionsFor (chordFrom ("G7"));

    CHECK (suggestions.size() > 3);
    CHECK (suggestions.front().isPrimary);
    CHECK (offers (suggestions, "G Lydian Dominant"));
    CHECK (offers (suggestions, "G Bebop Dominant"));
}

TEST ("pentatonics rooted away from the chord can be offered or suppressed")
{
    const ScaleSuggester withPentatonics { ScaleSuggester::Options { true, 40 } };
    const ScaleSuggester rootedOnly { ScaleSuggester::Options { false, 40 } };

    const auto chord = chordFrom ("Cm7");

    CHECK (offers (withPentatonics.suggestionsFor (chord), "Eb Major Pentatonic"));
    CHECK (! offers (rootedOnly.suggestionsFor (chord), "Eb Major Pentatonic"));
}

TEST ("suggestions are ordered best first")
{
    const ScaleSuggester suggester;
    const auto suggestions = suggester.suggestionsFor (chordFrom ("Dm7"));

    for (std::size_t i = 1; i < suggestions.size(); ++i)
        CHECK (suggestions[i - 1].score >= suggestions[i].score);
}

TEST ("modes of a scale share its pitches")
{
    const Scale dMinor { findScaleDefinition ("Dorian"), 2 };
    const auto modes = modesOf (dMinor);

    CHECK (! modes.empty());

    for (const auto& mode : modes)
        CHECK_EQ (mode.pitchClassMask(), dMinor.pitchClassMask());
}

TEST ("seven-note scales are spelled with one letter per degree")
{
    const auto spelling = [] (const char* name, PitchClass tonic)
    {
        std::string text;

        for (const auto& note : Scale { findScaleDefinition (name), tonic }.noteNames())
            text += note + " ";

        if (! text.empty())
            text.pop_back();

        return text;
    };

    CHECK_EQ (spelling ("Ionian", 0),          std::string ("C D E F G A B"));
    CHECK_EQ (spelling ("Lydian Dominant", 7), std::string ("G A B C# D E F"));
    CHECK_EQ (spelling ("Dorian", 3),          std::string ("Eb F Gb Ab Bb C Db"));
    // Spelled from the sharp side: one accidental beats the six that spelling
    // the same scale from Gb would need.
    CHECK_EQ (spelling ("Altered", 6),         std::string ("F# G A Bb C D E"));
    CHECK_EQ (spelling ("Harmonic Minor", 9),  std::string ("A B C D E F G#"));
}

TEST ("scales that do not fit seven letters fall back to plain names")
{
    std::string text;

    for (const auto& note : Scale { findScaleDefinition ("Whole Tone"), 0 }.noteNames())
        text += note + " ";

    CHECK_EQ (text, std::string ("C D E F# G# A# "));
}
