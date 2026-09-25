#include "TestFramework.h"
#include "jazz/core/LineWriter.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jazz::core;

namespace
{
    Chart chartFrom (const std::string& text)
    {
        const auto result = parseProgressionText (text);
        CHECK (result.ok());
        return *result.chart;
    }

    /** The written line played back through a take, exactly as a player would.

        This is the whole point of the feature and so it is the whole point of
        these tests: the analyser has to read back what the writer wrote. Run
        through a real take rather than through `read()`, because `read()` sees
        one note with no line around it and answers `outside` for anything
        chromatic by design - an approach note only exists once the note after
        it has arrived.
    */
    std::vector<LineNote> readBack (const Chart& chart,
                                    const std::vector<WrittenNote>& line,
                                    const std::string& chosenScale = "",
                                    const std::string& style = "")
    {
        LineAnalyzer::Options options;
        options.chosenScale = chosenScale;
        options.style = style;
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
}

TEST ("a written line is read back as the line it was written as")
{
    /*  The invariant this feature lives on. Same shape as
        `VoicingAnalyzerTests`' rule that every voicing `idiomaticVoicings`
        offers must classify as the type it was offered for, and the same
        reason: otherwise the app hands you a line and then marks it wrong. */
    /*  Three charts, and neither of the last two is decoration. Four bars
        offer a lick almost nowhere to fit, so for a while this swept
        twenty-four seeds over a line that was never once quoted and reported
        that quoting round-trips perfectly. Sixteen bars is where the catalogue
        actually reaches.

        The third carries a passing diminished, a tonic minor-major, a sus vamp
        and an augmented chord - the four qualities the catalogue used to reach
        over nothing at all. Without it the licks written for them round-trip
        nowhere, and neither does a **generated** note over a symmetric chord,
        which nothing here had ever asked about either. Measured when it was
        added: 7,570 notes, 973 of them quoted, no pitch or colour
        disagreement. */
    const std::vector<Chart> charts {
        chartFrom ("| Dm7 | G7 | Cmaj7 | Cmaj7 |"),
        chartFrom ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Em7 | A7 | Dm7 | Dm7 "
                   "| Gm7 | C7 | Fmaj7 | Fmaj7 | Bm7b5 | E7 | Am7 | Am7 |"),
        chartFrom ("| Cmaj7 | C#dim7 | Dm7 | G7 | CmMaj7 | CmMaj7 | Fsus4 | Fsus4 "
                   "| C+ | C+ | Em7b5 | A7 | Dm7 | G7 | Cmaj7 | Cmaj7 |") };

    /*  Swept over every style now, not only the default. A style carries its
        own scale vocabulary, and the reader is given that same vocabulary -
        which is the whole of what `readingScaleFor` being public is for. Hand
        the reader a different one and half the line colours wrong; that is not
        a hypothetical, it is what this test caught when the styles arrived. */
    for (const auto& chart : charts)
    for (const auto& style : lineStyles())
    {
        for (std::uint32_t seed = 1; seed <= 24; ++seed)
        {
            const auto written = improvisedLine (chart, 0, chart.measureCount() - 1,
                                                 "", style.key, seed);
            CHECK (! written.empty());

            const auto read = readBack (chart, written, "", style.scaleStyle);
            CHECK_EQ (read.size(), written.size());

            for (std::size_t i = 0; i < read.size() && i < written.size(); ++i)
            {
                CHECK (read[i].midiNote == written[i].midiNote);

                /*  A **quoted** note promises less, and the promise it does
                    make is the one that matters. A documented device may be
                    deliberately outside the scale a take reads against - a
                    side-slipped cell is outside by design - so what the writer
                    guarantees is the *pitch* classification: what it wrote as
                    inside reads as inside, and what it wrote as outside reads
                    as outside. Which of the three outside colours the line
                    produces depends on what happens after the note, and that
                    is the analyser's to say.

                    Measured rather than assumed: over two hundred seeds of
                    this tune, every quoted note agreed on the pitch
                    classification and about one in forty differed on the
                    colour - all of them approach against outside, in both
                    directions, and none of them chord against scale. */
                if (! written[i].lickKey.empty())
                {
                    CHECK (read[i].colour == written[i].colour
                           || (isOutsideByPitch (read[i].colour)
                               && isOutsideByPitch (written[i].colour)));
                    continue;
                }

                CHECK (read[i].colour == written[i].colour);

                /*  The gesture is deliberately *not* asserted to be any
                    particular one. The writer commits to the colour; which kind
                    of approach a note turns out to be depends on the note after
                    it and is the analyser's to name. What is asserted is that
                    an approach note got home by some gesture rather than none. */
                if (written[i].colour == NoteColour::approach)
                    CHECK (read[i].approachKind != ApproachKind::none);
            }
        }
    }
}

