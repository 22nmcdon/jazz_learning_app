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

    /** Just the hits of a comp plan - what the band actually plays.

        Compared instead of the whole reply because the reply also echoes the
        style's *name*, and a described style deliberately has none to echo: it
        carries no key, name or summary, which is what keeps its grammar free
        of quoting. Two plans being the same comp is a question about the
        hits, and this is the part that answers it.
    */
    std::string hitsOf (const std::string& json)
    {
        const auto at = json.find ("\"hits\":");

        if (at == std::string::npos)
            return "(no hits)";

        const auto close = json.find ("}]", at);

        return json.substr (at, close == std::string::npos ? std::string::npos
                                                          : close + 2 - at);
    }

    /** One style's `reference` out of `compStyles()`'s answer.

        Read back out of the engine's own reply rather than written here, so
        the round-trip test holds no second copy of the catalogue - the thing
        a fixture would quietly become.
    */
    std::string referenceFor (const std::string& styles, const std::string& key)
    {
        const auto at = styles.find ("\"key\":\"" + key + "\"");

        if (at == std::string::npos)
            return {};

        const std::string marker = "\"reference\":\"";
        const auto from = styles.find (marker, at);

        if (from == std::string::npos)
            return {};

        const auto begin = from + marker.size();

        return styles.substr (begin, styles.find ('"', begin) - begin);
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

TEST ("a chord in the line crosses the wire read as a chord")
{
    soloStartTake();
    soloSetBar (0, "Dm7", "", "");

    soloPlayNote (57);                        // A3, on its own: a line, not a chord
    const auto alone = soloPlayNote (53, -1, 0, 0);

    CHECK (contains (alone, "\"voicing\":null"));

    // Struck with the one before it, and there is a chord to read: F and C
    // under Dm7 are the b3 and the b7, which is the whole of what it needs.
    const auto pair = soloPlayNote (60, -1, 0, 1);

    CHECK (contains (pair, "\"melodyName\":\"C4\""));
    CHECK (contains (pair, "\"saysTheChord\":true"));
    CHECK (contains (pair, "\"chord\":\"Dm7\""));
}

TEST ("a passing chord crosses the wire named, not marked")
{
    soloStartTake();
    soloSetBar (0, "Dm7", "", "");

    soloPlayNote (51);                        // Eb Gb A C - Ebdim7 through the bar
    soloPlayNote (54, -1, 0, 1);
    soloPlayNote (57, -1, 0, 1);

    const auto json = soloPlayNote (60, -1, 0, 1);

    CHECK (contains (json, "\"saysTheChord\":false"));
    CHECK (contains (json, "\"spelled\":\"Ebdim7\""));
    CHECK (contains (json, "\"reading\":\"C4 on top"));

    const auto take = soloEndTake();

    CHECK (contains (take, "\"chordsPlayed\":1"));
    CHECK (contains (take, "\"chordsSpellingTheBar\":0"));
    CHECK (contains (take, "\"chords\":[{"));
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
}

TEST ("a style carries its figure, so a shell can draw one and copy it")
{
    /*  This used to assert the opposite - that `slots` never crossed - on the
        grounds that "placing a hit is the engine's job, and a copy of the slot
        table in a shell is where a second theory starts."

        That rule narrows rather than falls, and the narrowed version is the
        one that was always meant. What starts a second theory is a shell
        *deciding* whether a hit is in style; `compHit` and `compTake` still
        answer that and nothing else does. What a shell may now do is **draw**
        a figure and hand an edited one back - which is not a second opinion
        about placement, it is the player having one.

        Held to it on the other side: nothing on the page reads these slots to
        judge a hit, and `rememberCompMarks` still works no number out that the
        engine did not work out first.
    */
    const auto json = compStyles();

    CHECK (contains (json, "\"slots\":[{"));
    CHECK (contains (json, "\"heldFor\":"));

    // The grid the ticks are counted on, so a stored style can tell that the
    // grid itself moved rather than trusting a hand-raised version number.
    CHECK (contains (json, "\"ticksPerBeat\":24"));

    /*  And the feels a style may be counted in. A shell offering that choice
        would otherwise hold its own list of four - the same drift the scale
        styles are kept out of the page for. Sixteenths matter most here: no
        style that ships uses them, so a list derived from the catalogue's own
        answers would quietly offer three. */
    CHECK (contains (json, "\"feels\":["));
    CHECK (contains (json, "\"sixteenths\""));
    CHECK (contains (json, "\"eighth-note triplets\""));
}

TEST ("a slot says what it means, not the nearest number to it")
{
    /*  The half with teeth. `"slots":[]` would pass the check above, and so
        would a writer that flattened every optional to a number - which is
        the one mistake that matters here, because both of this struct's
        optionals have an empty case that means something a number cannot say.

        Four to the bar is **one** slot with no beat at all, not four slots.
        And Basie's push is beat -1, counting back from the end of the bar,
        which is what keeps "the and of the last beat" the same idea in three
        as in four. A writer that resolved either one would be writing down a
        different style from the one it was given.
    */
    const auto json = compStyles();

    CHECK (contains (json, "\"beat\":\"\""));     // four to the bar: every beat
    CHECK (contains (json, "\"beat\":\"-1\""));   // counted back from the end
    CHECK (contains (json, "\"anticipates\":true"));

    // And the same two, in the flat form a shell hands back.
    CHECK (contains (json, "\"reference\":\"custom:beats|"));
    CHECK (contains (json, ";-1:"));
}

TEST ("a style described plays exactly what the same style named plays")
{
    /*  The load-bearing one. `reference` is written by the engine and read
        back by the engine, so if the two ever disagree - a field added to one
        side and not the other, an optional flattened on the way out - this is
        where it shows, for every style that ships rather than for one
        hand-written fixture.

        Byte for byte, because `compPlan` is seeded and reproducible: two runs
        of the same style over the same bars at the same seed are the same
        comp, and anything less than identical means a field did not survive.
    */
    const auto styles = compStyles();

    for (const auto& key : { "four", "basie", "charleston", "ballad" })
    {
        // The engine's own description of that style, out of its own answer.
        const auto reference = referenceFor (styles, key);

        CHECK (reference.rfind ("custom:", 0) == 0);

        const auto named = compPlan ("| Dm7 | G7 | Cmaj7 | A7 |", key, 0, 3, 9);
        const auto described = compPlan ("| Dm7 | G7 | Cmaj7 | A7 |", reference.c_str(), 0, 3, 9);

        // The style's own name is the one thing that cannot survive, because
        // a description deliberately carries no name to survive with.
        CHECK_EQ (hitsOf (described), hitsOf (named));
    }
}

TEST ("and a described style that differs plays differently")
{
    /*  The negative control, and without it the test above is satisfied by a
        reader that ignored the description and fell back to the catalogue -
        which is exactly the bug it is there to catch.
    */
    const auto onOne   = compPlan ("| Dm7 | G7 |", "custom:eighths|0|3|48|79|0|12|0:0:100:0:0", 0, 1, 9);
    const auto onThree = compPlan ("| Dm7 | G7 |", "custom:eighths|0|3|48|79|0|12|2:0:100:0:0", 0, 1, 9);

    CHECK (contains (onOne, "\"ok\":true"));
    CHECK (contains (onThree, "\"ok\":true"));

    // The same figure moved two beats over. If the description were being
    // ignored these would be the same comp - which is the bug this catches.
    CHECK (hitsOf (onOne) != hitsOf (onThree));
    CHECK (contains (onOne, "\"beat\":0"));
    CHECK (contains (onThree, "\"beat\":2"));
}

TEST ("a description that cannot be read is an error, not a shrug")
{
    /*  A broken message is not a renamed style. Falling back would comp four
        to the bar underneath someone who had just written their own figure -
        working-looking, wrong, and impossible to notice.
    */
    for (const auto& broken : { "custom:",                                     // nothing at all
                                "custom:quavers|0|3|48|79|20|24|0:0:90:0:0",   // a feel nobody writes
                                "custom:eighths|0|3|48|79|20|24|",             // a figure with no slots
                                "custom:eighths|0|3|48|79|20|24|0:900:90:0:0", // a tick off the grid
                                "custom:eighths|0|3|48|79|20|24|0:0:-5:0:0",   // a weight below nothing
                                "custom:eighths|0|3|79|48|20|24|0:0:90:0:0",   // register upside down
                                "custom:eighths|0|3|48|200|20|24|0:0:90:0:0",  // a note off the keyboard
                                "custom:eighths|0|3|48|79|20|24" })            // a field short
    {
        const auto json = compPlan ("| Dm7 |", broken, 0, 0, 1);

        CHECK (contains (json, "\"ok\":false"));

        // Named, so this cannot pass by falling back and looking successful.
        CHECK (! contains (json, "\"hits\""));
    }
}

TEST ("but an unknown style name still comps, which is the opposite rule")
{
    /*  These two are each other's control. A key is a name that may have been
        renamed since a shell last looked, and the promise there is comping in
        some style rather than silence; a description is a message, and the
        promise there is that a broken one is refused. A change that made both
        strict, or both forgiving, would break one of the two.
    */
    const auto json = compPlan ("| Dm7 |", "no such style", 0, 0, 1);

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"hits\""));
}

