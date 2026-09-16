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
    const auto json = scalesForChord ("Dm7");

    CHECK (contains (json, "\"ok\":true"));
    CHECK (contains (json, "Dorian"));
}

TEST ("an unreadable chord symbol is an error object, not a crash")
{
    const auto json = scalesForChord ("not a chord");

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
