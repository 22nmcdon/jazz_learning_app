#include "TestFramework.h"
#include "jazz/core/Comping.h"
#include "jazz/core/LineAnalyzer.h"
#include "jazz/core/VoicingAnalyzer.h"

#include <map>
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

    /** What the generator played, handed back as though a player had played it.

        The whole of the both-directions check: a `PlayedHit` deliberately drops
        the two things a `CompHit` knows and a player does not - which chord it
        voices and whether it pushed - so the evaluator has to work both out
        again from the notes and the position alone.
    */
    std::vector<PlayedHit> playedFrom (const CompPlan& plan)
    {
        std::vector<PlayedHit> played;

        for (const auto& hit : plan.hits)
            played.push_back (PlayedHit { hit.measureIndex, hit.at, hit.midiNotes });

        return played;
    }

    PlayedHit playedAt (int measureIndex, BarPosition at, std::vector<int> notes)
    {
        return PlayedHit { measureIndex, at, std::move (notes) };
    }

    bool saysOf (const CompEvaluation& comp, const std::string& fragment)
    {
        for (const auto& observation : comp.observations)
            if (observation.find (fragment) != std::string::npos)
                return true;

        return false;
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

TEST ("a feel written down can be read back")
{
    /*  The round trip, against `subdivisionName`'s own output rather than
        against a second list of spellings written here - the two directions
        are one table, and a name added to one cannot go missing from the
        other. */
    for (auto subdivision : { Subdivision::beat, Subdivision::eighth,
                              Subdivision::tripletEighth, Subdivision::sixteenth })
    {
        const auto read = subdivisionFrom (subdivisionName (subdivision));

        CHECK (read.has_value());
        CHECK (*read == subdivision);
    }
}

TEST ("there is one list of feels, and everything walks it")
{
    /*  `subdivisionFrom` read against a list written out where it stood, and
        a shell offering the choice would have written out a second. Both now
        walk this one, so a subdivision added to the enum and forgotten here is
        a hole with a single place to plug rather than three that drift. */
    const auto all = allSubdivisions();

    CHECK_EQ (static_cast<int> (all.size()), 4);

    // Every one of them is readable, distinct, and lands on a whole tick.
    std::set<std::string> names;

    for (const auto subdivision : all)
    {
        names.insert (subdivisionName (subdivision));

        CHECK (subdivisionFrom (subdivisionName (subdivision)).has_value());
        CHECK_EQ (ticksPerBeat % ticksFor (subdivision), 0);
    }

    CHECK_EQ (static_cast<int> (names.size()), 4);
}

TEST ("a feel this version cannot read is refused, never defaulted")
{
    /*  The negative control, and the reason the return is an optional. Both
        `ticksFor` and `subdivisionName` end in a trailing `return` - a reader
        written to match them would answer `beat` for anything at all, and a
        style whose feel failed to parse would be read as straight where it
        asked for triplets. Every triplet a player landed on would then read as
        outside the style that asked for them, which is the one mistake this
        parser exists to make impossible. */
    CHECK (! subdivisionFrom ("quavers").has_value());
    CHECK (! subdivisionFrom ("").has_value());
    CHECK (! subdivisionFrom ("eighth").has_value());    // the singular is not the name
    CHECK (! subdivisionFrom ("Eighths").has_value());   // nor is the capital
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
    const CompSlot everyBeat { std::nullopt, 0, 100, false, {} };

    CHECK_EQ (slotPositions (everyBeat, 4).size(), std::size_t (4));
    CHECK_EQ (slotPositions (everyBeat, 3).size(), std::size_t (3));
    CHECK_EQ (slotPositions (everyBeat, 5).size(), std::size_t (5));
}

TEST ("a slot counting back from the end means the same thing in any metre")
{
    const CompSlot andOfLast { -1, ticksPerBeat / 2, 100, true, {} };

    CHECK_EQ (slotPositions (andOfLast, 4).front().beat, 3);
    CHECK_EQ (slotPositions (andOfLast, 3).front().beat, 2);
    CHECK_EQ (slotPositions (andOfLast, 4).front().tick, 12);
}

TEST ("a slot naming a beat the metre has not got says nothing")
{
    // A style built around the fourth beat has less to say in three. Silence
    // is more honest than folding it onto a beat that does exist.
    const CompSlot fourthBeat { 3, 0, 100, false, {} };

    CHECK (slotPositions (fourthBeat, 4).size() == 1);
    CHECK (slotPositions (fourthBeat, 3).empty());
}

TEST ("a style's own numbers cannot make the planner throw")
{
    /*  `mostPerBar` trims the bar's candidates, and a `std::size_t` cast of a
        negative is not a small number - it is about eighteen quintillion, so
        `resize` threw `std::length_error` rather than trimming. In the app
        that is an exception out of a plain call; in wasm it is a trap.

        It could not happen while `compStyles()` was the only thing that made
        one of these. It can the moment a style is described from outside, and
        a field on a plain struct should not be able to throw whoever fills it
        in - which is why this is floored here rather than only at the wire,
        where it is *also* refused. */
    CompStyleDefinition broken;
    broken.slots = { CompSlot { 0, 0, 100, false, {} }, CompSlot { 1, 0, 100, false, {} },
                     CompSlot { 2, 0, 100, false, {} }, CompSlot { 3, 0, 100, false, {} } };
    broken.mostPerBar = -1;

    const auto plan = compPlan (chartOf ("| Dm7 | G7 |"), broken, 0, 1, 7);

    /*  A comp, rather than a crash - and not an empty one. The ceiling trims
        the bar to nothing and then the *floor* tops it back up to
        `fewestPerBar`, which is 1 by default: the two ends of the density
        window are read in order, and a nonsense ceiling does not disable the
        floor. One hit per bar over two bars. */
    CHECK_EQ (static_cast<int> (plan.hits.size()), 2);
    CHECK_EQ (hitsInBar (plan, 0), 1);
    CHECK_EQ (hitsInBar (plan, 1), 1);
}

TEST ("and the flooring changes nothing about the styles that ship")
{
    /*  The negative control for the test above, and the half that matters:
        a guard that quietly altered what the catalogue plays would be a worse
        bug than the one it fixed. Every shipped style is already above the
        floor, so the clamp is arithmetic that never fires. */
    for (const auto& style : compStyles())
        CHECK (style.mostPerBar > 0);
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

TEST ("every voicing the comp plans is a shape the app itself offers")
{
    /*  The other half of the same invariant: the plan's rhythm is in style and
        its notes are a voicing the app would have suggested.

        It used to say `twoHandedRootless` and only that, which was true while
        the band knew one shape. It knows four now - the two-handed pair plain
        and rich, and the thinner one-hand pair - so what has to hold is that
        every one of them is a shape the analyser recognises as comping. */
    const auto chart = chartOf ("| Dm7 | G7alt | Cmaj7 | Am7b5 |");

    for (const auto& style : compStyles())
    {
        for (auto seed = 1; seed <= 8; ++seed)
        {
            const auto plan = compPlan (chart, style, 0, 3, static_cast<std::uint32_t> (seed));

            CHECK (! plan.isEmpty());

            for (const auto& hit : plan.hits)
            {
                const auto chord = ChordSymbol::parse (hit.chordSymbol);
                CHECK (chord.has_value());

                if (! chord.has_value())
                    continue;

                const auto voicing = Voicing::fromNotes (hit.midiNotes);
                const auto type = VoicingAnalyzer::classify (voicing, *chord);

                CHECK (type == VoicingType::twoHandedRootless
                         || type == VoicingType::rootlessLeftHand);

                /*  And never the root at the bottom, which is what kept the
                    shell out of the band's vocabulary: the bass player is
                    already playing that note, and `readCompHit` calls it a
                    real comping fault. A band playing what the app marks a
                    player for is the contradiction the style's own register
                    exists to avoid, pointed at the shape instead. */
                CHECK (toPitchClass (voicing.lowestNote()) != chord->root());
            }
        }
    }
}

TEST ("a chord is not voiced the same way every time it comes round")
{
    /*  There used to be exactly one voicing per chord, for ever: the search
        took the strict minimum and the minimum never moved, so Dm7 came out
        F3 C4 E4 B4 every time it appeared, in every chorus, in every tune.
        Nobody comps like that. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 |");

    std::map<std::string, std::set<std::vector<int>>> voicingsFor;

    for (auto seed = 1; seed <= 6; ++seed)
        for (const auto& hit : compPlan (chart, compStyleFor ("charleston"), 0, 7,
                                         static_cast<std::uint32_t> (seed)).hits)
            voicingsFor[hit.chordSymbol].insert (hit.midiNotes);

    CHECK (! voicingsFor.empty());

    for (const auto& [symbol, voicings] : voicingsFor)
    {
        (void) symbol;
        CHECK (voicings.size() > 1);
    }
}

TEST ("a different seed is a different chorus, and the same seed is the same one")
{
    /*  Both halves matter. Variety that could not be reproduced would make a
        plan untestable and a loop drift; reproducibility without variety is
        what this started as. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");
    const auto& style = compStyleFor ("charleston");

    const auto notesOf = [] (const CompPlan& plan)
    {
        std::vector<std::vector<int>> out;

        for (const auto& hit : plan.hits)
            out.push_back (hit.midiNotes);

        return out;
    };

    const auto first = notesOf (compPlan (chart, style, 0, 3, 1));

    CHECK (notesOf (compPlan (chart, style, 0, 3, 1)) == first);
    CHECK (notesOf (compPlan (chart, style, 0, 3, 2)) != first);
}

TEST ("the thinner shapes come up, and come up less often")
{
    /*  Weighted rather than equal, which is the whole of what makes this sound
        like one player rather than a shuffle: the two-handed shapes are what
        comping *is*, and a comper reaching for a thin one every other chord
        would sound like one who had run out of right hand. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 |");

    auto twoHanded = 0;
    auto oneHanded = 0;

    for (auto seed = 1; seed <= 20; ++seed)
    {
        for (const auto& hit : compPlan (chart, compStyleFor ("charleston"), 0, 7,
                                         static_cast<std::uint32_t> (seed)).hits)
        {
            const auto chord = ChordSymbol::parse (hit.chordSymbol);

            if (! chord.has_value())
                continue;

            const auto type = VoicingAnalyzer::classify (Voicing::fromNotes (hit.midiNotes), *chord);

            if (type == VoicingType::rootlessLeftHand) ++oneHanded;
            else                                       ++twoHanded;
        }
    }

    CHECK (oneHanded > 0);
    CHECK (twoHanded > oneHanded * 2);
}

TEST ("asked without a seed, a comper still plays the one obvious voicing")
{
    /*  Two questions, two answers. "What would a comper play here" is what
        `Show me a comp` shows and what a bar sounds when you land on it with
        no clock - it has one answer and should keep having it. The variety
        belongs to the band playing a chorus, not to this.

        These are the notes the README walks through, exactly: over
        | Dm7 | G7 | Cmaj7 | the hands play F3 C4 E4 B4, then F3 B3 E4 A4, then
        E3 B3 D4 A4 - two voices held each time and two moving by a semitone. */
    const auto dm7 = ChordSymbol::parse ("Dm7");
    const auto g7 = ChordSymbol::parse ("G7");
    const auto cmaj7 = ChordSymbol::parse ("Cmaj7");

    CHECK (dm7.has_value() && g7.has_value() && cmaj7.has_value());

    if (! dm7.has_value() || ! g7.has_value() || ! cmaj7.has_value())
        return;

    const auto first = compingVoicing (*dm7, {});
    const auto second = compingVoicing (*g7, first.midiNotes);
    const auto third = compingVoicing (*cmaj7, second.midiNotes);

    CHECK (first.midiNotes == std::vector<int> ({ 53, 60, 64, 71 }));    // F3 C4 E4 B4
    CHECK (second.midiNotes == std::vector<int> ({ 53, 59, 64, 69 }));   // F3 B3 E4 A4
    CHECK (third.midiNotes == std::vector<int> ({ 52, 59, 62, 69 }));    // E3 B3 D4 A4
}

TEST ("the comp leads its voicings through the whole plan, not bar by bar")
{
    /*  Planned in one pass precisely so this holds across barlines: hit to hit,
        the hands move a little. Done per bar, every downbeat would re-spell
        from scratch and leap. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cm7 | F7 | Bbmaj7 |");

    auto small = 0;
    auto moves = 0;

    for (auto seed = 1; seed <= 10; ++seed)
    {
        const auto plan = compPlan (chart, compStyleFor ("four"), 0, 5,
                                    static_cast<std::uint32_t> (seed));

        for (std::size_t i = 1; i < plan.hits.size(); ++i)
        {
            const auto moved = std::abs (plan.hits[i].midiNotes.front()
                                           - plan.hits[i - 1].midiNotes.front());

            /*  Never further than a hand reaches. This said "never more than a
                fifth" while the band took the nearest voicing every time; it
                reaches for another register now and then now, and a reach is a
                real move. What it still may not do is re-spell the chord from
                somewhere else entirely, which is the bug this was written for. */
            CHECK (moved <= 12);

            ++moves;

            if (moved <= 6)
                ++small;
        }
    }

    // And reaching stays the exception rather than the way it moves.
    CHECK (moves > 0);
    CHECK (small * 4 > moves * 3);
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
    /*  The rule the rest of this file is built on, and the one that had to
        survive solo practice gaining a placement number of its own: **this**
        number reads colour and nothing else. The same notes read against the
        same chord score the same whether or not the shell could say where
        they fell.

        `readLinePlacement` does score where they fell, against a style the
        player chose - and it is a second number, in its own file, which never
        touches a `LineStats`. That this test needed no change when it arrived
        is the evidence the split was made in the right place, which is why it
        stays exactly as it was. */
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

//==============================================================================
// The evaluator. The other half of the invariant: everything the generator
// plays for a style must come back out of the evaluator as a comp in that
// style - placement, register and density, not the slot half alone.

TEST ("the evaluator marks the generator's own comp a perfect fit")
{
    /*  The widened invariant, and the one most likely to catch a change. The
        slot half has been checked since the generator was written; this adds
        the two halves of `CompStyleDefinition` that nothing read at all -
        the register and the density - and checks them the same way, over every
        style and a sweep of seeds. A failure here means the generator and the
        style data disagree, not that the evaluator is strict. */
    const std::string progression = "| Dm7 | G7alt | Cmaj7 | Ab7 "
                                    "| Cm7b5 | F7b9 | Bb6 | Emaj9 "
                                    "| A13 | Dm9 | G7sus4 | C6/9 "
                                    "| F#m7 | B7 | Emaj7 | Emaj7 |";

    // Swept over the metre as well as the seed. The one thing this ever caught
    // was in three: a style's density is a plain count, and a count does not
    // survive a change of metre the way its slots do.
    for (const auto beatsPerBar : { 4, 3, 5 })
    {
        const auto chart = chartOf (progression, beatsPerBar);

        for (const auto& style : compStyles())
            for (std::uint32_t seed = 0; seed < 24; ++seed)
            {
                const auto plan = compPlan (chart, style, 0, 15, seed);
                const auto comp = evaluateComp (chart, style, playedFrom (plan), 0, 15);

                CHECK_EQ (comp.placementFit, 100);
                CHECK_EQ (comp.registerFit, 100);
                CHECK_EQ (comp.densityFit, 100);
                CHECK (comp.fit.has_value());
                CHECK_EQ (*comp.fit, 100);
            }
    }
}

TEST ("a hit the style never offers is not in style")
{
    /*  The negative direction, which nothing checked. An invariant that only
        ever asserts "yes" is satisfied by a function that always says yes. */
    const auto& charleston = compStyleFor ("charleston");
    const auto& ballad = compStyleFor ("ballad");

    CHECK (! fitsStyle (CompHit { 0, { 2, 6 }, { 60, 64, 67 }, "C", false }, charleston, 4));
    CHECK (! fitsStyle (CompHit { 0, { 1, 7 }, { 60, 64, 67 }, "C", false }, charleston, 4));

    // A straight eighth in a style counted in triplets is a real off-style
    // position rather than a rounding error - it is why the grid is 24.
    CHECK (! fitsStyle (CompHit { 0, { 0, 12 }, { 60, 64, 67 }, "C", false }, ballad, 4));
}

TEST ("a push from a slot that does not push is not in style")
{
    /*  The untested half of the asymmetry `fitsStyle` claims: the rule is
        one-way, and the way it does bite is a hit that pushed from a slot
        that never does. */
    const auto& charleston = compStyleFor ("charleston");
    const auto downbeat = BarPosition { 0, 0 };

    CHECK (fitsStyle (CompHit { 0, downbeat, { 60, 64, 67 }, "C", false }, charleston, 4));
    CHECK (! fitsStyle (CompHit { 0, downbeat, { 60, 64, 67 }, "C", true }, charleston, 4));
}

TEST ("a slot the metre has not got offers nothing to fit")
{
    CompStyleDefinition onFour;
    onFour.slots = { CompSlot { 3, 0, 100, false, {} } };

    CHECK (fitsStyle (CompHit { 0, { 3, 0 }, { 60 }, "C", false }, onFour, 4));

    // In three there is no fourth beat, so the style simply has nothing to say
    // rather than folding its figure onto a beat that does exist.
    CHECK (! fitsStyle (CompHit { 0, { 3, 0 }, { 60 }, "C", false }, onFour, 3));
    CHECK (slotAt ({ 3, 0 }, onFour, 3) == nullptr);
}

TEST ("a comp on positions the style never uses is marked off style, and nothing else is")
{
    const auto chart = chartOf ("| Dm7 | G7 |");
    const auto& charleston = compStyleFor ("charleston");

    // In the style's register, one chord a bar, on positions it never plays.
    const auto comp = evaluateComp (chart, charleston,
                                    { playedAt (0, { 1, 6 }, { 53, 57, 60, 65 }),
                                      playedAt (1, { 2, 18 }, { 53, 57, 59, 65 }) },
                                    0, 1);

    CHECK_EQ (comp.hitsOffStyle, 2);
    CHECK_EQ (comp.placementFit, 0);
    CHECK_EQ (comp.registerFit, 100);
    CHECK_EQ (comp.densityFit, 100);
    CHECK (saysOf (comp, "off the grid"));
}

TEST ("a comp outside the style's register is marked out of it, in either direction")
{
    const auto chart = chartOf ("| Dm7 | Dm7 |");
    const auto& charleston = compStyleFor ("charleston");
    const auto downbeat = BarPosition { 0, 0 };

    const auto low = evaluateComp (chart, charleston,
                                   { playedAt (0, downbeat, { 29, 33, 36, 41 }) }, 0, 0);

    CHECK (! low.hits.front().inRegister);
    CHECK_EQ (low.hits.front().outsideRegisterBy, compStyleFor ("charleston").lowestNote - 29);
    CHECK_EQ (low.registerFit, 0);
    CHECK_EQ (low.placementFit, 100);

    const auto high = evaluateComp (chart, charleston,
                                    { playedAt (0, downbeat, { 89, 93, 96, 101 }) }, 0, 0);

    CHECK (! high.hits.front().inRegister);
    CHECK_EQ (high.registerFit, 0);
    CHECK (saysOf (high, "outside the register"));
}

TEST ("a bar with more chords than the style plays is marked busy")
{
    const auto chart = chartOf ("| Dm7 |");
    const auto& basie = compStyleFor ("basie");

    std::vector<PlayedHit> hits;

    for (auto beat = 0; beat < 4; ++beat)
        hits.push_back (playedAt (0, { beat, 0 }, { 53, 57, 60, 65 }));

    const auto comp = evaluateComp (chart, basie, hits, 0, 0);

    CHECK (basie.mostPerBar < 4);
    CHECK (comp.bars.front().tooBusy);
    CHECK (comp.densityFit < 100);
    CHECK (saysOf (comp, "more chords in"));

    // The busy direction is knowable per hit, and is said there too.
    CHECK (comp.hits.back().oneTooMany);
}

TEST ("a bar a dense style never leaves empty, left empty, is marked sparse")
{
    /*  The other direction, and the reason a take has to say which bars it
        covered: a bar with no hits leaves no trace in the hits themselves. */
    const auto chart = chartOf ("| Dm7 | G7 |");
    const auto& four = compStyleFor ("four");

    const auto comp = evaluateComp (chart, four,
                                    { playedAt (0, { 0, 0 }, { 53, 57, 60, 65 }) }, 0, 1);

    CHECK_EQ (static_cast<int> (comp.bars.size()), 2);

    // Said, and not scored. Leaving space is what a comper does, so density is
    // graded in the busy direction only - but four to the bar is the one style
    // that means it, so it is still worth a word.
    CHECK_EQ (comp.densityFit, 100);
    CHECK (saysOf (comp, "Not a fault"));
}

TEST ("and a quiet bar under any other style is not even mentioned")
{
    const auto chart = chartOf ("| Dm7 | G7 |");

    for (const auto& style : { compStyleFor ("charleston"), compStyleFor ("basie"),
                               compStyleFor ("ballad") })
    {
        const auto comp = evaluateComp (chart, style,
                                        { playedAt (0, { 0, 0 }, { 53, 57, 60, 65 }) }, 0, 1);

        CHECK_EQ (comp.densityFit, 100);
        CHECK (! saysOf (comp, "Not a fault"));
    }
}

TEST ("a push is read as the next bar's chord")
{
    const auto chart = chartOf ("| Dm7 | G7 |");
    const auto& basie = compStyleFor ("basie");

    // The and of four, voicing G7 rather than Dm7 - B, F, A, E.
    const auto comp = evaluateComp (chart, basie,
                                    { playedAt (0, { 3, 12 }, { 59, 65, 69, 76 }) }, 0, 1);

    const auto& hit = comp.hits.front();

    CHECK (hit.anticipation);
    CHECK_EQ (static_cast<int> (hit.placement), static_cast<int> (HitPlacement::theFigure));
    CHECK_EQ (hit.chordSymbol, std::string ("G7"));
    CHECK_EQ (comp.hitsPushed, 1);
    CHECK (saysOf (comp, "across the barline"));
}

TEST ("the same position played as this bar's chord is not called a push")
{
    /*  A tie is not a push. The slot anticipates, so the question is asked -
        and the answer has to come from the notes, because that is the only
        evidence there is that the player meant the next chord. */
    const auto chart = chartOf ("| Dm7 | G7 |");
    const auto& basie = compStyleFor ("basie");

    // The same slot, voicing Dm7 - F, A, C, E.
    const auto comp = evaluateComp (chart, basie,
                                    { playedAt (0, { 3, 12 }, { 53, 57, 60, 64 }) }, 0, 1);

    const auto& hit = comp.hits.front();

    CHECK (! hit.anticipation);
    CHECK_EQ (static_cast<int> (hit.placement), static_cast<int> (HitPlacement::theFigure));
    CHECK_EQ (hit.chordSymbol, std::string ("Dm7"));
    CHECK_EQ (comp.placementFit, 100);
}

TEST ("a bar repeating its chord never reads as a push")
{
    /*  The reason a tie must not promote: over two bars of one chord the two
        readings are identical, and every hit on an anticipating slot would
        come back pushed. */
    const auto chart = chartOf ("| Dm7 | Dm7 |");
    const auto& basie = compStyleFor ("basie");

    const auto comp = evaluateComp (chart, basie,
                                    { playedAt (0, { 3, 12 }, { 53, 57, 60, 64 }) }, 0, 1);

    CHECK (! comp.hits.front().anticipation);
    CHECK_EQ (comp.hitsPushed, 0);
}

TEST ("a chord struck a hair either side of the barline belongs to the bar it is in")
{
    /*  The page quantises a moment, so a chord played just before a downbeat
        arrives as the beat past the end of the bar before - and a position read
        back from a tick count arrives as beat -1. Both are the neighbouring
        bar, and left alone both would be marked off style at a position no slot
        has ever offered. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 |");
    const auto& four = compStyleFor ("four");

    const auto late = inItsOwnBar (playedAt (0, { 4, 0 }, { 53, 57, 60, 65 }), 4);
    CHECK_EQ (late.measureIndex, 1);
    CHECK_EQ (late.at->beat, 0);

    const auto early = inItsOwnBar (playedAt (1, { -1, 12 }, { 53, 57, 60, 65 }), 4);
    CHECK_EQ (early.measureIndex, 0);
    CHECK_EQ (early.at->beat, 3);
    CHECK_EQ (early.at->tick, 12);

    const auto comp = evaluateComp (chart, four,
                                    { playedAt (0, { 4, 0 }, { 53, 57, 60, 65 }) }, 0, 2);

    CHECK_EQ (comp.hits.front().measureIndex, 1);
    CHECK_EQ (comp.placementFit, 100);
}

TEST ("the root underneath is named, and costs the fit nothing")
{
    /*  A real comping fault, and not one the style has an opinion about - so
        it produces words. Scoring it would put a number on something
        `CompStyleDefinition` never states, which is the line the fit is drawn
        on. */
    const auto chart = chartOf ("| Dm7 |");
    const auto& four = compStyleFor ("four");

    // D in the bass, under a bass player already playing it.
    const auto comp = evaluateComp (chart, four,
                                    { playedAt (0, { 0, 0 }, { 50, 57, 60, 65 }),
                                      playedAt (0, { 1, 0 }, { 50, 57, 60, 65 }),
                                      playedAt (0, { 2, 0 }, { 50, 57, 60, 65 }),
                                      playedAt (0, { 3, 0 }, { 50, 57, 60, 65 }) },
                                    0, 0);

    CHECK (comp.hits.front().takesTheBassNote);
    CHECK (comp.hits.front().rootAnywhere);
    CHECK_EQ (comp.hitsTakingTheBassNote, 4);
    CHECK (comp.fit.has_value());
    CHECK_EQ (*comp.fit, 100);
    CHECK (saysOf (comp, "bass player's note"));
}

TEST ("a rootless voicing is not asked about a root it was built without")
{
    const auto chart = chartOf ("| Dm7 |");
    const auto& four = compStyleFor ("four");

    // F A C E - rootless, which is what a comper plays over a bass line.
    const auto comp = evaluateComp (chart, four,
                                    { playedAt (0, { 0, 0 }, { 53, 57, 60, 64 }) }, 0, 0);

    CHECK (! comp.hits.front().takesTheBassNote);
    CHECK (! comp.hits.front().rootAnywhere);
    CHECK (! saysOf (comp, "bass player's note"));
}

TEST ("what the notes said is the analyser's own answer, not a second opinion")
{
    /*  Two standards, kept apart: the fit is read against the style the player
        chose, the voicing against the symbol the chart wrote. A comp perfectly
        placed and spelling nothing like the chord is exactly that - well
        placed, and saying the wrong thing. */
    const auto chart = chartOf ("| Dm7 |");
    const auto& four = compStyleFor ("four");

    std::vector<PlayedHit> hits;

    for (auto beat = 0; beat < 4; ++beat)
        hits.push_back (playedAt (0, { beat, 0 }, { 54, 58, 61, 66 }));   // Gb7-ish, over Dm7

    const auto comp = evaluateComp (chart, four, hits, 0, 0);

    CHECK_EQ (comp.placementFit, 100);
    CHECK (comp.fit.has_value());
    CHECK_EQ (*comp.fit, 100);
    CHECK (comp.voicingScore.has_value());
    CHECK (*comp.voicingScore < 100);
}

TEST ("nothing played is nothing read, not nought out of a hundred")
{
    const auto chart = chartOf ("| Dm7 | G7 |");
    const auto comp = evaluateComp (chart, compStyleFor ("four"), {}, 0, 1);

    CHECK (! comp.fit.has_value());
    CHECK (! comp.voicingScore.has_value());
    CHECK (comp.hits.empty());
    CHECK_EQ (comp.summary, std::string ("Nothing played, so there is nothing to read."));
}

TEST ("a comp with no clock behind it is read for its notes and not for its placing")
{
    /*  The same rule a solo take played statically is read by: every reading
        that needs a position is silent without one, and nothing is invented to
        fill the gap. */
    const auto chart = chartOf ("| Dm7 |");

    PlayedHit unplaced;
    unplaced.measureIndex = 0;
    unplaced.midiNotes = { 53, 57, 60, 64 };

    const auto comp = evaluateComp (chart, compStyleFor ("charleston"), { unplaced }, 0, 0);

    CHECK_EQ (static_cast<int> (comp.hits.front().placement),
              static_cast<int> (HitPlacement::unplaced));
    CHECK (! comp.fit.has_value());
    CHECK (comp.voicingScore.has_value());
    CHECK (comp.hits.front().inRegister);
    CHECK_EQ (comp.hits.front().chordSymbol, std::string ("Dm7"));
}

TEST ("the same comp is a different comp in a different style")
{
    /*  The test of the whole idea. The number is a reading against a standard
        the player chose, so the same playing read against another standard is
        honestly a different answer - which is why a verdict always names the
        style it was read against. */
    const auto chart = chartOf ("| Dm7 | G7 |");

    std::vector<PlayedHit> fourToTheBar;

    for (auto bar = 0; bar < 2; ++bar)
        for (auto beat = 0; beat < 4; ++beat)
            fourToTheBar.push_back (playedAt (bar, { beat, 0 }, { 53, 57, 60, 65 }));

    const auto asFour = evaluateComp (chart, compStyleFor ("four"), fourToTheBar, 0, 1);
    const auto asBasie = evaluateComp (chart, compStyleFor ("basie"), fourToTheBar, 0, 1);

    CHECK (asFour.fit.has_value());
    CHECK (asBasie.fit.has_value());
    CHECK_EQ (*asFour.fit, 100);
    CHECK (*asBasie.fit < *asFour.fit);
    CHECK (asBasie.bars.front().tooBusy);
}

TEST ("a comp in three is judged in three")
{
    const auto chart = chartOf ("| Dm7 | G7 |", 3);
    const auto& four = compStyleFor ("four");

    std::vector<PlayedHit> waltz;

    for (auto bar = 0; bar < 2; ++bar)
        for (auto beat = 0; beat < 3; ++beat)
            waltz.push_back (playedAt (bar, { beat, 0 }, { 53, 57, 60, 65 }));

    const auto comp = evaluateComp (chart, four, waltz, 0, 1);

    CHECK_EQ (comp.placementFit, 100);

    // Three to the bar is the whole bar in three, whatever it would be in four -
    // so nothing is said about it either way.
    CHECK_EQ (comp.bars.front().fewest, 3);
    CHECK (! comp.bars.front().tooBusy);
    CHECK (! saysOf (comp, "Not a fault"));
}

TEST ("only a style built on the push is told it never pushed")
{
    /*  Three of the four styles have an anticipating slot, and in two of them
        the push is an occasional colour rather than the figure - Charleston
        pushes at 30 against a downbeat at 95. Saying "you never pushed" about
        those is advice to play a Charleston like a Basie. */
    const auto chart = chartOf ("| Dm7 | G7 |");
    const auto onTheBeat = std::vector<PlayedHit> { playedAt (0, { 0, 0 }, { 53, 57, 60, 65 }),
                                                    playedAt (1, { 0, 0 }, { 53, 59, 65, 69 }) };

    CHECK (saysOf (evaluateComp (chart, compStyleFor ("basie"), onTheBeat, 0, 1),
                   "own figure"));

    CHECK (! saysOf (evaluateComp (chart, compStyleFor ("charleston"), onTheBeat, 0, 1),
                     "own figure"));
    CHECK (! saysOf (evaluateComp (chart, compStyleFor ("ballad"), onTheBeat, 0, 1),
                     "own figure"));
    CHECK (! saysOf (evaluateComp (chart, compStyleFor ("four"), onTheBeat, 0, 1),
                     "own figure"));
}

//==============================================================================
// The style is a figure, not a fence.
//
// Reported, and true: a swing comper varies far more than any one style's
// slots. Counted over the eight positions one actually uses - the four beats
// and the four ands - the Charleston accepted three, and no style in the
// catalogue accepted the and of one or the and of three at all. The slots are
// what the *band* plays; the feel's grid is what a player of that style may.

TEST ("every swung eighth is in style in a style counted in eighths")
{
    const auto chart = chartOf ("| Dm7 | G7 |");
    const BarPosition ands[] = { { 0, 12 }, { 1, 12 }, { 2, 12 }, { 3, 12 } };
    const BarPosition beats[] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } };

    for (const auto& key : { "basie", "charleston" })
    {
        const auto& style = compStyleFor (key);

        for (const auto& at : ands)
            CHECK (onTheGrid (at, style.feel));

        for (const auto& at : beats)
            CHECK (onTheGrid (at, style.feel));

        // And none of them reads as outside once it is actually played.
        for (const auto& at : ands)
        {
            const auto comp = evaluateComp (chart, style,
                                            { playedAt (0, at, { 53, 57, 60, 65 }) }, 0, 1);

            CHECK (comp.hits.front().placement != HitPlacement::offStyle);
            CHECK_EQ (comp.placementFit, 100);
        }
    }
}