TEST ("the same seed writes the same line, note for note")
{
    const auto chart = chartFrom ("| Dm7 | G7 |");
    const auto once = improvisedLine (chart, 0, 1, "", "", 7);
    const auto again = improvisedLine (chart, 0, 1, "", "", 7);
    const auto other = improvisedLine (chart, 0, 1, "", "", 8);

    CHECK_EQ (once.size(), again.size());

    for (std::size_t i = 0; i < once.size(); ++i)
    {
        CHECK (once[i].midiNote == again[i].midiNote);
        CHECK (once[i].at.inTicks() == again[i].at.inTicks());
    }

    // And a different seed is a different line, or the seed does nothing.
    auto differs = once.size() != other.size();

    for (std::size_t i = 0; i < once.size() && i < other.size(); ++i)
        if (once[i].midiNote != other[i].midiNote || once[i].at.inTicks() != other[i].at.inTicks())
            differs = true;

    CHECK (differs);
}

TEST ("a written line stays inside a soloist's register")
{
    // A tune that climbs, for the reason the comping register test uses one:
    // following the harmony upward is exactly how a generator walks off the top.
    const auto chart = chartFrom ("| Cmaj7 | Ebmaj7 | Gbmaj7 | Amaj7 | Cmaj7 | Ebmaj7 |");

    for (std::uint32_t seed = 1; seed <= 12; ++seed)
        for (const auto& note : improvisedLine (chart, 0, 5, "", "", seed))
        {
            CHECK (note.midiNote >= lowestLineNote);
            CHECK (note.midiNote <= highestLineNote);
        }
}

TEST ("the strong beats carry the chord")
{
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    for (std::uint32_t seed = 1; seed <= 12; ++seed)
        for (const auto& note : improvisedLine (chart, 0, 3, "", "", seed))
        {
            if (! isStrong (note.at, 4))
                continue;

            /*  A quoted note is not held to this, and that is the decision
                rather than a let-off. It is a rule about what the *generator*
                may invent - see `lineFaults`, which draws the same line - and
                a documented device may break it on purpose: L03 side-slips a
                whole cell over the V and lands the first note of it, a b13, on
                beat one. Marking that would be marking Coltrane. What the app
                does instead is read it back honestly: that note was outside,
                and here is where it landed. */
            if (! note.lickKey.empty())
                continue;

            /*  A strong beat is a chord tone, unless it is the note approaching
                the next bar - which is the one place a line is allowed to be
                outside on a strong beat, and is the whole gesture. */
            CHECK (note.colour == NoteColour::chordTone
                   || note.colour == NoteColour::approach);
        }
}

TEST ("a line writes nothing outside the bars it was asked for")
{
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    for (const auto& note : improvisedLine (chart, 1, 2, "", "", 3))
    {
        CHECK (note.measureIndex >= 1);
        CHECK (note.measureIndex <= 2);
        CHECK (note.at.beat >= 0);
        CHECK (note.at.beat < 4);
    }

    // Past the end of the chart is an empty line rather than a crash or a
    // bar that does not exist.
    CHECK (improvisedLine (chart, 9, 12, "", "", 3).empty());
    CHECK (improvisedLine (chart, -1, 2, "", "", 3).empty());
}

TEST ("a scale the player chose is the scale the line is built from")
{
    /*  The reason `readingScaleFor` is public. A line written against the
        engine's first answer and read against the player's choice colours its
        own notes wrong - so both ends ask the same question. */
    const auto chart = chartFrom ("| Dm7 | Dm7 |");

    // "modal" is the line style; "modes" is the scale vocabulary it draws on,
    // and the reader is given that rather than the style's own key.
    const auto& style = lineStyleFor ("modal");
    CHECK_EQ (style.scaleStyle, std::string ("modes"));

    const auto written = improvisedLine (chart, 0, 1, "D Dorian", style.key, 5);

    CHECK (! written.empty());

    const auto read = readBack (chart, written, "D Dorian", style.scaleStyle);
    CHECK_EQ (read.size(), written.size());

    for (std::size_t i = 0; i < read.size() && i < written.size(); ++i)
        CHECK (read[i].colour == written[i].colour);
}

