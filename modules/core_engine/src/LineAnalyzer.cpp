#include "jazz/core/LineAnalyzer.h"

#include "jazz/core/Pitch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace jazz::core
{

namespace
{
    /*  The shape of `LineStats::score`, named rather than written into the
        arithmetic - every one of these is a judgement someone may want to
        argue with, and an argument is easier when the number has a name. */
    constexpr double lowestComfortableChordShare  = 0.35;
    constexpr double highestComfortableChordShare = 0.65;
    constexpr double mostBalanceCanCost           = 0.15;
    constexpr double notesBeforeBalanceCounts     = 4.0;

    /*  The shape of the window, same idea. A step is a semitone or a tone -
        wider than that and a line is not stepping, it is going somewhere else,
        which is the whole distinction an approach note turns on. A leap starts
        at a fourth, which is where common practice starts expecting the next
        note to step back. */
    constexpr int widestStep = 2;
    constexpr int smallestLeap = 5;

    /*  An enclosure is the widest pattern here: two attacks and the target.
        Counted in attacks rather than notes because a chord is one of them -
        a window three notes wide would hold less than one voicing. */
    constexpr std::size_t attacksInTheWindow = 3;

    /*  A bar the line never coloured is only worth mentioning once there is
        enough of it to be talking about - the same threshold the summary
        already uses before it names a worst bar. */
    constexpr int notesBeforeABarIsWorthNaming = 3;

    /*  A take that never left an octave is narrow. Below this many notes it is
        not narrow, it is short. */
    constexpr int narrowRange = 12;
    constexpr int notesBeforeRangeCounts = 8;

    /*  Notes on strong beats before their share is worth naming. A bar or two
        of them is not a habit, and the reading is about where a player puts
        the harmony over a stretch of line rather than over one bar. */
    constexpr int strongBeatsBeforeItCounts = 8;

    /** Rounded percentages of @p counts that add up to exactly 100.

        Rounding each share on its own gives three numbers that make 99 or 101
        often enough to be noticed - 0.505 and 0.495 both round up. So: take
        every whole percent, then hand the points left over to whichever shares
        were cut shortest. Standard largest-remainder, and the only reason it is
        here is that a panel reading "34% / 52% / 15%" looks like a bug.
    */
    std::array<int, 5> sharesOfOneHundred (const std::array<int, 5>& counts)
    {
        const auto total = std::accumulate (counts.begin(), counts.end(), 0);

        if (total <= 0)
            return { 0, 0, 0, 0, 0 };

        std::array<int, 5> whole {};
        std::array<int, 5> remainder {};

        for (std::size_t i = 0; i < counts.size(); ++i)
        {
            whole[i] = counts[i] * 100 / total;
            remainder[i] = counts[i] * 100 % total;
        }

        auto spare = 100 - std::accumulate (whole.begin(), whole.end(), 0);

        while (spare > 0)
        {
            const auto largest = static_cast<std::size_t> (
                std::distance (remainder.begin(), std::max_element (remainder.begin(), remainder.end())));

            whole[largest] += 1;
            remainder[largest] = -1;   // taken: do not let it win twice
            --spare;
        }

        return whole;
    }

    /** Reads one note against a chord and the scales that fit it.

        The scales arrive ordered best-first, so the first one accounting for a
        note is the best reading of it - with one preference on top: a scale
        where the note is not an avoid note is named ahead of one where it is.
        The note is a scale tone either way; this only decides which scale gets
        the credit, and "the 9th of C Lydian" is more use than "the 4th you
        should not sit on" when a perfectly good reading exists.
    */
    LineNote readAgainst (int midiNote,
                          int measureIndex,
                          const ChordSymbol& chord,
                          const std::vector<ScaleSuggestion>& scales)
    {
        LineNote note;
        note.midiNote = midiNote;
        note.measureIndex = measureIndex;
        note.chordSymbol = chord.toString();

        const auto pitch = toPitchClass (midiNote);
        note.degree = intervalLabel (ascendingInterval (chord.root(), pitch), chord.hasMinorThird());

        if (chord.containsPitchClass (pitch))
        {
            note.colour = NoteColour::chordTone;
            return note;
        }

        const ScaleSuggestion* fallback = nullptr;

        for (const auto& suggestion : scales)
        {
            if (! suggestion.scale.contains (pitch))
                continue;

            const auto& avoid = suggestion.avoidNotes;
            const auto isAvoid = std::find (avoid.begin(), avoid.end(), pitch) != avoid.end();

            if (isAvoid)
            {
                if (fallback == nullptr)
                    fallback = &suggestion;

                continue;
            }

            note.colour = NoteColour::scaleTone;
            note.scaleName = suggestion.scale.name (chord.accidental());
            return note;
        }

        if (fallback != nullptr)
        {
            note.colour = NoteColour::scaleTone;
            note.scaleName = fallback->scale.name (chord.accidental());
            note.avoidNote = true;
            return note;
        }

        note.colour = NoteColour::outside;
        return note;
    }

    /** Fills in the nearest note that would close an open one.

        Looked for a step either way, semitones before tones, and a chord tone
        before a scale tone at the same distance. Nothing here is a rule about
        what the player *should* do - it is a statement about what is within
        reach, which is the only useful thing to say about a note whose fate
        has not been decided yet.
    */
    void describeResolution (LineNote& note,
                             const ChordSymbol& chord,
                             const std::vector<ScaleSuggestion>& scales)
    {
        /*  Three strengths rather than two, and the third is the tiebreak that
            matters: Db over Dm7 is a semitone from C and a semitone from D, and
            both are chord tones. Without a preference the answer came down to
            which direction the loop happened to try first, which is no way to
            decide a thing a player is going to read. The root is the strongest
            place a line can land, so it wins. */
        const auto lands = [&chord, &scales] (int midiNote)
        {
            const auto pitch = toPitchClass (midiNote);

            if (pitch == chord.root())
                return 3;

            if (chord.containsPitchClass (pitch))
                return 2;

            for (const auto& suggestion : scales)
                if (suggestion.scale.contains (pitch))
                    return 1;

            return 0;
        };

        auto best = 0;

        for (const auto distance : { 1, 2 })
        {
            for (const auto step : { -distance, distance })
            {
                const auto strength = lands (note.midiNote + step);

                if (strength > best)
                {
                    best = strength;
                    note.wantsToReach = note.midiNote + step;
                }
            }

            // A semitone away beats anything a tone away, so stop as soon as
            // this distance found something rather than letting the wider
            // search overwrite it.
            if (best > 0)
                break;
        }

        if (note.wantsToReach > 0)
            note.wantsToReachDegree = intervalLabel (
                ascendingInterval (chord.root(), toPitchClass (note.wantsToReach)),
                chord.hasMinorThird());
    }

    /** The scales a note will be read against, best-first. */
    std::vector<ScaleSuggestion> scalesFor (const ChordSymbol& chord,
                                            const LineAnalyzer::Options& options)
    {
        ScaleSuggester::Options suggesterOptions;

        if (const auto* style = findScaleStyle (options.style))
            suggesterOptions.families = style->families;

        auto suggestions = ScaleSuggester { suggesterOptions }.suggestionsFor (chord);

        // Nothing in this style fits this chord - a diminished bar while working
        // on bebop scales, say. Read it against the whole catalogue rather than
        // against nothing, which would call every note outside.
        if (suggestions.empty() && ! suggesterOptions.families.empty())
            suggestions = ScaleSuggester {}.suggestionsFor (chord);

        if (options.acceptAnyValidScale || suggestions.empty())
            return suggestions;

        if (! options.chosenScale.empty())
        {
            for (const auto& suggestion : suggestions)
                if (suggestion.scale.name() == options.chosenScale
                    || suggestion.scale.name (Accidental::sharps) == options.chosenScale)
                    return { suggestion };
        }

        // No choice made, or a name from some other chord's list: the engine's
        // own first answer, which is what the panel shows by default anyway.
        return { suggestions.front() };
    }

    std::string plural (int count, const std::string& singular, const std::string& many)
    {
        return std::to_string (count) + " " + (count == 1 ? singular : many);
    }

    /** "2", "2 and 5", "2, 5 and 9" - a list a person would read out. */
    std::string joined (const std::vector<std::string>& parts)
    {
        std::string text;

        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0)
                text += (i + 1 == parts.size() ? " and " : ", ");

            text += parts[i];
        }

        return text;
    }
}