TEST ("one and the and of one is a comp, not a mistake")
{
    /*  The report, as a test. This scored nought for placement in every style
        the app shipped, because `1&` is in nobody's slots. */
    const auto chart = chartOf ("| Dm7 | G7 |");

    const auto figure = std::vector<PlayedHit> { playedAt (0, { 0, 0 }, { 53, 57, 60, 65 }),
                                                 playedAt (0, { 0, 12 }, { 53, 57, 60, 65 }) };

    for (const auto& key : { "basie", "charleston", "ballad" })
    {
        const auto comp = evaluateComp (chart, compStyleFor (key), figure, 0, 1);

        // The ballad is counted in triplets, so a straight eighth is genuinely
        // outside it - which is the point of the tier, not an exception to it.
        if (std::string (key) == "ballad")
        {
            CHECK_EQ (comp.hitsOffStyle, 1);
            continue;
        }

        CHECK_EQ (comp.placementFit, 100);
        CHECK_EQ (comp.hitsOffStyle, 0);
        CHECK (comp.fit.has_value());
        CHECK_EQ (*comp.fit, 100);
    }
}

TEST ("the style's own figure is counted, and said, and not scored")
{
    const auto chart = chartOf ("| Dm7 | G7 |");

    // The Charleston's own downbeat, then the and of one, which is not its
    // figure and is perfectly good comping.
    const auto comp = evaluateComp (chart, compStyleFor ("charleston"),
                                    { playedAt (0, { 0, 0 }, { 53, 57, 60, 65 }),
                                      playedAt (0, { 0, 12 }, { 53, 57, 60, 65 }) },
                                    0, 1);

    CHECK_EQ (comp.hitsOnTheFigure, 1);
    CHECK_EQ (comp.hitsIdiomatic, 1);
    CHECK_EQ (comp.placementFit, 100);
    CHECK (saysOf (comp, "own figure"));
}

