#include "TestFramework.h"
#include "jazz/api/EngineApi.h"

#include <string>

using namespace jazz::api;

namespace
{
    /** True when @p json contains @p fragment verbatim.

        These tests are about the wire format both shells read, so they assert
        on the text rather than on a parsed object: the page's JSON.parse is the
        only parser involved in the real thing, and a field renamed here would
        break it silently.
    */
    bool contains (const std::string& json, const std::string& fragment)
    {
        return json.find (fragment) != std::string::npos;
    }
}

TEST ("a chart comes back as JSON the page can read")
{
    const auto json = parseChart ("| Dm7 | G7 | Cmaj7 |");

    CHECK (contains (json, "\"ok\":true"));

    // Bars arrive as a numbered list, not a count: three bars means indices
    // 0 to 2 and no third.
    CHECK (contains (json, "\"measures\":["));
    CHECK (contains (json, "\"index\":2"));
    CHECK (! contains (json, "\"index\":3"));

    CHECK (contains (json, "\"symbol\":\"Dm7\""));
    CHECK (contains (json, "\"symbol\":\"G7\""));
    CHECK (contains (json, "\"symbol\":\"Cmaj7\""));
}

TEST ("a chart that cannot be read says so rather than coming back empty")
{
    const auto json = parseChart ("| Dm7 | G13b |");

    // Either the whole parse fails or the chord is named as unreadable, but the
    // answer never pretends the chart is complete.
    CHECK (contains (json, "\"ok\":false") || contains (json, "G13b"));
}

TEST ("scales for a chord name the primary suggestion")
{
    const auto json = scalesForChord ("Dm7", "");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "Dorian"));
}

TEST ("an unreadable chord symbol is an error object, not a crash")
{
    const auto json = scalesForChord ("not a chord", "");

    CHECK (contains (json, "\"ok\":false"));
    CHECK (contains (json, "\"error\":"));
}

TEST ("naming a voicing returns the readings")
{
    // F3 C4 E4 - a rootless D minor voicing.
    const auto json = identifyChord ("53,60,64");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "Dm"));
}

TEST ("a chord no name explains returns no readings rather than a guess")
{
    const auto json = identifyChord ("60,61,62,63");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"readings\":[]"));
}

TEST ("an iReal Pro link survives a round trip through the API")
{
    const auto exported = exportIRealPro ("| Dm7 | G7 | Cmaj7 |", "Test Tune", "Nobody",
                                          "Medium Swing", 4, 4);

    CHECK (contains (exported, "\"ok\":true"));
    CHECK (contains (exported, "irealbook://"));
    CHECK (contains (exported, "Test Tune"));

    // Pull the link back out of the JSON and read it again.
    const auto linkStart = exported.find ("irealbook://");
    const auto linkEnd = exported.find ('"', linkStart);

    CHECK (linkStart != std::string::npos);
    CHECK (linkEnd != std::string::npos);

    if (linkStart == std::string::npos || linkEnd == std::string::npos)
        return;

    const auto link = exported.substr (linkStart, linkEnd - linkStart);
    const auto reimported = importIRealPro (link.c_str());

    CHECK (contains (reimported, "\"ok\":true"));
    CHECK (contains (reimported, "\"bars\":3"));
    CHECK (contains (reimported, "Dm7"));
}

TEST ("the metre survives a round trip through the API")
{
    /*  A progression text carries chords and barlines and nothing about how a
        bar is counted, so the chart this rebuilds opens in four unless the
        shell says otherwise. It is the shell that knows - the readers have
        always brought a time signature in - and until it was on this wire a
        waltz imported and exported came back in four. */
    const auto exported = exportIRealPro ("| Dm7 | G7 | Cmaj7 |", "Waltz", "Nobody",
                                          "Jazz Waltz", 3, 4);

    const auto linkStart = exported.find ("irealbook://");
    const auto linkEnd = exported.find ('"', linkStart);

    CHECK (linkStart != std::string::npos);
    CHECK (linkEnd != std::string::npos);

    if (linkStart == std::string::npos || linkEnd == std::string::npos)
        return;

    const auto link = exported.substr (linkStart, linkEnd - linkStart);

    CHECK (contains (link, "T34"));
    CHECK (contains (importIRealPro (link.c_str()), "\"beatsPerBar\":3"));
}