//==============================================================================
namespace
{
    std::array<int, 5> sharesOf (const LineStats& stats)
    {
        return sharesOfOneHundred ({ stats.chordTones, stats.scaleTones,
                                     stats.approachTones, stats.unresolved, stats.outside });
    }
}

int LineStats::percentChordTones() const noexcept    { return sharesOf (*this)[0]; }
int LineStats::percentScaleTones() const noexcept    { return sharesOf (*this)[1]; }
int LineStats::percentApproachTones() const noexcept { return sharesOf (*this)[2]; }
int LineStats::percentUnresolved() const noexcept    { return sharesOf (*this)[3]; }
int LineStats::percentOutside() const noexcept       { return sharesOf (*this)[4]; }

int LineStats::score() const noexcept
{
    // Notes the line has opened and not yet closed are not judged - see the
    // header. A bar of nothing but those has no score, rather than a bad one.
    const auto judged = settled();

    if (judged <= 0)
        return 0;

    const auto working = landed();

    // Everything that worked, and a quarter of what did not.
    const auto reading = 100.0 * (working + 0.25 * outside) / judged;

    // Nothing that worked is nothing to be one-sided about.
    if (working <= 0)
        return static_cast<int> (std::lround (reading));

    const auto chordShare = static_cast<double> (chordTones) / working;

    // How far outside the band a line wants to sit in. Zero within it, and at
    // most the width of one side of it - which is what the allowance is scaled
    // against, so leaning either way costs the same.
    const auto off = std::max (0.0, lowestComfortableChordShare - chordShare)
                   + std::max (0.0, chordShare - highestComfortableChordShare);

    // Scaled against the furthest either edge of the band can be from an
    // extreme, so the two sides cost the same and stay that way if the band
    // is ever moved.
    const auto worstLean = std::max (lowestComfortableChordShare,
                                     1.0 - highestComfortableChordShare);

    const auto lean = (off / worstLean)
                    * std::min (1.0, working / notesBeforeBalanceCounts);

    return static_cast<int> (std::lround (reading * (1.0 - mostBalanceCanCost * lean)));
}

