#include "jazz/core/ChordSymbol.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace jazz::core
{

int semitonesAboveRoot (Extension extension) noexcept
{
    switch (extension)
    {
        case Extension::flatNine:     return 1;
        case Extension::nine:         return 2;
        case Extension::sharpNine:    return 3;
        case Extension::eleven:       return 5;
        case Extension::sharpEleven:  return 6;
        case Extension::flatThirteen: return 8;
        case Extension::thirteen:     return 9;
        case Extension::six:          return 9;
        case Extension::flatFive:     return 6;
        case Extension::sharpFive:    return 8;
    }

    return 0;
}

std::string extensionLabel (Extension extension)
{
    switch (extension)
    {
        case Extension::flatNine:     return "b9";
        case Extension::nine:         return "9";
        case Extension::sharpNine:    return "#9";
        case Extension::eleven:       return "11";
        case Extension::sharpEleven:  return "#11";
        case Extension::flatThirteen: return "b13";
        case Extension::thirteen:     return "13";
        case Extension::six:          return "6";
        case Extension::flatFive:     return "b5";
        case Extension::sharpFive:    return "#5";
    }

    return {};
}

namespace
{
    /** Alterations change the chord's identity; natural colour tones do not. */
    bool isAlteration (Extension extension)
    {
        switch (extension)
        {
            case Extension::flatNine:
            case Extension::sharpNine:
            case Extension::sharpEleven:
            case Extension::flatThirteen:
            case Extension::flatFive:
            case Extension::sharpFive:
                return true;
            default:
                return false;
        }
    }

    /** Ranks extensions for printing: alterations follow the natural tones. */
    int printOrder (Extension extension)
    {
        switch (extension)
        {
            case Extension::flatFive:     return 0;
            case Extension::sharpFive:    return 1;
            case Extension::flatNine:     return 2;
            case Extension::sharpNine:    return 3;
            case Extension::sharpEleven:  return 4;
            case Extension::flatThirteen: return 5;
            default:                      return 6;
        }
    }

    /** Cursor over the part of a symbol that follows the root. */
    struct Scanner
    {
        std::string_view text;
        std::size_t pos {};

        bool atEnd() const { return pos >= text.size(); }

        bool take (std::string_view token)
        {
            if (text.substr (pos, token.size()) != token)
                return false;

            pos += token.size();
            return true;
        }

        bool takeAny (std::initializer_list<std::string_view> tokens)
        {
            return std::any_of (tokens.begin(), tokens.end(),
                                [this] (std::string_view t) { return take (t); });
        }
    };
}

bool ChordSymbol::hasMinorThird() const noexcept
{
    switch (chordQuality)
    {
        case ChordQuality::minor:
        case ChordQuality::minorMajor:
        case ChordQuality::halfDiminished:
        case ChordQuality::diminished:
            return true;
        default:
            return false;
    }
}

void ChordSymbol::addExtension (Extension extension)
{
    if (! hasExtension (extension))
        namedExtensions.push_back (extension);
}

bool ChordSymbol::hasExtension (Extension extension) const
{
    return std::find (namedExtensions.begin(), namedExtensions.end(), extension)
           != namedExtensions.end();
}

std::optional<ChordSymbol> ChordSymbol::parse (std::string_view text)
{
    // Trim surrounding whitespace without copying the whole symbol first.
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.front())))
        text.remove_prefix (1);

    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.back())))
        text.remove_suffix (1);

    if (text.empty())
        return std::nullopt;

    ChordSymbol chord;
    chord.sourceText = std::string (text);

    auto body = text;

    // A trailing "/X" is a slash bass only when X is a note name - "6/9" is not.
    if (const auto slash = body.rfind ('/'); slash != std::string_view::npos)
    {
        if (const auto bass = parsePitchClass (body.substr (slash + 1)))
        {
            chord.bassPitchClass = *bass;
            body = body.substr (0, slash);
        }
    }

    const auto root = parseRoot (body);

    if (! root.has_value())
        return std::nullopt;

    chord.rootPitchClass = root->pitchClass;

    Scanner scanner { body.substr (root->charactersConsumed), 0 };
    bool majorSeventhIntent = false;

    // Applies a bare 7/9/11/13 to whichever quality has been seen so far.
    const auto applySeventh = [&chord, &majorSeventhIntent]
    {
        if (chord.seventhType != SeventhType::none)
            return;

        chord.seventhType = majorSeventhIntent ? SeventhType::major : SeventhType::minor;
    };

    while (! scanner.atEnd())
    {
        // Minor-major sevenths first: they start with the same letters as both
        // the minor and the major tokens.
        if (scanner.takeAny ({ "mMaj7", "mMaj9", "mmaj7", "minMaj7", "min(maj7)",
                               "m(maj7)", "-Maj7", "-maj7", "mM7" }))
        {
            chord.chordQuality = ChordQuality::minorMajor;
            chord.seventhType = SeventhType::major;
            majorSeventhIntent = true;
            continue;
        }

        if (scanner.takeAny ({ "m7b5", "m7(b5)", "min7b5", "-7b5", "h7", "h" }))
        {
            chord.chordQuality = ChordQuality::halfDiminished;
            chord.seventhType = SeventhType::minor;
            continue;
        }

        if (scanner.takeAny ({ "dim7", "o7" }))
        {
            chord.chordQuality = ChordQuality::diminished;
            chord.seventhType = SeventhType::diminished;
            continue;
        }

        if (scanner.takeAny ({ "dim", "o" }))
        {
            chord.chordQuality = ChordQuality::diminished;
            continue;
        }

        if (scanner.take ("sus2"))
        {
            chord.chordQuality = ChordQuality::suspended;
            chord.suspendedSecond = true;
            continue;
        }

        if (scanner.takeAny ({ "sus4", "sus" }))
        {
            chord.chordQuality = ChordQuality::suspended;
            continue;
        }

        if (scanner.take ("alt"))
        {
            chord.chordQuality = ChordQuality::dominant;
            chord.seventhType = SeventhType::minor;

            for (auto extension : { Extension::flatNine, Extension::sharpNine,
                                    Extension::sharpEleven, Extension::flatThirteen })
                chord.addExtension (extension);

            continue;
        }

        if (scanner.takeAny ({ "maj7", "Maj7", "ma7", "M7", "^7", "j7" }))
        {
            majorSeventhIntent = true;
            chord.seventhType = SeventhType::major;
            continue;
        }

        if (scanner.takeAny ({ "maj", "Maj", "ma", "M", "^" }))
        {
            majorSeventhIntent = true;
            continue;
        }

        if (scanner.takeAny ({ "min", "mi", "m", "-" }))
        {
            if (chord.chordQuality == ChordQuality::major)
                chord.chordQuality = ChordQuality::minor;

            continue;
        }

        if (scanner.takeAny ({ "aug", "+" }))
        {
            chord.chordQuality = ChordQuality::augmented;
            continue;
        }

        if (scanner.takeAny ({ "add9", "add2" }))  { chord.addExtension (Extension::nine); continue; }
        if (scanner.takeAny ({ "add11", "add4" })) { chord.addExtension (Extension::eleven); continue; }
        if (scanner.takeAny ({ "add13", "add6" })) { chord.addExtension (Extension::thirteen); continue; }

        if (scanner.takeAny ({ "6/9", "69" }))
        {
            chord.addExtension (Extension::six);
            chord.addExtension (Extension::nine);
            continue;
        }

        if (scanner.take ("b5"))   { chord.addExtension (Extension::flatFive); continue; }
        if (scanner.take ("#5"))   { chord.addExtension (Extension::sharpFive); continue; }
        if (scanner.take ("b9"))   { chord.addExtension (Extension::flatNine); continue; }
        if (scanner.take ("#9"))   { chord.addExtension (Extension::sharpNine); continue; }
        if (scanner.takeAny ({ "#11", "#4" })) { chord.addExtension (Extension::sharpEleven); continue; }
        if (scanner.takeAny ({ "b13", "b6" }))  { chord.addExtension (Extension::flatThirteen); continue; }

        if (scanner.take ("13"))
        {
            applySeventh();
            chord.addExtension (Extension::thirteen);
            continue;
        }

        if (scanner.take ("11"))
        {
            applySeventh();
            chord.addExtension (Extension::eleven);
            continue;
        }

        if (scanner.take ("9"))
        {
            applySeventh();
            chord.addExtension (Extension::nine);
            continue;
        }

        if (scanner.take ("7"))
        {
            if (chord.chordQuality == ChordQuality::diminished)
                chord.seventhType = SeventhType::diminished;
            else
                applySeventh();

            continue;
        }

        if (scanner.take ("6"))
        {
            chord.addExtension (Extension::six);
            continue;
        }

        // Cosmetic characters that carry no harmonic information.
        if (scanner.takeAny ({ "5", "(", ")", " ", ",", "*", "no3", "no5" }))
            continue;

        return std::nullopt;  // unrecognised text: not a chord symbol
    }

    // A plain seventh over a major triad is a dominant chord.
    if (chord.chordQuality == ChordQuality::major
        && chord.seventhType == SeventhType::minor
        && ! majorSeventhIntent)
    {
        chord.chordQuality = ChordQuality::dominant;
    }

    return chord;
}