TEST ("a shell that does not know its metre leaves the chart's own alone")
{
    /*  Zero is what an older shell effectively sent and what one with nothing
        to say should send. It must not be read as a metre of nought, and it
        must not be read as an instruction either: the text's own default
        stands, which is what every caller got before this was on the wire. */
    const auto exported = exportIRealPro ("| Dm7 | G7 |", "No Metre", "", "", 0, 0);

    CHECK (contains (exported, "\"ok\":true"));
    CHECK (contains (exported, "T44"));
}

TEST ("every plan is offered, each saying how many bars it moves")
{
    const auto json = reharmPlans ("| Dm7 | G7 | Cmaj7 | Cmaj7 |");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "Minimal touch"));
    CHECK (contains (json, "Recommended"));
    CHECK (contains (json, "Out there"));
    CHECK (contains (json, "\"barsChanged\":"));
}

TEST ("a voicing is analysed against the chord the bar asks for")
{
    // D F A C against Dm7: the chord, in root position.
    const auto json = analyseVoicing ("Dm7", "50,53,57,60", "");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"score\":"));
}

TEST ("asking for a shape returns voicings in it")
{
    const auto json = idiomaticVoicings ("Dm7", 0, "rootless", 0);

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"notes\":"));
}

//==============================================================================
// The solo calls are the one stateful corner of the API: there is one take in
// the process, so each of these arms its own rather than leaning on the last.

TEST ("arming a take clears whatever the last one left")
{
    soloSetBar (0, "Dm7", "", "");
    soloStartTake();
    soloPlayNote (62);
    soloPlayNote (65);

    const auto armed = soloStartTake();

    CHECK (contains (armed, "\"taking\":true"));
    CHECK (contains (armed, "\"total\":0"));
}

TEST ("a played note comes back with its colour, its degree and the running count")
{
    soloStartTake();
    soloSetBar (0, "Dm7", "", "");

    const auto json = soloPlayNote (62);   // D - the root

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"colour\":\"chordTone\""));
    CHECK (contains (json, "\"degree\":\"R\""));
    CHECK (contains (json, "\"name\":\"D4\""));
    CHECK (contains (json, "\"chord\":\"Dm7\""));
    CHECK (contains (json, "\"chordTones\":1"));
}

TEST ("a chart brings its time signature with it")
{
    /*  iReal Pro writes the metre into the body of the link as a T token, not
        into the header fields, and the reader has always pulled it out; until
        now nothing asked for it, so a waltz arrived as a waltz and was counted
        in four. */
    const auto waltz = importIRealPro ("irealb://Blue%20Waltz=Someone==Medium%20Swing===="
                                      "*A{T34Dm7 |G7 |C^7 |C^7 }");

    CHECK (contains (waltz, "\"ok\":true"));
    CHECK (contains (waltz, "\"beatsPerBar\":3"));
    CHECK (contains (waltz, "\"beatUnit\":4"));

    const auto five = importIRealPro ("irealb://Take%20Some=Nobody==Medium%20Swing===="
                                     "*A{T54Dm7 |G7 |C^7 |C^7 }");

    CHECK (contains (five, "\"beatsPerBar\":5"));
    CHECK (contains (five, "\"beatUnit\":4"));
}

TEST ("a chart with nothing to say about its metre says four four")
{
    const auto plain = importIRealPro ("irealb://Plain=Nobody==Medium Swing===*A{Dm7 |G7 |C^7 |C^7 }");

    CHECK (contains (plain, "\"beatsPerBar\":4"));
    CHECK (contains (plain, "\"beatUnit\":4"));
}

TEST ("a note outside the scale comes across as open, with what would close it")
{
    /*  Not "outside": nothing knows that yet, and the wire should not say it
        before the window has had its chance. What it can say is the degree, and
        the note a step away that would land it. */
    soloStartTake();
    soloSetBar (0, "Cmaj7", "", "");

    const auto json = soloPlayNote (61);   // Db over Cmaj7

    CHECK (contains (json, "\"colour\":\"unresolved\""));
    CHECK (contains (json, "\"degree\":\"b9\""));
    CHECK (contains (json, "\"wantsToReach\":\"C4\""));
    CHECK (contains (json, "\"wantsToReachStep\":-1"));
    CHECK (contains (json, "\"unresolved\":1"));
    CHECK (contains (json, "\"settled\":0"));
}

