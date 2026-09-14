#include "jazz/core/Chart.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace jazz::core
{

const ChordSymbol* Chart::chordAt (int measureIndex, int beat) const
{
    if (measureIndex < 0 || measureIndex >= measureCount())
        return nullptr;

    const auto& measure = measures[static_cast<std::size_t> (measureIndex)];
    auto beatsSoFar = 0;

    for (const auto& slot : measure.slots)
    {
        beatsSoFar += slot.beats;

        if (beat < beatsSoFar)
            return &slot.chord;
    }

    return measure.slots.empty() ? nullptr : &measure.slots.back().chord;
}

std::vector<ChordSymbol> Chart::flattenedChords() const
{
    std::vector<ChordSymbol> chords;

    for (const auto& measure : measures)
        for (const auto& slot : measure.slots)
            chords.push_back (slot.chord);

    return chords;
}

Chart Chart::transposed (int semitones) const
{
    auto copy = *this;

    for (auto& measure : copy.measures)
        for (auto& slot : measure.slots)
            slot.chord = slot.chord.transposed (semitones);

    return copy;
}

std::string Chart::toProgressionText() const
{
    std::string text;

    for (const auto& measure : measures)
    {
        text += "| ";

        for (const auto& slot : measure.slots)
            text += slot.chord.toString() + " ";
    }

    if (! measures.empty())
        text += "|";

    return text;
}

namespace
{
    std::vector<std::string> splitWhitespace (std::string_view text)
    {
        std::vector<std::string> tokens;
        std::string current;

        for (auto c : text)
        {
            if (std::isspace (static_cast<unsigned char> (c)))
            {
                if (! current.empty())
                    tokens.push_back (std::exchange (current, std::string {}));
            }
            else
            {
                current += c;
            }
        }

        if (! current.empty())
            tokens.push_back (current);

        return tokens;
    }
}

ChartParseResult parseProgressionText (std::string_view text, std::string title)
{
    Chart chart;
    chart.title = title.empty() ? "Untitled" : std::move (title);

    // Newlines act as bar lines, and a run of bar lines (from a line break
    // between bars, or a "||" repeat mark) opens one measure, not several.
    std::string normalised;
    normalised.reserve (text.size());

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = (text[i] == '\n' || text[i] == '\r') ? ' ' : text[i];

        if (c != '|')
        {
            normalised += c;
            continue;
        }

        normalised += '|';

        while (i + 1 < text.size()
               && (text[i + 1] == '|' || text[i + 1] == ' ' || text[i + 1] == '\t'
                   || text[i + 1] == '\n' || text[i + 1] == '\r'))
        {
            // Keep a single space so "| Dm7" still separates cleanly.
            if (text[i + 1] != '|' && normalised.back() != ' ')
                normalised += ' ';

            ++i;
        }
    }

    std::size_t position = 0;
    auto measureNumber = 0;

    while (position < normalised.size())
    {
        const auto barStart = normalised.find ('|', position);

        if (barStart == std::string::npos)
            break;

        const auto nextBar = normalised.find ('|', barStart + 1);
        const auto body = normalised.substr (barStart + 1,
                                             nextBar == std::string::npos
                                                 ? std::string::npos
                                                 : nextBar - barStart - 1);

        ++measureNumber;
        Measure measure;

        for (const auto& token : splitWhitespace (body))
        {
            if (token == "%" || token == "/")
            {
                // Repeat the previous measure.
                if (chart.measures.empty())
                    return { std::nullopt, "Measure " + std::to_string (measureNumber)
                                               + ": '%' has no previous measure to repeat." };

                measure = chart.measures.back();
                measure.sectionLabel.clear();
                break;
            }

            const auto chord = ChordSymbol::parse (token);

            if (! chord.has_value())
                return { std::nullopt, "Measure " + std::to_string (measureNumber)
                                           + ": '" + token + "' is not a chord symbol." };

            measure.slots.push_back ({ *chord, 0 });
        }

        // Split the bar evenly between the chords in it.
        if (! measure.slots.empty())
        {
            const auto beats = chart.timeSignature.numerator;
            const auto perChord = std::max (1, beats / static_cast<int> (measure.slots.size()));

            for (auto& slot : measure.slots)
                slot.beats = perChord;

            measure.slots.back().beats = std::max (1, beats - perChord * (static_cast<int> (measure.slots.size()) - 1));
        }

        chart.appendMeasure (std::move (measure));

        if (nextBar == std::string::npos)
            break;

        position = nextBar;

        // A trailing '|' closes the last bar rather than opening an empty one.
        if (normalised.find_first_not_of (" |", nextBar) == std::string::npos)
            break;
    }

    if (chart.measures.empty())
        return { std::nullopt, "No measures found - write the progression as | Dm7 | G7 | Cmaj7 |" };

    return { std::move (chart), {} };
}

bool TextProgressionImporter::canImport (std::string_view content) const
{
    return content.find ('|') != std::string_view::npos;
}

ChartParseResult TextProgressionImporter::import (std::string_view content) const
{
    return parseProgressionText (content);
}

} // namespace jazz::core
