#pragma once

#include "jazz/core/Chart.h"
#include "jazz/core/Rhythm.h"
#include "jazz/core/Voicing.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jazz::core
{

/** One place in a bar a style is willing to put a hit, and how often it does.

    A slot is written the way a player would describe one - "every beat", "the
    and of four" - rather than as an absolute tick, because a style has to
    survive being played in a metre it was not written for. `beat` says which
    beat, counting from zero, or from the end of the bar when it is negative,
    so "the and of the last beat" is one slot that means the same thing in four
    and in three. Left empty it means every beat, which is how four-to-the-bar
    is one slot rather than four.
*/
struct CompSlot
{
    std::optional<int> beat;      ///< empty = every beat; negative counts back from the end
    int tick { 0 };               ///< where within that beat, 0 to ticksPerBeat - 1
    int weight { 50 };            ///< 0-100: how often this style takes the slot

    /** This hit belongs to the *next* bar's chord, played early.

        A comper pushing the chord change across the barline is the single most
        characteristic thing about the idiom, and it is a property of the slot
        rather than of the moment: the and of four anticipates, the downbeat
        does not.
    */
    bool anticipates { false };
};

/** Everything a comping style is, as data.

    One artifact, three readers: the generator samples from it, the evaluator
    scores a player against it, and the menu is built from the same list. A
    style described in one place and re-described in another is how the two
    drift, which is the same reason `scaleStyles()` lives in the engine.
*/
struct CompStyleDefinition
{
    std::string key;           ///< "four", "basie" - what the wire and the page use
    std::string name;          ///< "Four to the bar"
    std::string summary;       ///< one line, for the menu

    /** The subdivision the style is counted in. Nothing enforces it - it is
        what a shell needs in order to say "swung eighths" and mean it, and
        what tells a reader at a glance whether a style is a triplet feel. */
    Subdivision feel { Subdivision::eighth };

    std::vector<CompSlot> slots;

    /** Hits per bar, before anticipations are counted. The generator samples
        slots by weight and then trims or tops up to land inside this, which is
        what stops a run of unlucky rolls producing a silent bar or a wall. */
    int fewestPerBar { 1 };
    int mostPerBar { 4 };

    /** The register the style's voicings sit in, as MIDI notes. Comping under
        a soloist and comping alone are different registers, and this is where
        that difference is written down. */
    int lowestNote { 45 };
    int highestNote { 84 };
};

/** Every style the engine knows, in menu order. */
std::vector<CompStyleDefinition> compStyles();

/** The style with this key, or the first one when the key is unknown.

    Never empty: a shell asking for a style that has been renamed should get
    comping in some style rather than silence.
*/
const CompStyleDefinition& compStyleFor (const std::string& key);

/** The positions a slot resolves to in a bar of @p beatsPerBar beats.

    Empty when the slot names a beat the metre does not have - a style written
    around the fourth beat simply has less to say in three, which is more
    honest than folding it onto a beat that does exist.
*/
std::vector<BarPosition> slotPositions (const CompSlot& slot, int beatsPerBar);

/** One chord, struck once. */
struct CompHit
{
    int measureIndex {};
    BarPosition at {};

    /** The voicing to play, already led from the hit before it. */
    std::vector<int> midiNotes;

    std::string chordSymbol;   ///< what this hit is voicing
    bool anticipation {};      ///< sounds the next bar's chord, early
};

/** What a comper plays over a range of bars. */
struct CompPlan
{
    std::vector<CompHit> hits;

    bool isEmpty() const noexcept { return hits.empty(); }
};

/** Plans the comping for a range of bars, in a style, reproducibly.

    Planned ahead rather than decided per beat, for two reasons that are really
    one: a hit that anticipates the next bar has to know what the next chord is,
    and a voicing has to be led from the one before it. Both need more than the
    moment they are played in, which a reactive comper does not have.

    @param seed  the same seed gives the same plan, note for note. Bar index is
                 mixed into it, so a loop that comes round again is planned the
                 same way it was the first time rather than drifting.
*/
CompPlan compPlan (const Chart& chart,
                   const CompStyleDefinition& style,
                   int fromBar,
                   int toBar,
                   std::uint32_t seed);

/** Whether a hit is one this style could have played.

    The slot half of the evaluator, and the half the generator is held to: a
    style that generates what it would then mark as out of style is broken in
    one of the two directions, and this is what catches it. Scoring a player's
    density, register and voicing against a style is the rest of the evaluator
    and is not built yet - this is the seam it grows from.
*/
bool fitsStyle (const CompHit& hit, const CompStyleDefinition& style, int beatsPerBar);

} // namespace jazz::core
