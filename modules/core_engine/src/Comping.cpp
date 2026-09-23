#include "jazz/core/Comping.h"

#include <algorithm>
#include <map>

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

    /** One place the band might put a chord in a bar, and how much it wants to.

        The weight is carried rather than looked up again because it comes from
        two places now - a slot's own, and `variation` for the rest of the feel's
        vocabulary - and both ends of the density clamp read it.
    */
    struct Candidate
    {
        BarPosition at;
        bool anticipates;
        int weight;
    };

    /*  What a comp's fit is made of.

        Placement carries twice the weight of the other two because placement is
        what a comping style *is*: a comper in the right register playing the
        wrong figure is not comping in that style, while a comper playing the
        right figure a little low still is. */
    constexpr int placementWeight = 2;
    constexpr int registerWeight  = 1;
    constexpr int densityWeight   = 1;

    /*  A bar one hit outside what the style plays is a bar that got a little
        away from the player; four hits outside is a different style. Linear,
        and steep enough that the second reading is not called the first. */
    constexpr int densityPointsPerExtraHit = 25;

    /** How many places this style has to put a chord in a bar of this metre.

        `fewestPerBar` and `mostPerBar` are plain counts, and a count does not
        survive a change of metre the way a slot does: four-to-the-bar is four
        chords in four and three in three, and holding a waltz to the written
        four marks the generator's own comp sparse in every bar. So the density
        window is clamped to what the style actually has room for here - the
        same move `slotPositions` makes when it returns nothing for a beat the
        metre has not got. A style has less to say in a metre it was not written
        for, and asking it for chords it has nowhere to put is not a reading of
        the player.
    */
    int positionsOffered (const CompStyleDefinition& style, int beatsPerBar)
    {
        std::vector<BarPosition> positions;

        for (const auto& slot : style.slots)
            for (const auto& position : slotPositions (slot, beatsPerBar))
                if (std::none_of (positions.begin(), positions.end(),
                                  [&position] (const BarPosition& seen) { return seen == position; }))
                    positions.push_back (position);

        return static_cast<int> (positions.size());
    }

    std::string countOf (int n, const std::string& singular, const std::string& plural)
    {
        return std::to_string (n) + " " + (n == 1 ? singular : plural);
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
        four.slots = { CompSlot { std::nullopt, 0, 100, false, std::nullopt } };

        // Damped, and well short of the beat it sits on. Freddie Green's part
        // is a chunk rather than a chord: held for its full beat it stops being
        // a pulse and becomes an organ.
        four.heldFor = ticksPerBeat / 2;
        four.fewestPerBar = 4;
        four.mostPerBar = 8;
        four.lowestNote = 45;
        four.highestNote = 76;

        // No variation, and its feel is the beat rather than the eighth: four to
        // the bar is exactly four to the bar, and an "and" in it is not this
        // style being played loosely, it is a different style.
        four.variation = 0;
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
        /*  The style where the duration is not one number. A punch is a punch -
            short, and the silence after it is the point - but the bar-end push
            is carrying the next chord in, and a chord stating a new harmony
            has to last long enough to be heard as one. */
        basie.slots = {
            CompSlot { -1,           ticksPerBeat / 2, 75, true,  ticksPerBeat * 3 / 2 },
            CompSlot { 0,            0,                25, false, std::nullopt },
            CompSlot { 1,            ticksPerBeat / 2, 20, false, std::nullopt }
        };

        basie.heldFor = ticksPerBeat / 2;
        basie.fewestPerBar = 0;
        basie.mostPerBar = 2;
        basie.lowestNote = 48;
        basie.highestNote = 79;

        /*  Under its lightest slot (20), on purpose. The trim takes the heaviest
            candidates, so a variation weight above the style's own figure would
            quietly replace the figure with the vocabulary - the band would stop
            sounding like the style it was asked for. */
        basie.variation = 15;
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
        /*  One is short and the and of two rings - which is what makes the
            figure sound like the figure rather than like two even stabs. The
            second chord has the back half of the bar to itself and takes it. */
        charleston.slots = {
            CompSlot { 0,  0,                95, false, ticksPerBeat / 2 },
            CompSlot { 1,  ticksPerBeat / 2, 90, false, ticksPerBeat * 3 / 2 },
            CompSlot { -1, ticksPerBeat / 2, 30, true,  ticksPerBeat }
        };
        // Nought, so the band can leave a bar alone the way a player does. Its
        // two main slots fire almost always, so an empty bar stays rare.
        charleston.fewestPerBar = 0;
        charleston.mostPerBar = 3;
        charleston.lowestNote = 48;
        charleston.highestNote = 79;
        charleston.variation = 20;
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
            CompSlot { 0,  0,                    90, false, std::nullopt },
            CompSlot { 2,  0,                    55, false, std::nullopt },
            CompSlot { 1,  2 * ticksPerBeat / 3, 30, false, std::nullopt },
            CompSlot { -1, 2 * ticksPerBeat / 3, 35, true,  std::nullopt }
        };

        // The other end of the range from a punch, and the whole reason this
        // field exists: a ballad's chords are held, and one stabbed at the
        // length Basie uses is a ballad played like a swing tune.
        ballad.heldFor = ticksPerBeat * 2;
        ballad.fewestPerBar = 0;
        ballad.mostPerBar = 3;
        ballad.lowestNote = 45;
        ballad.highestNote = 81;

        // Its vocabulary is the triplet, so varying means the other two notes of
        // the beat - never a straight eighth, which is what makes playing this
        // like a swing tune something the reading can actually say.
        ballad.variation = 20;
        styles.push_back (ballad);
    }

    return styles;
}

