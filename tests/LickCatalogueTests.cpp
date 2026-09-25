#include "TestFramework.h"
#include "jazz/core/Chart.h"
#include "jazz/core/LickCatalogue.h"
#include "jazz/core/LineStyle.h"
#include "jazz/core/LineWriter.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace jazz::core;

namespace
{
    /** Every note of a lick, sounded with its first chord's root at @p root. */
    std::vector<int> pitchesOf (const LickDefinition& lick, int root)
    {
        std::vector<int> sounded;

        for (const auto& note : lick.notes)
            sounded.push_back (lick.midiFor (note, root));

        return sounded;
    }
}

TEST ("every lick is filled in, and says who it came from")
{
    CHECK (! licks().empty());

    for (const auto& lick : licks())
    {
        CHECK (! lick.key.empty());
        CHECK (! lick.name.empty());
        CHECK (! lick.summary.empty());
        CHECK (! lick.notes.empty());
        CHECK (! lick.chords.empty());

        /*  Nobody behind it means it is a pattern rather than a lick, and the
            catalogue is for the ones with somebody behind them - the whole
            reason it was built rather than more atoms added to the generator. */
        CHECK (! lick.attribution.empty());

        CHECK (lick.weight > 0 && lick.weight <= 100);
    }
}

TEST ("a lick's keys are its own")
{
    std::set<std::string> seen;

    for (const auto& lick : licks())
    {
        CHECK (seen.find (lick.key) == seen.end());
        seen.insert (lick.key);
    }
}

TEST ("every note belongs to a chord the lick has")
{
    for (const auto& lick : licks())
        for (const auto& note : lick.notes)
        {
            CHECK (note.chordIndex >= 0);
            CHECK (note.chordIndex < static_cast<int> (lick.chords.size()));
        }
}

TEST ("a lick's chords are laid end to end from its start")
{
    /*  Contiguous, because the matcher walks them against the chart's bars in
        order: a gap would be a chord the lick has an opinion about and no
        notes over, which is a different kind of thing. */
    for (const auto& lick : licks())
    {
        auto at = 0;

        for (const auto& chord : lick.chords)
        {
            CHECK_EQ (chord.startTick, at);
            CHECK (chord.lengthTicks > 0);
            at += chord.lengthTicks;
        }
    }
}

TEST ("every note falls inside the chords it is written over")
{
    for (const auto& lick : licks())
    {
        auto covered = 0;

        for (const auto& chord : lick.chords)
            covered += chord.lengthTicks;

        for (const auto& note : lick.notes)
        {
            // A pickup reaches back before the first chord, and nothing else does.
            CHECK (note.tick >= 0 || lick.startsOnAPickup);
            CHECK (note.tick < covered);
            CHECK (note.lengthTicks > 0);
        }
    }
}

TEST ("a lick that reaches back before its first chord says so")
{
    /*  Both ways round. A lick with a pickup that does not declare one would
        be placed on the first bar of a line and lose its lead-in; one that
        declares a pickup it does not have would be refused the first bar for
        nothing. */
    for (const auto& lick : licks())
    {
        const auto reachesBack = std::any_of (lick.notes.begin(), lick.notes.end(),
                                              [] (const LickNote& note) { return note.tick < 0; });

        CHECK_EQ (reachesBack, lick.startsOnAPickup);
    }
}

TEST ("a lick's notes are in the order they are played")
{
    for (const auto& lick : licks())
        for (std::size_t i = 1; i < lick.notes.size(); ++i)
            CHECK (lick.notes[i].tick >= lick.notes[i - 1].tick);
}

TEST ("every note of a lick sits on that lick's own subdivision")
{
    /*  Ornaments included, and that is the rule rather than an oversight: an
        ornament is a rendering hint and never a position, so a crush is
        written at its target's tick and struck early by whoever plays it -
        the same split `docs/RHYTHM.md` draws for swing. Written early instead
        it is off every grid there is, and `readLinePlacement`, which reads a
        player's take and knows nothing about ornaments, marked a line down for
        a grid it was never off. */
    for (const auto& lick : licks())
    {
        const auto step = ticksFor (lick.feel);
        CHECK (step > 0);

        for (const auto& note : lick.notes)
            CHECK_EQ (note.tick % step, 0);
    }
}

