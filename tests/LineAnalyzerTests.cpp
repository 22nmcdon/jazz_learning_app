#include "TestFramework.h"
#include "jazz/core/LineAnalyzer.h"

#include <string>

using namespace jazz::core;

namespace
{
    ChordSymbol chordFrom (const std::string& text)
    {
        const auto chord = ChordSymbol::parse (text);
        CHECK (chord.has_value());
        return *chord;
    }

    NoteColour colourOf (int midiNote, const std::string& symbol)
    {
        return LineAnalyzer::read (midiNote, chordFrom (symbol)).colour;
    }

    /** Field by field, never `{ a, b, c }`: a fourth tier landed in the middle
        of this struct once, and positional init would have gone on compiling. */
    LineStats statsOf (int chordTones, int scaleTones, int outside, int approachTones = 0)
    {
        LineStats stats;
        stats.chordTones = chordTones;
        stats.scaleTones = scaleTones;
        stats.approachTones = approachTones;
        stats.outside = outside;
        return stats;
    }

    /** Plays a run of notes into an armed take over one bar. */
    void playAll (LineAnalyzer& analyzer, const std::vector<int>& notes)
    {
        for (auto note : notes)
            analyzer.play (note);
    }

    bool mentions (const TakeSummary& take, const std::string& fragment)
    {
        if (take.summary.find (fragment) != std::string::npos)
            return true;

        for (const auto& observation : take.observations)
            if (observation.find (fragment) != std::string::npos)
                return true;

        return false;
    }
}

//==============================================================================
TEST ("a chord tone is read as one, wherever it is on the keyboard")
{
    // F is the b3 of Dm7, in three octaves.
    CHECK (colourOf (41, "Dm7") == NoteColour::chordTone);
    CHECK (colourOf (53, "Dm7") == NoteColour::chordTone);
    CHECK (colourOf (77, "Dm7") == NoteColour::chordTone);
}

TEST ("a note names its degree against the chord, not just its colour")
{
    const auto seventh = LineAnalyzer::read (60, chordFrom ("Dm7"));   // C over Dm7

    CHECK (seventh.colour == NoteColour::chordTone);
    CHECK_EQ (seventh.degree, std::string ("b7"));
    CHECK_EQ (seventh.chordSymbol, std::string ("Dm7"));
}

TEST ("the third of a minor chord is spelled as a third, not a sharp ninth")
{
    CHECK_EQ (LineAnalyzer::read (53, chordFrom ("Dm7")).degree, std::string ("b3"));
}

TEST ("a note in a scale that fits, but not in the chord, is a scale tone")
{
    // E over Dm7: the 9th. Dorian has it, the chord does not.
    const auto ninth = LineAnalyzer::read (64, chordFrom ("Dm7"));

    CHECK (ninth.colour == NoteColour::scaleTone);
    CHECK_EQ (ninth.degree, std::string ("9"));
    CHECK (! ninth.scaleName.empty());
}

TEST ("a scale tone says which scale accounts for it")
{
    // B over Dm7 is the 13th - Dorian, not Aeolian, is what has it.
    const auto thirteenth = LineAnalyzer::read (71, chordFrom ("Dm7"));

    CHECK (thirteenth.colour == NoteColour::scaleTone);
    CHECK (thirteenth.scaleName.find ("D ") == 0);
}

TEST ("a note the scale does not have is outside")
{
    // Bb over Cmaj7, read against C Ionian - the minor seventh of a major chord.
    const auto note = LineAnalyzer::read (70, chordFrom ("Cmaj7"));

    CHECK (note.colour == NoteColour::outside);
    CHECK (note.scaleName.empty());
}

TEST ("an outside note still says where it sits, because that is the useful part")
{
    // Db over Cmaj7 - a b9. "Outside" alone tells a player nothing they can use.
    const auto note = LineAnalyzer::read (61, chordFrom ("Cmaj7"));

    CHECK (note.colour == NoteColour::outside);
    CHECK_EQ (note.degree, std::string ("b9"));
}

