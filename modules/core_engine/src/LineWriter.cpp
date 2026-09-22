#include "jazz/core/LineWriter.h"

#include <algorithm>
#include <cstdlib>

namespace jazz::core
{

namespace
{
    /*  The same hash `compPlan` draws from, and for the same reason: a
        generator whose distribution differs between standard libraries plays a
        different line in the browser and in the app. Copied rather than shared
        because moving it would touch comping's byte-for-byte reproducibility
        to save nine lines, and that trade is the wrong way round. */
    std::uint32_t mix (std::uint32_t seed, std::uint32_t salt)
    {
        auto x = seed + 0x9e3779b9u * (salt + 1u);

        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;

        return x;
    }

    int roll (std::uint32_t seed, std::uint32_t salt)
    {
        return static_cast<int> (mix (seed, salt) % 100u);
    }

    bool holds (const std::vector<PitchClass>& classes, int midiNote)
    {
        return std::find (classes.begin(), classes.end(), toPitchClass (midiNote)) != classes.end();
    }

    /** The nearest note of @p classes to @p from, inside the register.

        Nearest rather than next-in-the-scale, because a line moves by the
        smallest step available and a generator that always went up would be an
        arpeggio with extra steps. Ties go to the direction it was already
        travelling, which is what keeps a phrase going somewhere.
    */
    int nearestOf (const std::vector<PitchClass>& classes, int from, int direction)
    {
        auto best = -1;
        auto bestDistance = 1000;

        for (auto note = lowestLineNote; note <= highestLineNote; ++note)
        {
            if (! holds (classes, note) || note == from)
                continue;

            auto distance = std::abs (note - from) * 2;

            // A half-step of preference for carrying on the way it was going.
            if (direction != 0 && ((note > from) != (direction > 0)))
                distance += 1;

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = note;
            }
        }

        return best;
    }

    /** Where a line starts when it has nowhere to come from: a chord tone in
        the middle of the register, so it has room to move either way. */
    int firstNote (const std::vector<PitchClass>& chordTones)
    {
        const auto middle = (lowestLineNote + highestLineNote) / 2;
        const auto found = nearestOf (chordTones, middle, 0);

        return found > 0 ? found : middle;
    }

    std::vector<PitchClass> toneClasses (const ChordSymbol& chord)
    {
        std::vector<PitchClass> classes;

        for (const auto& tone : chord.chordTones())
            classes.push_back (toPitchClass (chord.root() + tone.semitones));

        return classes;
    }
}

std::vector<WrittenNote> improvisedLine (const Chart& chart,
                                         int fromBar,
                                         int toBar,
                                         const std::string& chosenScale,
                                         const std::string& scaleStyle,
                                         std::uint32_t seed)
{
    std::vector<WrittenNote> line;

    if (fromBar < 0 || toBar < fromBar || chart.measureCount() == 0)
        return line;

    toBar = std::min (toBar, chart.measureCount() - 1);

    LineAnalyzer::Options options;
    options.chosenScale = chosenScale;
    options.style = scaleStyle;
    options.beatsPerBar = std::max (1, chart.timeSignature.numerator);

    auto previous = -1;
    auto direction = 1;

    for (auto bar = fromBar; bar <= toBar; ++bar)
    {
        const auto* chord = chart.chordAt (bar);

        if (chord == nullptr)
            continue;

        const auto barSeed = mix (seed, static_cast<std::uint32_t> (bar));
        const auto chordTones = toneClasses (*chord);
        const auto scale = readingScaleFor (*chord, options);
        const auto scaleTones = scale.has_value() ? scale->scale.pitchClasses() : chordTones;

        // The bar after this one, for the approach to aim at. The last bar of
        // the range aims at nothing and simply finishes on the chord.
        const auto* next = bar < toBar ? chart.chordAt (bar + 1) : nullptr;
        const auto changes = next != nullptr && next->root() != chord->root();

        std::uint32_t salt = 0;

        /*  Every eighth of the bar, then thinned. Written as a full grid first
            because which notes are *left* is what gives a line its rhythm, and
            deciding that per slot against the beat it sits on is the same
            arithmetic `strengthAt` already does for reading one. */
        std::vector<BarPosition> slots;

        for (auto beat = 0; beat < options.beatsPerBar; ++beat)
        {
            slots.push_back ({ beat, 0 });
            slots.push_back ({ beat, ticksPerBeat / 2 });
        }

        std::vector<BarPosition> sounding;

        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const auto strong = isStrong (slots[i], options.beatsPerBar);

            // The downbeat always sounds - it is what states the bar. A strong
            // beat nearly always does. The rest is where the line breathes.
            const auto chance = i == 0 ? 100 : strong ? 85 : 62;

            if (roll (barSeed, salt++) < chance)
                sounding.push_back (slots[i]);
        }

        if (sounding.empty())
            sounding.push_back ({ 0, 0 });

        for (std::size_t i = 0; i < sounding.size(); ++i)
        {
            const auto at = sounding[i];
            const auto strong = isStrong (at, options.beatsPerBar);
            const auto last = i + 1 == sounding.size();

            WrittenNote note;
            note.measureIndex = bar;
            note.at = at;
            note.chordSymbol = chord->toString();

            /*  The approach: the last note of a bar whose chord is about to
                change, a semitone from where the next bar starts.

                It has to be **outside the scale the take reads against**, or
                the analyser will quite correctly call it a scale tone and the
                colour written here would be a lie. When neither neighbour is
                outside, there is no chromatic approach to write and the line
                simply plays a scale tone - which is the honest answer rather
                than a note tagged as something it is not. */
            if (last && changes && previous > 0)
            {
                const auto nextTones = toneClasses (*next);
                const auto target = nearestOf (nextTones, previous, direction);

                if (target > 0)
                {
                    for (const auto step : { -1, 1 })
                    {
                        const auto candidate = target + step;

                        if (candidate < lowestLineNote || candidate > highestLineNote) continue;
                        if (holds (scaleTones, candidate) || holds (chordTones, candidate)) continue;

                        note.midiNote = candidate;
                        note.colour = NoteColour::approach;
                        break;
                    }
                }
            }

            if (note.midiNote == 0)
            {
                const auto& from = strong ? chordTones : scaleTones;
                const auto picked = previous > 0 ? nearestOf (from, previous, direction)
                                                 : firstNote (chordTones);

                note.midiNote = picked > 0 ? picked : firstNote (chordTones);
                note.colour = holds (chordTones, note.midiNote) ? NoteColour::chordTone
                                                                : NoteColour::scaleTone;
            }

            if (previous > 0 && note.midiNote != previous)
                direction = note.midiNote > previous ? 1 : -1;

            // Turned round at the edges rather than clamped: a line that piled
            // up against the top of its register would repeat a note instead of
            // coming back down, which is not a phrase.
            if (note.midiNote >= highestLineNote - 2) direction = -1;
            if (note.midiNote <= lowestLineNote + 2)  direction = 1;

            previous = note.midiNote;
            line.push_back (note);
        }
    }

    return line;
}

} // namespace jazz::core
