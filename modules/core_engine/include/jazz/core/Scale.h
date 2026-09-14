#pragma once

#include "jazz/core/Pitch.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jazz::core
{

/** The parent collection a scale belongs to. Grouping modes by parent lets the
    UI offer "other modes of this scale" without hard-coding relationships.
*/
enum class ScaleFamily
{
    major,
    melodicMinor,
    harmonicMinor,
    symmetric,
    pentatonic,
    bebop
};

/** A scale shape, independent of the key it is played in. */
struct ScaleDefinition
{
    std::string name;            ///< "Lydian Dominant"
    ScaleFamily family {};
    std::vector<int> intervals;  ///< semitones above the tonic, starting at 0

    /** Bitmask of scale degrees relative to the tonic, bit 0 = tonic. */
    std::uint16_t intervalMask() const;
};

/** A scale definition rooted on a specific pitch: "Bb Lydian Dominant". */
struct Scale
{
    const ScaleDefinition* definition {};
    PitchClass tonic {};

    std::string name (Accidental accidental = Accidental::flats) const;

    /** Bitmask of the pitch classes in this scale, bit 0 = C. */
    std::uint16_t pitchClassMask() const;

    /** The scale's pitches as absolute pitch classes, tonic first. */
    std::vector<PitchClass> pitchClasses() const;

    /** The scale spelled the way a musician would write it, tonic first.

        A seven-note scale uses each letter once, so G Lydian Dominant reads
        G A B C# D E F rather than G A B Db D E F. Scales that do not map onto
        seven letters - pentatonics, whole tone, diminished, bebop - fall back
        to plain sharp or flat names.
    */
    std::vector<std::string> noteNames() const;

    bool contains (PitchClass pitchClass) const;
};

/** Every scale shape the engine knows about. */
const std::vector<ScaleDefinition>& scaleCatalogue();

/** Looks a definition up by name, or nullptr if it is not in the catalogue. */
const ScaleDefinition* findScaleDefinition (std::string_view name);

/** The other modes of the same parent scale, for the "related scales" list. */
std::vector<Scale> modesOf (const Scale& scale);

} // namespace jazz::core
