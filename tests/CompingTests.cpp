#include "TestFramework.h"
#include "jazz/core/Comping.h"
#include "jazz/core/LineAnalyzer.h"
#include "jazz/core/VoicingAnalyzer.h"

#include <set>

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

    int hitsInBar (const CompPlan& plan, int measureIndex)
    {
        auto count = 0;

        for (const auto& hit : plan.hits)
            if (hit.measureIndex == measureIndex)
                ++count;

        return count;
    }
}

//==============================================================================
// The grid. One representation, and the reason it is 24 ticks rather than 8
// slots: a straight eighth grid cannot write a triplet or a sixteenth at all.

TEST ("the grid divides a beat every way a comper needs")
{
    CHECK_EQ (ticksFor (Subdivision::beat), 24);
    CHECK_EQ (ticksFor (Subdivision::eighth), 12);
    CHECK_EQ (ticksFor (Subdivision::tripletEighth), 8);
    CHECK_EQ (ticksFor (Subdivision::sixteenth), 6);

    // Every one of them lands exactly on a tick - no remainder anywhere, which
    // is the whole reason for 24.
    for (auto subdivision : { Subdivision::beat, Subdivision::eighth,
                              Subdivision::tripletEighth, Subdivision::sixteenth })
        CHECK_EQ (ticksPerBeat % ticksFor (subdivision), 0);
}

TEST ("a position counts the way a player counts")
{
    // Parenthesised because a brace initialiser's comma would otherwise split
    // the macro's arguments.
    CHECK_EQ ((BarPosition { 0, 0 }).describe(), std::string ("1"));
    CHECK_EQ ((BarPosition { 1, 12 }).describe(), std::string ("2 and"));
    CHECK_EQ ((BarPosition { 2, 8 }).describe(), std::string ("3 trip"));
    CHECK_EQ ((BarPosition { 3, 6 }).describe(), std::string ("4 e"));
}

TEST ("a position before the downbeat belongs to the bar before it")
{
    // Floored, not truncated: an anticipation read back from a shell's clock
    // arrives as a small negative, and folding that onto beat zero would put a
    // pushed chord on the downbeat it was pushed ahead of.
    const auto early = BarPosition::fromTicks (-12);

    CHECK_EQ (early.beat, -1);
    CHECK_EQ (early.tick, 12);
}

TEST ("which beats are strong is derived from the metre, not tabulated")
{
    CHECK (strengthAt ({ 0, 0 }, 4) == BeatStrength::downbeat);
    CHECK (strengthAt ({ 2, 0 }, 4) == BeatStrength::strong);
    CHECK (strengthAt ({ 1, 0 }, 4) == BeatStrength::weak);
    CHECK (strengthAt ({ 1, 12 }, 4) == BeatStrength::offbeat);

    // A waltz has no second half to start, so only the downbeat is strong -
    // which is the character of three rather than a gap in the table.
    CHECK (strengthAt ({ 0, 0 }, 3) == BeatStrength::downbeat);
    CHECK (strengthAt ({ 1, 0 }, 3) == BeatStrength::weak);
    CHECK (strengthAt ({ 2, 0 }, 3) == BeatStrength::weak);
}

//==============================================================================
// The style definition, proved against styles that genuinely differ rather
// than against one style the shape was fitted to.

TEST ("a slot naming no beat lands on every beat of whatever metre it is in")
{
    const CompSlot everyBeat { std::nullopt, 0, 100, false };

    CHECK_EQ (slotPositions (everyBeat, 4).size(), std::size_t (4));
    CHECK_EQ (slotPositions (everyBeat, 3).size(), std::size_t (3));
    CHECK_EQ (slotPositions (everyBeat, 5).size(), std::size_t (5));
}

TEST ("a slot counting back from the end means the same thing in any metre")
{
    const CompSlot andOfLast { -1, ticksPerBeat / 2, 100, true };

    CHECK_EQ (slotPositions (andOfLast, 4).front().beat, 3);
    CHECK_EQ (slotPositions (andOfLast, 3).front().beat, 2);
    CHECK_EQ (slotPositions (andOfLast, 4).front().tick, 12);
}

TEST ("a slot naming a beat the metre has not got says nothing")
{
    // A style built around the fourth beat has less to say in three. Silence
    // is more honest than folding it onto a beat that does exist.
    const CompSlot fourthBeat { 3, 0, 100, false };

    CHECK (slotPositions (fourthBeat, 4).size() == 1);
    CHECK (slotPositions (fourthBeat, 3).empty());
}

