#pragma once

#include "jazz/core/ChordSymbol.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jazz::core
{

/** One chord and how long it lasts within its measure. */
struct ChordSlot
{
    ChordSymbol chord;
    int beats { 4 };
};

struct Measure
{
    std::vector<ChordSlot> slots;
    std::string sectionLabel;  ///< "A", "B", "Bridge" - empty unless the measure starts a section

    bool isEmpty() const { return slots.empty(); }
};

struct TimeSignature
{
    int numerator { 4 };
    int denominator { 4 };
};

/** A chord chart: the progression the two POC modules work against. */
class Chart
{
public:
    std::string title;
    std::string composer;
    std::string style;
    int tempoBpm { 120 };
    TimeSignature timeSignature;
    std::vector<Measure> measures;

    int measureCount() const { return static_cast<int> (measures.size()); }

    /** The chord sounding at a position, or nullptr if the position is empty. */
    const ChordSymbol* chordAt (int measureIndex, int beat = 0) const;

    Chart transposed (int semitones) const;

    /** Renders the progression back to the text format parsed below. */
    std::string toProgressionText() const;

    void appendMeasure (Measure measure) { measures.push_back (std::move (measure)); }
};

/** Outcome of parsing user- or file-supplied chart text. */
struct ChartParseResult
{
    std::optional<Chart> chart;
    std::string error;          ///< empty when parsing succeeded

    /** Chord symbols the reader found but could not understand, in the order
        they appeared. A chart can come through with these and still be usable,
        but it is missing those chords, so an importer should say so rather than
        hand back a chart that looks complete and is not.
    */
    std::vector<std::string> unreadable {};

    bool ok() const { return chart.has_value(); }
};

/** Parses pipe-delimited progression text:

        | Dm7 | G7 | Cmaj7 | % |

    Each bar is separated by '|', chords within a bar by whitespace, and '%'
    repeats the previous measure. A run of bar lines - a line break between
    bars, or a "||" repeat mark - opens a single measure.
*/
ChartParseResult parseProgressionText (std::string_view text, std::string title = {});

} // namespace jazz::core