TEST ("widening the grid did not leave everything inside it")
{
    /*  The count the rule has to survive - the same one that killed solo
        practice's "read against every scale". A sixteenth and the first triplet
        of a beat are both still outside an eighth feel, and a straight eighth
        is outside the ballad's triplet feel. */
    const auto chart = chartOf ("| Dm7 |");
    const auto& charleston = compStyleFor ("charleston");
    const auto& ballad = compStyleFor ("ballad");

    for (const auto& at : { BarPosition { 1, 6 }, BarPosition { 1, 8 }, BarPosition { 1, 18 } })
    {
        const auto comp = evaluateComp (chart, charleston,
                                        { playedAt (0, at, { 53, 57, 60, 65 }) }, 0, 0);

        CHECK_EQ (static_cast<int> (comp.hits.front().placement),
                  static_cast<int> (HitPlacement::offStyle));
        CHECK_EQ (comp.placementFit, 0);
        CHECK (saysOf (comp, "off the grid"));
    }

    // Playing a ballad like a swing tune, which the old reading could not say.
    const auto swung = evaluateComp (chart, ballad,
                                     { playedAt (0, { 1, 12 }, { 53, 57, 60, 65 }) }, 0, 0);

    CHECK_EQ (static_cast<int> (swung.hits.front().placement),
              static_cast<int> (HitPlacement::offStyle));

    // While the beat's own triplets are its vocabulary.
    for (const auto& at : { BarPosition { 1, 8 }, BarPosition { 1, 16 } })
    {
        const auto comp = evaluateComp (chart, ballad,
                                        { playedAt (0, at, { 53, 57, 60, 65 }) }, 0, 0);

        CHECK (comp.hits.front().placement != HitPlacement::offStyle);
    }
}

