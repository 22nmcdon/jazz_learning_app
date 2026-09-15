#include "TestFramework.h"
#include "jazz/core/ChartFormats.h"

#include <algorithm>

using namespace jazz::core;

namespace
{
    Chart chartOf (const std::string& progression, std::string title = "Practice Chart")
    {
        auto result = parseProgressionText (progression, std::move (title));
        CHECK (result.ok());
        return *result.chart;
    }

    std::string irealSymbol (const std::string& symbol)
    {
        const auto chord = ChordSymbol::parse (symbol);
        CHECK (chord.has_value());
        return toIRealProSymbol (*chord);
    }
}

//==============================================================================
TEST ("writes chord symbols the way iReal Pro writes them")
{
    CHECK_EQ (irealSymbol ("Cmaj7"), std::string ("C^7"));
    CHECK_EQ (irealSymbol ("Cmaj9"), std::string ("C^9"));
    CHECK_EQ (irealSymbol ("Dm7"),   std::string ("D-7"));
    CHECK_EQ (irealSymbol ("Dm9"),   std::string ("D-9"));
    CHECK_EQ (irealSymbol ("G7"),    std::string ("G7"));
    CHECK_EQ (irealSymbol ("G13"),   std::string ("G13"));
    CHECK_EQ (irealSymbol ("Bm7b5"), std::string ("Bh7"));
    CHECK_EQ (irealSymbol ("Cdim7"), std::string ("Co7"));
    CHECK_EQ (irealSymbol ("CmMaj7"), std::string ("C-^7"));
    CHECK_EQ (irealSymbol ("C6/9"),  std::string ("C69"));
    CHECK_EQ (irealSymbol ("G7sus4"), std::string ("G7sus"));
    CHECK_EQ (irealSymbol ("G7alt"), std::string ("G7alt"));
    CHECK_EQ (irealSymbol ("G7b9"),  std::string ("G7b9"));
    CHECK_EQ (irealSymbol ("Am7/D"), std::string ("A-7/D"));
}

TEST ("exports a chart as an iReal Pro link")
{
    const auto link = exportIRealPro (chartOf ("| Dm7 | G7 | Cmaj7 |", "Blue Practice"));

    CHECK (link.rfind ("irealbook://", 0) == 0);
    CHECK (link.find ("Blue Practice") != std::string::npos);
    CHECK (link.find ("T44") != std::string::npos);
    CHECK (link.find ("D-7") != std::string::npos);
    CHECK (link.find ("C^7") != std::string::npos);
    CHECK (link.find ("]Z") != std::string::npos);
}

TEST ("a chart survives the round trip through iReal Pro")
{
    const auto original = chartOf ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Am7b5 | D7alt | Gm6 | Gm6 |");
    const auto returned = importIRealPro (exportIRealPro (original));

    CHECK (returned.ok());
    CHECK_EQ (returned.chart->measureCount(), original.measureCount());
    CHECK_EQ (returned.chart->toProgressionText(), original.toProgressionText());
    CHECK_EQ (returned.chart->title, original.title);
}

TEST ("a bar with two chords survives the round trip")
{
    const auto original = chartOf ("| Dm7 G7 | Cmaj7 |");
    const auto returned = importIRealPro (exportIRealPro (original));

    CHECK (returned.ok());
    CHECK_EQ (returned.chart->measures.front().slots.size(), std::size_t (2));
    CHECK_EQ (returned.chart->toProgressionText(), std::string ("| Dm7 G7 | Cmaj7 |"));
}

TEST ("reads an iReal Pro link written by hand")
{
    const auto result = importIRealPro (
        "irealbook://Autumn Practice=Someone=Medium Swing=C=n=*A[T44D-7 |G7 |C^7 |C^7 ]Z");

    CHECK (result.ok());
    CHECK_EQ (result.chart->title, std::string ("Autumn Practice"));
    CHECK_EQ (result.chart->composer, std::string ("Someone"));
    CHECK_EQ (result.chart->style, std::string ("Medium Swing"));
    CHECK_EQ (result.chart->measureCount(), 4);
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Dm7 | G7 | Cmaj7 | Cmaj7 |"));
}

TEST ("a blank bar holds the chord before it")
{
    const auto result = importIRealPro ("irealbook://T=C=S=C=n=[T44C^7 |   |D-7 |x ]Z");

    CHECK (result.ok());
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Cmaj7 | Cmaj7 | Dm7 | Dm7 |"));
}

