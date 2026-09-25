#include "jazz/core/LineWriter.h"

#include "jazz/core/LickCatalogue.h"

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

namespace
{
    /** A phrase of the plan, as a half-open range of slots. */
    struct PlannedPhrase
    {
        std::size_t first {};
        std::size_t last {};   ///< inclusive - the slot carrying `endsPhrase`
    };

    std::vector<PlannedPhrase> phrasesIn (const std::vector<PlannedNote>& planned)
    {
        std::vector<PlannedPhrase> phrases;
        auto first = std::size_t { 0 };

        for (std::size_t i = 0; i < planned.size(); ++i)
            if (planned[i].endsPhrase || i + 1 == planned.size())
            {
                phrases.push_back ({ first, i });
                first = i + 1;
            }

        return phrases;
    }

    /** How far a lick's notes reach either side of the tick it starts on.

        Two numbers rather than a span, because a lick that leads in from the
        bar before reaches *backwards*: a caller placing one needs to know how
        far back it goes as well as how far on.

        Both are **onsets**, and that is the whole of why the pair is not a
        duration. The reader calls a gap a phrase boundary by counting onset to
        onset, so measuring the end by where the last note stops sounding
        quietly subtracts that note's own length from every comparison - eight
        ticks, for a triplet, which turned a 24-tick rule into a 20-tick one.
    */
    struct LickReach
    {
        int first {};       ///< the earliest onset, negative for a pickup
        int lastOnset {};   ///< where the last note is struck
    };

    LickReach reachOf (const LickDefinition& lick)
    {
        LickReach reach { 0, 0 };

        for (std::size_t i = 0; i < lick.notes.size(); ++i)
        {
            const auto& note = lick.notes[i];

            reach.first = i == 0 ? note.tick : std::min (reach.first, note.tick);
            reach.lastOnset = i == 0 ? note.tick : std::max (reach.lastOnset, note.tick);
        }

        return reach;
    }

