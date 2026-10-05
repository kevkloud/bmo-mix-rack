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

/** A host value that is not a number is not a value (2026-10-04).

    The standalone products' parameters are the framework's own classes, which
    store what they are given: a NaN from a host sat in the parameter until
    something put it back, read "nan dB", and a state saved in that window
    left the parameter out. These subclasses refuse it where it arrives, on
    whatever thread that is: the framework calls `valueChanged` from inside
    `setValue`, after the store, so the hook puts back the value set
    immediately before, before `setValue` returns. An infinity is clamped to
    the rail it points at -- the framework already does that for a float and
    a choice, and the hook does it for a switch, which stores its argument as
    it is -- which is also what a rack lane does (`SlotParameter::setValue`).

    **What cannot be refused is the notification.** `setValueNotifyingHost`
    and `sendValueChangedMessageToListeners` are not virtual and hand every
    listener the argument, not the stored value, so a listener is still sent
    the NaN once. The owner is told (`takeRefused`) and sends the value that
    stands to every listener on the message thread straight after
    (`SingleModuleProcessor::handleAsyncUpdate`), so the last value each one
    holds is the right one. */
class HostValueGuard
{
public:
    virtual ~HostValueGuard() = default;

    /** True once since the last call if a NaN was refused. Any thread. */
    bool takeRefused() noexcept { return refused.exchange (false, std::memory_order_acq_rel); }

protected:
    void startGuarding (juce::AudioProcessorParameter& self) { kept.store (self.getValue(), std::memory_order_relaxed); }

    /** From the class's `valueChanged`, inside `setValue`. `isNaN` is read
        from the stored value; `normalised` is the stored value in 0..1. The
        nested `setValue` comes back through here with a finite value. */
    void guard (juce::AudioProcessorParameter& self, bool isNaN, float normalised)
    {
        if (isNaN)
        {
            refused.store (true, std::memory_order_release);
            self.setValue (kept.load (std::memory_order_relaxed));
            return;
        }

        if (normalised < 0.0f || normalised > 1.0f)
        {
            self.setValue (juce::jlimit (0.0f, 1.0f, normalised));
            return;
        }

        kept.store (normalised, std::memory_order_relaxed);
    }

private:
    std::atomic<float> kept { 0.0f };
    std::atomic<bool> refused { false };
};

class GuardedFloat final : public juce::AudioParameterFloat, public HostValueGuard
{
public:
    template <typename... Args>
    explicit GuardedFloat (Args&&... args) : juce::AudioParameterFloat (std::forward<Args> (args)...) { startGuarding (*this); }

private:
    void valueChanged (float real) override
    {
        juce::AudioProcessorParameter& self = *this;
        guard (self, std::isnan (real), std::isnan (real) ? 0.0f : self.getValue());
    }
};

class GuardedBool final : public juce::AudioParameterBool, public HostValueGuard
{
public:
    template <typename... Args>
    explicit GuardedBool (Args&&... args) : juce::AudioParameterBool (std::forward<Args> (args)...) { startGuarding (*this); }

private:
    void valueChanged (bool) override
    {
        juce::AudioProcessorParameter& self = *this;
        const auto stored = self.getValue();
        guard (self, std::isnan (stored), stored);
    }
};

class GuardedChoice final : public juce::AudioParameterChoice, public HostValueGuard
{
public:
    template <typename... Args>
    explicit GuardedChoice (Args&&... args) : juce::AudioParameterChoice (std::forward<Args> (args)...) { startGuarding (*this); }

private:
    void valueChanged (int) override
    {
        juce::AudioProcessorParameter& self = *this;
        const auto stored = self.getValue();
        guard (self, std::isnan (stored), stored);
    }
};

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
                layout.add (std::make_unique<GuardedBool> (id, s.name, s.def >= 0.5f));
                break;

            case ParamKind::Choice:
            {
                juce::StringArray choices;
                for (auto* c : s.choices)
                    choices.add (c);

                layout.add (std::make_unique<GuardedChoice> (id, s.name, choices, (int) s.def));
                break;
            }

            case ParamKind::Float:
            {
                juce::AudioParameterFloatAttributes attr;
                const auto spec = s;   // copied into the lambda: specs() lists are static

                // A spec that prints itself takes the same route a formatted
                // one does, so the host's lane, the panel's value line and a
                // rack slot all read what ParamSpec::text says. Plain's label
                // is the empty string, which is what JUCE would have shown
                // anyway, so nothing a formatted parameter reports changes.
                if (s.format != ParamFormat::Plain || s.textFn != nullptr)
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

                layout.add (std::make_unique<GuardedFloat> (
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