TEST ("page furniture in the body is ignored")
{
    // Section markers, endings, annotations, fermatas and coda signs.
    const auto result = importIRealPro (
        "irealbook://T=C=S=C=n=*A[T44C^7 <D.C. al Coda>|N1D-7 f|Q G7 |A-7 ]Z");

    CHECK (result.ok());
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Cmaj7 | Dm7 | G7 | Am7 |"));
}

TEST ("url-encoded links are decoded before reading")
{
    const auto result = importIRealPro ("irealbook://My%20Tune=X=Y=C=n=[T44C%5E7 |D-7 ]Z");

    CHECK (result.ok());
    CHECK_EQ (result.chart->title, std::string ("My Tune"));
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Cmaj7 | Dm7 |"));
}

TEST ("a scrambled irealb link says so rather than guessing")
{
    const auto result = importIRealPro ("irealb://1r34LbKcu7CFQyX...=Some Tune=Swing");

    CHECK (! result.ok());
    CHECK (result.error.find ("scrambled") != std::string::npos);
}

TEST ("recognises iReal Pro text without being handed a whole link")
{
    CHECK (looksLikeIRealPro ("irealbook://T=C=S=C=n=[C^7 ]Z"));
    CHECK (looksLikeIRealPro ("irealb://scrambled"));
    CHECK (! looksLikeIRealPro ("| Dm7 | G7 | Cmaj7 |"));
}

//==============================================================================
// Rebuilding a chart from the text on a page.

namespace
{
    /** Chords laid out as a lead sheet does: four bars a line, top down. */
    std::vector<PlacedText> pageOf (std::vector<std::vector<std::string>> lines,
                                    double startY = 100.0)
    {
        std::vector<PlacedText> items;
        auto y = startY;   // y grows downward, as the reader hands it over

        for (const auto& line : lines)
        {
            auto x = 60.0;

            for (const auto& text : line)
            {
                items.push_back ({ x, y, text });
                x += 120.0;
            }

            y += 60.0;
        }

        return items;
    }
}

TEST ("rebuilds a chart from chords laid out on a page")
{
    const auto result = chartFromPlacedText (pageOf ({ { "Dm7", "G7", "Cmaj7", "Cmaj7" },
                                                       { "Cm7", "F7", "Bbmaj7", "Bbmaj7" } }));

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 8);
    CHECK_EQ (result.chart->toProgressionText(),
              std::string ("| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 | Bbmaj7 | Bbmaj7 |"));
}

TEST ("text that is not a chord is left on the page")
{
    auto items = pageOf ({ { "Dm7", "G7", "Cmaj7", "Cmaj7" } });
    items.push_back ({ 250.0, 40.0, "Autumn Leaves" });       // title, above the chords
    items.push_back ({ 60.0, 20.0, "Medium Swing" });         // tempo marking
    items.push_back ({ 300.0, 760.0, "1" });                  // page number

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 4);
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Dm7 | G7 | Cmaj7 | Cmaj7 |"));
}

TEST ("the first line that is not chords becomes the title")
{
    auto items = pageOf ({ { "Dm7", "G7" } });
    items.push_back ({ 200.0, 40.0, "Blue Bossa" });

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->title, std::string ("Blue Bossa"));
}

TEST ("chords crowded together share a bar")
{
    // Four bars where the second holds two chords, drawn closer together.
    std::vector<PlacedText> items {
        { 60.0,  700.0, "Cmaj7" },
        { 180.0, 700.0, "Dm7" },
        { 220.0, 700.0, "G7" },      // only 40 apart: the same bar
        { 300.0, 700.0, "Cmaj7" }
    };

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 3);
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Cmaj7 | Dm7 G7 | Cmaj7 |"));
}

TEST ("lines are read top to bottom however the text arrives")
{
    auto items = pageOf ({ { "Dm7", "G7" }, { "Cmaj7", "A7b9" } });
    std::reverse (items.begin(), items.end());   // as a PDF might hand them over

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Dm7 | G7 | Cmaj7 | A7b9 |"));
}

TEST ("a page with no chords on it says so")
{
    const auto result = chartFromPlacedText ({ { 60.0, 700.0, "Lorem ipsum" },
                                               { 60.0, 640.0, "dolor sit amet" } });

    CHECK (! result.ok());
    CHECK (result.error.find ("No chord symbols") != std::string::npos);
}

TEST ("an empty page says so")
{
    CHECK (! chartFromPlacedText ({}).ok());
}

