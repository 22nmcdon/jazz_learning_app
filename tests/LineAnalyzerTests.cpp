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
    // 0.505 and 0.495 both round up, so three shares rounded on their own make
    // 101 often enough to be seen. This is the case that used to.
    for (auto chordTones = 0; chordTones <= 7; ++chordTones)
    {
        for (auto scaleTones = 0; scaleTones <= 7; ++scaleTones)
        {
            for (auto outside = 0; outside <= 7; ++outside)
            {
                const LineStats stats { chordTones, scaleTones, outside };

                if (stats.total() == 0)
                    continue;

                const auto sum = stats.percentChordTones()
                               + stats.percentScaleTones()
                               + stats.percentOutside();

                CHECK_EQ (sum, 100);
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