TEST ("a note the line never closed is reported as soon as nothing can reach it")
{
    soloStartTake();
    soloSetBar (0, "Cmaj7", "", "");

    soloPlayNote (61);

    // The very next note lands somewhere else, which is the earliest moment
    // the verdict is true - and so the moment the wire carries it.
    const auto json = soloPlayNote (72);

    CHECK (contains (json, "\"stranded\":[{\"name\":\"Db4\",\"midi\":61,\"kind\":\"\",\"to\":\"\"}]"));
    CHECK (contains (json, "\"outside\":1"));
}

TEST ("the bar a player moves to answers with what they have done on it")
{
    soloStartTake();

    soloSetBar (0, "Dm7", "", "");
    soloPlayNote (62);
    soloPlayNote (65);

    soloSetBar (1, "G7", "", "");
    soloPlayNote (67);

    // Back to the first bar: its own two notes, not the take's three.
    const auto json = soloSetBar (0, "Dm7", "", "");

    CHECK (contains (json, "\"bar\":{\"index\":0"));
    CHECK (contains (json, "\"chordTones\":2"));
    CHECK (contains (json, "\"take\":{\"total\":3"));
}

TEST ("a note that resolves an earlier one says so, and resends the bar it changed")
{
    /*  Db over Dm7 is outside; landing C over the next bar makes it a
        chromatic approach into that bar's root. The bar it changed is the one
        behind, so the reply has to carry that bar too - a shell that only read
        the bar just played into would leave the earlier one drawing numbers
        that stopped being true. */
    soloStartTake();

    soloSetBar (0, "Dm7", "", "");
    soloPlayNote (62);
    soloPlayNote (61);

    soloSetBar (1, "Cmaj7", "", "");
    const auto json = soloPlayNote (60);

    // D, Db, C is a descending chromatic line, so the fuller reading wins:
    // the Db was passed through, not merely leaned on.
    CHECK (contains (json, "\"resolved\":[{\"name\":\"Db4\",\"midi\":61,\"kind\":\"passing\",\"to\":\"C4\"}]"));
    CHECK (contains (json, "\"bar\":{\"index\":1"));
    CHECK (contains (json, "\"index\":0"));          // the earlier bar came back too
    CHECK (contains (json, "\"approachTones\":1"));
}

TEST ("an enclosure comes across the wire as one, not as an approach note")
{
    soloStartTake();
    soloSetBar (0, "Dm7", "", "");

    soloPlayNote (63);
    soloPlayNote (61);

    const auto json = soloPlayNote (62);

    // Both notes of it, and neither called a chromatic approach - which the
    // second of them would honestly answer to on its own.
    CHECK (contains (json, "\"kind\":\"enclosure\""));
    CHECK (! contains (json, "\"kind\":\"chromatic\""));
}

TEST ("a take that never coloured a bar says which bar, on the bar")
{
    soloStartTake();
    soloSetBar (0, "Dm7", "", "");
    soloPlayNote (62);
    soloPlayNote (65);
    soloPlayNote (69);

    const auto json = soloEndTake();

    CHECK (contains (json, "\"neverLeftTheChord\":true"));
    CHECK (contains (json, "\"leaps\":"));
    CHECK (contains (json, "\"range\":"));
}

TEST ("the scale a bar is read against comes across with it")
{
    soloStartTake();

    // Bb over Dm7: outside D Dorian, inside D Aeolian.
    soloSetBar (0, "Dm7", "", "");
    CHECK (contains (soloPlayNote (70), "\"colour\":\"unresolved\""));

    soloSetBar (0, "Dm7", "D Aeolian", "");
    CHECK (contains (soloPlayNote (70), "\"colour\":\"scaleTone\""));
}

