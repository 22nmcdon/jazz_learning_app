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

} // namespace jazz::core
