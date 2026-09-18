#pragma once

#include "core/state/ParamSpec.h"
#include "core/state/Parameters.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace bmo
{

/** One of the rack's generic host parameters: slotN_pMM.

    The rack exposes a fixed 8 x 32 grid of these, because a host's automation
    lanes are bound to a parameter list that cannot change after the plugin
    is loaded. Which module is in a slot can change, so each of its parameters
    is remapped live: the range, the name, the text and the step count all
    come from whatever ParamSpec is assigned. Unassigned, it is a plain 0..1
    called "Slot N P MM" that does nothing.

    The same class holds a module's parameters past the 32nd, in a
    SlotOverflow that is never shown to the host. Those have an id like
    slot1_p33, which is unique but belongs to no lane.

    The value stored is normalised, as the host sees it. The mapping between
    that and a real unit is the spec's own arithmetic (ParamSpec::toNormalised
    and back), which the rack tests pin to what juce::NormalisableRange gives
    for the same range -- otherwise a preset saved standalone would read
    differently in a slot.
*/
class SlotParameter final : public juce::RangedAudioParameter
{
public:
    SlotParameter (int slot, int index, int versionHint)
        : juce::RangedAudioParameter (juce::ParameterID (idFor (slot, index), versionHint),
                                      defaultNameFor (slot, index)),
          slotNumber (slot), paramIndex (index),
          defaultName (defaultNameFor (slot, index))
    {
    }

    static juce::String idFor (int slot, int index)
    {
        return "slot" + juce::String (slot + 1) + "_p" + juce::String (index + 1).paddedLeft ('0', 2);
    }

    static juce::String defaultNameFor (int slot, int index)
    {
        return "Slot " + juce::String (slot + 1) + " P" + juce::String (index + 1).paddedLeft ('0', 2);
    }

    int getSlot() const noexcept  { return slotNumber; }
    int getIndex() const noexcept { return paramIndex; }

    //== Assignment ===========================================================
    // Message thread, under the rack's chain lock. The value is reset to the
    // spec's default: a module arriving in a slot starts from Init, never
    // from whatever the previous module left in the lane.

    void assign (const ParamSpec* s)
    {
        spec.store (s, std::memory_order_release);

        range = s != nullptr ? rangeFor (*s) : juce::NormalisableRange<float> (0.0f, 1.0f);

        value.store (getDefaultValue(), std::memory_order_relaxed);
    }

    const ParamSpec* getSpec() const noexcept { return spec.load (std::memory_order_acquire); }
    bool isAssigned() const noexcept          { return getSpec() != nullptr; }

    //== RangedAudioParameter =================================================
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }

    float getValue() const override { return value.load (std::memory_order_relaxed); }

    void setValue (float newValue) override
    {
        value.store (juce::jlimit (0.0f, 1.0f, newValue), std::memory_order_relaxed);
    }

    float getDefaultValue() const override
    {
        if (auto* s = getSpec())
            return s->toNormalised (s->def);

        return 0.0f;
    }

    juce::String getName (int maximumStringLength) const override
    {
        if (auto* s = getSpec())
            return limit (juce::String (slotNumber + 1) + ": " + s->name, maximumStringLength);

        return limit (defaultName, maximumStringLength);
    }

    juce::String getLabel() const override
    {
        if (auto* s = getSpec())
            return s->label();

        return {};
    }

    int getNumSteps() const override
    {
        if (auto* s = getSpec())
        {
            if (s->kind == ParamKind::Bool)   return 2;
            if (s->kind == ParamKind::Choice) return s->numChoices();
        }

        return juce::AudioProcessor::getDefaultNumParameterSteps();
    }

    bool isDiscrete() const override
    {
        auto* s = getSpec();
        return s != nullptr && s->kind != ParamKind::Float;
    }

    bool isBoolean() const override
    {
        auto* s = getSpec();
        return s != nullptr && s->kind == ParamKind::Bool;
    }

    bool isAutomatable() const override { return true; }

    juce::String getText (float normalised, int maximumStringLength) const override
    {
        if (auto* s = getSpec())
            return limit (s->text (s->fromNormalised (normalised)), maximumStringLength);

        return juce::String (normalised, 3);
    }

    float getValueForText (const juce::String& text) const override
    {
        auto* s = getSpec();

        if (s == nullptr)
            return juce::jlimit (0.0f, 1.0f, text.getFloatValue());

        if (s->kind == ParamKind::Choice)
        {
            for (int i = 0; i < s->numChoices(); ++i)
                if (text.trim().equalsIgnoreCase (s->choices[(size_t) i]))
                    return s->toNormalised ((float) i);
        }

        if (s->kind == ParamKind::Bool)
            return text.trim().equalsIgnoreCase ("on") || text.getIntValue() != 0 ? 1.0f : 0.0f;

        // "L 50" / "R 50" for pan; anything else is the number in front.
        auto t = text.trim();

        if (s->format == ParamFormat::Pan)
        {
            if (t.equalsIgnoreCase ("C")) return s->toNormalised (0.0f);
            const auto side = t.startsWithIgnoreCase ("L") ? -1.0f : 1.0f;
            return s->toNormalised (side * t.retainCharacters ("0123456789.").getFloatValue());
        }

        if (s->format == ParamFormat::Hertz)
            return s->toNormalised (s->valueFromText (t.toStdString()));

        return s->toNormalised (t.getFloatValue());
    }

private:
    /** JUCE's convention: a maximum of 0 means no limit. */
    static juce::String limit (const juce::String& text, int maximumLength)
    {
        return maximumLength > 0 ? text.substring (0, maximumLength) : text;
    }

    const int slotNumber, paramIndex;
    const juce::String defaultName;

    std::atomic<float> value { 0.0f };
    std::atomic<const ParamSpec*> spec { nullptr };
    juce::NormalisableRange<float> range { 0.0f, 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotParameter)
};

} // namespace bmo
