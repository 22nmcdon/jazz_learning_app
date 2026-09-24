#include "TestFramework.h"
#include "jazz/core/LinePlacement.h"
#include "jazz/core/LineWriter.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace jazz::core;

namespace
{
    Chart chartOf (const std::string& progression, int beatsPerBar = 4)
    {
        auto parsed = parseProgressionText (progression);
        CHECK (parsed.ok());

        auto built = *parsed.chart;
        built.timeSignature.numerator = beatsPerBar;

        // The parser fills a bar's chords out to four beats because that is
        // what the text means; a waltz's bars have to be told.
        if (beatsPerBar != 4)
            for (auto& measure : built.measures)
                for (auto& slot : measure.slots)
                    slot.beats = beatsPerBar / static_cast<int> (measure.slots.size());

        return built;
    }

    /** A written line played back through a real take, as `LineWriterTests`
        does it - the placement reading consumes what a take produces, so it
        has to be given one rather than a list of `WrittenNote`s. */
    std::vector<LineNote> readBack (const Chart& chart,
                                    const std::vector<WrittenNote>& line,
                                    const std::string& scaleStyle)
    {
        LineAnalyzer::Options options;
        options.style = scaleStyle;
        options.beatsPerBar = chart.timeSignature.numerator;

        LineAnalyzer analyzer;
        analyzer.setOptions (options);
        analyzer.startTake();

        auto bar = -1;

        for (const auto& note : line)
        {
            if (note.measureIndex != bar)
            {
                bar = note.measureIndex;

                if (const auto* chord = chart.chordAt (bar))
                    analyzer.setTarget (bar, *chord);
            }

            analyzer.play (note.midiNote, note.at);
        }

        analyzer.endTake();
        return analyzer.notes();
    }

    /** The same, with every note's position thrown away - a take played with
        no clock, which is what Static practice sends. */
    std::vector<LineNote> readBackWithNoClock (const Chart& chart,
                                               const std::vector<WrittenNote>& line,
                                               const std::string& scaleStyle)
    {
        LineAnalyzer::Options options;
        options.style = scaleStyle;

        LineAnalyzer analyzer;
        analyzer.setOptions (options);
        analyzer.startTake();

        auto bar = -1;

        for (const auto& note : line)
        {
            if (note.measureIndex != bar)
            {
                bar = note.measureIndex;

                if (const auto* chord = chart.chordAt (bar))
                    analyzer.setTarget (bar, *chord);
            }

            analyzer.play (note.midiNote);
        }

        analyzer.endTake();
        return analyzer.notes();
    }

    const std::string tune = "| Dm7 | G7 | Cmaj7 | Cmaj7 "
                             "| Em7 | A7 | Dm7 | Dm7 "
                             "| Gm7 | C7 | Fmaj7 | Fmaj7 "
                             "| Bm7b5 | E7 | Am7 | Am7 |";
}

//==============================================================================
TEST ("the reading marks the generator's own line a perfect fit")
{
    /*  The headline invariant, and the same one `CompingTests` holds the
        comping evaluator to: everything the writer writes in a style has to
        come back out of here fitting that style. A failure means the writer
        and the reading have become two descriptions of one thing, not that
        the reading is strict.

        Swept over the metre as well as the seed, because a bar's tick count
        is the metre's and the phrase boundaries are measured in absolute
        ticks across barlines. */
    for (const auto beatsPerBar : { 4, 3, 5 })
    {
        const auto chart = chartOf (tune, beatsPerBar);

        for (const auto& style : lineStyles())
            for (std::uint32_t seed = 1; seed <= 24; ++seed)
            {
                const auto written = improvisedLine (chart, 0, 15, "", style.key, seed);
                CHECK (! written.empty());

                const auto line = readBack (chart, written, style.scaleStyle);
                const auto reading = readLinePlacement (line, style, beatsPerBar);

                CHECK_EQ (reading.gridFit, 100);
                CHECK_EQ (reading.phraseFit, 100);
                CHECK_EQ (reading.registerFit, 100);
                CHECK (reading.fit.has_value());
                CHECK_EQ (*reading.fit, 100);
            }
    }
}