TEST ("the fourth over a major seventh is a scale tone, and is named as an avoid note")
{
    // F over Cmaj7 is in C Ionian and clashes with the third. A line passes
    // through it constantly, so it must not be demoted - only pointed at.
    const auto note = LineAnalyzer::read (65, chordFrom ("Cmaj7"));

    CHECK (note.colour == NoteColour::scaleTone);
    CHECK (note.avoidNote);
}

TEST ("a scale where the note is not an avoid note is named ahead of one where it is")
{
    // D over Cmaj7 is the 9th: plainly fine, and no scale should be blamed for it.
    const auto note = LineAnalyzer::read (62, chordFrom ("Cmaj7"));

    CHECK (note.colour == NoteColour::scaleTone);
    CHECK (! note.avoidNote);
}

TEST ("accepting every scale at once leaves almost nothing outside anything")
{
    // This is why it is not the default. Between them the scales the engine
    // offers for a chord cover nearly the whole octave, so the three tiers
    // collapse into two and the feedback stops saying anything. The numbers are
    // the argument, so they are the test.
    LineAnalyzer::Options everything;
    everything.acceptAnyValidScale = true;

    for (const auto* symbol : { "Cmaj7", "G7", "Bbmaj7" })
    {
        auto outside = 0;

        for (auto pitch = 0; pitch < 12; ++pitch)
            if (LineAnalyzer::read (60 + pitch, chordFrom (symbol), everything).colour == NoteColour::outside)
                ++outside;

        CHECK_EQ (outside, 0);
    }
}

TEST ("one scale at a time leaves a line a player can learn from")
{
    // The same three chords, read against one scale each: four chord tones,
    // three more in the scale, and five that are neither.
    for (const auto* symbol : { "Cmaj7", "G7", "Bbmaj7" })
    {
        LineStats spread;

        for (auto pitch = 0; pitch < 12; ++pitch)
        {
            const auto note = LineAnalyzer::read (60 + pitch, chordFrom (symbol));

            if (note.colour == NoteColour::chordTone)      ++spread.chordTones;
            else if (note.colour == NoteColour::scaleTone) ++spread.scaleTones;
            else                                           ++spread.outside;
        }

        CHECK_EQ (spread.chordTones, 4);
        CHECK_EQ (spread.scaleTones, 3);
        CHECK_EQ (spread.outside, 5);
    }
}

TEST ("the scale the player chose is the one they are held to")
{
    // Bb over Dm7 is the b6: outside D Dorian, which is what the engine offers
    // first, and squarely inside D Aeolian, which a player may well have picked.
    LineAnalyzer::Options chosen;
    chosen.chosenScale = "D Aeolian";

    CHECK (LineAnalyzer::read (70, chordFrom ("Dm7")).colour == NoteColour::outside);
    CHECK (LineAnalyzer::read (70, chordFrom ("Dm7"), chosen).colour == NoteColour::scaleTone);
}

TEST ("a scale that does not belong to the chord falls back rather than failing")
{
    LineAnalyzer::Options nonsense;
    nonsense.chosenScale = "Q Hyperlydian";

    // Reads exactly as it would with no choice at all: the engine's own answer.
    CHECK (LineAnalyzer::read (64, chordFrom ("Dm7"), nonsense).colour
             == LineAnalyzer::read (64, chordFrom ("Dm7")).colour);
}

//==============================================================================
TEST ("a take counts only what was played while it was running")
{
    LineAnalyzer analyzer;
    analyzer.setTarget (0, chordFrom ("Dm7"));

    // Warming up before arming: read back, never counted.
    analyzer.play (62);
    analyzer.play (65);
    CHECK_EQ (analyzer.stats().total(), 0);

    analyzer.startTake();
    playAll (analyzer, { 62, 65, 69 });
    CHECK_EQ (analyzer.stats().total(), 3);

    analyzer.endTake();
    analyzer.play (72);
    CHECK_EQ (analyzer.stats().total(), 3);
}

TEST ("a note played with no take running is still read back")
{
    LineAnalyzer analyzer;
    analyzer.setTarget (0, chordFrom ("Dm7"));

    // The keyboard lights up before you arm; it is the counting that waits.
    CHECK (analyzer.play (62).colour == NoteColour::chordTone);
    CHECK (! analyzer.isTaking());
}

