#include "jazz/core/Scale.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace jazz::core
{

std::string Scale::name (Accidental accidental) const
{
    if (definition == nullptr)
        return {};

    return pitchClassName (tonic, accidental) + " " + definition->name;
}

std::uint16_t Scale::pitchClassMask() const
{
    if (definition == nullptr)
        return 0;

    std::uint16_t mask = 0;

    for (auto interval : definition->intervals)
        mask |= static_cast<std::uint16_t> (1u << toPitchClass (tonic + interval));

    return mask;
}

std::vector<PitchClass> Scale::pitchClasses() const
{
    std::vector<PitchClass> pitches;

    if (definition == nullptr)
        return pitches;

    for (auto interval : definition->intervals)
        pitches.push_back (toPitchClass (tonic + interval));

    return pitches;
}

namespace
{
    /** Pitch class of each natural letter, C through B. */
    constexpr int naturalPitchClasses[] { 0, 2, 4, 5, 7, 9, 11 };
    constexpr char letterNames[] { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };

    std::string accidentalSuffix (int offset)
    {
        switch (offset)
        {
            case -2: return "bb";
            case -1: return "b";
            case 0:  return "";
            case 1:  return "#";
            case 2:  return "x";
            default: return "?";
        }
    }

    /** Spells a scale starting from a given letter, or nullopt if that letter
        would need an accidental beyond a double sharp or double flat.
    */
    std::optional<std::vector<std::string>> spellFromLetter (const std::vector<PitchClass>& pitches,
                                                             int tonicLetter,
                                                             int& accidentalCount)
    {
        std::vector<std::string> names;
        accidentalCount = 0;

        for (std::size_t degree = 0; degree < pitches.size(); ++degree)
        {
            const auto letter = (tonicLetter + static_cast<int> (degree)) % 7;

            // Signed distance from the letter's natural pitch, in -6..5.
            const auto offset = ((pitches[degree] - naturalPitchClasses[letter] + 18) % 12) - 6;

            if (offset < -2 || offset > 2)
                return std::nullopt;

            accidentalCount += std::abs (offset);
            names.push_back (std::string (1, letterNames[letter]) + accidentalSuffix (offset));
        }

        return names;
    }
}

std::vector<std::string> Scale::noteNames() const
{
    const auto pitches = pitchClasses();

    if (definition == nullptr)
        return {};

    if (pitches.size() == 7)
    {
        // Try every letter that could name the tonic and keep the spelling with
        // the fewest accidentals - that is the one a musician would write.
        std::optional<std::vector<std::string>> best;
        auto bestCount = std::numeric_limits<int>::max();

        for (auto letter = 0; letter < 7; ++letter)
        {
            const auto tonicOffset = ((tonic - naturalPitchClasses[letter] + 18) % 12) - 6;

            if (tonicOffset < -1 || tonicOffset > 1)
                continue;  // no one spells a tonic with a double accidental

            auto count = 0;

            if (auto spelling = spellFromLetter (pitches, letter, count); spelling.has_value() && count < bestCount)
            {
                bestCount = count;
                best = std::move (spelling);
            }
        }

        if (best.has_value())
            return *best;
    }

    // Symmetric, pentatonic and bebop scales do not fit seven letters: name
    // them with whichever accidental suits the tonic.
    const auto accidental = (tonic == 6 || tonic == 1 || tonic == 3 || tonic == 8 || tonic == 10)
                                ? Accidental::flats
                                : Accidental::sharps;

    std::vector<std::string> names;

    for (auto pitchClass : pitches)
        names.push_back (pitchClassName (pitchClass, accidental));

    return names;
}

bool Scale::contains (PitchClass pitchClass) const
{
    return (pitchClassMask() & (1u << toPitchClass (pitchClass))) != 0;
}

const std::vector<ScaleDefinition>& scaleCatalogue()
{
    static const std::vector<ScaleDefinition> catalogue
    {
        // Modes of the major scale.
        { "Ionian",             ScaleFamily::major, { 0, 2, 4, 5, 7, 9, 11 } },
        { "Dorian",             ScaleFamily::major, { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian",           ScaleFamily::major, { 0, 1, 3, 5, 7, 8, 10 } },
        { "Lydian",             ScaleFamily::major, { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian",         ScaleFamily::major, { 0, 2, 4, 5, 7, 9, 10 } },
        { "Aeolian",            ScaleFamily::major, { 0, 2, 3, 5, 7, 8, 10 } },
        { "Locrian",            ScaleFamily::major, { 0, 1, 3, 5, 6, 8, 10 } },

        // Modes of melodic minor.
        { "Melodic Minor",      ScaleFamily::melodicMinor, { 0, 2, 3, 5, 7, 9, 11 } },
        { "Dorian b2",          ScaleFamily::melodicMinor, { 0, 1, 3, 5, 7, 9, 10 } },
        { "Lydian Augmented",   ScaleFamily::melodicMinor, { 0, 2, 4, 6, 8, 9, 11 } },
        { "Lydian Dominant",    ScaleFamily::melodicMinor, { 0, 2, 4, 6, 7, 9, 10 } },
        { "Mixolydian b6",      ScaleFamily::melodicMinor, { 0, 2, 4, 5, 7, 8, 10 } },
        { "Locrian natural 2",  ScaleFamily::melodicMinor, { 0, 2, 3, 5, 6, 8, 10 } },
        { "Altered",            ScaleFamily::melodicMinor, { 0, 1, 3, 4, 6, 8, 10 } },

        // Modes of harmonic minor.
        { "Harmonic Minor",     ScaleFamily::harmonicMinor, { 0, 2, 3, 5, 7, 8, 11 } },
        { "Locrian natural 6",  ScaleFamily::harmonicMinor, { 0, 1, 3, 5, 6, 9, 10 } },
        { "Ionian #5",          ScaleFamily::harmonicMinor, { 0, 2, 4, 5, 8, 9, 11 } },
        { "Dorian #4",          ScaleFamily::harmonicMinor, { 0, 2, 3, 6, 7, 9, 10 } },
        { "Phrygian Dominant",  ScaleFamily::harmonicMinor, { 0, 1, 4, 5, 7, 8, 10 } },
        { "Lydian #2",          ScaleFamily::harmonicMinor, { 0, 3, 4, 6, 7, 9, 11 } },

        // Symmetric scales.
        { "Whole Tone",         ScaleFamily::symmetric, { 0, 2, 4, 6, 8, 10 } },
        { "Diminished (Whole-Half)", ScaleFamily::symmetric, { 0, 2, 3, 5, 6, 8, 9, 11 } },
        { "Diminished (Half-Whole)", ScaleFamily::symmetric, { 0, 1, 3, 4, 6, 7, 9, 10 } },

        // Pentatonics and blues.
        { "Major Pentatonic",   ScaleFamily::pentatonic, { 0, 2, 4, 7, 9 } },
        { "Minor Pentatonic",   ScaleFamily::pentatonic, { 0, 3, 5, 7, 10 } },
        { "Blues Scale",        ScaleFamily::pentatonic, { 0, 3, 5, 6, 7, 10 } },

        // Bebop scales: eight-note scales with a chromatic passing tone.
        { "Bebop Dominant",     ScaleFamily::bebop, { 0, 2, 4, 5, 7, 9, 10, 11 } },
        { "Bebop Major",        ScaleFamily::bebop, { 0, 2, 4, 5, 7, 8, 9, 11 } },
        { "Bebop Dorian",       ScaleFamily::bebop, { 0, 2, 3, 4, 5, 7, 9, 10 } },
    };

    return catalogue;
}

//==============================================================================
bool ScaleStyle::includes (ScaleFamily family) const
{
    // No families named means the whole catalogue, which is what "Everything"
    // is - rather than a list that has to be kept in step with the catalogue.
    return families.empty()
        || std::find (families.begin(), families.end(), family) != families.end();
}

const std::vector<ScaleStyle>& scaleStyles()
{
    /* Ordered the way a player meets them rather than the way the catalogue is
       laid out: the seven modes first, because that is where everyone starts
       and it is the one vocabulary that covers a whole tune on its own.

       The summaries name scales, not categories. "Melodic minor" means nothing
       to someone who has not met it; "Lydian dominant and altered" is the same
       thing said in sounds they have heard. */
    static const std::vector<ScaleStyle> styles = {
        { "modes", "The modes",
          "Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian - one parent "
          "scale, seven ways in. Where most players start.",
          { ScaleFamily::major } },

        { "melodicminor", "Melodic minor",
          "Lydian dominant, altered and the rest of that family - the sound of a dominant "
          "chord going somewhere.",
          { ScaleFamily::melodicMinor } },

        { "harmonicminor", "Harmonic minor",
          "Phrygian dominant and its relatives: the minor-key sound, and the flavour a "
          "V7 takes resolving to one.",
          { ScaleFamily::harmonicMinor } },

        { "bebop", "Bebop",
          "The eight-note scales, with the passing tone that puts chord tones back on the "
          "beat. Built for running a line, not for holding one.",
          { ScaleFamily::bebop } },

        { "pentatonic", "Pentatonics and blues",
          "Five notes and the blues scale. Nothing to avoid, which is why they carry so "
          "far from the chord they started on.",
          { ScaleFamily::pentatonic } },

        { "symmetric", "Whole tone and diminished",
          "Scales with no home note: they repeat, so a shape learned once transposes all "
          "over the keyboard.",
          { ScaleFamily::symmetric } },

        { "everything", "Everything",
          "Every scale the engine knows, all 29 of them, best fit first.",
          {} }
    };

    return styles;
}

const ScaleStyle* findScaleStyle (std::string_view key)
{
    const auto& styles = scaleStyles();
    const auto found = std::find_if (styles.begin(), styles.end(),
                                     [key] (const ScaleStyle& style) { return style.key == key; });

    return found == styles.end() ? nullptr : &*found;
}

const ScaleDefinition* findScaleDefinition (std::string_view name)
{
    const auto& catalogue = scaleCatalogue();
    const auto found = std::find_if (catalogue.begin(), catalogue.end(),
                                     [name] (const ScaleDefinition& d) { return d.name == name; });

    return found == catalogue.end() ? nullptr : &*found;
}

std::vector<Scale> modesOf (const Scale& scale)
{
    std::vector<Scale> modes;

    if (scale.definition == nullptr)
        return modes;

    const auto mask = scale.pitchClassMask();

    for (const auto& definition : scaleCatalogue())
    {
        if (definition.family != scale.definition->family)
            continue;

        for (PitchClass tonic = 0; tonic < semitonesPerOctave; ++tonic)
        {
            const Scale candidate { &definition, tonic };

            // Same set of pitches, different tonic: that is a mode of this scale.
            if (candidate.pitchClassMask() == mask
                && ! (definition.name == scale.definition->name && tonic == scale.tonic))
                modes.push_back (candidate);
        }
    }

    return modes;
}

} // namespace jazz::core