TEST ("a push is read from anywhere in the last beat, not only from a slot")
{
    /*  A player leaning into the next chord from the and of three is pushing,
        whether or not the style lists that position. Reading them by the slots
        was the same mistake one level down. */
    const auto chart = chartOf ("| Dm7 | G7 |");
    const auto& charleston = compStyleFor ("charleston");

    // Beat four, voicing G7 - not one of the Charleston's slots.
    CHECK (slotAt ({ 3, 0 }, charleston, 4) == nullptr);

    const auto comp = evaluateComp (chart, charleston,
                                    { playedAt (0, { 3, 0 }, { 59, 65, 69, 76 }) }, 0, 1);

    CHECK (comp.hits.front().anticipation);
    CHECK_EQ (comp.hits.front().chordSymbol, std::string ("G7"));
    CHECK_EQ (static_cast<int> (comp.hits.front().placement),
              static_cast<int> (HitPlacement::idiomatic));
    CHECK_EQ (comp.hitsPushed, 1);
}

TEST ("a chord early in the bar is a chord in the wrong bar, not a push")
{
    const auto chart = chartOf ("| Dm7 | G7 |");

    // A G7 voicing on beat one of the Dm7 bar. Nothing about that is a push.
    const auto comp = evaluateComp (chart, compStyleFor ("charleston"),
                                    { playedAt (0, { 0, 0 }, { 59, 65, 69, 76 }) }, 0, 1);

    CHECK (! comp.hits.front().anticipation);
    CHECK_EQ (comp.hits.front().chordSymbol, std::string ("Dm7"));
}