TEST ("disarming keeps the take, because that is when it becomes worth reading")
{
    LineAnalyzer analyzer;
    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.startTake();
    playAll (analyzer, { 62, 65, 69, 72 });
    analyzer.endTake();

    CHECK_EQ (analyzer.summary().overall.total(), 4);
    CHECK (! analyzer.isTaking());
}

TEST ("arming again starts from nothing")
{
    LineAnalyzer analyzer;
    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.startTake();
    playAll (analyzer, { 62, 65, 69 });
    analyzer.endTake();

    analyzer.startTake();
    CHECK_EQ (analyzer.stats().total(), 0);
}

TEST ("walking to another bar during a take keeps the same take")
{
    LineAnalyzer analyzer;
    analyzer.startTake();

    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 62, 65 });

    analyzer.setTarget (1, chordFrom ("G7"));
    playAll (analyzer, { 67, 71, 65 });

    // One take, five notes, two bars - not two takes.
    CHECK_EQ (analyzer.stats().total(), 5);
    CHECK_EQ (static_cast<int> (analyzer.summary().bars.size()), 2);
}

TEST ("a bar's stats are that bar's, not the take's")
{
    LineAnalyzer analyzer;
    analyzer.startTake();

    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 62, 65, 69, 72 });     // D F A C - all chord tones

    analyzer.setTarget (1, chordFrom ("G7"));
    playAll (analyzer, { 61, 63 });             // Db Eb over G7

    const auto first = analyzer.statsForBar (0);
    CHECK_EQ (first.total(), 4);
    CHECK_EQ (first.chordTones, 4);

    const auto second = analyzer.statsForBar (1);
    CHECK_EQ (second.total(), 2);
    CHECK_EQ (second.chordTones, 0);
}

TEST ("a bar that was never played over has no stats rather than an error")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.play (62);

    CHECK_EQ (analyzer.statsForBar (7).total(), 0);
}

TEST ("walking back to a bar adds to it rather than making a second one")
{
    LineAnalyzer analyzer;
    analyzer.startTake();

    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.play (62);

    analyzer.setTarget (1, chordFrom ("G7"));
    analyzer.play (67);

    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.play (65);

    const auto take = analyzer.summary();
    CHECK_EQ (static_cast<int> (take.bars.size()), 2);
    CHECK_EQ (analyzer.statsForBar (0).total(), 2);
}

TEST ("the percentages in a summary add up to a hundred")
{
    /*  0.505 and 0.495 both round up, so shares rounded on their own make 101
        often enough to be seen. This is the case that used to.

        Written out field by field rather than as `{ a, b, c }`. A fourth tier
        was added between two of these, and positional init went on compiling
        while quietly meaning something else - which is the same trap
        `ScaleSuggester::with()` exists to close. */
    for (auto chordTones = 0; chordTones <= 5; ++chordTones)
    {
        for (auto scaleTones = 0; scaleTones <= 5; ++scaleTones)
        {
            for (auto approachTones = 0; approachTones <= 5; ++approachTones)
            {
                for (auto outside = 0; outside <= 5; ++outside)
                {
                    const auto stats = statsOf (chordTones, scaleTones, outside, approachTones);

                    if (stats.total() == 0)
                        continue;

                    const auto sum = stats.percentChordTones()
                                   + stats.percentScaleTones()
                                   + stats.percentApproachTones()
                                   + stats.percentOutside();

                    CHECK_EQ (sum, 100);
                }
            }
        }
    }
}

TEST ("an empty take says so rather than reporting nothing as zero per cent")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.endTake();

    const auto take = analyzer.summary();
    CHECK_EQ (take.overall.total(), 0);
    CHECK (mentions (take, "Nothing played"));
    CHECK (take.observations.empty());
}

TEST ("a summary counts its notes and its bars")
{
    LineAnalyzer analyzer;
    analyzer.startTake();

    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 62, 65, 69 });

    analyzer.setTarget (1, chordFrom ("G7"));
    playAll (analyzer, { 67, 71 });

    analyzer.endTake();

    CHECK (mentions (analyzer.summary(), "5 notes over 2 bars"));
}

