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

TEST ("a metre survives the round trip")
{
    auto original = chartOf ("| Dm7 | G7 | Cmaj7 |");
    original.timeSignature.numerator = 3;
    original.timeSignature.denominator = 4;

    const auto returned = importIRealPro (exportIRealPro (original));

    CHECK (returned.ok());
    CHECK_EQ (returned.chart->timeSignature.numerator, 3);
    CHECK_EQ (returned.chart->timeSignature.denominator, 4);
}

TEST ("a two-digit metre survives the round trip, and eats none of the music")
{
    /*  T128 is the one metre iReal Pro writes in three characters, and reading
        it two at a time gave 1/2 and left the 8 to be read as music. Both
        halves are checked here: the metre that came back, and the chart it did
        not take a bite out of. */
    auto original = chartOf ("| Dm7 | G7 | Cmaj7 |");
    original.timeSignature.numerator = 12;
    original.timeSignature.denominator = 8;

    const auto returned = importIRealPro (exportIRealPro (original));

    CHECK (returned.ok());
    CHECK_EQ (returned.chart->timeSignature.numerator, 12);
    CHECK_EQ (returned.chart->timeSignature.denominator, 8);
    CHECK_EQ (returned.chart->toProgressionText(), original.toProgressionText());
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

// The link iReal Pro 2026 writes when a song is shared, url-encoded exactly as it
// arrives in the exported .html file.
static const char* sharedLink =
    "irealb://Tbd3=McDonald%20Noah==Medium%20Swing=Db==1r34LbKcu7%7CQyX7b%5E7Xy43T%7CQ"
    "yX9b7F%7CQy7X%2DC%7CQyX9%239b7G%7CQN1Db%5ED44T%5ByX%7CQy9XyQ%7CQyX7%5EbD1N43T%7DQ"
    "Xy%2CB%2F9%5E%23F%7CQyX7%2DC%7CB%5E7X%239b7G%5EAZLQ%20%7BT64QyX%7D%207bAQyX7%5EAN"
    "1ZLB%2F9%5E%23FQyX7%5EAXyQXy%20QyXQ7%20%20Ab7%20LZT44Db%5E7XyQ%5D%20==0=0";

TEST ("a shared irealb link is unscrambled and read")
{
    const auto result = importIRealPro (sharedLink);

    CHECK (result.ok());
    CHECK_EQ (result.chart->title, std::string ("Tbd3"));
    CHECK_EQ (result.chart->style, std::string ("Medium Swing"));
    CHECK_EQ (result.chart->measureCount(), 14);
    CHECK (result.unreadable.empty());
    CHECK_EQ (result.chart->toProgressionText(),
              std::string ("| Dbmaj7 | G7b9#9 | Cm7 | F7b9 | Dbmaj7 | G7b9#9 | Cm7 | F#maj9/B "
                           "| Dbmaj7 | Bmaj7 | Amaj7 F#maj9/B | Amaj7 Ab7 | Amaj7 Ab7 | Dbmaj7 |"));
}

TEST ("iReal Pro files a composer surname first and shows it the other way round")
{
    const auto result = importIRealPro (sharedLink);

    CHECK (result.ok());
    CHECK_EQ (result.chart->composer, std::string ("Noah McDonald"));
}

TEST ("the metre a tune opens in is the one the chart keeps")
{
    // Tbd3 goes 4/4, 3/4, 6/4, 4/4; a Chart holds one time signature.
    const auto result = importIRealPro (sharedLink);

    CHECK (result.ok());
    CHECK_EQ (result.chart->timeSignature.numerator, 4);
    CHECK_EQ (result.chart->timeSignature.denominator, 4);
}

TEST ("padding cells at the end of a row are not bars")
{
    // iReal Pro fills a row out to four cells; "XyQ" is its spacing, and the
    // empty cells after the last bar of a section are not bars of music.
    const auto result = importIRealPro ("irealbook://T=C=S=C=n=[T44C^7XyQ|D-7XyQ|XyQXyQ  {G7XyQ ]Z");

    CHECK (result.ok());
    CHECK_EQ (result.chart->toProgressionText(), std::string ("| Cmaj7 | Dm7 | G7 |"));
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
    const auto result = importIRealPro ("irealbook://Test===C=n=*A[T44C^7 |G13b |C^7 |C^7 ]Z");

    CHECK (result.ok());
    CHECK_EQ (result.unreadable.size(), std::size_t (1));
    CHECK_EQ (result.unreadable.front(), std::string ("G13b"));
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

//==============================================================================
// iReal Pro's own PDF export. It draws its chord symbols rather than writing
// them, so the only chords on the page are the spoken descriptions it attaches
// for a screen reader. These items are taken from a real export.
static std::vector<PlacedText> iRealProPage()
{
    return {
        { 292.5,  31.2, "Tbd3" },
        { 6.0,    52.2, "(Medium Swing)" },
        { 476.3,  52.2, "Noah McDonald" },
        { 0.0,     0.0, "Made with iReal Pro" },
        { 241.3, 728.9, "Title: Tbd3" },
        { 5.0,   711.7, "Style: (Medium Swing)" },
        { 370.8, 711.7, "Composer: Noah McDonald" },
        { 19.8,  633.6, "Opening double bar line" },
        { 2.5,   638.0, "Time Signature: 4, 4" },
        { 18.1,  633.6, "Bar 1, d Flat Major  7" },
        { 143.6, 633.6, "Bar Line" },
        { 143.6, 633.6, "Bar 2, g 7Flat  9Sharp 9" },
        { 268.9, 633.6, "Bar 3, c Minor 7" },
        { 394.4, 633.6, "Bar 4, f 7Flat  9" },
        { 2.5,   478.6, "Time Signature: 3, 4" },
        { 19.8,  532.0, "First Ending" },
        { 18.1,  474.2, "Bar 5, b Major  7" },
        { 18.1,  394.5, "Bar 6, 'A' Major  7" },
        { 143.6, 394.5, "f Sharp Major  9 Over b" },
        { 206.2, 394.5, "Bar 7, 'A' Major  7" },
        { 331.6, 394.5, "'A' Flat 7" },
        { 370.8, 388.7, "Closing repeat bar line" }
    };
}

TEST ("an iReal Pro page is read from the descriptions it attaches to its chords")
{
    const auto result = chartFromPlacedText (iRealProPage());

    CHECK (result.ok());
    CHECK_EQ (result.chart->title, std::string ("Tbd3"));
    CHECK_EQ (result.chart->composer, std::string ("Noah McDonald"));
    CHECK_EQ (result.chart->style, std::string ("Medium Swing"));
    CHECK (result.unreadable.empty());
    CHECK_EQ (result.chart->toProgressionText(),
              std::string ("| Dbmaj7 | G7b9#9 | Cm7 | F7b9 | Bmaj7 | Amaj7 F#maj9/B | Amaj7 Ab7 |"));
}

TEST ("the metre the page opens in is the one the chart keeps")
{
    const auto result = chartFromPlacedText (iRealProPage());

    CHECK (result.ok());
    CHECK_EQ (result.chart->timeSignature.numerator, 4);
    CHECK_EQ (result.chart->timeSignature.denominator, 4);
}

TEST ("a page read upside down gives the same chart")
{
    // The reader orders bars by the numbers iReal Pro gives them, so which way
    // the page coordinates run does not matter.
    auto items = iRealProPage();

    for (auto& item : items)
        item.y = 1000.0 - item.y;

    std::reverse (items.begin(), items.end());

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->toProgressionText(),
              std::string ("| Dbmaj7 | G7b9#9 | Cm7 | F7b9 | Bmaj7 | Amaj7 F#maj9/B | Amaj7 Ab7 |"));
}

TEST ("a second chord goes in the bar to its left, not the nearest one")
{
    // Two bars share a line: the loose chord at x=331 belongs to bar 7 at x=206,
    // not to bar 6 at x=18.
    const auto result = chartFromPlacedText (iRealProPage());

    CHECK (result.ok());
    CHECK_EQ (result.chart->measures[6].slots.size(), std::size_t (2));
    CHECK_EQ (result.chart->measures[6].slots[1].chord.toString(), std::string ("Ab7"));
}

TEST ("marks that start on a note name are not read as chords")
{
    // "Bar Line", "First Ending" and the composer's name all begin on B, F or N.
    const auto result = chartFromPlacedText (iRealProPage());

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 7);
}

