#pragma once

#include <optional>
#include <string>

namespace jazz::core
{

/** Where in a bar something falls, and nothing about when it happened.

    This is the one grid in the engine, and it has two consumers: comping, which
    asks which slots a style is allowed to place a hit in, and solo practice,
    which asks where in the bar a note landed. One representation rather than
    two, because the moment there are two they disagree about what an eighth is.

    There is still no clock here. A `BarPosition` is a *position*, like a
    measure index is - the shell turns wall-clock time into one of these before
    the engine ever sees it, and turns one back into a time when it plays it.
    That is the same division the transport already works to.
*/

/** Ticks in one beat.

    24 divides by 2, 3, 4, 6, 8 and 12, so straight eighths (12), eighth-note
    triplets (8), sixteenths (6) and the dotted forms of each all land exactly
    on a tick. A straight eighth grid would have been simpler and could not
    have written a ballad's triplets or a bebop line's sixteenths at all.

    Swing is deliberately *not* in here. Swung eighths are a performance ratio,
    not a different set of positions: the "and" is still the eighth, and the
    shell decides how late it falls when it turns the position into a time. An
    engine that wrote swing into the grid would be holding a tempo-dependent
    feel in the one place that must not know about time.
*/
constexpr int ticksPerBeat = 24;

/** How a beat is being divided, for styles to be written in readable terms. */
enum class Subdivision
{
    beat,
    eighth,
    tripletEighth,
    sixteenth
};

/** Ticks between two onsets of @p subdivision. */
int ticksFor (Subdivision subdivision);

std::string subdivisionName (Subdivision subdivision);

/** A position within a bar: which beat, and how far into it. */
struct BarPosition
{
    int beat { 0 };   ///< 0-based; 0 is the downbeat
    int tick { 0 };   ///< 0 to ticksPerBeat - 1

    int inTicks() const noexcept { return beat * ticksPerBeat + tick; }

    static BarPosition fromTicks (int ticks);

    /** Where a beat count that may be fractional lands, for a shell turning a
        moment on its own clock into a position. Out-of-range beats are the
        caller's to clamp: a note played a hair before the downbeat is the next
        bar's problem, and only the shell knows which bar it meant.
    */
    static BarPosition fromBeats (double beats);

    bool onTheBeat() const noexcept { return tick == 0; }

    /** "1", "2 and", "3 (e)", "4 trip" - how a player would count it aloud. */
    std::string describe() const;
};

bool operator== (const BarPosition& a, const BarPosition& b) noexcept;
bool operator!= (const BarPosition& a, const BarPosition& b) noexcept;
bool operator<  (const BarPosition& a, const BarPosition& b) noexcept;

/** Whether a position falls on the grid a subdivision implies.

    The **vocabulary** a feel makes available, as against the particular figure
    played out of it: an eighth feel offers the beats and the ands, a triplet
    feel offers the three notes of the beat. Derived from `ticksFor` rather than
    tabulated, for the same reason `strengthAt` is derived from the metre - a
    table is a second place for the answer to live.

    Comping leans on the distinction hardest. A style's slots say where the band
    puts its chords; this says where a player of that style could put one and
    still be playing it, which is a much wider set and the honest standard to
    read someone against.
*/
bool onTheGrid (BarPosition position, Subdivision subdivision);

/** How much weight a position carries in the bar.

    Shared by both consumers on purpose: a comping style says which of these it
    likes to land on, and solo practice asks whether a chord tone landed on one.
    Written as metre-aware rather than as a table, so a waltz is not a special
    case - in three, only the downbeat is strong, which is the whole character
    of the metre.
*/
enum class BeatStrength
{
    downbeat,   ///< beat one
    strong,     ///< the other accented beat of the bar - three, in four
    weak,       ///< an unaccented beat
    offbeat     ///< anywhere that is not on a beat at all
};

BeatStrength strengthAt (BarPosition position, int beatsPerBar);

std::string beatStrengthName (BeatStrength strength);

/** True for the downbeat and the bar's other accented beat. */
bool isStrong (BarPosition position, int beatsPerBar);

} // namespace jazz::core