TEST ("one note over one bar is not written as plurals")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.play (62);
    analyzer.endTake();

    CHECK (mentions (analyzer.summary(), "1 note over 1 bar"));
}

TEST ("a line that is all chord tones is told so, without being told it is wrong")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 62, 65, 69, 72, 62, 65, 69, 72 });
    analyzer.endTake();

    const auto take = analyzer.summary();
    CHECK_EQ (take.overall.percentChordTones(), 100);
    CHECK (mentions (take, "chord tones"));
    CHECK (! mentions (take, "wrong"));
}

TEST ("a line mostly outside is told what to do about it, not that it failed")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Cmaj7"));

    // Bb and Eb over Cmaj7: nothing the engine offers for a major seventh has them.
    playAll (analyzer, { 70, 63, 70, 63, 70, 63 });
    analyzer.endTake();

    const auto take = analyzer.summary();
    CHECK_EQ (take.overall.outside, 6);
    CHECK (mentions (take, "outside"));
    CHECK (! mentions (take, "wrong"));
    CHECK (! mentions (take, "bad"));
}

TEST ("the one bar that pulled away is named, when it stands apart from the rest")
{
    LineAnalyzer analyzer;
    analyzer.startTake();

    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 62, 65, 69, 72, 62, 65, 69, 72 });

    analyzer.setTarget (3, chordFrom ("Cmaj7"));
    playAll (analyzer, { 70, 63, 70, 63, 70, 63 });

    analyzer.endTake();

    // Bar 4 on screen, index 3 in the chart - the summary talks to the player.
    CHECK (mentions (analyzer.summary(), "Bar 4"));
}

TEST ("one bar alone is never named as the one that pulled away")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Cmaj7"));
    playAll (analyzer, { 70, 63, 70, 63, 70, 63 });
    analyzer.endTake();

    CHECK (! mentions (analyzer.summary(), "pulled away"));
}

TEST ("a note played with no bar to read it against says so rather than guessing")
{
    LineAnalyzer analyzer;
    analyzer.startTake();

    const auto note = analyzer.play (62);

    CHECK (! analyzer.hasTarget());
    CHECK (note.colour == NoteColour::outside);
    CHECK (note.degree.empty());
}

//==============================================================================
// Soloing styles: which vocabulary of scales a bar is read against.

TEST ("every style is offered with a key, a name and something to read")
{
    CHECK (! scaleStyles().empty());

    for (const auto& style : scaleStyles())
    {
        CHECK (! style.key.empty());
        CHECK (! style.name.empty());
        CHECK (! style.summary.empty());
    }
}

TEST ("the modes are the plainest style, and come first")
{
    CHECK_EQ (scaleStyles().front().key, std::string ("modes"));
}

TEST ("every style but Everything names the families it is made of")
{
    for (const auto& style : scaleStyles())
        CHECK_EQ (style.families.empty(), style.key == "everything");
}

TEST ("a style with no families named takes in the whole catalogue")
{
    const auto* everything = findScaleStyle ("everything");

    CHECK (everything != nullptr);

    if (everything == nullptr)
        return;

    for (const auto& definition : scaleCatalogue())
        CHECK (everything->includes (definition.family));
}

TEST ("an unknown style widens the answer rather than emptying it")
{
    // A key stored by another version must not leave a player with no scales.
    LineAnalyzer::Options fromTheFuture;
    fromTheFuture.style = "hyperphrygian";

    CHECK (LineAnalyzer::read (64, chordFrom ("Dm7"), fromTheFuture).colour
             == LineAnalyzer::read (64, chordFrom ("Dm7")).colour);
}

TEST ("a style changes which scale a bar is read against")
{
    LineAnalyzer::Options modes;
    modes.style = "modes";

    LineAnalyzer::Options pentatonics;
    pentatonics.style = "pentatonic";

    // G over Dm7 is the 11th: in D Dorian, and in D minor pentatonic too - but
    // the two styles must name different scales for it.
    const auto inModes = LineAnalyzer::read (67, chordFrom ("Dm7"), modes);
    const auto inPentatonics = LineAnalyzer::read (67, chordFrom ("Dm7"), pentatonics);

    CHECK (inModes.colour == NoteColour::scaleTone);
    CHECK (inPentatonics.colour == NoteColour::scaleTone);
    CHECK (inModes.scaleName != inPentatonics.scaleName);
}

