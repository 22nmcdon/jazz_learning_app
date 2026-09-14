#include "jazz/core/Voicing.h"

#include <algorithm>

namespace jazz::core
{

Voicing Voicing::fromNotes (std::vector<int> notes)
{
    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());

    Voicing voicing;
    voicing.midiNotes = std::move (notes);
    return voicing;
}

int Voicing::lowestNote() const  { return midiNotes.empty() ? -1 : midiNotes.front(); }
int Voicing::highestNote() const { return midiNotes.empty() ? -1 : midiNotes.back(); }

int Voicing::spanInSemitones() const
{
    return midiNotes.empty() ? 0 : highestNote() - lowestNote();
}

std::uint16_t Voicing::pitchClassMask() const
{
    std::uint16_t mask = 0;

    for (auto note : midiNotes)
        mask |= static_cast<std::uint16_t> (1u << toPitchClass (note));

    return mask;
}

bool Voicing::containsPitchClass (PitchClass pitchClass) const
{
    return (pitchClassMask() & (1u << toPitchClass (pitchClass))) != 0;
}

std::vector<int> Voicing::doubledNotes() const
{
    std::vector<int> doubled;

    for (std::size_t i = 0; i < midiNotes.size(); ++i)
        for (std::size_t j = i + 1; j < midiNotes.size(); ++j)
            if (toPitchClass (midiNotes[i]) == toPitchClass (midiNotes[j]))
                doubled.push_back (midiNotes[j]);

    return doubled;
}

std::string Voicing::describe (Accidental accidental) const
{
    std::string result;

    for (std::size_t i = 0; i < midiNotes.size(); ++i)
    {
        if (i > 0)
            result += " ";

        result += midiNoteName (midiNotes[i], accidental);
    }

    return result;
}

std::string voicingTypeName (VoicingType type)
{
    switch (type)
    {
        case VoicingType::singleNote:        return "single note";
        case VoicingType::shell:             return "shell voicing";
        case VoicingType::rootPosition:      return "root-position voicing";
        case VoicingType::rootlessLeftHand:  return "rootless left-hand voicing";
        case VoicingType::twoHandedRootless: return "two-handed rootless voicing";
        case VoicingType::spread:            return "spread voicing";
        case VoicingType::unknown:           break;
    }

    return "voicing";
}

namespace
{
    bool has (const ChordSymbol& chord, Extension extension)
    {
        const auto& extensions = chord.extensions();
        return std::find (extensions.begin(), extensions.end(), extension) != extensions.end();
    }

    /** The chord's own ninth: altered if the symbol says so, natural otherwise. */
    int ninthOf (const ChordSymbol& chord)
    {
        if (has (chord, Extension::flatNine))  return 1;
        if (has (chord, Extension::sharpNine)) return 3;
        return 2;
    }

    /** What sits in the 5th/13th slot of a rootless voicing. */
    int upperFifthOf (const ChordSymbol& chord)
    {
        if (has (chord, Extension::flatThirteen) || has (chord, Extension::sharpFive)) return 8;
        if (has (chord, Extension::thirteen) || has (chord, Extension::six))           return 9;
        if (has (chord, Extension::flatFive) || chord.quality() == ChordQuality::diminished
            || chord.quality() == ChordQuality::halfDiminished)                        return 6;
        return 7;
    }

    int thirdOf (const ChordSymbol& chord)
    {
        if (chord.quality() == ChordQuality::suspended)
            return 5;

        return chord.hasMinorThird() ? 3 : 4;
    }

    int seventhOf (const ChordSymbol& chord)
    {
        switch (chord.seventh())
        {
            case SeventhType::major:      return 11;
            case SeventhType::minor:      return 10;
            case SeventhType::diminished: return 9;
            case SeventhType::none:       return 9;  // 6th chords: the 6 takes the slot
        }

        return 10;
    }

    /** Stacks the given root-relative offsets upwards, starting at @p anchorNote. */
    Voicing stack (const ChordSymbol& chord, const std::vector<int>& offsets, int anchorNote)
    {
        std::vector<int> notes;
        auto floorNote = anchorNote;

        for (auto offset : offsets)
        {
            const auto pitchClass = toPitchClass (chord.root() + offset);
            auto note = floorNote + toPitchClass (pitchClass - toPitchClass (floorNote));

            if (! notes.empty() && note <= notes.back())
                note += semitonesPerOctave;

            notes.push_back (note);
            floorNote = note;
        }

        return Voicing::fromNotes (std::move (notes));
    }
}

std::vector<Voicing> idiomaticVoicings (const ChordSymbol& chord, VoicingType type, int anchorNote)
{
    const auto third = thirdOf (chord);
    const auto seventh = seventhOf (chord);
    const auto ninth = ninthOf (chord);
    const auto fifth = upperFifthOf (chord);

    std::vector<Voicing> voicings;

    switch (type)
    {
        case VoicingType::shell:
            voicings.push_back (stack (chord, { 0, third, seventh }, anchorNote));
            voicings.push_back (stack (chord, { 0, seventh, third }, anchorNote));
            break;

        case VoicingType::rootPosition:
        case VoicingType::spread:
            voicings.push_back (stack (chord, { 0, third, fifth, seventh }, anchorNote));
            voicings.push_back (stack (chord, { 0, seventh, third, fifth, ninth }, anchorNote));
            break;

        case VoicingType::rootlessLeftHand:
        case VoicingType::unknown:
        case VoicingType::singleNote:
            // The two standard rootless shapes: "A form" from the 3rd, "B form"
            // from the 7th.
            voicings.push_back (stack (chord, { third, fifth, seventh, ninth }, anchorNote));
            voicings.push_back (stack (chord, { seventh, ninth, third, fifth }, anchorNote));
            break;

        case VoicingType::twoHandedRootless:
            voicings.push_back (stack (chord, { third, seventh, ninth, fifth }, anchorNote));
            voicings.push_back (stack (chord, { seventh, third, fifth, ninth }, anchorNote));
            break;
    }

    return voicings;
}

} // namespace jazz::core
