#include "jazz/core/Pitch.h"

#include <array>
#include <cctype>

namespace jazz::core
{

namespace
{
    constexpr std::array<const char*, 12> sharpNames { "C", "C#", "D", "D#", "E", "F",
                                                       "F#", "G", "G#", "A", "A#", "B" };
    constexpr std::array<const char*, 12> flatNames { "C", "Db", "D", "Eb", "E", "F",
                                                      "Gb", "G", "Ab", "A", "Bb", "B" };

    /** Semitone offset of each natural note name above C. */
    std::optional<int> naturalOffset (char letter)
    {
        switch (std::toupper (static_cast<unsigned char> (letter)))
        {
            case 'C': return 0;
            case 'D': return 2;
            case 'E': return 4;
            case 'F': return 5;
            case 'G': return 7;
            case 'A': return 9;
            case 'B': return 11;
            default:  return std::nullopt;
        }
    }
}

int toPitchClass (int value) noexcept
{
    const auto folded = value % semitonesPerOctave;
    return folded < 0 ? folded + semitonesPerOctave : folded;
}

int ascendingInterval (PitchClass from, PitchClass to) noexcept
{
    return toPitchClass (to - from);
}

int octaveOf (int midiNote) noexcept
{
    // Floor division so notes below MIDI 0 still report a sensible octave.
    return (midiNote - (toPitchClass (midiNote))) / semitonesPerOctave - 1;
}

std::string pitchClassName (PitchClass pitchClass, Accidental accidental)
{
    const auto index = static_cast<std::size_t> (toPitchClass (pitchClass));
    return accidental == Accidental::sharps ? sharpNames[index] : flatNames[index];
}

std::string midiNoteName (int midiNote, Accidental accidental)
{
    return pitchClassName (midiNote, accidental) + std::to_string (octaveOf (midiNote));
}

std::optional<ParsedRoot> parseRoot (std::string_view text)
{
    if (text.empty())
        return std::nullopt;

    const auto natural = naturalOffset (text[0]);

    if (! natural.has_value())
        return std::nullopt;

    auto semitones = *natural;
    std::size_t consumed = 1;

    // Accept repeated accidentals so "Bbb" and "F##" parse.
    while (consumed < text.size())
    {
        const auto c = text[consumed];

        if (c == '#')
            ++semitones;
        else if (c == 'b' && consumed == 1)  // only a leading 'b' is an accidental: "Bb" vs "Bbm"
            --semitones;
        else if (c == 'b' && consumed > 1 && text[consumed - 1] == 'b')
            --semitones;
        else
            break;

        ++consumed;
    }

    return ParsedRoot { toPitchClass (semitones), consumed };
}

std::optional<PitchClass> parsePitchClass (std::string_view text)
{
    const auto parsed = parseRoot (text);

    if (! parsed.has_value() || parsed->charactersConsumed != text.size())
        return std::nullopt;

    return parsed->pitchClass;
}

std::string intervalLabel (int semitones, bool spellThirdAsMinor)
{
    switch (toPitchClass (semitones))
    {
        case 0:  return "R";
        case 1:  return "b9";
        case 2:  return "9";
        case 3:  return spellThirdAsMinor ? "b3" : "#9";
        case 4:  return "3";
        case 5:  return "11";
        case 6:  return "#11";
        case 7:  return "5";
        case 8:  return "b13";
        case 9:  return "13";
        case 10: return "b7";
        case 11: return "maj7";
        default: return "?";
    }
}

} // namespace jazz::core
