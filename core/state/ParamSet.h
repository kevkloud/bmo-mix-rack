#pragma once

#include "ParamSpec.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace bmo
{

/** A module's parameters, whoever owns them.

    In a standalone product they are the plugin's own AudioParameterFloat /
    Bool / Choice objects inside an APVTS. In the rack they are the generic
    slot parameters, remapped to this module's specs. Panels, presets and the
    engine all work through this so neither knows which.
*/
class ParamSet
{
public:
    ParamSet (const ParamSpecs& specs, std::vector<juce::RangedAudioParameter*> params)
        : specList (specs), paramList (std::move (params))
    {
        jassert (specList.size() == paramList.size());
    }

    int size() const noexcept { return (int) paramList.size(); }

    /** The same specs, other parameter objects: a rack slot's engine that a
        chain edit moves to another slot reads that slot's lanes from then on.
        The caller copies the values across first and owns the threading --
        `RackProcessor::rebuild` holds the audio thread off these reads while
        it does this. */
    void rebind (std::vector<juce::RangedAudioParameter*> params)
    {
        jassert (params.size() == paramList.size());
        paramList = std::move (params);
    }

    const ParamSpecs& specs() const noexcept                 { return specList; }
    const ParamSpec& spec (int i) const noexcept             { return specList[(size_t) i]; }
    juce::RangedAudioParameter& param (int i) const noexcept { return *paramList[(size_t) i]; }

    int indexOf (const char* id) const noexcept { return indexOfParam (specList, id); }

    juce::RangedAudioParameter* find (const char* id) const noexcept
    {
        const auto i = indexOf (id);
        return i >= 0 ? paramList[(size_t) i] : nullptr;
    }

    const ParamSpec* findSpec (const char* id) const noexcept
    {
        const auto i = indexOf (id);
        return i >= 0 ? &specList[(size_t) i] : nullptr;
    }

    //== Values, in real units ================================================
    float getReal (int i) const noexcept
    {
        auto& p = param (i);
        return p.convertFrom0to1 (p.getValue());
    }

    void setReal (int i, float real) const
    {
        auto& p = param (i);
        p.setValueNotifyingHost (p.convertTo0to1 (real));
    }

    float getReal (const char* id) const noexcept
    {
        const auto i = indexOf (id);
        return i >= 0 ? getReal (i) : 0.0f;
    }

    void setReal (const char* id, float real) const
    {
        const auto i = indexOf (id);
        if (i >= 0) setReal (i, real);
    }

    /** Every value into an array in spec order, for the DSP. */
    void readAll (float* out) const noexcept
    {
        for (int i = 0; i < size(); ++i)
            out[i] = getReal (i);
    }

    void resetToDefaults() const
    {
        for (auto* p : paramList)
            p->setValueNotifyingHost (p->getDefaultValue());
    }

    void apply (const std::vector<Setting>& settings) const
    {
        for (const auto& s : settings)
            setReal (s.id, s.value);
    }

    //== State ================================================================
    // The same shape an APVTS writes -- <PARAMS stateVersion="1"><PARAM id=..
    // value=.. /> -- so a module's state in a rack preset, in a standalone
    // preset file and in a saved session all read identically. Values are in
    // real units; a parameter the file does not mention keeps its default.

    static constexpr auto kRootTag  = "PARAMS";
    static constexpr auto kParamTag = "PARAM";

    std::unique_ptr<juce::XmlElement> toXml (int stateVersion) const
    {
        auto xml = std::make_unique<juce::XmlElement> (kRootTag);
        xml->setAttribute ("stateVersion", stateVersion);

        for (int i = 0; i < size(); ++i)
        {
            auto* e = xml->createNewChildElement (kParamTag);
            e->setAttribute ("id", spec (i).id);
            e->setAttribute ("value", (double) getReal (i));
        }

        return xml;
    }

    /** Defaults first, then whatever the element carries. */
    void applyXml (const juce::XmlElement& xml) const
    {
        resetToDefaults();

        for (auto* e : xml.getChildWithTagNameIterator (kParamTag))
        {
            if (! e->hasAttribute ("id") || ! e->hasAttribute ("value"))
                continue;

            const auto i = indexOf (e->getStringAttribute ("id").toRawUTF8());

            if (i >= 0)
                setReal (i, (float) e->getDoubleAttribute ("value"));
        }
    }

private:
    const ParamSpecs& specList;
    std::vector<juce::RangedAudioParameter*> paramList;
};

} // namespace bmo