ChordSymbol ChordSymbol::build (PitchClass root,
                                ChordQuality quality,
                                SeventhType seventh,
                                std::vector<Extension> extensions)
{
    ChordSymbol chord;
    chord.rootPitchClass = toPitchClass (root);
    chord.chordQuality = quality;
    chord.seventhType = seventh;

    for (auto extension : extensions)
        chord.addExtension (extension);

    chord.sourceText = chord.toString();
    return chord;
}

std::vector<ChordTone> ChordSymbol::chordTones() const
{
    std::vector<ChordTone> tones;
    const auto minorThird = hasMinorThird();

    const auto add = [&tones] (int semitones, ChordToneRole role, std::string label, bool essential)
    {
        const auto folded = toPitchClass (semitones);
        const auto existing = std::find_if (tones.begin(), tones.end(),
                                            [folded] (const ChordTone& t) { return t.semitones == folded; });

        if (existing != tones.end())
        {
            existing->essential = existing->essential || essential;
            return;
        }

        tones.push_back ({ folded, role, std::move (label), essential });
    };

    add (0, ChordToneRole::root, "R", true);

    if (chordQuality == ChordQuality::suspended)
        add (suspendedSecond ? 2 : 5, ChordToneRole::third, suspendedSecond ? "sus2" : "sus4", true);
    else
        add (minorThird ? 3 : 4, ChordToneRole::third, minorThird ? "b3" : "3", true);

    if (chordQuality == ChordQuality::diminished || chordQuality == ChordQuality::halfDiminished
        || hasExtension (Extension::flatFive))
        add (6, ChordToneRole::fifth, "b5", true);
    else if (chordQuality == ChordQuality::augmented || hasExtension (Extension::sharpFive))
        add (8, ChordToneRole::fifth, "#5", true);
    else
        add (7, ChordToneRole::fifth, "5", false);

    switch (seventhType)
    {
        case SeventhType::minor:      add (10, ChordToneRole::seventh, "b7", true); break;
        case SeventhType::major:      add (11, ChordToneRole::seventh, "maj7", true); break;
        case SeventhType::diminished: add (9, ChordToneRole::seventh, "bb7", true); break;
        case SeventhType::none:       break;
    }

    for (auto extension : namedExtensions)
    {
        if (extension == Extension::flatFive || extension == Extension::sharpFive)
            continue;  // already placed as the chord's fifth

        if (extension == Extension::six)
        {
            // On a chord with no seventh the 6th carries the sound; alongside a
            // seventh it is heard as the 13th.
            add (9, ChordToneRole::sixth, "6", seventhType == SeventhType::none);
            continue;
        }

        add (semitonesAboveRoot (extension),
             ChordToneRole::extension,
             extensionLabel (extension),
             isAlteration (extension));
    }

    // A named 13th implies the 9th underneath it as available colour.
    if (hasExtension (Extension::thirteen) && ! hasExtension (Extension::flatNine)
        && ! hasExtension (Extension::sharpNine))
        add (2, ChordToneRole::extension, "9", false);

    std::stable_sort (tones.begin(), tones.end(),
                      [] (const ChordTone& a, const ChordTone& b) { return a.semitones < b.semitones; });

    return tones;
}

