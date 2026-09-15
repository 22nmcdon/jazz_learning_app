#include "jazz/core/ChartFormats.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

namespace jazz::core
{

namespace
{
    bool has (const ChordSymbol& chord, Extension extension)
    {
        const auto& extensions = chord.extensions();
        return std::find (extensions.begin(), extensions.end(), extension) != extensions.end();
    }

    /** The highest natural extension, which iReal Pro writes in place of the 7. */
    std::string highestNatural (const ChordSymbol& chord)
    {
        if (has (chord, Extension::thirteen)) return "13";
        if (has (chord, Extension::eleven))   return "11";
        if (has (chord, Extension::nine))     return "9";
        return "7";
    }

    void appendAlterations (const ChordSymbol& chord, std::string& text)
    {
        const auto altered = { Extension::flatFive, Extension::sharpFive, Extension::flatNine,
                               Extension::sharpNine, Extension::sharpEleven, Extension::flatThirteen };

        for (auto extension : altered)
        {
            if (! has (chord, extension))
                continue;

            // The quality already carries these two.
            if (extension == Extension::flatFive
                && (chord.quality() == ChordQuality::diminished
                    || chord.quality() == ChordQuality::halfDiminished))
                continue;

            if (extension == Extension::sharpFive && chord.quality() == ChordQuality::augmented)
                continue;

            text += extensionLabel (extension);
        }
    }
}

std::string toIRealProSymbol (const ChordSymbol& chord)
{
    auto text = pitchClassName (chord.root());

    const auto seventh = chord.seventh();
    const auto highest = highestNatural (chord);
    const auto isAltered = has (chord, Extension::flatNine) && has (chord, Extension::sharpNine)
                           && has (chord, Extension::sharpEleven) && has (chord, Extension::flatThirteen);

    switch (chord.quality())
    {
        case ChordQuality::major:
            if (seventh == SeventhType::major)
                text += "^" + highest;
            else if (has (chord, Extension::six))
                text += has (chord, Extension::nine) ? "69" : "6";
            else if (has (chord, Extension::nine))
                text += "add9";
            break;

        case ChordQuality::minor:
            text += "-";
            if (seventh == SeventhType::minor)
                text += highest;
            else if (has (chord, Extension::six))
                text += has (chord, Extension::nine) ? "69" : "6";
            break;

        case ChordQuality::minorMajor:
            text += "-^" + (seventh == SeventhType::major ? highest : std::string ("7"));
            break;

        case ChordQuality::dominant:
            if (isAltered)
            {
                text += "7alt";
                return text;   // "alt" says it all; no alterations after it
            }

            text += highest;
            break;

        case ChordQuality::halfDiminished:
            text += "h" + (seventh == SeventhType::minor ? highest : std::string ("7"));
            break;

        case ChordQuality::diminished:
            text += seventh == SeventhType::diminished ? "o7" : "o";
            break;

        case ChordQuality::augmented:
            text += seventh == SeventhType::none ? "+" : "+" + highest;
            break;

        case ChordQuality::suspended:
            if (seventh != SeventhType::none)
                text += highest;
            text += "sus";
            break;
    }

    appendAlterations (chord, text);

    if (chord.bass().has_value() && *chord.bass() != chord.root())
        text += "/" + pitchClassName (*chord.bass());

    return text;
}

std::string exportIRealPro (const Chart& chart)
{
    std::string body = "*A[T"
                       + std::to_string (chart.timeSignature.numerator)
                       + std::to_string (chart.timeSignature.denominator);

    for (auto index = 0; index < chart.measureCount(); ++index)
    {
        const auto& measure = chart.measures[static_cast<std::size_t> (index)];

        if (index > 0)
            body += "|";

        for (std::size_t slot = 0; slot < measure.slots.size(); ++slot)
        {
            if (slot > 0)
                body += " ";

            body += toIRealProSymbol (measure.slots[slot].chord);
        }

        // iReal Pro pads bars with spaces; one keeps the bar from running into
        // the next chord when the app re-lays it out.
        body += " ";
    }

    body += "]Z";

    return "irealbook://" + chart.title + "=" + chart.composer + "="
           + (chart.style.empty() ? std::string ("Medium Swing") : chart.style)
           + "=C=n=" + body;
}

bool looksLikeIRealPro (std::string_view text)
{
    return text.find ("irealbook://") != std::string_view::npos
           || text.find ("irealb://") != std::string_view::npos
           || text.find ("=n=") != std::string_view::npos;
}

namespace
{
    std::string urlDecoded (std::string_view text)
    {
        std::string out;

        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '%' && i + 2 < text.size()
                && std::isxdigit (static_cast<unsigned char> (text[i + 1]))
                && std::isxdigit (static_cast<unsigned char> (text[i + 2])))
            {
                out += static_cast<char> (std::stoi (std::string (text.substr (i + 1, 2)), nullptr, 16));
                i += 2;
            }
            else
            {
                out += text[i] == '+' ? ' ' : text[i];
            }
        }