bool isOutsideByPitch (NoteColour colour) noexcept
{
    return colour == NoteColour::approach
        || colour == NoteColour::unresolved
        || colour == NoteColour::outside;
}

bool isSettled (NoteColour colour) noexcept
{
    return colour != NoteColour::unresolved;
}

std::string approachKindName (ApproachKind kind)
{
    switch (kind)
    {
        case ApproachKind::chromatic: return "chromatic approach";
        case ApproachKind::passing:   return "passing tone";
        case ApproachKind::enclosure: return "enclosure";
        case ApproachKind::none:      break;
    }

    return "";
}

std::string noteColourName (NoteColour colour)
{
    switch (colour)
    {
        case NoteColour::chordTone:  return "chord tone";
        case NoteColour::scaleTone:  return "scale tone";
        case NoteColour::approach:   return "approach note";
        case NoteColour::unresolved: return "outside for now";
        case NoteColour::outside:    break;
    }

    return "outside";
}

//==============================================================================
LineNote LineAnalyzer::read (int midiNote, const ChordSymbol& chord, Options options)
{
    return readAgainst (midiNote, 0, chord, scalesFor (chord, options));
}

void LineAnalyzer::setOptions (Options newOptions)
{
    options = std::move (newOptions);

    // The bar in front of the player is read the new way immediately; the notes
    // behind them keep the reading they were given.
    if (target.has_value())
        setTarget (target->measureIndex, target->chord);
}

void LineAnalyzer::startTake()
{
    played.clear();
    recent.clear();
    justResolvedAt.clear();
    justStrandedAt.clear();
    strandedThisAttack.clear();
    justResolved.clear();
    justStranded.clear();
    taking = true;
}

void LineAnalyzer::endTake()
{
    // The notes stay. Disarming freezes a summary to read, so throwing the take
    // away at exactly the moment it becomes worth looking at would be perverse.
    //
    // Anything still open closes now, as outside: there will be no more notes,
    // so the resolution it was waiting for is not coming. A summary carrying
    // "waiting to see" about a take that has ended would be waiting for good.
    justResolvedAt.clear();
    justStrandedAt.clear();
    strandedThisAttack.clear();

    for (std::size_t i = 0; i < played.size(); ++i)
        if (! isSettled (played[i].colour))
        {
            played[i].colour = NoteColour::outside;
            justStrandedAt.push_back (i);
        }

    publishJust (played);

    // The window starts clean too: the first note after a take is not the
    // resolution of the last note of it.
    recent.clear();
    taking = false;
}

//==============================================================================
std::vector<LineAnalyzer::AttackSpan> LineAnalyzer::attacksIn (const std::vector<LineNote>& line)
{
    std::vector<AttackSpan> attacks;

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (attacks.empty() || ! line[i].struckWithPrevious)
            attacks.push_back ({ i, i + 1 });
        else
            attacks.back().end = i + 1;
    }

    return attacks;
}

/** Rebuilds the two public lists from the indices behind them.

    Built at the end of `play()` rather than as the window goes, so a note that
    was stranded and then reached by the rest of its own chord appears in one
    list rather than in both - and so neither list can hand out a copy of a
    reading that has moved on since it was taken.
*/
void LineAnalyzer::publishJust (const std::vector<LineNote>& line)
{
    const auto fill = [&line] (std::vector<LineNote>& into, const std::vector<std::size_t>& from)
    {
        into.clear();

        for (const auto index : from)
            if (index < line.size())
                into.push_back (line[index]);
    };

    fill (justResolved, justResolvedAt);
    fill (justStranded, justStrandedAt);
}

void LineAnalyzer::setTarget (int measureIndex, const ChordSymbol& chord)
{
    Target next;
    next.measureIndex = measureIndex;
    next.chord = chord;
    next.scales = scalesFor (chord, options);

    target = std::move (next);
}

LineNote LineAnalyzer::play (int midiNote, BarPosition where, Attack attack)
{
    /*  The position is attached before the note is read, so everything the
        window does behind it - resolving, settling, and now marking the note
        before it as passed through - can see it. */
    pendingPosition = where;

    auto note = play (midiNote, attack);

    pendingPosition.reset();

    return note;
}