TEST ("every lick names line styles the engine actually has")
{
    /*  The same shape as `LineStyleTests`' rule about scale vocabularies, and
        for the same reason: a lick tagged on a style nobody has is a lick
        nothing can ever draw, which is exactly the hole `brazilian` left in
        `ReharmStyle` by being tagged on no rule at all. */
    for (const auto& lick : licks())
    {
        CHECK (! lick.styles.empty());

        for (const auto& key : lick.styles)
        {
            const auto known = std::any_of (lineStyles().begin(), lineStyles().end(),
                                            [&key] (const LineStyleDefinition& style)
                                            { return style.key == key; });

            CHECK (known);
        }
    }
}

TEST ("the styles that quote have something to quote")
{
    /*  Coverage, counted rather than assumed. It is deliberately not asserted
        for every style: the research's catalogue is ii-V heavy, so `modal` and
        `pentatonic` have one lick between them and `bebop` has most of them.
        That is a real gap and it is recorded in docs/HANDOFF.md rather than
        papered over here - what this holds is that the two styles the
        catalogue was written for can actually reach it. */
    for (const auto& key : { "bebop", "blues" })
    {
        const auto forThisStyle = std::count_if (licks().begin(), licks().end(),
                                                 [&key] (const LickDefinition& lick)
                                                 {
                                                     return std::find (lick.styles.begin(), lick.styles.end(), key)
                                                         != lick.styles.end();
                                                 });

        CHECK (forThisStyle > 0);
    }
}

TEST ("the licks on offer are genuinely different from one another")
{
    /*  The shape `CompingTests` uses on the comping styles. A catalogue whose
        entries all sound the same is one that was fitted to its first entry,
        and nothing else here would notice. */
    std::set<std::vector<int>> shapes;

    for (const auto& lick : licks())
        shapes.insert (pitchesOf (lick, 60));

    CHECK_EQ (shapes.size(), licks().size());
}

//==============================================================================
TEST ("a lick sounds the pitches it was written from")
{
    /*  The one test that pins the octave arithmetic, and the reason `octave`
        is on `LickNote` at all. L01 is written in C as

            D E F A C E D C | B D F Ab G F D F | E

        and a degree alone cannot say that: the second bar climbs through an
        octave and a half, and the same four degrees flattened into one octave
        is a different figure. Asked for with its first chord's root on D4,
        every note has to come back where the source put it. */
    const auto& l01 = lickFor ("L01");
    const std::vector<int> written {
        62, 64, 65, 69, 72, 76, 74, 72,   // D4 E4 F4 A4 C5 E5 D5 C5  over Dm7
        71, 74, 77, 80, 79, 77, 74, 77,   // B4 D5 F5 Ab5 G5 F5 D5 F5 over G7
        76 };                             // E5                      over Cmaj7

    const auto sounded = pitchesOf (l01, 62);

    CHECK_EQ (sounded.size(), written.size());

    for (std::size_t i = 0; i < sounded.size() && i < written.size(); ++i)
        CHECK_EQ (sounded[i], written[i]);
}

TEST ("a lick is the same figure in every key")
{
    /*  What makes it a lick rather than a line: the offsets transpose, which
        is the same claim `VoicingShape` makes one dimension down. */
    for (const auto& lick : licks())
    {
        const auto inC = pitchesOf (lick, 60);

        for (const auto root : { 55, 62, 67, 71 })
        {
            const auto moved = pitchesOf (lick, root);

            CHECK_EQ (moved.size(), inC.size());

            for (std::size_t i = 0; i < moved.size() && i < inC.size(); ++i)
                CHECK_EQ (moved[i] - inC[i], root - 60);
        }
    }
}

TEST ("a lick stays inside a range a soloist could play it in")
{
    /*  Not the style's register - the placer moves a lick into that. What is
        checked here is that no lick is written so wide that no placement could
        fit it, which would be a data error nothing else would catch: it would
        simply never be chosen. Two octaves and a fourth is the widest the
        catalogue reaches. */
    for (const auto& lick : licks())
    {
        const auto sounded = pitchesOf (lick, 60);
        const auto low = *std::min_element (sounded.begin(), sounded.end());
        const auto high = *std::max_element (sounded.begin(), sounded.end());

        CHECK (high - low <= 29);
    }
}