TEST ("chord symbols split across several runs are stitched back together")
{
    // How a PDF really hands over "Dm7 G7": one run per glyph, a few points
    // apart, with a wide gap between the two chords.
    const std::vector<PlacedText> items {
        { 10.0,  172.0, "D" }, { 24.0, 172.0, "m" }, { 40.0, 172.0, "7" },
        { 195.0, 172.0, "G" }, { 209.0, 172.0, "7" }
    };

    const auto result = chartFromPlacedText (items);

    CHECK (result.ok());
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Dm7 | G7 |"));
}

TEST ("run widths are used to stitch words when the reader supplies them")
{
    // Same line, but the reader knows how wide each run is, so the joining is
    // exact rather than inferred.
    const std::vector<PlacedText> items {
        { 10.0,  172.0, "D", 13.0 }, { 23.0, 172.0, "m", 16.0 }, { 39.0, 172.0, "7", 9.0 },
        { 195.0, 172.0, "G", 13.0 }, { 208.0, 172.0, "7", 9.0 }
    };

    const auto result = chartFromPlacedText (items);

    CHECK (result.ok());
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Dm7 | G7 |"));
}

TEST ("separate chords are not glued together")
{
    // Evenly spaced runs have no word gap to find, so nothing is joined.
    const auto result = chartFromPlacedText (pageOf ({ { "Dm7", "G7", "Cmaj7", "Fmaj7" } }));

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 4);
}

TEST ("a title split across runs is put back together with its spaces")
{
    std::vector<PlacedText> items {
        { 200.0, 40.0, "Blue" }, { 226.0, 40.0, "Bossa" },
        { 60.0, 100.0, "Dm7" },  { 180.0, 100.0, "G7" }
    };

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->title, std::string ("Blue Bossa"));
}

TEST ("a tempo marking above the title is not mistaken for the title")
{
    std::vector<PlacedText> items {
        { 40.0,  20.0, "MEDIUM SWING" },
        { 200.0, 40.0, "Blue Bossa" },
        { 60.0, 100.0, "Cm7" }, { 180.0, 100.0, "Fm7" }
    };

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->title, std::string ("Blue Bossa"));
}

TEST ("a letter-spaced tempo marking is still read as furniture")
{
    // Engravers letter-space these, and a PDF reader hands the spaces back.
    std::vector<PlacedText> items {
        { 40.0,  20.0, "M E D I U M  U P  S W I N G" },
        { 200.0, 40.0, "Solar" },
        { 60.0, 100.0, "Cm7" }, { 180.0, 100.0, "Fm7" }
    };

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->title, std::string ("Solar"));
}

TEST ("a metronome mark is furniture too")
{
    std::vector<PlacedText> items {
        { 40.0,  20.0, "quarter = 132" },
        { 200.0, 40.0, "Stella" },
        { 60.0, 100.0, "Cm7" }, { 180.0, 100.0, "Fm7" }
    };

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->title, std::string ("Stella"));
}

TEST ("a chord the reader cannot understand is reported, not dropped in silence")
{
    const auto result = importIRealPro ("irealbook://Test===C=n=*A[T44C^7 |G7zzz9 |C^7 |C^7 ]Z");

    CHECK (result.ok());
    CHECK_EQ (result.unreadable.size(), std::size_t (1));
    CHECK_EQ (result.unreadable.front(), std::string ("G7zzz9"));
}

TEST ("a chart that reads cleanly reports nothing unreadable")
{
    const auto result = importIRealPro ("irealbook://Test===C=n=*A[T44C^7 |A-7 |D-7 |G7 ]Z");

    CHECK (result.ok());
    CHECK (result.unreadable.empty());
}

TEST ("an unreadable chord on a page is reported")
{
    std::vector<PlacedText> items {
        { 200.0, 40.0, "Test Tune" },
        { 60.0, 100.0, "Dm7" }, { 180.0, 100.0, "G7zzz9" }, { 300.0, 100.0, "Cmaj7" }
    };

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.unreadable.size(), std::size_t (1));
    CHECK_EQ (result.unreadable.front(), std::string ("G7zzz9"));
}

TEST ("page text beside the music is not reported as an unreadable chord")
{
    // "Bridge" starts on a note name; it carries no digit or accidental, so it
    // is a rehearsal mark rather than a chord the engine failed to read.
    std::vector<PlacedText> items {
        { 200.0, 40.0, "Test Tune" },
        { 20.0, 100.0, "Bridge" }, { 160.0, 100.0, "Dm7" }, { 300.0, 100.0, "G7" }
    };

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK (result.unreadable.empty());
}
