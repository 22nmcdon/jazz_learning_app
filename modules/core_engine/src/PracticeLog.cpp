#include "jazz/core/PracticeLog.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <set>

namespace jazz::core
{

namespace
{
    /*  The eight qualities, in the order `ChordQuality` declares them, and the
        words a practice record uses for them.

        Not `ChordSymbol::toString()`, which writes a *symbol* - "m7b5" is what
        goes on a chart and "half-diminished chords" is what goes in a sentence
        about what somebody has been playing. The two are different jobs and
        neither reads well in the other's place. */
    struct NamedQuality
    {
        ChordQuality quality;
        const char* name;
    };

    constexpr std::array<NamedQuality, 8> allQualities { {
        { ChordQuality::major,          "major" },
        { ChordQuality::minor,          "minor" },
        { ChordQuality::dominant,       "dominant" },
        { ChordQuality::halfDiminished, "half-diminished" },
        { ChordQuality::diminished,     "diminished" },
        { ChordQuality::augmented,      "augmented" },
        { ChordQuality::suspended,      "suspended" },
        { ChordQuality::minorMajor,     "minor-major" }
    } };

    std::string plural (int count, const std::string& one, const std::string& many)
    {
        return std::to_string (count) + " " + (count == 1 ? one : many);
    }

    /** "A, B and C" - the way a sentence lists things, not the way a machine
        does. Empty for an empty list, which every caller guards first. */
    std::string joined (const std::vector<std::string>& items)
    {
        std::string result;

        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0)
                result += (i + 1 == items.size() ? " and " : ", ");

            result += items[i];
        }

        return result;
    }

    /** "1-4, 9 and 12" from a sorted list of bar numbers.

        Runs rather than every number, because the finding this serves is about
        stretches of a tune - "bars 1-16 in every take" is a shape somebody
        recognises, and sixteen numbers in a row is a list they skip. */
    std::string runsOf (const std::vector<int>& numbers)
    {
        std::vector<std::string> parts;

        for (std::size_t i = 0; i < numbers.size(); )
        {
            auto end = i;

            while (end + 1 < numbers.size() && numbers[end + 1] == numbers[end] + 1)
                ++end;

            // Two in a row is written out: "4 and 5" reads better than "4-5"
            // and is no longer.
            if (end > i + 1)
                parts.push_back (std::to_string (numbers[i]) + "-" + std::to_string (numbers[end]));
            else
                for (auto n = i; n <= end; ++n)
                    parts.push_back (std::to_string (numbers[n]));

            i = end + 1;
        }

        return joined (parts);
    }

    LineStats& add (LineStats& into, const LineStats& more)
    {
        into.chordTones += more.chordTones;
        into.scaleTones += more.scaleTones;
        into.approachTones += more.approachTones;
        into.unresolved += more.unresolved;
        into.outside += more.outside;

        return into;
    }

    /** A tier as a percentage of what has settled, 0 when nothing has.

        Over `settled()` rather than `total()` for the reason `LineStats` gives:
        a note the line has opened and not closed is not a note that went
        wrong, and counting it in a denominator marks it as one. */
    int rateOf (int part, const LineStats& of)
    {
        const auto settled = of.settled();

        return settled > 0 ? static_cast<int> ((part * 100 + settled / 2) / settled) : 0;
    }

    /** One rate, earlier against lately, when the record is long enough for the
        difference to be a habit rather than a day.

        Six takes is the floor and the split is down the middle. Anything
        smaller compares one afternoon with another, which is weather. */
    struct Movement
    {
        std::string what;
        int wasRate {};
        int isRate {};

        int size() const noexcept { return std::abs (isRate - wasRate); }
    };
}

unsigned int qualityBit (ChordQuality quality) noexcept
{
    return 1u << static_cast<unsigned int> (quality);
}

unsigned int rootBit (PitchClass root) noexcept
{
    return 1u << static_cast<unsigned int> (toPitchClass (root));
}