TEST ("a style's grid is its own feel plus the feels it can quote")
{
    /*  `subdivisionsFor` and `onAnyGrid` are what stop the app teaching a
        figure and then marking a player for playing it, and until now both
        were covered only through their callers. They are the single answer two
        readers share - `lineFaults` about a written note, `readLinePlacement`
        about a played one - so they are worth asking directly. */
    const auto& bebop = lineStyleFor ("bebop");
    const auto accepted = subdivisionsFor (bebop);

    // Its own feel is always in, and the triplet is in because L11 is.
    CHECK (std::find (accepted.begin(), accepted.end(), bebop.feel) != accepted.end());
    CHECK (std::find (accepted.begin(), accepted.end(), Subdivision::tripletEighth)
           != accepted.end());

    // Nothing bebop can quote is written in sixteenths, so a sixteenth is
    // still off its grid - which is the "you are playing this like a
    // different feel" reading the widening must not swallow.
    CHECK (std::find (accepted.begin(), accepted.end(), Subdivision::sixteenth)
           == accepted.end());

    // Derived, never listed: the answer is only ever as wide as the catalogue.
    for (const auto& style : lineStyles())
        for (const auto feel : subdivisionsFor (style))
        {
            if (feel == style.feel)
                continue;

            const auto quoted = std::any_of (licks().begin(), licks().end(),
                                             [&style, feel] (const LickDefinition& lick)
                                             {
                                                 return lick.feel == feel
                                                     && std::find (lick.styles.begin(), lick.styles.end(),
                                                                   style.key) != lick.styles.end();
                                             });

            CHECK (quoted);
        }
}

TEST ("on any grid is on one of them, and an empty list is none")
{
    const std::vector<Subdivision> eighthsAndTriplets { Subdivision::eighth,
                                                        Subdivision::tripletEighth };

    CHECK (onAnyGrid (BarPosition { 1, 0 }, eighthsAndTriplets));                  // the beat
    CHECK (onAnyGrid (BarPosition { 1, ticksPerBeat / 2 }, eighthsAndTriplets));   // the and
    CHECK (onAnyGrid (BarPosition { 1, ticksPerBeat / 3 }, eighthsAndTriplets));   // a triplet

    // A sixteenth is on neither, which is the whole point of asking.
    CHECK (! onAnyGrid (BarPosition { 1, ticksPerBeat / 4 }, eighthsAndTriplets));

    CHECK (! onAnyGrid (BarPosition { 0, 0 }, {}));
}

TEST ("every value in these enums is carried by some lick")
{
    /*  The `brazilian` check, one file over. That style sat in `ReharmStyle`
        tagged on no rule at all, so the menu it would have shipped had an
        entry that was a lie, and nothing said so because nothing counted. Two
        values here were the same shape and were deleted rather than wired up:
        `LickSource::transcription`, because nothing in the catalogue is taken
        note for note off a recording, and `LickOrnament::grace`, because both
        ornamented notes are crushes.

        This is what stops the next one. A value earns its place by being on
        some lick - not on many, since one documented device is a real
        category, but on at least one. Author a transcription and the value
        comes back with it. */
    std::set<LickSource> sources;
    std::set<LickOrnament> ornaments;

    for (const auto& lick : licks())
    {
        sources.insert (lick.source);

        for (const auto& note : lick.notes)
            ornaments.insert (note.ornament);
    }

    for (const auto source : { LickSource::documentedDevice,
                               LickSource::teachingSite,
                               LickSource::composite })
        CHECK (sources.count (source) == 1);

    for (const auto ornament : { LickOrnament::none, LickOrnament::crush })
        CHECK (ornaments.count (ornament) == 1);
}

TEST ("a lick nobody has heard of falls back rather than failing")
{
    // The same forgiveness `lineStyleFor` and `compStyleFor` give, for the
    // same reason: a stored key this version renamed should still play.
    CHECK_EQ (lickFor ("L99").key, licks().front().key);
    CHECK_EQ (lickFor ("").key, licks().front().key);
}