TEST ("a chord description outside the vocabulary is reported")
{
    std::vector<PlacedText> items {
        { 18.1, 633.6, "Bar 1, d Flat Major  7" },
        { 143.6, 633.6, "Bar 2, g Peculiar 7" }
    };

    const auto result = chartFromPlacedText (std::move (items));

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 1);
    CHECK_EQ (result.unreadable.size(), std::size_t (1));
    CHECK_EQ (result.unreadable.front(), std::string ("g Peculiar 7"));
}

TEST ("spoken chord descriptions carry the same chords as the symbols")
{
    struct Case { const char* spoken; const char* symbol; };

    const Case cases[] = {
        { "Bar 1, d Flat Major  7",       "Dbmaj7" },
        { "Bar 1, g 7Flat  9Sharp 9",     "G7b9#9" },
        { "Bar 1, c Minor 7",             "Cm7" },
        { "Bar 1, f Sharp Major  9 Over b", "F#maj9/B" },
        { "Bar 1, 'A' Flat 7",            "Ab7" },
        { "Bar 1, b Half Diminished 7",   "Bm7b5" },
        { "Bar 1, e Diminished 7",        "Edim7" },
        { "Bar 1, g 7 Suspended 4",       "G7sus4" }
    };

    for (const auto& one : cases)
    {
        const auto result = chartFromPlacedText ({ { 18.0, 100.0, one.spoken } });

        CHECK (result.ok());

        if (result.ok())
            CHECK_EQ (result.chart->measures[0].slots[0].chord.toString(), std::string (one.symbol));
    }
}

