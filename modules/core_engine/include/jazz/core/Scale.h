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

/** A named vocabulary to solo out of: which families of scale are in play.

    The catalogue holds 29 shapes and a player working on one thing does not
    want all of them. A style is how that choice is offered - "the modes",
    "bebop" - and it is curated here rather than in a UI, because which scales
    belong together is theory and the shells hold none.

    Styles are built out of `ScaleFamily` rather than listing scales by name, so
    a shape added to the catalogue joins the style it belongs to by itself and
    cannot be forgotten.
*/
struct ScaleStyle
{
    std::string key;       ///< stable across versions; what a shell stores
    std::string name;      ///< "The modes"
    std::string summary;   ///< one line, for the menu
    std::vector<ScaleFamily> families;  ///< empty means every family

    bool includes (ScaleFamily family) const;
};

/** Every style, in the order a menu should offer them: the plainest first. */
const std::vector<ScaleStyle>& scaleStyles();

/** Looks a style up by key. Null for an unknown one - which a caller should
    read as "no style", not as an error: a stored key from an older version
    should widen the choice, never empty it. */
const ScaleStyle* findScaleStyle (std::string_view key);

/** Every scale shape the engine knows about. */
const std::vector<ScaleDefinition>& scaleCatalogue();

/** Looks a definition up by name, or nullptr if it is not in the catalogue. */
const ScaleDefinition* findScaleDefinition (std::string_view name);

/** The other modes of the same parent scale, for the "related scales" list. */
std::vector<Scale> modesOf (const Scale& scale);

} // namespace jazz::core