TEST ("the phrases read back are the phrases that were planned")
{
    /*  The structural half of the invariant above. A fit of 100 could be had
        by a reading that found one enormous phrase and happened not to mind,
        so the boundaries are checked against the planner's own - `endsPhrase`
        is `planPhrases` answering the same question, before a single note has
        been chosen.

        Driven off `planPhrases` rather than off `improvisedLine` because that
        is the only way to have both answers: a `WrittenNote` carries no phrase
        mark, deliberately, since by then the phrase is a fact about the line
        rather than about any note in it. */
    const auto chart = chartOf (tune);

    for (const auto& style : lineStyles())
        for (std::uint32_t seed = 1; seed <= 24; ++seed)
        {
            const auto planned = planPhrases (style, 0, 15, 4, seed);
            CHECK (! planned.empty());

            std::vector<WrittenNote> written;

            for (const auto& slot : planned)
            {
                WrittenNote note;
                note.measureIndex = slot.measureIndex;
                note.at = slot.at;

                // Any pitch inside the register: this test is about where the
                // notes fell, and the colours are the round trip's business.
                note.midiNote = 60;
                written.push_back (note);
            }

            const auto reading = readLinePlacement (readBack (chart, written, style.scaleStyle),
                                                    style, 4);

            const auto ended = std::count_if (planned.begin(), planned.end(),
                                              [] (const PlannedNote& slot)
                                              { return slot.endsPhrase; });

            CHECK_EQ (static_cast<int> (reading.phrases.size()), static_cast<int> (ended));

            // And the boundaries themselves, not only how many there were.
            auto at = std::size_t { 0 };

            for (const auto& phrase : reading.phrases)
            {
                CHECK_EQ (phrase.firstNote, at);
                at += static_cast<std::size_t> (phrase.onsets);
                CHECK (at > 0 && at <= planned.size());
                CHECK (planned[at - 1].endsPhrase);
            }

            CHECK_EQ (at, planned.size());
        }
}

TEST ("every phrase the writer writes is a length its style writes")
{
    const auto chart = chartOf (tune);

    for (const auto& style : lineStyles())
        for (std::uint32_t seed = 1; seed <= 24; ++seed)
        {
            const auto written = improvisedLine (chart, 0, 15, "", style.key, seed);
            const auto reading = readLinePlacement (readBack (chart, written, style.scaleStyle),
                                                    style, 4);

            // A sixteen-bar line breathes at least once in every style there
            // is - which is the thing the generator did not do before the
            // phrase planner, and the reason it exists.
            CHECK (reading.phrases.size() > 1);

            auto counted = 0;

            for (std::size_t i = 0; i < reading.phrases.size(); ++i)
            {
                const auto& phrase = reading.phrases[i];
                counted += phrase.onsets;

                /*  A quoted phrase is the length the source made it, not the
                    length this style's planner draws. L09 is a two-note bebop
                    ending and bebop's shortest planned phrase is six; the
                    ending is right and the range is about what the *planner*
                    invents. Skipped rather than loosened, so the rule still
                    binds on every phrase the writer wrote itself. */
                const auto isQuoted = std::any_of (written.begin() + static_cast<long> (phrase.firstNote),
                                                   written.begin() + static_cast<long> (phrase.lastNote) + 1,
                                                   [] (const WrittenNote& note)
                                                   { return ! note.lickKey.empty(); });

                if (isQuoted)
                    continue;

                /*  Only the long end holds for every phrase. The last one is
                    cut off by the end of the chart rather than by a rest -
                    `planPhrases` stops laying notes down when it runs out of
                    bars - so a line's final phrase can be shorter than the
                    style's shortest and that is the line ending, not the
                    style being broken. It is also the one direction the
                    number does not read, for the same reason. */
                if (i + 1 < reading.phrases.size())
                    CHECK (phrase.onsets >= style.shortestPhrase);

                CHECK (phrase.onsets <= style.longestPhrase);
                CHECK_EQ (phrase.overBy, 0);
            }

            CHECK_EQ (counted, static_cast<int> (written.size()));
        }
}

//==============================================================================
// The negative controls. An invariant that only ever asserts "yes" is
// satisfied by a function that always says yes, so each of the three parts is
// made to fail on its own.