TEST ("the catalogue's composites weigh less than its documented devices")
{
    /*  The research is candid that L01, L06 and L17 are assembled from
        documented devices rather than quoted, and that The Lick is a meme
        rather than a line - so throwing the distinction away would lose the
        most useful thing it said. Weight decides the draw, never the cost. */
    const auto weightOf = [] (const char* key) { return lickFor (key).weight; };

    for (const auto& composite : { "L01", "L06", "L17" })
    {
        CHECK (lickFor (composite).source == LickSource::composite);
        CHECK (weightOf (composite) < weightOf ("L05"));
    }

    CHECK (weightOf ("L08") < weightOf ("L01"));
}

//==============================================================================
//  The matcher.

namespace
{
    Chart chartOf (const std::string& progression)
    {
        const auto parsed = parseProgressionText (progression);
        CHECK (parsed.ok());
        return *parsed.chart;
    }

    bool found (const std::vector<LickMatch>& matches, const char* key)
    {
        return std::any_of (matches.begin(), matches.end(),
                            [&key] (const LickMatch& match)
                            { return match.lick->key == key; });
    }

    std::vector<LickMatch> matchesOn (const std::string& progression, const char* styleKey = "bebop")
    {
        const auto chart = chartOf (progression);
        return licksFitting (chart, lineStyleFor (styleKey), 0, chart.measureCount() - 1);
    }
}

TEST ("a ii-V-I offers the ii-V-I licks")
{
    const auto matches = matchesOn ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    CHECK (! matches.empty());
    CHECK (found (matches, "L01"));
    CHECK (found (matches, "L02"));
    CHECK (found (matches, "L03"));

    // And not the ones written over other progressions.
    CHECK (! found (matches, "L05"));   // the minor ii-V-i
    CHECK (! found (matches, "L17"));   // the turnaround
}

TEST ("a chord's quality is not enough - the offsets between them have to line up")
{
    /*  The negative control, and the one that matters most here: a matcher
        that always finds something is satisfied by a function that always says
        yes. The qualities below are exactly a ii-V-I's - minor, dominant,
        major - and the roots are wrong, so nothing written for a ii-V-I may
        match. This is the whole of what "keyed on a chord-sequence shape"
        means: a progression, not a bag of qualities. */
    const auto matches = matchesOn ("| Dm7 | Ab7 | Emaj7 | Emaj7 |");

    CHECK (! found (matches, "L01"));
    CHECK (! found (matches, "L02"));
    CHECK (! found (matches, "L03"));
}

TEST ("the same lick is found in every key, and says which")
{
    /*  Twelve keys, because a lick that only works in C is a line. The match
        reports the chart's root rather than the lick's, which is what lets the
        writer sound it where the chart actually is. */
    for (auto step = 0; step < 12; ++step)
    {
        const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |").transposed (step);
        const auto matches = licksFitting (chart, lineStyleFor ("bebop"), 0, 3);

        CHECK (found (matches, "L01"));

        for (const auto& match : matches)
            if (match.lick->key == "L01")
            {
                CHECK_EQ (match.startTick, 0);
                CHECK_EQ (match.rootPitchClass, chart.chordAt (0)->root());
            }
    }
}

TEST ("a lick written two beats to a chord needs two beats to a chord")
{
    /*  The boundary rule. L17's turnaround is written over | C6 A7b9 | Dm7 G7 |
        and L01's ii-V-I over a bar apiece, and neither may be stretched or
        squeezed into the other's shape - a figure whose second half arrives a
        bar late is not that figure. */
    const auto turnaround = matchesOn ("| C6 A7 | Dm7 G7 | Cmaj7 | Cmaj7 |");

    CHECK (found (turnaround, "L17"));
    CHECK (! found (turnaround, "L01"));

    const auto barApiece = matchesOn ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    CHECK (found (barApiece, "L01"));
    CHECK (! found (barApiece, "L17"));
}