TEST ("the styles on offer are genuinely different from one another")
{
    const auto styles = compStyles();
    CHECK (styles.size() >= 2);

    const auto& four = compStyleFor ("four");
    const auto& basie = compStyleFor ("basie");

    // Dense against sparse: the shape has to hold both, or it was fitted to
    // one style and generalised afterwards.
    CHECK (four.fewestPerBar > basie.mostPerBar);

    // And a feel that is not eighths at all, which is the case a straight
    // eighth grid could not have represented.
    CHECK (compStyleFor ("ballad").feel == Subdivision::tripletEighth);

    const auto ballad = compStyleFor ("ballad");
    auto offTheEighthGrid = false;

    for (const auto& slot : ballad.slots)
        if (slot.tick % ticksFor (Subdivision::eighth) != 0)
            offTheEighthGrid = true;

    CHECK (offTheEighthGrid);
}

TEST ("an unknown style still comps rather than going silent")
{
    // A renamed style should not leave the band with nothing to play.
    CHECK_EQ (compStyleFor ("no such style").key, compStyles().front().key);
}

//==============================================================================
// The generator, and the invariant that holds it to the same definition the
// evaluator reads.

TEST ("everything the generator plays is in the style it was asked for")
{
    /*  The invariant, and it is checked from both directions at once: what
        compPlan() produces for a style must pass the evaluator's own "is this
        in style" test. Without it the app comps in a style and then marks its
        own playing out of style - the same trap idiomaticVoicings and
        VoicingAnalyzer are held out of. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 |");

    for (const auto& style : compStyles())
        for (std::uint32_t seed = 0; seed < 24; ++seed)
        {
            const auto plan = compPlan (chart, style, 0, 7, seed);

            for (const auto& hit : plan.hits)
                CHECK (fitsStyle (hit, style, 4));
        }
}

TEST ("the same seed plans the same comp, note for note")
{
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");
    const auto& style = compStyleFor ("charleston");

    const auto first = compPlan (chart, style, 0, 3, 7);
    const auto again = compPlan (chart, style, 0, 3, 7);

    CHECK_EQ (first.hits.size(), again.hits.size());

    for (std::size_t i = 0; i < first.hits.size(); ++i)
    {
        CHECK (first.hits[i].at == again.hits[i].at);
        CHECK_EQ (first.hits[i].midiNotes.size(), again.hits[i].midiNotes.size());
        CHECK_EQ (first.hits[i].chordSymbol, again.hits[i].chordSymbol);
    }
}

TEST ("a bar is planned the same way every time the loop comes round")
{
    /*  The bar index is mixed into the seed rather than the plan being walked
        forward, so planning bars 4-7 on their own gives the same four bars as
        planning 0-7 and taking the tail. A loop that drifted every time round
        would be a different band each chorus. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 |");
    const auto& style = compStyleFor ("basie");

    const auto whole = compPlan (chart, style, 0, 7, 11);
    const auto tail = compPlan (chart, style, 4, 7, 11);

    std::vector<BarPosition> fromWhole;

    for (const auto& hit : whole.hits)
        if (hit.measureIndex >= 4)
            fromWhole.push_back (hit.at);

    CHECK_EQ (fromWhole.size(), tail.hits.size());

    for (std::size_t i = 0; i < tail.hits.size(); ++i)
        CHECK (fromWhole[i] == tail.hits[i].at);
}

TEST ("a dense style fills the bar and a sparse one leaves it alone")
{
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 |");

    const auto four = compPlan (chart, compStyleFor ("four"), 0, 7, 3);
    const auto basie = compPlan (chart, compStyleFor ("basie"), 0, 7, 3);

    CHECK (four.hits.size() > basie.hits.size() * 2);

    // Four to the bar means four to the bar, in every bar, however the rolls
    // fell - which is what the density floor is for.
    for (auto bar = 0; bar < 8; ++bar)
        CHECK_EQ (hitsInBar (four, bar), 4);
}

TEST ("a style with a density floor never leaves a bar empty")
{
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    for (std::uint32_t seed = 0; seed < 40; ++seed)
    {
        const auto plan = compPlan (chart, compStyleFor ("charleston"), 0, 3, seed);

        for (auto bar = 0; bar < 4; ++bar)
            CHECK (hitsInBar (plan, bar) >= 1);
    }
}

TEST ("a comp in three is in three")
{
    // The styles are written in four and none of them says so. A waltz gets
    // three hits to the bar from the same definition, not four.
    const auto waltz = chartOf ("| Dm7 | G7 | Cmaj7 |", 3);
    const auto plan = compPlan (waltz, compStyleFor ("four"), 0, 2, 5);

    for (auto bar = 0; bar < 3; ++bar)
        CHECK_EQ (hitsInBar (plan, bar), 3);

    for (const auto& hit : plan.hits)
    {
        CHECK (hit.at.beat >= 0 && hit.at.beat < 3);
        CHECK (fitsStyle (hit, compStyleFor ("four"), 3));
    }
}

TEST ("a pushed hit sounds the next bar's chord, not this one's")
{
    /*  The thing that makes comping sound like comping rather than like a
        chord chart being read aloud. Basie's main slot anticipates, so over
        Dm7 into G7 the and of four is a G7. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    auto foundOne = false;

    for (std::uint32_t seed = 0; seed < 20 && ! foundOne; ++seed)
    {
        const auto plan = compPlan (chart, compStyleFor ("basie"), 0, 3, seed);

        for (const auto& hit : plan.hits)
            if (hit.measureIndex == 0 && hit.anticipation)
            {
                CHECK_EQ (hit.chordSymbol, std::string ("G7"));
                CHECK (hit.at.beat == 3);
                foundOne = true;
            }
    }

    CHECK (foundOne);
}

TEST ("a push at the end of the range is a hit, not a push into silence")
{
    // There is no next chord to pull forward, so the hit voices the bar it is
    // in. It is still a position the style offers, so it is still in style.
    const auto chart = chartOf ("| Dm7 | G7 |");

    for (std::uint32_t seed = 0; seed < 20; ++seed)
    {
        const auto plan = compPlan (chart, compStyleFor ("basie"), 1, 1, seed);

        for (const auto& hit : plan.hits)
        {
            CHECK (! hit.anticipation);
            CHECK (fitsStyle (hit, compStyleFor ("basie"), 4));
        }
    }
}

TEST ("every voicing the comp plans is one the analyser would call two-handed")
{
    // The other half of the same invariant: the plan's rhythm is in style and
    // its notes are the voicing the app would have suggested.
    const auto chart = chartOf ("| Dm7 | G7alt | Cmaj7 | Am7b5 |");
    const auto plan = compPlan (chart, compStyleFor ("charleston"), 0, 3, 2);

    CHECK (! plan.isEmpty());

    for (const auto& hit : plan.hits)
    {
        const auto chord = ChordSymbol::parse (hit.chordSymbol);
        CHECK (chord.has_value());

        const auto voicing = Voicing::fromNotes (hit.midiNotes);
        CHECK (VoicingAnalyzer::classify (voicing, *chord) == VoicingType::twoHandedRootless);
    }
}

TEST ("the comp leads its voicings through the whole plan, not bar by bar")
{
    /*  Planned in one pass precisely so this holds across barlines: hit to hit,
        the hands move a little. Done per bar, every downbeat would re-spell
        from scratch and leap. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cm7 | F7 | Bbmaj7 |");
    const auto plan = compPlan (chart, compStyleFor ("four"), 0, 5, 1);

    for (std::size_t i = 1; i < plan.hits.size(); ++i)
    {
        const auto moved = std::abs (plan.hits[i].midiNotes.front()
                                       - plan.hits[i - 1].midiNotes.front());
        CHECK (moved <= 6);
    }
}

TEST ("a bar of two chords is comped as two chords")
{
    // The hit's chord is the one sounding under it, so a ii-V in one bar is
    // not comped as the ii all the way through.
    const auto chart = chartOf ("| Dm7 G7 | Cmaj7 |");
    const auto plan = compPlan (chart, compStyleFor ("four"), 0, 0, 4);

    std::set<std::string> heard;

    for (const auto& hit : plan.hits)
        if (! hit.anticipation)
            heard.insert (hit.chordSymbol);

    CHECK (heard.count ("Dm7") == 1);
    CHECK (heard.count ("G7") == 1);
}

//==============================================================================
// The grid's other consumer. One representation, two features - and the proof
// that solo practice can read a position is that it changes what the take says.

namespace
{
    /** Plays a line over one chord at given positions, and reads it back. */
    TakeSummary takeOf (const std::string& symbol,
                        const std::vector<std::pair<int, BarPosition>>& notes,
                        int beatsPerBar = 4)
    {
        LineAnalyzer::Options options;
        options.beatsPerBar = beatsPerBar;

        LineAnalyzer analyzer { options };
        analyzer.startTake();
        analyzer.setTarget (0, *ChordSymbol::parse (symbol));

        for (const auto& [midiNote, at] : notes)
            analyzer.play (midiNote, at);

        analyzer.endTake();
        return analyzer.summary();
    }

    bool says (const TakeSummary& take, const std::string& fragment)
    {
        for (const auto& observation : take.observations)
            if (observation.find (fragment) != std::string::npos)
                return true;

        return false;
    }
}