TEST ("the band varies rather than looping one figure")
{
    /*  The other half of the same rule. Held to its slots alone the Charleston
        picks from three at 95, 90 and 30, so eight bars of one chord came out
        as very nearly the same two chords eight times. */
    const auto chart = chartOf ("| Dm7 | Dm7 | Dm7 | Dm7 | Dm7 | Dm7 | Dm7 | Dm7 |");

    for (const auto& key : { "charleston", "basie", "ballad" })
    {
        const auto plan = compPlan (chart, compStyleFor (key), 0, 7, 5);

        std::set<std::string> figures;

        for (auto bar = 0; bar < 8; ++bar)
        {
            std::string figure;

            for (const auto& hit : plan.hits)
                if (hit.measureIndex == bar)
                    figure += hit.at.describe() + " ";

            figures.insert (figure);
        }

        CHECK (figures.size() > 1);
    }

    // Four to the bar is the exception, and it is one on purpose: it plays
    // every beat and nothing else, which is the whole of what it is.
    const auto even = compPlan (chart, compStyleFor ("four"), 0, 7, 5);

    for (auto bar = 0; bar < 8; ++bar)
        CHECK_EQ (hitsInBar (even, bar), 4);
}

TEST ("trimming a busy bar keeps what the style likes most")
{
    /*  It used to sort by position and resize, which kept the *earliest* hits -
        a bias that gets much worse once the whole vocabulary is on offer. */
    const auto chart = chartOf ("| Dm7 | Dm7 | Dm7 | Dm7 | Dm7 | Dm7 | Dm7 | Dm7 |");
    const auto& basie = compStyleFor ("basie");

    // Basie's heaviest slot is the and of the last beat, at 75 - the latest
    // position in the bar, and the first thing a position-ordered trim dropped.
    auto onTheHeaviestSlot = 0;

    for (std::uint32_t seed = 0; seed < 24; ++seed)
        for (const auto& hit : compPlan (chart, basie, 0, 7, seed).hits)
            if (hit.at == BarPosition { 3, 12 })
                ++onTheHeaviestSlot;

    CHECK (onTheHeaviestSlot > 0);

    for (std::uint32_t seed = 0; seed < 24; ++seed)
        for (auto bar = 0; bar < 8; ++bar)
            CHECK (hitsInBar (compPlan (chart, basie, 0, 7, seed), bar) <= basie.mostPerBar);
}