LineNote LineAnalyzer::play (int midiNote, Attack attack)
{
    auto note = readAgainstTarget (midiNote);

    note.at = pendingPosition;

    if (pendingPosition.has_value())
        note.onStrongBeat = isStrong (*pendingPosition, options.beatsPerBar);

    auto& line = taking ? played : recent;

    /*  Joining the attack before it, rather than starting one. Refused across
        a bar change: notes struck together are struck against one chord, and
        two notes read against different bars are two gestures whatever the
        shell believed about the keyboard. */
    note.struckWithPrevious = attack == Attack::withPrevious
                           && ! line.empty()
                           && line.back().measureIndex == note.measureIndex;

    /*  Cleared per attack, not per note. A chord's notes arrive one call at a
        time and between them they have one piece of news, so a shell reading
        this after each of them sees that news accumulate and correct itself
        rather than flickering through it. */
    if (! note.struckWithPrevious)
    {
        justResolvedAt.clear();
        justStrandedAt.clear();
        strandedThisAttack.clear();
    }

    /*  Outside the harmony, played this instant: the line has opened something
        and nothing yet knows whether it will close it. `read()` says `outside`
        because one note has no line around it; a take says `unresolved`, and
        waits the two notes the window can reach.

        With no bar to read against there is nothing to resolve *into*, so that
        note stays `outside` - which is the honest answer to how it sits against
        nothing, and not a question waiting on an answer that cannot come. */
    if (note.colour == NoteColour::outside && target.has_value())
    {
        note.colour = NoteColour::unresolved;
        describeResolution (note, target->chord, target->scales);
    }

    line.push_back (note);

    const auto attacks = attacksIn (line);

    markPassedThrough (line, attacks);
    resolveTail (line, attacks);
    settleTail (line, attacks);

    // Without a take the window is all there is, and it never grows past what
    // the widest pattern needs: this is a window, not a second take hiding
    // behind the first. Trimmed by whole attacks, because half a chord is not
    // a thing the line can resolve into - and after settling rather than
    // before, or a note could be dropped off the front while still open and
    // its verdict would go with it.
    if (! taking)
    {
        const auto spans = attacksIn (line);

        if (spans.size() > attacksInTheWindow)
        {
            const auto dropped = spans[spans.size() - attacksInTheWindow].begin;

            line.erase (line.begin(), line.begin() + static_cast<std::ptrdiff_t> (dropped));

            const auto shift = [dropped] (std::vector<std::size_t>& indices)
            {
                std::vector<std::size_t> kept;

                for (const auto index : indices)
                    if (index >= dropped)
                        kept.push_back (index - dropped);

                indices = std::move (kept);
            };

            shift (justResolvedAt);
            shift (justStrandedAt);
            shift (strandedThisAttack);
        }
    }

    publishJust (line);

    return note;
}

/** Says of the note before this one whether the line stayed on it.

    The distinction the grid was wanted for, and the one thing here that could
    not be said before a note carried a position. An avoid note passed through
    at speed is what every bebop line does; the same note sat on is the one
    that sounds like a mistake. Both are the same pitch against the same chord,
    so nothing but the rhythm can tell them apart.

    Filled in behind rather than at the time, because "passed through" is a
    fact about the gap to the *next* note and that note has only just arrived.
    An eighth is the boundary: at any tempo a player would call two notes an
    eighth apart a run and two notes a beat apart two notes.

    Silent when either note has no position, which is every note of a take
    played without a clock.

    Asked of a whole attack at once, and only when the newest note started one.
    The note struck with a chord's Ab is not the note after it, so it says
    nothing about how long the line stayed there; what does is the next thing
    struck, which is what the next attack is.
*/
void LineAnalyzer::markPassedThrough (std::vector<LineNote>& line,
                                      const std::vector<AttackSpan>& attacks)
{
    if (attacks.size() < 2)
        return;

    const auto& latestAttack = attacks.back();

    // The newest note joined the attack rather than starting one, so the attack
    // before it was already asked when this one began.
    if (latestAttack.begin + 1 != line.size())
        return;

    const auto& latest = line[latestAttack.begin];

    if (! latest.at.has_value())
        return;

    const auto& previousAttack = attacks[attacks.size() - 2];

    for (auto i = previousAttack.begin; i < previousAttack.end; ++i)
    {
        auto& previous = line[i];

        if (! previous.at.has_value())
            continue;

        // Only a note worth asking the question about. A chord tone held for
        // two bars is a held chord tone, not something the line sat on.
        if (! previous.avoidNote
            && previous.colour != NoteColour::outside
            && previous.colour != NoteColour::unresolved)
            continue;

        // Bars are whole numbers of beats apart, so the gap is measured in
        // ticks across the barline rather than within one bar - a note on the
        // and of four and the downbeat after it are an eighth apart, not a bar
        // and a bit.
        const auto barsApart = latest.measureIndex - previous.measureIndex;
        const auto gap = latest.at->inTicks() - previous.at->inTicks()
                           + barsApart * options.beatsPerBar * ticksPerBeat;

        previous.passedThrough = gap > 0 && gap <= ticksFor (Subdivision::eighth);
    }
}