TEST ("a waltz is written in three")
{
    // The metre is the chart's, not the progression text's - so it is set the
    // way `CompingTests` sets one rather than spelled into the bars.
    auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 |");
    chart.timeSignature.numerator = 3;

    const auto written = improvisedLine (chart, 0, 2, "", "", 4);

    CHECK (! written.empty());

    for (const auto& note : written)
        CHECK (note.at.beat < 3);
}

//==============================================================================
// The phrase planner, and the constraints a written line is held to.

TEST ("a line is phrases and rests, not a grid with holes in it")
{
    /*  The pitfall the research names by name: "every phrase exactly 1, 2 or 4
        bars long and starting on beat 1". That was a fair description of the
        version of this file that laid a full eighth grid over each bar and
        thinned it with an independent coin flip per slot - which gives a
        texture, not phrasing. */
    const auto& style = lineStyleFor ("bebop");
    const auto planned = planPhrases (style, 0, 7, 4, 11);

    CHECK (! planned.empty());

    auto phrases = 0;
    auto longest = 0;
    auto shortest = 1000;
    auto inThisOne = 0;

    for (const auto& note : planned)
    {
        ++inThisOne;

        if (note.endsPhrase)
        {
            ++phrases;
            longest = std::max (longest, inThisOne);
            shortest = std::min (shortest, inThisOne);
            inThisOne = 0;
        }
    }

    CHECK (phrases >= 2);

    // Every phrase inside the style's own range, give or take the last one,
    // which is cut off by the end of the range rather than by choice.
    CHECK (longest <= style.longestPhrase);
    CHECK (shortest >= 1);
}

TEST ("a phrase leaves silence after it, rather than running into the next")
{
    const auto& style = lineStyleFor ("blues");
    const auto planned = planPhrases (style, 0, 7, 4, 3);

    CHECK (planned.size() >= 2);

    const auto step = ticksFor (style.feel);
    auto gapsFound = 0;

    for (std::size_t i = 1; i < planned.size(); ++i)
    {
        const auto before = planned[i - 1].measureIndex * 4 * ticksPerBeat
                          + planned[i - 1].at.inTicks();
        const auto now = planned[i].measureIndex * 4 * ticksPerBeat
                       + planned[i].at.inTicks();

        // Always forwards, and never two notes in one place.
        CHECK (now > before);

        if (planned[i - 1].endsPhrase)
        {
            CHECK (now - before >= style.shortestRest);
            ++gapsFound;
        }
        else
        {
            CHECK_EQ (now - before, step);
        }
    }

    CHECK (gapsFound >= 1);
}

TEST ("phrases start where the style says they start")
{
    /*  R14, as a bias rather than a rule: bebop's phrases begin on the "and"
        most of the time. Asserted over several seeds because one seed says
        nothing about a weighting. */
    const auto& style = lineStyleFor ("bebop");
    CHECK_EQ (style.startTicks.front(), ticksPerBeat / 2);

    auto offTheBeat = 0;
    auto starts = 0;

    for (std::uint32_t seed = 1; seed <= 12; ++seed)
    {
        const auto planned = planPhrases (style, 0, 7, 4, seed);
        auto newPhrase = true;

        for (const auto& note : planned)
        {
            if (newPhrase)
            {
                ++starts;
                if (note.at.tick != 0) ++offTheBeat;
            }

            newPhrase = note.endsPhrase;
        }
    }

    CHECK (starts >= 12);
    CHECK (offTheBeat * 2 > starts);   // most of them, which is what a bias is
}

TEST ("a waltz is planned in three, and nothing lands on a beat it has not got")
{
    const auto planned = planPhrases (lineStyleFor ("bebop"), 0, 5, 3, 9);

    CHECK (! planned.empty());

    for (const auto& note : planned)
    {
        CHECK (note.at.beat >= 0);
        CHECK (note.at.beat < 3);
        CHECK (note.measureIndex >= 0);
        CHECK (note.measureIndex <= 5);
    }
}

