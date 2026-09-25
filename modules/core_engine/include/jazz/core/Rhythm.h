#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

/** Every subdivision there is, in the order a player would count them.

    One list, so nothing has to write out the four by hand - `subdivisionFrom`
    reads against it and a shell builds its menu from it. A fifth added to the
    enum and not to this is a compile-time hole rather than a silent one,
    because everything that walks the set walks this.
*/
std::vector<Subdivision> allSubdivisions();

/** Ticks between two onsets of @p subdivision. */
int ticksFor (Subdivision subdivision);

std::string subdivisionName (Subdivision subdivision);

/** The inverse of `subdivisionName`, for text that has to come back in.

    Empty for a name this version does not know, rather than a guess. A style
    arriving from outside the engine carries its feel as the word the engine
    itself wrote, and a feel that cannot be read is the one thing about a style
    that must not be quietly defaulted: the feel is the grid the evaluator
    measures a player's placement against, so reading "eighth-note triplets" as
    eighths would mark a ballad's every hit as off the grid.
*/
std::optional<Subdivision> subdivisionFrom (std::string_view name);

/** A position within a bar: which beat, and how far into it. */
struct BarPosition
{
    int beat { 0 };   ///< 0-based; 0 is the downbeat
    int tick { 0 };   ///< 0 to ticksPerBeat - 1

    int inTicks() const noexcept { return beat * ticksPerBeat + tick; }

    static BarPosition fromTicks (int ticks);

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

/** Whether a position falls on *any* of several subdivisions' grids.

    A player's vocabulary is rarely one subdivision. A bebop line is written in
    eighths and plays triplets, and holding it to the eighths alone marks a
    player down for a figure the app itself taught them - which is why
    `subdivisionsFor` exists and why both of its readers ask this rather than
    asking `onTheGrid` in a loop of their own.

    One answer in one place, deliberately: `lineFaults` decides whether a
    *written* note is off the style's grid and `readLinePlacement` decides
    whether a *played* one is, and the two coming to different conclusions
    about the same note is the failure the whole arrangement exists to prevent.
    They held identical five-line lambdas for a while, which is that failure
    waiting rather than that failure happening.

    False for an empty list, which is the honest answer to "is this on none of
    these grids" and not a case any caller has.
*/
bool onAnyGrid (BarPosition position, const std::vector<Subdivision>& subdivisions);

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

/** True for the downbeat and the bar's other accented beat. */
bool isStrong (BarPosition position, int beatsPerBar);

} // namespace jazz::core