//==============================================================================
// How long a hit rings. A style says where the chords fall and, now, how long
// they last - the difference between a Basie punch and a ballad's sustain,
// which for a long time this shape had no field for.

TEST ("every style says how long its chords ring")
{
    for (const auto& style : compStyles())
    {
        CHECK (style.heldFor > 0);

        // And every slot that overrides it says something playable rather than
        // something silent.
        for (const auto& slot : style.slots)
            CHECK (heldForSlot (slot, style) > 0);
    }
}

TEST ("a slot's own duration wins over the style's")
{
    CompStyleDefinition style;
    style.heldFor = ticksPerBeat;

    const CompSlot quiet { 0, 0, 100, false, std::nullopt };
    const CompSlot held  { 1, 0, 100, false, ticksPerBeat * 3 };

    CHECK_EQ (heldForSlot (quiet, style), ticksPerBeat);
    CHECK_EQ (heldForSlot (held, style), ticksPerBeat * 3);
}

TEST ("a stabbed style and a held one really are different lengths")
{
    /*  The point of the field, stated as a test: the two styles at the ends of
        the range have to come out sounding different, or nothing has been
        added. Compared as the styles say it rather than as one plan happened
        to come out, because a plan's durations are trimmed by where the next
        chord fell. */
    CHECK (compStyleFor ("ballad").heldFor > compStyleFor ("basie").heldFor);
    CHECK (compStyleFor ("four").heldFor < ticksPerBeat);
}

