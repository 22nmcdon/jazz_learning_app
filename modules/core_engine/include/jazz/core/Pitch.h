#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace jazz::core
{

/** Number of semitones in an octave. Used everywhere pitch classes are folded. */
inline constexpr int semitonesPerOctave = 12;

/** MIDI note number of middle C (C4 in the convention this project uses). */
inline constexpr int middleC = 60;

/** A pitch class is a note name folded into a single octave: 0 = C ... 11 = B. */
using PitchClass = int;

/** Folds any integer (MIDI note, negative interval, ...) into 0..11. */
int toPitchClass (int value) noexcept;

/** Ascending distance in semitones from one pitch class to another, 0..11. */
int ascendingInterval (PitchClass from, PitchClass to) noexcept;

/** Octave number of a MIDI note, with middle C (60) in octave 4. */
int octaveOf (int midiNote) noexcept;

/** Spelling preference for a pitch class that has no single correct name. */
enum class Accidental
{
    sharps,
    flats
};

/** "C", "Bb", "F#" ... Enharmonic spelling follows @p accidental. */
std::string pitchClassName (PitchClass pitchClass, Accidental accidental = Accidental::flats);

/** "Bb3", "C4" ... */
std::string midiNoteName (int midiNote, Accidental accidental = Accidental::flats);

/** Parses "C", "c", "Bb", "F#", "Eb" into a pitch class. */
std::optional<PitchClass> parsePitchClass (std::string_view text);

/** Reads a root note from the front of @p text, returning the pitch class and
    how many characters it consumed. Returns nullopt if @p text does not start
    with a note name.
*/
struct ParsedRoot
{
    PitchClass pitchClass {};
    std::size_t charactersConsumed {};
};

std::optional<ParsedRoot> parseRoot (std::string_view text);

/** Interval spelling relative to a chord root, e.g. 4 -> "3", 10 -> "b7".

    @param semitones          distance above the root, any range, folded to an octave
    @param spellThirdAsMinor  true when the chord's third is minor, so 3 semitones
                              reads as "b3" rather than "#9"
*/
std::string intervalLabel (int semitones, bool spellThirdAsMinor);

} // namespace jazz::core
