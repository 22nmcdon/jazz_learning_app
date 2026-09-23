#include "jazz/core/LineWriter.h"

#include <algorithm>
#include <cstdlib>
#include <string>

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
    int nearestOf (const std::vector<PitchClass>& classes, int from, int direction,
                   int lowest, int highest)
    {
        auto best = -1;
        auto bestDistance = 1000;

        for (auto note = lowest; note <= highest; ++note)
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
    int firstNote (const std::vector<PitchClass>& chordTones, int lowest, int highest)
    {
        const auto middle = (lowest + highest) / 2;
        const auto found = nearestOf (chordTones, middle, 0, lowest, highest);

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

std::vector<PlannedNote> planPhrases (const LineStyleDefinition& style,
                                      int fromBar,
                                      int toBar,
                                      int beatsPerBar,
                                      std::uint32_t seed)
{
    std::vector<PlannedNote> planned;

    if (fromBar < 0 || toBar < fromBar || beatsPerBar < 1)
        return planned;

    const auto step = std::max (1, ticksFor (style.feel));
    const auto barTicks = beatsPerBar * ticksPerBeat;
    const auto total = (toBar - fromBar + 1) * barTicks;

    /*  Which tick of a beat this style likes to begin on, weighted towards the
        first entry. The research's R14 - chromatic pickups on the "and" of
        four into the downbeat, starts on the "and" of one - is a bias rather
        than a rule, so it is a weighting rather than a constraint. */
    const auto startTickFor = [&style] (std::uint32_t r)
    {
        if (style.startTicks.empty())
            return 0;

        if (style.startTicks.size() == 1 || r % 100u < 70u)
            return style.startTicks.front();

        const auto rest = style.startTicks.size() - 1;
        return style.startTicks[1 + (r / 100u) % rest];
    };

    /** The next position at or after @p at that is on the grid and on a tick
        this style starts phrases on. Gives up after a bar and takes the grid. */
    const auto nextStart = [&] (int at, int wanted)
    {
        for (auto tryAt = at; tryAt < at + barTicks; ++tryAt)
            if (tryAt % step == 0 && tryAt % ticksPerBeat == wanted)
                return tryAt;

        return at + (step - at % step) % step;
    };

    std::uint32_t salt = 0;
    auto at = nextStart (0, startTickFor (mix (seed, salt++)));

    while (at < total)
    {
        const auto span = std::max (1, style.longestPhrase - style.shortestPhrase + 1);
        const auto notes = style.shortestPhrase
                         + static_cast<int> (mix (seed, salt++) % static_cast<std::uint32_t> (span));

        const auto firstOfPhrase = planned.size();

        for (auto i = 0; i < notes && at < total; ++i)
        {
            PlannedNote note;
            note.measureIndex = fromBar + at / barTicks;
            note.at = BarPosition::fromTicks (at % barTicks);
            planned.push_back (note);
            at += step;
        }

        if (planned.size() == firstOfPhrase)
            break;

        planned.back().endsPhrase = true;

        const auto restSpan = std::max (1, style.longestRest - style.shortestRest + 1);
        at += style.shortestRest
            + static_cast<int> (mix (seed, salt++) % static_cast<std::uint32_t> (restSpan));

        at = nextStart (at, startTickFor (mix (seed, salt++)));
    }

    return planned;
}

std::vector<LineFinding> lineFaults (const std::vector<WrittenNote>& line,
                                     const LineStyleDefinition& style,
                                     int beatsPerBar)
{
    std::vector<LineFinding> found;

    const auto step = std::max (1, ticksFor (style.feel));
    const auto beats = std::max (1, beatsPerBar);

    const auto note = [] (const WrittenNote& n)
    {
        return " (" + n.chordSymbol + ", bar " + std::to_string (n.measureIndex + 1)
             + ", " + n.at.describe() + ")";
    };

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const auto& written = line[i];

        if (written.midiNote < style.lowestNote || written.midiNote > style.highestNote)
            found.push_back ({ LineFault::outsideTheRegister, i,
                               "outside the style's register" + note (written) });

        if (written.at.tick % step != 0)
            found.push_back ({ LineFault::offTheStyleGrid, i,
                               "not on this style's subdivision" + note (written) });

        if (written.colour != NoteColour::approach)
            continue;

        /*  R1, the half of it that is checkable about one note: a chromatic
            belongs off the beat. Parker's own figures are 11.7% of all notes
            offbeat chromatics against 4.6% on-beat, so this is a strong bias
            in a corpus and a hard rule here - a generator allowed to put one
            on the downbeat will, and it sounds like a mistake rather than
            like colour. */
        if (isStrong (written.at, beats))
            found.push_back ({ LineFault::chromaticOnTheBeat, i,
                               "an approach note on a strong beat" + note (written) });

        /*  R2: an approach that is not followed by a step is not an approach,
            it is a note left hanging - and the analyser will read it as one. */
        const auto moved = i + 1 < line.size()
                               ? std::abs (line[i + 1].midiNote - written.midiNote) : 0;

        // One or two semitones. Zero is not a step - it is the same note again,
        // and the analyser will not promote an approach that did not move.
        const auto resolved = i + 1 < line.size() && moved >= 1 && moved <= 2;

        if (! resolved)
            found.push_back ({ LineFault::approachThatNeverLands, i,
                               "an approach note with nowhere to land" + note (written) });
    }

    return found;
}

std::vector<WrittenNote> improvisedLine (const Chart& chart,
                                         int fromBar,
                                         int toBar,
                                         const std::string& chosenScale,
                                         const std::string& lineStyle,
                                         std::uint32_t seed)
{
    std::vector<WrittenNote> line;

    if (fromBar < 0 || toBar < fromBar || chart.measureCount() == 0)
        return line;

    toBar = std::min (toBar, chart.measureCount() - 1);

    const auto& style = lineStyleFor (lineStyle);

    LineAnalyzer::Options options;
    options.chosenScale = chosenScale;

    // The style's scale vocabulary, not the style's own key - which is what
    // keeps `readingScaleFor` answering the same question here as in a take.
    options.style = style.scaleStyle;
    options.beatsPerBar = std::max (1, chart.timeSignature.numerator);

    const auto planned = planPhrases (style, fromBar, toBar, options.beatsPerBar,
                                      mix (seed, 0x9101u));

    auto previous = -1;
    auto direction = -1;

    /*  Where the approach just written is going.

        An approach note and the note it lands on are one gesture, and picking
        them independently is how the first version of this got it wrong: the
        writer chose a semitone neighbour of a target, then chose the next note
        as "the nearest scale tone", which on a pentatonic can be three
        semitones away. The approach then landed nowhere, the analyser quite
        correctly read it as `outside`, and the round trip refused the line.
        Only the blues style failed, because only it draws its steps from a
        scale with no half steps in it - which is exactly the kind of bug a
        sweep over every style catches and a test of one style does not. */
    auto landingOn = -1;

    for (std::size_t i = 0; i < planned.size(); ++i)
    {
        const auto& slot = planned[i];
        const auto* chord = chart.chordAt (slot.measureIndex);

        if (chord == nullptr)
            continue;

        const auto chordTones = toneClasses (*chord);
        const auto scale = readingScaleFor (*chord, options);
        const auto scaleTones = scale.has_value() ? scale->scale.pitchClasses() : chordTones;
        const auto strong = isStrong (slot.at, options.beatsPerBar);

        WrittenNote note;
        note.measureIndex = slot.measureIndex;
        note.at = slot.at;
        note.chordSymbol = chord->toString();

        /*  The approach: a note a semitone from where the line is going next,
            written only when the chord is about to change under it.

            Three conditions, and each one is a rule from the research. The
            style has to use approaches at all (a modal line does not). It must
            not be on a strong beat, which is R1 and is also what `lineFaults`
            checks. And it has to be **outside the scale the take reads
            against**, or the analyser will quite correctly call it a scale
            tone and the colour written here would be a lie - when neither
            neighbour is outside there is no approach to write, and the line
            plays an ordinary note instead. */
        const auto* comingNext = i + 1 < planned.size()
                                     ? chart.chordAt (planned[i + 1].measureIndex)
                                     : nullptr;

        const auto changes = comingNext != nullptr
                          && planned[i + 1].measureIndex != slot.measureIndex
                          && comingNext->root() != chord->root();

        auto aimedAt = -1;

        /*  A landing owed from the note before wins over starting another
            approach. Without this an approach can follow an approach - both
            of them aimed, neither of them arriving - and the first is left on
            a pitch the line never resolves. It showed up as two approaches on
            the same note in consecutive bars, which the analyser reads as
            `outside` because nothing moved. */
        if (landingOn > 0)
        {
            note.midiNote = landingOn;
            note.colour = holds (chordTones, landingOn) ? NoteColour::chordTone
                                                        : NoteColour::scaleTone;
        }

        if (note.midiNote == 0 && style.usesApproaches && changes && ! strong && previous > 0)
        {
            const auto nextTones = toneClasses (*comingNext);
            const auto target = nearestOf (nextTones, previous, direction,
                                           style.lowestNote, style.highestNote);

            if (target > 0)
            {
                for (const auto stepBy : { -1, 1 })
                {
                    const auto candidate = target + stepBy;

                    if (candidate < style.lowestNote || candidate > style.highestNote) continue;
                    if (holds (scaleTones, candidate) || holds (chordTones, candidate)) continue;

                    note.midiNote = candidate;
                    note.colour = NoteColour::approach;
                    aimedAt = target;
                    break;
                }
            }
        }

        if (note.midiNote == 0)
        {
            const auto& from = strong ? chordTones : scaleTones;
            const auto picked = previous > 0
                                    ? nearestOf (from, previous, direction,
                                                 style.lowestNote, style.highestNote)
                                    : firstNote (chordTones, style.lowestNote, style.highestNote);

            note.midiNote = picked > 0 ? picked
                                       : firstNote (chordTones, style.lowestNote, style.highestNote);
            note.colour = holds (chordTones, note.midiNote) ? NoteColour::chordTone
                                                            : NoteColour::scaleTone;
        }

        landingOn = note.colour == NoteColour::approach ? aimedAt : -1;

        if (previous > 0 && note.midiNote != previous)
            direction = note.midiNote > previous ? 1 : -1;

        /*  The style's descending bias (R9), applied where a phrase begins
            rather than note by note. Parker descends 54% of the time, but a
            coin flipped at every note is a line that shivers; turning the
            phrase over at its start is what gives it a shape. */
        if (slot.endsPhrase)
            direction = roll (seed, static_cast<std::uint32_t> (i) + 0x51edu) < style.descending
                            ? -1 : 1;

        // Turned round at the edges rather than clamped: a line that piled
        // up against the top of its register would repeat a note instead of
        // coming back down, which is not a phrase.
        if (note.midiNote >= style.highestNote - 2) direction = -1;
        if (note.midiNote <= style.lowestNote + 2)  direction = 1;

        previous = note.midiNote;
        line.push_back (note);
    }

    return line;
}

} // namespace jazz::core