TEST ("a comped chord never rings into the one after it")
{
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 |");

    for (const auto& style : compStyles())
    {
        for (auto seed = 1; seed <= 6; ++seed)
        {
            const auto plan = compPlan (chart, style, 0, 7, static_cast<std::uint32_t> (seed));

            for (std::size_t i = 0; i + 1 < plan.hits.size(); ++i)
            {
                const auto& hit = plan.hits[i];
                const auto& next = plan.hits[i + 1];

                const auto at = hit.measureIndex * 4 * ticksPerBeat + hit.at.inTicks();
                const auto then = next.measureIndex * 4 * ticksPerBeat + next.at.inTicks();

                CHECK (hit.heldFor > 0);
                CHECK (at + hit.heldFor <= then);
            }
        }
    }
}

TEST ("a comped chord is held for what its slot asked, when there is room")
{
    /*  The trim must not be the only thing deciding lengths, or every style
        would sound the same and the field would be decoration. Over a ballad -
        the sparsest, longest style - some hit has to come out holding its full
        written length. */
    const auto chart = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");
    const auto& ballad = compStyleFor ("ballad");
    const auto plan = compPlan (chart, ballad, 0, 3, 7);

    auto full = 0;

    for (const auto& hit : plan.hits)
        if (hit.heldFor == ballad.heldFor)
            ++full;

    CHECK (! plan.hits.empty());
    CHECK (full > 0);
}

