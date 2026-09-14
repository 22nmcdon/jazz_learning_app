#include "jazz/core/ScaleSuggester.h"

#include <algorithm>

namespace jazz::core
{

namespace
{
    struct CanonicalChoice
    {
        std::string scaleName;
        std::string rationale;
    };

    bool has (const ChordSymbol& chord, Extension extension)
    {
        const auto& extensions = chord.extensions();
        return std::find (extensions.begin(), extensions.end(), extension) != extensions.end();
    }

    /** The default scale for a chord quality, following standard chord-scale practice. */
    CanonicalChoice canonicalScaleFor (const ChordSymbol& chord)
    {
        const auto raisedFifth = has (chord, Extension::sharpFive) || has (chord, Extension::flatThirteen);
        const auto alteredNinth = has (chord, Extension::flatNine) || has (chord, Extension::sharpNine);

        switch (chord.quality())
        {
            case ChordQuality::major:
                if (has (chord, Extension::sharpEleven))
                    return { "Lydian", "The #11 is Lydian's defining note, and Lydian has no avoid notes over a major chord." };

                if (has (chord, Extension::sharpFive))
                    return { "Lydian Augmented", "Covers both the major 7th and the #5." };

                return { "Ionian", "The home major scale. Treat the 4th as a passing tone - it clashes with the 3rd." };

            case ChordQuality::minor:
                if (has (chord, Extension::flatNine))
                    return { "Phrygian", "The b9 points to Phrygian rather than Dorian." };

                if (has (chord, Extension::flatThirteen))
                    return { "Aeolian", "The b13 (b6) is what separates Aeolian from Dorian." };

                return { "Dorian", "The default minor-7th sound: natural 13th, no avoid notes." };

            case ChordQuality::minorMajor:
                return { "Melodic Minor", "A minor triad with a major 7th is melodic minor by definition." };

            case ChordQuality::dominant:
                if (raisedFifth && alteredNinth)
                    return { "Altered", "Every tension is altered (b9 #9 #11 b13) - the altered scale supplies all of them." };

                if (has (chord, Extension::flatNine) && has (chord, Extension::flatThirteen))
                    return { "Phrygian Dominant", "b9 with b13 over a major 3rd is the 5th mode of harmonic minor." };

                if (has (chord, Extension::sharpNine) && ! has (chord, Extension::flatNine))
                    return { "Altered", "The #9 comes from the altered scale; it also gives you the b9, #11 and b13." };

                if (has (chord, Extension::flatNine))
                    return { "Diminished (Half-Whole)", "Gives the b9 and #9 while keeping the natural 13th." };

                if (has (chord, Extension::sharpEleven))
                    return { "Lydian Dominant", "b7 with a #11 - the 4th mode of melodic minor." };

                if (raisedFifth)
                    return { "Whole Tone", "A dominant with a #5 and no perfect 5th is whole-tone territory." };

                return { "Mixolydian", "The unaltered dominant sound. The natural 11th is an avoid note over the 3rd." };

            case ChordQuality::halfDiminished:
                if (has (chord, Extension::flatNine))
                    return { "Locrian", "With a b9 spelled in the chord, plain Locrian is the match." };

                return { "Locrian natural 2", "Locrian with a natural 9th - avoids the b9 clash against the root." };

            case ChordQuality::diminished:
                return { "Diminished (Whole-Half)", "The symmetric scale built from the chord itself." };

            case ChordQuality::augmented:
                if (chord.seventh() == SeventhType::major)
                    return { "Lydian Augmented", "Major 7th with a #5." };

                return { "Whole Tone", "An augmented triad with a b7 sits inside the whole-tone scale." };

            case ChordQuality::suspended:
                if (has (chord, Extension::flatNine))
                    return { "Phrygian", "The sus b9 sound (often written as a slash chord)." };

                return { "Mixolydian", "The 4th is a chord tone here, so nothing needs to be avoided." };
        }

        return { "Ionian", {} };
    }

    /** Tensions the scale adds on top of the chord, for the rationale text. */
    std::vector<std::string> tensionsAdded (const Scale& scale, const ChordSymbol& chord)
    {
        std::vector<std::string> labels;

        for (auto pitchClass : scale.pitchClasses())
        {
            if (chord.containsPitchClass (pitchClass))
                continue;

            labels.push_back (intervalLabel (ascendingInterval (chord.root(), pitchClass),
                                             chord.hasMinorThird()));
        }

        return labels;
    }