TEST ("a described style grades a player too, not only the band")
{
    /*  Both directions, or the editor would let you write a figure the band
        plays and the reading still marks against something else.

        The and of two is the Charleston's second slot and is nowhere in a
        style whose only slot is the downbeat - so the same hit reads one way
        under each, which is the whole of what it means for a description to
        reach the evaluator.
    */
    const auto onlyOne = "custom:eighths|0|3|48|79|0|12|0:0:100:0:0";

    const auto inStyle = compHit ("| Dm7 | G7 |", "charleston", 0, 1, 12, "53,57,60,64", 0);
    const auto outside = compHit ("| Dm7 | G7 |", onlyOne, 0, 1, 12, "53,57,60,64", 0);

    CHECK (contains (inStyle, "\"placement\":\"figure\""));
    CHECK (contains (outside, "\"ok\":true"));
    CHECK (! contains (outside, "\"placement\":\"figure\""));
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
// The guide tones of the chord that is coming, voiced for the hand that is
// playing now. A chord chart cannot show this, and it is most of what makes a
// progression work.

TEST ("the next chord's guide tones cross the wire voiced for the hand")
{
    // A rootless Dm7 in the left hand, asking where G7 is.
    const auto json = voicedGuideTones ("G7", "53,57,60,64");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"chord\":\"G7\""));

    // The 7th of Dm7 falls a semitone to the 3rd of G7, and the F stays put -
    // and both are given as notes to play, not as degrees to work out.
    CHECK (contains (json, "{\"note\":59,\"label\":\"3\",\"from\":60,\"semitones\":-1}"));
    CHECK (contains (json, "{\"note\":53,\"label\":\"b7\",\"from\":53,\"semitones\":0}"));
}