TEST ("a chord held on past the lick still counts, and one held on inside it does not")
{
    /*  The one asymmetry, and the reason for it: a chart sitting on the tonic
        after the lick has landed is still the tonic it landed on, so the last
        chord may be longer. A chord in the *middle* that outlasts what the
        lick expects would put every note after it in the wrong place. */
    CHECK (found (matchesOn ("| Dm7 | G7 | Cmaj7 | Cmaj7 |"), "L01"));
    CHECK (found (matchesOn ("| Dm7 | G7 | Cmaj7 |"), "L01"));

    // The V held for two bars: the I now arrives a bar after L01 expects it.
    CHECK (! found (matchesOn ("| Dm7 | G7 | G7 | Cmaj7 |"), "L01"));
}

TEST ("two bars of one chord are one chord, not two")
{
    /*  L15 and L16 are written across two bars of a single chord, and a chart
        writes that as two measures. Without merging the runs they would match
        nothing at all - which is a bug that looks exactly like a lick nobody
        ever draws. */
    CHECK (found (matchesOn ("| Dm7 | Dm7 |", "modal"), "L15"));
    CHECK (found (matchesOn ("| Cmaj7 | Cmaj7 |"), "L16"));

    CHECK (! found (matchesOn ("| Dm7 |", "modal"), "L15"));
}

TEST ("a lick is only offered to a style that plays it")
{
    // L12's blues figure is tagged blues, and bebop must not be handed it.
    CHECK (found (matchesOn ("| C7 | C7 | C7 | C7 |", "blues"), "L12"));
    CHECK (! found (matchesOn ("| C7 | C7 | C7 | C7 |", "bebop"), "L12"));
}

TEST ("a lick with a pickup is not offered the first bar of a line")
{
    /*  There is nothing in front of it to lead in from. L12 leads in on the
        and of four, so it may start on the second chord of a blues and not on
        the first. */
    const auto matches = matchesOn ("| C7 | C7 | F7 | C7 |", "blues");

    CHECK (! matches.empty());

    for (const auto& match : matches)
        if (match.lick->startsOnAPickup)
            CHECK (match.startTick >= ticksPerBeat);
}

TEST ("a lick is not laid across a bar with nothing in it")
{
    /*  An empty bar breaks the runs up and is then dropped, so the runs either
        side sit next to each other in the list while being a bar apart in the
        music. Without asking they were contiguous, L01's V would land on the
        chart's I. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    auto withAHole = chart;
    withAHole.measures[1].slots.clear();

    const auto matches = licksFitting (withAHole, lineStyleFor ("bebop"), 0, 3);

    CHECK (! found (matches, "L01"));
}

TEST ("a chart nothing fits offers nothing rather than the nearest thing")
{
    /*  The other half of the negative control. A catalogue that falls back to
        something when it has nothing is worse than one that stays quiet: the
        generator has a perfectly good answer of its own for a bar no lick was
        written for.

        **This used to fail on the quality and now fails on the boundary**,
        which is the change the four new licks made. It was
        `| Cdim7 | Csus4 | C+ | CmMaj7 |` while those four qualities were
        unreached; L18 and L21 now fit two of those bars, so a chart that fits
        nothing has to be built out of the other rule instead - the one that
        says a lick's chord must last as long as the chart's.

        Both licks written over these two qualities want **two bars** of the
        chord, because that is how long a sus vamp or a tonic minor-major sits.
        Every chord below changes after one, so each is a quality the catalogue
        reaches and a place it cannot be played. Verified in both directions:
        the same chords held for two bars each do fit. */
    CHECK (matchesOn ("| Csus4 | CmMaj7 | Ebsus4 | AbmMaj7 |").empty());
    CHECK (matchesOn ("| Csus4 | CmMaj7 | Ebsus4 | AbmMaj7 |", "modal").empty());

    // The positive half, so this is about the boundaries and not the chords.
    CHECK (found (matchesOn ("| Csus4 | Csus4 | CmMaj7 | CmMaj7 |", "modal"), "L19"));
    CHECK (found (matchesOn ("| Csus4 | Csus4 | CmMaj7 | CmMaj7 |", "modal"), "L22"));
}