TEST ("a note with no position is read exactly as it always was")
{
    /*  The whole grid is additive. A shell with no clock gives no positions,
        and every reading that depends on one has to stay silent rather than
        inventing a downbeat - so this take is the take this file has always
        produced. */
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, *ChordSymbol::parse ("Dm7"));

    const auto note = analyzer.play (62);

    CHECK (! note.at.has_value());
    CHECK (! note.onStrongBeat);

    analyzer.endTake();
    const auto take = analyzer.summary();

    CHECK_EQ (take.notesSatOn, 0);
    CHECK_EQ (take.notesPassedThrough, 0);
    CHECK_EQ (take.bars.front().notesOnStrongBeats, 0);
}

TEST ("an avoid note passed through is told apart from one sat on")
{
    /*  The reading the grid was wanted for, and the one thing no amount of
        pitch analysis could ever have done: both lines play the same notes
        against the same chord. Only the rhythm differs. */
    const auto passing = takeOf ("Cmaj7", {
        { 60, { 0, 0 } },        // C  on one
        { 65, { 0, 12 } },       // F  - the avoid note, on the and, in passing
        { 64, { 1, 0 } },        // E  on two
        { 67, { 1, 12 } },
        { 60, { 2, 0 } },
        { 64, { 2, 12 } },
        { 65, { 3, 0 } },
        { 64, { 3, 12 } }
    });

    const auto dwelling = takeOf ("Cmaj7", {
        { 60, { 0, 0 } },        // C  on one
        { 65, { 1, 0 } },        // F  - the same avoid note, held a whole beat
        { 64, { 2, 0 } },        // E  two beats later
        { 67, { 3, 0 } }
    });

    CHECK (passing.notesPassedThrough > 0);
    CHECK_EQ (passing.notesSatOn, 0);

    CHECK (dwelling.notesSatOn > 0);
    CHECK_EQ (dwelling.notesPassedThrough, 0);

    CHECK (says (dwelling, "sat on rather than passed through"));
    CHECK (! says (passing, "sat on rather than passed through"));
}