TEST ("a style narrows what counts as inside")
{
    // E over Dm7 is the 9th - squarely in D Dorian, and not in D minor
    // pentatonic, which has only five notes and no 9th among them.
    LineAnalyzer::Options pentatonics;
    pentatonics.style = "pentatonic";

    LineAnalyzer::Options modes;
    modes.style = "modes";

    CHECK (LineAnalyzer::read (64, chordFrom ("Dm7"), modes).colour == NoteColour::scaleTone);
    CHECK (LineAnalyzer::read (64, chordFrom ("Dm7"), pentatonics).colour == NoteColour::outside);
}

TEST ("a style that has nothing for a chord falls back rather than calling everything outside")
{
    // A fully diminished chord has no bebop scale containing it. Working on
    // bebop and reaching such a bar must not make every note you play outside.
    LineAnalyzer::Options bebop;
    bebop.style = "bebop";

    auto outside = 0;

    for (auto pitch = 0; pitch < 12; ++pitch)
        if (LineAnalyzer::read (60 + pitch, chordFrom ("Cdim7"), bebop).colour == NoteColour::outside)
            ++outside;

    CHECK (outside < 12);
}

TEST ("a scale chosen from within the style is the one that is read against")
{
    LineAnalyzer::Options chosen;
    chosen.style = "modes";
    chosen.chosenScale = "D Aeolian";

    // Bb over Dm7: outside Dorian, inside Aeolian, and both are modes.
    CHECK (LineAnalyzer::read (70, chordFrom ("Dm7"), chosen).colour == NoteColour::scaleTone);
}

TEST ("changing the style overrides a scale left behind by the last one")
{
    /* The two can disagree - pick D Dorian under the modes, then switch to
       pentatonics - and the style wins, deliberately. Changing the vocabulary
       is the newer and more sweeping instruction of the two; a scale name left
       over from before quietly overriding it would mean choosing a style and
       watching nothing happen. (The page drops the stale choice when the style
       changes, so this is the belt to that pair of braces.) */
    LineAnalyzer::Options stale;
    stale.style = "pentatonic";
    stale.chosenScale = "D Dorian";

    CHECK (LineAnalyzer::read (64, chordFrom ("Dm7"), stale).colour == NoteColour::outside);
}

//==============================================================================
// The score. It is the only judgement in this file, so it gets the most tests:
// every constant in it is arguable, and a test is where the argument is held.


TEST ("nothing played scores nothing, rather than nothing out of nothing")
{
    CHECK (statsOf (0, 0, 0).score() == 0);
}

TEST ("chord tones anchoring and scale tones colouring is the top of the scale")
{
    CHECK (statsOf (2, 2, 0).score() == 100);
    CHECK (statsOf (5, 4, 0).score() == 100);
}

TEST ("a bar that never leaves the chord does not reach the top")
{
    const auto plain = statsOf (6, 0, 0).score();

    CHECK (plain < 100);
    CHECK (plain >= 80);   // safe ground is still ground: this is not a failure
}

TEST ("leaning off the chord costs exactly what leaning onto it does")
{
    // A line that never touches a chord tone is as one-sided as one that never
    // leaves them. Neither is wrong, and the reading says the same of both.
    CHECK (statsOf (6, 0, 0).score() == statsOf (0, 6, 0).score());
}

TEST ("two notes are not unbalanced, they are two notes")
{
    // The same one-sidedness, in a bar too short for it to mean anything.
    CHECK (statsOf (1, 0, 0).score() > statsOf (6, 0, 0).score());
}

TEST ("outside pulls a bar down, but never to nothing")
{
    const auto clean = statsOf (2, 2, 0).score();
    const auto some  = statsOf (2, 2, 2).score();
    const auto lots  = statsOf (1, 1, 6).score();

    CHECK (some < clean);
    CHECK (lots < some);
    CHECK (lots > 0);   // an outside note is a choice, not a mistake
}