TEST ("a bar that is not a chord is an error rather than a silent no-op")
{
    const auto json = soloSetBar (0, "not a chord", "", "");

    CHECK (contains (json, "\"ok\":false"));
    CHECK (contains (json, "\"error\":"));
}

TEST ("ending a take hands back the whole thing, bar by bar")
{
    soloStartTake();

    soloSetBar (0, "Dm7", "", "");
    soloPlayNote (62);
    soloPlayNote (65);

    soloSetBar (1, "G7", "", "");
    soloPlayNote (67);

    const auto json = soloEndTake();

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"taking\":false"));
    CHECK (contains (json, "3 notes over 2 bars"));
    CHECK (contains (json, "\"bars\":[{\"index\":0"));
    CHECK (contains (json, "\"index\":1"));
    CHECK (contains (json, "\"observations\":"));
}

TEST ("a take with nothing in it says so rather than reporting zero per cent")
{
    soloStartTake();

    const auto json = soloEndTake();

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "Nothing played"));
    CHECK (contains (json, "\"observations\":[]"));
}

TEST ("the styles come across with a key, a name and a line to read")
{
    const auto json = scaleStyles();

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"key\":\"modes\""));
    CHECK (contains (json, "\"key\":\"everything\""));
    CHECK (contains (json, "\"name\":\"The modes\""));
    CHECK (contains (json, "\"summary\":"));
}

TEST ("a style narrows the scales offered for a chord")
{
    const auto everything = scalesForChord ("Dm7", "everything");
    const auto pentatonics = scalesForChord ("Dm7", "pentatonic");

    CHECK (contains (everything, "Dorian"));
    CHECK (! contains (pentatonics, "Dorian"));
    CHECK (contains (pentatonics, "Pentatonic"));
    CHECK (contains (pentatonics, "\"styleHasNothing\":false"));
}

TEST ("a style with nothing for a chord shows the rest and says so")
{
    const auto json = scalesForChord ("Cdim7", "bebop");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"styleHasNothing\":true"));
    CHECK (contains (json, "\"scales\":[{"));
}

TEST ("the bar is read against the style it was given")
{
    soloStartTake();

    // E over Dm7 is the 9th: in D Dorian, not in D minor pentatonic.
    soloSetBar (0, "Dm7", "", "modes");
    CHECK (contains (soloPlayNote (64), "\"colour\":\"scaleTone\""));

    soloSetBar (0, "Dm7", "", "pentatonic");
    CHECK (contains (soloPlayNote (64), "\"colour\":\"unresolved\""));
}

TEST ("the wire hands back a comping voicing, and leads it from the last one")
{
    const auto opening = compingVoicing ("Dm7", "");

    CHECK (contains (opening, "\"ok\":true"));
    CHECK (contains (opening, "\"notes\":["));
    CHECK (contains (opening, "\"describe\":"));

    // Dm7 to G7 over a common tone: the wire has to carry the previous voicing
    // in, or every chord is spelled from scratch and the hands jump.
    CHECK (contains (compingVoicing ("G7", "65,72,76,83"), "\"notes\":[65,71,76,81]"));
}

TEST ("a comping voicing is refused for something that is not a chord")
{
    CHECK (contains (compingVoicing ("not a chord", ""), "\"ok\":false"));
}

TEST ("the comping styles go over the wire for a shell to build a menu from")
{
    const auto json = compStyles();

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"key\":\"four\""));
    CHECK (contains (json, "\"key\":\"basie\""));
    CHECK (contains (json, "\"summary\":"));

    // The feel goes too, so a page can say "triplet" without a second copy of
    // the list to go stale.
    CHECK (contains (json, "eighth-note triplets"));
}

TEST ("a comp plan goes over as positions, never as times")
{
    /*  The whole division this rests on: the engine has no clock, so a hit is
        a beat and a tick and the shell turns that into a moment. A plan
        carrying seconds would have put a tempo in the engine. */
    const auto json = compPlan ("| Dm7 | G7 | Cmaj7 | Cmaj7 |", "charleston", 0, 3, 7);

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"ticksPerBeat\":24"));
    CHECK (contains (json, "\"beat\":"));
    CHECK (contains (json, "\"tick\":"));
    CHECK (contains (json, "\"notes\":["));
    CHECK (! contains (json, "\"seconds\""));
    CHECK (! contains (json, "\"when\""));
}