TEST ("every style writes a line its own rules accept")
{
    /*  The hard constraints, run as the research recommends - as a validator
        over what the generator produced. The same shape as CompingTests'
        "everything the generator plays is in the style it was asked for":
        without it the app writes a line in a style and then marks that line
        out of style. */
    /*  Two charts: a major ii-V, and one built out of the four qualities the
        catalogue reaches by way of L18-L22. The second is here because a
        quoted note is exempt from the grid, approach and chromatic rules and a
        **generated** one beside it is not - so a chart whose harmony is
        symmetric is where the atom writer is most likely to drift out of the
        style it claims to play, and until L18 arrived nothing asked it to
        write over one. */
    const std::vector<Chart> charts {
        chartFrom ("| Dm7 | G7 | Cmaj7 | A7 | Dm7 | G7 | Cmaj7 | Cmaj7 |"),
        chartFrom ("| Cmaj7 | C#dim7 | Dm7 | G7 | CmMaj7 | CmMaj7 | Fsus4 | Fsus4 "
                   "| C+ | C+ | Em7b5 | A7 | Dm7 | G7 | Cmaj7 | Cmaj7 |") };

    for (const auto& chart : charts)
    for (const auto& style : lineStyles())
    {
        for (std::uint32_t seed = 1; seed <= 24; ++seed)
        {
            const auto written = improvisedLine (chart, 0, chart.measureCount() - 1,
                                                 "", style.key, seed);
            CHECK (! written.empty());

            const auto faults = lineFaults (written, style, 4);

            if (! faults.empty())
                CHECK_EQ (style.key + ": " + faults.front().message, std::string ("no faults"));

            CHECK (faults.empty());
        }
    }
}

TEST ("and the validator is not a function that always says yes")
{
    /*  The negative control. An invariant that only ever asserts "nothing
        wrong" is satisfied by a checker that finds nothing, so each fault gets
        a line built to trip it. */
    const auto& style = lineStyleFor ("bebop");

    const auto note = [] (int midi, int beat, int tick, NoteColour colour)
    {
        WrittenNote written;
        written.midiNote = midi;
        written.at = { beat, tick };
        written.colour = colour;
        written.chordSymbol = "Dm7";
        return written;
    };

    // Off the top of the register.
    CHECK (! lineFaults ({ note (120, 0, 0, NoteColour::chordTone) }, style, 4).empty());
    CHECK (lineFaults ({ note (120, 0, 0, NoteColour::chordTone) }, style, 4).front().fault == LineFault::outsideTheRegister);

    // On a tick an eighth-note style has no subdivision for.
    CHECK (lineFaults ({ note (60, 0, 5, NoteColour::chordTone) }, style, 4).front().fault == LineFault::offTheStyleGrid);

    // A chromatic on the downbeat, which is R1's whole point.
    const std::vector<WrittenNote> onTheBeat {
        note (61, 0, 0, NoteColour::approach), note (60, 0, ticksPerBeat / 2, NoteColour::chordTone) };

    CHECK (lineFaults (onTheBeat, style, 4).front().fault == LineFault::chromaticOnTheBeat);

    // An approach that goes nowhere, and one that repeats itself - which is
    // not a step either, and is the bug the blues style actually hit.
    const std::vector<WrittenNote> stranded {
        note (61, 0, ticksPerBeat / 2, NoteColour::approach), note (72, 1, 0, NoteColour::chordTone) };

    CHECK (lineFaults (stranded, style, 4).front().fault == LineFault::approachThatNeverLands);

    const std::vector<WrittenNote> repeated {
        note (61, 0, ticksPerBeat / 2, NoteColour::approach), note (61, 1, 0, NoteColour::chordTone) };

    CHECK (lineFaults (repeated, style, 4).front().fault == LineFault::approachThatNeverLands);

    // And a line that obeys really does come back clean, or the four above
    // would be passing for the wrong reason.
    const std::vector<WrittenNote> good {
        note (61, 0, ticksPerBeat / 2, NoteColour::approach), note (60, 1, 0, NoteColour::chordTone) };

    CHECK (lineFaults (good, style, 4).empty());
}

TEST ("a modal line has nothing chromatic in it at all")
{
    /*  `usesApproaches` is a musical claim, not a simplification: in a modal
        line a note from outside the mode is a wrong note rather than colour on
        the way somewhere. So this is the one thing that must differ between
        the styles no matter what the seed does. */
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 | A7 |");
    CHECK (! lineStyleFor ("modal").usesApproaches);
    CHECK (lineStyleFor ("bebop").usesApproaches);

    auto modalApproaches = 0;
    auto bebopApproaches = 0;

    for (std::uint32_t seed = 1; seed <= 24; ++seed)
    {
        for (const auto& note : improvisedLine (chart, 0, 3, "", "modal", seed))
            if (note.colour == NoteColour::approach) ++modalApproaches;

        for (const auto& note : improvisedLine (chart, 0, 3, "", "bebop", seed))
            if (note.colour == NoteColour::approach) ++bebopApproaches;
    }

    CHECK_EQ (modalApproaches, 0);
    CHECK (bebopApproaches > 0);   // or the comparison above proves nothing
}