TEST ("a hand playing nothing gets no guide tones over the wire")
{
    // Not an error: there is no chord to be wrong about, only no hand to
    // measure from. A shell shows nothing rather than a banner.
    const auto json = voicedGuideTones ("G7", "");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"tones\":[]"));
}

TEST ("a chord that will not parse gives no guide tones")
{
    CHECK (contains (voicedGuideTones ("not a chord", "60"), "\"ok\":false"));
}

//  --- the practice record on the wire -------------------------------------

namespace
{
    /** One take's worth of the history grammar, with the fields nothing in a
        given test cares about left at zero.

        Written out longhand rather than built by a helper with eighteen
        arguments: the grammar *is* what these tests are about, and a builder
        that got a field's position wrong would agree with a reader that got it
        wrong the same way.
    */
    const std::string oneSoloTake =
        // day mode tune secs qual roots  ct st ap un out  leaps lr chords  fig idio push off
        "10:0:7:300:7:1157:40:20:4:0:6:3:2:0:0:0:0:0"
        "|0,10,5,1,0,2;1,12,6,2,0,1;2,18,9,1,0,3";
}

TEST ("an empty practice record reads as a record with nothing in it")
{
    const std::string json = practiceReading ("", 100);

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"takes\":0"));
    CHECK (contains (json, "Nothing practised yet"));
    CHECK (contains (json, "\"observations\":[]"));
}

TEST ("a history the shell hands over is read back as words")
{
    const std::string json = practiceReading (oneSoloTake.c_str(), 12);

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"takes\":1"));
    CHECK (contains (json, "\"bars\":3"));
    CHECK (contains (json, "\"minutes\":5"));
    CHECK (contains (json, "\"daysSinceLast\":2"));
    CHECK (contains (json, "\"soloTakes\":1"));
    CHECK (contains (json, "\"compTakes\":0"));

    // The three qualities in mask 7, and the three roots in mask 1157
    // (C, D, G, plus Bb) - named by the engine, never by the page.
    CHECK (contains (json, "\"qualitiesMet\":[\"major\",\"minor\",\"dominant\"]"));
    CHECK (contains (json, "\"half-diminished\""));
    CHECK (contains (json, "\"rootsMet\":[\"C\",\"D\",\"G\",\"Bb\"]"));
}

TEST ("no answer from the practice wire carries a score")
{
    const std::string json = practiceReading (oneSoloTake.c_str(), 12);

    // `lineStatsJson` carries one and this deliberately does not use it. A
    // score is a reading of a bar just played; the same counts summed over
    // weeks and drawn as a line is the grade it refuses to be, so the page is
    // given nothing to plot even if somebody later wants to.
    CHECK (! contains (json, "\"score\""));
    CHECK (contains (json, "\"percentOutside\""));
    CHECK (contains (json, "\"settled\""));
}

