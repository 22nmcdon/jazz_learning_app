#pragma once

#include <string>
#include <string_view>

/*  Is this actually JSON?

    Every answer the engine gives a shell is JSON built by concatenating
    strings - which is the right call (the wire is the shell's business and the
    engine has no JSON reader, deliberately, so that nothing can be tempted to
    *read* JSON in there) and is also the one way of producing JSON that can
    emit something unparseable without anybody noticing. The tests either side
    of this header all assert with `contains`, on fragments: a stray comma, an
    unescaped quote inside a message or a bare `nan` where a number belongs
    would pass every one of them and fail in the page as
    `SyntaxError: Unexpected token`, with no clue which call produced it.

    So this is a validator rather than a parser: it answers *well formed or
    not* and builds nothing. Written here rather than in `modules/` on purpose
    - the engine must still have no JSON reader in it, and a test is the one
    place a reader is safe, because nothing ships it.

    It is deliberately strict about the things concatenation gets wrong:
    trailing commas, a control character loose in a string, a number like
    `nan`, `inf` or `01`, and anything at all after the closing brace.
*/
namespace jazz::testing
{

class JsonScanner
{
public:
    explicit JsonScanner (std::string_view json) : text (json) {}

    /** True when the whole string is one well-formed JSON value. */
    bool wholeValue()
    {
        skipSpace();

        if (! value())
            return false;

        skipSpace();
        return at == text.size();
    }

private:
    std::string_view text;
    std::size_t at { 0 };

    bool more() const { return at < text.size(); }
    char here() const { return text[at]; }

    void skipSpace()
    {
        while (more() && (here() == ' ' || here() == '\t' || here() == '\n' || here() == '\r'))
            ++at;
    }

    bool take (char c)
    {
        if (! more() || here() != c)
            return false;

        ++at;
        return true;
    }

    bool word (std::string_view expected)
    {
        if (text.compare (at, expected.size(), expected) != 0)
            return false;

        at += expected.size();
        return true;
    }

    bool value()
    {
        if (! more())
            return false;

        switch (here())
        {
            case '{':  return object();
            case '[':  return array();
            case '"':  return string();
            case 't':  return word ("true");
            case 'f':  return word ("false");
            case 'n':  return word ("null");
            default:   return number();
        }
    }

    bool object()
    {
        if (! take ('{'))
            return false;

        skipSpace();

        if (take ('}'))
            return true;

        for (;;)
        {
            skipSpace();

            if (! string())
                return false;

            skipSpace();

            if (! take (':'))
                return false;

            skipSpace();

            if (! value())
                return false;

            skipSpace();

            if (take (','))
                continue;           // and a '}' after this comma is a trailing one

            return take ('}');
        }
    }

    bool array()
    {
        if (! take ('['))
            return false;

        skipSpace();

        if (take (']'))
            return true;

        for (;;)
        {
            skipSpace();

            if (! value())
                return false;

            skipSpace();

            if (take (','))
                continue;

            return take (']');
        }
    }

    bool string()
    {
        if (! take ('"'))
            return false;

        while (more())
        {
            const auto c = here();
            ++at;

            if (c == '"')
                return true;

            if (c == '\\')
            {
                if (! more())
                    return false;

                const auto escaped = here();
                ++at;

                if (escaped == 'u')
                {
                    for (auto i = 0; i < 4; ++i)
                    {
                        if (! more() || ! std::isxdigit (static_cast<unsigned char> (here())))
                            return false;

                        ++at;
                    }

                    continue;
                }

                if (std::string_view ("\"\\/bfnrt").find (escaped) == std::string_view::npos)
                    return false;

                continue;
            }

            // A raw control character in a string is what an un-escaped
            // newline out of an error message looks like.
            if (static_cast<unsigned char> (c) < 0x20)
                return false;
        }

        return false;   // ran off the end without a closing quote
    }

    bool number()
    {
        const auto from = at;

        take ('-');

        if (! more() || ! std::isdigit (static_cast<unsigned char> (here())))
            return false;   // catches "nan", "inf" and an empty slot

        if (take ('0'))
        {
            // 01 is not a number, and a leading zero is what a hand-built
            // integer looks like when something has gone wrong upstream.
            if (more() && std::isdigit (static_cast<unsigned char> (here())))
                return false;
        }
        else
        {
            while (more() && std::isdigit (static_cast<unsigned char> (here())))
                ++at;
        }

        if (take ('.'))
        {
            if (! more() || ! std::isdigit (static_cast<unsigned char> (here())))
                return false;

            while (more() && std::isdigit (static_cast<unsigned char> (here())))
                ++at;
        }

        if (more() && (here() == 'e' || here() == 'E'))
        {
            ++at;
            take ('+') || take ('-');

            if (! more() || ! std::isdigit (static_cast<unsigned char> (here())))
                return false;

            while (more() && std::isdigit (static_cast<unsigned char> (here())))
                ++at;
        }

        return at > from;
    }
};

/** True when @p json is one well-formed JSON value and nothing else. */
inline bool isWellFormedJson (std::string_view json)
{
    return JsonScanner (json).wholeValue();
}

} // namespace jazz::testing