TEST ("a bar of nothing but outside still scores the quarter it is worth")
{
    const auto all = statsOf (0, 0, 8).score();

    CHECK (all == 25);
}

TEST ("the score stays inside its own range, whatever it is given")
{
    for (int chordTones = 0; chordTones <= 8; ++chordTones)
        for (int scaleTones = 0; scaleTones <= 8; ++scaleTones)
            for (int outside = 0; outside <= 8; ++outside)
            {
                const auto score = statsOf (chordTones, scaleTones, outside).score();

                CHECK (score >= 0);
                CHECK (score <= 100);
            }
}

TEST ("a take's bars are scored one by one, not all together")
{
    LineAnalyzer analyzer;
    analyzer.startTake();

    // Bar one: the root and the ninth of Dm7 - anchored and coloured.
    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.play (62);
    analyzer.play (64);

    // Bar two: two notes in neither the chord nor the scale.
    analyzer.setTarget (1, chordFrom ("Cmaj7"));
    analyzer.play (61);
    analyzer.play (66);

    CHECK (analyzer.statsForBar (0).score() > analyzer.statsForBar (1).score());
    CHECK (analyzer.statsForBar (1).score() == 25);
}

//==============================================================================
// The window. Everything below needs more than one note to decide, which is the
// whole point of it - and is why none of it belongs in `read()`.

namespace
{
    /** The colours of a run of notes over one chord, after the take has seen
        all of them. Every test here is about how a reading changes once the
        note after it arrives, so reading them back at the end is the only
        honest way to look. */
    std::vector<NoteColour> coloursAfter (const std::string& symbol, const std::vector<int>& notes)
    {
        LineAnalyzer analyzer;
        analyzer.startTake();
        analyzer.setTarget (0, chordFrom (symbol));
        playAll (analyzer, notes);

        std::vector<NoteColour> colours;

        for (const auto& note : analyzer.notes())
            colours.push_back (note.colour);

        return colours;
    }
}

TEST ("a note on its own is never an approach, however it looks")
{
    // Db over Dm7 is a semitone from the root either way, and `read()` cannot
    // know whether the line is about to use that. It says what it sees.
    CHECK (colourOf (61, "Dm7") == NoteColour::outside);
    CHECK (LineAnalyzer::read (61, chordFrom ("Dm7")).colour != NoteColour::approach);
}

TEST ("an outside note that steps home is an approach, not a miss")
{
    // Db, then D: the root of Dm7 approached from a semitone below.
    const auto colours = coloursAfter ("Dm7", { 61, 62 });

    CHECK_EQ (static_cast<int> (colours.size()), 2);
    CHECK (colours[0] == NoteColour::approach);
    CHECK (colours[1] == NoteColour::chordTone);
}

TEST ("the reading only improves once the note after it has arrived")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));

    // Read the moment it is played, it is outside - and correctly so.
    CHECK (analyzer.play (61).colour == NoteColour::outside);
    CHECK (analyzer.statsForBar (0).outside == 1);

    analyzer.play (62);

    CHECK (analyzer.notes().front().colour == NoteColour::approach);
    CHECK_EQ (analyzer.statsForBar (0).outside, 0);
    CHECK_EQ (analyzer.statsForBar (0).approachTones, 1);
}

TEST ("an outside note that leaps away stays outside")
{
    // Db, then A: a fifth away, resolving nothing.
    const auto colours = coloursAfter ("Dm7", { 61, 69 });

    CHECK (colours[0] == NoteColour::outside);
}

TEST ("two outside notes in a row are only excused if they enclose something")
{
    // Db then Eb, then a leap away: neither went anywhere.
    const auto wandering = coloursAfter ("Dm7", { 61, 63, 72 });

    CHECK (wandering[0] == NoteColour::outside);
    CHECK (wandering[1] == NoteColour::outside);

    // Eb above, Db below... no: Eb (63) above D (62), C# (61) below it. Both
    // sides of the root, then the root. That is an enclosure, and both notes
    // were the line aiming rather than missing.
    const auto enclosing = coloursAfter ("Dm7", { 63, 61, 62 });

    CHECK (enclosing[0] == NoteColour::approach);
    CHECK (enclosing[1] == NoteColour::approach);
    CHECK (enclosing[2] == NoteColour::chordTone);
}