    std::string join (const std::vector<std::string>& items, const std::string& separator)
    {
        std::string result;

        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0)
                result += separator;

            result += items[i];
        }

        return result;
    }
}

std::vector<PitchClass> ScaleSuggester::avoidNotesFor (const Scale& scale, const ChordSymbol& chord)
{
    std::vector<PitchClass> avoid;

    const auto dominantFunction = chord.quality() == ChordQuality::dominant
                                  || chord.quality() == ChordQuality::suspended;

    std::vector<PitchClass> essential;

    for (const auto& tone : chord.essentialTones())
        essential.push_back (toPitchClass (chord.root() + tone.semitones));

    for (auto pitchClass : scale.pitchClasses())
    {
        if (chord.containsPitchClass (pitchClass))
            continue;

        // A b9 over a dominant root is a colour, not a clash - that is the whole
        // point of the altered and half-whole diminished scales.
        if (dominantFunction && pitchClass == toPitchClass (chord.root() + 1))
            continue;

        const auto semitoneBelow = toPitchClass (pitchClass - 1);

        if (std::find (essential.begin(), essential.end(), semitoneBelow) != essential.end())
            avoid.push_back (pitchClass);
    }

    return avoid;
}

std::vector<ScaleSuggestion> ScaleSuggester::suggestionsFor (const ChordSymbol& chord) const
{
    const auto canonical = canonicalScaleFor (chord);

    std::vector<PitchClass> essential;

    for (const auto& tone : chord.essentialTones())
        essential.push_back (toPitchClass (chord.root() + tone.semitones));

    std::vector<ScaleSuggestion> suggestions;

    for (const auto& definition : scaleCatalogue())
    {
        const auto pentatonicFamily = definition.family == ScaleFamily::pentatonic;

        for (PitchClass tonic = 0; tonic < semitonesPerOctave; ++tonic)
        {
            // Seven- and eight-note scales are only offered rooted on the chord:
            // the same pitches rooted elsewhere are just another name for the
            // same mode, which would flood the alternatives list.
            if (! pentatonicFamily && tonic != chord.root())
                continue;

            if (pentatonicFamily && tonic != chord.root() && ! options.includeNonRootedPentatonics)
                continue;

            const Scale scale { &definition, tonic };
            const auto mask = scale.pitchClassMask();

            const auto containsEveryEssentialTone =
                std::all_of (essential.begin(), essential.end(),
                             [mask] (PitchClass pc) { return (mask & (1u << pc)) != 0; });

            if (! containsEveryEssentialTone)
                continue;

            ScaleSuggestion suggestion;
            suggestion.scale = scale;
            suggestion.avoidNotes = avoidNotesFor (scale, chord);

            const auto isCanonical = definition.name == canonical.scaleName && tonic == chord.root();

            suggestion.score = 100;
            suggestion.score += isCanonical ? 60 : 0;
            suggestion.score += tonic == chord.root() ? 40 : 0;
            suggestion.score -= 12 * static_cast<int> (suggestion.avoidNotes.size());

            const auto tensions = tensionsAdded (scale, chord);
            suggestion.score += 3 * static_cast<int> (tensions.size());

            if (isCanonical)
            {
                suggestion.rationale = canonical.rationale;
            }
            else
            {
                suggestion.rationale = "Contains every chord tone";

                if (! tensions.empty())
                    suggestion.rationale += "; adds " + join (tensions, " ");

                if (! suggestion.avoidNotes.empty())
                {
                    std::vector<std::string> names;

                    for (auto pitchClass : suggestion.avoidNotes)
                        names.push_back (pitchClassName (pitchClass));

                    suggestion.rationale += ". Handle " + join (names, ", ") + " as a passing tone";
                }

                suggestion.rationale += ".";
            }

            suggestions.push_back (std::move (suggestion));
        }
    }

    std::stable_sort (suggestions.begin(), suggestions.end(),
                      [] (const ScaleSuggestion& a, const ScaleSuggestion& b) { return a.score > b.score; });

    if (options.maxSuggestions > 0 && suggestions.size() > static_cast<std::size_t> (options.maxSuggestions))
        suggestions.resize (static_cast<std::size_t> (options.maxSuggestions));

    if (! suggestions.empty())
        suggestions.front().isPrimary = true;

    return suggestions;
}

ScaleSuggestion ScaleSuggester::primarySuggestionFor (const ChordSymbol& chord) const
{
    const auto suggestions = suggestionsFor (chord);
    return suggestions.empty() ? ScaleSuggestion {} : suggestions.front();
}

} // namespace jazz::core