TEST ("a note on a strong beat is counted as one, and a waltz is counted in three")
{
    // Beat three is strong in four and weak in three, so the same line over
    // the same chord reads differently in the two metres - which is the metre
    // meaning something rather than being a number on the page.
    const std::vector<std::pair<int, BarPosition>> line {
        { 62, { 0, 0 } },      // the downbeat, strong in both
        { 65, { 1, 0 } },      // beat two, weak in both
        { 69, { 2, 0 } }       // beat three: strong in four, weak in three
    };

    const auto inFour = takeOf ("Dm7", line, 4);
    const auto inThree = takeOf ("Dm7", line, 3);

    CHECK_EQ (inFour.bars.front().notesOnStrongBeats, 2);
    CHECK_EQ (inThree.bars.front().notesOnStrongBeats, 1);
}

TEST ("chord tones landing off the beat is something the take can say")
{
    // Every chord tone on an offbeat, every strong beat taken by something
    // else. The harmony is there and it is not where the ear listens for it.
    // Four bars of it, because one is a moment and the reading is about where
    // a player puts the harmony over a stretch of line.
    std::vector<std::pair<int, BarPosition>> line;

    for (auto bar = 0; bar < 5; ++bar)
    {
        line.push_back ({ 62, { 0, 0 } });    // D  - not in Cmaj7, on the downbeat
        line.push_back ({ 60, { 0, 12 } });   // C  - the root, off the beat
        line.push_back ({ 69, { 2, 0 } });    // A  - not in Cmaj7, on the strong beat
        line.push_back ({ 64, { 2, 12 } });   // E  - the third, off the beat
    }

    const auto take = takeOf ("Cmaj7", line);

    CHECK (take.bars.front().notesOnStrongBeats > 0);
    CHECK_EQ (take.bars.front().chordTonesOnStrongBeats, 0);
    CHECK (says (take, "landed on a strong beat was a"));
}