TEST ("a line between the style's subdivisions is off its grid")
{
    const auto chart = chartOf (tune);
    const auto& bebop = lineStyleFor ("bebop");

    auto written = improvisedLine (chart, 0, 15, "", bebop.key, 7);
    CHECK (! written.empty());

    const auto clean = readLinePlacement (readBack (chart, written, bebop.scaleStyle), bebop, 4);
    CHECK_EQ (clean.gridFit, 100);

    // A sixteenth off the beat, which is a real subdivision and not this
    // style's - the "you are playing this like a different feel" mismatch.
    for (auto& note : written)
        note.at.tick += ticksPerBeat / 4;

    const auto shifted = readLinePlacement (readBack (chart, written, bebop.scaleStyle), bebop, 4);

    CHECK_EQ (shifted.gridFit, 0);
    CHECK_EQ (shifted.onsetsOnTheGrid, 0);
    CHECK (shifted.fit.has_value());
    CHECK (*shifted.fit < *clean.fit);

    // And it is said as well as scored.
    CHECK (std::any_of (shifted.observations.begin(), shifted.observations.end(),
                        [] (const std::string& line)
                        { return line.find ("fell between") != std::string::npos; }));
}

TEST ("a line outside the style's register costs the register and nothing else")
{
    const auto chart = chartOf (tune);
    const auto& bebop = lineStyleFor ("bebop");

    auto written = improvisedLine (chart, 0, 15, "", bebop.key, 11);

    /*  Three octaves, not two. Two was enough while every line was generated
        into the middle of the register; a quoted phrase is placed as a whole,
        so a wide lick is forced to the bottom of the register to fit and two
        octaves up is still inside it. Three is above the top of any style's
        register from anywhere inside it. */
    for (auto& note : written)
        note.midiNote += 36;

    const auto reading = readLinePlacement (readBack (chart, written, bebop.scaleStyle), bebop, 4);

    CHECK_EQ (reading.notesInRegister, 0);
    CHECK_EQ (reading.registerFit, 0);

    // The other two are untouched: moving a line bodily up two octaves does
    // not change where any of its notes fell, or how they were parcelled.
    CHECK_EQ (reading.gridFit, 100);
    CHECK_EQ (reading.phraseFit, 100);
}

TEST ("a line that never takes a breath is one phrase, and it costs")
{
    const auto chart = chartOf (tune);
    const auto& blues = lineStyleFor ("blues");

    /*  Running eighths from the downbeat for sixteen bars - the pitfall the
        research names, written out. Every note is on the grid and in the
        register, so this isolates the phrasing: nothing else can move. */
    std::vector<WrittenNote> written;

    for (auto bar = 0; bar < 16; ++bar)
        for (auto eighth = 0; eighth < 8; ++eighth)
        {
            WrittenNote note;
            note.measureIndex = bar;
            note.at = BarPosition::fromTicks (eighth * (ticksPerBeat / 2));
            note.midiNote = 60 + (eighth % 5);
            written.push_back (note);
        }

    const auto reading = readLinePlacement (readBack (chart, written, blues.scaleStyle), blues, 4);

    CHECK_EQ (static_cast<int> (reading.phrases.size()), 1);
    CHECK_EQ (reading.gridFit, 100);
    CHECK_EQ (reading.registerFit, 100);
    CHECK_EQ (reading.phraseFit, 0);

    CHECK (std::any_of (reading.observations.begin(), reading.observations.end(),
                        [] (const std::string& line)
                        { return line.find ("ran past") != std::string::npos; }));
}

TEST ("a phrase shorter than the style's shortest costs nothing")
{
    /*  The one-way rule, and the reason it is one-way: stopping early is a
        player leaving space, which is the same argument `CompBarReading`
        makes about a bar left alone. Four notes against blues' floor of
        three is inside it; two is under, and still full marks. */
    const auto chart = chartOf (tune);
    const auto& blues = lineStyleFor ("blues");

    std::vector<WrittenNote> written;

    for (auto bar = 0; bar < 8; ++bar)
        for (auto eighth = 0; eighth < 2; ++eighth)
        {
            WrittenNote note;
            note.measureIndex = bar;
            note.at = BarPosition::fromTicks (eighth * (ticksPerBeat / 2));
            note.midiNote = 60 + eighth;
            written.push_back (note);
        }

    const auto reading = readLinePlacement (readBack (chart, written, blues.scaleStyle), blues, 4);

    CHECK_EQ (static_cast<int> (reading.phrases.size()), 8);

    for (const auto& phrase : reading.phrases)
    {
        CHECK_EQ (phrase.onsets, 2);
        CHECK (phrase.onsets < blues.shortestPhrase);
        CHECK_EQ (phrase.overBy, 0);
    }

    CHECK_EQ (reading.phraseFit, 100);
}

