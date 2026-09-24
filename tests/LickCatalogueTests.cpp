#include "TestFramework.h"
#include "jazz/core/LickCatalogue.h"
#include "jazz/core/LineStyle.h"

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

TEST ("a lick's notes sit on its own subdivision, ornaments excepted")
{
    /*  The exception is the point rather than a let-off: a grace note or a
        crush is played *off* the grid by definition - it takes its time from
        the note before it and lands with its target. Everything a player would
        count is on the lick's own feel. */
    for (const auto& lick : licks())
    {
        const auto step = ticksFor (lick.feel);
        CHECK (step > 0);

        for (const auto& note : lick.notes)
        {
            if (note.ornament != LickOrnament::none)
                continue;

            CHECK_EQ (note.tick % step, 0);
        }
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