TEST ("the catalogue reaches every chord quality, and says which lick does it")
{
    /*  This asserted **four** of the eight for as long as the catalogue was
        the research's Part B and nothing else, and said so: Part B is a ii-V
        catalogue, so it is written over majors, minors, dominants and
        half-diminished chords and over nothing at all else. A tune with a
        passing diminished, a sus vamp or a tonic minor-major got a generated
        line and no quotes.

        Four licks closed it, one quality each, and they are named here rather
        than counted so that removing one fails this test with the name of what
        went missing. */
    std::set<ChordQuality> covered;

    for (const auto& lick : licks())
        for (const auto& chord : lick.chords)
            covered.insert (chord.quality);

    CHECK_EQ (covered.size(), std::size_t { 8 });

    // The four the research's own catalogue reaches.
    CHECK (covered.count (ChordQuality::major) == 1);
    CHECK (covered.count (ChordQuality::minor) == 1);
    CHECK (covered.count (ChordQuality::dominant) == 1);
    CHECK (covered.count (ChordQuality::halfDiminished) == 1);

    // And the four that were added, each by the lick named beside it.
    const auto onlyChordOf = [] (const char* key) { return lickFor (key).chords.front().quality; };

    CHECK (onlyChordOf ("L18") == ChordQuality::diminished);
    CHECK (onlyChordOf ("L19") == ChordQuality::suspended);
    CHECK (onlyChordOf ("L21") == ChordQuality::augmented);
    CHECK (onlyChordOf ("L22") == ChordQuality::minorMajor);
}

TEST ("a range outside the chart is empty rather than a crash")
{
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 |");

    CHECK (licksFitting (chart, lineStyleFor ("bebop"), -1, 2).empty());
    CHECK (licksFitting (chart, lineStyleFor ("bebop"), 2, 1).empty());
    CHECK (! licksFitting (chart, lineStyleFor ("bebop"), 0, 99).empty());
}

TEST ("a style that asks to quote has something it can actually quote")
{
    /*  The `brazilian` lesson, generalised. That style sat in `ReharmStyle`
        tagged on no rule at all, so the menu it would have shipped had an
        entry that was a lie - and this is the same shape: a style with a
        `lickShare` above nothing and no lick it can reach is a setting that
        does nothing, which looks exactly like a setting that works.

        "Can reach" is the whole test rather than "is tagged for": the
        pentatonic style was tagged on a thirteen-note cell it could never
        phrase, because its own longest phrase is twelve. Tagged and
        unreachable is worse than untagged, because it reads as covered. */
    for (const auto& style : lineStyles())
    {
        if (style.lickShare <= 0)
            continue;

        const auto reachable = std::count_if (licks().begin(), licks().end(),
                                              [&style] (const LickDefinition& lick)
                                              {
                                                  return std::find (lick.styles.begin(), lick.styles.end(),
                                                                    style.key) != lick.styles.end()
                                                      && static_cast<int> (lick.notes.size())
                                                             <= style.longestPhrase;
                                              });

        CHECK (reachable > 0);
    }
}

//==============================================================================
//  The cross-check the header promises: the source's own note categories
//  against what a take actually reads.

namespace
{
    /** The tunes both cross-checks sweep.

        One list, because a chart added to one of them and not the other tests
        half the catalogue against half the claim - and because which charts
        these are is the whole of what decides which licks get cross-checked at
        all. The first four are the ii-V world the research's Part B is written
        for. The last four are the habitats of the licks that are not: a
        passing diminished, a sus vamp, a tonic minor-major and an augmented
        chord. Without them L18-L22 would be authored, drawn, credited to a
        player - and held to nothing.

        Measured when they were added: chord-tone claims checked went 2,211 to
        3,338 and outside claims 91 to 161, with no violation in either
        direction. L18 126, L19 48, L20 301, L21 120, L22 200. */
    const std::vector<std::string>& crossCheckTunes()
    {
        static const std::vector<std::string> tunes {
            "| Dm7 | G7 | Cmaj7 | Cmaj7 | Em7 | A7 | Dm7 | Dm7 "
            "| Gm7 | C7 | Fmaj7 | Fmaj7 | Bm7b5 | E7 | Am7 | Am7 |",
            "| C7 | F7 | C7 | C7 | F7 | F7 | C7 | C7 | G7 | F7 | C7 | G7 |",
            "| Dm7b5 | G7 | Cm7 | Cm7 | Dm7b5 | G7 | Cm7 | Cm7 |",
            "| E7 | E7 | A7 | A7 | E7 | E7 | A7 | A7 |",
            "| Cmaj7 | C#dim7 | Dm7 | G7 | Cmaj7 | C#dim7 | Dm7 | G7 |",
            "| Fsus4 | Fsus4 | Ebsus4 | Ebsus4 | Dsus4 | Dsus4 | Csus4 | Csus4 |",
            "| CmMaj7 | CmMaj7 | Ab7 | G7 | CmMaj7 | CmMaj7 | Fm7 | G7 |",
            "| C+ | C+ | Fmaj7 | Fmaj7 | C+ | C+ | Fmaj7 | Fmaj7 |" };

        return tunes;
    }