//==============================================================================
TEST ("a take with no clock behind it gets no number at all")
{
    /*  Empty rather than zero, the distinction `LineStats` draws between
        nothing and zero and `CompEvaluation::fit` keeps on the wire. A page
        drawing a nought would say exactly the thing this was careful not to.  */
    const auto chart = chartOf (tune);
    const auto& bebop = lineStyleFor ("bebop");

    const auto written = improvisedLine (chart, 0, 15, "", bebop.key, 3);
    const auto line = readBackWithNoClock (chart, written, bebop.scaleStyle);

    CHECK (! line.empty());

    const auto reading = readLinePlacement (line, bebop, 4);

    CHECK (! reading.fit.has_value());
    CHECK_EQ (reading.onsetsPlaced, 0);
    CHECK_EQ (reading.onsets, static_cast<int> (line.size()));
    CHECK (reading.phrases.empty());

    // The notes are still there to be counted, and the register still reads -
    // it is the fit that has nothing to stand on.
    CHECK_EQ (reading.notes, static_cast<int> (line.size()));
    CHECK_EQ (reading.registerFit, 100);
    CHECK (reading.summary.find ("Nothing was counting") != std::string::npos);
}

TEST ("nothing played is nothing read")
{
    const auto reading = readLinePlacement ({}, lineStyleFor ("bebop"), 4);

    CHECK (! reading.fit.has_value());
    CHECK_EQ (reading.onsets, 0);
    CHECK (reading.phrases.empty());
    CHECK (reading.observations.empty());
}

TEST ("a chord in the line is one onset and several notes")
{
    /*  Placement is a fact about a moment and register is a fact about a
        note, so a block chord counts once for where it fell and once per
        voice for where it sat. Counting its voices separately would weight a
        chordal player's placement four times as heavily. */
    const auto chart = chartOf ("| Dm7 | Dm7 |");

    LineAnalyzer::Options options;
    options.beatsPerBar = 4;

    LineAnalyzer analyzer;
    analyzer.setOptions (options);
    analyzer.startTake();
    analyzer.setTarget (0, *chart.chordAt (0));

    /*  One four-note voicing on the downbeat, then a single note an eighth
        later. An eighth rather than a beat because bebop's shortest rest is
        an eighth, so a beat of silence between two onsets reads as a rest -
        which is right, and is not what this test is about. */
    analyzer.play (62, BarPosition { 0, 0 });
    analyzer.play (65, BarPosition { 0, 0 }, Attack::withPrevious);
    analyzer.play (69, BarPosition { 0, 0 }, Attack::withPrevious);
    analyzer.play (72, BarPosition { 0, 0 }, Attack::withPrevious);
    analyzer.play (74, BarPosition { 0, ticksPerBeat / 2 });
    analyzer.endTake();

    const auto reading = readLinePlacement (analyzer.notes(), lineStyleFor ("bebop"), 4);

    CHECK_EQ (reading.notes, 5);
    CHECK_EQ (reading.onsets, 2);
    CHECK_EQ (reading.onsetsPlaced, 2);
    CHECK_EQ (static_cast<int> (reading.placements.size()), 2);
    CHECK_EQ (static_cast<int> (reading.phrases.size()), 1);
    CHECK_EQ (reading.phrases.front().onsets, 2);
}