void coverageOf (const Chart& chart, int fromBar, int toBar,
                 unsigned int& qualities, unsigned int& roots)
{
    qualities = 0;
    roots = 0;

    const auto first = std::max (0, fromBar);
    const auto last = std::min (chart.measureCount() - 1, toBar);

    for (auto bar = first; bar <= last; ++bar)
        for (const auto& slot : chart.measures[static_cast<std::size_t> (bar)].slots)
        {
            qualities |= qualityBit (slot.chord.quality());
            roots |= rootBit (slot.chord.root());
        }
}

PracticeReading readPractice (const std::vector<PracticeTake>& takes, int today)
{
    PracticeReading reading;

    if (takes.empty())
    {
        reading.summary = "Nothing practised yet. A take is the thing that writes a line here.";
        return reading;
    }

    auto sorted = takes;
    std::sort (sorted.begin(), sorted.end(),
               [] (const PracticeTake& a, const PracticeTake& b) { return a.day < b.day; });

    std::set<int> days;
    auto seconds = 0;
    auto chordsPlayed = 0;
    unsigned int qualities = 0;
    unsigned int roots = 0;

    for (const auto& take : sorted)
    {
        ++reading.takes;
        reading.bars += static_cast<int> (take.bars.size());
        seconds += take.seconds;
        chordsPlayed += take.chordsPlayed;
        days.insert (take.day);
        qualities |= take.qualities;
        roots |= take.roots;

        if (take.mode == PracticeMode::comping)
            ++reading.compTakes;
        else
        {
            ++reading.soloTakes;
            add (reading.notes, take.notes);
        }
    }

    reading.minutes = (seconds + 30) / 60;
    reading.daysPractised = static_cast<int> (days.size());
    reading.span = sorted.back().day - sorted.front().day + 1;
    reading.daysSinceLast = std::max (0, today - sorted.back().day);

    for (const auto& named : allQualities)
        (qualities & qualityBit (named.quality) ? reading.qualitiesMet
                                                : reading.qualitiesMissing).push_back (named.name);

    for (PitchClass root = 0; root < semitonesPerOctave; ++root)
        (roots & rootBit (root) ? reading.rootsMet
                                : reading.rootsMissing).push_back (pitchClassName (root));

    /*  The headline. Three facts and no adjective: how much, over how long, and
        when it last happened. */
    reading.summary = plural (reading.takes, "take", "takes")
                    + " over " + plural (reading.daysPractised, "day", "days");

    if (reading.minutes > 0)
        reading.summary += ", " + plural (reading.minutes, "minute", "minutes") + " playing";

    reading.summary += reading.daysSinceLast == 0
                         ? ". The last one was today."
                         : ". The last one was " + plural (reading.daysSinceLast, "day", "days") + " ago.";

    /*  Which half of the app has been getting the time.

        Only worth saying when one side is untouched: a player who has done both
        can see the two numbers, and a sentence telling them what they just did
        is a sentence that earns nothing. */
    if (reading.compTakes == 0 && reading.soloTakes > 2)
        reading.observations.push_back (
            "All of it was soloing. Comping is the other half of the same harmony - the exercise"
            " is in chord practice with the clock on, and it reads placement rather than notes.");
    else if (reading.soloTakes == 0 && reading.compTakes > 2)
        reading.observations.push_back (
            "All of it was comping. A line over the same chords is read note by note, which says"
            " different things about the same harmony.");

    /*  The coverage half, and the reason this file exists.

        What somebody has played is something they already know. What they have
        never played is the thing no single take can tell them, and the thing a
        practice record is uniquely able to. */
    if (! reading.qualitiesMissing.empty() && ! reading.qualitiesMet.empty())
    {
        const auto& missing = reading.qualitiesMissing;

        // Named individually while the list is short enough to read. Past that
        // the sentence is a wall, and what has been played is the shorter half.
        if (missing.size() <= 3)
            reading.observations.push_back (
                "Nothing yet over " + joined (missing) + " chords."
                + (qualities & qualityBit (ChordQuality::halfDiminished) ? ""
                     : " A half-diminished chord is half of every minor ii-V, so it is the one"
                       " most worth going looking for."));
        else
            reading.observations.push_back (
                "Everything so far has been over " + joined (reading.qualitiesMet)
                + " chords. The other " + std::to_string (missing.size())
                + " qualities the engine knows have not come up.");
    }

    if (reading.rootsMissing.size() >= 6)
        reading.observations.push_back (
            plural (static_cast<int> (reading.rootsMet.size()), "key", "keys") + " so far - "
            + joined (reading.rootsMet)
            + ". A tune moved to a key you have not played is the same tune and a different"
              " set of fingerings.");
    else if (reading.rootsMissing.empty())
        reading.observations.push_back (
            "Every one of the twelve roots has been under something you played.");

    /*  What has moved.

        Rates rather than counts, so a long take does not outvote a short one,
        and read over the take-wide `settled()` for the reason `LineStats`
        gives. Words either way: a line that has gone further outside has gone
        further outside, which is what reaching sounds like, and is not a thing
        this file marks anybody down for. */
    std::vector<PracticeTake> lines;

    for (const auto& take : sorted)
        if (take.mode == PracticeMode::soloing && take.notes.settled() > 0)
            lines.push_back (take);

    if (lines.size() >= 6)
    {
        const auto half = lines.size() / 2;

        LineStats earlier;
        LineStats lately;

        for (std::size_t i = 0; i < lines.size(); ++i)
            add (i < half ? earlier : lately, lines[i].notes);

        const std::array<Movement, 3> moves { {
            { "notes outside the harmony", rateOf (earlier.outside, earlier),
                                           rateOf (lately.outside, lately) },
            { "approach notes", rateOf (earlier.approachTones, earlier),
                                rateOf (lately.approachTones, lately) },
            { "chord tones", rateOf (earlier.chordTones, earlier),
                             rateOf (lately.chordTones, lately) }
        } };

        auto ranked = std::vector<Movement> (moves.begin(), moves.end());

        std::sort (ranked.begin(), ranked.end(),
                   [] (const Movement& a, const Movement& b) { return a.size() > b.size(); });

        auto said = 0;

        for (const auto& move : ranked)
        {
            // Eight points is the floor. Below it the two halves are the same
            // playing, and a record that narrates noise teaches a player to
            // ignore it.
            if (move.size() < 8 || said >= 2)
                continue;

            reading.observations.push_back (
                "Lately your line is " + std::to_string (move.isRate) + "% "
                + move.what + ", against " + std::to_string (move.wasRate)
                + "% across the earlier half of this record.");

            ++said;
        }
    }

    if (chordsPlayed > 0)
        reading.observations.push_back (
            plural (chordsPlayed, "chord", "chords") + " played inside a line, rather than one"
            " note at a time. That is a different thing to be practising and it is counted"
            " separately for that reason - never scored differently.");

    return reading;
}

