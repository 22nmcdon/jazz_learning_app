#include "jazz/core/ChartFormats.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
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
    // Spell it the way the chord spells itself, so a chart that came in writing
    // F#maj9 goes back out writing F#maj9.
    auto text = pitchClassName (chord.root(), chord.accidental());

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
        text += "/" + pitchClassName (*chord.bass(), chord.accidental());

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

    /** Can this character be part of a chord symbol?

        Saying what belongs in a chord is safer than listing what ends one. iReal
        Pro packs padding and marks in against the chords - "Db^7XyQ|", "F#^9/BLZ" -
        and the list of those grows with the format, while the spelling of a chord
        does not. Anything not spelled here ends the token.
    */
    bool canAppearInChordToken (char c)
    {
        if (c >= 'A' && c <= 'G')                             // roots and bass notes
            return true;

        if (std::isdigit (static_cast<unsigned char> (c)))
            return true;

        // The letters chord qualities are spelled with: add, alt, aug, dim, h, maj,
        // min, o, sus - and 'b' for a flat.
        return std::string_view ("abdghijlmnostu").find (c) != std::string_view::npos
               || c == '#' || c == '^' || c == '-' || c == '+' || c == '/';
    }

    /** Undoes the shuffling iReal Pro applies to an irealb:// body.

        The body is cut into 50-character blocks; in each one the first five
        characters swap with the last five, and characters 10 to 23 swap with the
        24 to 39 facing them. A block shorter than that, and a trailing remainder,
        are left alone. The same shuffle undoes itself, which is why iReal Pro can
        use one routine for both directions.
    */
    std::string unscrambleIRealPro (std::string_view body)
    {
        const auto unshuffle = [] (std::string block)
        {
            for (std::size_t i = 0; i < 5; ++i)
                std::swap (block[i], block[49 - i]);

            for (std::size_t i = 10; i < 24; ++i)
                std::swap (block[i], block[49 - i]);

            return block;
        };

        std::string out;

        while (body.size() > 50)
        {
            const auto block = std::string (body.substr (0, 50));
            body.remove_prefix (50);

            // A block with almost nothing after it was never shuffled.
            out += body.size() < 2 ? block : unshuffle (block);
        }

        return out + std::string (body);
    }

    /** Divides each bar's beats between the chords written in it. */
    void shareBeatsAcrossBars (Chart& chart)
    {
        for (auto& bar : chart.measures)
        {
            const auto beats = chart.timeSignature.numerator;
            const auto perChord = std::max (1, beats / static_cast<int> (bar.slots.size()));

            for (auto& slot : bar.slots)
                slot.beats = perChord;

            bar.slots.back().beats
                = std::max (1, beats - perChord * (static_cast<int> (bar.slots.size()) - 1));
        }
    }

    /** iReal Pro files a composer under "Last First" and shows "First Last". */
    std::string composerAsWritten (const std::string& stored)
    {
        const auto space = stored.find (' ');

        if (space == std::string::npos || stored.find (' ', space + 1) != std::string::npos)
            return stored;

        return stored.substr (space + 1) + " " + stored.substr (0, space);
    }
}