TEST ("the strong beats a bar has always counted are on the reading")
{
    /*  `LineBar` has counted these since the grid arrived and nothing could
        see them. Words, here as there - none of this is in `fit`. */
    const auto chart = chartOf ("| Dm7 | Dm7 |");

    LineAnalyzer::Options options;
    options.beatsPerBar = 4;

    LineAnalyzer analyzer;
    analyzer.setOptions (options);
    analyzer.startTake();
    analyzer.setTarget (0, *chart.chordAt (0));

    analyzer.play (62, BarPosition { 0, 0 });                  // D, the root, on one
    analyzer.play (64, BarPosition { 1, 0 });                  // E, a scale tone, on two
    analyzer.play (65, BarPosition { 2, 0 });                  // F, a chord tone, on three
    analyzer.play (67, BarPosition { 3, 0 });                  // G, a scale tone, on four
    analyzer.endTake();

    const auto reading = readLinePlacement (analyzer.notes(), lineStyleFor ("bebop"), 4);

    // In four, one and three are the strong beats - `strengthAt`'s answer,
    // and the reason a waltz is not a special case anywhere here.
    CHECK_EQ (reading.notesOnStrongBeats, 2);
    CHECK_EQ (reading.chordTonesOnStrongBeats, 2);

    // And the same bar, asked of the analyser, says the same thing - the two
    // readings share one accumulation on purpose.
    CHECK_EQ (analyzer.barFor (0).notesOnStrongBeats, 2);
    CHECK_EQ (analyzer.barFor (0).chordTonesOnStrongBeats, 2);
}

TEST ("a bar asked mid-take carries what a LineStats cannot hold")
{
    /*  The gap `barFor` closes: until it existed a shell could have a bar's
        tiers while the take was running and had to wait for `summary()` for
        everything else about that bar. */
    const auto chart = chartOf ("| Dm7 | G7 |");

    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, *chart.chordAt (0));

    analyzer.play (62);
    analyzer.play (65);
    analyzer.play (69);

    const auto bar = analyzer.barFor (0);

    CHECK_EQ (bar.measureIndex, 0);
    CHECK_EQ (bar.chordSymbol, std::string ("Dm7"));
    CHECK_EQ (bar.stats.chordTones, 3);
    CHECK (bar.neverLeftTheChord);

    // A bar nothing was played over is a real answer rather than an error.
    const auto untouched = analyzer.barFor (1);
    CHECK_EQ (untouched.stats.total(), 0);
    CHECK (! untouched.neverLeftTheChord);
    CHECK (untouched.chordSymbol.empty());
}

TEST ("the number is the style's own rules and nothing the engine merely believes")
{
    /*  The line that keeps this explainable. `startTicks` is a bias - the
        phrase planner says so in as many words - so a line that began every
        phrase on the beat is told about it and not marked for it. Same shape
        as the approach-note rule: `lineFaults` refuses a chromatic on the
        beat, and that is the engine's rule about its own generator rather
        than something the style put its name to. */
    const auto chart = chartOf (tune);
    const auto& bebop = lineStyleFor ("bebop");

    std::vector<WrittenNote> written;

    for (auto bar = 0; bar < 12; ++bar)
    {
        // Four notes from the downbeat, then most of the bar off - every
        // phrase starting on a beat, which is not one of bebop's own ticks.
        for (auto eighth = 0; eighth < 4; ++eighth)
        {
            WrittenNote note;
            note.measureIndex = bar;
            note.at = BarPosition::fromTicks (eighth * (ticksPerBeat / 2));
            note.midiNote = 62 + eighth;
            written.push_back (note);
        }
    }

    const auto reading = readLinePlacement (readBack (chart, written, bebop.scaleStyle), bebop, 4);

    CHECK_EQ (static_cast<int> (reading.phrases.size()), 12);

    for (const auto& phrase : reading.phrases)
    {
        CHECK (phrase.startsAt.onTheBeat());

        /*  And the beat is on bebop's list - as its *second* choice, which is
            the whole reason the flag is not the interesting half. A style
            weights its start ticks; it does not rule any of them out. What is
            worth telling a player is that they used one of them every single
            time, and that is the observation below rather than the flag. */
        CHECK (phrase.startedOnAStyleTick);
    }

    // Said, and not scored: all three parts are full marks.
    CHECK_EQ (reading.gridFit, 100);
    CHECK_EQ (reading.phraseFit, 100);
    CHECK_EQ (reading.registerFit, 100);
    CHECK_EQ (*reading.fit, 100);

    CHECK (std::any_of (reading.observations.begin(), reading.observations.end(),
                        [] (const std::string& line)
                        { return line.find ("began on a beat") != std::string::npos; }));
}

TEST ("an unknown style key reads against the first one rather than nothing")
{
    // The same forgiveness `lineStyleFor` gives the writer, for the same
    // reason: a stored key this version has renamed should still read.
    CHECK_EQ (lineStyleFor ("a style nobody wrote").key, lineStyles().front().key);
}
