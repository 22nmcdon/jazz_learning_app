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

    /** The distinct pitch classes in the voicing, lowest note first. */
    std::vector<PitchClass> pitchClasses() const;

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
    solo,               ///< root and a partner in the left hand, colour in the right
    spread              ///< wide open voicing, root present
};

std::string voicingTypeName (VoicingType type);

/** How much colour a suggested voicing carries.

    The base shape of each type is the one it is defined by - a rootless left
    hand is 3-7-9 and 7-3-13, a two-handed voicing is 3-7 under 9-13. Asking for
    a rich voicing keeps that shape and opens it out with the tensions the symbol
    allows, which is where the alterations of a chord like G7alt actually sound.
*/
enum class VoicingDensity
{
    plain,
    rich
};

/** Builds idiomatic voicings for a chord - the "sentence starters" in the design
    doc, used both as suggestions and as the reference the analyser compares to.

    @param anchorNote  the lowest note the voicing may use. For a solo voicing
                       this is where the left hand starts, and it decides what
                       the left hand can safely play: see soloLeftHandPartner.
*/
std::vector<Voicing> idiomaticVoicings (const ChordSymbol& chord,
                                        VoicingType type,
                                        int anchorNote = 53 /* F3 */,
                                        VoicingDensity density = VoicingDensity::plain);

/** The voicing a comping piano plays for @p chord, having just played
    @p previousNotes.

    Two-handed rootless, because that is what a pianist comps behind a soloist
    with: no root to fight the bass, guide tones under colour. The shape comes
    from idiomaticVoicings like every other suggestion - what this adds is the
    choice *between* them and the register to put it in, made by voice leading
    rather than by a fixed anchor. Comping a tune at one anchor re-spells every
    chord from scratch and leaps a hand around the keyboard; a pianist moves as
    little as the next chord allows.

    @param previousNotes  the voicing just played, empty for the first chord of
                          a tune - which is what makes this a pure function of
                          the two rather than something with a memory.
*/
Voicing compingVoicing (const ChordSymbol& chord,
                        const std::vector<int>& previousNotes = {});

/** The same, kept inside a stated register.

    Two overloads rather than a defaulted pair, because the numbers belong to
    whoever asked. The window in the one above is where a comping voicing sits
    when nobody has said; a `CompStyleDefinition` is a style saying, and comping
    under a soloist and comping alone are genuinely different registers.

    This is what holds the generator to the same register the evaluator marks a
    player against. Without it the search reaches wherever the voice leading is
    cheapest - which was three semitones under Basie's own floor - and the app
    comps in a style it would then read as out of that style's register.

    A chord with nothing at all inside the window falls back to the best
    voicing outside it: a comp that goes silent is worse than a comp that
    stretches, and a style whose window has no room for a chord it will meet is
    a number that wants changing rather than a rule that wants bending.
*/
Voicing compingVoicing (const ChordSymbol& chord,
                        const std::vector<int>& previousNotes,
                        int lowestNote,
                        int highestNote);

/** The register a shape belongs in - the lowest note it should reach for.

    A solo left hand lives an octave and a half below a rootless one, so handing
    every type the same anchor puts half of them in the wrong part of the
    keyboard. Callers that have no register in mind should ask for this one.
*/
int naturalAnchorFor (VoicingType type);

/** What the left hand plays above the root in a solo voicing, in semitones.

    Playing the root and the 7th together states the harmony on its own, which is
    why it is the first choice - but low down those two notes turn to mud, so the
    interval opens out as the root descends: the 7th down to about C3, the 5th
    below that, and nothing but the octave in the bottom register.
*/
int soloLeftHandPartner (const ChordSymbol& chord, int rootNote);

} // namespace jazz::core