int heldForSlot (const CompSlot& slot, const CompStyleDefinition& style)
{
    // At least a tick: a chord that rings for no time is not a chord, and a
    // style written with a zero in it should be heard rather than be silent.
    return std::max (1, slot.heldFor.value_or (style.heldFor));
}

const CompStyleDefinition& compStyleFor (std::string_view key)
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

        std::vector<Candidate> chosen;    // what this bar came out as
        std::vector<Candidate> offered;   // everything the style could have taken

        std::uint32_t salt = 0;

        // The style's own figure, at its own weights.
        for (const auto& slot : style.slots)
        {
            for (const auto& position : slotPositions (slot, beatsPerBar))
            {
                offered.push_back ({ position, slot.anticipates, slot.weight });

                if (roll (barSeed, salt++) < slot.weight)
                    chosen.push_back ({ position, slot.anticipates, slot.weight });
            }
        }

        /*  And the rest of the feel's vocabulary, at `variation`. A style is a
            characteristic figure rather than the only thing a player of it ever
            plays, which is the rule the evaluator reads - and a band held to its
            slots alone repeats itself: the Charleston's three slots at 95, 90
            and 30 produce nearly the same two chords every bar.

            Never anticipating. Stating the next chord early is the figure doing
            something particular, and `fitsStyle` holds the band to its own
            pushing slots for it. */
        if (style.variation > 0)
        {
            const auto step = ticksFor (style.feel);

            for (auto beat = 0; beat < beatsPerBar; ++beat)
            {
                for (auto tick = 0; tick < ticksPerBeat; tick += step)
                {
                    const BarPosition at { beat, tick };

                    if (slotAt (at, style, beatsPerBar) != nullptr)
                        continue;   // already offered, at the figure's own weight

                    offered.push_back ({ at, false, style.variation });

                    if (roll (barSeed, salt++) < style.variation)
                        chosen.push_back ({ at, false, style.variation });
                }
            }
        }

        /*  A run of unlucky rolls should not empty a bar the style says is never
            empty, nor fill one it says is sparse. Both ends work by weight: the
            trim drops what the style likes least and the top-up takes what it
            likes most.

            By weight rather than by position, which is what this did before the
            vocabulary was on offer - sorting by position and resizing kept the
            *earliest* hits, so a trimmed bar was always front-loaded. Stable, so
            equal weights keep the order they were offered in and the plan stays
            the same everywhere; `std::sort` would not promise that. */
        const auto byWeight = [] (const Candidate& a, const Candidate& b)
                              { return a.weight > b.weight; };

        std::stable_sort (chosen.begin(), chosen.end(), byWeight);
        std::stable_sort (offered.begin(), offered.end(), byWeight);

        /*  Floored at nothing, because a `std::size_t` cast of a negative is
            not a small number - it is about eighteen quintillion, and `resize`
            throws rather than trimming. Unreachable while the catalogue was
            the only thing that could hand this function a style; reachable the
            moment one can be described from outside, and a plain struct field
            should not be able to throw whoever fills it in.

            A no-op for every style that ships - all four have a `mostPerBar`
            of at least 2 - so nothing that worked comps differently by a
            single tick. There is a test that says so. */
        const auto ceiling = static_cast<std::size_t> (std::max (0, style.mostPerBar));

        if (chosen.size() > ceiling)
            chosen.resize (ceiling);

        for (const auto& candidate : offered)
        {
            if (static_cast<int> (chosen.size()) >= style.fewestPerBar)
                break;

            const auto already = std::any_of (chosen.begin(), chosen.end(),
                                              [&candidate] (const Candidate& taken)
                                              { return taken.at == candidate.at; });

            if (! already)
                chosen.push_back (candidate);
        }

        std::sort (chosen.begin(), chosen.end(),
                   [] (const Candidate& a, const Candidate& b) { return a.at < b.at; });

        std::uint32_t voicingSalt = 0;

        for (const auto& [position, anticipates, weight] : chosen)
        {
            (void) weight;

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

            // In the style's own register, not this file's: the evaluator marks
            // a player against `lowestNote`/`highestNote`, so the band has to be
            // held to the same two numbers or the app comps in a style it would
            // then read as out of that style's register.
            /*  Seeded per hit, not per bar: two chords in one bar should not be
                offered the same choice and then differ only by where the voice
                leading took them. The bar's own seed is already mixed with the
                plan's, so a bar comes round the same way on the same seed and
                a *different* one when the caller asks for a different seed -
                which is how a second chorus is a second chorus rather than a
                repeat. */
            const auto voicing = compingVoicing (*chord, previous,
                                                 style.lowestNote, style.highestNote,
                                                 mix (barSeed, voicingSalt++));

            if (voicing.isEmpty())
                continue;

            previous = voicing.midiNotes;

            const auto* slot = slotAt (position, style, beatsPerBar);

            plan.hits.push_back (CompHit { measureIndex, position, voicing.midiNotes,
                                           chord->toString(), pushed,
                                           slot != nullptr ? heldForSlot (*slot, style)
                                                           : std::max (1, style.heldFor) });
        }
    }

    /*  Nothing rings into the chord after it.

        One instrument plays these in order, so a duration past the next onset
        is a length nothing could sound: a shell would stop the voicing there to
        play the next one whatever this said. Trimmed here rather than left to
        each shell, because a number the shell has to correct is two opinions
        about one thing - and the browser and the app would eventually hold
        different ones.

        Absolute ticks, so a hit at the end of a bar is measured against the
        next bar's downbeat rather than against a beat number that starts over.
        A chart has one metre, so one bar's worth of ticks is every bar's. */
    const auto atTicks = [beatsPerBar] (const CompHit& hit)
    {
        return hit.measureIndex * beatsPerBar * ticksPerBeat + hit.at.inTicks();
    };

    for (std::size_t i = 0; i + 1 < plan.hits.size(); ++i)
        plan.hits[i].heldFor = std::max (1, std::min (plan.hits[i].heldFor,
                                                      atTicks (plan.hits[i + 1])
                                                        - atTicks (plan.hits[i])));

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
const CompSlot* slotAt (BarPosition at, const CompStyleDefinition& style, int beatsPerBar)
{
    for (const auto& slot : style.slots)
        for (const auto& position : slotPositions (slot, beatsPerBar))
            if (position == at)
                return &slot;

    return nullptr;
}

