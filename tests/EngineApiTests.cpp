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
    const auto exported = exportIRealPro ("| Dm7 | G7 | Cmaj7 |", "Test Tune", "Nobody", "Medium Swing");

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

    CHECK (contains (json, "\"stranded\":[{\"name\":\"Db4\",\"midi\":61,\"kind\":\"\"}]"));
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
    CHECK (contains (json, "\"resolved\":[{\"name\":\"Db4\",\"midi\":61,\"kind\":\"passing\"}]"));
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