TEST ("where a note sat in the bar never moves the score")
{
    /*  The rule the rest of this file is built on, held to for the new reading
        as well: a line's shape produces words, never points. The same notes
        read against the same chord score the same whether or not the shell
        could say where they fell. */
    const std::vector<int> pitches { 60, 65, 64, 67, 60, 64, 65, 64 };

    LineAnalyzer plain;
    plain.startTake();
    plain.setTarget (0, *ChordSymbol::parse ("Cmaj7"));

    for (auto pitch : pitches)
        plain.play (pitch);

    plain.endTake();
    const auto withoutPositions = plain.summary();

    LineAnalyzer timed;
    timed.startTake();
    timed.setTarget (0, *ChordSymbol::parse ("Cmaj7"));

    for (std::size_t i = 0; i < pitches.size(); ++i)
        timed.play (pitches[i], BarPosition::fromTicks (static_cast<int> (i) * 12));

    timed.endTake();
    const auto withPositions = timed.summary();

    CHECK_EQ (withoutPositions.overall.score(), withPositions.overall.score());
}

TEST ("a note passed through across the barline is still passed through")
{
    // The and of four into the downbeat is an eighth, not a bar and a bit. The
    // gap has to be measured through the barline or every pushed note in the
    // idiom reads as one the line sat on.
    LineAnalyzer::Options options;
    LineAnalyzer analyzer { options };

    analyzer.startTake();
    analyzer.setTarget (0, *ChordSymbol::parse ("Cmaj7"));
    analyzer.play (60, { 0, 0 });
    analyzer.play (61, { 3, 12 } );      // Db over Cmaj7, on the and of four
    analyzer.setTarget (1, *ChordSymbol::parse ("Cmaj7"));

    // A leap away, so the Db is never promoted to an approach: it stays a note
    // outside the harmony, which is the kind this reading is about. A note that
    // stepped home has already earned a better verdict than "passed through".
    analyzer.play (67, { 0, 0 });        // G on the next downbeat

    analyzer.endTake();
    const auto take = analyzer.summary();

    CHECK_EQ (take.notesSatOn, 0);
    CHECK (take.notesPassedThrough >= 1);
}

//==============================================================================
// The walking bass. Same grid, same seeding discipline, one note to the beat.

namespace
{
    int pitchClassOf (int midiNote) { return ((midiNote % 12) + 12) % 12; }

    int rootPitchClassOf (const std::string& symbol)
    {
        const auto chord = ChordSymbol::parse (symbol);
        CHECK (chord.has_value());
        return static_cast<int> (chord->root());
    }
}

TEST ("a walking line plays one note on every beat")
{
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");
    const auto line = walkingBass (chart, 0, 3, 5);

    CHECK_EQ (line.size(), std::size_t (16));

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        // Walking is what the name says: on the beat, never between.
        CHECK_EQ (line[i].at.tick, 0);
        CHECK_EQ (line[i].at.beat, static_cast<int> (i % 4));
        CHECK_EQ (line[i].measureIndex, static_cast<int> (i / 4));
    }
}

TEST ("the root lands on the beat the chord arrives")
{
    // The one note a walking line is not free about - it is what states the
    // harmony, and everything else is travel between two of them.
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Ebmaj7 |");

    for (std::uint32_t seed = 0; seed < 20; ++seed)
        for (const auto& note : walkingBass (chart, 0, 3, seed))
            if (note.role == BassRole::root)
            {
                CHECK_EQ (note.at.beat, 0);
                CHECK_EQ (pitchClassOf (note.midiNote), rootPitchClassOf (note.chordSymbol));
            }
}

