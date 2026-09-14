#pragma once

#include "jazz/core/ChordSymbol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jazz::core
{

/** A set of notes sounding together, however they were entered.

    Both the MIDI keyboard and the on-screen keyboard produce Voicings through
    VoicingCollector, so nothing downstream knows which one the player used.
*/
struct Voicing
{
    std::vector<int> midiNotes;  ///< sorted ascending, no duplicates

    static Voicing fromNotes (std::vector<int> notes);

    bool isEmpty() const noexcept    { return midiNotes.empty(); }
    std::size_t size() const noexcept { return midiNotes.size(); }

    int lowestNote() const;
    int highestNote() const;
    int spanInSemitones() const;

    std::uint16_t pitchClassMask() const;
    bool containsPitchClass (PitchClass pitchClass) const;

    /** MIDI notes sharing a pitch class with another note in the voicing. */
    std::vector<int> doubledNotes() const;

    /** "Eb3 G3 Bb3 D4" */
    std::string describe (Accidental accidental = Accidental::flats) const;
};

/** How a voicing is laid out on the keyboard. The analyser judges a voicing
    against the expectations of its own type: a rootless left-hand voicing is
    not missing its root, it is built that way.
*/
enum class VoicingType
{
    unknown,
    singleNote,
    shell,              ///< root + 3rd + 7th
    rootPosition,       ///< root in the bass, chord stacked above
    rootlessLeftHand,   ///< no root, one hand, below middle C
    twoHandedRootless,  ///< no root, spread across both hands
    spread              ///< wide open voicing, root present
};

std::string voicingTypeName (VoicingType type);

/** Builds idiomatic voicings for a chord - the "sentence starters" in the design
    doc, used both as suggestions and as the reference the analyser compares to.

    @param anchorNote  the lowest note the voicing may use
*/
std::vector<Voicing> idiomaticVoicings (const ChordSymbol& chord,
                                        VoicingType type,
                                        int anchorNote = 53 /* F3 */);

} // namespace jazz::core