ChartParseResult importIRealPro (std::string_view text)
{
    auto decoded = urlDecoded (text);

    if (const auto prefix = decoded.find ("irealbook://"); prefix != std::string::npos)
        decoded = decoded.substr (prefix + std::string ("irealbook://").size());
    else if (const auto shortPrefix = decoded.find ("irealb://"); shortPrefix != std::string::npos)
        decoded = decoded.substr (shortPrefix + std::string ("irealb://").size());

    Chart chart;

    // The two formats lay their fields out differently, so split the lot and work
    // out which is which from the marker iReal Pro puts in front of a shuffled
    // body:
    //   irealbook://Title=Composer=Style=Key=n=chords
    //   irealb://Title=Composer==Style=Key==1r34LbKcu7<shuffled chords>==0=0
    std::vector<std::string> fields;
    std::string current;

    for (auto c : decoded)
    {
        if (c == '=')
        {
            fields.push_back (std::exchange (current, std::string {}));
            continue;
        }

        current += c;
    }

    fields.push_back (current);

    static const std::string marker = "1r34LbKcu7";

    auto bodyField = fields.size();
    auto scrambled = false;

    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        if (fields[i].find (marker) != std::string::npos)
        {
            bodyField = i;
            scrambled = true;
            break;
        }
    }

    std::string body;

    if (scrambled)
    {
        if (bodyField < 4)
            return { std::nullopt, "This iReal Pro link is missing the song's details." };

        chart.style = fields[3];
        chart.composer = composerAsWritten (fields[1]);

        const auto& raw = fields[bodyField];
        body = unscrambleIRealPro (raw.substr (raw.find (marker) + marker.size()));
    }
    else
    {
        // Everything after the fifth "=" is body, including any "=" in it.
        if (fields.size() < 6)
            return { std::nullopt, "This does not look like an iReal Pro link: expected "
                                   "Title=Composer=Style=Key=n=chords." };

        chart.style = fields[2];
        chart.composer = fields[1];

        body = fields[5];

        for (std::size_t i = 6; i < fields.size(); ++i)
            body += "=" + fields[i];
    }

    chart.title = fields[0].empty() ? "Imported chart" : fields[0];

    Measure measure;
    auto sawAnyChord = false;
    auto sawTimeSignature = false;
    std::vector<std::string> unreadable;

    // "|   |" is a bar that holds the chord before it. "]Z" is two closing marks
    // with nothing between them, and "|XyQ  {" is the padding iReal Pro uses to
    // fill out a row - neither is a bar. What separates the three is the mark the
    // bar closes on: only a plain barline closes a bar that was really there.
    const auto closeMeasure = [&chart, &measure] (char closedBy)
    {
        if (measure.slots.empty() && closedBy == '|' && ! chart.measures.empty())
            measure = chart.measures.back();

        if (! measure.slots.empty())
            chart.appendMeasure (measure);

        measure = Measure {};
    };

    for (std::size_t i = 0; i < body.size(); ++i)
    {
        const auto c = body[i];

        if (isBarDelimiter (c))
        {
            closeMeasure (c);
            continue;
        }

        if (std::isspace (static_cast<unsigned char> (c)))
            continue;

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
                // A tune that changes metre writes several of these. A Chart holds
                // one, so it holds the one the tune opens in.
                if (! sawTimeSignature)
                {
                    chart.timeSignature.numerator = body[i + 1] - '0';
                    chart.timeSignature.denominator = body[i + 2] - '0';
                    sawTimeSignature = true;
                }

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

        while (end < body.size() && canAppearInChordToken (body[end]))
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

    closeMeasure ('\0');   // whatever is left over is not a sustained bar

    if (! sawAnyChord)
        return { std::nullopt, "No chords found in that iReal Pro link." };

    shareBeatsAcrossBars (chart);

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

namespace
{
    //==========================================================================
    // iReal Pro does not write its chord symbols as text: it draws them, and
    // attaches a spoken description to each - "Bar 1, d Flat Major  7". Pull the
    // text off one of its PDFs and the chords are not in it; these descriptions
    // are. Being spelled out in words they are less ambiguous than the symbols
    // would have been, and they carry the bar numbers with them, so this reader
    // never has to guess where a barline was.

    std::vector<std::string> spokenWords (const std::string& text)
    {
        std::vector<std::string> words;

        for (std::size_t i = 0; i < text.size();)
        {
            const auto c = static_cast<unsigned char> (text[i]);

            // Runs of letters and runs of digits are separate words even when
            // nothing separates them: "7Flat" is a flattened seventh.
            const auto isLetter = std::isalpha (c) != 0;
            const auto isDigit = std::isdigit (c) != 0;

            if (! isLetter && ! isDigit)
            {
                ++i;
                continue;
            }

            auto end = i;

            while (end < text.size()
                   && (isLetter ? std::isalpha (static_cast<unsigned char> (text[end])) != 0
                                : std::isdigit (static_cast<unsigned char> (text[end])) != 0))
                ++end;

            auto word = text.substr (i, end - i);

            for (auto& character : word)
                character = static_cast<char> (std::tolower (static_cast<unsigned char> (character)));

            words.push_back (std::move (word));
            i = end;
        }

        return words;
    }

    /** Turns "f Sharp Major  9 Over b" into "F#maj9/B", which the chord parser
        already reads. Returns nothing when a word is not in the vocabulary,
        rather than dropping it and returning a chord that is nearly right.
    */
    std::optional<std::string> spokenChordSymbol (const std::string& text)
    {
        const auto words = spokenWords (text);

        if (words.empty())
            return std::nullopt;

        std::string symbol;

        // A word of one letter is always a note: the root, or the bass after
        // "over". Every other word in the vocabulary is longer than that.
        const auto takeNote = [&symbol] (const std::string& word)
        {
            if (word.size() != 1 || word[0] < 'a' || word[0] > 'g')
                return false;

            symbol += static_cast<char> (std::toupper (static_cast<unsigned char> (word[0])));
            return true;
        };

        if (! takeNote (words.front()))
            return std::nullopt;

        for (std::size_t i = 1; i < words.size(); ++i)
        {
            const auto& word = words[i];

            if (std::isdigit (static_cast<unsigned char> (word[0])) != 0)
            {
                symbol += word;
                continue;
            }

            if (takeNote (word))
                continue;

            // "Half Diminished" is the only two-word quality.
            if (word == "half" && i + 1 < words.size() && words[i + 1] == "diminished")
            {
                symbol += "m7b5";
                ++i;
                continue;
            }

            static const std::vector<std::pair<std::string, std::string>> vocabulary {
                { "flat", "b" },      { "sharp", "#" },     { "major", "maj" },
                { "minor", "m" },     { "diminished", "dim" }, { "augmented", "+" },
                { "aug", "+" },       { "suspended", "sus" },  { "sus", "sus" },
                { "altered", "alt" }, { "alt", "alt" },     { "add", "add" },
                { "over", "/" }
            };

            const auto found = std::find_if (vocabulary.begin(), vocabulary.end(),
                                             [&word] (const auto& entry) { return entry.first == word; });

            if (found == vocabulary.end())
                return std::nullopt;

            symbol += found->second;
        }

        return symbol;
    }

    bool containsIgnoringCase (const std::string& haystack, const std::string& needle)
    {
        const auto found = std::search (haystack.begin(), haystack.end(),
                                        needle.begin(), needle.end(),
                                        [] (char a, char b)
                                        {
                                            return std::tolower (static_cast<unsigned char> (a))
                                                   == std::tolower (static_cast<unsigned char> (b));
                                        });

        return found != haystack.end();
    }

    /** The marks iReal Pro describes between the chords. They are not chords, and
        several of them start on a note name, so they are ruled out by name.
    */
    bool isSpokenPageMark (const std::string& text)
    {
        static const char* const marks[] = {
            "bar line", "ending", "repeat", "segno", "coda", "fine", "fermata",
            "made with", "title:", "composer:", "style:", "time signature", "measure"
        };

        return std::any_of (std::begin (marks), std::end (marks),
                            [&text] (const char* mark) { return containsIgnoringCase (text, mark); });
    }

    std::string trimmed (std::string text)
    {
        const auto notSpace = [] (unsigned char c) { return std::isspace (c) == 0; };

        text.erase (text.begin(), std::find_if (text.begin(), text.end(), notSpace));
        text.erase (std::find_if (text.rbegin(), text.rend(), notSpace).base(), text.end());

        // iReal Pro writes its style in brackets: "(Medium Swing)".
        if (text.size() > 1 && text.front() == '(' && text.back() == ')')
            return text.substr (1, text.size() - 2);

        return text;
    }

    /** Reads "Bar 12, c Minor 7" - the number, and the description after it. */
    bool readBarLabel (const std::string& text, int& number, std::string& description)
    {
        if (text.rfind ("Bar ", 0) != 0)
            return false;

        std::size_t i = 4;
        auto digits = 0;
        number = 0;

        while (i < text.size() && std::isdigit (static_cast<unsigned char> (text[i])) != 0)
        {
            number = number * 10 + (text[i] - '0');
            ++i;
            ++digits;
        }

        if (digits == 0 || i >= text.size() || text[i] != ',')
            return false;

        description = trimmed (text.substr (i + 1));
        return true;
    }

    bool looksLikeIRealProPage (const std::vector<PlacedText>& items)
    {
        auto number = 0;
        std::string description;

        return std::any_of (items.begin(), items.end(), [&] (const PlacedText& item)
                            { return readBarLabel (item.text, number, description); });
    }

    ChartParseResult chartFromSpokenPage (const std::vector<PlacedText>& items,
                                          const PageReadingOptions& options)
    {
        struct SpokenBar
        {
            double x {};
            double y {};
            std::vector<ChordSymbol> chords;
        };

        Chart chart;
        std::map<int, SpokenBar> bars;      // keyed by bar number: order and gaps sort themselves out
        std::vector<std::string> unreadable;
        auto sawTimeSignature = false;

        const auto readChord = [&unreadable] (const std::string& description) -> std::optional<ChordSymbol>
        {
            if (const auto symbol = spokenChordSymbol (description))
                if (const auto chord = ChordSymbol::parse (*symbol))
                    return chord;

            unreadable.push_back (description);
            return std::nullopt;
        };

        // First pass: the bars themselves, and what iReal Pro says about the song.
        for (const auto& item : items)
        {
            auto number = 0;
            std::string description;

            if (readBarLabel (item.text, number, description))
            {
                auto& bar = bars[number];
                bar.x = item.x;
                bar.y = item.y;

                if (const auto chord = readChord (description))
                    bar.chords.push_back (*chord);

                continue;
            }

            if (item.text.rfind ("Title:", 0) == 0)
                chart.title = trimmed (item.text.substr (6));
            else if (item.text.rfind ("Composer:", 0) == 0)
                chart.composer = trimmed (item.text.substr (9));
            else if (item.text.rfind ("Style:", 0) == 0)
                chart.style = trimmed (item.text.substr (6));
            else if (item.text.rfind ("Time Signature:", 0) == 0 && ! sawTimeSignature)
            {
                // "Time Signature: 6, 4", and a tune that changes metre says so
                // more than once; a Chart keeps the one it opens in.
                const auto numbers = spokenWords (item.text.substr (15));

                if (numbers.size() >= 2)
                {
                    chart.timeSignature.numerator = std::stoi (numbers[0]);
                    chart.timeSignature.denominator = std::stoi (numbers[1]);
                    sawTimeSignature = true;
                }
            }
        }

        if (bars.empty())
            return { std::nullopt, "That page is from iReal Pro, but no bars were described on it." };

        // Second pass: a bar can hold more than one chord, and the second one is
        // described on its own. It belongs to the nearest bar to its left on the
        // same line - which holds however the reader chose to order the page.
        for (const auto& item : items)
        {
            auto number = 0;
            std::string description;

            if (readBarLabel (item.text, number, description) || isSpokenPageMark (item.text))
                continue;

            if (! spokenChordSymbol (item.text).has_value())
                continue;   // the song title and the composer's name, printed on the page

            auto owner = bars.end();

            for (auto entry = bars.begin(); entry != bars.end(); ++entry)
            {
                if (std::abs (entry->second.y - item.y) > options.lineTolerance)
                    continue;

                if (entry->second.x > item.x)
                    continue;

                if (owner == bars.end() || entry->second.x > owner->second.x)
                    owner = entry;
            }

            if (owner == bars.end())
                continue;

            if (const auto chord = readChord (item.text))
                owner->second.chords.push_back (*chord);
        }

        for (auto& entry : bars)
        {
            if (entry.second.chords.empty())
                continue;

            Measure measure;

            for (const auto& chord : entry.second.chords)
                measure.slots.push_back ({ chord, 0 });

            chart.appendMeasure (measure);
        }

        if (chart.measures.empty())
            return { std::nullopt, "That page is from iReal Pro, but none of its chords could be read." };

        if (chart.title.empty())
            chart.title = "Imported chart";

        shareBeatsAcrossBars (chart);

        return { std::move (chart), {}, std::move (unreadable) };
    }
}

//==============================================================================
ChartParseResult chartFromPlacedText (std::vector<PlacedText> items, PageReadingOptions options)
{
    if (items.empty())
        return { std::nullopt, "There is no text on that page." };

    // A page from iReal Pro carries its chords as spoken descriptions rather than
    // as symbols, and says which bar each one is in, so it is read on its own
    // terms rather than by where the text sits.
    if (looksLikeIRealProPage (items))
        return chartFromSpokenPage (items, options);

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
