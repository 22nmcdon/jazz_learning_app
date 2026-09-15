#pragma once

#include "jazz/core/Pitch.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jazz::core
{

/** The basic triad/seventh family a chord belongs to. Extensions and alterations
    are carried separately so a symbol like "C7alt" is one quality plus colours.
*/
enum class ChordQuality
{
    major,          ///< C, Cmaj7, C6
    minor,          ///< Cm, Cm7, Cm6
    dominant,       ///< C7, C13, C7alt
    halfDiminished, ///< Cm7b5
    diminished,     ///< Cdim, Cdim7
    augmented,      ///< C+, Caug7
    suspended,      ///< Csus4, C7sus4, Csus2
    minorMajor      ///< CmMaj7
};

/** Which seventh (if any) the symbol carries. */
enum class SeventhType
{
    none,
    minor,      ///< b7, 10 semitones
    major,      ///< maj7, 11 semitones
    diminished  ///< bb7, 9 semitones (fully diminished chords)
};

/** A colour tone named explicitly by the symbol. */
enum class Extension
{
    flatNine,
    nine,
    sharpNine,
    eleven,
    sharpEleven,
    flatThirteen,
    thirteen,
    six,
    flatFive,
    sharpFive
};

/** Semitones above the root, folded into one octave. */
int semitonesAboveRoot (Extension extension) noexcept;

/** "b9", "#11", ... as written on a lead sheet. */
std::string extensionLabel (Extension extension);

/** The role a note plays within a chord, used to decide how badly it is missed. */
enum class ChordToneRole
{
    root,
    third,      ///< or the 4th/2nd of a suspended chord
    fifth,
    seventh,
    sixth,
    extension
};

struct ChordTone
{
    int semitones {};          ///< above the root, 0..11
    ChordToneRole role {};
    std::string label;         ///< "R", "b3", "#11", ...
    bool essential {};         ///< defines the chord's identity, as opposed to colour
};

/** A parsed chord symbol: everything the engine needs to reason about one chord.

    ChordSymbol is pure data with no JUCE or UI dependency, so the same instance
    is shared by the chart view, the scale suggester and the voicing analyser.
*/
class ChordSymbol
{
public:
    ChordSymbol() = default;

    /** Parses a lead-sheet symbol such as "Dm7", "F#m7b5", "Bb13#11", "C7alt",
        "Ebmaj7#5" or "Am7/D". Returns nullopt when the text is not a chord.
    */
    static std::optional<ChordSymbol> parse (std::string_view text);

    PitchClass root() const noexcept                   { return rootPitchClass; }
    const std::optional<PitchClass>& bass() const noexcept { return bassPitchClass; }
    ChordQuality quality() const noexcept              { return chordQuality; }
    SeventhType seventh() const noexcept               { return seventhType; }
    const std::vector<Extension>& extensions() const noexcept { return namedExtensions; }

    /** The symbol as it was written, e.g. "Bb13#11". */
    const std::string& text() const noexcept           { return sourceText; }

    /** True when the chord's third is a minor third (affects interval spelling). */
    bool hasMinorThird() const noexcept;

    /** Every tone the symbol implies, ordered root upwards. */
    std::vector<ChordTone> chordTones() const;

    /** Tones whose absence changes what chord is heard: root, third, seventh,
        altered fifths and any named alteration. A rootless voicing is judged
        against this list minus the root - see VoicingAnalyzer.
    */
    std::vector<ChordTone> essentialTones() const;

    /** The 3rd and 7th (or 6th), which carry the harmony in a jazz voicing. */
    std::vector<ChordTone> guideTones() const;

    /** Bitmask of pitch classes in the chord, bit 0 = C. */
    std::uint16_t pitchClassMask() const;

    bool containsPitchClass (PitchClass pitchClass) const;

    /** Re-renders the symbol from the parsed data rather than echoing the input,
        so generated chords (reharmonisations) print consistently. Uses the
        chord's own accidental preference, which is flats unless set otherwise.
    */
    std::string toString() const;

    /** Re-renders with a specific accidental, ignoring the chord's preference. */
    std::string toString (Accidental accidental) const;

    /** How this chord spells itself: how it was written, when it was parsed. */
    Accidental accidental() const noexcept             { return preferredAccidental; }

    /** Returns a copy that spells itself with @p accidental. Chromatic chords
        that rise into the next chord read better sharp (C#dim7 between Cmaj7
        and Dm7); everything else in jazz is conventionally flat.
    */
    ChordSymbol withAccidental (Accidental accidental) const;

    /** Returns a copy transposed by @p semitones. */
    ChordSymbol transposed (int semitones) const;

    /** Returns a copy sounding over @p bass, e.g. Cmaj7 over E, or a D triad
        over a C pedal. Passing the chord's own root clears the slash.
    */
    ChordSymbol overBass (PitchClass bass) const;

    /** Semitones from the root to the chord's third (or the 4th/2nd of a
        suspended chord), for rules that need the third specifically.
    */
    int thirdSemitones() const;

    /** Builds a symbol directly, for reharmonisation output. */
    static ChordSymbol build (PitchClass root,
                              ChordQuality quality,
                              SeventhType seventh,
                              std::vector<Extension> extensions = {});

    bool operator== (const ChordSymbol& other) const;
    bool operator!= (const ChordSymbol& other) const { return ! (*this == other); }

private:
    void addExtension (Extension extension);
    bool hasExtension (Extension extension) const;

    PitchClass rootPitchClass {};
    std::optional<PitchClass> bassPitchClass;
    ChordQuality chordQuality { ChordQuality::major };
    SeventhType seventhType { SeventhType::none };
    std::vector<Extension> namedExtensions;
    bool suspendedSecond {};   ///< sus2 rather than sus4
    Accidental preferredAccidental { Accidental::flats };
    std::string sourceText;
};

} // namespace jazz::core