TEST ("a bar of two chords puts a root under each of them")
{
    const auto chart = chartOf ("| Dm7 G7 | Cmaj7 |");
    const auto line = walkingBass (chart, 0, 1, 3);

    CHECK_EQ (pitchClassOf (line[0].midiNote), rootPitchClassOf ("Dm7"));
    CHECK (line[0].role == BassRole::root);

    // The second chord arrives halfway through the bar, so its root does too.
    CHECK_EQ (pitchClassOf (line[2].midiNote), rootPitchClassOf ("G7"));
    CHECK (line[2].role == BassRole::root);
}

TEST ("the beat before a change leads into the next root")
{
    /*  A semitone either side, or a fifth. Those are the approaches every bass
        player has, and between them they are what makes a line sound like
        walking rather than like an arpeggio repeated once a bar. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Ebmaj7 | Am7 | D7 | Gmaj7 | Gmaj7 |");

    for (std::uint32_t seed = 0; seed < 20; ++seed)
    {
        const auto line = walkingBass (chart, 0, 7, seed);

        for (std::size_t i = 0; i + 1 < line.size(); ++i)
        {
            if (line[i].role != BassRole::approach)
                continue;

            const auto gap = std::abs (line[i + 1].midiNote - line[i].midiNote);

            CHECK (gap == 1 || gap == 7 || gap == 5 || gap == 11 || gap == 13);
        }
    }
}

TEST ("a walking line never plays the same note twice in a row")
{
    // The first version of this played D, C, D, D over one bar of Dm7, because
    // "the nearest chord tone" walks straight back where it came from. A run
    // that knows where it has to be by its last beat does not.
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 |");

    for (std::uint32_t seed = 0; seed < 30; ++seed)
    {
        const auto line = walkingBass (chart, 0, 7, seed);

        for (std::size_t i = 1; i < line.size(); ++i)
            CHECK (line[i].midiNote != line[i - 1].midiNote);
    }
}

TEST ("a walking line stays on the instrument")
{
    /*  Voice leading on its own climbs: every note reaches for the nearest
        next one, and a tune that rises takes the line off the top of the bass
        inside a chorus. The range is what stops that, so the test is a
        progression that keeps rising. */
    const auto climbing = chartOf ("| Cmaj7 | Ebmaj7 | Gbmaj7 | Amaj7 | Cmaj7 | Ebmaj7 |"
                                   " Gbmaj7 | Amaj7 | Cmaj7 | Ebmaj7 | Gbmaj7 | Amaj7 |");

    for (std::uint32_t seed = 0; seed < 12; ++seed)
        for (const auto& note : walkingBass (climbing, 0, 11, seed))
        {
            CHECK (note.midiNote >= lowestBassNote);
            CHECK (note.midiNote <= highestBassNote);
        }
}

TEST ("a walking line steps rather than leaping about")
{
    // Walking is the word. A line that jumps a tenth every beat is doing
    // something else, whatever notes it picks.
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 |");
    const auto line = walkingBass (chart, 0, 7, 9);

    auto biggest = 0;

    for (std::size_t i = 1; i < line.size(); ++i)
        biggest = std::max (biggest, std::abs (line[i].midiNote - line[i - 1].midiNote));

    CHECK (biggest <= 12);
}

TEST ("the same seed walks the same line, and a different one does not")
{
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    const auto one = walkingBass (chart, 0, 3, 4);
    const auto same = walkingBass (chart, 0, 3, 4);

    CHECK_EQ (one.size(), same.size());

    for (std::size_t i = 0; i < one.size(); ++i)
        CHECK_EQ (one[i].midiNote, same[i].midiNote);

    // Different seeds should not reliably give the same line - if they did the
    // seed would be decoration.
    auto anyDifferent = false;

    for (std::uint32_t seed = 0; seed < 12; ++seed)
    {
        const auto other = walkingBass (chart, 0, 3, seed);

        for (std::size_t i = 0; i < one.size() && i < other.size(); ++i)
            if (other[i].midiNote != one[i].midiNote)
                anyDifferent = true;
    }

    CHECK (anyDifferent);
}

TEST ("a waltz walks in three")
{
    const auto waltz = chartOf ("| Dm7 | G7 | Cmaj7 |", 3);
    const auto line = walkingBass (waltz, 0, 2, 6);

    CHECK_EQ (line.size(), std::size_t (9));

    for (const auto& note : line)
        CHECK (note.at.beat >= 0 && note.at.beat < 3);
}