TEST ("an approach resolves into a scale tone as readily as into a chord tone")
{
    /*  This test started out asserting the opposite, on the assumption that Eb
        and E over Dm7 were two outside notes circling the root. E is the 9th -
        it is in D Dorian - so Eb steps into it, and that is a chromatic
        approach like any other. The target is anything the line landed on, not
        the chord in particular. */
    const auto colours = coloursAfter ("Dm7", { 63, 64 });

    CHECK (colours[1] == NoteColour::scaleTone);
    CHECK (colours[0] == NoteColour::approach);
}

TEST ("two outside notes on the same side of a target are not an enclosure")
{
    /*  An enclosure takes the target from both sides; this takes it twice from
        above. Over a pentatonic both Eb and E are outside, which is what makes
        the case constructible at all - in a seven-note scale there is not room
        for two outside notes a step apart on one side. */
    LineAnalyzer::Options pentatonic;
    pentatonic.style = "pentatonic";

    LineAnalyzer analyzer { pentatonic };
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 63, 64, 62 });

    CHECK (analyzer.notes()[0].colour == NoteColour::outside);
    CHECK (analyzer.notes()[1].colour == NoteColour::outside);
    CHECK (analyzer.notes()[2].colour == NoteColour::chordTone);
}

TEST ("a passing tone through a gap the scale leaves open is an approach")
{
    /*  Over a pentatonic the gaps are wide enough to pass through by a tone,
        where a seven-note scale would have made it a semitone. D pentatonic
        minor is D F G A C, so B is outside; A - B - C steps up through it. */
    LineAnalyzer::Options pentatonic;
    pentatonic.style = "pentatonic";

    LineAnalyzer analyzer { pentatonic };
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 69, 71, 72 });

    CHECK (analyzer.notes()[1].colour == NoteColour::approach);
}

TEST ("an approach note says what it resolved into")
{
    const LineAnalyzer::Options options;

    LineAnalyzer analyzer { options };
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.play (61);
    analyzer.play (62);

    CHECK_EQ (analyzer.notes().front().resolvesTo, 62);
    CHECK_EQ (static_cast<int> (analyzer.resolvedByLastNote().size()), 1);
    CHECK_EQ (analyzer.resolvedByLastNote().front().midiNote, 61);
}

TEST ("a note that resolved nothing is reported as resolving nothing")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.play (62);
    analyzer.play (69);

    CHECK (analyzer.resolvedByLastNote().empty());
}

TEST ("running chromatically into the next bar is not punished for crossing the barline")
{
    /*  Db is outside Dm7 and a semitone above the root of the bar after it.
        Landing the next chord from a semitone away is one of the most
        idiomatic things in the idiom, and a window that stopped at the barline
        would call it a mistake at the moment it was working. */
    LineAnalyzer analyzer;
    analyzer.startTake();

    analyzer.setTarget (0, chordFrom ("Dm7"));
    analyzer.play (62);
    analyzer.play (61);

    analyzer.setTarget (1, chordFrom ("Cmaj7"));
    analyzer.play (60);

    CHECK (analyzer.notes()[1].colour == NoteColour::approach);
    CHECK_EQ (analyzer.notes()[1].measureIndex, 0);   // it still belongs to the bar it was played in
    CHECK_EQ (analyzer.statsForBar (0).outside, 0);
    CHECK_EQ (analyzer.statsForBar (0).approachTones, 1);
}

TEST ("approach notes count as landing, so a chromatic line scores better than a lost one")
{
    // The same four outside pitches: once resolving, once wandering.
    LineAnalyzer resolving;
    resolving.startTake();
    resolving.setTarget (0, chordFrom ("Dm7"));
    playAll (resolving, { 61, 62, 64, 65 });

    LineAnalyzer wandering;
    wandering.startTake();
    wandering.setTarget (0, chordFrom ("Dm7"));
    playAll (wandering, { 61, 68, 61, 68 });

    CHECK (resolving.stats().score() > wandering.stats().score());
    CHECK_EQ (resolving.stats().outside, 0);
}