TEST ("the same seed gives the same plan over the wire too")
{
    CHECK_EQ (compPlan ("| Dm7 | G7 |", "basie", 0, 1, 3),
              compPlan ("| Dm7 | G7 |", "basie", 0, 1, 3));

    CHECK (compPlan ("| Dm7 | G7 |", "basie", 0, 1, 3)
             != compPlan ("| Dm7 | G7 |", "four", 0, 1, 3));
}

TEST ("a plan for something that is not a chart is refused")
{
    // Not "|||" - that is a real, if empty, one-bar chart. Text with no bar
    // lines in it at all is the thing the reader turns away.
    CHECK (contains (compPlan ("nothing like a chart", "four", 0, 1, 1), "\"ok\":false"));
}

TEST ("a bar with no chord in it is comped with silence, not with a guess")
{
    const auto json = compPlan ("| |", "four", 0, 0, 1);

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"hits\":[]"));
}

TEST ("the comping styles carry the register and the density they expect")
{
    /*  A page showing what a style asks for must not hold its own copy of the
        numbers, for the same reason it holds no copy of the scale styles. */
    const auto json = compStyles();

    CHECK (contains (json, "\"fewestPerBar\":"));
    CHECK (contains (json, "\"mostPerBar\":"));
    CHECK (contains (json, "\"lowestNote\":"));
    CHECK (contains (json, "\"highestNote\":"));

    // How far the band strays from its own figure, which a shell shows and
    // never decides.
    CHECK (contains (json, "\"variation\":"));

    // Not the slots. Placing a hit is the engine's job, and a copy of the slot
    // table in a shell is where a second theory starts.
    CHECK (! contains (json, "\"slots\""));
}

TEST ("a comped chord is read back over the wire")
{
    // The and of two, in the Charleston, voicing Dm7 rootless - F A C E.
    const auto json = compHit ("| Dm7 | G7 |", "charleston", 0, 1, 12, "53,57,60,64", 0);

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"placement\":\"figure\""));
    CHECK (contains (json, "\"chord\":\"Dm7\""));
    CHECK (contains (json, "\"inRegister\":true"));
    CHECK (contains (json, "\"voicing\":{"));
    CHECK (contains (json, "\"findings\":["));
}

TEST ("a comped chord with no clock behind it says so rather than claiming the downbeat")
{
    /*  The same signal `soloPlayNote` uses, and the same reason: a made-up
        downbeat would be read as a real one. */
    const auto json = compHit ("| Dm7 |", "charleston", 0, -1, 0, "53,57,60,64", 0);

    CHECK (contains (json, "\"placement\":\"unplaced\""));
    CHECK (! contains (json, "\"beat\":"));

    // The notes still read - only the placing is silent.
    CHECK (contains (json, "\"chord\":\"Dm7\""));
    CHECK (contains (json, "\"inRegister\":true"));
}

TEST ("a comping take goes over as positions, never as times")
{
    const auto json = compTake ("| Dm7 | G7 |", "four", 0, 1,
                                "0:0:0:53,57,60,65;0:1:0:53,57,60,65;"
                                "0:2:0:53,57,60,65;0:3:0:53,57,60,65");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"ticksPerBeat\":24"));
    CHECK (contains (json, "\"fit\":"));
    CHECK (contains (json, "\"placement\":"));
    CHECK (contains (json, "\"bars\":["));
    CHECK (contains (json, "\"observations\":["));
    CHECK (! contains (json, "\"seconds\""));
    CHECK (! contains (json, "\"when\""));
}

TEST ("a take with nothing comped in it reports no fit rather than nought per cent")
{
    const auto json = compTake ("| Dm7 | G7 |", "four", 0, 1, "");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"fit\":null"));
    CHECK (contains (json, "\"voicingScore\":null"));
    CHECK (contains (json, "\"hits\":[]"));
}

