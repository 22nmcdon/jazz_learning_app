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

    /** Flattens the chart into chord-per-slot order, for reharmonisation and
        voice-leading passes that do not care about bar lines.
    */
    std::vector<ChordSymbol> flattenedChords() const;

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

    bool ok() const { return chart.has_value(); }
};

/** Parses pipe-delimited progression text:

        | Dm7 | G7 | Cmaj7 | % |

    Each bar is separated by '|', chords within a bar by whitespace, and '%'
    repeats the previous measure. A run of bar lines - a line break between
    bars, or a "||" repeat mark - opens a single measure.
*/
ChartParseResult parseProgressionText (std::string_view text, std::string title = {});

/** Anything that can turn an external file into a Chart.

    Only the plain-text importer ships in the POC. iReal Pro and MusicXML import
    are named in the design doc but their scope is an open question, so the
    interface exists and the implementations do not - see README.
*/
class ChartImporter
{
public:
    virtual ~ChartImporter() = default;

    /** Name shown in the import UI, e.g. "Text progression". */
    virtual std::string formatName() const = 0;

    /** Cheap check so the shell can pick an importer for pasted content. */
    virtual bool canImport (std::string_view content) const = 0;

    virtual ChartParseResult import (std::string_view content) const = 0;
};

class TextProgressionImporter : public ChartImporter
{
public:
    std::string formatName() const override { return "Text progression"; }
    bool canImport (std::string_view content) const override;
    ChartParseResult import (std::string_view content) const override;
};

} // namespace jazz::core