bool fitsStyle (const CompHit& hit, const CompStyleDefinition& style, int beatsPerBar)
{
    const auto* slot = slotAt (hit.at, style, beatsPerBar);

    /*  Anticipation is checked one way only: a hit that pushed must have come
        from a slot that pushes, but a slot that pushes may honestly produce a
        hit that did not - the last bar of a range has no next chord to pull
        forward, and that is a fact about where the chart ended rather than
        about the style.

        A push is the band stating the next chord early, which is the style's
        own figure doing something particular. So it is held to the slots even
        though everything else here is held to the vocabulary. */
    if (hit.anticipation)
        return slot != nullptr && slot->anticipates;

    if (slot != nullptr)
        return true;

    /*  The vocabulary, and only inside the bar. `onTheGrid` is a question about
        a tick and knows nothing of the metre, so a position past the end of the
        bar would otherwise pass it on the strength of its tick alone - and a
        style written around a fourth beat, played in three, would come back
        fitting a beat that does not exist. */
    return hit.at.beat >= 0 && hit.at.beat < beatsPerBar && onTheGrid (hit.at, style.feel);
}


//==============================================================================
std::string hitPlacementName (HitPlacement placement)
{
    switch (placement)
    {
        case HitPlacement::theFigure: return "figure";
        case HitPlacement::idiomatic: return "idiomatic";
        case HitPlacement::offStyle:  return "offStyle";
        case HitPlacement::unplaced:  break;
    }

    return "unplaced";
}