std::vector<ChordTone> ChordSymbol::essentialTones() const
{
    auto tones = chordTones();
    tones.erase (std::remove_if (tones.begin(), tones.end(),
                                 [] (const ChordTone& t) { return ! t.essential; }),
                 tones.end());
    return tones;
}

std::vector<ChordTone> ChordSymbol::guideTones() const
{
    std::vector<ChordTone> guides;

    for (const auto& tone : chordTones())
        if (tone.role == ChordToneRole::third || tone.role == ChordToneRole::seventh
            || tone.role == ChordToneRole::sixth)
            guides.push_back (tone);

    return guides;
}

std::uint16_t ChordSymbol::pitchClassMask() const
{
    std::uint16_t mask = 0;

    for (const auto& tone : chordTones())
        mask |= static_cast<std::uint16_t> (1u << toPitchClass (rootPitchClass + tone.semitones));

    if (bassPitchClass.has_value())
        mask |= static_cast<std::uint16_t> (1u << *bassPitchClass);

    return mask;
}

bool ChordSymbol::containsPitchClass (PitchClass pitchClass) const
{
    return (pitchClassMask() & (1u << toPitchClass (pitchClass))) != 0;
}

std::string ChordSymbol::toString (Accidental accidental) const
{
    std::string result = pitchClassName (rootPitchClass, accidental);

    const auto hasNine = hasExtension (Extension::nine);
    const auto hasEleven = hasExtension (Extension::eleven);
    const auto hasThirteen = hasExtension (Extension::thirteen);

    // The highest natural extension stands in for the seventh: C7 + 13 -> C13.
    const std::string highest = hasThirteen ? "13" : hasEleven ? "11" : hasNine ? "9" : "7";

    switch (chordQuality)
    {
        case ChordQuality::major:
            if (seventhType == SeventhType::major)
                result += "maj" + highest;
            else if (hasExtension (Extension::six))
                result += hasNine ? "6/9" : "6";
            break;

        case ChordQuality::minor:
            result += "m";
            if (seventhType == SeventhType::minor)
                result += highest;
            else if (hasExtension (Extension::six))
                result += "6";
            break;

        case ChordQuality::minorMajor:
            result += "mMaj" + (seventhType == SeventhType::major ? highest : std::string ("7"));
            break;

        case ChordQuality::dominant:
            result += highest;
            break;

        case ChordQuality::halfDiminished:
            result += "m7b5";
            break;

        case ChordQuality::diminished:
            result += seventhType == SeventhType::diminished ? "dim7" : "dim";
            break;

        case ChordQuality::augmented:
            result += seventhType == SeventhType::none ? "+" : "+" + highest;
            break;

        case ChordQuality::suspended:
            if (seventhType != SeventhType::none)
                result += highest;
            result += suspendedSecond ? "sus2" : "sus4";
            break;
    }

    // A dominant carrying every alteration is written "alt", not spelled out.
    if (chordQuality == ChordQuality::dominant
        && hasExtension (Extension::flatNine) && hasExtension (Extension::sharpNine)
        && hasExtension (Extension::sharpEleven) && hasExtension (Extension::flatThirteen))
    {
        result += "alt";

        if (bassPitchClass.has_value() && *bassPitchClass != rootPitchClass)
            result += "/" + pitchClassName (*bassPitchClass, accidental);

        return result;
    }

    auto printable = namedExtensions;
    std::stable_sort (printable.begin(), printable.end(),
                      [] (Extension a, Extension b) { return printOrder (a) < printOrder (b); });

    for (auto extension : printable)
    {
        if (! isAlteration (extension))
            continue;  // natural colour tones are already folded into the suffix

        // A fifth that the quality already implies must not be printed twice.
        if ((extension == Extension::flatFive && chordQuality == ChordQuality::diminished)
            || (extension == Extension::flatFive && chordQuality == ChordQuality::halfDiminished)
            || (extension == Extension::sharpFive && chordQuality == ChordQuality::augmented))
            continue;

        result += extensionLabel (extension);
    }

    if (bassPitchClass.has_value() && *bassPitchClass != rootPitchClass)
        result += "/" + pitchClassName (*bassPitchClass, accidental);

    return result;
}

ChordSymbol ChordSymbol::transposed (int semitones) const
{
    auto copy = *this;
    copy.rootPitchClass = toPitchClass (rootPitchClass + semitones);

    if (bassPitchClass.has_value())
        copy.bassPitchClass = toPitchClass (*bassPitchClass + semitones);

    copy.sourceText = copy.toString();
    return copy;
}

bool ChordSymbol::operator== (const ChordSymbol& other) const
{
    auto sorted = [] (std::vector<Extension> v)
    {
        std::sort (v.begin(), v.end());
        return v;
    };

    return rootPitchClass == other.rootPitchClass
           && bassPitchClass == other.bassPitchClass
           && chordQuality == other.chordQuality
           && seventhType == other.seventhType
           && suspendedSecond == other.suspendedSecond
           && sorted (namedExtensions) == sorted (other.namedExtensions);
}

} // namespace jazz::core
