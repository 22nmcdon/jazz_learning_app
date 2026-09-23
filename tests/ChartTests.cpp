#include "TestFramework.h"
#include "jazz/core/Chart.h"

using namespace jazz::core;

TEST ("parses a pipe-delimited progression")
{
    const auto result = parseProgressionText ("| Dm7 | G7 | Cmaj7 | Cmaj7 |", "ii-V-I");

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 4);
    CHECK_EQ (result.chart->title, std::string ("ii-V-I"));
    CHECK_EQ (result.chart->chordAt (0)->toString(), std::string ("Dm7"));
    CHECK_EQ (result.chart->chordAt (2)->toString(), std::string ("Cmaj7"));
}

TEST ("splits a bar between two chords")
{
    const auto result = parseProgressionText ("| Dm7 G7 | Cmaj7 |");

    CHECK (result.ok());

    const auto& measure = result.chart->measures.front();
    CHECK_EQ (measure.slots.size(), std::size_t (2));
    CHECK_EQ (measure.slots[0].beats, 2);
    CHECK_EQ (measure.slots[1].beats, 2);

    // Beat 0-1 is the ii chord, beat 2-3 the V.
    CHECK_EQ (result.chart->chordAt (0, 0)->toString(), std::string ("Dm7"));
    CHECK_EQ (result.chart->chordAt (0, 3)->toString(), std::string ("G7"));
}

TEST ("'%' repeats the previous measure")
{
    const auto result = parseProgressionText ("| Fmaj7 | % |");

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 2);
    CHECK_EQ (result.chart->chordAt (1)->toString(), std::string ("Fmaj7"));
}

TEST ("reports where the text stopped making sense")
{
    const auto result = parseProgressionText ("| Dm7 | Gsevenths | Cmaj7 |");

    CHECK (! result.ok());
    CHECK (result.error.find ("Measure 2") != std::string::npos);
    CHECK (result.error.find ("Gsevenths") != std::string::npos);
}

TEST ("multi-line charts paste cleanly")
{
    const auto result = parseProgressionText ("| Dm7 | G7 |\n| Cmaj7 | A7b9 |");

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 4);
}

TEST ("rejects text with no bar lines")
{
    CHECK (! parseProgressionText ("Dm7 G7 Cmaj7").ok());
}

TEST ("round-trips through progression text")
{
    const auto result = parseProgressionText ("| Dm7 | G7 | Cmaj7 |");
    CHECK (result.ok());
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Dm7 | G7 | Cmaj7 |"));
}

TEST ("transposes every chord in the chart")
{
    const auto result = parseProgressionText ("| Dm7 | G7 | Cmaj7 |");
    const auto transposed = result.chart->transposed (2);

    CHECK_EQ (transposed.toProgressionText(), std::string ("| Em7 | A7 | Dmaj7 |"));
}