/** Whether any pattern could still promote the note at @p noteIndex.

    All three patterns are at most three attacks wide, so this is a
    question about what has been struck since.

    A chromatic approach and a passing tone are settled by the very next
    attack: something in it landed a step away or nothing did, and no later
    attack can change that. An enclosure needs the attack after that as
    well - but only when the next attack has a note that is itself outside,
    and only when the two leave room for a target between them. Opposite
    sides, each within a step, is only possible when they are two to four
    semitones apart: closer and there is nothing between them, wider and no
    one note is a step from both.

    Anything else has had every chance it is going to get.

    Read over attacks rather than notes, which is the whole of what a chord
    changes here: the note struck with this one is not the note after it,
    and cannot close the question about it. With nothing struck together
    every attack is one note and this is the rule it always was.
*/
bool LineAnalyzer::canStillBeReached (const std::vector<LineNote>& line,
                                      const std::vector<AttackSpan>& attacks,
                                      std::size_t attackIndex,
                                      std::size_t noteIndex)
{
    // Nothing has followed it yet, so everything is still open to it.
    if (attackIndex + 1 >= attacks.size())
        return true;

    const auto& next = attacks[attackIndex + 1];

    /*  An enclosure needs a note of the next attack to be outside as well,
        and to leave room for a target between the two. Nothing like that
        in it means the step patterns have been tried and failed. */
    auto roomForAnEnclosure = false;

    for (auto j = next.begin; j < next.end && ! roomForAnEnclosure; ++j)
    {
        if (! isOutsideByPitch (line[j].colour))
            continue;

        const auto apart = std::abs (line[j].midiNote - line[noteIndex].midiNote);

        roomForAnEnclosure = apart >= 2 && apart <= 2 * widestStep;
    }

    if (! roomForAnEnclosure)
        return false;

    // Room for one, so it turns on the attack after next - which has either
    // been struck and not made one, or has not been struck at all.
    return attackIndex + 2 >= attacks.size();
}

/** Closes every open note the line can no longer reach.

    Waiting a fixed two notes was wrong, and wrong in the direction that
    matters: the commonest case by far is an outside note followed by one that
    simply lands somewhere else, and there is nothing to wait for there. The
    next note either stepped home or it did not, and once it has landed no
    enclosure can involve the note before it either. Holding the verdict back
    another note meant the one piece of bad news this reads arrived a note late
    for no reason at all.

    So a note closes as soon as nothing can still reach it, which is usually
    the very next note - and waits only when an enclosure is genuinely still in
    play. `canStillBeReached` is where that is decided.
*/
void LineAnalyzer::settleTail (std::vector<LineNote>& line,
                              const std::vector<AttackSpan>& attacks)
{
    /*  Every attack but the newest. A chord's own notes cannot close each
        other - they sounded together, and a note is not the resolution of one
        played at the same moment - so the attack being struck is left alone
        until something follows it. */
    for (std::size_t k = 0; k + 1 < attacks.size(); ++k)
    {
        for (auto i = attacks[k].begin; i < attacks[k].end; ++i)
        {
            auto& note = line[i];

            if (isSettled (note.colour) || canStillBeReached (line, attacks, k, i))
                continue;

            note.colour = NoteColour::outside;
            justStrandedAt.push_back (i);
            strandedThisAttack.push_back (i);
        }
    }
}

/** Promotes the note at @p index to an approach note resolving into @p target.

    Returns false, harmlessly, for a note that was not outside to begin with -
    the callers below try patterns in order and several of them overlap, so
    "already landed" is an answer rather than a mistake.
*/
bool LineAnalyzer::promote (std::vector<LineNote>& line, std::size_t index, int target,
                            ApproachKind kind)
{
    auto& note = line[index];

    // Only a note still open can be promoted. One that landed does not need it,
    // and one the window has already passed is not reachable any more.
    if (note.colour != NoteColour::unresolved)
    {
        /*  With one exception, and it is the one a chord needs. The window
            cannot tell a chord's first note from an ordinary next note until
            the second one arrives, so it judges on the first - and a voicing
            whose lowest note lands nowhere near an open note will have
            stranded it a few milliseconds before the note that was actually
            resolving it was struck. Taking that back inside the same gesture
            is not revisiting a settled note; it is finishing reading the
            gesture that settled it. */
        const auto stranded = std::find (strandedThisAttack.begin(),
                                         strandedThisAttack.end(), index);

        if (note.colour != NoteColour::outside || stranded == strandedThisAttack.end())
            return false;

        strandedThisAttack.erase (stranded);
        justStrandedAt.erase (std::remove (justStrandedAt.begin(), justStrandedAt.end(), index),
                              justStrandedAt.end());
    }

    note.colour = NoteColour::approach;
    note.resolvesTo = target;
    note.approachKind = kind;

    justResolvedAt.push_back (index);
    return true;
}