TuneProgress readTuneProgress (const Chart& chart,
                               const std::vector<PracticeTake>& takes,
                               int today)
{
    TuneProgress progress;
    progress.barsInChart = chart.measureCount();

    for (auto bar = 0; bar < progress.barsInChart; ++bar)
    {
        TuneBarMemory memory;
        memory.measureIndex = bar;

        const auto& measure = chart.measures[static_cast<std::size_t> (bar)];

        if (! measure.isEmpty())
            memory.chordSymbol = measure.slots.front().chord.toString();

        progress.bars.push_back (memory);
    }

    if (takes.empty())
    {
        progress.summary = "Never played with a take running.";
        return progress;
    }

    std::set<int> days;
    auto lastDay = takes.front().day;

    for (const auto& take : takes)
    {
        ++progress.takes;
        days.insert (take.day);
        lastDay = std::max (lastDay, take.day);

        for (const auto& bar : take.bars)
        {
            // A chart edited shorter since the take was played still has a
            // record of the bars that are gone. They are dropped rather than
            // drawn: the tune on the stand is the tune this reading is about.
            if (bar.measureIndex < 0 || bar.measureIndex >= progress.barsInChart)
                continue;

            auto& memory = progress.bars[static_cast<std::size_t> (bar.measureIndex)];
            ++memory.takes;
            add (memory.notes, bar.notes);
        }
    }

    progress.daysPractised = static_cast<int> (days.size());
    progress.daysSinceLast = std::max (0, today - lastDay);

    progress.summary = plural (progress.takes, "take", "takes")
                     + " over " + plural (progress.daysPractised, "day", "days")
                     + (progress.daysSinceLast == 0
                          ? ". The last one was today."
                          : ". The last one was "
                              + plural (progress.daysSinceLast, "day", "days") + " ago.");

    /*  One take is not a habit.

        Everything below reads a pattern across takes, and a pattern needs
        something to be a pattern across. With one take every bar it reached was
        reached in "every take", which is true and says nothing at all. */
    if (progress.takes < 3)
        return progress;

    std::vector<int> always;
    std::vector<int> rarely;
    std::vector<int> never;

    // A third of the takes is the line between a bar you practise and one you
    // happen to pass through on the way to stopping.
    const auto seldom = std::max (1, progress.takes / 3);

    for (const auto& bar : progress.bars)
    {
        if (bar.takes == 0)
            never.push_back (bar.measureIndex + 1);
        else if (bar.takes == progress.takes)
            always.push_back (bar.measureIndex + 1);
        else if (bar.takes <= seldom)
            rarely.push_back (bar.measureIndex + 1);
    }

    /*  The headline finding, and the reason this function exists.

        Only said when the tune is actually lopsided - some bars in every take
        and some in hardly any. A tune played end to end every time has nothing
        to answer for and should not be handed a sentence implying it does. */
    if (! always.empty() && ! (rarely.empty() && never.empty()))
    {
        auto said = "Bar" + std::string (always.size() == 1 ? " " : "s ") + runsOf (always)
                  + (always.size() == 1 ? " has been" : " have been") + " in every take";

        if (! rarely.empty())
        {
            // The most any of them managed, not the first one's count: the
            // sentence is about a group, and quoting one member's number as if
            // it were the group's is the kind of true-ish thing a reader
            // catches and then stops trusting the rest of.
            auto most = 0;

            for (auto number : rarely)
                most = std::max (most, progress.bars[static_cast<std::size_t> (number - 1)].takes);

            said += ", " + runsOf (rarely) + " in no more than "
                  + plural (most, "take", "takes") + " of " + std::to_string (progress.takes);
        }

        said += ". Starting a take somewhere other than the top is the cheapest way to even"
                " that out.";

        progress.observations.push_back (said);
    }

    if (! never.empty())
        progress.observations.push_back (
            "Bar" + std::string (never.size() == 1 ? " " : "s ") + runsOf (never)
            + (never.size() == 1 ? " has" : " have") + " never been reached with a take running.");

    /*  The bar that keeps pulling away.

        The same reading `LineAnalyzer` makes of one take, one level out - and
        it means something different here. In one take a bar that went outside
        was a moment; in nine takes it is the bar of this tune that is actually
        difficult, which is the sentence worth having. */
    LineStats everything;

    for (const auto& bar : progress.bars)
        add (everything, bar.notes);

    if (everything.settled() >= 20)
    {
        const TuneBarMemory* worst = nullptr;

        for (const auto& bar : progress.bars)
            if (bar.takes >= 2 && bar.notes.settled() >= 8
                  && (worst == nullptr
                        || bar.notes.percentOutside() > worst->notes.percentOutside()))
                worst = &bar;

        if (worst != nullptr && worst->notes.percentOutside() >= everything.percentOutside() + 20)
            progress.observations.push_back (
                "Bar " + std::to_string (worst->measureIndex + 1)
                + (worst->chordSymbol.empty() ? "" : " (" + worst->chordSymbol + ")")
                + " is the one that keeps pulling away: "
                + std::to_string (worst->notes.percentOutside())
                + "% of what you have played there sat outside, against "
                + std::to_string (everything.percentOutside()) + "% across the tune.");
    }

    return progress;
}

} // namespace jazz::core