    /** Where to put the lick's first chord's root, in a real octave.

        A degree gives a pitch class and a pitch class cannot be played - the
        point `voicingFromShape` makes one dimension down. So the octave is
        chosen here: the highest one that keeps every note of the lick inside
        the style's register, and among those the one starting nearest the note
        the line is already on, so a quote does not arrive from nowhere.

        Zero when no octave fits, which is a real answer - a lick spanning more
        than the style's register cannot be played in it, and the caller writes
        the phrase itself instead.
    */
    int placeLick (const LickDefinition& lick, const LickMatch& match,
                   const LineStyleDefinition& style, int previous)
    {
        auto best = 0;
        auto bestDistance = 0;

        for (auto octave = 0; octave < 11; ++octave)
        {
            const auto root = match.rootPitchClass + 12 * octave;

            auto low = 0;
            auto high = 0;

            for (std::size_t i = 0; i < lick.notes.size(); ++i)
            {
                const auto sounded = lick.midiFor (lick.notes[i], root);

                low = i == 0 ? sounded : std::min (low, sounded);
                high = i == 0 ? sounded : std::max (high, sounded);
            }

            if (low < style.lowestNote || high > style.highestNote)
                continue;

            /*  Nearest the note the line is already on, so a quote does not
                arrive from nowhere - and, when there is no such note, nearest
                the middle of the register rather than wherever the octave
                loop happens to reach first. Without the second half every
                lick opening a line sat at the bottom of the register, because
                every octave tied at a distance of nothing and the lowest one
                won. */
            const auto from = lick.midiFor (lick.notes.front(), root);
            const auto towards = previous > 0 ? previous
                                              : (style.lowestNote + style.highestNote) / 2;
            const auto distance = std::abs (from - towards);

            if (best == 0 || distance < bestDistance)
            {
                best = root;
                bestDistance = distance;
            }
        }

        return best;
    }
/** Which phrases are quoted, and which lick from where.

    One entry per phrase, in the planner's order; a null `lick` means that
    phrase is written note by note instead. Between the phrase planner and the
    note chooser, because a phrase is either quoted whole or written whole -
    never half of each, since half a documented figure is not that figure.

    Which one is a weighted draw against `lickShare`, and a phrase with nothing
    fitting it simply generates, which is the whole of what the research means
    by "atom fallback".
*/
std::vector<LickMatch> chooseQuotes (const Chart& chart,
                                     const LineStyleDefinition& style,
                                     const std::vector<PlannedNote>& planned,
                                     const std::vector<PlannedPhrase>& phrases,
                                     int fromBar,
                                     int toBar,
                                     int beatsPerBar,
                                     std::uint32_t seed)
{
    std::vector<LickMatch> quoted (phrases.size());

    const auto matches = licksFitting (chart, style, fromBar, toBar);

    if (matches.empty())
        return quoted;

    const auto barTicks = beatsPerBar * ticksPerBeat;
    const auto total = (toBar - fromBar + 1) * barTicks;

    const auto tickOf = [barTicks, fromBar] (const PlannedNote& slot)
    {
        return (slot.measureIndex - fromBar) * barTicks + slot.at.inTicks();
    };

    /*  A rest either side of a quote, measured **exactly** the way the reader
        measures one: `readLinePlacement` calls a gap a phrase boundary when it
        is at least one step of the grid plus the style's shortest rest,
        counted onset to onset. Anything less and the quote reads back joined
        to its neighbour, and the line stops breathing where the quote lands.

        Onset to onset is the part that took two goes. Measuring the end of the
        lick by where its last note stops *sounding* quietly subtracts that
        note's own length from the gap - eight ticks, for a triplet, which
        turned a 24-tick rule into a 20-tick one. */
    const auto phraseGap = std::max (1, ticksFor (style.feel))
                         + std::max (1, style.shortestRest);

    /*  Where the phrase before this one actually ended, which is not always
        where the planner put it: a quoted phrase is the lick's length, not the
        slot's. Carried through the loop because the check below is about the
        gap between two *real* phrases - two quotes landing back to back with
        eight ticks between them read as one phrase of twenty-four notes, which
        is what a first version of this shipped. */
    auto previousLastOnset = 0;
    auto havePrevious = false;

    for (std::size_t p = 0; p < phrases.size(); ++p)
    {
        const auto plannedLastOnset = tickOf (planned[phrases[p].last]);

        const auto remember = [&] (int lastOnset)
        {
            previousLastOnset = lastOnset;
            havePrevious = true;
        };

        const auto salt = static_cast<std::uint32_t> (p) + 0x11c7u;

        if (roll (seed, salt) >= style.lickShare)
        {
            remember (plannedLastOnset);
            continue;
        }

        /*  The room this phrase has: after the phrase before it has finished
            sounding, and before the one after it arrives. A lick is placed
            where the *harmony* says it goes rather than where the phrase
            happened to begin - `licksFitting` returns chord boundaries - so
            what the phrase contributes is this territory, and the lick has to
            fit inside it, pickup and all, or the next phrase would be played
            over the top of it. */
        const auto from = havePrevious ? previousLastOnset + phraseGap : 0;

        const auto to = phrases[p].last + 1 < planned.size()
                            ? tickOf (planned[phrases[p].last + 1])
                            : total;

        std::vector<const LickMatch*> fitting;
        auto weights = 0;

        for (const auto& match : matches)
        {
            /*  A style only quotes a figure it could have phrased. If it says
                its phrases run to twelve notes, a thirteen-note quote is out
                of character for it whatever else fits - which is how the
                pentatonic style came to be offered a cell one note longer than
                anything its own planner writes.

                The long end only, and that is the same asymmetry `phraseFit`
                reads and `CompBarReading::tooBusy` has: a quote *shorter* than
                the style's shortest phrase is a player leaving space, and L09
                is a two-note ending on purpose. */
            if (static_cast<int> (match.lick->notes.size()) > style.longestPhrase)
                continue;

            const auto reach = reachOf (*match.lick);

            if (match.startTick + reach.first < from
                || match.startTick + reach.lastOnset + phraseGap > to)
                continue;

            fitting.push_back (&match);
            weights += match.lick->weight;
        }

        if (fitting.empty() || weights <= 0)
        {
            remember (plannedLastOnset);
            continue;
        }

        //  The weighted draw. Same shape as `compingVoicing`'s: weight decides
        //  how often a lick is reached for, never whether it fits.
        auto drawn = static_cast<int> (mix (seed, salt + 0x5b1u) % static_cast<std::uint32_t> (weights));

        for (const auto* match : fitting)
        {
            drawn -= match->lick->weight;

            if (drawn < 0)
            {
                quoted[p] = *match;
                remember (match->startTick + reachOf (*match->lick).lastOnset);
                break;
            }
        }
    }

    return quoted;
}
/** Writes one quoted phrase, and says whether it could be written at all.

    False when no octave puts the whole lick inside the style's register, in
    which case nothing has been added to @p line and the caller writes the
    phrase note by note instead. That is a real answer rather than a failure:
    a lick wider than the register cannot be played in it.

    `previous` and `direction` carry on through, because a quote is part of the
    same line - the phrase after it steps on from where the lick left off
    rather than starting again from nowhere.
*/
bool writeQuote (std::vector<WrittenNote>& line,
                 const LickMatch& match,
                 const Chart& chart,
                 const LineStyleDefinition& style,
                 const LineAnalyzer::Options& options,
                 int fromBar,
                 int barTicks,
                 int total,
                 int& previous,
                 int& direction)
{
    const auto& lick = *match.lick;
    const auto root = placeLick (lick, match, style, previous);

    if (root <= 0)
        return false;

    for (const auto& quotedNote : lick.notes)
    {
        const auto at = match.startTick + quotedNote.tick;

        if (at < 0 || at >= total)
            continue;

        const auto bar = fromBar + at / barTicks;
        const auto* over = chart.chordAt (bar);

        if (over == nullptr)
            continue;

        WrittenNote note;
        note.measureIndex = bar;
        note.at = BarPosition::fromTicks (at % barTicks);
        note.midiNote = lick.midiFor (quotedNote, root);
        note.chordSymbol = over->toString();
        note.lengthTicks = quotedNote.lengthTicks;
        note.lickKey = lick.key;

        /*  Coloured by the same tables the reader uses, never by the role the
            source wrote down. A `LickRole` is what the person who transcribed
            it said; a `NoteColour` is what the analyser will read off a take,
            and a take has no idea a lick was involved. Where the note is
            outside by pitch, the writer says which of the outside colours by
            looking at the note after it in the lick - a step away is an
            approach, anything else is outside - which is the one part of the
            reader's answer that can be known from the figure alone. */
        const auto tones = toneClasses (*over);
        const auto reading = readingScaleFor (*over, options);
        const auto scaleNotes = reading.has_value() ? reading->scale.pitchClasses() : tones;

        if (holds (tones, note.midiNote))
            note.colour = NoteColour::chordTone;
        else if (holds (scaleNotes, note.midiNote))
            note.colour = NoteColour::scaleTone;
        else
        {
            const auto next = &quotedNote != &lick.notes.back()
                                  ? lick.midiFor (*(&quotedNote + 1), root) : 0;
            const auto moved = next > 0 ? std::abs (next - note.midiNote) : 0;

            note.colour = moved >= 1 && moved <= 2 ? NoteColour::approach
                                                   : NoteColour::outside;
        }

        if (note.midiNote != previous)
            direction = note.midiNote > previous ? 1 : -1;

        previous = note.midiNote;
        line.push_back (note);
    }

    return true;
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

    /*  Every subdivision the style's vocabulary uses, not only its own feel.
        A style that can draw on a triplet lick has players who play triplets,
        and marking one off the grid would be this file disagreeing with the
        catalogue it quotes from. One source for it, in `subdivisionsFor`. */
    const auto accepted = subdivisionsFor (style);
    const auto beats = std::max (1, beatsPerBar);

    const auto note = [] (const WrittenNote& n)
    {
        return " (" + n.chordSymbol + ", bar " + std::to_string (n.measureIndex + 1)
             + ", " + n.at.describe() + ")";
    };

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const auto& written = line[i];

        /*  The register is checked for every note, quoted or not, because it
            is the one rule nothing downstream can recover from: a line that
            walks off the keyboard is not playable whoever wrote it, and
            `placeLick` is supposed to guarantee this. */
        if (written.midiNote < style.lowestNote || written.midiNote > style.highestNote)
            found.push_back ({ LineFault::outsideTheRegister, i,
                               "outside the style's register" + note (written) });

        /*  Everything below is about *generated* material, and that is the
            point rather than an exemption. These are the constraints that stop
            the atom writer drifting away from the style it claims to play: an
            approach it invented has to land, a chromatic it invented belongs
            off the beat, and a note it invented belongs on the grid. A
            documented device may do none of the three - L03 side-slips a whole
            cell over the V and puts the first note of it on beat one, and L13
            crushes a grace note two ticks before its target - and calling
            those faults would be calling Coltrane and Red Garland faults.
            A lick answers to its provenance; see `LickCatalogue.h`. */
        if (! written.lickKey.empty())
            continue;

        if (! onAnyGrid (written.at, accepted))
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

    const auto barTicks = options.beatsPerBar * ticksPerBeat;
    const auto total = (toBar - fromBar + 1) * barTicks;

    const auto phrases = phrasesIn (planned);
    const auto quoted = chooseQuotes (chart, style, planned, phrases,
                                      fromBar, toBar, options.beatsPerBar, seed);

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

    //  Which phrase each slot belongs to, so the loop below can tell when it
    //  has reached one that is quoted rather than written.
    std::vector<std::size_t> phraseOf (planned.size(), 0);

    for (std::size_t p = 0; p < phrases.size(); ++p)
        for (auto i = phrases[p].first; i <= phrases[p].last && i < planned.size(); ++i)
            phraseOf[i] = p;

    auto skipUntil = std::size_t { 0 };

    for (std::size_t i = 0; i < planned.size(); ++i)
    {
        const auto& slot = planned[i];
        const auto* chord = chart.chordAt (slot.measureIndex);

        if (chord == nullptr)
            continue;

        if (i < skipUntil)
            continue;

        /*  A quoted phrase: the lick's own notes, at the lick's own ticks, in
            the lick's own rhythm. The slots the planner laid out for this
            phrase are dropped - the lick is the phrase now. */
        const auto& match = quoted[phraseOf[i]];

        if (match.lick != nullptr && i == phrases[phraseOf[i]].first
            && writeQuote (line, match, chart, style, options,
                           fromBar, barTicks, total, previous, direction))
        {
            landingOn = -1;
            skipUntil = phrases[phraseOf[i]].last + 1;
            continue;
        }

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

        /*  ...and never on the last note of a phrase. An approach and its
            landing are one gesture, and the landing is owed to the next note
            this loop writes - which, once a phrase can be quoted rather than
            written, may be the first note of a lick that knows nothing about
            it. The gesture completes inside its own phrase or it is not
            written at all. It was always the weaker place for one: an approach
            that resolves across a rest is an approach the ear has lost. */
        if (note.midiNote == 0 && style.usesApproaches && changes && ! strong
            && previous > 0 && ! slot.endsPhrase)
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

        note.lengthTicks = std::max (1, ticksFor (style.feel));

        previous = note.midiNote;
        line.push_back (note);
    }

    return line;
}

} // namespace jazz::core