/** Looks back over the notes the newest one could have resolved.

    Two attacks, and no more: the attack before it, which the new note may have
    been approached from, and the one before that, which the pair may have
    enclosed. Every pattern here is at most three attacks wide, so a longer
    look back would find nothing and a shorter one would miss the enclosure.

    Attacks rather than notes, and that is the whole of what chordal playing
    needs. A voicing descending into the next one is several lines at once -
    the Ab of a G7alt going to the G of a Cmaj7 while its Eb goes to the D -
    and each of those voices finds its own note in the attack that follows. Run
    over notes instead, an inner voice's resolution is whatever happened to be
    struck next, which over a chord is one of its own notes and never a
    resolution at all. Every rule below is the rule it always was with "the
    note before" widened to "any note of the attack before".

    Nothing here cares which bar a note was in. Running chromatically into the
    first beat of the next chord is one of the most idiomatic things in the
    idiom, and a window that stopped at the barline would call it a mistake at
    exactly the moment it was working.
*/
void LineAnalyzer::resolveTail (std::vector<LineNote>& line,
                                const std::vector<AttackSpan>& attacks)
{
    if (attacks.size() < 2)
        return;

    const auto last = line.size() - 1;

    // Only a note that landed is somewhere to land. The newest note is the only
    // one that can have resolved anything: everything before it was asked when
    // it arrived.
    if (isOutsideByPitch (line[last].colour))
        return;

    const auto target = line[last].midiNote;

    const auto& before = attacks[attacks.size() - 2];
    const auto haveTwoBefore = attacks.size() >= 3;
    const auto& twoBefore = attacks[haveTwoBefore ? attacks.size() - 3 : attacks.size() - 2];

    const auto step = [] (int from, int to)
    {
        const auto distance = std::abs (to - from);
        return distance >= 1 && distance <= widestStep;
    };

    /*  Most specific first, and it matters now that the three gestures are
        told apart: a note can honestly answer to more than one of them, and a
        promoted note is never re-promoted, so whichever rule reaches it first
        decides what it is called.

        The second note of an enclosure is a chromatic approach in its own
        right - Db into D is a semitone either way - and a note stepped into
        and stepped out of, still rising, is a passing tone *and* a chromatic
        approach when the last step is a semitone. Both of those are true and
        the fuller description is the more useful one, so the order runs
        enclosure, passing tone, chromatic approach. */
    if (haveTwoBefore)
    {
        // Two notes taking the target from both sides before landing on it.
        // Both were the line aiming rather than missing, so both are promoted.
        for (auto j = before.begin; j < before.end; ++j)
        {
            if (! isOutsideByPitch (line[j].colour))
                continue;

            for (auto i = twoBefore.begin; i < twoBefore.end; ++i)
            {
                if (! isOutsideByPitch (line[i].colour))
                    continue;

                const auto above = line[i].midiNote - target;
                const auto below = line[j].midiNote - target;

                if (((above > 0) != (below > 0))
                    && step (line[i].midiNote, target)
                    && step (line[j].midiNote, target))
                {
                    promote (line, i, target, ApproachKind::enclosure);
                    promote (line, j, target, ApproachKind::enclosure);
                }
            }
        }
    }

    // A passing tone: stepped into, stepped out of, and still going the same
    // way. Says more than "a semitone from the next note" does - the note was
    // in transit rather than leaning - and it is what catches the wider gaps,
    // where a pentatonic leaves room to pass through by a whole tone.
    if (haveTwoBefore)
    {
        for (auto j = before.begin; j < before.end; ++j)
        {
            for (auto i = twoBefore.begin; i < twoBefore.end; ++i)
            {
                if (isOutsideByPitch (line[i].colour))
                    continue;

                const auto in = line[j].midiNote - line[i].midiNote;
                const auto out = target - line[j].midiNote;

                if (((in > 0) == (out > 0))
                    && step (line[i].midiNote, line[j].midiNote)
                    && step (line[j].midiNote, target))
                    promote (line, j, target, ApproachKind::passing);
            }
        }
    }

    // A chromatic approach: one note outside, and the next one a semitone away
    // and home. The commonest of the three by a wide margin, and the one left
    // when neither of the fuller readings fits.
    for (auto j = before.begin; j < before.end; ++j)
        if (std::abs (target - line[j].midiNote) == 1)
            promote (line, j, target, ApproachKind::chromatic);
}

LineNote LineAnalyzer::readAgainstTarget (int midiNote) const
{
    if (! target.has_value())
    {
        LineNote note;
        note.midiNote = midiNote;
        note.measureIndex = -1;
        note.colour = NoteColour::outside;
        return note;
    }

    return readAgainst (midiNote, target->measureIndex, target->chord, target->scales);
}

//==============================================================================
namespace
{
    void count (LineStats& stats, NoteColour colour)
    {
        switch (colour)
        {
            case NoteColour::chordTone:  ++stats.chordTones;    break;
            case NoteColour::scaleTone:  ++stats.scaleTones;    break;
            case NoteColour::approach:   ++stats.approachTones; break;
            case NoteColour::unresolved: ++stats.unresolved;    break;
            case NoteColour::outside:    ++stats.outside;       break;
        }
    }
}

LineStats LineAnalyzer::statsForBar (int measureIndex) const
{
    LineStats stats;

    for (const auto& note : played)
        if (note.measureIndex == measureIndex)
            count (stats, note.colour);

    return stats;
}

LineStats LineAnalyzer::stats() const
{
    LineStats stats;

    for (const auto& note : played)
        count (stats, note.colour);

    return stats;
}