TEST ("a malformed history is an error, never an empty reading")
{
    // The asymmetry a described comping style already draws: an unknown style
    // key falls back, a broken description does not. A shell's bug reading back
    // as "you have not practised" is the one wrong answer here.
    for (const char* broken : { "not a take at all",
                                "10:0:7:300:7:1157:40:20:4:0:6:3:2:0:0:0:0|",     // 17 fields
                                "10:0:7:300:7:1157:40:20:4:0:6:3:2:0:0:0:0:0:9|",  // 19 fields
                                "10:0:7:300:7:1157:40:20:4:0:6:3:2:0:0:0:0:0",    // no bar half
                                "10:0:7:300:7:1157:40:20:4:0:6:3:2:0:0:0:0:0|0,10,5,1,0",
                                "10:2:7:300:7:1157:40:20:4:0:6:3:2:0:0:0:0:0|",   // mode 2
                                "-1:0:7:300:7:1157:40:20:4:0:6:3:2:0:0:0:0:0|" })
    {
        const std::string json = practiceReading (broken, 100);

        CHECK (contains (json, "\"ok\":false"));
        CHECK (contains (json, "could not be read"));
    }
}

TEST ("a take whose bars a shell did not keep is still a take")
{
    const std::string json =
        practiceReading ("10:0:7:300:7:1157:40:20:4:0:6:3:2:0:0:0:0:0|", 10);

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"takes\":1"));
    CHECK (contains (json, "\"bars\":0"));

    // The take-wide counts are not derived from the bars, so they survive.
    CHECK (contains (json, "\"total\":70"));
}

TEST ("several takes cross on one string, separated by a tilde")
{
    const auto history = oneSoloTake + "~"
                       + "11:1:7:600:4:128:0:0:0:0:0:0:0:0:6:4:1:0|";

    const std::string json = practiceReading (history.c_str(), 11);

    CHECK (contains (json, "\"takes\":2"));
    CHECK (contains (json, "\"soloTakes\":1"));
    CHECK (contains (json, "\"compTakes\":1"));
    CHECK (contains (json, "\"daysPractised\":2"));
    CHECK (contains (json, "\"minutes\":15"));
}

TEST ("one tune is read across its own takes, and the chart says which bars exist")
{
    const auto history = std::string (
        "1:0:7:300:7:1157:12:6:0:0:2:0:0:0:0:0:0:0|0,4,2,0,0,1;1,4,2,0,0,0;2,4,2,0,0,1~"
        "2:0:7:300:7:1157:12:6:0:0:2:0:0:0:0:0:0:0|0,4,2,0,0,1;1,4,2,0,0,0;2,4,2,0,0,1~"
        "3:0:7:300:7:1157:12:6:0:0:2:0:0:0:0:0:0:0|0,4,2,0,0,1;1,4,2,0,0,0;2,4,2,0,0,1");

    const std::string json =
        tuneProgress ("| Dm7 | G7 | Cmaj7 | Am7b5 |", history.c_str(), 5);

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "\"takes\":3"));
    CHECK (contains (json, "\"barsInChart\":4"));
    CHECK (contains (json, "\"daysSinceLast\":2"));

    // Every bar of the chart is drawn, reached or not - the unreached one is
    // the finding, so it cannot be left out of the list.
    CHECK (contains (json, "\"index\":3,\"chord\":\"Am7b5\",\"takes\":0"));
    CHECK (contains (json, "\"index\":0,\"chord\":\"Dm7\",\"takes\":3"));
    CHECK (contains (json, "Bar 4 has never been reached"));
    CHECK (! contains (json, "\"score\""));
}

TEST ("a tune progress call refuses a chart it cannot parse, before the history")
{
    const std::string json = tuneProgress ("| not a chord |", "", 5);

    CHECK (contains (json, "\"ok\":false"));
    CHECK (! contains (json, "could not be read"));   // the chart's error, not the history's
}

TEST ("a solo take says which harmony it was played over")
{
    soloStartTake();
    soloSetBar (0, "Dm7", "", "everything");
    soloPlayNote (62);
    soloSetBar (1, "G7", "", "everything");
    soloPlayNote (67);
    soloSetBar (2, "Am7b5", "", "everything");
    soloPlayNote (69);

    const std::string json = soloEndTake();

    // Masks, not names: a row stores these and the page never parses a symbol
    // to get them. minor | dominant | halfDiminished = 2 + 4 + 8.
    CHECK (contains (json, "\"qualities\":14"));

    // D, G, A = bits 2, 7 and 9 = 4 + 128 + 512.
    CHECK (contains (json, "\"roots\":644"));
}

TEST ("a comping take says the same, off the chart it was graded against")
{
    const std::string json = compTake ("| Dm7 | G7 | Cmaj7 | Cmaj7 |", "freddie", 0, 1,
                                       "0:1:0:62,65,69;1:1:0:65,69,72");

    CHECK (contains (json, "\"ok\":true"));

    // Bars 0 to 1 only - the range the take actually covered. Cmaj7 is on the
    // chart and was not played over, so major must not be in the mask.
    CHECK (contains (json, "\"qualities\":6"));       // minor | dominant
    CHECK (contains (json, "\"roots\":132"));         // D and G
}
