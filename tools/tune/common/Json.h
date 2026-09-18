#pragma once

/*
    The smallest JSON the tools need: a flat object of string keys to
    numbers, booleans or strings, for parameter files --

        { "retune_ms": 0.4, "scale": "Major", "key": "A" }

    -- and a writer for flat scorecards. Nested values are rejected with a
    message rather than half-parsed; a parameter file is flat by design.
*/

#include <cctype>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace bmo::tune::json
{

struct Value
{
    enum class Kind { number, boolean, string } kind = Kind::number;
    double number = 0.0;
    bool boolean = false;
    std::string text;
};

using Object = std::map<std::string, Value>;

/** Parses a flat object. On failure returns false and says why in `error`. */
inline bool parseFlat (const std::string& src, Object& out, std::string& error)
{
    size_t i = 0;
    const auto skip = [&] { while (i < src.size() && std::isspace ((unsigned char) src[i])) ++i; };
    const auto fail = [&] (const char* what) { error = std::string (what) + " at offset " + std::to_string (i); return false; };

    const auto parseString = [&] (std::string& s) -> bool
    {
        if (i >= src.size() || src[i] != '"') return false;
        ++i;
        while (i < src.size() && src[i] != '"')
        {
            if (src[i] == '\\' && i + 1 < src.size()) ++i;
            s += src[i++];
        }
        if (i >= src.size()) return false;
        ++i;
        return true;
    };

    skip();
    if (i >= src.size() || src[i] != '{')
        return fail ("expected '{'");
    ++i;

    while (true)
    {
        skip();
        if (i < src.size() && src[i] == '}') { ++i; return true; }

        std::string key;
        if (! parseString (key)) return fail ("expected a key string");
        skip();
        if (i >= src.size() || src[i] != ':') return fail ("expected ':'");
        ++i;
        skip();

        Value v;
        if (i < src.size() && src[i] == '"')
        {
            v.kind = Value::Kind::string;
            if (! parseString (v.text)) return fail ("unterminated string");
        }
        else if (src.compare (i, 4, "true") == 0)  { v.kind = Value::Kind::boolean; v.boolean = true;  i += 4; }
        else if (src.compare (i, 5, "false") == 0) { v.kind = Value::Kind::boolean; v.boolean = false; i += 5; }
        else if (i < src.size() && (src[i] == '{' || src[i] == '['))
            return fail ("nested values are not supported in a flat parameter file");
        else
        {
            const auto start = i;
            while (i < src.size() && (std::isdigit ((unsigned char) src[i]) || src[i] == '-' || src[i] == '+'
                                      || src[i] == '.' || src[i] == 'e' || src[i] == 'E'))
                ++i;
            if (i == start) return fail ("expected a value");
            v.number = std::stod (src.substr (start, i - start));
        }

        out[key] = v;
        skip();
        if (i < src.size() && src[i] == ',') { ++i; continue; }
        skip();
        if (i < src.size() && src[i] == '}') { ++i; return true; }
        return fail ("expected ',' or '}'");
    }
}

/** A flat, ordered writer for scorecards. */
class Writer
{
public:
    void number (const std::string& key, double v)
    {
        char buf[64];
        std::snprintf (buf, sizeof buf, "%.9g", v);
        add (key, buf);
    }

    void text (const std::string& key, const std::string& v) { add (key, "\"" + v + "\""); }
    void boolean (const std::string& key, bool v) { add (key, v ? "true" : "false"); }

    std::string str() const
    {
        std::ostringstream s;
        s << "{\n";
        for (size_t i = 0; i < entries.size(); ++i)
            s << "  \"" << entries[i].first << "\": " << entries[i].second << (i + 1 < entries.size() ? ",\n" : "\n");
        s << "}\n";
        return s.str();
    }

private:
    void add (const std::string& k, const std::string& v) { entries.emplace_back (k, v); }
    std::vector<std::pair<std::string, std::string>> entries;
};

} // namespace bmo::tune::json