TEST ("a chart written with sharps goes back out written with sharps")
{
    const auto result = importIRealPro (sharedLink);

    CHECK (result.ok());

    const auto link = exportIRealPro (*result.chart);

    CHECK (link.find ("F#^9/B") != std::string::npos);
    CHECK (link.find ("Gb") == std::string::npos);
}

//==============================================================================
// Numbers out of a file someone else wrote.
//
// Both readers take their metre off text they did not produce: an iReal Pro
// link, which travels in a URL and so arrives from anywhere a link does, and a
// PDF, which is a file from a stranger by definition. Both converted it with
// `std::stoi`, which throws on a word and on a run of digits too long to fit an
// int - and no caller anywhere above catches that. On the web it aborted the
// WebAssembly engine, leaving a page whose every later call failed; in the app
// it took the process down. A chart that cannot be read is an answer, not an
// exit, and these say so in both directions.

TEST ("a metre of too many digits does not take the reader down with it")
{
    const auto result = importIRealPro (
        "irealbook://T=C=S=C=n=*A[T99999999999999999999999999999999C^7 |D-7 ]Z");

    // The digits are still skipped rather than read as music: a metre that
    // cannot be true leaves the chart in four, not in whatever the digits spell.
    CHECK (result.ok());
    CHECK_EQ (result.chart->timeSignature.numerator, 4);
    CHECK_EQ (result.chart->timeSignature.denominator, 4);
    CHECK_EQ (result.chart->measureCount(), 2);
}

TEST ("a bar of no beats is not a metre")
{
    // "T04" reads as a bar of no beats. It divided by nothing further down and
    // went out on the wire as beatsPerBar: 0, which is not a tune anyone is in.
    const auto result = importIRealPro ("irealbook://T=C=S=C=n=*A[T04C^7 |D-7 ]Z");

    CHECK (result.ok());
    CHECK_EQ (result.chart->timeSignature.numerator, 4);
    CHECK_EQ (result.chart->timeSignature.denominator, 4);
}

TEST ("the metres iReal Pro really writes still read")
{
    struct Case { const char* body; int numerator; int denominator; };

    const Case cases[] = {
        { "T44", 4, 4 }, { "T34", 3, 4 }, { "T24", 2, 4 }, { "T54", 5, 4 },
        { "T68", 6, 8 }, { "T78", 7, 8 }, { "T98", 9, 8 }, { "T128", 12, 8 },
        { "T22", 2, 2 }, { "T32", 3, 2 }
    };

    for (const auto& one : cases)
    {
        const auto result = importIRealPro ("irealbook://T=C=S=C=n=*A["
                                            + std::string (one.body) + "C^7 |D-7 ]Z");

        CHECK (result.ok());

        if (result.ok())
        {
            CHECK_EQ (result.chart->timeSignature.numerator, one.numerator);
            CHECK_EQ (result.chart->timeSignature.denominator, one.denominator);
        }
    }
}

TEST ("a page whose metre is not a number is still a page")
{
    // An ordinary PDF is enough for this: "Time Signature:" followed by
    // anything that is not two numbers was a word handed straight to std::stoi.
    for (const char* spoken : { "Time Signature: many, lots",
                                "Time Signature: 0Z",
                                "Time Signature: 99999999999999999999, 4",
                                "Time Signature:" })
    {
        const auto result = chartFromPlacedText ({ { 18.0, 40.0, spoken },
                                                   { 18.0, 100.0, "Bar 1, c Major 7" },
                                                   { 18.0, 140.0, "Bar 2, d Minor 7" } });

        CHECK (result.ok());

        if (result.ok())
        {
            CHECK_EQ (result.chart->timeSignature.numerator, 4);
            CHECK_EQ (result.chart->measureCount(), 2);
        }
    }
}

TEST ("a page that does say its metre is still read in it")
{
    const auto result = chartFromPlacedText ({ { 18.0, 40.0, "Time Signature: 3, 4" },
                                               { 18.0, 100.0, "Bar 1, c Major 7" } });

    CHECK (result.ok());
    CHECK_EQ (result.chart->timeSignature.numerator, 3);
    CHECK_EQ (result.chart->timeSignature.denominator, 4);
}

TEST ("a bar number too long to be one is not read as a bar")
{
    // It used to be accumulated a digit at a time into an int, which overflowed
    // and wrapped round into a bar the page never described.
    const auto result = chartFromPlacedText ({ { 18.0, 100.0, "Bar 99999999999999999999, c Major 7" },
                                               { 18.0, 140.0, "Bar 2, d Minor 7" } });

    CHECK (result.ok());
    CHECK_EQ (result.chart->measureCount(), 1);
    CHECK_EQ (result.chart->measures[0].slots[0].chord.toString(), std::string ("Dm7"));
}
