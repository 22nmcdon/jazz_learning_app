#include "jazz/core/Rhythm.h"

#include <algorithm>
#include <cmath>

namespace jazz::core
{

std::vector<Subdivision> allSubdivisions()
{
    return { Subdivision::beat, Subdivision::eighth,
             Subdivision::tripletEighth, Subdivision::sixteenth };
}

int ticksFor (Subdivision subdivision)
{
    switch (subdivision)
    {
        case Subdivision::beat:          return ticksPerBeat;
        case Subdivision::eighth:        return ticksPerBeat / 2;
        case Subdivision::tripletEighth: return ticksPerBeat / 3;
        case Subdivision::sixteenth:     return ticksPerBeat / 4;
    }

    return ticksPerBeat;
}

std::string subdivisionName (Subdivision subdivision)
{
    switch (subdivision)
    {
        case Subdivision::beat:          return "beats";
        case Subdivision::eighth:        return "eighths";
        case Subdivision::tripletEighth: return "eighth-note triplets";
        case Subdivision::sixteenth:     return "sixteenths";
    }

    return "beats";
}

std::optional<Subdivision> subdivisionFrom (std::string_view name)
{
    /*  Against `subdivisionName`'s own output, not against a second list of
        spellings. The two are one table read in two directions, so a name
        added to one cannot go missing from the other - which is the way every
        other catalogue in this engine is kept honest. */
    for (const auto subdivision : allSubdivisions())
        if (subdivisionName (subdivision) == name)
            return subdivision;

    return {};
}

BarPosition BarPosition::fromTicks (int ticks)
{
    // Floored rather than truncated, so a position before the downbeat lands in
    // the bar before it rather than folding onto beat zero.
    auto beat = ticks / ticksPerBeat;
    auto tick = ticks % ticksPerBeat;

    if (tick < 0)
    {
        tick += ticksPerBeat;
        beat -= 1;
    }

    return { beat, tick };
}

std::string BarPosition::describe() const
{
    const auto number = std::to_string (beat + 1);

    if (tick == 0)
        return number;

    if (tick == ticksPerBeat / 2)
        return number + " and";

    if (tick == ticksPerBeat / 3)
        return number + " trip";

    if (tick == 2 * ticksPerBeat / 3)
        return number + " let";

    if (tick == ticksPerBeat / 4)
        return number + " e";

    if (tick == 3 * ticksPerBeat / 4)
        return number + " a";

    // Anything else has no name a player would use, so say where it is.
    return number + " +" + std::to_string (tick);
}

bool operator== (const BarPosition& a, const BarPosition& b) noexcept
{
    return a.beat == b.beat && a.tick == b.tick;
}

bool operator!= (const BarPosition& a, const BarPosition& b) noexcept
{
    return ! (a == b);
}

bool operator< (const BarPosition& a, const BarPosition& b) noexcept
{
    return a.inTicks() < b.inTicks();
}

bool onTheGrid (BarPosition position, Subdivision subdivision)
{
    const auto step = ticksFor (subdivision);

    return step > 0 && position.tick % step == 0;
}

bool onAnyGrid (BarPosition position, const std::vector<Subdivision>& subdivisions)
{
    return std::any_of (subdivisions.begin(), subdivisions.end(),
                        [position] (Subdivision subdivision)
                        { return onTheGrid (position, subdivision); });
}

BeatStrength strengthAt (BarPosition position, int beatsPerBar)
{
    if (position.tick != 0)
        return BeatStrength::offbeat;

    if (position.beat == 0)
        return BeatStrength::downbeat;

    /*  The other accented beat is the one that starts the bar's second half,
        which is three in four and does not exist in three. Deriving it rather
        than tabulating it is what keeps a waltz from being a special case: in
        an odd metre there is no half to start, and only the downbeat is
        strong, which is exactly how three feels. */
    if (beatsPerBar % 2 == 0 && position.beat == beatsPerBar / 2)
        return BeatStrength::strong;

    return BeatStrength::weak;
}

bool isStrong (BarPosition position, int beatsPerBar)
{
    const auto strength = strengthAt (position, beatsPerBar);

    return strength == BeatStrength::downbeat || strength == BeatStrength::strong;
}

} // namespace jazz::core