TakeSummary LineAnalyzer::summary() const
{
    TakeSummary take;
    take.overall = stats();

    for (const auto& note : played)
    {
        auto existing = std::find_if (take.bars.begin(), take.bars.end(),
                                      [&note] (const LineBar& bar)
                                      { return bar.measureIndex == note.measureIndex; });

        if (existing == take.bars.end())
        {
            take.bars.push_back ({ note.measureIndex, note.chordSymbol, {} });
            existing = std::prev (take.bars.end());
        }

        count (existing->stats, note.colour);

        // Where it sat in the bar, for the shells that gave a position. Both
        // counts stay zero without one, which reads the same as "nothing
        // landed on a strong beat" and is the honest answer.
        if (note.onStrongBeat)
        {
            ++existing->notesOnStrongBeats;

            if (note.colour == NoteColour::chordTone)
                ++existing->chordTonesOnStrongBeats;
        }

        /*  Only the notes that have no other verdict. An approach note stepped
            home, which is the reading that matters about it - saying it was
            also passed through adds nothing and would count the line's best
            notes among the ones being asked about. */
        if (note.at.has_value()
            && (note.avoidNote || note.colour == NoteColour::outside))
        {
            if (note.passedThrough) ++take.notesPassedThrough;
            else                    ++take.notesSatOn;
        }
    }

    /*  Chords in the line, and the voices of them that were outside and stepped
        home. A note is in a chord when it joined the attack before it or the
        note after it joined this one - the flag describes the join rather than
        the chord, so both ends of a two-note attack have to be read from it.

        Counted, never weighted. A note struck with three others is counted,
        coloured and scored exactly like any other note; this is here so the
        summary can say back what the player was doing. */
    for (std::size_t i = 0; i < played.size(); ++i)
    {
        const auto joinedByNext = i + 1 < played.size() && played[i + 1].struckWithPrevious;

        if (played[i].struckWithPrevious && ! joinedByNext)
            ++take.chordsPlayed;   // one per chord, counted at its last note

        if ((played[i].struckWithPrevious || joinedByNext)
            && played[i].colour == NoteColour::approach)
            ++take.chordVoicesResolved;
    }

    // Which bars the line went over without ever colouring. Approach notes do
    // not clear the flag - a note on its way somewhere else has not said
    // anything about this chord.
    for (auto& bar : take.bars)
        bar.neverLeftTheChord = bar.stats.scaleTones == 0
                             && bar.stats.chordTones >= notesBeforeABarIsWorthNaming;

    // How the line moved, as opposed to where it sat. Measured across the whole
    // take rather than bar by bar, because a leap over a barline is still a
    // leap and a range is a property of a line, not of a chord.
    for (std::size_t i = 0; i < played.size(); ++i)
    {
        take.highestNote = i == 0 ? played[i].midiNote
                                  : std::max (take.highestNote, played[i].midiNote);
        take.lowestNote = i == 0 ? played[i].midiNote
                                 : std::min (take.lowestNote, played[i].midiNote);

        if (i == 0)
            continue;

        if (std::abs (played[i].midiNote - played[i - 1].midiNote) < smallestLeap)
            continue;

        ++take.leaps;

        // Resolved means the note after the leap stepped, in either direction.
        // Common practice asks for the step to turn back; asking for that here
        // would be marking a player down for a shape this idiom uses on
        // purpose, so the step alone is enough.
        if (i + 1 < played.size()
            && std::abs (played[i + 1].midiNote - played[i].midiNote) <= widestStep)
            ++take.leapsResolved;
    }

    if (take.overall.total() == 0)
    {
        take.summary = "Nothing played yet.";
        return take;
    }

    // The approach share is named only when there is one. A line that used no
    // approach notes should not have to read past a nought to find out.
    const auto approachShare = take.overall.percentApproachTones() > 0
                             ? std::to_string (take.overall.percentApproachTones()) + "% approach notes, "
                             : std::string {};

    take.summary = plural (take.overall.total(), "note", "notes")
                 + " over " + plural (static_cast<int> (take.bars.size()), "bar", "bars")
                 + " - " + std::to_string (take.overall.percentChordTones()) + "% chord tones, "
                 + std::to_string (take.overall.percentScaleTones()) + "% scale tones, "
                 + approachShare
                 + std::to_string (take.overall.percentOutside()) + "% outside.";

    // Problems first, the way the voicing analyser orders its findings - and in
    // the same voice: a note outside the scale is outside the scale, not wrong.
    // These describe the take; none of them scores it.
    const auto outside = take.overall.percentOutside();
    const auto chordTones = take.overall.percentChordTones();

    if (outside >= 35)
        take.observations.push_back (
            "A good deal of the line sat outside the scales these bars take. Landing on a chord "
            "tone as each bar arrives is the quickest way to bring it back in.");

    // Only worth naming once there is enough of a bar to be talking about, and
    // only when it stands apart from the take around it.
    if (take.bars.size() > 1)
    {
        const auto worst = std::max_element (take.bars.begin(), take.bars.end(),
                                             [] (const LineBar& a, const LineBar& b)
                                             { return a.stats.percentOutside() < b.stats.percentOutside(); });

        if (worst->stats.total() >= 4 && worst->stats.percentOutside() >= outside + 25)
            take.observations.push_back (
                "Bar " + std::to_string (worst->measureIndex + 1) + " (" + worst->chordSymbol
                + ") was the one that pulled away: " + std::to_string (worst->stats.percentOutside())
                + "% of it sat outside.");
    }

    if (chordTones >= 75)
        take.observations.push_back (
            "Nearly all of it was chord tones. That is safe ground, and a little plain - the "
            "scale tones in between are where a line starts to sound like one.");
    else if (outside < 20 && chordTones >= 25)
        take.observations.push_back (
            "A good spread: chord tones anchoring it, scale tones colouring it.");

    // Bar by bar, which is more use than the take-wide version above: it says
    // where to put the colour rather than that there should be some.
    std::vector<std::string> uncoloured;

    for (const auto& bar : take.bars)
        if (bar.neverLeftTheChord)
            uncoloured.push_back (std::to_string (bar.measureIndex + 1));

    if (! uncoloured.empty())
        take.observations.push_back (
            (uncoloured.size() == 1 ? "Bar " : "Bars ") + joined (uncoloured)
            + (uncoloured.size() == 1 ? " never left the chord." : " never left their chords.")
            + " Nothing there to fix - but one note either side of a chord tone is where a"
              " bar stops sounding like an arpeggio.");

    if (take.overall.approachTones > 0)
        take.observations.push_back (
            plural (take.overall.approachTones, "note", "notes")
            + " sat outside and stepped home - approaches and enclosures, not misses. They"
              " count as landing, which is why the outside figure is lower than the number of"
              " notes you played away from the scale.");

    // A leap wants a step after it. Only worth saying once there are enough
    // leaps for the pattern to be a habit rather than a moment.
    if (take.leaps >= 3 && take.leapsResolved * 2 < take.leaps)
    {
        const auto unresolved = take.leaps - take.leapsResolved;

        take.observations.push_back (
            plural (take.leaps, "big jump", "big jumps") + ", and "
            + (unresolved == take.leaps ? "every one of them"
                                        : std::to_string (unresolved) + " of them")
            + " jumped again rather than stepping back. A leap opens a gap the ear wants"
              " filled; the note after it is where that happens.");
    }

    /*  The two rhythmic readings. Both are silent for a take played with no
        clock, because every count behind them is zero - a shell that cannot
        say where a note fell gets exactly the take it always got.

        Words, never points, like every other shape reading. Where a note sits
        in the bar does not make it a better or worse note, and the moment it
        moved the score the score would stop being explainable. */
    if (take.notesSatOn > 0 && take.notesSatOn * 2 > take.notesPassedThrough)
        take.observations.push_back (
            plural (take.notesSatOn, "note", "notes")
            + " outside the harmony " + (take.notesSatOn == 1 ? "was" : "were")
            + " sat on rather than passed through. The same note at speed is what every bebop"
              " line is made of; it is the dwelling that the ear hears, not the note.");
    else if (take.notesPassedThrough >= 3)
        take.observations.push_back (
            plural (take.notesPassedThrough, "note", "notes")
            + " outside the harmony went by at an eighth or quicker - passed through rather"
              " than sat on, which is what makes them read as line rather than as error.");

    {
        auto onStrong = 0;
        auto chordTonesOnStrong = 0;

        for (const auto& bar : take.bars)
        {
            onStrong += bar.notesOnStrongBeats;
            chordTonesOnStrong += bar.chordTonesOnStrongBeats;
        }

        if (onStrong >= strongBeatsBeforeItCounts)
        {
            const auto share = chordTonesOnStrong * 100 / onStrong;

            if (share < 40)
                take.observations.push_back (
                    "Only " + std::to_string (share) + "% of what landed on a strong beat was a"
                    " chord tone. The beat is where the harmony is heard, so that is where the"
                    " chord tones do the most work - the colour goes in between.");
            else if (share >= 70)
                take.observations.push_back (
                    std::to_string (share) + "% of the notes on strong beats were chord tones."
                    " The harmony is coming through clearly.");
        }
    }

    /*  Chordal playing, said back. A player comping behind themselves or
        soloing in block chords is doing a different thing from playing a line,
        and the reading they most need is the one that used to be wrong: the
        inner voices of a voicing moving into the next one are resolutions, not
        a handful of notes that went nowhere. */
    if (take.chordsPlayed > 0)
    {
        auto said = plural (take.chordsPlayed, "chord", "chords") + " in the line";

        if (take.chordVoicesResolved > 0)
            said += ", and " + plural (take.chordVoicesResolved, "voice", "voices")
                  + " inside them stepped home into the next voicing. Voices resolving"
                    " together is what moving a whole voicing chromatically sounds like,"
                    " and each of them is read on its own way home rather than against"
                    " whatever else was struck with it.";
        else
            said += ". Every note of one is read against the bar on its own, and the"
                    " voices resolve into the next voicing rather than into each other.";

        take.observations.push_back (said);
    }

    if (take.overall.total() >= notesBeforeRangeCounts && take.rangeInSemitones() < narrowRange)
        take.observations.push_back (
            "The whole take stayed inside "
            + plural (take.rangeInSemitones(), "semitone", "semitones")
            + " - about a hand's width. The horn players you steal from use the whole range,"
              " and the same line an octave up is a different line.");

    return take;
}

} // namespace jazz::core