TEST ("the reading says nothing about how long a chord was held")
{
    /*  The other half of the decision. A style now says how long the *band*
        holds a chord, and that must not become a standard the player is marked
        against: a comper holding one through a four-to-the-bar is reading a
        style that does not say not to.

        Two styles alike in every way but their durations, reading the same
        chord in the same place. The readings have to be the same reading - and
        they are for a reason stronger than care, because a `PlayedHit` is a
        bar, a position and some notes, with nowhere to put a duration at all.
        This is what would fail if somebody added the field in good faith and
        the reading quietly started to use it. */
    const auto chart = chartOf ("| Dm7 | G7 |");

    auto stabbed = compStyleFor ("charleston");
    auto held = stabbed;

    held.heldFor = stabbed.heldFor * 4;

    for (auto& slot : held.slots)
        slot.heldFor = ticksPerBeat * 3;

    const PlayedHit hit { 0, BarPosition { 0, 0 }, { 53, 57, 60, 65 } };

    const auto a = readCompHit (chart, stabbed, hit);
    const auto b = readCompHit (chart, held, hit);

    CHECK_EQ (a.summary, b.summary);
    CHECK (a.placement == b.placement);
    CHECK_EQ (a.anticipation, b.anticipation);
    CHECK_EQ (a.inRegister, b.inRegister);
    CHECK_EQ (a.oneTooMany, b.oneTooMany);
    CHECK_EQ (a.voicing.score, b.voicing.score);
}