        return out;
    }

    bool isBarDelimiter (char c)
    {
        return c == '|' || c == '[' || c == ']' || c == '{' || c == '}' || c == 'Z';
    }

    /** Characters that end a chord token: anything that is not part of a symbol. */
    bool endsChordToken (char c)
    {
        return std::isspace (static_cast<unsigned char> (c)) || isBarDelimiter (c)
               || c == ',' || c == '(' || c == ')' || c == '<' || c == '>' || c == '*';
    }
}

ChartParseResult importIRealPro (std::string_view text)
{
    auto decoded = urlDecoded (text);

    if (decoded.find ("irealb://") != std::string::npos
        && decoded.find ("irealbook://") == std::string::npos)
    {
        return { std::nullopt,
                 "That is an irealb:// link, whose chords are scrambled by iReal Pro. Export the "
                 "song again as an irealbook:// link, or paste the chords as text." };
    }

    if (const auto prefix = decoded.find ("irealbook://"); prefix != std::string::npos)
        decoded = decoded.substr (prefix + std::string ("irealbook://").size());

    Chart chart;

    // Title=Composer=Style=Key=transpose=body, and the body is everything left.
    std::vector<std::string> fields;
    std::string current;

    for (auto c : decoded)
    {
        if (c == '=' && fields.size() < 5)
        {
            fields.push_back (std::exchange (current, std::string {}));
            continue;
        }

        current += c;
    }

    fields.push_back (current);

    if (fields.size() < 6)
        return { std::nullopt, "This does not look like an iReal Pro link: expected "
                               "Title=Composer=Style=Key=n=chords." };

    chart.title = fields[0].empty() ? "Imported chart" : fields[0];
    chart.composer = fields[1];
    chart.style = fields[2];

    const auto& body = fields[5];

    Measure measure;
    auto sawAnyChord = false;
    std::vector<std::string> unreadable;

    // "|   |" is a bar that holds the chord before it; "]Z" is two closing
    // marks with nothing between them. The space is what tells them apart.
    auto sawSpaceInBar = false;

    const auto closeMeasure = [&chart, &measure, &sawSpaceInBar] (bool blankBarSustains)
    {
        if (measure.slots.empty() && blankBarSustains && sawSpaceInBar && ! chart.measures.empty())
            measure = chart.measures.back();

        if (! measure.slots.empty())
            chart.appendMeasure (measure);

        measure = Measure {};
        sawSpaceInBar = false;
    };

    for (std::size_t i = 0; i < body.size(); ++i)
    {
        const auto c = body[i];

        if (isBarDelimiter (c))
        {
            closeMeasure (true);
            continue;
        }

        if (std::isspace (static_cast<unsigned char> (c)))
        {
            sawSpaceInBar = true;
            continue;
        }

        if (c == '<')   // an annotation: skip to its end
        {
            while (i < body.size() && body[i] != '>')
                ++i;

            continue;
        }

        if (c == '*' || c == 'N')   // section marker or ending number
        {
            ++i;   // and the letter or digit that names it
            continue;
        }

        if (c == 'T')   // time signature, e.g. T44
        {
            if (i + 2 < body.size() && std::isdigit (static_cast<unsigned char> (body[i + 1])))
            {
                chart.timeSignature.numerator = body[i + 1] - '0';
                chart.timeSignature.denominator = body[i + 2] - '0';
                i += 2;
            }

            continue;
        }

        if (c == 'x')   // repeat the previous bar
        {
            if (! chart.measures.empty())
                measure = chart.measures.back();

            continue;
        }

        if (! (c >= 'A' && c <= 'G'))
            continue;   // spacing, rests, coda signs and other page furniture

        auto end = i;

        while (end < body.size() && ! endsChordToken (body[end]))
            ++end;

        const auto token = body.substr (i, end - i);
        i = end - 1;

        if (const auto chord = ChordSymbol::parse (token))
        {
            measure.slots.push_back ({ *chord, 0 });
            sawAnyChord = true;
        }
        else
        {
            // It starts on a note name, so it was meant to be a chord. Losing it
            // quietly would hand back a chart that looks complete and is not.
            unreadable.push_back (token);
        }
    }

    closeMeasure (false);   // whatever is left over is not a sustained bar

    if (! sawAnyChord)
        return { std::nullopt, "No chords found in that iReal Pro link." };

    // Share each bar out between the chords written in it.
    for (auto& bar : chart.measures)
    {
        const auto beats = chart.timeSignature.numerator;
        const auto perChord = std::max (1, beats / static_cast<int> (bar.slots.size()));

        for (auto& slot : bar.slots)
            slot.beats = perChord;

        bar.slots.back().beats = std::max (1, beats - perChord * (static_cast<int> (bar.slots.size()) - 1));
    }

    return { std::move (chart), {}, std::move (unreadable) };
}