TEST ("a bar that never left the chord is flagged, and one that did is not")
{
    LineAnalyzer analyzer;
    analyzer.startTake();

    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 62, 65, 69, 72 });          // all chord tones

    analyzer.setTarget (1, chordFrom ("Cmaj7"));
    playAll (analyzer, { 60, 62, 64, 67 });          // D is the 9th

    const auto take = analyzer.summary();

    CHECK_EQ (static_cast<int> (take.bars.size()), 2);
    CHECK (take.bars[0].neverLeftTheChord);
    CHECK (! take.bars[1].neverLeftTheChord);
    CHECK (mentions (take, "Bar 1 never left the chord"));
    CHECK (! mentions (take, "wrong"));
}

TEST ("an approach note does not count as colour for the bar flag")
{
    // Chord tones and one note passing between two of them. The line never
    // said anything about this chord, and the flag is about that.
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 62, 63, 62, 65 });   // Eb passes D - D - F... 

    const auto take = analyzer.summary();

    CHECK_EQ (take.overall.scaleTones, 0);
    CHECK (take.bars[0].neverLeftTheChord);
}

TEST ("leaps are counted, and so is whether the line stepped away from them")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));

    // Up a seventh and step back; up a seventh and leap again.
    playAll (analyzer, { 62, 72, 71, 60, 70, 60, 70, 62 });

    const auto take = analyzer.summary();

    CHECK (take.leaps >= 3);
    CHECK (take.leapsResolved < take.leaps);
    CHECK (mentions (take, "big jumps"));
}

TEST ("a line that steps away from every leap is not told about leaps")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 62, 69, 67, 60, 62, 69, 67, 65 });

    CHECK (! mentions (analyzer.summary(), "big jumps"));
}

TEST ("a take that never leaves a hand's width is told to use the horn")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 62, 64, 65, 67, 69, 67, 65, 64 });

    const auto take = analyzer.summary();

    CHECK (take.rangeInSemitones() < 12);
    CHECK (mentions (take, "whole range"));
}

TEST ("a take that covers two octaves is not told to spread out")
{
    LineAnalyzer analyzer;
    analyzer.startTake();
    analyzer.setTarget (0, chordFrom ("Dm7"));
    playAll (analyzer, { 50, 53, 57, 62, 65, 69, 74, 77 });

    const auto take = analyzer.summary();

    CHECK (take.rangeInSemitones() >= 24);
    CHECK (! mentions (take, "whole range"));
}

TEST ("the range of a take that was never played is nothing, not a negative")
{
    LineAnalyzer analyzer;
    analyzer.startTake();

    CHECK_EQ (analyzer.summary().rangeInSemitones(), 0);
}

TEST ("the window works without a take, and still counts nothing")
{
    /*  Someone trying things out has not armed anything, and is the person most
        likely to be experimenting with chromatic notes. Telling them those were
        misses is the lesson this whole window exists to stop - so it runs, and
        says what it found, without a single note being counted. */
    LineAnalyzer analyzer;
    analyzer.setTarget (0, chordFrom ("Dm7"));

    analyzer.play (61);
    analyzer.play (62);

    CHECK_EQ (static_cast<int> (analyzer.resolvedByLastNote().size()), 1);
    CHECK_EQ (analyzer.resolvedByLastNote().front().midiNote, 61);

    CHECK (analyzer.notes().empty());
    CHECK_EQ (analyzer.stats().total(), 0);
}

TEST ("arming clears the window, so a take does not inherit the note before it")
{
    LineAnalyzer analyzer;
    analyzer.setTarget (0, chordFrom ("Dm7"));

    analyzer.play (61);      // outside, before the take
    analyzer.startTake();
    analyzer.play (62);      // the take's first note

    CHECK (analyzer.resolvedByLastNote().empty());
    CHECK_EQ (analyzer.stats().total(), 1);
}

TEST ("the window does not grow into a second take")
{
    LineAnalyzer analyzer;
    analyzer.setTarget (0, chordFrom ("Dm7"));

    for (auto i = 0; i < 40; ++i)
        analyzer.play (62 + (i % 5));

    CHECK (analyzer.notes().empty());
    CHECK_EQ (analyzer.stats().total(), 0);
}