PlayedHit inItsOwnBar (PlayedHit hit, int beatsPerBar)
{
    if (beatsPerBar <= 0 || ! hit.at.has_value())
        return hit;

    auto at = *hit.at;

    while (at.beat >= beatsPerBar)
    {
        at.beat -= beatsPerBar;
        hit.measureIndex += 1;
    }

    while (at.beat < 0)
    {
        at.beat += beatsPerBar;
        hit.measureIndex -= 1;
    }

    hit.at = at;

    return hit;
}

CompHitReading readCompHit (const Chart& chart, const CompStyleDefinition& style,
                            const PlayedHit& played, int hitsAlreadyInBar)
{
    const auto beatsPerBar = std::max (1, chart.timeSignature.numerator);
    const auto hit = inItsOwnBar (played, beatsPerBar);
    const auto voicing = Voicing::fromNotes (hit.midiNotes);

    CompHitReading reading;
    reading.measureIndex = hit.measureIndex;
    reading.at = hit.at;
    reading.oneTooMany = hitsAlreadyInBar >= style.mostPerBar;

    // The register the style comps in, at last read by something. The excursion
    // is kept as well as the verdict, so a shell can say "a semitone low"
    // rather than only "out of register".
    if (! voicing.isEmpty())
    {
        const auto below = std::max (0, style.lowestNote - voicing.lowestNote());
        const auto above = std::max (0, voicing.highestNote() - style.highestNote);

        reading.outsideRegisterBy = std::max (below, above);
        reading.inRegister = reading.outsideRegisterBy == 0;
    }

    const auto* slot = hit.at.has_value() ? slotAt (*hit.at, style, beatsPerBar) : nullptr;
    const auto* sounding = chordUnder (chart, hit.measureIndex,
                                       hit.at.value_or (BarPosition {}));
    const auto* next = chordStartingBar (chart, hit.measureIndex + 1);
    const auto* chord = sounding;

    /*  Whether this was a push is decided from the notes, because that is the
        only evidence there is: a player does not declare an anticipation, they
        play the next chord early.

        Asked of anything in the bar's **last beat**, not only of a slot that
        anticipates. A player leaning into the next chord from the and of three
        is pushing whether or not the style lists that position - the slots are
        the band's figure, and reading a player by them was the mistake this
        whole tier system exists to undo. Earlier in the bar it is not asked at
        all: a chord early on the downbeat is a chord in the wrong bar.

        A tie is not a push. Over a bar repeating its chord the two readings are
        identical, and calling that an anticipation would be inventing intent -
        the mirror of `fitsStyle`'s one-way asymmetry, seen from the player's
        side. */
    const auto inTheLastBeat = hit.at.has_value() && hit.at->beat == beatsPerBar - 1;

    if (inTheLastBeat && next != nullptr && ! voicing.isEmpty())
    {
        const VoicingAnalyzer analyzer;
        const auto there = analyzer.analyse (voicing, *next);
        const auto here = sounding != nullptr ? analyzer.analyse (voicing, *sounding).score : -1;

        if (there.score > here)
        {
            chord = next;
            reading.anticipation = true;
        }
    }

    // The style's own figure, then its vocabulary, then outside. Anticipation
    // is a separate fact about the hit and does not decide which of these it is.
    if (! hit.at.has_value())                    reading.placement = HitPlacement::unplaced;
    else if (slot != nullptr)                    reading.placement = HitPlacement::theFigure;
    else if (onTheGrid (*hit.at, style.feel))    reading.placement = HitPlacement::idiomatic;
    else                                         reading.placement = HitPlacement::offStyle;

    if (chord != nullptr)
    {
        reading.chordSymbol = chord->toString();

        if (! voicing.isEmpty())
        {
            reading.voicing = VoicingAnalyzer {}.analyse (voicing, *chord);

            /*  Not `VoicingAnalyzer`'s question. Asking it as one - through
                `practiseType` - would take points off the voicing's own score
                for a reason belonging to the comp rather than to the symbol,
                and would call a shell voicing the wrong shape when it is
                perfectly good comping. The fault is not the shape, it is that
                somebody else is already playing that note. */
            reading.rootAnywhere = voicing.containsPitchClass (chord->root());
            reading.takesTheBassNote = toPitchClass (voicing.lowestNote()) == chord->root();
        }
    }

    const auto where = reading.at.has_value() ? reading.at->describe() : std::string();

    if (reading.anticipation)
    {
        reading.summary = where + " - pushed into " + reading.chordSymbol + ".";
    }
    else
    {
        switch (reading.placement)
        {
            case HitPlacement::theFigure:
                reading.summary = where + " - this style's own figure.";
                break;

            case HitPlacement::idiomatic:
                reading.summary = where + " - in the style's vocabulary, "
                                          "though not its own figure.";
                break;

            case HitPlacement::offStyle:
                reading.summary = where + " - off the grid this style is counted in.";
                break;

            case HitPlacement::unplaced:
                reading.summary = "Nothing is counting, so this is read for its notes "
                                  "and not for where it fell.";
                break;
        }
    }

    if (! reading.inRegister && ! voicing.isEmpty())
        reading.summary += " It sits " + countOf (reading.outsideRegisterBy, "semitone", "semitones")
                         + (voicing.highestNote() > style.highestNote ? " above" : " below")
                         + " the register this style comps in.";

    if (reading.takesTheBassNote)
        reading.summary += " The " + pitchClassName (toPitchClass (voicing.lowestNote()))
                         + " underneath is the bass player's note.";

    if (reading.oneTooMany)
        reading.summary += " That is one more chord than this style puts in a bar.";

    return reading;
}