namespace
{
    /** Stitches the runs of one line back into words.

        With run widths the answer is exact: a run that starts where the last
        one ended is part of the same word. Without them, the gaps on a line
        fall into two groups - inside words and between them - and the widest
        jump between those groups is the line between the two.
    */
    std::vector<PlacedText> joinRunsIntoWords (const std::vector<PlacedText>& line,
                                               const PageReadingOptions& options)
    {
        if (line.size() < 2)
            return line;

        const auto widthsKnown = std::all_of (line.begin(), line.end(),
                                              [] (const PlacedText& item) { return item.width > 0.0; });

        std::vector<double> gaps;

        for (std::size_t i = 1; i < line.size(); ++i)
            gaps.push_back (widthsKnown ? line[i].x - (line[i - 1].x + line[i - 1].width)
                                        : line[i].x - line[i - 1].x);

        auto threshold = options.wordGap;

        if (! widthsKnown)
        {
            auto sorted = gaps;
            std::sort (sorted.begin(), sorted.end());

            // No clear split means every run is its own word: leave them alone
            // rather than gluing separate chords together.
            threshold = -1.0;

            for (std::size_t i = 1; i < sorted.size(); ++i)
                if (sorted[i] > sorted[i - 1] * 2.5 && sorted[i - 1] > 0.0)
                    threshold = (sorted[i] + sorted[i - 1]) * 0.5;
        }

        std::vector<PlacedText> words { line.front() };

        for (std::size_t i = 1; i < line.size(); ++i)
        {
            if (gaps[i - 1] <= threshold)
            {
                words.back().text += line[i].text;
                words.back().width = line[i].x + line[i].width - words.back().x;
            }
            else
            {
                words.push_back (line[i]);
            }
        }

        return words;
    }

    /** A lead sheet puts the tempo/feel marking above the title, so a line being
        the first non-chord line on the page is not enough to make it the title.

        A line is furniture only when the whole of it is made of marking words and
        numbers, which is what lets "Medium Swing" fall away while "Blue Bossa"
        stays. Spacing is ignored before the test: engravers letter-space these
        markings, and a reader hands that back as "M E D I U M  S W I N G".
    */
    bool looksLikePageFurniture (const std::string& text)
    {
        std::string squeezed;

        for (const auto c : text)
            if (! std::isspace (static_cast<unsigned char> (c)))
                squeezed += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));

        if (squeezed.empty())
            return true;

        static const char* const markings[] =
        {
            "swing", "ballad", "bossa", "samba", "latin", "funk", "waltz", "rubato",
            "straight", "shuffle", "bebop", "modal", "blues", "afrocuban", "calypso",
            "medium", "moderate", "slow", "fast", "bright", "easy", "up", "tempo",
            "eighth", "eighths", "quarter", "note", "feel", "groove", "bpm", "time",
            "-", "=", ".", ",", "/", "(", ")", "'"
        };

        // Can the line be read end to end as nothing but markings and numbers?
        std::vector<bool> reachable (squeezed.size() + 1, false);
        reachable[0] = true;

        for (std::size_t i = 0; i < squeezed.size(); ++i)
        {
            if (! reachable[i])
                continue;

            auto digits = i;

            while (digits < squeezed.size() && std::isdigit (static_cast<unsigned char> (squeezed[digits])) != 0)
                ++digits;

            if (digits > i)
                reachable[digits] = true;

            for (const auto* marking : markings)
            {
                const auto length = std::char_traits<char>::length (marking);

                if (squeezed.compare (i, length, marking) == 0)
                    reachable[i + length] = true;
            }
        }

        return reachable.back();
    }

    /** Was this word on a chord line meant to be a chord?

        Only words that carry a digit or an accidental count: that is what tells
        a chord the engine could not read, like a quality it has never met, from
        a rehearsal mark or a word of text sitting on the same line.
    */
    bool looksLikeAChordSymbol (const std::string& text)
    {
        if (text.empty() || text.front() < 'A' || text.front() > 'G')
            return false;

        return std::any_of (text.begin() + 1, text.end(), [] (char c)
                            {
                                return std::isdigit (static_cast<unsigned char> (c)) != 0
                                    || c == '#' || c == '+' || c == '-' || c == '/';
                            });
    }
}

