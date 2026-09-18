#pragma once

/*
    Parameters for the offline tools, by the same IDs and in the same real
    units a host sees: `retune_ms=0.4`, `scale=Major`, `key=Bb`. Values go
    through ParamSpec::clampReal, so a tool can never set something a host
    could not.
*/

#include "modules/tune/dsp/TuneCore.h"
#include "tools/tune/common/Json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace bmo::tune::tools
{

inline std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

/** Every parameter at its default, in spec order. */
inline std::vector<float> defaultValues()
{
    std::vector<float> v;
    for (const auto& s : specs())
        v.push_back (s.def);
    return v;
}

/** Sets one parameter from text. Choices take their name or their index;
    switches take on/off/true/false/1/0. Returns false with a reason. */
inline bool setFromText (std::vector<float>& values, const std::string& id, const std::string& text, std::string& error)
{
    const auto& all = specs();
    const auto index = indexOfParam (all, id.c_str());

    if (index < 0)
    {
        error = "unknown parameter '" + id + "'";
        return false;
    }

    const auto& spec = all[(size_t) index];
    const auto t = lower (text);

    if (spec.kind == ParamKind::Choice)
    {
        for (int c = 0; c < spec.numChoices(); ++c)
            if (lower (spec.choices[(size_t) c]) == t)
            {
                values[(size_t) index] = (float) c;
                return true;
            }

        // A choice list whose names are numbers with a unit -- Retune
        // Speed's "0.4 ms", "12 ms" -- takes the number: retune_ms=12 is
        // 12 ms. Never an index there, or retune_ms=12 would quietly be step
        // 12, which is 1.2 ms.
        if (spec.numChoices() > 0 && std::isdigit ((unsigned char) spec.choices[0][0]))
        {
            try
            {
                size_t used = 0;
                const auto v = std::stod (text, &used);
                for (int c = 0; c < spec.numChoices(); ++c)
                    if (used == text.size() && std::abs (std::stod (spec.choices[(size_t) c]) - v) < 1.0e-6)
                    {
                        values[(size_t) index] = (float) c;
                        return true;
                    }
            }
            catch (...) {}

            error = "'" + text + "' is not one of " + id + "'s steps (" + spec.choices.front()
                    + " ... " + spec.choices.back() + ")";
            return false;
        }
    }

    if (spec.kind == ParamKind::Bool)
    {
        if (t == "on" || t == "true")  { values[(size_t) index] = 1.0f; return true; }
        if (t == "off" || t == "false") { values[(size_t) index] = 0.0f; return true; }
    }

    try
    {
        size_t used = 0;
        const auto v = std::stod (text, &used);
        if (used != text.size())
            throw std::invalid_argument ("trailing");
        values[(size_t) index] = spec.clampReal ((float) v);
        return true;
    }
    catch (...)
    {
        error = "bad value '" + text + "' for " + id;
        return false;
    }
}

inline bool loadJson (const std::string& path, std::vector<float>& values, std::string& error)
{
    std::ifstream f (path);
    if (! f)
    {
        error = "cannot open " + path;
        return false;
    }

    std::stringstream ss;
    ss << f.rdbuf();

    json::Object obj;
    if (! json::parseFlat (ss.str(), obj, error))
    {
        error = path + ": " + error;
        return false;
    }

    for (const auto& [key, value] : obj)
    {
        std::string text;
        switch (value.kind)
        {
            case json::Value::Kind::string:  text = value.text; break;
            case json::Value::Kind::boolean: text = value.boolean ? "on" : "off"; break;
            case json::Value::Kind::number:
            {
                std::ostringstream n;
                n.precision (17);
                n << value.number;
                text = n.str();
                break;
            }
        }

        if (! setFromText (values, key, text, error))
            return false;
    }

    return true;
}

/** "id=value" */
inline bool applyAssignment (std::vector<float>& values, const std::string& assignment, std::string& error)
{
    const auto eq = assignment.find ('=');
    if (eq == std::string::npos)
    {
        error = "expected id=value, got '" + assignment + "'";
        return false;
    }

    return setFromText (values, assignment.substr (0, eq), assignment.substr (eq + 1), error);
}

inline TuneParams toParams (const std::vector<float>& values)
{
    return TuneParams::fromValues (values.data(), (int) values.size());
}

} // namespace bmo::tune::tools
