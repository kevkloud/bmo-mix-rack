#pragma once

#include "SlotParameter.h"
#include <vector>

namespace bmo
{

/** A slot's parameters past the host grid: spec index 32 onwards.

    The rack shows a host a fixed 8 x 32 grid and cannot show it more. A
    plugin's parameter list is fixed once it is loaded, and growing the grid
    would move every lane a saved session already has. A module may still
    have more than 32 parameters. Its first 32 land on the slot's host lanes
    exactly as they always have; the rest live here, one SlotParameter each,
    and the panel, the DSP, presets and saved state reach them through the
    same ParamSet as the others. What they do not get is a host lane, so they
    cannot be automated in a rack. Standalone, every parameter a module has
    is a host parameter.

    This is an AudioProcessor only because a JUCE parameter has to belong to
    one: begin/endChangeGesture assert on a parameter with no index, and every
    knob drag calls them. Nothing hosts it, runs audio through it or asks it
    for state. It owns parameters and that is all it does.

    Built by RackProcessor::rebuild for a module arriving in a slot, new or
    moved there, since its parameters are named and numbered for the slot. It
    is retired with its engine when the module leaves the chain, or alone when
    the module moves on, and destroyed only after the engine and after the
    audio thread has finished with both.
*/
class SlotOverflow final : public juce::AudioProcessor
{
public:
    /** Holds `specs` from `firstIndex` on, assigned and at their defaults,
        each reporting to `listener` the way the slot's host lanes do. */
    SlotOverflow (int slot, const ParamSpecs& specs, int firstIndex, int versionHint,
                  juce::AudioProcessorParameter::Listener& l)
        : listener (l)
    {
        for (int p = firstIndex; p < (int) specs.size(); ++p)
        {
            auto param = std::make_unique<SlotParameter> (slot, p, versionHint);
            param->assign (&specs[(size_t) p]);
            param->addListener (&listener);
            held.push_back (param.get());
            addParameter (param.release());
        }
    }

    ~SlotOverflow() override
    {
        for (auto* p : held)
            p->removeListener (&listener);
    }

    /** In spec order, starting at `firstIndex`. */
    const std::vector<SlotParameter*>& parameters() const noexcept { return held; }

    //== Not a processor in any other sense ===================================
    const juce::String getName() const override                   { return {}; }
    void prepareToPlay (double, int) override                       {}
    void releaseResources() override                                {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override                    { return 0.0; }
    bool acceptsMidi() const override                               { return false; }
    bool producesMidi() const override                              { return false; }
    juce::AudioProcessorEditor* createEditor() override             { return nullptr; }
    bool hasEditor() const override                                 { return false; }
    int getNumPrograms() override                                   { return 1; }
    int getCurrentProgram() override                                { return 0; }
    void setCurrentProgram (int) override                           {}
    const juce::String getProgramName (int) override                { return {}; }
    void changeProgramName (int, const juce::String&) override      {}
    void getStateInformation (juce::MemoryBlock&) override          {}
    void setStateInformation (const void*, int) override            {}

private:
    juce::AudioProcessorParameter::Listener& listener;
    std::vector<SlotParameter*> held;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotOverflow)
};

} // namespace bmo
