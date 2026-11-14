#pragma once

// What more than one of the wire's four files needs.
//
// Minimal JSON writing, and reading flat text back off the wire. The engine has
// no serialisation of its own and should not grow any: how results are encoded
// is the shell's business, which is what this whole module is.
//
// The bar for living here is **two callers in two different files**. A helper
// only one of them uses stays in that file, in its own anonymous namespace,
// where it can be read beside the call that wants it - which is why this is a
// short header and not a second engine.
//
// Header-only and `inline` throughout: every one of these is a few lines, and a
// `Wire.cpp` holding ten of them would be a translation unit that exists to
// hold a translation unit.

#include "jazz/core/VoicingAnalyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace jazz::api::wire
{

/*  Named one at a time rather than with `using namespace jazz::core`, which a
    header has no business doing to every file that includes it. */
using jazz::core::FindingSeverity;

inline std::string quoted (const std::string& text)
{
    std::string out = "\"";

    for (auto c : text)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char> (c) < 0x20)
                    out += ' ';
                else
                    out += c;
        }
    }

    return out + "\"";
}

template <typename Item, typename Fn>
std::string jsonArray (const std::vector<Item>& items, Fn&& toJson)
{
    std::string out = "[";

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            out += ",";

        out += toJson (items[i]);
    }

    return out + "]";
}

inline std::string jsonError (const std::string& message)
{
    return "{\"ok\":false,\"error\":" + quoted (message) + "}";
}

/** Was a static buffer when these returned `const char*` for C. The
    answers are std::string now, so this is the identity - kept only so
    every entry point below reads exactly as it did before the move.
*/
inline std::string hold (std::string&& json)
{
    return std::move (json);
}

inline std::vector<int> parseNoteList (const std::string& csv)
{
    std::vector<int> notes;
    std::string current;

    /*  A note that will not read is dropped, not thrown over.

        `readHit` wraps its own three conversions and then calls this one
        *outside* that try, so a hit whose notes were rubbish threw straight
        through `compHit` and out of the shell. Both shells now catch as
        well, but the reader should not be the thing that needs catching.

        Dropping is the same answer `coverageOf` gives a chord symbol it
        cannot name: the caller gets a shorter list, which every analyser
        here already handles, because an empty one is an ordinary case. */
    const auto flush = [&notes, &current]
    {
        if (! current.empty())
        {
            try
            {
                notes.push_back (std::stoi (current));
            }
            catch (...)
            {
                // Not a note. The list is shorter by one.
            }

            current.clear();
        }
    };

    for (auto c : csv)
    {
        if (c == ',' || c == ' ')
            flush();
        else
            current += c;
    }

    flush();
    return notes;
}

/** A number the engine may not have. Null rather than zero: nothing played
    is not nought out of a hundred, and a page drawing a nought would say
    exactly the thing the engine was careful not to. */
inline std::string orNull (const std::optional<int>& value)
{
    return value.has_value() ? std::to_string (*value) : std::string ("null");
}

/** The harmony a take was played over, as the two masks a practice row
    stores.

    On the take rather than worked out by a shell, because which quality
    "Am7b5" is is theory and CLAUDE.md puts theory in the engine. A page
    parsing chord symbols for itself is a second chord reader, and it would
    be wrong about the first symbol somebody spells unusually.
*/
inline std::string coverageJson (unsigned int qualities, unsigned int roots)
{
    return ",\"qualities\":" + std::to_string (qualities)
         + ",\"roots\":" + std::to_string (roots);
}

inline std::string severityName (FindingSeverity severity)
{
    switch (severity)
    {
        case FindingSeverity::problem:    return "problem";
        case FindingSeverity::suggestion: return "suggestion";
        case FindingSeverity::good:       break;
    }

    return "good";
}

/** Splits on one character, keeping empty fields - an empty field is a value
    in this grammar, not a gap. */
inline std::vector<std::string> splitOn (const std::string& text, char separator)
{
    std::vector<std::string> fields { "" };

    for (auto c : text)
    {
        if (c == separator)
            fields.push_back ("");
        else
            fields.back() += c;
    }

    return fields;
}

/** A whole number inside a stated range, or nothing at all.

    Refuses rather than clamps, and refuses trailing rubbish as well as an
    out-of-range value. A tick of 900 is not a tick this reader should round
    down to 23 - it is a message that did not mean what it says, and comping
    something plausible out of it is how a shell's bug becomes a mystery about
    the band. Clamping belongs where a value is *computed*; this is where one
    is *received*.
*/
inline std::optional<int> numberIn (const std::string& text, int low, int high)
{
    if (text.empty())
        return {};

    try
    {
        std::size_t read = 0;
        const auto value = std::stoi (text, &read);

        if (read != text.size() || value < low || value > high)
            return {};

        return value;
    }
    catch (...)
    {
        return {};
    }
}

} // namespace jazz::api::wire