//==============================================================================
ChartParseResult chartFromPlacedText (std::vector<PlacedText> items, PageReadingOptions options)
{
    if (items.empty())
        return { std::nullopt, "There is no text on that page." };

    // Top of the page first, then left to right: reading order.
    std::stable_sort (items.begin(), items.end(), [] (const PlacedText& a, const PlacedText& b)
                      {
                          if (std::abs (a.y - b.y) > 0.01)
                              return a.y < b.y;

                          return a.x < b.x;
                      });

    std::vector<std::vector<PlacedText>> lines;

    for (auto& item : items)
    {
        if (! lines.empty() && std::abs (lines.back().front().y - item.y) <= options.lineTolerance)
            lines.back().push_back (item);
        else
            lines.push_back ({ item });
    }

    Chart chart;
    auto sawAnyChord = false;
    std::vector<std::string> unreadable;

    for (const auto& rawLine : lines)
    {
        const auto line = joinRunsIntoWords (rawLine, options);

        // Keep only what reads as a chord; titles, tempo marks and page numbers
        // fall away here.
        std::vector<std::pair<double, ChordSymbol>> chords;
        std::vector<std::string> missed;

        for (const auto& item : line)
        {
            if (const auto chord = ChordSymbol::parse (item.text))
                chords.push_back ({ item.x, *chord });
            else if (looksLikeAChordSymbol (item.text))
                missed.push_back (item.text);
        }

        if (chords.empty())
        {
            // The first line above the music that is neither chords nor page
            // furniture is the title of the chart.
            if (! sawAnyChord && chart.title.empty() && ! line.empty())
            {
                std::string text;

                for (const auto& word : line)
                    text += (text.empty() ? "" : " ") + word.text;

                if (! looksLikePageFurniture (text))
                    chart.title = text;
            }

            continue;
        }

        sawAnyChord = true;

        // A word that was meant to be a chord but did not read as one is only
        // worth reporting on a line that is music; elsewhere it is page text.
        unreadable.insert (unreadable.end(), missed.begin(), missed.end());

        // Bar lines are drawn, not written, so the spacing has to say where the
        // bars are: a gap much smaller than the usual one means two chords are
        // sharing a bar.
        std::vector<double> gaps;

        for (std::size_t i = 1; i < chords.size(); ++i)
            gaps.push_back (chords[i].first - chords[i - 1].first);

        auto typicalGap = 0.0;

        if (! gaps.empty())
        {
            auto sorted = gaps;
            std::sort (sorted.begin(), sorted.end());
            typicalGap = sorted[sorted.size() / 2];
        }

        Measure measure;

        for (std::size_t i = 0; i < chords.size(); ++i)
        {
            const auto sharesBar = i > 0 && typicalGap > 0.0
                                   && (chords[i].first - chords[i - 1].first)
                                          < typicalGap * options.sameBarRatio;

            if (! sharesBar && ! measure.slots.empty())
            {
                chart.appendMeasure (measure);
                measure = Measure {};
            }

            measure.slots.push_back ({ chords[i].second, 0 });
        }

        if (! measure.slots.empty())
            chart.appendMeasure (measure);
    }

    if (! sawAnyChord)
        return { std::nullopt,
                 "No chord symbols were found on that page. If the chords are drawn as pictures "
                 "rather than text, they cannot be read." };

    if (chart.title.empty())
        chart.title = "Imported chart";

    for (auto& bar : chart.measures)
    {
        const auto beats = chart.timeSignature.numerator;
        const auto perChord = std::max (1, beats / static_cast<int> (bar.slots.size()));

        for (auto& slot : bar.slots)
            slot.beats = perChord;

        bar.slots.back().beats = std::max (1, beats - perChord * (static_cast<int> (bar.slots.size()) - 1));
    }

    return { std::move (chart), {}, std::move (unreadable) };
}

} // namespace jazz::core
