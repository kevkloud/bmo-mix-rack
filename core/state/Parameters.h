#pragma once

#include "ParamSpec.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace bmo
{

/** The JUCE range for a spec -- the one place a standalone parameter's range
    and a rack slot's are made, so the two cannot drift apart.

    A linear spec gets JUCE's own {min, max, step}, exactly as before, which
    is what every existing golden schema recorded. A logarithmic one gets
    JUCE's range with its mapping handed back to the spec's own arithmetic:
    ParamSpec is the only implementation of the log law, not one of two. */
inline juce::NormalisableRange<float> rangeFor (const ParamSpec& s)
{
    if (! s.logarithmic)
        return { s.min, s.max, s.step };

    const auto spec = s;   // copied: the lambdas outlive the call

    juce::NormalisableRange<float> range (
        s.min, s.max,
        [spec] (float, float, float n) { return spec.fromNormalised (n); },
        [spec] (float, float, float v) { return spec.toNormalised (v); },
        [spec] (float, float, float v) { return spec.clampReal (v); });

    range.interval = s.step;
    return range;
}

/** JUCE parameter objects from a spec list, for a standalone product.

    Types, names, ranges, steps and text functions have to come out exactly as
    the original plugins made them, because the golden schema tests compare
    against what those plugins reported and a host session references it.
*/
inline juce::AudioProcessorValueTreeState::ParameterLayout makeLayout (const ParamSpecs& specs,
                                                                       int versionHint)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& s : specs)
    {
        const juce::ParameterID id { s.id, versionHint };

        switch (s.kind)
        {
            case ParamKind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, s.name, s.def >= 0.5f));
                break;

            case ParamKind::Choice:
            {
                juce::StringArray choices;
                for (auto* c : s.choices)
                    choices.add (c);

                layout.add (std::make_unique<juce::AudioParameterChoice> (id, s.name, choices, (int) s.def));
                break;
            }

            case ParamKind::Float:
            {
                juce::AudioParameterFloatAttributes attr;
                const auto spec = s;   // copied into the lambda: specs() lists are static

                if (s.format != ParamFormat::Plain)
                    attr = attr.withLabel (s.label())
                               .withStringFromValueFunction ([spec] (float v, int)
                               {
                                   return juce::String (spec.text (v));
                               });

                // Only the formats that print a unit a number parse misreads
                // ("2.10 kHz") get a parser of their own; the rest keep JUCE's.
                if (s.format == ParamFormat::Hertz)
                    attr = attr.withValueFromStringFunction ([spec] (const juce::String& text)
                    {
                        return spec.valueFromText (text.toStdString());
                    });

                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, s.name, rangeFor (s), s.def, attr));
                break;
            }
        }
    }

    return layout;
}

/** The parameters an APVTS holds, in spec order. */
inline std::vector<juce::RangedAudioParameter*> collect (juce::AudioProcessorValueTreeState& apvts,
                                                         const ParamSpecs& specs)
{
    std::vector<juce::RangedAudioParameter*> out;

    for (const auto& s : specs)
    {
        auto* p = apvts.getParameter (s.id);
        jassert (p != nullptr);
        out.push_back (p);
    }

    return out;
}

} // namespace bmo