    /** A written line played back through a real take, as `LineWriterTests`
        does it - the roles are about what the analyser says, so it has to be
        the analyser saying it. */
    std::vector<LineNote> readBack (const Chart& chart, const std::vector<WrittenNote>& line,
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
}

TEST ("what the source calls a chord tone is never read as outside")
{
    /*  Half of the cross-check `LickCatalogue.h` promises, and the half with
        teeth: it is what catches a degree typed wrong. Author the 5 of a minor
        seventh as 8 instead of 7 and the note stops being a chord tone, which
        nothing else here would notice - the round trip only asks that the
        *writer* and the *reader* agree, and they would, about a wrong note.

        Never *outside* rather than always *a chord tone*, and the difference
        is a real ambiguity rather than slack. A lick is keyed on a chord
        **quality**, and a quality is realised by more than one symbol: the 6
        is a chord tone of C6 and a scale tone of Cmaj7, so L16's major bebop
        line - which is written in the Baker/Harris view where the 6 belongs to
        the chord - reads as a scale tone over a maj7 bar. Inside either way,
        which is the claim the source can be held to.

        Measured before it was written: five tunes, every style, 150 seeds -
        21,232 notes the source calls chord tones, 21,213 read as chord tones
        and 19 as scale tones, none outside. */
    auto checked = 0;

    for (const auto& text : crossCheckTunes())
    {
        const auto chart = chartOf (text);

        for (const auto& style : lineStyles())
            for (std::uint32_t seed = 1; seed <= 24; ++seed)
            {
                const auto written = improvisedLine (chart, 0, chart.measureCount() - 1,
                                                     "", style.key, seed);
                const auto read = readBack (chart, written, style.scaleStyle);

                if (read.size() != written.size())
                    continue;

                for (std::size_t i = 0; i < written.size(); )
                {
                    if (written[i].lickKey.empty()) { ++i; continue; }

                    const auto& lick = lickFor (written[i].lickKey);

                    auto run = std::size_t { 0 };

                    while (i + run < written.size()
                           && written[i + run].lickKey == lick.key)
                        ++run;

                    /*  Paired by position, so a run the writer had to truncate
                        at the end of the range is skipped rather than
                        mis-paired - and with it any chance of testing the
                        wrong note against the wrong role. */
                    if (run == lick.notes.size())
                        for (std::size_t k = 0; k < run; ++k)
                        {
                            /*  A pickup sounds over the bar *before* the one
                                the lick is aimed at, while its degree is
                                relative to the lick's own first chord. There
                                is no claim to check there, and pretending
                                otherwise would test a role against a chord it
                                was never written for. */
                            if (lick.notes[k].tick < 0)
                                continue;

                            if (lick.notes[k].role != LickRole::chordTone)
                                continue;

                            ++checked;
                            CHECK (! isOutsideByPitch (read[i + k].colour));
                        }

                    i += run;
                }
            }
    }

    // A cross-check that checked nothing would pass silently. 3,338 when this
    // was last measured, so the floor is a floor rather than the number.
    CHECK (checked > 2000);
}

TEST ("what the source calls outside is never read as a chord tone")
{
    /*  The other half, and the other direction a mis-typed degree fails in:
        author a chromatic as the degree next door and it lands on a chord
        tone, which sounds like nothing and reads like nothing.

        Only `outside` is held to this. An **enclosure** is a gesture rather
        than a pitch - L06 encloses the tonic with the b7 and the 5, both chord
        tones - and an **approach** is one too. A **colour tone** is a tension,
        and whether a tension reads inside depends on the scale the take is
        being read against, which is the player's choice and not the source's.
        Those three are named here rather than asserted, which is what the
        header means by not tidying the disagreements away. */
    /*  The same tunes as above, because the licks that author an `outside`
        note are spread across them - L03's side-slip over a major ii-V, L11's
        cascade over any dominant, L10's added half step on a static one, and
        L18's Db and Bb, which are the dominant's own 4th and 9th and are
        outside the scale a dim7 is read against. */
    auto checked = 0;

    for (const auto& text : crossCheckTunes())
    {
        const auto chart = chartOf (text);

        for (const auto& style : lineStyles())
            for (std::uint32_t seed = 1; seed <= 24; ++seed)
            {
                const auto written = improvisedLine (chart, 0, chart.measureCount() - 1,
                                                     "", style.key, seed);
                const auto read = readBack (chart, written, style.scaleStyle);

                if (read.size() != written.size())
                    continue;

                for (std::size_t i = 0; i < written.size(); )
                {
                    if (written[i].lickKey.empty()) { ++i; continue; }

                    const auto& lick = lickFor (written[i].lickKey);

                    auto run = std::size_t { 0 };

                    while (i + run < written.size() && written[i + run].lickKey == lick.key)
                        ++run;

                    if (run == lick.notes.size())
                        for (std::size_t k = 0; k < run; ++k)
                        {
                            if (lick.notes[k].tick < 0 || lick.notes[k].role != LickRole::outside)
                                continue;

                            ++checked;
                            CHECK (read[i + k].colour != NoteColour::chordTone);
                        }

                    i += run;
                }
            }
    }

    // 161 when last measured; 91 of them before the new licks arrived.
    CHECK (checked > 120);
}

TEST ("a bar with two chords is written against the first, and that is known")
{
    /*  Not an assertion that this is right. It pins a limitation that was
        found while writing the cross-check above and was silent until then,
        so that it is a stated property of solo practice rather than something
        the next person rediscovers.

        `improvisedLine` asks `chart.chordAt (bar)`, which answers with the
        chord on beat one. On `| C6 A7 |` every note in the bar is therefore
        written against C6 - including the four in its second half, which
        sound over A7. The take reads it back the same way, because
        `LineAnalyzer::setTarget` takes a *bar* and a chord and the page
        targets bars too, so writer and reader agree and the round trip has
        never had anything to say about it.

        It matters here because L17 is the one lick keyed on two chords to a
        bar: `licksFitting` will only place it where the boundaries line up,
        and then half of it is coloured against the wrong chord.

        Fixing it is not a `chordAt` argument. It is the take model: a target
        would have to become a chord *within* a bar, in the engine, on the wire
        and in the page's `selectBar`. Recorded in docs/HANDOFF.md.
    */
    const auto chart = chartOf ("| C6 A7 | Dm7 G7 | Cmaj7 | Cmaj7 |");

    CHECK_EQ (chart.chordAt (0, 0)->toString(), std::string ("C6"));
    CHECK_EQ (chart.chordAt (0, 2)->toString(), std::string ("A7"));

    const auto written = improvisedLine (chart, 0, 3, "", "bebop", 1);
    CHECK (! written.empty());

    auto secondHalf = 0;

    for (const auto& note : written)
    {
        // Every note carries the chord on beat one of its bar, whatever the
        // chart says is sounding where it actually falls.
        CHECK_EQ (note.chordSymbol, chart.chordAt (note.measureIndex, 0)->toString());

        if (note.at.beat >= 2 && note.measureIndex <= 1)
            ++secondHalf;
    }

    // And there really are notes in the half of the bar this gets wrong, so
    // the check above is not passing for want of anything to look at.
    CHECK (secondHalf > 0);
}