TEST ("a list of comped chords that is not one is refused")
{
    CHECK (contains (compTake ("| Dm7 |", "four", 0, 0, "nonsense"), "\"ok\":false"));
    CHECK (contains (compTake ("not a chart", "four", 0, 0, ""), "\"ok\":false"));

    // A hit naming a bar and a position but no notes is still a hit somebody
    // struck - it is the text that has to be well formed, not the playing.
    CHECK (contains (compTake ("| Dm7 |", "four", 0, 0, "0:0:0:"), "\"ok\":true"));
}

TEST ("a note played with no clock says so rather than claiming the downbeat")
{
    soloStartTake();
    soloSetBar (0, "Dm7", "", "", 4);

    // The default is "nowhere", because there is no position that means no
    // position and a made-up downbeat would be read as a real one.
    CHECK (contains (soloPlayNote (62), "\"at\":\"\""));
    CHECK (contains (soloPlayNote (64), "\"onStrongBeat\":false"));

    soloEndTake();
}

TEST ("a note played on a clock carries where it fell")
{
    soloStartTake();
    soloSetBar (0, "Dm7", "", "", 4);

    CHECK (contains (soloPlayNote (62, 0, 0), "\"at\":\"1\""));
    CHECK (contains (soloPlayNote (64, 1, 12), "\"at\":\"2 and\""));

    // Beat three is strong in four...
    CHECK (contains (soloPlayNote (65, 2, 0), "\"onStrongBeat\":true"));

    soloEndTake();

    // ...and weak in three, which the bar is told when it is set.
    soloStartTake();
    soloSetBar (0, "Dm7", "", "", 3);
    CHECK (contains (soloPlayNote (65, 2, 0), "\"onStrongBeat\":false"));
    soloEndTake();
}

TEST ("a chord crosses the wire one note at a time, and reads as one gesture")
{
    /*  The shell says which notes were struck together; nothing waits for the
        chord to be finished. G7alt into Cmaj7, two voices stepping down a
        semitone each, which read as two notes that went nowhere before the
        engine knew what a chord was. */
    soloStartTake();
    soloSetBar (0, "G7", "", "", 4);

    soloPlayNote (53, 0, 0, 0);
    CHECK (contains (soloPlayNote (56, 0, 0, 1), "\"withPrevious\":true"));
    soloPlayNote (59, 0, 0, 1);
    soloPlayNote (63, 0, 0, 1);

    soloSetBar (1, "Cmaj7", "", "", 4);

    soloPlayNote (52, 0, 0, 0);
    soloPlayNote (55, 0, 0, 1);
    soloPlayNote (59, 0, 0, 1);
    const auto json = soloPlayNote (62, 0, 0, 1);

    /*  Both voices, each named with the note it reached rather than with
        whatever was struck last. Chromatic rather than passing: with only two
        voicings there is nothing before the G7alt for the line to have been
        stepping through from. */
    CHECK (contains (json, "\"name\":\"Ab3\",\"midi\":56,\"kind\":\"chromatic\",\"to\":\"G3\""));
    CHECK (contains (json, "\"name\":\"Eb4\",\"midi\":63,\"kind\":\"chromatic\",\"to\":\"D4\""));
    CHECK (contains (json, "\"stranded\":[]"));

    const auto take = soloEndTake();

    CHECK (contains (take, "\"outside\":0"));
    CHECK (contains (take, "chords in the line"));
}

TEST ("a note with no attack given starts one of its own")
{
    /*  The default on both sides of the wire, and the reading every take had
        before chords were a thing an engine could be told about. */
    soloStartTake();
    soloSetBar (0, "Dm7", "", "", 4);

    CHECK (contains (soloPlayNote (62), "\"withPrevious\":false"));
    CHECK (contains (soloPlayNote (64, 1, 0), "\"withPrevious\":false"));

    soloEndTake();
}

TEST ("a comp that varies still comes back in style over the wire")
{
    /*  The report, through the wire this time: one and the and of one, which
        used to come back off style in every catalogue entry. */
    const auto json = compTake ("| Dm7 | G7 |", "charleston", 0, 1,
                                "0:0:0:53,57,60,65;0:0:12:53,57,60,65");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"placement\":100"));
    CHECK (contains (json, "\"offStyle\":0"));

    // And the two tiers are both reported, so a shell can say what the figure
    // contributed without the number having said it.
    CHECK (contains (json, "\"onTheFigure\":1"));
    CHECK (contains (json, "\"idiomatic\":1"));
}