//==============================================================================
CompEvaluation evaluateComp (const Chart& chart, const CompStyleDefinition& style,
                             const std::vector<PlayedHit>& hits, int fromBar, int toBar)
{
    CompEvaluation out;

    const auto bars = chart.measureCount();
    const auto beatsPerBar = std::max (1, chart.timeSignature.numerator);

    // Counted as they are read, so each hit is told how many were already in
    // its bar - and counted in the bar the hands *played* it, anticipations
    // included, because that is how the generator trims and tops up.
    std::map<int, int> perBar;

    for (const auto& played : hits)
    {
        const auto placed = inItsOwnBar (played, beatsPerBar);
        const auto already = perBar[placed.measureIndex]++;

        out.hits.push_back (readCompHit (chart, style, played, already));
    }

    if (bars > 0 && toBar >= fromBar)
    {
        const auto first = std::max (0, std::min (fromBar, bars - 1));
        const auto last = std::max (first, std::min (toBar, bars - 1));

        const auto room = positionsOffered (style, beatsPerBar);
        const auto fewest = std::min (style.fewestPerBar, room);
        const auto most = std::min (style.mostPerBar, room);

        for (auto measureIndex = first; measureIndex <= last; ++measureIndex)
        {
            const auto found = perBar.find (measureIndex);
            const auto played = found != perBar.end() ? found->second : 0;

            out.bars.push_back (CompBarReading { measureIndex, played, fewest, most,
                                                 played > most });
        }
    }

    auto positioned = 0;
    auto sounded = 0;
    auto inRegister = 0;
    auto analysed = 0;
    auto voicingTotal = 0;

    for (std::size_t i = 0; i < out.hits.size(); ++i)
    {
        const auto& reading = out.hits[i];

        // A hit that carried no notes is not a chord anybody played, so it is
        // counted for its placement and left out of everything about notes.
        const auto struck = ! hits[i].midiNotes.empty();

        if (reading.at.has_value())
            ++positioned;

        switch (reading.placement)
        {
            case HitPlacement::theFigure: ++out.hitsOnTheFigure; break;
            case HitPlacement::idiomatic: ++out.hitsIdiomatic; break;
            case HitPlacement::offStyle:  ++out.hitsOffStyle; break;
            case HitPlacement::unplaced:  break;
        }

        // Counted beside the tiers rather than instead of one, because a push
        // can come from the figure or from anywhere else in the vocabulary.
        if (reading.anticipation)
            ++out.hitsPushed;

        if (reading.takesTheBassNote)
            ++out.hitsTakingTheBassNote;

        if (! struck)
            continue;

        ++sounded;

        if (reading.inRegister)
            ++inRegister;

        if (! reading.chordSymbol.empty())
        {
            ++analysed;
            voicingTotal += reading.voicing.score;
        }
    }

    /*  The figure and the vocabulary score the same. A comper who never plays
        the style's literal figure but lands everything on the feel's grid is
        comping in that style, and marking them down for varying was the bug
        this reading was rewritten to fix. What the figure contributed is said
        in words instead. */
    if (positioned > 0)
        out.placementFit = 100 * (out.hitsOnTheFigure + out.hitsIdiomatic) / positioned;

    if (sounded > 0)
        out.registerFit = 100 * inRegister / sounded;

    if (! out.bars.empty())
    {
        auto total = 0;

        // The busy direction only - see `CompBarReading::tooBusy`. A bar left
        // alone is one of the most idiomatic things a comper does.
        for (const auto& bar : out.bars)
        {
            const auto over = std::max (0, bar.hits - bar.most);

            total += std::max (0, 100 - densityPointsPerExtraHit * over);
        }

        out.densityFit = total / static_cast<int> (out.bars.size());
    }

    if (analysed > 0)
        out.voicingScore = voicingTotal / analysed;

    /*  Empty rather than zero. Nothing played is not nought out of a hundred,
        and neither is a comp nothing was counting behind: placement is two of
        the four parts of this, so without it there is no fit to report. */
    if (! out.hits.empty() && positioned > 0)
        out.fit = std::max (0, std::min (100, (out.placementFit * placementWeight
                                                 + out.registerFit * registerWeight
                                                 + out.densityFit * densityWeight)
                                              / (placementWeight + registerWeight + densityWeight)));

    out.summary = out.hits.empty()
                    ? "Nothing played, so there is nothing to read."
                    : style.name + " - " + countOf (static_cast<int> (out.hits.size()), "chord", "chords")
                        + " over " + countOf (static_cast<int> (out.bars.size()), "bar", "bars") + ".";

    if (! out.hits.empty() && positioned == 0)
        out.summary = style.name + " - read for the notes. Nothing was counting, "
                      "so there is no placing them.";

    //  The words half. Everything true of the take that the style does not pin
    //  down, and everything it does pin down that a number alone does not say.
    auto busy = 0;
    auto quiet = 0;

    for (const auto& bar : out.bars)
    {
        if (bar.tooBusy)              ++busy;
        if (bar.hits < bar.fewest)    ++quiet;
    }

    // Quoting the bars' own numbers rather than the style's, because in a metre
    // the style was not written for those are not the same - see the note on
    // `CompBarReading::fewest`.
    if (busy > 0)
        out.observations.push_back (countOf (busy, "bar", "bars") + " had more chords in "
                                    + (busy == 1 ? "it" : "them") + " than this style plays - it goes up to "
                                    + countOf (out.bars.front().most, "chord", "chords") + " a bar.");

    /*  Said, and never scored - and only of a style whose floor is genuinely
        dense, which is four-to-the-bar alone. Leaving space is what a comper
        does, so a Charleston with empty bars in it hears nothing about them;
        but four to the bar means four to the bar, and a take that goes quiet
        under it is not doing the thing it asked to practise. */
    if (quiet > 0 && ! out.bars.empty() && out.bars.front().fewest >= 3)
        out.observations.push_back (countOf (quiet, "bar", "bars") + " had fewer than the "
                                    + countOf (out.bars.front().fewest, "chord", "chords")
                                    + " a bar this style puts down. Not a fault - but it is the "
                                      "one thing four to the bar is.");

    if (out.hitsOffStyle > 0)
        out.observations.push_back (countOf (out.hitsOffStyle, "chord", "chords")
                                    + " landed off the grid this style is counted in.");

    /*  What the figure contributed, which is the thing the number deliberately
        stopped saying. Only worth a line when the two genuinely differ - a take
        that was all figure or all vocabulary says so on its own. */
    if (out.hitsOnTheFigure > 0 && out.hitsIdiomatic > 0)
        out.observations.push_back ("You played this style's own figure "
                                    + countOf (out.hitsOnTheFigure, "time", "times")
                                    + "; the other "
                                    + countOf (out.hitsIdiomatic, "chord", "chords")
                                    + " were your own, and in the style either way.");

    if (out.hitsPushed > 0)
        out.observations.push_back ("You pushed " + countOf (out.hitsPushed, "chord", "chords")
                                    + " across the barline, which is what this style is for.");

    /*  Only worth saying of a style the push is the *figure* of. Three of the
        four have an anticipating slot and in two of them it is an occasional
        colour - Charleston pushes at 30 against a downbeat at 95 - so "you
        never pushed" would be advice to play a Charleston like a Basie. The
        test is whether the push is the heaviest thing the style does, which is
        true of Basie alone and is what its own summary says about it. */
    const auto heaviest = std::max_element (style.slots.begin(), style.slots.end(),
                                            [] (const CompSlot& a, const CompSlot& b)
                                            { return a.weight < b.weight; });

    const auto builtOnThePush = heaviest != style.slots.end() && heaviest->anticipates;

    if (builtOnThePush && out.hitsPushed == 0 && positioned > 0)
        out.observations.push_back ("Nothing came across a barline. The push is this style's "
                                    "own figure, so that is the one to reach for next.");

    auto outOfRegister = sounded - inRegister;

    if (outOfRegister > 0)
        out.observations.push_back (countOf (outOfRegister, "chord", "chords")
                                    + " sat outside the register this style comps in.");

    if (out.hitsTakingTheBassNote > 0)
        out.observations.push_back (countOf (out.hitsTakingTheBassNote, "chord", "chords")
                                    + " had the root underneath - that is the bass player's note, "
                                    "and a comper's hands are free without it.");

    return out;
}

} // namespace jazz::core
