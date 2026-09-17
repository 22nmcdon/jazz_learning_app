#include "jazz/core/Comping.h"

#include <algorithm>

namespace jazz::core
{

namespace
{
    /*  A hash, not a generator. `std::mt19937` is reproducible but the
        distributions over it are not specified, so the same seed can plan a
        different bar on another standard library - and a plan that differs
        between the browser and the app is two bands playing. This is a few
        lines of splitmix and it is identical everywhere. */
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

    /** 0-99 from a seed, for weighing against a slot's weight. */
    int roll (std::uint32_t seed, std::uint32_t salt)
    {
        return static_cast<int> (mix (seed, salt) % 100u);
    }

    const ChordSymbol* chordStartingBar (const Chart& chart, int measureIndex)
    {
        if (measureIndex < 0 || measureIndex >= chart.measureCount())
            return nullptr;

        const auto& measure = chart.measures[static_cast<std::size_t> (measureIndex)];

        return measure.isEmpty() ? nullptr : &measure.slots.front().chord;
    }

    /** The chord sounding at a position, so a bar of two chords is comped as
        two chords rather than as the first one all the way through. */
    const ChordSymbol* chordUnder (const Chart& chart, int measureIndex, BarPosition at)
    {
        if (measureIndex < 0 || measureIndex >= chart.measureCount())
            return nullptr;

        const auto& measure = chart.measures[static_cast<std::size_t> (measureIndex)];

        if (measure.isEmpty())
            return nullptr;

        auto beatsSoFar = 0;

        for (const auto& slot : measure.slots)
        {
            if (at.beat < beatsSoFar + slot.beats)
                return &slot.chord;

            beatsSoFar += slot.beats;
        }

        return &measure.slots.back().chord;
    }
}

std::vector<CompStyleDefinition> compStyles()
{
    std::vector<CompStyleDefinition> styles;

    {
        /*  Freddie Green: a chord on every beat, evenly, never louder than the
            band. One slot says all of it, and because the slot names no beat
            it is four in four and three in three without being told. */
        CompStyleDefinition four;
        four.key = "four";
        four.name = "Four to the bar";
        four.summary = "A chord on every beat, even and quiet - the rhythm guitar's job, "
                       "taken by the left hand.";
        four.feel = Subdivision::beat;
        four.slots = { CompSlot { std::nullopt, 0, 100, false } };
        four.fewestPerBar = 4;
        four.mostPerBar = 8;
        four.lowestNote = 45;
        four.highestNote = 76;
        styles.push_back (four);
    }

    {
        /*  The other end of the same idiom: Basie leaves the bar alone and
            answers at the end of it. Almost everything here is an anticipation,
            which is why `anticipates` had to be a property of the slot rather
            than a rule applied afterwards. */
        CompStyleDefinition basie;
        basie.key = "basie";
        basie.name = "Basie - sparse";
        basie.summary = "Next to nothing, mostly pushed across the barline. Leaves the most "
                        "room for a line.";
        basie.feel = Subdivision::eighth;
        basie.slots = {
            CompSlot { -1,           ticksPerBeat / 2, 75, true  },   // the and of the last beat
            CompSlot { 0,            0,                25, false },   // the downbeat, now and then
            CompSlot { 1,            ticksPerBeat / 2, 20, false }    // the and of two
        };
        basie.fewestPerBar = 0;
        basie.mostPerBar = 2;
        basie.lowestNote = 48;
        basie.highestNote = 79;
        styles.push_back (basie);
    }

    {
        /*  The figure everyone learns first, and the reason the grid had to
            carry eighths: beat one and the and of two, over and over. */
        CompStyleDefinition charleston;
        charleston.key = "charleston";
        charleston.name = "Charleston";
        charleston.summary = "One, and the and of two - the first comping figure anybody learns, "
                             "and still the most useful.";
        charleston.feel = Subdivision::eighth;
        charleston.slots = {
            CompSlot { 0, 0,                95, false },
            CompSlot { 1, ticksPerBeat / 2, 90, false },
            CompSlot { -1, ticksPerBeat / 2, 30, true }
        };
        charleston.fewestPerBar = 1;
        charleston.mostPerBar = 3;
        charleston.lowestNote = 48;
        charleston.highestNote = 79;
        styles.push_back (charleston);
    }

    {
        /*  A ballad does not comp in eighths at all, which is the case a
            straight-eighth grid could not have written down. The middle
            triplet of a beat is not an eighth and is not a sixteenth. */
        CompStyleDefinition ballad;
        ballad.key = "ballad";
        ballad.name = "Ballad - triplet";
        ballad.summary = "Slow and wide, leaning on the triplet inside the beat rather than "
                         "on eighths.";
        ballad.feel = Subdivision::tripletEighth;
        ballad.slots = {
            CompSlot { 0,  0,                    90, false },
            CompSlot { 2,  0,                    55, false },
            CompSlot { 1,  2 * ticksPerBeat / 3, 30, false },
            CompSlot { -1, 2 * ticksPerBeat / 3, 35, true  }
        };
        ballad.fewestPerBar = 1;
        ballad.mostPerBar = 3;
        ballad.lowestNote = 45;
        ballad.highestNote = 81;
        styles.push_back (ballad);
    }

    return styles;
}

const CompStyleDefinition& compStyleFor (const std::string& key)
{
    static const auto styles = compStyles();

    for (const auto& style : styles)
        if (style.key == key)
            return style;

    return styles.front();
}

std::vector<BarPosition> slotPositions (const CompSlot& slot, int beatsPerBar)
{
    std::vector<BarPosition> positions;

    if (beatsPerBar <= 0 || slot.tick < 0 || slot.tick >= ticksPerBeat)
        return positions;

    if (! slot.beat.has_value())
    {
        for (auto beat = 0; beat < beatsPerBar; ++beat)
            positions.push_back ({ beat, slot.tick });

        return positions;
    }

    // Negative counts back from the end, so -1 is the last beat whatever the
    // metre - which is what makes "the and of four" survive being played in
    // three as "the and of three".
    const auto beat = *slot.beat < 0 ? beatsPerBar + *slot.beat : *slot.beat;

    if (beat >= 0 && beat < beatsPerBar)
        positions.push_back ({ beat, slot.tick });

    return positions;
}

CompPlan compPlan (const Chart& chart, const CompStyleDefinition& style,
                   int fromBar, int toBar, std::uint32_t seed)
{
    CompPlan plan;

    const auto bars = chart.measureCount();

    if (bars == 0)
        return plan;

    const auto first = std::max (0, std::min (fromBar, bars - 1));
    const auto last = std::max (first, std::min (toBar, bars - 1));
    const auto beatsPerBar = std::max (1, chart.timeSignature.numerator);

    // Voice leading runs through the whole plan rather than through each bar,
    // which is why this is planned in one pass: the hit that decides where the
    // hands go for bar five is the one before it, in bar four.
    std::vector<int> previous;

    for (auto measureIndex = first; measureIndex <= last; ++measureIndex)
    {
        // Mixed with the bar rather than carried along, so the same bar of the
        // same loop is planned the same way every time round.
        const auto barSeed = mix (seed, static_cast<std::uint32_t> (measureIndex));

        std::vector<std::pair<BarPosition, bool>> chosen;   // where, and whether it anticipates
        std::vector<std::pair<BarPosition, bool>> offered;  // everything the style could have taken

        std::uint32_t salt = 0;

        for (const auto& slot : style.slots)
        {
            for (const auto& position : slotPositions (slot, beatsPerBar))
            {
                offered.push_back ({ position, slot.anticipates });

                if (roll (barSeed, salt++) < slot.weight)
                    chosen.push_back ({ position, slot.anticipates });
            }
        }

        std::sort (chosen.begin(), chosen.end(),
                   [] (const auto& a, const auto& b) { return a.first < b.first; });
        std::sort (offered.begin(), offered.end(),
                   [] (const auto& a, const auto& b) { return a.first < b.first; });

        // A run of unlucky rolls should not empty a bar the style says is never
        // empty, nor fill one it says is sparse. Topping up takes the slots the
        // style likes most; trimming drops the ones it likes least.
        if (static_cast<int> (chosen.size()) > style.mostPerBar)
            chosen.resize (static_cast<std::size_t> (style.mostPerBar));

        for (const auto& candidate : offered)
        {
            if (static_cast<int> (chosen.size()) >= style.fewestPerBar)
                break;

            const auto already = std::any_of (chosen.begin(), chosen.end(),
                                              [&candidate] (const auto& taken)
                                              { return taken.first == candidate.first; });

            if (! already)
                chosen.push_back (candidate);
        }

        std::sort (chosen.begin(), chosen.end(),
                   [] (const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& [position, anticipates] : chosen)
        {
            // An anticipation is the next bar's chord arriving early. At the
            // end of the range there is no next bar, so it voices this one -
            // a push into silence is just a hit.
            const auto* chord = anticipates ? chordStartingBar (chart, measureIndex + 1)
                                            : chordUnder (chart, measureIndex, position);

            const auto pushed = anticipates && chord != nullptr;

            if (chord == nullptr)
                chord = chordUnder (chart, measureIndex, position);

            if (chord == nullptr)
                continue;

            const auto voicing = compingVoicing (*chord, previous);

            if (voicing.isEmpty())
                continue;

            previous = voicing.midiNotes;

            plan.hits.push_back (CompHit { measureIndex, position, voicing.midiNotes,
                                           chord->toString(), pushed });
        }
    }

    return plan;
}

//==============================================================================
std::string bassRoleName (BassRole role)
{
    switch (role)
    {
        case BassRole::root:      return "root";
        case BassRole::chordTone: return "chord tone";
        case BassRole::scaleTone: return "scale tone";
        case BassRole::approach:  return "approach";
    }

    return "note";
}

namespace
{
    /** The octave of @p pitchClass nearest @p near, inside the bass's range. */
    int bassNoteNear (PitchClass pitchClass, int near)
    {
        auto note = lowestBassNote + toPitchClass (static_cast<int> (pitchClass)
                                                     - toPitchClass (lowestBassNote));

        auto best = note;

        for (; note <= highestBassNote; note += semitonesPerOctave)
            if (std::abs (note - near) < std::abs (best - near))
                best = note;

        return best;
    }

    /** Every chord tone of @p chord playable in the bass's range. */
    std::vector<int> bassChordTones (const ChordSymbol& chord)
    {
        std::vector<int> notes;

        for (const auto& tone : chord.chordTones())
        {
            const auto pitchClass = static_cast<PitchClass> (
                toPitchClass (static_cast<int> (chord.root()) + tone.semitones));

            for (auto note = lowestBassNote
                               + toPitchClass (static_cast<int> (pitchClass)
                                                 - toPitchClass (lowestBassNote));
                 note <= highestBassNote; note += semitonesPerOctave)
                notes.push_back (note);
        }

        std::sort (notes.begin(), notes.end());
        notes.erase (std::unique (notes.begin(), notes.end()), notes.end());

        return notes;
    }

    /** The chord that arrives on each beat of a bar, or nullptr where none does.

        A walking line cares about *changes*, not about which chord is sounding:
        the root goes down when a chord arrives, and the beat before a change is
        where the approach goes. A bar of one chord changes once, at its
        downbeat.
    */
    std::vector<const ChordSymbol*> changesInBar (const Chart& chart, int measureIndex,
                                                  int beatsPerBar)
    {
        std::vector<const ChordSymbol*> arriving (static_cast<std::size_t> (beatsPerBar), nullptr);

        if (measureIndex < 0 || measureIndex >= chart.measureCount())
            return arriving;

        const auto& measure = chart.measures[static_cast<std::size_t> (measureIndex)];
        auto beat = 0;

        for (const auto& slot : measure.slots)
        {
            if (beat < beatsPerBar)
                arriving[static_cast<std::size_t> (beat)] = &slot.chord;

            beat += std::max (1, slot.beats);
        }

        return arriving;
    }
}

std::vector<BassNote> walkingBass (const Chart& chart, int fromBar, int toBar,
                                   std::uint32_t seed)
{
    std::vector<BassNote> line;

    const auto bars = chart.measureCount();

    if (bars == 0)
        return line;

    const auto first = std::max (0, std::min (fromBar, bars - 1));
    const auto last = std::max (first, std::min (toBar, bars - 1));
    const auto beatsPerBar = std::max (1, chart.timeSignature.numerator);

    /*  A walking line is built in *runs* - a chord arriving, then the beats
        before the next one arrives - rather than beat by beat. Written beat by
        beat it has nothing to aim at, and picking the nearest chord tone each
        time walks straight back where it came from: the first version of this
        played D, C, D, D over a bar of Dm7. A run knows where it starts and
        where it has to be by the end, and everything between is travel. */
    struct Beat
    {
        int measureIndex {};
        int beat {};
        const ChordSymbol* arriving {};
        const ChordSymbol* sounding {};
    };

    std::vector<Beat> beats;

    for (auto measureIndex = first; measureIndex <= last; ++measureIndex)
    {
        const auto arriving = changesInBar (chart, measureIndex, beatsPerBar);
        const ChordSymbol* sounding = nullptr;

        for (auto beat = 0; beat < beatsPerBar; ++beat)
        {
            if (arriving[static_cast<std::size_t> (beat)] != nullptr)
                sounding = arriving[static_cast<std::size_t> (beat)];

            if (sounding == nullptr)
                sounding = chordUnder (chart, measureIndex, { beat, 0 });

            beats.push_back ({ measureIndex, beat, arriving[static_cast<std::size_t> (beat)],
                               sounding });
        }
    }

    if (beats.empty())
        return line;

    // Where each run begins. The first beat starts one whether or not a chord
    // is marked as arriving on it - the line has to start somewhere.
    std::vector<std::size_t> runs;

    for (std::size_t i = 0; i < beats.size(); ++i)
        if (i == 0 || beats[i].arriving != nullptr)
            runs.push_back (i);

    auto previous = 40;   // E2: where a bass player's hand starts

    for (std::size_t r = 0; r < runs.size(); ++r)
    {
        const auto start = runs[r];
        const auto end = r + 1 < runs.size() ? runs[r + 1] : beats.size();
        const auto length = end - start;

        const auto* chord = beats[start].sounding;

        if (chord == nullptr)
            continue;

        const auto salt = static_cast<std::uint32_t> (beats[start].measureIndex * 16
                                                        + beats[start].beat);

        // The root, on the beat the chord arrives. The one note the line is not
        // free about, because it is what states the harmony.
        const auto root = bassNoteNear (chord->root(), previous);

        std::vector<int> notes { root };
        std::vector<BassRole> roles { BassRole::root };

        if (length > 1)
        {
            /*  Where the run has to be by its last beat: leading into whatever
                arrives next. A semitone either side is what makes a line sound
                like walking; the fifth above and the fifth below are the other
                two answers every bass player has, and they are the ones that
                land when the chromatic notes are out of the instrument. */
            const auto* nextChord = end < beats.size() ? beats[end].sounding : chord;
            const auto target = bassNoteNear (nextChord->root(), root);

            const std::vector<int> options { target - 1, target + 1, target + 7, target - 5 };
            std::vector<int> usable;

            for (auto option : options)
                if (option >= lowestBassNote && option <= highestBassNote && option != root)
                    usable.push_back (option);

            auto approach = usable.empty() ? root
                                           : usable[static_cast<std::size_t> (roll (seed, salt))
                                                      % usable.size()];

            // Anything more than a sixth from the root is a leap the run has to
            // cross rather than walk, so take a nearer answer when there is one.
            for (auto option : usable)
                if (std::abs (option - root) > 9 && std::abs (approach - root) > 9
                    && std::abs (option - root) < std::abs (approach - root))
                    approach = option;

            // Everything between: chord tones, travelling towards the approach
            // rather than wandering, and never the note just played.
            const auto tones = bassChordTones (*chord);
            auto walker = root;
            auto rising = approach >= root;

            for (std::size_t step = 1; step + 1 < length; ++step)
            {
                auto next = walker;

                for (auto tone : tones)
                    if (rising ? (tone > walker && tone <= approach + 12)
                               : (tone < walker && tone >= approach - 12))
                    {
                        if (next == walker || std::abs (tone - walker) < std::abs (next - walker))
                            next = tone;
                    }

                // Ran out of chord in that direction: turn round rather than
                // repeat the note, which is what a bass player does too.
                if (next == walker)
                {
                    rising = ! rising;

                    for (auto tone : tones)
                        if (rising ? tone > walker : tone < walker)
                            if (next == walker || std::abs (tone - walker) < std::abs (next - walker))
                                next = tone;
                }

                if (next == walker)
                    next = std::max (lowestBassNote,
                                     std::min (highestBassNote, walker + (rising ? 2 : -2)));

                notes.push_back (next);
                roles.push_back (BassRole::chordTone);
                walker = next;
            }

            /*  The run has walked itself onto the approach note: a bass player
                would not play the same note twice to get somewhere it already
                is. Take the nearest of the other answers instead. */
            if (approach == walker)
                for (auto option : usable)
                    if (option != walker
                        && (approach == walker
                            || std::abs (option - walker) < std::abs (approach - walker)))
                        approach = option;

            notes.push_back (approach);
            roles.push_back (BassRole::approach);
        }

        for (std::size_t i = 0; i < length && i < notes.size(); ++i)
        {
            const auto& here = beats[start + i];
            const auto note = std::max (lowestBassNote, std::min (highestBassNote, notes[i]));

            line.push_back ({ here.measureIndex, { here.beat, 0 }, note,
                              chord->toString(), roles[i] });

            previous = note;
        }
    }

    return line;
}

//==============================================================================
bool fitsStyle (const CompHit& hit, const CompStyleDefinition& style, int beatsPerBar)
{
    /*  The position has to be one the style offers. Anticipation is checked
        one way only: a hit that pushed must have come from a slot that pushes,
        but a slot that pushes may honestly produce a hit that did not - the
        last bar of a range has no next chord to pull forward, and that is a
        fact about where the chart ended rather than about the style. */
    for (const auto& slot : style.slots)
        for (const auto& position : slotPositions (slot, beatsPerBar))
            if (position == hit.at && (slot.anticipates || ! hit.anticipation))
                return true;

    return false;
}

} // namespace jazz::core