//==============================================================================
// The guide-tone line: the 3rd and the 7th walked across a whole chart. A chord
// chart cannot show this, and it is most of what makes a progression work.

TEST ("the guide tones of a tune come back as two continuous strands")
{
    const auto json = guideTones ("| Dm7 | G7 | Cmaj7 |");

    CHECK (contains (json, "\"ok\":true"));

    // The classic ii-V-I: the 7th of Dm7 falls a semitone to the 3rd of G7,
    // and the 7th of G7 falls a semitone to the 3rd of Cmaj7.
    CHECK (contains (json, "\"fromLabel\":\"b7\",\"toLabel\":\"3\",\"semitones\":-1"));

    // And the last bar has tones with nowhere to go, which is not the same as
    // no tones: a shell drawing the line needs somewhere to end it.
    CHECK (contains (json, "\"chord\":\"Cmaj7\""));
    CHECK (contains (json, "\"motions\":[]"));
}

TEST ("a guide-tone line does not climb away over a long chart")
{
    /*  The octave is carried rather than reset, so the strands are continuous -
        but carrying it must not let them drift off the keyboard either. Every
        move is to the *nearest* target, so a whole tune of fourths has to stay
        inside about an octave of where it started. */
    const auto json = guideTones ("| Dm7 | G7 | Cmaj7 | F7 | Bbmaj7 | Eb7 | Abmaj7 | Db7 |");

    CHECK (contains (json, "\"ok\":true"));

    auto lowest = 200;
    auto highest = 0;

    for (std::size_t at = json.find ("\"note\":"); at != std::string::npos;
         at = json.find ("\"note\":", at + 1))
    {
        const auto note = std::stoi (json.substr (at + 7, 4));

        lowest = std::min (lowest, note);
        highest = std::max (highest, note);
    }

    CHECK (highest > 0);
    CHECK (highest - lowest <= 18);
}

TEST ("a chart that will not parse gives no guide tones")
{
    CHECK (contains (guideTones ("| not a chord |"), "\"ok\":false"));
}

//==============================================================================
// A note list is an argument, not a promise.
//
// The four calls that take MIDI notes took them as text and converted each
// field with std::stoi, which throws on a field that is not a number and on one
// too big for an int. Nothing catches it in either shell, so a malformed
// argument ended the process rather than the call. A note is 0 to 127 by the
// standard this wire format is describing; a field saying anything else is a
// field this layer has nothing to do with, and dropping it is the answer.

TEST ("a note list that is not numbers is answered, not thrown at")
{
    for (const char* csv : { "abc,def", "99999999999999999999", "60,,64", "-1", "" })
    {
        CHECK (contains (identifyChord (csv), "\"ok\":"));
        CHECK (contains (analyseVoicing ("Cmaj7", csv, "rootless"), "\"ok\":"));
        CHECK (contains (compingVoicing ("Cmaj7", csv), "\"ok\":"));
        CHECK (contains (compHit ("| Cmaj7 |", "charleston", 0, 0, 0, csv, 0), "\"ok\":"));
    }
}

TEST ("a note outside the MIDI range is not a note")
{
    /*  128 and up used to reach the naming code, which named an octave by
        dividing a number that was never a note - "E178956969" came back from a
        field of twenty digits. Anything outside 0 to 127 is dropped, so what is
        left is the chord the real notes spell. */
    CHECK (contains (identifyChord ("60,64,67,128,9999"), "\"played\":\"C4 E4 G4\""));
}

TEST ("a note list that is notes still reads as the chord it spells")
{
    // The guard above must not cost the ordinary case: these are the same
    // arguments the page sends on every keypress.
    CHECK (contains (identifyChord ("60,64,67"), "\"played\":\"C4 E4 G4\""));
    CHECK (contains (identifyChord ("0,127"), "\"ok\":true"));
    CHECK (contains (analyseVoicing ("Cmaj7", "60,64,67,71", "rootless"), "\"ok\":true"));
}
